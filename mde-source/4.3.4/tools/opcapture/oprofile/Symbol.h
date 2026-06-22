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
// Symbol.h -- binary file symbols
// ==========================================================================

// multiple-inclusion guard
#ifndef SYMBOL_H
#define SYMBOL_H

// C/C++ includes

// OProfile includes
// libutil++
#include <op_bfd.h>         // op_bfd

// custom includes
#include "collections.h"    // collections, FOR_EACH
#include "io_utils.h"       // IO streams
#include "Pathname.h"       // Unix pathnames
#include "SharedPointer.h"  // shared pointer class


// --------------------------------------------------------------------------
// type definitions
// --------------------------------------------------------------------------

// fref
class Symbol;

/** Array of symbols. */
typedef Array<Symbol>  SymbolArray;


// --------------------------------------------------------------------------
// Symbol
// --------------------------------------------------------------------------
class Symbol
{
  // --- members ---
private:
  /** internal symbol data structure */
  const op_bfd_symbol* m_symbol;

  /** symbol index */
  symbol_index_t m_index;

  /** symbol name */
  std::string m_name;

  /** start address of section */
  bfd_vma m_section_vma;

  /** start address */
  bfd_vma m_start_vma;

  /** filepos of symbol */
  unsigned long m_filepos;

  /** symbol offset in section */
  unsigned long m_offset;

  /** end address */
  bfd_vma m_end_vma;

  /** size */
  bfd_vma m_size;

  /** whether this is a hidden symbol. */
  bool m_hidden;

  /** Whether this is a Java pseudo-binary (.jo) symbol. */
  bool m_java_symbol;


  // --- constructors/destructors ---
public:
  /** Constructor */
  Symbol(const op_bfd_symbol* op_bfd_symbol,
         const symbol_index_t& index);


protected:
  /** Init method */
  void
  init();

  /** Demangles C++/Java symbol name, returns other names as-is */
  const std::string
  demangle_name(const std::string& symbol_name);


  // --- object methods ---
public:
  /** Equality comparison. */
  bool
  equals(const Symbol& s) const
  {
    // Can just compare internal symbols,
    // since most other properties are derived.
    return (m_symbol == s.m_symbol);
  }

  /** Less-than comparison. */
  bool
  less_than(const Symbol& s) const
  {
    // Compare start addresses.
    return (m_start_vma < s.m_start_vma);
  }

  /** Operator overload, equivalent to equals(). */
  bool
  operator==(const Symbol& s) const 
  {
    return equals(s);
  }

  /** Operator overload, equivalent to less_than(). */
  bool
  operator<(const Symbol& s) const 
  {
    return less_than(s);
  }


  // --- methods ---
public:

  /** Returns symbol name */
  const std::string&
  get_name() const
  {
    return m_name;
  };

  /** Sets whether this is a symbol in a Java pseudo-binary */
  void
  set_java_symbol(bool is_java_symbol);

  /** Returns whether this is a symbol in a Java pseudo-binary */
  bool
  is_java_symbol() const;

  /** Returns symbol index in containing SymbolFile */
  const symbol_index_t&
  get_index() const
  {
    return m_index;
  };

  /** Returns start vma of symbol's section */
  bfd_vma
  get_section_vma() const
  {
    return m_section_vma;
  };

  /** Returns start vma of symbol */
  bfd_vma
  get_start_vma() const
  {
    return m_start_vma;
  };

  /** Returns offset of symbol within original file */
  unsigned long
  get_filepos() const
  {
    return m_filepos;
  };

  /** Returns offset of symbol within its section */
  unsigned long
  get_offset() const
  {
    return m_offset;
  };

  /** Returns end vma of symbol */
  bfd_vma
  get_end_vma() const
  {
    return m_end_vma;
  };

  /** Returns the size of the symbol */
  bfd_vma
  get_size() const
  {
    return m_size;
  };

  /** Offset value to use when looking up debugging information for this symbol.
      For C/C++ code, this is the symbol's file position,
      but for Java pseudo-binary (.jo) files, it's the symbol's vma. */
  bfd_vma
  get_debug_location() const
  {
    return (is_java_symbol()) ? m_offset + m_section_vma : m_filepos;
  }

  /** Offset value to use when looking up debugging information
      for an address contained in this symbol.
      For C/C++ code, this is the file offset corresponding to the vma
      but for Java pseudo-binary (.jo) files, it's the vma itself. */
  bfd_vma
  get_debug_location(bfd_vma vma) const
  {
    return (is_java_symbol()) ? vma : vma - m_start_vma + m_filepos;
  }

  /** Returns true if symbol contains address */
  bool
  contains(bfd_vma& address) const
  {
    return (m_start_vma <= address) && (address <= m_end_vma);
  };

  /** Returns true if symbol is a hidden (internal) symbol */
  bool
  is_hidden() const 
  {
    return m_hidden;
  };
};

// multiple-inclusion guard
#endif
