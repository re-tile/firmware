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
// Sample.cc -- OProfile sample
// ==========================================================================

#include "Sample.h"

// C/C++ includes
#include <sstream>

// custom includes


// --------------------------------------------------------------------------
// OProfile sample "key" utilities
// --------------------------------------------------------------------------

/** Gets the "from" offset from 64-bit sample key. */
inline bfd_vma from_offset (const odb_key_t& key)
{
  return (key >> 32) & 0xFFFFFFFF;
}

/** Gets the "to" offset from 64-bit sample key. */
inline bfd_vma to_offset (const odb_key_t& key)
{
  return key & 0xFFFFFFFF;
}


// --------------------------------------------------------------------------
// Sample
// --------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
Sample::Sample (
  const odb_key_t& key,
  const odb_value_t& value,

  bool is_callgraph_sample,

  const SymbolFilePtr& from_symbol_file,
  const bool from_kernel,
  const bfd_vma& from_start_offset,

  const SymbolFilePtr& to_symbol_file,
  const bool to_kernel,
  const bfd_vma& to_start_offset
)
  : m_value(value), m_is_callgraph_sample(is_callgraph_sample),
    m_hash_key()
{
  if (is_callgraph_sample) {
    // key contains "from" and "to" offsets,
    // create a separate location instance for each
    m_from_location = SampleLocation(
      from_offset(key), from_symbol_file, from_kernel, from_start_offset);
    m_to_location = SampleLocation(
      to_offset(key), to_symbol_file, to_kernel, to_start_offset);
  }
  else {
    // key contains only "to" offset,
    // set "from" and "to" location to same value
    m_to_location = SampleLocation(
      to_offset(key), to_symbol_file, to_kernel, to_start_offset);
    m_from_location = m_to_location;
  }
}

/** Copy constructor */
Sample::Sample(const Sample& sample)
{
  m_value = sample.m_value;
  m_is_callgraph_sample = sample.m_is_callgraph_sample;
  m_from_location = sample.m_from_location;
  m_to_location = sample.m_to_location;
  m_hash_key = sample.m_hash_key;
}

/** Assignment operator */
const Sample&
Sample::operator=(const Sample& sample)
{
  if (&sample != this) // self-copy guard
  {
    m_value = sample.m_value;
    m_is_callgraph_sample = sample.m_is_callgraph_sample;
    m_from_location = sample.m_from_location;
    m_to_location = sample.m_to_location;
    m_hash_key = sample.m_hash_key;
  }
  return *this;
}


// --- accessors ---

/** Returns hashmap key for this sample. */
const std::string&
Sample::get_hash_key()
{
  if (m_hash_key.length() == 0) {
    std::stringstream ss;
    if (m_is_callgraph_sample) {
      ss << from_location().to_string() << "->" << to_location().to_string();
    }
    else {
      ss << from_location().to_string();
    }
    m_hash_key = ss.str();
  }
  return m_hash_key;
}
