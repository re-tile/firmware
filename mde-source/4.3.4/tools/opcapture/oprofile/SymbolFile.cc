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
// SymbolFile.cc -- binary file that contains symbols
// ==========================================================================

#include "SymbolFile.h"

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

// custom includes
#include "Application.h"           // global settings
#include "Symbol.h"                // Symbol


// --------------------------------------------------------------------------
// SymbolFile
// --------------------------------------------------------------------------


// --- constuctors/destructors ---

/** Constructor */
SymbolFile::SymbolFile(const char* path) : m_pathname(path)
{
  init();
}

/** Constructor */
SymbolFile::SymbolFile(const std::string& path) : m_pathname(path)
{
  init();
}

/** Constructor */
SymbolFile::SymbolFile(const Pathname& path) : m_pathname(path)
{
  init();
}


/** Init method */
void
SymbolFile::init()
{
  // Check that file actually exists.
  m_valid = m_pathname.exists();

  // Check for Java pseudo-binary.
  m_java_binary = ends_with(m_pathname, ".jo");

  // Create bfd data structure, if we can.
  if (m_valid) {
    m_abfd = new op_bfd(m_pathname.to_string(), string_filter(),
                        extra_images(), m_valid);
  }
  if (! m_valid) {
    m_abfd = NULL;
  }

  // If so, extract symbol information.
  if (m_valid) {
    if (Application::show_symbols()) {
          u64 zero = 0L;
      bfd_vma start_vma, end_vma;
      m_abfd->get_vma_range(start_vma, end_vma);
      unsigned int start_offset = get_start_offset(zero);

      std::cout << "Symbol File: " << m_pathname;
      if (m_java_binary) 
        std::cout << " (java psuedo-binary)";
      std::cout << std::endl;
      std::cout << "offset = "    << in_hex(start_offset) << ", "
           << "vma start = " << in_hex(start_vma)    << ", "
           << "vma end   = " << in_hex(end_vma)      << std::endl;
    }

    // read symbols and create wrappers for them
    std::vector<op_bfd_symbol>& symbols = m_abfd->syms;
    for (symbol_index_t index = 0; index < symbols.size(); ++index) {
      const op_bfd_symbol& bfd_symbol = m_abfd->syms[index];

      // ignore a few common pseudo-symbols that mark beginning of sections
      const std::string& name = bfd_symbol.name();
      if (name == ".init" || name == ".fini" || name == ".text" || name == ".plt")
      {
        if (Application::show_symbols()) {
          std::cout << "Skipped section header symbol: " << name << std::endl;
        }
        continue;
      }

      Symbol symbol(&bfd_symbol, index);
      symbol.set_java_symbol(m_java_binary);

      m_symbols.add(symbol);

      if (Application::show_symbols()) {
        bfd_vma filepos     = symbol.get_filepos();     // absolute offset of symbol in binary file
        bfd_vma offset      = symbol.get_offset();      // offset of symbol within its ELF section
        bfd_vma start       = symbol.get_start_vma();   // memory address at which symbol is loaded
        bfd_vma end         = symbol.get_end_vma();     // start_vma + symbol size
        bfd_vma debug_location = symbol.get_debug_location(); // offset used to look up debugging info

        Pathname file = "";
        unsigned int line = 0;

        bool have_debug_info = get_debug_info(&symbol, file, line) ;

        std::cout << "Symbol: "   << symbol.get_name()   << ", "
                  << "filepos="   << in_hex(filepos) << ", "
                  << "offset="    << in_hex(offset)  << ", "
                  << "vma start=" << in_hex(start)   << ", "
                  << "vma end="   << in_hex(end)     << ", "
                  << "debug_location=" << in_hex(debug_location);

        if (have_debug_info) {
          std::cout << ", loc=" << file << ":" << line;
        }
        else {
          std::cout << ", no debug info for this symbol";
        }

        std::cout << std::endl;
      }
    }

    // Sort collected symbols by their start addresses.
    // (Would like to have used a Set instead, but we need an array
    //  later on to do binary searching by vmas.)
    m_symbols.sort();

  }
}


/** Destructor */
SymbolFile::~SymbolFile()
{
  if (m_abfd != NULL)
    delete m_abfd;
}


// --- accessors ---

/** The executable pathname */
const std::string
SymbolFile::get_pathname() const
{
  return m_pathname.to_string();
}

/** Whether the executable file exists */
bool
SymbolFile::exists() const
{
  return m_pathname.exists();
}

/** Whether the executable was loaded successfully */
bool
SymbolFile::is_valid() const
{
  return m_valid;
}

/** Whether executable has debug info (line number info, etc.) */
bool
SymbolFile::has_debug_info()
{
  return m_valid && m_abfd->has_debug_info();
}

/** Whether this is a Java pseudo-binary (.jo) file. */
bool
SymbolFile::is_java_binary() const
{
  return m_java_binary;
}

/** Returns starting offset for specified address */
u32
SymbolFile::get_start_offset(u64& address)
{
  return (m_abfd == NULL) ? 0 : m_abfd->get_start_offset(address);
}

