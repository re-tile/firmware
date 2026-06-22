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
// Binary.cc -- Binary class
// ============================================================================

#include "Binary.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// Binary
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Binary::Binary(const std::string& type,
               const std::string& pathname) :
  m_id(-1),
  m_type(type),
  m_pathname(pathname),
  m_bfd_file(NULL)
{
  init();
}

/** Copy constructor. */
Binary::Binary(const Binary& binary) :
  m_id(binary.m_id),
  m_type(binary.m_type),
  m_pathname(binary.m_pathname),
  m_bfd_file(binary.m_bfd_file)
{
}

/** Copy constructor. */
Binary::Binary(const Binary* binary) :
  m_id(binary->m_id),
  m_type(binary->m_type),
  m_pathname(binary->m_pathname),
  m_bfd_file(binary->m_bfd_file)
{
}

/** Assignment operator. */
const Binary&
Binary::operator=(const Binary& binary)
{
  if (&binary != this) // self-assignment guard
  {
    m_id       = binary.m_id;
    m_type     = binary.m_type;
    m_pathname = binary.m_pathname;
    m_bfd_file = binary.m_bfd_file;
  }
  return *this;
}

/** Assignment operator. */
const Binary&
Binary::operator=(const Binary* binary)
{
  return operator=(*binary);
}

/** Destructor. */
Binary::~Binary()
{}


// --- init methods ---

/** Initializes object. */
void Binary::init()
{
  if (Pathname(m_pathname).exists())
  {
    m_bfd_file = BFDFilePtr(new BFDFile(m_pathname));
  }
}


// --- object methods ---

/** Equality test. */
bool
Binary::equals(const Binary& binary) const
{
  return(
    m_id       == binary.m_id &&
    m_type     == binary.m_type &&
    m_pathname == binary.m_pathname
  );
}

/** Equality operator. */
bool
Binary::operator==(const Binary& binary) const
{
  return equals(binary);
}


// --- accessors ---



// --- methods ---

/** Gets source file / line number for given address. */
bool
Binary::addr2line(const uint64_t address,
                  std::string& function_name,
                  Pathname& source_file, 
                  unsigned int& source_line,
                  bool demangle)
{
  bool result = false;

  if (m_bfd_file == NULL)
  {
    function_name = "unknown";
    source_file = "unknown";
    source_line = 0;
    return result;
  }

  // check whether we've visited this address before
  source_file = m_debug_source_files.get(address,"");
  if (! source_file.is_empty())
  {
    source_line = m_debug_source_lines.get(address, 0);
    result = true;
  }

  else {
    result = m_bfd_file->addr2line(address,
                                   function_name,
                                   source_file,
                                   source_line,
                                   demangle);
    if (! result)
    {
      source_file = "";
      source_line = 0;
    }

    // cache result, if any, for next time
    m_debug_source_files.put(address, source_file);
    m_debug_source_lines.put(address, source_line);
  }

  return result;
}
