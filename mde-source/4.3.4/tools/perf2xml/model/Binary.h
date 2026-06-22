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
// Binary.h -- binary module class
// ============================================================================

// multiple-inclusion guard
#ifndef BINARY_H
#define BINARY_H

// C/C++ includes
#include <string>     // std::string

// custom includes
#include "Pathname.h"    // Unix/Linux pathnames
#include "Function.h"    // Function class
#include "collections.h" // collections, FOR_EACH macros
#include "bfd_api.h"     // BFDFile, used for looking up source file/line info


// ----------------------------------------------------------------------------
// Binary
// ----------------------------------------------------------------------------

/** Represents a binary module (executable, library, etc.) */
class Binary
{
  // --- members ---
protected:
  /** Binary pathname. */
  Pathname m_pathname;

  /** Symbol file pathname, if different. */
  Pathname m_symbol_pathname;

  /** BFD file, used for looking up source file/line info. */
  BFDFile* m_bfd_file;

  /** Map from function ids to functions. */
  Map<int, Function*> m_functions;


  // --- constructors/destructors ---
public:
  Binary(const Pathname& pathname,
         const Pathname& symbol_pathname = "");

protected:
  /** Copy constructor */
  Binary(const Binary& obj);

  /** Assignment operator */
  Binary& operator=(const Binary& obj);

public:
  ~Binary();


  // --- init methods ---
protected:
  /** Initializes object. */
  void
  init();


  // --- accessors ---
public:
  /** Gets whether this binary is successfully initialized. */
  bool
  is_valid()
  {
    return (m_symbol_pathname.exists() && m_bfd_file != NULL);
  }

  /** Gets pathname of module. */
  const Pathname&
  get_pathname() const
  {
    return m_pathname;
  }


  // --- methods ---
public:
  /** Adds function. */
  void
  add_function(Function* function);

  /** Returns map from function ids to functions. */
  const Map<int, Function*>&
  get_functions();

  /** Returns function with specified name. */
  Function*
  get_function_by_name(const std::string& name);

  /** Gets source file / line number for given address. */
  bool
  addr2line(uint64_t address,
            std::string& function_name,
            Pathname& source_file,
            unsigned int& source_line,
            bool demangle = true);

};


// multiple-inclusion guard
#endif

