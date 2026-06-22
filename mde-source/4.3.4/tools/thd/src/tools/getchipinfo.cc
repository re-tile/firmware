// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

// =============================================================================
// getchipinfo.cc -- utility to collect chip data from /proc filesystem
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// custom includes
#include "CPUInfo.h"
#include "utils.h"
#include "string_utils.h"
#include "cgibin.h"

#define MAX_LINE_LEN 512


// -----------------------------------------------------------------------------
// main()
// -----------------------------------------------------------------------------

/** main function */
int main(int argc, char** argv)
{
  int status = 0;

  char line[MAX_LINE_LEN+1];
  FILE* fp;

  // Print CGI-BIN header.
  CGI_BIN_HEADER_TEXT();

  // Print version info before opening any files.
  P0("chipinfo 1.0");
  fflush(stdout);

  // Get chip type from /proc/cpuinfo.
  char chiptype[512];
  strcpy(chiptype, "unknown");
  if ((fp = fopen("/proc/cpuinfo", "r")) != NULL)
  {
    while((fgets(line, sizeof(line), fp)) > 0)
    {
      if (starts_with("model name", line))
      {
        int start = index_of_first(":", line);
        if (start >= 0)
        {
          start++;
          while (line[start] == ' ' ||
                 line[start] == '\t')
            ++start;
          sscanf(line + start, "%[^\n]", chiptype);
        }
      }
    }
  }

  // Get chip size from /sys/devices/system/cpu/chip_width/height
  int columns = 6, rows=6;

  if ((fp = fopen("/sys/devices/system/cpu/chip_width", "r")) != NULL)
  {
    if (fgets(line, sizeof(line), fp) > 0) {
      sscanf(line, "%i", &columns);
    }
    fclose(fp);
  }
  if ((fp = fopen("/sys/devices/system/cpu/chip_height", "r")) != NULL)
  {
    if (fgets(line, sizeof(line), fp) > 0) {
      sscanf(line, "%i", &rows);
    }
    fclose(fp);
  }

  // TODO: need to pick up chip shims from somewhere

  // allocate space for cpu info
  int ncpus = columns * rows;
  Array<CPUInfo*> cpus;
  for (int i=0; i<ncpus; ++i) {
    cpus.add(new CPUInfo(i));
  }

  // collect info about tiles from hvconfig file
  // TODO: this file's format is subject to change,
  // so we need something more permanent to base this on
  if ((fp = fopen("/sys/hypervisor/hvconfig", "r")) != NULL)
  {
    std::string current_device = "";
    while((fgets(line, sizeof(line), fp)) > 0)
    {
      char* after;

      // default_shared=x,y
      if ((after = find_substring_after("default_shared=", line)) != NULL)
      {
        int x=-1, y=-1;
        sscanf(after, "%i,%i", &x, &y);
        if (x>-1 && y>-1)
        {
          int cpu = y*columns+x;
          cpus[cpu]->set_default_shared(true);
          current_device = "";
        }
      }

      // device name
      else if (starts_with("device ", line))
      {
        char device_name[32];
        sscanf(line + 7, "%[^ \n]", device_name);
        current_device = device_name;
      }

      // "dedicated" sub argument of device
      else if ((after = find_substring_after("dedicated", line)) != NULL)
      {
        char *end = after + strlen(after);
	for( ; after < end ; after += 4) {
          int x=-1, y=-1;
          sscanf(after, " %i,%i", &x, &y);
          if (x>-1 && y>-1) {
            int cpu = y*columns+x;
            cpus[cpu]->set_dedicated(true);
            cpus[cpu]->set_device_name(current_device);
          }
        }
      }

      // look for "network_cpus" in "args" sub argument to linux client
      // (watch out, it also appears in the summary line of config file options)
      // TODO: make this work if there are multiple Linux clients
      // (maybe we just collect the list of network_cpus for all clients?)
      else if ((after = find_substring_after("  args", line)) != NULL &&
               (after = find_substring_after("network_cpus=", line)) != NULL)
      {
	// parse list of network cpu(s)
	// this can be single cpu ids ("n"), or ranges ("lo-hi")
	FOR_EACH_TOKEN(t, after, ",")
	{
	  int from_cpu = -1, to_cpu = -1;
	  if (strchr(t, '-') != NULL) {
	    // range of ids, lo-hi
	    sscanf(t, "%i-%i", &from_cpu, &to_cpu);
	  }
	  else {
	    // single cpu id
	    sscanf(t, "%i", &from_cpu);
	    // pretend this is a trivial one-cpu range
	    to_cpu = from_cpu;
	  }
	  if (from_cpu >= 0 && to_cpu >=0 && from_cpu <= to_cpu) {
	    for (int i=from_cpu; i<=to_cpu; i++) {
	      cpus[i]->set_network_cpu(true);
	    }
	  }
	}
      }
    }

    fclose(fp);
  }

  // collect info about dataplane tiles from dataplane file
  if ((fp = fopen("/sys/devices/system/cpu/dataplane", "r")) != NULL)
  {
    fgets(line, sizeof(line), fp);
    fclose(fp);

    // parse list of network cpu(s)
    // this can be single cpu ids ("n"), or ranges ("lo-hi")
    FOR_EACH_TOKEN(t, line, ",")
    {
      int from_cpu = -1, to_cpu = -1;
      if (strchr(t, '-') != NULL) {
        // range of ids, lo-hi
        sscanf(t, "%i-%i", &from_cpu, &to_cpu);
      }
      else {
        // single cpu id
        sscanf(t, "%i", &from_cpu);
        // pretend this is a trivial one-cpu range
        to_cpu = from_cpu;
      }
      if (from_cpu >= 0 && to_cpu >=0 && from_cpu <= to_cpu) {
        for (int i=from_cpu; i<=to_cpu; i++) {
          cpus[i]->set_dataplane(true);
        }
      }
    }
  }

  // get total cpu times from /proc/stat
  if ((fp = fopen("/proc/stat", "r")) != NULL)
  {
    while((fgets(line, sizeof(line), fp)) > 0)
    {
      // parse lines that start with "cpuNN"
      if (starts_with("cpu", line) && line[3] >= '0' && line[4] <= '9') {
        long cpu = -1;
        long user   = 0;
        long nice = 0;
        long system = 0;
        long idle   = 0;
        sscanf(line, "cpu%li %li %li %li %li", &cpu, &user, &nice, &system, &idle);
        if (cpu >= 0) {
          cpus[cpu]->set_user_time(user + nice);
          cpus[cpu]->set_system_time(system);
          cpus[cpu]->set_total_time(user + nice + system);
          cpus[cpu]->set_idle_time(idle);
        }
      }
    }

    fclose(fp);
  }

  // generate the collected data in an easily-parsable format

  // Chip type.
  P1("chip %s", chiptype);

  // grid size before CPUs, so consumer can allocate an array
  P2("grid %i %i", columns, rows);

  // TODO: add chip/shim info, when we have it

  // iterate over cpus
  for (int i=0; i<ncpus; ++i) {

    P1("cpu %i", i);

    bool dedicated                 = cpus[i]->is_dedicated();
    if (dedicated)
    {
      const std::string& device_name = cpus[i]->get_device_name();
      P1("dedicated %s", device_name.c_str());
    }

    bool default_shared = cpus[i]->is_default_shared();
    if (default_shared)
      P0("default_shared 1");

    bool dataplane      = cpus[i]->is_dataplane();
    if (dataplane)
      P0("dataplane 1");

    bool network_cpu    = cpus[i]->is_network_cpu();
    if (network_cpu)
      P0("network_cpu 1");

    long user           = cpus[i]->get_user_time();
    long system         = cpus[i]->get_system_time();
    long idle           = cpus[i]->get_idle_time();
    long total          = cpus[i]->get_total_time();

    P1("user_time %li",   user);
    P1("system_time %li", system);
    P1("total_time %li",  total);
    P1("idle_time %li",   idle);
  }

  // clean up allocated storage
  FOR_EACH(iterator, it, Array<CPUInfo*>, cpus)
  {
    delete *it;
  }
  cpus.clear();

  return status;
}
