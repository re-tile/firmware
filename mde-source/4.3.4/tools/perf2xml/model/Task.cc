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
// Task.cc -- Linux task (process/thread) class
// ============================================================================

#include "Task.h"


// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
Task::Task(int id, int session_id,
     int pid, int tid,
     const Pathname& pathname) :
  m_id(id),
  m_session_id(session_id),
  m_pid(pid),
  m_tid(tid),
  m_pathname(pathname)
{}

/** Constructor. */
Task::Task(int id, int session_id,
     int cpu, int pid, int tid,
     const Pathname& pathname) :
  m_id(id),
  m_session_id(session_id),
  m_pid(pid),
  m_tid(tid),
  m_pathname(pathname)
{
  add_cpu(cpu);
}

/** Copy constructor */
Task::Task(const Task& obj)
{}

/** Assignment operator */
Task&
Task::operator=(const Task& obj)
{
  if (this != &obj) // handle self-assignment
  {
  }
  return *this;
}

Task::~Task()
{
  FOR_EACH(iterator, it, Array<Frame*>, m_frames)
  {
    Frame* f = *it;
    delete f;
  }
  m_frames.clear();

  m_cpus.clear();
}


// --- accessors ---


// --- methods ---

/** Adds cpu id. */
void
Task::add_cpu(int cpu)
{
  m_cpus.add(cpu);
}

/** Gets set of cpu ids. */
const Set<int>&
Task::get_cpus()
{
  return m_cpus;
}

/** Adds root stack frame. */
void
Task::add_frame(Frame* frame)
{
  m_frames.add(frame);
}

/** Gets list of root stack frames. */
Array<Frame*>&
Task::get_frames()
{
  return m_frames;
}

/** Gets list of root stack frames. */
const Array<Frame*>&
Task::get_frames() const
{
  return m_frames;
}
