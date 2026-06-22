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
// Task.cc -- Task class
// ============================================================================

#include "Task.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Task::Task(int pid, int binary_id) :
  m_id(-1),
  m_pid(pid),
  m_tid(pid), // not a typo, tid == pid in this case
  m_binary_id(binary_id)
{
}

/** Constructor. */
Task::Task(int pid, int tid, int binary_id) :
  m_id(-1),
  m_pid(pid),
  m_tid(tid),
  m_binary_id(binary_id)
{
}

/** Copy constructor. */
Task::Task(const Task& task) :
  m_id(task.m_id),
  m_pid(task.m_pid),
  m_tid(task.m_tid),
  m_binary_id(task.m_binary_id)
{
}

/** Copy constructor. */
Task::Task(const Task* task) :
  m_id(task->m_id),
  m_pid(task->m_pid),
  m_tid(task->m_tid),
  m_binary_id(task->m_binary_id)
{
}

/** Assignment operator. */
const Task&
Task::operator=(const Task& task)
{
  if (&task != this) // self-assignment guard
  {
    m_id        = task.m_id;
    m_pid       = task.m_pid;
    m_tid       = task.m_tid;
    m_binary_id = task.m_binary_id;
  }
  return *this;
}

/** Assignment operator. */
const Task&
Task::operator=(const Task* task)
{
  return operator=(*task);
}

/** Destructor. */
Task::~Task()
{}


// --- object methods ---

/** Equality test. */
bool
Task::equals(const Task& task) const
{
  return(
    m_id        == task.m_id &&
    m_pid       == task.m_pid &&
    m_tid       == task.m_tid &&
    m_binary_id == task.m_binary_id
  );
}

/** Equality operator. */
bool
Task::operator==(const Task& task) const
{
  return equals(task);
}


// --- accessors ---



// --- methods ---

