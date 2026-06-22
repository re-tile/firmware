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
// CPUInfo.h -- CPU (aka tile) info class
// Copyright (C) 2010. Tilera Corporation
// ============================================================================

// Multiple-inclusion guard.
#ifndef CPUINFO_H
#define CPUINFO_H

// C++ includes.
#include <string>        // std::string

// custom includes
#include "TaskInfo.h"    // process/thread info objects
#include "collections.h" // Array, FOR_EACH


// -----------------------------------------------------------------------------
// CPUInfo
// -----------------------------------------------------------------------------

/** CPU (aka tile) info class. */
class CPUInfo
{
  // --- members ---
protected:
  /** CPU ID */
  int m_id;

  /** Whether this is a dedicated tile. */
  bool m_is_dedicated;

  /** Device tile is associated with, if any. */
  std::string m_device_name;

  /** Whether this is a default_shared tile. */
  bool m_is_default_shared;

  /** Whether this is a dataplane tile. */
  bool m_is_dataplane;

  /** Whether this is a network cpu tile. */
  bool m_is_network_cpu;

  /** Total user-space time for processes on this tile. */
  long m_user_time;

  /** Total kernel-space time for processes on this tile. */
  long m_system_time;

  /** Total non-idle time (user + system)
      for processes on this tile. */
  long m_total_time;

  /** Total idle time for processes on this tile. */
  long m_idle_time;

  /** List of TaskInfo objects representing tasks on this tile. */
  Array<TaskInfo*> m_tasks;


  // --- constructors/destructors ---
public:
  /** Constructor */
  CPUInfo(int id);

  /** Destructor */
  ~CPUInfo();


  // --- accessors ---
public:
  /** Sets whether this is a dedicated tile for a device. */
  void
  set_dedicated(bool flag);

  /** Gets whether this is a dedicated tile for a device. */
  bool
  is_dedicated() const;


  /** Sets device this tile is associated with. */
  void
  set_device_name(const std::string& device_name);

  /** Gets device this tile is associated with. */
  const std::string&
  get_device_name() const;


  /** Sets whether this is a default shared tile. */
  void
  set_default_shared(bool flag);

  /** Gets whether this is a default shared tile. */
  bool
  is_default_shared() const;


  /** Sets whether this is a dataplane tile. */
  void
  set_dataplane(bool flag);

  /** Gets whether this is a dataplane tile. */
  bool
  is_dataplane() const;


  /** Sets whether this is a network-cpu tile. */
  void
  set_network_cpu(bool flag);

  /** Gets whether this is a network-cpu tile. */
  bool
  is_network_cpu() const;


  /** Sets user time. */
  void
  set_user_time(long value);

  /** Gets user time. */
  long
  get_user_time() const;


  /** Sets system time. */
  void
  set_system_time(long value);

  /** Gets system time. */
  long
  get_system_time() const;


  /** Sets total time (user + system). */
  void
  set_total_time(long value);

  /** Gets total time. */
  long
  get_total_time() const;


  /** Sets idle time. */
  void
  set_idle_time(long value);

  /** Gets idle time. */
  long
  get_idle_time() const;


  // --- methods ---
public:
  /** Adds task to task list for this CPU/tile */
  void
  add(TaskInfo* task);

  /** Gets list of tasks for this CPU/tile. */
  const Array<TaskInfo*>
  get_tasks() const;

};

// Multiple-inclusion guard.
#endif
