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
// Binary.cc -- Binary class
// ============================================================================

#include "Binary.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// Binary
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Binary::Binary(const std::string& type,
               const std::string& pathname) :
  m_id(-1),
  m_type(type),
  m_pathname(pathname)
{
}

/** Copy constructor. */
Binary::Binary(const Binary& binary) :
  m_id(binary.m_id),
  m_type(binary.m_type),
  m_pathname(binary.m_pathname)
{
}

/** Copy constructor. */
Binary::Binary(const Binary* binary) :
  m_id(binary->m_id),
  m_type(binary->m_type),
  m_pathname(binary->m_pathname)
{
}

/** Assignment operator. */
const Binary&
Binary::operator=(const Binary& binary)
{
  if (&binary != this) // self-assignment guard
  {
    m_id       = binary.m_id;
    m_type     = binary.m_type;
    m_pathname = binary.m_pathname;
  }
  return *this;
}

/** Assignment operator. */
const Binary&
Binary::operator=(const Binary* binary)
{
  return operator=(*binary);
}

/** Destructor. */
Binary::~Binary()
{}


// --- object methods ---

/** Equality test. */
bool
Binary::equals(const Binary& binary) const
{
  return(
    m_id       == binary.m_id &&
    m_type     == binary.m_type &&
    m_pathname == binary.m_pathname
  );
}

/** Equality operator. */
bool
Binary::operator==(const Binary& binary) const
{
  return equals(binary);
}


// --- accessors ---



// --- methods ---

