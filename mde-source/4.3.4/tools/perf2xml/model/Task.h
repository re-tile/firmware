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
// Task.h -- Linux task (process/thread) class
// ============================================================================

// multiple-inclusion guard
#ifndef TASK_H
#define TASK_H

// C/C++ includes
#include <string>     // std::string

// custom includes
#include "Pathname.h" // Unix/Linux pathnames
#include "Frame.h"    // Frame class


// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------

/** Represents a single Linux "task" (process/thread). */
class Task
{
  // --- members ---
protected:
  /** Unique profile ID.
      NOTE: this is distinct from the Linux task id,
      which is accessible via the tid member. */
  int m_id;
  
  /** Profile "session" this process comes from. */
  int m_session_id;

  /** Linux process ID. */
  int m_pid;

  /** Linux thread ID (also the Linux task ID). */
  int m_tid;

  /** Pathname of module. */
  Pathname m_pathname;

  /** Set of cpu ids. */
  Set<int> m_cpus;

  /** List of root stack frame(s). */
  Array<Frame*> m_frames;


  // --- constructors/destructors ---
public:
  Task(int id, int session_id,
       int pid, int tid,
       const Pathname& pathname);

  Task(int id, int session_id,
       int cpu, int pid, int tid,
       const Pathname& pathname);

protected:
  /** Copy constructor */
  Task(const Task& obj);

  /** Assignment operator */
  Task& operator=(const Task& obj);

public:
  ~Task();


  // --- accessors ---
public:
  /** Gets unique profile ID of this task.
      NOTE: this is distinct from the Linux task id,
      which is accessible via the tid member. */
  int
  get_id() const
  {
    return m_id;
  }
  
  /** Gets profile "session" this process comes from. */
  int
  get_session_id() const
  {
    return m_session_id;
  }

  /** Gets linux "process" ID (Linux task id of process task). */
  int
  get_pid() const
  {
    return m_pid;
  }

  /** Gets linux "thread" ID (also the Linux task id). */
  int
  get_tid() const
  {
    return m_tid;
  }

  /** Gets pathname of module for this process. */
  const Pathname&
  get_pathname() const
  {
    return m_pathname;
  }

  /** Gets pathname of module for this process. */
  void
  set_pathname(const Pathname& pathname)
  {
    m_pathname = pathname;
  }


  // --- methods ---
public:
  /** Adds cpu id. */
  void
  add_cpu(int cpu);

  /** Gets set of cpu ids. */
  const Set<int>&
  get_cpus();

  /** Adds root stack frame. */
  void
  add_frame(Frame* frame);

  /** Gets list of root stack frames. */
  Array<Frame*>&
  get_frames();

  /** Gets list of root stack frames. */
  const Array<Frame*>&
  get_frames() const;

};

// multiple-inclusion guard
#endif


