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
// PageInfo.cc -- Process/thread info object
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "PageInfo.h"

// C/C++ includes
#include <stdio.h> // fprintf

// custom includes
#include "string_utils.h"


// -----------------------------------------------------------------------------
// PageInfo
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
PageInfo::PageInfo(long virtual_addr, long physical_addr, std::string access,
                   int controller, std::string prop_string) :
  m_virtual_address(virtual_addr),
  m_physical_address(physical_addr),
  m_access(access),
  m_controller(controller)
{
  FOR_EACH_TOKEN(tok, prop_string.c_str(), " ")
  {
    std::string name;
    std::string value;
    if (split_string("=", tok, name, value))
    {
      m_properties.put(name, value);
    }
    else {
      m_properties.put(name, "");
    }
  }
}

/** Destructor */
PageInfo::~PageInfo()
{
  m_properties.clear();
}


// --- accessors --

/** Gets virtual address. */
long
PageInfo::get_virtual_address() const
{
  return m_virtual_address;
}

/** Gets physical address. */
long
PageInfo::get_physical_address() const
{
  return m_physical_address;
}

/** Gets access permissions string. */
const std::string&
PageInfo::get_access() const
{
  return m_access;
}

/** Gets memory controller number. */
int
PageInfo::get_controller()
{
  return m_controller;
}

/** Get properties list. */
const Map<std::string, std::string>&
PageInfo::get_properties()
{
  return m_properties;
}


// --- methods ---

/** Prints memory info object to stream */
void
PageInfo::print(FILE* stream)
{
  fprintf(stream, "V %08lx = P %08lx %s %i",
	  m_virtual_address,
	  m_physical_address,
          m_access.c_str(),
          m_controller);

  FOR_EACH_PAIR(const_iterator, it, Map<std::string COMMA std::string>, m_properties)
  {
    const std::string& name  = it->first;
    const std::string& value = it->second;
    if (! value.empty())
      printf(" %s=%s", name.c_str(), value.c_str());
    else
      printf(" %s", name.c_str());
  }
}
