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
// Task.h -- Task class
// ============================================================================

// multiple-inclusion guard
#ifndef TASK_H
#define TASK_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class Task;

/** Array of task instances. */
typedef Array<Task> TaskArray;


// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------

/** Task class. */
class Task
{
  // --- members ---
protected:
  /** Task id. */
  int m_id;

  /** Linux process id. */
  int m_pid;

  /** Linux thread id. */
  int m_tid;

  /** Binary id. */
  int m_binary_id;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Task(int pid, int binary_id);

  /** Constructor. */
  Task(int pid, int tid, int binary_id);

  /** Copy constructor. */
  Task(const Task& task);

  /** Copy constructor. */
  Task(const Task* task);

  /** Assignment operator. */
  const Task& operator=(const Task& task);

  /** Assignment operator. */
  const Task& operator=(const Task* task);

  /** Destructor. */
  ~Task();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const Task& task) const;

  /** Equality operator. */
  bool operator==(const Task& task) const;


  // --- accessors ---
public:
  /** Gets id. */
  int
  get_id() const
  {
    return m_id;
  }
  /** Sets id. */
  void
  set_id(int id)
  {
    m_id = id;
  }


  /** Gets process id. */
  int
  get_pid() const
  {
    return m_pid;
  }

  /** Gets thread id. */
  int
  get_tid() const
  {
    return m_tid;
  }

  /** Gets binary id. */
  int
  get_binary_id() const
  {
    return m_binary_id;
  }


  // --- methods ---
public:


};

// multiple-inclusion guard
#endif
