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
#include "Pathname.h"       // Unix pathnames
#include "Map.h"            // map/multimap classes
#include "SharedPointer.h"  // shared pointers
#include "SymbolFile.h"     // symbol file (executable or library)


extern int
get_install_path(char* buf, size_t size, const char* tail);


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

// mapping from target pathnames to local executables
typedef Map<Pathname, Pathname>      SymbolFilePathMap;
typedef Map<Pathname, SymbolFilePtr> SymbolFileMap;



// -------------------------------------------------------------------------
// SymbolFileManager
// -------------------------------------------------------------------------

/** Manager for symbol files, both paths and actual binary files */
class SymbolFileManager
{
  // --- members ---
private:
  /** TILERA_ROOT/tile pathname, for looking up dlls, etc. */
  Pathname m_root_tile_path;

  /** remote-to-local pathname mappings */
  SymbolFilePathMap m_symbol_file_path_map;

  /** cache of symbol files we've loaded */
  SymbolFileMap     m_symbol_file_map;

  /** cache of local symbol files we've previously found */
  SymbolFileMap     m_local_symbol_file_map;


  // --- constructors/destructors ---
public:
  /** Constructor */
  SymbolFileManager();


  // --- methods ---
public:  
  /** Adds mapping from remote to local pathname for a symbol file */
  void add_path_mapping(const Pathname& remote, const Pathname& local);

  /** Adds mappings from remote to local pathnames for a symbol file */
  void add_path_mappings(const PathnameMap& map);

  /** Returns local symbol file for specified remote path.
      Attempts to map path to local path using path mappings.
      Loads and returns symbol file object from file found at local path.
      Caches opened symbol file objects so subsequent requests
      for same path return same symbol file pointer.
      Returns NULL if symbol file could not be loaded.
  */
  SymbolFilePtr get_symbol_file(Pathname& remote_path);

};

// multiple-inclusion guard
#endif


