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
// CPUInfo.cc -- CPU info class
// Copyright (C) 2010. Tilera Corporation
// ============================================================================

#include "CPUInfo.h"


// -----------------------------------------------------------------------------
// CPUInfo
// -----------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
CPUInfo::CPUInfo(int id) :
  m_id(id),
  m_is_dedicated(false),
  m_is_default_shared(false),
  m_is_dataplane(false),
  m_is_network_cpu(false),
  m_user_time(0),
  m_system_time(0),
  m_total_time(0),
  m_idle_time(0)
{}

/** Destructor */
CPUInfo::~CPUInfo()
{}


// --- accessors ---

/** Sets whether this is a dedicated tile for a device. */
void
CPUInfo::set_dedicated(bool flag)
{
  m_is_dedicated = flag;
}

/** Gets whether this is a dedicated tile for a device. */
bool
CPUInfo::is_dedicated() const
{
  return m_is_dedicated;
}


/** Sets device this tile is associated with. */
void
CPUInfo::set_device_name(const std::string& device_name)
{
  m_device_name = device_name;
}

/** Gets device this tile is associated with. */
const std::string&
CPUInfo::get_device_name() const
{
  return m_device_name;
}


/** Sets whether this is a default shared tile. */
void
CPUInfo::set_default_shared(bool flag)
{
  m_is_default_shared = flag;
}

/** Gets whether this is a default shared tile. */
bool
CPUInfo::is_default_shared() const
{
  return m_is_default_shared;
}


/** Sets whether this is a dataplane tile. */
void
CPUInfo::set_dataplane(bool flag)
{
  m_is_dataplane = flag;
}

/** Gets whether this is a dataplane tile. */
bool
CPUInfo::is_dataplane() const
{
  return m_is_dataplane;
}


/** Sets whether this is a network-cpu tile. */
void
CPUInfo::set_network_cpu(bool flag)
{
  m_is_network_cpu = flag;
}

/** Gets whether this is a network-cpu tile. */
bool
CPUInfo::is_network_cpu() const
{
  return m_is_network_cpu;
}


/** Sets user time. */
void
CPUInfo::set_user_time(long value)
{
  m_user_time = value;
}

/** Gets user time. */
long
CPUInfo::get_user_time() const
{
  return m_user_time;
}


/** Sets system time. */
void
CPUInfo::set_system_time(long value)
{
  m_system_time = value;
}

/** Gets system time. */
long
CPUInfo::get_system_time() const
{
  return m_system_time;
}


/** Sets total time (user + system). */
void
CPUInfo::set_total_time(long value)
{
  m_total_time = value;
}

/** Gets total time. */
long
CPUInfo::get_total_time() const
{
  return m_total_time;
}


/** Sets idle time. */
void
CPUInfo::set_idle_time(long value)
{
  m_idle_time = value;
}

/** Gets idle time. */
long
CPUInfo::get_idle_time() const
{
  return m_idle_time;
}


// --- methods ---

/** Adds task to task list */
void
CPUInfo::add(TaskInfo* task)
{
  if (task != NULL)
  {
    m_tasks.add(task);
  }
}

/** Gets list of tasks for this CPU/tile. */
const Array<TaskInfo*>
CPUInfo::get_tasks() const
{
  return m_tasks;
}
