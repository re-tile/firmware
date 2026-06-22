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
// SampleLocation.h -- Source location with associated
//                     symbol/filename/line-num information
// ==========================================================================

#include "SampleLocation.h"

// C/C++ includes
#include <sstream>

// custom headers
#include "Application.h"   // global settings


// --------------------------------------------------------------------------
// SampleLocation
// --------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
SampleLocation::SampleLocation()
{
  m_vma = 0;
  m_symbol = NULL;
  m_line = 0;
}

/** Constructor */
SampleLocation::SampleLocation(const bfd_vma& offset, 
                               const SymbolFilePtr& symbol_file,
                               const bool in_kernel,
                               const bfd_vma& start_offset)
{
  if (symbol_file == NULL)
  {
    // FIXME: Without a symbol file, we can't calculate
    // a real VMA for user/kernel space data from the offset.
    // The following is probably only correct for "anon" samples,
    // but these are the most likely to not have
    // a valid symbol file anyway.
    m_vma    = offset + start_offset;
    m_symbol = NULL;
  }
  else
  {
    m_vma    = symbol_file->sample_offset_to_vma(offset, in_kernel, start_offset);
    m_symbol = symbol_file->find_symbol(m_vma);
  }

  bool have_debug_info = false;

  std::stringstream as;   // Accumulates the "m_address" value
  as << "0x" << in_hex(m_vma);
  m_address = as.str();

  std::stringstream ss;   // Accumulates the "m_as_string" value


  // construct normal to_string(), which is also used to hash things
  if (m_symbol != NULL)
  {
    // look up debugging information
    have_debug_info = (symbol_file == NULL) ? false :
      symbol_file->get_debug_info(m_symbol, m_vma, m_pathname, m_line);

    ss << m_symbol->get_name();
    if ( ! m_pathname.is_empty() )
    {
      // NOTE: Need to include address so we hash locations at address level.
      ss << ":" << m_pathname.get_name() << ":" << m_line << ":" << m_address;
    }
    else
    {
      ss << ":" << m_address;
      m_line = 0;
    }
  }
  else
  {
    m_pathname = Pathname("");
    m_line = 0;
    ss << m_address;
  }
  m_as_string = ss.str();

  // construct debug string, which includes address/offset info
  if (m_symbol != NULL)
  {
    if ( ! m_pathname.is_empty() )
      ss << " (" << m_address << ", offset=" << in_hex(offset) << ")";
    else
      ss  << ", offset=" << in_hex(offset);
  }
  else
     ss  << ", offset=" << in_hex(offset);
  m_as_debug_string = ss.str();

  if (Application::show_samples() && Application::show_sample_details())
  {
    std::string name = (m_symbol != NULL) ? m_symbol->get_name() : "NULL";
    std::cout << "    "
         << "SampleLocation: "
         << "offset=" << in_hex(offset) << ", "
         << "vma=" << m_address << ", "
         << "symbol=" << name;
    if (have_debug_info)
      std::cout << ", loc=" << m_pathname << ":" << m_line;
    else
      std::cout << ", no debug info for this sample";
    std::cout << std::endl;
  }
}

/** Copy constructor */
SampleLocation::SampleLocation(const SampleLocation& loc)
{
  m_vma = loc.m_vma;
  m_address = loc.m_address;
  m_symbol = loc.m_symbol;
  m_line = loc.m_line;
  m_pathname = loc.m_pathname;
  m_as_string = loc.m_as_string;
  m_as_debug_string = loc.m_as_debug_string;
}

/** Assignment operator */
const SampleLocation&
SampleLocation::operator=(const SampleLocation& loc)
{
  if (&loc != this) // self-copy guard
  {
    m_vma = loc.m_vma;
    m_address = loc.m_address;
    m_symbol = loc.m_symbol;
    m_line = loc.m_line;
    m_pathname = loc.m_pathname;
    m_as_string = loc.m_as_string;
    m_as_debug_string = loc.m_as_debug_string;
  }
  return *this;
}
