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
// SampleFile.h -- OProfile sample file
// ==========================================================================

// multiple-inclusion guard
#ifndef SAMPLE_FILE_H
#define SAMPLE_FILE_H

// OProfile includes
// oprofile build directory
#include <op_config.h>          // OPD_VERSION
// libutil++
#include <op_bfd.h>             // op_bfd, bfd_vma, etc.
// libdb
#include <odb.h>                // odb_open(), odb_value, etc.
// libop
#include <op_sample_file.h>     // opd_header, etc.

// custom includes
#include "SampleFilePathname.h" // SampleFile pathname class
#include "Sample.h"             // Sample class
#include "SymbolFileManager.h"  // symbol file manager
#include "SymbolFile.h"         // binary symbol file
#include "collections.h"        // collections, FOR_EACH
#include "io_utils.h"           // IO stream operator<< overload macro
#include "Pathname.h"           // Unix pathnames


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

/** List of samples */
typedef Array<Sample> SampleArray;


// --------------------------------------------------------------------------
// SampleFile
// --------------------------------------------------------------------------

/** OProfile sample file */
class SampleFile
{
  // --- members ---
private:
  /** Pathname of the sample file */
  SampleFilePathname m_pathname;

  /** The number of the OProfile session that includes this file */
  unsigned int m_session_number;

  /** Whether we were able to open the file */
  bool m_valid;


  /** Whether this is a callgraph sample file */
  bool m_is_callgraph_file;


  /** Pathname for the remote "from" file */
  Pathname m_from_pathname;

  /** Symbol file for "from" samples, if found */
  SymbolFilePtr m_from_symbol_file;

  /** whether from offsets are in kernel space */
  bool m_from_kernel;

  /** Base address for kernel/anonymous "from" samples. */
  bfd_vma m_from_start_offset;


  /** Pathname for the remote "to" file */
  Pathname m_to_pathname;

  /** Symbol file for "to" samples, if found */
  SymbolFilePtr m_to_symbol_file;

  /** whether to offsets are in kernel space */
  bool m_to_kernel;

  /** Base address for kernel/anonymous "to" samples. */
  bfd_vma m_to_start_offset;


  /** Sample data */
private:
  SampleArray m_samples;


  // --- constuctors/destructors ---
public:
  /** Constructor */
  SampleFile (
    const Pathname&            path,
    unsigned int               session_number,
    SymbolFileManager&         symbol_file_manager
  );

  /** Constructor */
  SampleFile (
    const SampleFilePathname&  path,
    unsigned int               session_number,
    SymbolFileManager&         symbol_file_manager
  );


protected:
  /** Init method */
  void
  init(const SampleFilePathname&  path,
       unsigned int               session_number,
       SymbolFileManager&         symbol_file_manager);

  /** Check OProfile header version, return false if it's
      not readable or not consistent with OProfile version in use. */
  bool
  sample_file_header_version_check(
    SampleFilePathname& m_pathname);


public:
  /** Destructor */
  ~SampleFile() { };


  // --- accessors ---
public:
  /** Returns the number of the OProfile session that includes this file */
  const unsigned int
  get_session_number() const
  {
    return m_session_number;
  };


  /** The sample file pathname */
  const SampleFilePathname&
  get_pathname() const
  {
    return m_pathname;
  };

  /** Whether the sample file exists */
  bool
  exists() const
  {
    return m_pathname.exists();
  };

  /** Whether the sample file was loaded successfully */
  bool
  is_valid() const
  {
    return m_valid;
  };


  /** Hardware event name */
  const std::string&
  get_event_name() const
  {
    return m_pathname.get_event_name();
  };

  /** Hardware event interval */
  const long&
  get_event_interval() const
  {
    return m_pathname.get_event_interval();
  };

  /** Hardware event mask */
  const int&
  get_event_mask() const
  {
    return m_pathname.get_event_mask();
  };

  /** Linux process ID */
  const std::string&
  get_pid() const
  {
    return m_pathname.get_pid();
  };

  /** Linux thread ID */
  const std::string&
  get_tid() const
  {
    return m_pathname.get_tid();
  };

  /** CPU ID */
  const std::string&
  get_cpu_id() const
  {
    return m_pathname.get_cpu_id();
  };


  /** Executable pathname */
  const std::string&
  get_executable_pathname() const
  {
    return m_pathname.get_executable_pathname();
  };


  /** Whether this is a call-graph sample file.
      Callgraph samples have "from/to" caller/callee offset pairs.
      Non-callgraph samples have a single offset. */
  bool
  is_callgraph_file() const
  {
    return m_is_callgraph_file;
  };


  /** "from" symbol file pathname */
  const Pathname&
  get_from_pathname() const
  {
    return m_from_pathname;
  };

  /** "from" symbol file, if it exists */
  const SymbolFilePtr&
  get_from_symbol_file() const
  {
    return m_from_symbol_file;
  };


  /** "to" symbol file pathname */
  const Pathname&
  get_to_pathname() const
  {
    return m_to_pathname;
  };

  /** "to" symbol file, if it exists */
  const SymbolFilePtr&
  get_to_symbol_file() const
  {
    return m_to_symbol_file;
  };


  /** Whether offset is in kernel space.
      For callgraph sample files, this is the same as is_from_kernel(). */
  bool
  is_kernel() const
  {
    return m_from_kernel;
  };

  /** Whether "from" offset is in kernel space. */
  bool
  is_from_kernel() const
  {
    return m_from_kernel;
  };

  /** Whether "to" offset is in kernel space. */
  bool
  is_to_kernel() const
  {
    return m_to_kernel;
  };


  /** Base address for kernel/anonymous samples.
      For callgraph sample files, this is the same as from_start_offset(). */
  bfd_vma
  get_start_offset() const
  {
    return m_from_start_offset;
  };

  /** Base address for kernel/anonymous from samples. */
  bfd_vma
  get_from_start_offset() const
  {
    return m_from_start_offset;
  };

  /** Base address for kernel/anonymous to samples. */
  bfd_vma
  get_to_start_offset() const
  {
    return m_to_start_offset;
  };


  /** Sample data */
  const SampleArray&
  get_samples() const
  {
    return m_samples;
  };
};


// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, SampleFile, samplefile)
{
  out << samplefile.get_pathname();
  return out;
}


// multiple-inclusion guard
#endif
