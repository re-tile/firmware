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
// BinarySymbol.h -- BinarySymbol class
// ============================================================================

// multiple-inclusion guard
#ifndef BINARYSYMBOL_H
#define BINARYSYMBOL_H

// C/C++ includes
#include <string>               // std::string

// OProfile includes
// libutil++
#include <op_bfd.h>             // bfd_vma

// custom includes
#include "collections.h"        // collections, FOR_EACH


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class BinarySymbol;

/** Array of binary instances. */
typedef Array<BinarySymbol> BinarySymbolArray;


// ----------------------------------------------------------------------------
// BinarySymbol
// ----------------------------------------------------------------------------

/** BinarySymbol class. */
class BinarySymbol
{
  // --- members ---
protected:
  /** BinarySymbol id. */
  int m_id;

  /** BinarySymbol name. */
  std::string m_name;

  /** BinarySymbol start address. */
  bfd_vma m_start_address;

  /** BinarySymbol size. */
  bfd_vma m_size;

  /** BinarySymbol end address. */
  bfd_vma m_end_address;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  BinarySymbol(const std::string& name,
               bfd_vma start_address,
               bfd_vma size,
               bfd_vma end_address);

  /** Copy constructor. */
  BinarySymbol(const BinarySymbol& binarySymbol);

  /** Copy constructor. */
  BinarySymbol(const BinarySymbol* binarySymbol);

  /** Assignment operator. */
  const BinarySymbol& operator=(const BinarySymbol& binarySymbol);

  /** Assignment operator. */
  const BinarySymbol& operator=(const BinarySymbol* binarySymbol);

  /** Destructor. */
  ~BinarySymbol();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const BinarySymbol& binarySymbol) const;

  /** Equality operator. */
  bool operator==(const BinarySymbol& binarySymbol) const;


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


  /** Gets binary symbol name. */
  const std::string
  get_name() const
  {
    return m_name;
  }

  /** Gets binary symbol start address. */
  const bfd_vma
  get_start_address() const
  {
    return m_start_address;
  }

  /** Gets binary symbol size. */
  const bfd_vma
  get_size() const
  {
    return m_size;
  }

  /** Gets binary symbol end address. */
  const bfd_vma
  get_end_address() const
  {
    return m_end_address;
  }
  

  // --- methods ---
public:


};

// multiple-inclusion guard
#endif
