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
// SymbolFileManager.cc -- symbol file management
// ==========================================================================

#include "SymbolFileManager.h"

// C/C++ includes

// custom includes


// -------------------------------------------------------------------------
// SymbolFileManager
// -------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
SymbolFileManager::SymbolFileManager()
{
}


// --- methods ---

/** Gets symbol file for specified path.
    Caches previously located symbol files so subsequent requests
    for same path return same symbol file.
    Returns NULL if symbol file could not be loaded. */
SymbolFilePtr
SymbolFileManager::get_symbol_file(const Pathname& pathname)
{
  // See if we already have it.
  SymbolFilePtr result = m_symbol_file_map.get(pathname, NULL);

  // If not, create and cache it.
  if (result == NULL)
  {
    result = SymbolFilePtr(new SymbolFile(pathname));
    m_symbol_file_map.put(pathname, result);
  }

  return result;
}
