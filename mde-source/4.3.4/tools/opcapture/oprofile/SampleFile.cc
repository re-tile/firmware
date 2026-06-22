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
// SampleFile.cc -- OProfile sample file
// ==========================================================================

#include "SampleFile.h"

// C/C++ includes
#include <string.h>       // std::string
#include <errno.h>        // errno
#include <fcntl.h>        // open, read, O_RDONLY

// OProfile includes
// libpp
#include <op_header.h>    // read_header()

// custom includes
#include "Application.h"  // global settings

/** Gets the "from" offset from a 64-bit key */
inline bfd_vma from_offset (const odb_key_t& key)
{
  return (key >> 32) & 0xFFFFFFFF;
}

/** Gets the "to" offset from a 64-bit key */
inline bfd_vma to_offset (const odb_key_t& key)
{
  return key & 0xFFFFFFFF;
}


// --------------------------------------------------------------------------
// SampleFile
// --------------------------------------------------------------------------

// --- constuctors/destructors ---

/** Constructor */
SampleFile::SampleFile (
  const Pathname&            path,
  unsigned int               session_number,
  SymbolFileManager&         symbol_file_manager
)
{
  init(SampleFilePathname(path), session_number, symbol_file_manager);
}

/** Constructor */
SampleFile::SampleFile (
  const SampleFilePathname&  path,
  unsigned int               session_number,
  SymbolFileManager&         symbol_file_manager
)
{
  init(path, session_number, symbol_file_manager);
}

