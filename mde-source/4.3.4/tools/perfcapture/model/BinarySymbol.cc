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
// BinarySymbol.cc -- BinarySymbol class
// ============================================================================

#include "BinarySymbol.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// BinarySymbol
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
BinarySymbol::BinarySymbol(const std::string& name,
                           perf_vma start_address,
                           perf_vma size,
                           perf_vma end_address) :
  m_id(-1),
  m_name(name),
  m_start_address(start_address),
  m_size(size),
  m_end_address(end_address)
{
}

/** Copy constructor. */
BinarySymbol::BinarySymbol(const BinarySymbol& binarySymbol) :
  m_id(binarySymbol.m_id),
  m_name(binarySymbol.m_name),
  m_start_address(binarySymbol.m_start_address),
  m_size(binarySymbol.m_size),
  m_end_address(binarySymbol.m_end_address)
{
}

/** Copy constructor. */
BinarySymbol::BinarySymbol(const BinarySymbol* binarySymbol) :
  m_id(binarySymbol->m_id),
  m_name(binarySymbol->m_name),
  m_start_address(binarySymbol->m_start_address),
  m_size(binarySymbol->m_size),
  m_end_address(binarySymbol->m_end_address)
{
}

/** Assignment operator. */
const BinarySymbol&
BinarySymbol::operator=(const BinarySymbol& binarySymbol)
{
  if (&binarySymbol != this) // self-assignment guard
  {
    m_id            = binarySymbol.m_id;
    m_name          = binarySymbol.m_name;
    m_start_address = binarySymbol.m_start_address;
    m_size          = binarySymbol.m_size;
    m_end_address   = binarySymbol.m_end_address;
  }
  return *this;
}

/** Assignment operator. */
const BinarySymbol&
BinarySymbol::operator=(const BinarySymbol* binarySymbol)
{
  return operator=(*binarySymbol);
}

/** Destructor. */
BinarySymbol::~BinarySymbol()
{}


// --- object methods ---

/** Equality test. */
bool
BinarySymbol::equals(const BinarySymbol& binarySymbol) const
{
  return(
    m_id            == binarySymbol.m_id &&
    m_name          == binarySymbol.m_name &&
    m_start_address == binarySymbol.m_start_address &&
    m_size          == binarySymbol.m_size &&
    m_end_address   == binarySymbol.m_end_address
  );
}

/** Equality operator. */
bool
BinarySymbol::operator==(const BinarySymbol& binarySymbol) const
{
  return equals(binarySymbol);
}


// --- accessors ---



// --- methods ---

