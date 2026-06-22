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
// Binary.h -- Binary class
// ============================================================================

// multiple-inclusion guard
#ifndef BINARY_H
#define BINARY_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH
#include "BinarySymbol.h"       // BinarySymbol


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class Binary;

/** Array of binary instances. */
typedef Array<Binary> BinaryArray;


// ----------------------------------------------------------------------------
// Binary
// ----------------------------------------------------------------------------

/** Binary class. */
class Binary
{
  // --- members ---
protected:
  /** Binary id. */
  int m_id;

  /** Binary type. */
  std::string m_type;
  
  /** Binary pathname. */
  std::string m_pathname;

  /** Symbols for this binary. */
  Array<BinarySymbol*> m_symbols;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Binary(const std::string& type,
         const std::string& pathname);

  /** Copy constructor. */
  Binary(const Binary& binary);

  /** Copy constructor. */
  Binary(const Binary* binary);

  /** Assignment operator. */
  const Binary& operator=(const Binary& binary);

  /** Assignment operator. */
  const Binary& operator=(const Binary* binary);

  /** Destructor. */
  ~Binary();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const Binary& binary) const;

  /** Equality operator. */
  bool operator==(const Binary& binary) const;


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


  /** Gets binary type. */
  const std::string
  get_type() const
  {
    return m_type;
  }

  /** Gets binary pathname. */
  const std::string
  get_pathname() const
  {
    return m_pathname;
  }


  // --- symbol management ---
public:

  /** Adds symbol for this binary. */
  void
  add_symbol(BinarySymbol* binarySymbol)
  {
    m_symbols.add(binarySymbol);
  }

  /** Gets symbols for this binary. */
  Array<BinarySymbol*>&
  get_symbols()
  {
    return m_symbols;
  }


  // --- methods ---
public:



};

// multiple-inclusion guard
#endif
