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

// header file
#include "SampleFile.h"

// custom includes
#include "Map.h"
#include "Sample.h"
#include "global_options.h"

// C includes
#include <errno.h>
#include <string.h>
#include <fcntl.h> // open, read, O_RDONLY

// OProfile includes
#include <odb.h>               // odb_open(), etc.
#include <op_config.h>         // OPD_VERSION
#include <op_sample_file.h>    // opd_header, etc.

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
  const SampleFilePathname&  path,
  unsigned int               session_number,
  SymbolFileManager&         symbol_file_manager
)
  : m_pathname(path), m_session_number(session_number)
{
  // check sample file path validity
  // [4877] if sample filename is not parseable,
  // we just skip it to avoid issues,
  // for example if profile-capture snags an NFS temp file, etc. by accident
  m_valid = m_pathname.valid();
  if (! m_valid) {
    if (g_show_skipped) {
      cerr << "NOTE: skipping unparseable filename in sample directory: "
           << m_pathname << endl;
    }
    return;
  }


  // check whether this is a callgraph "{cg}" sample file
  // if non-callgraph file, we have single function offset,
  // and single "from" path
  // in callgraph file, we have from/to offsets/paths for
  // caller/callee functions
  m_is_callgraph_file = m_pathname.is_callgraph_file();


  // "from" path (for non-callgraph sample files, this is the only path)
  m_from_remote_pathname = m_pathname.from_module_pathname();
  m_from_symbol_file =
    symbol_file_manager.get_symbol_file(m_from_remote_pathname);

  // if we can't find symbol file,
  // skip these samples since we can't decode them
  if (m_from_symbol_file == NULL) {
    m_valid = false;
    if (g_show_skipped) {
      cerr << "NOTE: skipping sample file: " << m_pathname << endl;
      cerr << "Reason: can't find local symbol file for "
           << "'from' remote file: " << m_from_remote_pathname << endl;
    }
    return;
  }

  // "to" path, if any (only defined for callgraph sample files)
  if (! m_is_callgraph_file) {
    // copy "from" path
    m_to_remote_pathname = m_from_remote_pathname;
    m_to_symbol_file     = m_from_symbol_file;
  }
  else {
    m_to_remote_pathname = m_pathname.to_module_pathname();
    m_to_symbol_file =
      symbol_file_manager.get_symbol_file(m_to_remote_pathname);

    // if we can't find symbol file, skip these samples
    // since we can't decode them
    if (m_to_symbol_file == NULL) {
      m_valid = false;
      if (g_show_skipped) {
        cerr << "NOTE: skipping sample file: " << m_pathname << endl;
        cerr << "Reason: can't find local symbol file for "
             << "'to' remote file: " << m_to_remote_pathname << endl;
      }
      return;
    }
  }

  // now that we know we can access the symbol files,
  // attempt to open the samples file
  odb_t samples_db;

  // Check first if the sample file version is ok else
  // odb_open() can fail and the error message will be obscure.
  // NOTE: would like to use read_header() in libpp,
  // but this induces missing symbols when we try to link. Boo.
  int fd = open(m_pathname.c_str(), O_RDONLY);
  if (fd < 0) {
    m_valid = false;
    if (g_show_skipped) {
      cerr << "Could not open OProfile sample file header: " << m_pathname << "\n";
    }
    return;
  }

  struct opd_header header;
  int header_size = read(fd, &header, sizeof(header));
  close(fd);

  if (header_size != sizeof(header)) {
    m_valid = false;
    if (g_show_skipped) {
      cerr << "Could not read OProfile sample file header: " << m_pathname << "\n";
    }
    return;
  }

  if (header.version != OPD_VERSION) {
    m_valid = false;
    if (g_show_skipped) {
      cerr << "Could not open OProfile sample file: " << m_pathname << "\n"
           << "Reason: header version mismatch, got "
           << header.version << ", expected " << OPD_VERSION << "\n";
    }
    return;
  }

  int status = odb_open(&samples_db, m_pathname.c_str(),
                        ODB_RDONLY, sizeof(struct opd_header));
  m_valid = (0 == status);
  if (! m_valid) {
    if (g_show_skipped) {
      cerr << "Could not open OProfile sample file: " << m_pathname << "\n"
           << "Reason: (" << status << ") " << strerror(status) << "\n";
    }
    return;
  }

  // Note: This is a best guess as to how OProfile handles
  // sample addresses, based on looking at:
  // ../tools/oprofile/libpp/profile.cpp: set_offset()
  // There's also a brief discussion of this in the comment
  // for profile_t::start_offset in the header
  // .../tools/oprofile/libpp/profile.h

  // We need to track how the from/to offsets for all samples
  // in this file are represented:
  //  (a) user-space   -- offset (i.e. filepos) from start of executable/binary file
  //  (b) kernel-space -- offset from start of .text module in kernel space
  //  (c) anon         -- offset from start of arbitrary block of memory
  //                      (anon_start = block start address)
  // In callgraph data, the "from" and "to" function locations
  // can be in different parts
  // of the code, so we need to maintain the above info separately
  // for the "from" and "to" offsets.

  // get sample file header block, which has flags that determine
  // the above for this file;
  // opd_header is declared in ../tools/oprofile/libop/op_sample_file.h
  const opd_header& hdr =
    * static_cast<opd_header*> (odb_get_data(&samples_db));

  // "from" offset properties

  // flag indicating sample offset is in kernel space rather than user space
  m_from_kernel = hdr.is_kernel;

  // for "anon" samples, start of memory block that offset is relative to
  m_from_start_offset = hdr.anon_start;


  // "to" offset properties (if any, only defined for callgraph samples)

  if (m_is_callgraph_file) {
    // flag indicating sample offset is in kernel space rather than user space
    m_to_kernel       = hdr.cg_to_is_kernel;

    // for "anon" samples, start of memory block that offset is relative to
    m_to_start_offset = hdr.cg_to_anon_start;
  }
  else {
    // copy "from" properties, so both "from" and "to" methods
    // report the same data
    m_to_kernel       = m_from_kernel;
    m_to_start_offset = m_from_start_offset;
  }


  // collect sample data
  // samples are (key, value) pairs
  // - key is either:
  //   - for non-cg files, a single 32-bit offset
  //   - for cg files,     a 64-bit offset pair:
  //     ((from_offset<<32)|to_offset)
  // - value is the collected sample count

  // Use the following map to translate strings that summarize
  // the from and to code locations
  // to the index in "m_samples" of the corresponding Sample:
  typedef Map<string, int> SampleMap;
  SampleMap line_level_sample_map;

  odb_node_nr_t node_nr, pos;
  odb_node_t * node = odb_get_iterator(&samples_db, &node_nr);

  for (pos = 0; pos < node_nr; ++pos) {

    odb_key_t   key   = node[pos].key;
    odb_value_t value = node[pos].value;

    if (g_show_samples && g_show_sample_details) {
      if (m_is_callgraph_file) {
        cout << "  Sample raw data (cg): ";
      }
      else {
        cout << "  Sample raw data (non-cg): ";
      }

      cout << "key="  << in_hex(key) << ", ";

      cout << "from=" << in_hex(from_offset(key)) << ", "
           << "from_kernel=" << m_from_kernel << ", "
           << "from_start_offset=" << in_hex(m_from_start_offset)  << ", ";

      if (m_is_callgraph_file) {
        cout << "to=" << in_hex(to_offset(key)) << ", "
             << "to_kernel=" << m_to_kernel << ", "
             << "to_start_offset=" << in_hex(m_to_start_offset) << ", ";
      }

      cout << "value=" << value << endl;
    }

    Sample sample(key, value, m_is_callgraph_file,
         m_from_symbol_file, m_from_kernel, m_from_start_offset,
         m_to_symbol_file,   m_to_kernel,   m_to_start_offset);

    if (g_show_samples) {
      if (m_is_callgraph_file) {
        cout << "  Sample (cg): ";
      }
      else {
        cout << "  Sample (non-cg): ";
      }
      cout << "from="  << sample.from_location().to_debug_string() << ", "
           << "to="    << sample.to_location()  .to_debug_string() << ", "
           << "value=" << sample.value() << endl;

      if (g_show_sample_details) {
        // add extra whitespace to separate each sample from the next
        cout << endl;
      }
    }

    // HACK: discard wacky samples with a "to" of "_start",
    // since these create useless loops in the output
    if (sample.to_location().symbol_found() &&
        sample.to_location().symbol_name() == "_start")
    {
      if (g_show_samples) {
        cout << "NOTE: discarded sample with 'to' symbol of '_start'"
             << endl;
      }
      continue;
    }

    string map_key = sample.get_hash_key();
    int matched_samples_index = line_level_sample_map.get(map_key, -1);

    if (matched_samples_index == -1) {
      line_level_sample_map.add (map_key, m_samples.size());
      m_samples.add (sample);
    }
    else {
      m_samples[matched_samples_index].increment_value_by (value);
    }

  }

  // close the sample file
  odb_close(&samples_db);
}
