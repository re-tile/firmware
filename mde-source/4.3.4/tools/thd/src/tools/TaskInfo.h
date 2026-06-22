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

// ============================================================================
// TaskInfo.h -- Task (process/thread) info class
// Copyright (C) 2010. Tilera Corporation
// ============================================================================

// Multiple-inclusion guard.
#ifndef TASKINFO_H
#define TASKINFO_H

// C++ includes.
#include <string>        // std::string

// custom includes
#include "collections.h" // Array, FOR_EACH


// -----------------------------------------------------------------------------
// TaskInfo
// -----------------------------------------------------------------------------

/** Task (process/thread) info class. */
class TaskInfo
{
  // --- members ---
protected:
  /** Linux process ID. */
  int m_pid;

  /** Linux thread ID (for a process, this is same as pid). */
  int m_tid;

  /** Linux parent process ID. */
  int m_ppid;

  /** Process name. */
  std::string m_name;

  /** Current cpu for this process. */
  int m_cpu;

  /** Current user time for this process. */
  long int m_utime;

  /** Current system time for this process. */
  long int m_stime;

  /** List of associated thread tasks, if any. */
  Array<TaskInfo*> m_threads;


  // --- constructors/destructors ---
public:
  /** Constructor */
  TaskInfo(int pid, const std::string& name);

  /** Constructor */
  TaskInfo(int pid, int tid, const std::string& name);

  /** Destructor */
  ~TaskInfo();


  // --- accessors --
public:
  /** Gets Linux process ID. */
  int
  get_pid() const;

  /** Gets Linux thread ID (for a process, this is same as pid). */
  int
  get_tid() const;

  /** Gets Linux parent process ID. */
  int
  get_parent_pid() const;

  /** Sets Linux parent process ID. */
  void
  set_parent_pid(int ppid);

  /** Gets process name. */
  const std::string&
  get_name() const;

  /** Sets process name. */
  void
  set_name(const std::string& name);

  /** Gets current cpu for this process. */
  int
  get_cpu() const;

  /** Sets current cpu for this process. */
  void
  set_cpu(int cpu);


  /** Gets user time for this process. */
  long int
  get_user_time() const;

  /** Sets user time for this process. */
  void
  set_user_time(long int utime);

  /** Gets system time for this process. */
  long int
  get_system_time() const;

  /** Sets system time for this process. */
  void
  set_system_time(long int stime);


  // --- methods ---
public:

  /** Adds a child thread. */
  void
  add_thread(TaskInfo* thread);

  /** Removes a child thread. */
  void
  remove_thread(TaskInfo* thread);

  /** Removes all child threads. */
  void
  remove_all_threads();

  /** Gets list of child threads. */
  const Array<TaskInfo*>&
  get_threads() const;

  /** Gets list of child threads. */
  Array<TaskInfo*>&
  get_threads();
};

// Multiple-inclusion guard.
#endif
