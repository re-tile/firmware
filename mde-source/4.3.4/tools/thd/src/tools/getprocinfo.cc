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
// getprocinfo.cc -- utility to collect process/thread data
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// C/C++ includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// custom includes
#include "TaskInfo.h"
#include "collections.h"
#include "utils.h"
#include "string_utils.h"
#include "Pathname.h"
#include "cgibin.h"

/** maximum length of a process name */
#define MAX_PROC_NAME_LEN 128


// -----------------------------------------------------------------------------
// globals
// -----------------------------------------------------------------------------

/** names of ubiquitous OS processes we should ignore */
Array<std::string> g_process_filter_list;

/**
// TODO: make this selectable by a command-line option
const char* g_ignore_procs[] = {
  // processes to ignore when running on TILE
  "init",
  "kworker/",
  "migration/",
  "khungtaskd",
  "khvcd",
  "ksoftirqd/",
  "kswapd0",
  "kswapd1",
  "kswapd2",
  "kswapd3",
  "kswapd4",
  "kswapd5",
  "kthreadd",
  "khubd",
  "kintegrityd",
  "kblockd",
  "khelper",
  "aio",
  "aio/",
  "bdi-default",
  "busybox",
  "cpuset",
  "crypto",
  "dropbear",
  "edac-poller",
  "fsnotify_mark",
  "md",
  "netns",
  "portmap",
  "scsi_eh_",
  "sync_supers",
  "tempmond",
  "udhcpc",
  "watchdog/",
  "xfs_mru_cache",
  "xfsconvertd",
  "xfsdatad",
  "xfslogd",

  // additional processes to ignore when running on x86 host
#if !defined(__tile__)
  "kblockd/",
  "kauditd",
  "kswapd",
  "kthread",
  "kacpid",
  "kseriod",
  "kpsmoused",
  "kstriped",
  "kjournald",
  "kgameportd",
  "kmpathd/",
  "Xorg",
  "acpid",
  "ata/",
  "ata_",
  "atd",
  "audispd",
  "auditd",
  "avahi-",
  "bonobo-",
  "bt-applet",
  "clock-applet",
  "cqueue/",
  "crond",
  "cups-",
  "cupsd",
  "dbus-",
  "dhclient",
  "eggcups",
  "escd",
  "events/",
  "gam_server",
  "gconfd-2",
  "gdm-binary",
  "gdm-rh-",
  "gnome-",
  "gpm",
  "hal",
  "hald",
  "hald-",
  "hcid",
  "hidd",
  "hp",
  "hpiod",
  "http",
  "ipp",
  "irqbalance",
  "klogd",
  "kmpath_",
  "krfcommd",
  "lockd",
  "lpd",
  "lpinfo",
  "mapping-daemon",
  "metacity",
  "mingetty",
  "mixer_",
  "mpt_",
  "nautilus",
  "nfsd",
  "nfsd4",
  "nm-",
  "nmbd",
  "notification-ar",
  "ntpd",
  "pam-",
  "pam_",
  "parallel",
  "pcscd",
  "pdflush",
  "rpc.",
  "rpciod/",
  "run-mozilla.sh",
  "rwhod",
  "sdpd",
  "serial",
  "smartd",
  "smb",
  "smbd",
  "snmp",
  "socket",
  "ssh-agent",
  "sshd",
  "syslogd",
  "tpvmlp",
  "udevd",
  "uuidd",
  "vmmemctl",
  "vmtoolsd",
  "vmware-",
  "wnck-",
  "xfs",
  "xinetd",
#endif
  NULL
};
*/

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
  FILE* fp2;

  // Print CGI-BIN header.
  CGI_BIN_HEADER_TEXT();

  // Print version info before opening any files.
  P0("procinfo 1.0");
  fflush(stdout);

  // Map from IDs to tasks (processes/threads).
  Map<int, TaskInfo*> processes;

  // Get process name filter list, if any, from file.
  // We assume the current directory is the docroot directory.
  Pathname process_filter_list_file("views/process_filter_list");
  if ((fp = fopen(process_filter_list_file, "r")) != NULL)
  {
    std::string process_name;
    while(readline(fp, process_name) >= 0)
    {
      if (! process_name.empty() &&
          ! starts_with("//", process_name))
      {
        g_process_filter_list.add(process_name);
      }
    }

    fclose(fp);
  }

  // collect process data from /proc/nnn/stat files
  Pathname proc_dir("/proc");
  Array<Pathname> proc_dir_files;
  proc_dir.directory_list(proc_dir_files);

  FOR_EACH(const_iterator, it, Array<Pathname>, proc_dir_files)
  {
    // look for /proc/nnn directories, which represent "process" tasks
    const Pathname& proc_pid_dir = *it;
    if (! is_all_digits(proc_pid_dir)) continue;

    // check /proc/nnn/stat file for process info
    Pathname proc_stat_path = proc_dir + proc_pid_dir + "stat";
    if ((fp = fopen(proc_stat_path, "r")) != NULL)
    {
      // read single line from file
      fgets(line, sizeof(line), fp);
      fclose(fp);

      // parse it for values we need to capture
      int pid, ppid, cpu;
      long int utime, stime;
      char pname[MAX_PROC_NAME_LEN];
      // man 5 proc describes the format of /proc/PID/stat
      // pid command state ppid pgrp session tty ttypgid kflags minflt
      // cminflt majflt cmajflt utime stime cutime cstime priority nice nthreads
      // itreal starttime vsize rss rlim startcode endcode startstack kstkesp kstkeip
      // sigpend sigblocked sigignored sigcatch wchan nswap cnswap exit_sig processor rt_priority
      // policy ? ? ?
      sscanf(line, "%i (%[^)]) %*s %i %*s %*s %*s %*s %*s %*s "
                   "%*s %*s %*s %li %li %*s %*s %*s %*s %*s "
                   "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s "
                   "%*s %*s %*s %*s %*s %*s %*s %*s %i %*s "
                   "%*s %*s %*s %*s", &pid, pname, &ppid, &utime, &stime, &cpu);

      // Sanity check.
      if (cpu < 0 || cpu > 65536) continue;

      // Check whether this is a system/daemon process.
      bool system_process = false;
      /*
      for (int i=0; g_ignore_procs[i] != NULL; ++i)
      */
      FOR_EACH(const_iterator, it, Array<std::string>, g_process_filter_list)
      {
        const std::string& process_name = *it;

        // Handle some common prefixes for per-cpu processes.
        if (ends_with("/", process_name) ||
            ends_with("_", process_name) ||
            ends_with(".", process_name) ||
            ends_with("-", process_name))
        {
          if (starts_with(process_name, pname))
          {
            system_process = true;
            break;
          }
        }
        // Otherwise match exact names.
        else {
          if (pname == process_name)
          {
            system_process = true;
            break;
          }
        }
      }

      if (system_process) continue;

      // create process TaskInfo object
      TaskInfo* process = new TaskInfo(pid, pname);
      process->set_parent_pid(ppid);
      process->set_cpu(cpu);
      process->set_user_time(utime);
      process->set_system_time(stime);

      // add it to the list of process objects
      processes.put(pid, process);


      // look for /proc/nnn/task/nnn files, which represent "thread" tasks
      Pathname task_dir = proc_dir + proc_pid_dir + "task";

      Array<Pathname> task_dir_files;
      task_dir.directory_list(task_dir_files);

      FOR_EACH(const_iterator, it2, Array<Pathname>, task_dir_files)
      {
        // look for /proc/nnn/task/nnn directories, which represent "process" tasks
        const Pathname& task_pid_dir = *it2;

        if (! is_all_digits(task_pid_dir)) continue;

        // TODO: do we really need to create a separate "thread" entry
        // for the "process" thread, i.e. the task with pid == tid?
        // For now, we'll go ahead and create one anyway, since it's then easy
        // to just process the list of threads in order to see everything.

        // check /proc/nnn/task/nnn/stat file for thread info
        Pathname task_stat_path = proc_dir + proc_pid_dir + "task" + task_pid_dir + "stat";
        if ((fp2 = fopen(task_stat_path, "r")) != NULL)
        {
          // read single line from file
          fgets(line, sizeof(line), fp2);
          fclose(fp2);

          // parse it for values we need to capture
          int tid, tcpu;
          long int tutime, tstime;
          char tname[MAX_PROC_NAME_LEN];
          // man 5 proc describes the format of /proc/PID/stat
          // pid command state ppid pgrp session tty ttypgid kflags minflt
          // cminflt majflt cmajflt utime stime cutime cstime priority nice nthreads
          // itreal starttime vsize rss rlim startcode endcode startstack kstkesp kstkeip
          // sigpend sigblocked sigignored sigcatch wchan nswap cnswap exit_sig processor rt_priority
          // policy ? ? ?
          sscanf(line, "%i (%[^)]) %*s %*s %*s %*s %*s %*s %*s %*s "
                       "%*s %*s %*s %li %li %*s %*s %*s %*s %*s "
                       "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s "
                       "%*s %*s %*s %*s %*s %*s %*s %*s %i %*s "
                       "%*s %*s %*s %*s", &tid, tname, &tutime, &tstime, &tcpu);

          // Sanity check.
          if (tcpu < 0 || tcpu > 65536) continue;

          // create process TaskInfo object
          TaskInfo* thread = new TaskInfo(pid, tid, tname);
          thread->set_parent_pid(pid);
          thread->set_cpu(tcpu);
          thread->set_user_time(tutime);
          thread->set_system_time(tstime);

          // add it to process's list of process objects
          process->add_thread(thread);
        }
      }
    }
  }

  FOR_EACH_PAIR(iterator, it, Map<int COMMA TaskInfo*>, processes)
  {
    TaskInfo* process = it->second;

    int pid = process->get_pid();
    int ppid = process->get_parent_pid();
    int cpu = process->get_cpu();
    const std::string& pname = process->get_name();
    long int utime = process->get_user_time();
    long int stime = process->get_system_time();
    long int ttime = utime + stime;

    P2("process %i %s", pid, pname.c_str());
    if (ppid > 0) P1("ppid %i", ppid);
    P1("cpu %i", cpu);
    if (ttime > 0) {
      P1("user_time %li", utime);
      P1("system_time %li", stime);
      P1("total_time %li", ttime);
    }

    FOR_EACH(const_iterator, it2, Array<TaskInfo*>, process->get_threads())
    {
      const TaskInfo* thread = *it2;
      int tid = thread->get_tid();
      int tcpu = thread->get_cpu();
      const std::string& tname = thread->get_name();
      long int tutime = thread->get_user_time();
      long int tstime = thread->get_system_time();
      long int tttime = tutime + tstime;
      
      P2("thread %i %s", tid, tname.c_str());
      P1("cpu %i", tcpu);
      if (tttime > 0)
      {
        P1("user_time %li", tutime);
        P1("system_time %li", tstime);
        P1("total_time %li", tttime);
      }
    }
  }

  FOR_EACH_PAIR(iterator, it, Map<int COMMA TaskInfo*>, processes)
  {
    TaskInfo* process = it->second;

    FOR_EACH(iterator, it2, Array<TaskInfo*>, process->get_threads())
    {
      delete *it2;
    }

    process->remove_all_threads();
    delete process;
  }
  processes.clear();

  return status;
}
