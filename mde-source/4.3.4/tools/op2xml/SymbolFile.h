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

// custom includes
#include "io.h"             // IO streams
#include "Pathname.h"       // Unix pathnames
#include "Vector.h"         // Vector class
#include "SharedPointer.h"  // shared pointer class

// OProfile includes
#include <op_bfd.h>         // op_bfd
#include <string_filter.h>  // string_filter
#include "locate_images.h"  // extra_images

// frefs
class SymbolFile;


// --------------------------------------------------------------------------
// Symbol
// --------------------------------------------------------------------------
class Symbol
{
  // --- members ---
private:
  /** SymbolFile containing this symbol. */
  SymbolFile* m_symbol_file;

  /** internal symbol data structure */
  const op_bfd_symbol* m_symbol;

  /** symbol index */
  symbol_index_t m_index;

  /** symbol name */
  string m_name;

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

  /** whether this is a hidden symbol */
  bool m_hidden;

  /** whether the XML for this symbol has been written yet */
  bool m_written_to_xml;

  /** unique integral ID for this symbol */
  unsigned int m_unique_id;


  // --- constructors/destructors ---
public:
  /** Constructor */
  Symbol(SymbolFile* symbol_file, const symbol_index_t& index, const op_bfd_symbol* op_bfd_symbol);


protected:
  /** Init method */
  void init();

  /** Demangles C++/Java symbol name, returns other names as-is */
  const string demangle_name(const string& symbol_name);


  // --- methods ---
public:

  /** Returns symbol name */
  const string& name() const { return m_name; };

  /** Returns symbol file */
  SymbolFile* symbol_file() const { return m_symbol_file; };

  /** Returns whether this is a symbol in a Java pseudo-binary */
  bool is_java_symbol() const;

  /** Returns symbol index in containing SymbolFile */
  const symbol_index_t& index() const { return m_index; };

  /** Returns start vma of symbol's section */
  bfd_vma section_vma() const { return m_section_vma; };

  /** Returns start vma of symbol */
  bfd_vma start_vma() const { return m_start_vma; };

  /** Returns offset of symbol within original file */
  unsigned long filepos() const { return m_filepos; };

  /** Returns offset of symbol within its section */
  unsigned long offset() const { return m_offset; };

  /** Returns end vma of symbol */
  bfd_vma end_vma() const { return m_end_vma; };

  /** Returns the size of the symbol */
  bfd_vma size() const { return m_size; };

  /** Offset value to use when looking up debugging information for this symbol.
      For C/C++ code, this is the symbol's file position,
      but for Java pseudo-binary (.jo) files, it's the symbol's vma. */
  bfd_vma debug_location() const {
    return (is_java_symbol()) ? m_offset + m_section_vma : m_filepos;
  }

  /** Offset value to use when looking up debugging information
      for an address contained in this symbol.
      For C/C++ code, this is the file offset corresponding to the vma
      but for Java pseudo-binary (.jo) files, it's the vma itself. */
  bfd_vma debug_location(bfd_vma vma) const {
    return (is_java_symbol()) ? vma : vma - m_start_vma + m_filepos;
  }

  /** Returns true if symbol contains address */
  bool contains(bfd_vma& address) const {
    return (m_start_vma <= address) && (address <= m_end_vma);
  };

  /** Returns true if symbol is a hidden (internal) symbol */
  bool hidden() const  { return m_hidden; };

  /** Returns true iff this symbol was written to the XML */
  bool was_written_to_xml() const { return m_written_to_xml; };

  /** Marks this symbol as written to the XML */
  void set_written_to_xml() { m_written_to_xml = true; };

  /** Returns the unique integral ID for this symbol */
  long unique_id() const { return m_unique_id; };
};


// --------------------------------------------------------------------------
// type definitions
// --------------------------------------------------------------------------

typedef Symbol*        SymbolPtr;
typedef Vector<Symbol> SymbolList;


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
  SymbolList m_symbols;

  /** Whether this is a Java pseudo-binary (.jo) file. */
  bool m_java_binary;


  // --- constuctors/destructors ---
public:
  /** Constructor */
  SymbolFile(const char* path);

  /** Constructor */
  SymbolFile(const string& path);

  /** Constructor */
  SymbolFile(const Pathname& path);


protected:
  /** Init method */
  void init();


public:


  /** Destructor */
  ~SymbolFile();


  // --- accessors ---
public:
  /** The executable pathname */
  const string pathname() const;

  /** Whether the executable file exists */
  bool exists() const;

  /** Whether the executable was loaded successfully */
  bool is_valid() const;

  /** Whether executable has debug info (line number info, etc.) */
  bool has_debug_info();

  /** Whether this is a Java pseudo-binary (.jo) file. */
  bool is_java_binary() const;

  /** Returns starting offset for specified address */
  u32 get_start_offset(u64& address);

  /** List of symbols */
  const SymbolList& symbols();

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
  bfd_vma sample_offset_to_vma(bfd_vma sample_offset, bool is_kernel, bfd_vma start_offset) const;

  /** Finds symbol that contains the specified vma */
  SymbolPtr find_symbol(bfd_vma& address);

  /** Returns file and line number for specified symbol. */
  bool get_debug_info(SymbolPtr symbol,
                      Pathname& file, unsigned int& line);

  /** Returns file and line number for sample vma
      within the specified symbol. */
  bool get_debug_info(SymbolPtr symbol, bfd_vma location,
                      Pathname& file, unsigned int& line);

};


// --------------------------------------------------------------------------
// type definitions
// --------------------------------------------------------------------------

typedef SharedPointer<SymbolFile>  SymbolFilePtr;


// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, Symbol, symbol)
{
  out << symbol.name() << "(" << in_hex(symbol.start_vma()) << ")";
  return out;
}

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, SymbolFile, executable)
{
  out << executable.pathname();
  return out;
}

// multiple-inclusion guard
#endif
