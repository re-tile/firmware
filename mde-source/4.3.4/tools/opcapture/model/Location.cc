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
// Location.cc -- Location class
// ============================================================================

#include "Location.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// Location
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Location::Location(int binary_id,
                   const std::string& address,
                   int symbol_id,
                   const std::string& source_file,
                   int line) :
  m_id(-1),
  m_binary_id(binary_id),
  m_address(address),
  m_symbol_id(symbol_id),
  m_source_file(source_file),
  m_source_line(line)
{
}

/** Copy constructor. */
Location::Location(const Location& location) :
  m_id(location.m_id),
  m_binary_id(location.m_binary_id),
  m_address(location.m_address),
  m_symbol_id(location.m_symbol_id),
  m_source_file(location.m_source_file),
  m_source_line(location.m_source_line)
{
}

/** Copy constructor. */
Location::Location(const Location* location) :
  m_id(location->m_id),
  m_binary_id(location->m_binary_id),
  m_address(location->m_address),
  m_symbol_id(location->m_symbol_id),
  m_source_file(location->m_source_file),
  m_source_line(location->m_source_line)
{
}

/** Assignment operator. */
const Location&
Location::operator=(const Location& location)
{
  if (&location != this) // self-assignment guard
  {
    m_id           = location.m_id;
    m_binary_id    = location.m_binary_id;
    m_address      = location.m_address;
    m_symbol_id    = location.m_symbol_id;
    m_source_file  = location.m_source_file;
    m_source_line  = location.m_source_line;
  }
  return *this;
}

/** Assignment operator. */
const Location&
Location::operator=(const Location* location)
{
  return operator=(*location);
}

/** Destructor. */
Location::~Location()
{}


// --- object methods ---

/** Equality test. */
bool
Location::equals(const Location& location) const
{
  return(
    m_id           == location.m_id &&
    m_binary_id    == location.m_binary_id &&
    m_address      == location.m_address &&
    m_symbol_id    == location.m_symbol_id &&
    m_source_file  == location.m_source_file &&
    m_source_line  == location.m_source_line
  );
}

/** Equality operator. */
bool
Location::operator==(const Location& location) const
{
  return equals(location);
}


// --- accessors ---



// --- methods ---

