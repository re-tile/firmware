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
// Binary.cc -- binary module class
// ============================================================================

#include "Binary.h"

// ----------------------------------------------------------------------------
// Binary
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

Binary::Binary(const Pathname& pathname,
               const Pathname& symbol_pathname) :
  m_pathname(pathname),
  m_symbol_pathname((symbol_pathname.empty()) ? pathname : symbol_pathname),
  m_bfd_file(NULL)
{
  init();
}

/** Copy constructor */
Binary::Binary(const Binary& obj)
{}

/** Assignment operator */
Binary&
Binary::operator=(const Binary& obj)
{
  if (this != &obj) // handle self-assignment
  {
  }
  return *this;
}

Binary::~Binary()
{
  FOR_EACH_PAIR(iterator, it, Map<int COMMA Function*>, m_functions)
  {
    Function* f = it->second;
    delete f;
  }
  m_functions.clear();

  if (m_bfd_file != NULL)
  {
    delete m_bfd_file;
    m_bfd_file = NULL;
  }
}


// --- init methods ---

/** Initializes object. */
void Binary::init()
{
  if (m_symbol_pathname.exists())
  {
    m_bfd_file = new BFDFile(m_symbol_pathname);
  }
}


// --- accessors ---



// --- methods ---

/** Adds function. */
void
Binary::add_function(Function* function)
{
  m_functions.put(function->get_id(), function);
}

/** Returns map from function ids to functions. */
const Map<int, Function*>&
Binary::get_functions()
{
  return m_functions;
}

/** Returns function with specified name. */
Function*
Binary::get_function_by_name(const std::string& name)
{
  Function* result = NULL;

  FOR_EACH_PAIR(iterator, it, Map<int COMMA Function*>, m_functions)
  {
    Function& f = *(it->second);
    if (f.get_name() == name)
    {
      result = &f;
      break;
    }
  }

  return result;
}

/** Gets source file / line number for given address. */
bool
Binary::addr2line(const uint64_t address,
                  std::string& function_name,
                  Pathname& source_file, 
                  unsigned int& source_line,
                  bool demangle)
{
  if (m_bfd_file == NULL)
  {
    function_name = "unknown";
    source_file = "unknown";
    source_line = 0;
    return false;
  }
  else
  {
    return m_bfd_file->addr2line(address,
                                 function_name,
                                 source_file,
                                 source_line,
                                 demangle);
  }
}