/** Init method */
void
SampleFile::init(const SampleFilePathname&  path,
                 unsigned int               session_number,
                 SymbolFileManager&         symbol_file_manager)
{
  m_pathname = path;
  m_session_number = session_number;

  // Check sample file path validity.
  // See [4877] -- if sample filename is not parseable
  // as an OProfile sample file name
  // (for example if it's an NFS temp file, etc.)
  // we just skip it to avoid potential issues.
  m_valid = m_pathname.is_valid();
  if (! m_valid)
  {
    if (Application::show_sample_files_skipped())
    {
      std::cerr << "NOTE: skipping unparseable filename in sample directory: "
                << m_pathname << std::endl;
    }
    return;
  }

  // Check whether this is a callgraph "{cg}" sample file
  m_is_callgraph_file = m_pathname.is_callgraph_file();

  // The "from" path for this sample file.
  // For non-callgraph files, this is the only path.
  m_from_pathname = m_pathname.get_from_module_pathname();

  // Symbol file, if any, for this path.
  // May be NULL if file doesn't exist.
  m_from_symbol_file =
    symbol_file_manager.get_symbol_file(m_from_pathname);

  // The "to" path, if any.
  if (! m_is_callgraph_file)
  {
    // Copy "from" to "to" for simplicity.
    m_to_pathname    = m_from_pathname;
    m_to_symbol_file = m_from_symbol_file;
  }
  else
  {
    // The "to" path for this sample file.
    m_to_pathname = m_pathname.get_to_module_pathname();

    // Symbol file, if any, for this path.
    // May be NULL if file doesn't exist.
    m_to_symbol_file =
      symbol_file_manager.get_symbol_file(m_to_pathname);
  }

  // Check first if the OProfile sample file's version is ok,
  // otherwise odb_open() can fail with an obscure error message.
  if (! sample_file_header_version_check(m_pathname))
  {
    if (Application::show_sample_files_skipped())
    {
      std::cerr << "Could not open OProfile sample file header: " << m_pathname << "\n";
    }
  }

  // Now attempt to open the samples file itself.
  odb_t samples_db;
  int status = odb_open(&samples_db, m_pathname.c_str(),
                        ODB_RDONLY, sizeof(struct opd_header));
  m_valid = (0 == status);
  if (! m_valid)
  {
    if (Application::show_sample_files_skipped())
    {
      std::cerr << "Could not open OProfile sample file: " << m_pathname << "\n"
                << "Reason: (" << status << ") " << strerror(status) << "\n";
    }
    return;
  }

  // Note: This is a best guess as to how OProfile handles
  // sample data, based on looking at:
  // ../tools/oprofile/libpp/profile.cpp: set_offset()
  // There's also a brief discussion of this in the comment
  // for profile_t::start_offset in the header
  // .../tools/oprofile/libpp/profile.h

  // We need to track how the from/to offsets for all samples
  // in this file are represented:
  //  (a) user space        {root} -- offset (i.e. filepos) from start of executable/binary file
  //  (b) kernel space      {kern} -- offset from start of .text module in kernel space
  //  (c) anon (Java, etc.) {anon} -- offset from start of arbitrary block of memory
  //                                  (anon_start = block start address)

  // In callgraph data, the "from" and "to" function locations
  // can be in different parts of the code, so we need to maintain
  // the above details separately for the "from" and "to" offsets.

  // Get sample file header block, which has flags that determine
  // the above for this file.
  // Note: opd_header is declared in ../tools/oprofile/libop/op_sample_file.h.
  const opd_header& hdr =
    * static_cast<opd_header*> (odb_get_data(&samples_db));

  // "from" offset properties

  // kernel space flag
  m_from_kernel = hdr.is_kernel;

  // for "anon" samples, start of memory block that offset is relative to
  m_from_start_offset = hdr.anon_start;


  // "to" offset properties (if any, for callgraph samples)

  if (m_is_callgraph_file)
  {
    // kernel space flag
    m_to_kernel = hdr.cg_to_is_kernel;

    // for "anon" samples, start of memory block that offset is relative to
    m_to_start_offset = hdr.cg_to_anon_start;
  }
  else
  {
    // Copy "from" properties, so "from" and "to" report the same data.
    m_to_kernel       = m_from_kernel;
    m_to_start_offset = m_from_start_offset;
  }

  // Collect sample data.
  // Samples are (key, value) pairs:
  // - key is either:
  //   - for non-cg files, a single 32-bit offset
  //   - for cg files,     a 64-bit offset pair:
  //     ((from_offset<<32)|to_offset)
  // - value is the collected sample count
  //   - for non-cg files, this is the number of "self" samples taken
  //   - for cg files, this is the number of samples that included
  //     the from->to call in their callstack data.

  // Map from sample's hash key to location in m_samples list.
  // We use this to avoid creating duplicate entries
  // for samples at the same location.
  typedef Map<std::string, int> SampleMap;
  SampleMap line_level_sample_map;

  odb_node_nr_t node_nr, pos;
  odb_node_t * node = odb_get_iterator(&samples_db, &node_nr);

  for (pos = 0; pos < node_nr; ++pos)
  {
    odb_key_t   key   = node[pos].key;
    odb_value_t value = node[pos].value;

    if (Application::show_samples() && Application::show_sample_details())
    {
      if (m_is_callgraph_file)
        std::cout << "  Sample raw data (cg): ";
      else
        std::cout << "  Sample raw data (non-cg): ";

      std::cout << "key="  << in_hex(key) << ", ";

      std::cout << "from=" << in_hex(from_offset(key)) << ", "
                << "from_kernel=" << m_from_kernel << ", "
                << "from_start_offset=" << in_hex(m_from_start_offset)  << ", ";

      if (m_is_callgraph_file)
      {
        std::cout << "to=" << in_hex(to_offset(key)) << ", "
                  << "to_kernel=" << m_to_kernel << ", "
                  << "to_start_offset=" << in_hex(m_to_start_offset) << ", ";
      }

      std::cout << "value=" << value << std::endl;
    }

    // Construct sample object.
    Sample sample(key, value, m_is_callgraph_file,
         m_from_symbol_file, m_from_kernel, m_from_start_offset,
         m_to_symbol_file,   m_to_kernel,   m_to_start_offset);

    if (Application::show_samples())
    {
      if (m_is_callgraph_file)
        std::cout << "  Sample (cg): ";
      else
        std::cout << "  Sample (non-cg): ";

      std::cout << "from="  << sample.from_location().to_debug_string() << ", "
                << "to="    << sample.to_location()  .to_debug_string() << ", "
                << "value=" << sample.value()
                << std::endl;

      if (Application::show_sample_details())
      {
        // add extra whitespace to separate each sample from the next
        std::cout << std::endl;
      }
    }

    // Add sample to samples list.
    // If we've already seen a sample with the same location, etc.
    // we just increment its "value" field.
    std::string map_key = sample.get_hash_key();
    int matched_samples_index = line_level_sample_map.get(map_key, -1);

    if (matched_samples_index == -1)
    {
      line_level_sample_map.add (map_key, m_samples.size());
      m_samples.add(sample);
    }
    else {
      m_samples[matched_samples_index].increment_value_by(value);
    }

  }

  // close the sample file
  odb_close(&samples_db);
}

/** Check OProfile header version, return false if it's
    not readable or not consistent with OProfile version in use. */
bool
SampleFile::sample_file_header_version_check(SampleFilePathname& m_pathname)
{
  bool result = false;

  int fd = open(m_pathname.c_str(), O_RDONLY);
  if (fd < 0) {
    m_valid = false;
  }
  else {
    // NOTE: We would _like_ to use read_header() from libpp,
    // but when we try to link we get an unresolvable "undefined reference"
    // in libpp/xml_utils when we try to link. Boo.
    struct opd_header header;
    int header_size = read(fd, &header, sizeof(header));
    close(fd);

    if (header_size != sizeof(header)) {
      m_valid = false;
      return false;
    }
    else {
      if (header.version != OPD_VERSION) {
        m_valid = false;
        if (Application::show_sample_files_skipped()) {
          std::cerr << "OProfile sample file header version mismatch for: " << m_pathname << "\n"
                    << "Header version is: " << header.version << ", "
                    << "expected: " << OPD_VERSION << "\n";
        }
        else {
          result = true;
        }
      }
    }
  }

  return result;
}


