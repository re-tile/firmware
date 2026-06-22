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

// header file
#include "SymbolFile.h"
#include "global_options.h"
#include "demangle.h"             // C++ demangler
#include "demangle_java_symbol.h" // Java demangler

unsigned int s_next_unique_id = 0;


// --------------------------------------------------------------------------
// Symbol
// --------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
Symbol::Symbol(SymbolFile* symbol_file,
               const symbol_index_t& index,
               const op_bfd_symbol* op_bfd_symbol)
  : m_symbol_file(symbol_file),
    m_symbol(op_bfd_symbol), m_index(index),
    m_unique_id(s_next_unique_id++)
{
  init();
}


/** Init method */
void Symbol::init() {
  m_name      = demangle_name(m_symbol->name());
  m_start_vma = m_symbol->vma();
  m_filepos   = m_symbol->filepos();
  m_offset    = m_symbol->value();
  m_section_vma = (m_symbol->symbol() == NULL) ? 0 : m_symbol->section()->vma;
  m_size      = m_symbol->size();
  m_end_vma   = m_start_vma + m_size - ((m_size > 0) ? 1 : 0);
  m_hidden    = m_symbol->hidden();
  m_written_to_xml = false;
}

/** Returns whether this is a symbol in a Java pseudo-binary */
bool
Symbol::is_java_symbol() const { return m_symbol_file->is_java_binary(); };

/** Demangles C++/Java symbol name, returns other names as-is */
const string
Symbol::demangle_name(const string& symbol_name)
{
  // try to demangle the name; if we succeed, we use the demangled name

  // First, try C++ demangler
  // options are defined in tools/binutils/include/demangle.h
  int options = DMGL_AUTO;
  char* demangled_cpp = cplus_demangle(symbol_name.c_str(), options);
  if (demangled_cpp != NULL) {
    string result = demangled_cpp;
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
SymbolFile::SymbolFile(const string& path) : m_pathname(path)
{
  init();
}

/** Constructor */
SymbolFile::SymbolFile(const Pathname& path) : m_pathname(path)
{
  init();
}


/** Init method */
void SymbolFile::init()
{
  // create bfd data structure
  m_abfd = NULL;
  m_valid = m_pathname.exists();
  if (m_valid) {
    m_abfd = new op_bfd(m_pathname.to_string(), string_filter(),
                        extra_images(), m_valid);

    // check for Java pseudo-binary
    m_java_binary = ends_with(m_pathname, ".jo");

    if (g_show_symbols) {
	  u64 zero = 0L;
      bfd_vma start_vma, end_vma;
      m_abfd->get_vma_range(start_vma, end_vma);
      unsigned int start_offset = get_start_offset(zero);

      cout << "Symbol File: " << m_pathname;
      if (m_java_binary) cout << " (java psuedo-binary)";
      cout << endl;
      cout << "offset = "    << in_hex(start_offset) << ", "
           << "vma start = " << in_hex(start_vma)    << ", "
           << "vma end   = " << in_hex(end_vma)      << endl;
    }

    // read symbols and create wrappers for them
    std::vector<op_bfd_symbol>& symbols = m_abfd->syms;
    for (symbol_index_t i = 0; i < symbols.size(); ++i) {
      const op_bfd_symbol& bfd_symbol = m_abfd->syms[i];

      // ignore a few common pseudo-symbols that mark beginning of sections
      const std::string& name = bfd_symbol.name();
      if (name == ".init" || name == ".fini" || name == ".text" || name == ".plt")
      {
        if (g_show_symbols) {
          cout << "Skipped section header symbol: " << name << endl;
        }
        continue;
      }

      Symbol symbol(this, i, &bfd_symbol);
      m_symbols.add(symbol);

      if (g_show_symbols) {
        bfd_vma filepos     = symbol.filepos();     // absolute offset of symbol in binary file
        bfd_vma offset      = symbol.offset();      // offset of symbol within its ELF section
        bfd_vma start       = symbol.start_vma();   // memory address at which symbol is loaded
        bfd_vma end         = symbol.end_vma();     // start_vma + symbol size
        bfd_vma debug_location = symbol.debug_location(); // offset used to look up debugging info

        Pathname file = "";
        unsigned int line = 0;

        bool have_debug_info = get_debug_info(&symbol, file, line) ;

        cout << "Symbol: "   << symbol.name()   << ", "
             << "filepos="   << in_hex(filepos) << ", "
             << "offset="    << in_hex(offset)  << ", "
             << "vma start=" << in_hex(start)   << ", "
             << "vma end="   << in_hex(end)     << ", "
             << "debug_location=" << in_hex(debug_location);

        if (have_debug_info) {
          cout << ", loc=" << file << ":" << line;
        }
        else {
          cout << ", no debug info for this symbol";
        }

        cout << endl;
      }
    }
  }
}


/** Destructor */
SymbolFile::~SymbolFile()
{
  delete m_abfd;
}


// --- accessors ---

/** The executable pathname */
const string SymbolFile::pathname() const
{
  return m_pathname.to_string();
}

/** Whether the executable file exists */
bool SymbolFile::exists() const
{
  return m_pathname.exists();
}

/** Whether the executable was loaded successfully */
bool SymbolFile::is_valid() const
{
  return m_valid;
}

/** Whether executable has debug info (line number info, etc.) */
bool SymbolFile::has_debug_info() {
  return m_valid && m_abfd->has_debug_info();
}

/** Whether this is a Java pseudo-binary (.jo) file. */
bool SymbolFile::is_java_binary() const
{
  return m_java_binary;
}

/** Returns starting offset for specified address */
u32 SymbolFile::get_start_offset(u64& address)
{
  return m_abfd->get_start_offset(address);
}

/** List of symbols */
const SymbolList& SymbolFile::symbols()
{
  return m_symbols;
}

/** Converts sample file offset to runtime vma for this binary file */
bfd_vma SymbolFile::sample_offset_to_vma(bfd_vma sample_offset,
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
    offset = m_abfd->get_start_offset(0);

    result = m_abfd->offset_to_pc(sample_offset + offset);
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
    result = m_abfd->offset_to_pc(sample_offset);
  }

  if (g_show_samples && g_show_sample_details) {
    cout << "    SymbolFile.offset_to_vma: "
         << "sample_offset=" << in_hex(sample_offset) << ", "
         << "start_offset=" << in_hex(start_offset) << ", "
         << "is_kernel=" << is_kernel << ", "
         << "is_anon=" << is_anon << ", "
         << "offset=" << in_hex(offset) << ", "
         << "result=" << in_hex(result) << endl;
  }
  return result;
}

/** Finds symbol that contains the specified vma */
SymbolPtr SymbolFile::find_symbol(bfd_vma& address)
{
  SymbolPtr result = NULL;
  FOR_EACH(iterator, i, Vector<Symbol>, m_symbols)
  {
    Symbol& s = *i;
    if (s.contains(address)) {
      result = &s;
      break;
    }
  }
  return result;
}

/** Returns file and line number for specified symbol. */
bool SymbolFile::get_debug_info(SymbolPtr symbol,
                                Pathname& file, unsigned int& line)
{
  string filename;

  bool result = m_abfd->get_linenr(symbol->index(), 
                                   symbol->debug_location(), filename, line);
  file = filename;
  return result;
}

/** Returns file and line number for sample vma
    within the specified symbol. */
bool SymbolFile::get_debug_info(SymbolPtr symbol, bfd_vma vma,
                                Pathname& file, unsigned int& line)
{
  string filename;

  bool result = m_abfd->get_linenr(symbol->index(), 
                                   symbol->debug_location(vma), filename, line);
  file = filename;
  return result;
}
