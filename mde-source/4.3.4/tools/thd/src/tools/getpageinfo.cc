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
// getpageinfo.c -- utility to collect memory page info from /proc filesystem
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// C/C++ includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// custom includes
#include "MemoryInfo.h"
#include "PageInfo.h"
#include "utils.h"
#include "string_utils.h"
#include "io_utils.h"
#include "cgibin.h"


// -----------------------------------------------------------------------------
// main()
// -----------------------------------------------------------------------------

/** main function */
int main(int argc, char** argv)
{
  int status = 0;

  std::string line;
  FILE* fp;

  // Print CGI-BIN header.
  CGI_BIN_HEADER_TEXT();

  // Print version info before opening any files.
  P0("pageinfo 1.0");
  fflush(stdout);

  Array<MemoryInfo*> mappings;
  Array<PageInfo*> pages;

  // create a context we can break out of
  do {
    // get proc pid/tid paths
    int pid = 0;
    int tid = 0;
    if (argc > 1)
    {
      pid = atoi(argv[1]);
      tid = pid;
    }
    if (argc > 2)
    {
      tid = atoi(argv[2]);
    }

    Pathname proc_dir = "/proc";
    Pathname pid_dir = proc_dir + to_string(pid);
    Pathname tid_dir = proc_dir + to_string(pid) + "task" + to_string(tid);

    // get memory mapping info
    Pathname maps_path = tid_dir + "maps";
    if (maps_path.exists())
    {
      fp = fopen(maps_path, "r");
      if (fp != NULL)
      {
	while(readline(fp, line) > 0)
	{
	  long long int first_page_addr = 0;
	  long long int last_page_addr = 0;
	  char perms[5];
	  perms[0] = '\0';
	  unsigned int offset = 0;
	  char pathname[MAX_PATH_LEN];
	  pathname[0] = '\0';

	  sscanf(line.c_str(), "%llx-%llx %4s %x %*s %*s %s",
		 &first_page_addr,
		 &last_page_addr,
		 perms,
		 &offset,
		 pathname);

	  MemoryInfo* info =
            new MemoryInfo(first_page_addr,
                           (last_page_addr - first_page_addr + 1),
                           perms, pathname, offset);

          mappings.add(info);
	}
	fclose(fp);
      }
    }

    // get page table info
    Pathname pgtable_path = pid_dir + "pgtable";
    if (pgtable_path.exists())
    {
      fp = fopen(pgtable_path, "r");
      if (fp != NULL)
      {
	while(readline(fp, line) > 0)
	{
	  long long int virtual_addr = 0;
	  long long int physical_addr = 0;
	  char perms[4];
	  perms[0] = '\0';
	  int controller = 0;
	  const char* props = line.c_str() + 32;

	  sscanf(line.c_str(), "%llx %3s PA=%llx (N%1i)",
		 &virtual_addr,
		 perms,
		 &physical_addr,
		 &controller);

          PageInfo* info =
            new PageInfo(virtual_addr, physical_addr,
                         perms, controller, props);

          pages.add(info);
	}
	fclose(fp);
      }
    }

  }
  while (false);

  // display memory mappings
  P0("");
  P0("Mappings:");
  FOR_EACH(const_iterator, it, Array<MemoryInfo*>, mappings)
  {
    MemoryInfo* mapping = *it;

    mapping->print(stdout);
    printf("\n");
  }

  // clean up
  FOR_EACH(const_iterator, it, Array<MemoryInfo*>, mappings)
  {
    MemoryInfo* mapping = *it;
    delete mapping;
  }
  mappings.clear();

  FOR_EACH(const_iterator, it, Array<PageInfo*>, pages)
  {
    PageInfo* page = *it;
    delete page;
  }
  pages.clear();

  return status;
}
