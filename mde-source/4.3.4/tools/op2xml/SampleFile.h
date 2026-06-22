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

// custom includes
#include "io.h"                 // IO stream operator<< overload macro
#include "Pathname.h"           // Unix pathnames
#include "Sample.h"             // Sample class
#include "SymbolFile.h"         // binary symbol file
#include "SampleFilePathname.h" // SampleFile pathname class
#include "SymbolFileManager.h"  // symbol file manager

// OProfile includes
#include <op_bfd.h>             // op_bfd, bfd_vma, etc.
#include <odb.h>                // odb_open(), odb_value, etc.
#include <op_sample_file.h>     // opd_header, etc.


// --------------------------------------------------------------------------
// SampleFile
// --------------------------------------------------------------------------

/** OProfile sample file */
class SampleFile
{
public:

  // type definitions
  typedef Vector<Sample> SampleList;

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


  /** whether from offsets are in kernel space */
  bool m_from_kernel;

  /** Symbol file for "from" samples */
  SymbolFilePtr m_from_symbol_file;

  /** Pathname for the remote "from" file */
  Pathname m_from_remote_pathname;

  /** Base address for kernel/anonymous "from" samples. */
  bfd_vma m_from_start_offset;


  /** whether to offsets are in kernel space */
  bool m_to_kernel;

  /** Symbol file for "to" samples */
  SymbolFilePtr m_to_symbol_file;

  /** Pathname for the remote "to" file */
  Pathname m_to_remote_pathname;

  /** Base address for kernel/anonymous "to" samples. */
  bfd_vma m_to_start_offset;


  /** Sample data */
private:
  SampleList m_samples;


  // --- constuctors/destructors ---
public:
  /** Constructor */
  SampleFile (
    const SampleFilePathname&  path,
    unsigned int               session_number,
    SymbolFileManager&         symbol_file_manager
  );


protected:
  /** Init method */
  void init();

public:
  /** Destructor */
  ~SampleFile() { };


  // --- accessors ---
public:
  /** Returns the number of the OProfile session that includes this file */
  const unsigned int session_number() const { return m_session_number; };

  /** The sample file pathname */
  const SampleFilePathname& pathname() const { return m_pathname; };

  /** The executable pathname */
  const string& executable_pathname() const {
    return m_pathname.executable_pathname(); };

  /** Whether the sample file exists */
  bool exists() const { return m_pathname.exists(); };

  /** Whether the sample file was loaded successfully */
  bool is_valid() const { return m_valid; };

  /** Whether this is a call-graph sample file.
      Callgraph samples have "from/to" caller/callee offset pairs.
      Non-callgraph samples have a single offset. */
  bool is_callgraph_file() const { return m_is_callgraph_file; };


  /** Event name */
  const string& event_name() const { return m_pathname.event_name(); };

  /** Process ID */
  const string& process_id() const { return m_pathname.process_id(); };

  /** Thread ID */
  const string& thread_id() const { return m_pathname.thread_id(); };

  /** CPU ID */
  const string& cpu_id() const { return m_pathname.cpu_id(); };

  /** "from" symbol file */
  const SymbolFilePtr& from_symbol_file() const {
    return m_from_symbol_file; };

  /** local "from" file path */
  const string from_local_path() const {
    return m_from_symbol_file->pathname(); };

  /** remote "from" file path */
  const string from_remote_path() const {
    return m_from_remote_pathname.to_string(); };

  /** "to" symbol file */
  const SymbolFilePtr& to_symbol_file() const { return m_to_symbol_file; };

  /** local "from" file path */
  const string to_local_path() const {
    return m_to_symbol_file->pathname(); };

  /** remote "to" file path */
  const string to_remote_path() const {
    return m_to_remote_pathname.to_string(); };


  /** Whether offsets are in kernel space.
      (This is the same as is_from_kernel() for callgraph sample files) */
  bool is_kernel() const { return m_from_kernel; };

  /** Whether from offsets are in kernel space. */
  bool is_from_kernel() const { return m_from_kernel; };

  /** Whether to offsets are in kernel space. */
  bool is_to_kernel() const { return m_to_kernel; };


  /** Base address for kernel/anonymous samples.
      (Same as from_start_offset() for callgraph sample files) */
  bfd_vma start_offset() const { return m_from_start_offset; };

  /** Base address for kernel/anonymous from samples. */
  bfd_vma from_start_offset() const { return m_from_start_offset; };

  /** Base address for kernel/anonymous to samples. */
  bfd_vma to_start_offset() const { return m_to_start_offset; };


  /** Loaded sample data */
  const SampleList& samples() const { return m_samples; };
};


// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, SampleFile, samplefile)
{
  out << samplefile.pathname();
  return out;
}


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

/** List of sample file content objects */
typedef Vector<SampleFile> SampleFileVector;

typedef const SampleFile*     SampleFilePtr;
typedef Vector<SampleFilePtr> SampleFilePtrVector;



// multiple-inclusion guard
#endif
