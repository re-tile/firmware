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
// SymbolFile.h -- binary file that contains symbols
// ==========================================================================

// multiple-inclusion guard
#ifndef SYMBOL_FILE_H
#define SYMBOL_FILE_H

// C/C++ includes

// OProfile includes
// libutil++
#include <op_bfd.h>         // op_bfd

// custom includes
#include "collections.h"    // collections, FOR_EACH
#include "io_utils.h"       // IO streams
#include "Pathname.h"       // Unix pathnames
#include "SharedPointer.h"  // shared pointer class
#include "Symbol.h"         // Symbol



// --------------------------------------------------------------------------
// type definitions
// --------------------------------------------------------------------------

// fref
class SymbolFile;

/** Pointer to symbol file. */
typedef SharedPointer<SymbolFile>  SymbolFilePtr;


// --------------------------------------------------------------------------
// SymbolFile
// --------------------------------------------------------------------------

/** Binary file (executable or library) that contains symbols */
class SymbolFile
{

  // --- members ---
private:
  /** Pathname of the executable */
  Pathname m_pathname;

  /** OProfile helper class for loading the executable's symbol table */
  op_bfd* m_abfd;

  /** OProfile helper class for loading the executable's symbol table */
  bool m_valid;

  /** Symbol table for this executable */
  SymbolArray m_symbols;

  /** Whether this is a Java pseudo-binary (.jo) file. */
  bool m_java_binary;

  /** Cache of debug pathnames for visited addresses. */
  Map<bfd_vma, Pathname> m_debug_source_files;

  /** Cache of debug line numbers for visited addresses. */
  Map<bfd_vma, unsigned int> m_debug_source_lines;


  // --- constuctors/destructors ---
public:
  /** Constructor */
  SymbolFile(const char* path);

  /** Constructor */
  SymbolFile(const std::string& path);

  /** Constructor */
  SymbolFile(const Pathname& path);


protected:
  /** Init method */
  void
  init();


public:


  /** Destructor */
  ~SymbolFile();


  // --- accessors ---
public:
  /** The executable pathname */
  const std::string
  get_pathname() const;

  /** Whether the executable file exists */
  bool
  exists() const;

  /** Whether the executable was loaded successfully */
  bool
  is_valid() const;

  /** Whether executable has debug info (line number info, etc.) */
  bool
  has_debug_info();

  /** Whether this is a Java pseudo-binary (.jo) file. */
  bool
  is_java_binary() const;

  /** Returns starting offset for specified address */
  u32
  get_start_offset(u64& address);

  /** List of symbols */
  const SymbolArray&
  get_symbols();

  // An OProfile offset is an offset either from
  //  (a) the start of the file
  //  (b) some other "start_offset" vma, for a kernel module or anonymous memory region
  //
  //  We need to:
  //  1. convert the offset to a file-based offset
  //     - if start_offset is 0, the offset is already file-based
  //     - otherwise
  //       - convert start_offset to a section offset (or if 0, to the start of the .text region)
  //           Note: the function op_bfd::get_start_offset() does this conversion
  //       - add this section offset to the original offset to produce a file-based offset
  //
  //  2. convert the file-based offset to a runtime vma:
  //     - determine which ELF section of the binary file the offset falls within
  //     - convert the file/module offset to an offset within that section
  //       (i.e. subtract the section's file offset)
  //     - convert that section offset into a section vma, by adding the section's base vma
  //     This tells us what the offset corresponds to in actual run-time virtual memory space
  //     Note: the function op_bfd::offset_to_pc() does this translation.
  //
  // The method sample_offset_to_vma performs both of these steps.

  /** Converts sample file offset to runtime vma for this binary file */
  bfd_vma
  sample_offset_to_vma(bfd_vma sample_offset, bool is_kernel, bfd_vma start_offset) const;

  /** Finds symbol that contains the specified vma */
  Symbol*
  find_symbol(bfd_vma& address);

  /** Returns file and line number for specified symbol. */
  bool
  get_debug_info(Symbol* symbol,
                 Pathname& file, unsigned int& line);

  /** Returns file and line number for sample vma
      within the specified symbol. */
  bool
  get_debug_info(Symbol* symbol, bfd_vma location,
                 Pathname& file, unsigned int& line);

};

// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, Symbol, symbol)
{
  out << symbol.get_name() << "(" << in_hex(symbol.get_start_vma()) << ")";
  return out;
}

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, SymbolFile, executable)
{
  out << executable.get_pathname();
  return out;
}

// multiple-inclusion guard
#endif
