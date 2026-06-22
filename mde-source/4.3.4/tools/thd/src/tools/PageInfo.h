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
// PageInfo.h -- Memory page info object
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef PAGE_INFO_H
#define PAGE_INFO_H

// C++ includes.
#include <string>        // std::string

// custom includes
#include "collections.h" // Array, FOR_EACH


// -----------------------------------------------------------------------------
// PageInfo
// -----------------------------------------------------------------------------

/** Memory page info class. */
class PageInfo
{
  // --- members ---
protected:

  /** Virtual address */
  long m_virtual_address;

  /** Physical address */
  long m_physical_address;

  /** Access permissions string ("rwx{p|s}") */
  std::string m_access;

  /** Memory controller number */
  int m_controller;

  /** Properties */
  Map<std::string, std::string> m_properties;


  // --- constructors/destructors ---
public:
  /** Constructor */
  PageInfo(long virtual_addr, long physical_addr, std::string access,
           int controller, std::string prop_string);

  /** Destructor */
  ~PageInfo();


  // --- accessors --
public:
  /** Gets virtual address. */
  long
  get_virtual_address() const;

  /** Gets physical address. */
  long
  get_physical_address() const;

  /** Gets access permissions string. */
  const std::string&
  get_access() const;

  /** Gets memory controller number. */
  int
  get_controller();

  /** Get properties list. */
  const Map<std::string, std::string>&
  get_properties();


  // --- methods ---
public:

  /** Prints page info object to stream */
  void print(FILE* stream);

};

// Multiple-inclusion guard.
#endif
