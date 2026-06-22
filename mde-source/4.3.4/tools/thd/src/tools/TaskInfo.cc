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
// TaskInfo.cc -- Task (process/thread) info class
// Copyright (C) 2010. Tilera Corporation
// ============================================================================

#include "TaskInfo.h"

// C++ includes.

// custom includes


// -----------------------------------------------------------------------------
// TaskInfo
// -----------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
TaskInfo::TaskInfo(int pid, const std::string& name) :
  m_pid(pid),
  m_tid(pid),
  m_name(name)
{
}

/** Constructor */
TaskInfo::TaskInfo(int pid, int tid, const std::string& name) :
  m_pid(pid),
  m_tid(tid),
  m_name(name)
{
}

/** Destructor */
TaskInfo::~TaskInfo()
{
}


// --- accessors --

/** Gets Linux process ID. */
int
TaskInfo::get_pid() const
{
  return m_pid;
}

/** Gets Linux thread ID (for a process, this is same as pid). */
int
TaskInfo::get_tid() const
{
  return m_tid;
}

/** Gets Linux parent process ID. */
int
TaskInfo::get_parent_pid() const
{
  return m_ppid;
}

/** Sets Linux parent process ID. */
void
TaskInfo::set_parent_pid(int ppid)
{
  m_ppid = ppid;
}

/** Gets process name. */
const std::string&
TaskInfo::get_name() const
{
  return m_name;
}

/** Sets process name. */
void
TaskInfo::set_name(const std::string& name)
{
  m_name = name;
}

/** Gets current cpu for this process. */
int
TaskInfo::get_cpu() const
{
  return m_cpu;
}

/** Sets current cpu for this process. */
void
TaskInfo::set_cpu(int cpu)
{
  m_cpu = cpu;
}

/** Gets user time for this process. */
long int
TaskInfo::get_user_time() const
{
  return m_utime;
}

/** Sets user time for this process. */
void
TaskInfo::set_user_time(long int utime)
{
  m_utime = utime;
}

/** Gets system time for this process. */
long int
TaskInfo::get_system_time() const
{
  return m_stime;
}

/** Sets system time for this process. */
void
TaskInfo::set_system_time(long int stime)
{
  m_stime = stime;
}


// --- methods ---

/** Adds a child thread. */
void
TaskInfo::add_thread(TaskInfo* thread)
{
  if (thread != NULL)
    m_threads.add(thread);
}

/** Removes a child thread. */
void
TaskInfo::remove_thread(TaskInfo* thread)
{
  if (thread != NULL)
    m_threads.remove(thread);
}

/** Removes all child threads. */
void
TaskInfo::remove_all_threads()
{
  m_threads.clear();
}

/** Gets list of child threads. */
const Array<TaskInfo*>&
TaskInfo::get_threads() const
{
  return m_threads;
}

/** Gets list of child threads. */
Array<TaskInfo*>&
TaskInfo::get_threads()
{
  return m_threads;
}
