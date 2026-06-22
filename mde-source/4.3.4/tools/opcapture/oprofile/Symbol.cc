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
// Symbol.cc -- binary file symbol
// ==========================================================================

#include "Symbol.h"

// C/C++ includes
#include <stdlib.h>                // free()

// BFD includes
#include <demangle.h>              // BFD's C++ demangler

// OProfile includes
// libregex
#include <demangle_java_symbol.h>  // OProfile's Java demangler
// libpp
#include <locate_images.h>         // extra_images()
// libutil++
#include <string_filter.h>         // string_filter


// --------------------------------------------------------------------------
// Symbol
// --------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
Symbol::Symbol(const op_bfd_symbol* op_bfd_symbol,
               const symbol_index_t& index) :
  m_symbol(op_bfd_symbol),
  m_index(index)
{
  init();
}


/** Init method */
void
Symbol::init() {
  m_name      = demangle_name(m_symbol->name());
  m_start_vma = m_symbol->vma();
  m_filepos   = m_symbol->filepos();
  m_offset    = m_symbol->value();
  m_section_vma = (m_symbol->symbol() == NULL) ? 0 : m_symbol->section()->vma;
  m_size      = m_symbol->size();
  m_end_vma   = m_start_vma + m_size - ((m_size > 0) ? 1 : 0);
  m_hidden    = m_symbol->hidden();
  m_java_symbol    = false;
}

/** Sets whether this is a symbol in a Java pseudo-binary */
void
Symbol::set_java_symbol(bool is_java_symbol)
{
  m_java_symbol = is_java_symbol;
}

/** Returns whether this is a symbol in a Java pseudo-binary */
bool
Symbol::is_java_symbol() const
{
  return m_java_symbol;
}

/** Demangles C++/Java symbol name, returns other names as-is */
const std::string
Symbol::demangle_name(const std::string& symbol_name)
{
  // try to demangle the name; if we succeed, we use the demangled name

  // First, try C++ demangler
  // options are defined in tools/binutils/include/demangle.h
  int options = DMGL_AUTO;
  char* demangled_cpp = cplus_demangle(symbol_name.c_str(), options);
  if (demangled_cpp != NULL) {
    std::string result = demangled_cpp;
    free(demangled_cpp);
    return result;
  }

  // Next, try Java demangler
  std::string demangled_java = demangle_java_symbol(symbol_name);
  if (! demangled_java.empty()) {
    return demangled_java;
  }

  // If neither makes a difference, it's not a mangled symbol,
  // so return it as-is.
  return symbol_name;
}


// --- methods ---



