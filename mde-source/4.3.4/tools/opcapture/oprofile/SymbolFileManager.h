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

// ==========================================================================
// SymbolFileManager.h -- symbol file management
// ==========================================================================

// multiple-inclusion guard
#ifndef SYMBOL_FILE_MANAGER_H
#define SYMBOL_FILE_MANAGER_H

// custom includes
#include "collections.h"    // collections, FOR_EACH
#include "Pathname.h"       // Unix pathnames
#include "SymbolFile.h"     // symbol file (executable or library)


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

/** mapping from target pathnames to local executables. */
typedef Map<Pathname, SymbolFilePtr> SymbolFileMap;


// -------------------------------------------------------------------------
// SymbolFileManager
// -------------------------------------------------------------------------

/** Manager for symbol files, both paths and actual binary files */
class SymbolFileManager
{
  // --- members ---
private:
  /** cache of symbol files we've loaded */
  SymbolFileMap     m_symbol_file_map;


  // --- constructors/destructors ---
public:
  /** Constructor */
  SymbolFileManager();


  // --- accessors ---
public:  
  /** Gets number of symbol files cached. */
  int
  size() const
  {
    return m_symbol_file_map.size();
  }

  /** Gets map from pathnames to symbol files. */
  const SymbolFileMap&
  get_map() const
  {
    return m_symbol_file_map;
  }


  // --- methods ---
public:  
  /** Gets symbol file for specified path.
      Caches previously located symbol files so subsequent requests
      for same path return same symbol file.
      Returns NULL if symbol file could not be loaded. */
  SymbolFilePtr
  get_symbol_file(const Pathname& pathname);

};

// multiple-inclusion guard
#endif


