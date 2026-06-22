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
// MemoryInfo.h -- Memory region info object
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H

// C++ includes.
#include <string>        // std::string

// custom includes
#include "collections.h" // Array, FOR_EACH
#include "Pathname.h"    // Pathname


// -----------------------------------------------------------------------------
// MemoryInfo
// -----------------------------------------------------------------------------

/** Memory info class. */
class MemoryInfo
{
  // --- members ---
protected:
  /** Start address */
  long long m_address;

  /** Size in bytes*/
  long long m_size;

  /** Access permissions string ("rwx{p|s}") */
  std::string m_access;

  /** Pathname of binary file, if any */
  Pathname m_pathname;

  /** Offset into binary file */
  long long m_offset;


  // --- constructors/destructors ---
public:
  /** Constructor */
  MemoryInfo(long long address,
             long long size,
             const std::string& access,
             const Pathname& pathname,
             long long offset);

  /** Destructor */
  ~MemoryInfo();


  // --- accessors ---
public:

  /** Gets start address. */
  long long
  get_address() const;

  /** Gets size in bytes. */
  long long
  get_size() const;

  /** Gets access permissions. */
  const std::string&
  get_access() const;

  /** Gets pathname of binary file, if any. */
  const Pathname&
  get_pathname() const;

  /** Gets offset into binary file, if any. */
  long long
  get_offset() const;


  // --- methods ---
public:

  /** Prints memory info object to stream */
  void print(FILE* stream);

};

// Multiple-inclusion guard.
#endif