/** List of symbols */
const SymbolArray&
SymbolFile::get_symbols()
{
  return m_symbols;
}

/** Converts sample file offset to runtime vma for this binary file */
bfd_vma
SymbolFile::sample_offset_to_vma(bfd_vma sample_offset,
                                 bool is_kernel,
                                 bfd_vma start_offset) const
{
  bfd_vma result = 0;
  bfd_vma offset = 0;
  bool is_anon = false;

  // Note: This is a best guess as to how OProfile handles
  // sample addresses, based on looking at:
  // ../tools/oprofile/libpp/profile.cpp: set_offset()
  // There's also a brief discussion of this in the comment
  // for profile_t::start_offset in the header
  // .../tools/oprofile/libpp/profile.h

  if (is_kernel) {
    // For kernel samples, the sample offset
    // is an offset from the start of the kernel's .text region;
    // start_offset is not used and can be ignored.

    // with argument of 0, this function returns the file offset
    // of the .text region (e.g. 0x20000 for vmlinux)
    offset = (m_abfd == NULL) ? 0 : m_abfd->get_start_offset(0);

    result = (m_abfd == NULL) ? 0 : m_abfd->offset_to_pc(sample_offset + offset);
  }
  else if (start_offset > 0) {
    // For "anon" samples, the sample_offset is an offset
    // from the supplied start_offset of the memory region.

    is_anon = true;
    offset = start_offset;
    result = sample_offset + offset;
  }
  else {
    // For other user-space samples, the sample_offset
    // is a filepos in the binary file; this needs to be
    // converted to a vma.
    offset = 0;
    result = (m_abfd == NULL) ? 0 : m_abfd->offset_to_pc(sample_offset);
  }

  if (Application::show_samples() && Application::show_sample_details()) {
    std::cout << "    SymbolFile.offset_to_vma: "
              << "sample_offset=" << in_hex(sample_offset) << ", "
              << "start_offset=" << in_hex(start_offset) << ", "
              << "is_kernel=" << is_kernel << ", "
              << "is_anon=" << is_anon << ", "
              << "offset=" << in_hex(offset) << ", "
              << "result=" << in_hex(result) << std::endl;
  }
  return result;
}

/** Finds symbol that contains the specified vma */
Symbol*
SymbolFile::find_symbol(bfd_vma& address)
{
  Symbol* result = NULL;

  // Symbol set is implicitly sorted, so we can use binary search.
  int size = (int) m_symbols.size(); // allow unsigned values
  int i = 0, j = size;
  while (i < j)
  {
    int m = (i + j) / 2;
    Symbol& s = m_symbols.get(m);
    // Note: start address of the symbol that contains the address
    //       will be <= the address, so we check contains() first.
    if (s.contains(address))
    {
      result = &s;
      break;
    }
    else if (s.get_start_vma() < address)
    {
      i = m+1;
    }
    else {
      j = m;
    }
  }

  if (Application::validate_symbol_lookup())
  {
    Symbol* result2 = NULL;
    FOR_EACH(iterator, i, SymbolArray, m_symbols)
    {
      Symbol& s = *i;
      if (s.contains(address)) {
        result2 = &s;
        break;
      }
    }

    if (result2 != NULL) {
      if (result == NULL || !((*result2) == (*result)))
      {
        std::cout << "Search mismatch: flat search=" << result2 << ", binary search= NULL" << std::endl;
      }
    }
  }

  // Special case: a library without symbols may have a single
  // "placeholder" symbol containing the library's pathname.
  // Treat this the same as an unfound symbol.
  if (result != NULL &&
      result->get_name() == get_pathname())
  {
    result = NULL;
  }

  return result;
}

/** Returns file and line number for specified symbol. */
bool
SymbolFile::get_debug_info(Symbol* symbol,
                           Pathname& file, unsigned int& line)
{
  std::string filename;

  bool result = (m_abfd != NULL) &&
                m_abfd->get_linenr(symbol->get_index(), 
                                   symbol->get_debug_location(), filename, line);
  file = filename;
  return result;
}

/** Returns file and line number for sample vma
    within the specified symbol. */
bool
SymbolFile::get_debug_info(Symbol* symbol, bfd_vma vma,
                           Pathname& file, unsigned int& line)
{
  bool result = false;
  if (m_abfd == NULL) return result;

  // Check if we've visited this address before.
  file = m_debug_source_files.get(vma, "");
  if (! file.is_empty())
  {
    result = true;
    line = m_debug_source_lines.get(vma, 0);
  }

  // If not, do the expensive symbol lookup.
  else
  {
    std::string filename;
    bool result = m_abfd->get_linenr(symbol->get_index(), 
                                     symbol->get_debug_location(vma),
                                     filename, line);
    if (result)
    {
      file = filename;
    }
    else {
      file = "";
      line = 0;
    }

    // cache result, if any, for next time
    m_debug_source_files.put(vma, file);
    m_debug_source_lines.put(vma, line);

  }
  return result;
}
