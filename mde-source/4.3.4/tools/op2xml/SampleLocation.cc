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

// header file
#include "SampleLocation.h"

// system headers
#include <sstream>

// custom headers
#include "global_options.h"


// --------------------------------------------------------------------------
// SampleLocation
// --------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
SampleLocation::SampleLocation (const bfd_vma& offset, 
                                const SymbolFilePtr& symbol_file,
                                const bool in_kernel,
                                const bfd_vma& start_offset)
                             
{
  m_vma = symbol_file->sample_offset_to_vma(offset, in_kernel, start_offset);
  m_symbol = symbol_file->find_symbol(m_vma);
  bool have_debug_info = false;

  std::stringstream ss;   // Accumulates the "m_as_string" value
  
  // construct normal to_string(), which is also used to hash things
  if (m_symbol != NULL) {

    // look up debugging information
    have_debug_info = symbol_file->get_debug_info(m_symbol, m_vma, m_path, m_line);

    ss << m_symbol->name();
    if ( ! m_path.empty() ) {
      ss << ":" << m_path.name() << ":" << m_line;
    } else {
      ss << ":0x" << in_hex(m_vma);
      m_line = 0;
    }
  }
  else {
    m_path = Pathname("");
    m_line = 0;
    ss << "0x" << in_hex(m_vma);
  }
  m_as_string = ss.str();

  // construct debug string, which includes address/offset info
  if (m_symbol != NULL) {
    if ( ! m_path.empty() ) {
      ss << " (0x" << in_hex(m_vma) << ", offset=" << in_hex(offset) << ")";
    } else {
      ss  << ", offset=" << in_hex(offset);
    }
  }
  else {
     ss  << ", offset=" << in_hex(offset);
  }
  m_as_debug_string = ss.str();

  if (g_show_samples && g_show_sample_details) {
    string name = (m_symbol != NULL) ? m_symbol->name() : "NULL";
    cout << "    "
         << "SampleLocation: "
         << "offset=" << in_hex(offset) << ", "
         << "vma=" << in_hex(m_vma) << ", "
         << "symbol=" << name;
    if (have_debug_info) {
      cout << ", loc=" << m_path << ":" << m_line;
    }
    else {
      cout << ", no debug info for this sample";
    }
    cout << endl;
  }
}
