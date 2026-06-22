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

// header file
#include "SymbolFileManager.h"

// system includes
#include <string.h>
#include <limits.h>

// custom includes
#include "global_options.h"
#include "Pathname.h"       // Unix pathnames
#include "Map.h"            // map/multimap classes
#include "SharedPointer.h"  // shared pointers
#include "SymbolFile.h"     // symbol file (executable or library)


// Adopted from elsewhere.
// NOTE: Does not set "errno".
int
get_install_path(char* buf, size_t size, const char* tail)
{
  const char* path = "/proc/self/exe";

  int n = readlink(path, buf, size);
  if (n < 0)
    return -1;

  // Detect overflow.
  if ((size_t)n >= size)
    return -1;

  buf[n] = '\0';

  for (int i = n; i >= 0; i--)
  {
    if (!strncmp(buf + i, "/bin/", 5))
    {
      if (i + strlen(tail) > size)
        return -3;
      strcpy(buf + i, tail);
      return 0;
    }
  }

  return -2;
}


// -------------------------------------------------------------------------
// SymbolFileManager
// -------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
SymbolFileManager::SymbolFileManager()
{
  char root_tile_path[PATH_MAX];
  // FIXME: Aborting seems inappropriate.
  if (get_install_path(root_tile_path, sizeof(root_tile_path), "/tile") < 0)
    abort();
  m_root_tile_path = Pathname(root_tile_path);
}


// --- methods ---

/** Adds mapping from remote to local pathname for a symbol file */
void SymbolFileManager::add_path_mapping(const Pathname& remote,
                                         const Pathname& local)
{
  // set new binding for remote path (this displaces any existing binding,
  // which since it's a SharedPointer will be cleaned up automatically
  // when the last reference is deleted)
  m_symbol_file_path_map.add(remote, local);
}

/** Adds mappings from remote to local pathnames for a symbol file */
void SymbolFileManager::add_path_mappings(const PathnameMap& map)
{
  FOR_EACH(const_iterator, pair, PathnameMap, map) {
    const Pathname& remote = pair->first;
    const Pathname& local  = pair->second;
    add_path_mapping(remote, local);
  }
}

/** Returns local symbol file for specified remote path.
    Attempts to map path to local path using path mappings.
    Loads and returns symbol file object from file found at local path.
    Caches opened symbol file objects so subsequent requests
    for same path return same symbol file pointer.
    Returns NULL if symbol file could not be loaded.
*/
SymbolFilePtr SymbolFileManager::get_symbol_file(Pathname& remote_path)
{
  // first, see if we've already cached a local file for this remote path
  SymbolFilePtr result = m_symbol_file_map.get(remote_path, NULL);

  // if not...
  if (result == NULL) {
     // map remote to local path (defaults to remote_path if not found)
     Pathname local_path =
       (! g_use_remote_local_mappings) ? 
       remote_path :
       m_symbol_file_path_map.get(remote_path, remote_path);

     // see if we've looked for this local path before
     result = m_local_symbol_file_map.get(local_path, NULL);
     if (result == NULL) {
       // if not, create a new wrapper for it
       result = SymbolFilePtr(new SymbolFile(local_path));
       m_local_symbol_file_map.put(local_path, result);
     }

     // fallback: if we don't find remote_path based on executable mappings
     // (that is, it's a DLL, etc.)
     // try looking for it under ${TILERA_ROOT}/tile in the MDE installation
     if (! result->exists() ||
         ! result->is_valid()) {
       Pathname local_tile_path = Pathname(m_root_tile_path, remote_path);
       if (local_tile_path.exists()) {

         // see if we've looked for this local path before
         result = m_local_symbol_file_map.get(local_tile_path, NULL);
         if (result == NULL) {
           // if not, create a new wrapper for it
           result = SymbolFilePtr(new SymbolFile(local_tile_path));
           m_local_symbol_file_map.put(local_tile_path, result);
         }
       }
     }

     if (! result->exists()) {
        if (g_show_skipped) {
          cerr << "Could not find symbol file: " << *result << endl;
        }
       result = NULL;
     }
     else if (! result->is_valid()) {
       if (g_show_skipped) {
         cerr << "Could not load symbol file: " << *result << endl;
       }
       result = NULL;
     }
     else if (! result->has_debug_info()) {
        if (g_show_skipped) {
          cerr << "Symbol file does not contain debug symbols: "
               << *result << endl;
        }
       result = NULL;
     }
     else {
       // cache it for next time
       m_symbol_file_map.put(remote_path, result);
     }
  }

  return result;
}

