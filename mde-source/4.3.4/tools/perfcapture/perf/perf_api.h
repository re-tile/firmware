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
// perf_api.h -- Perf Events C/C++ Bridge API
// ============================================================================

// multiple-inclusion guard
#ifndef PERF_API_H
#define PERF_API_H

// Perf uses this module name for OS symbols it cannot find elsewhere.
#define KERNEL_KALLSYMS_MODULE_NAME "[kernel.kallsyms]"

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// The perf_events structs and functions are not designed to be C++-compatible,
// so we define renamings and wrapperings here for C structs and functions
// that we need to use.

// While C/C++ code _can_ use these functions directly, we define a C++ API
// on top of these below.

// The C api code is defined in: perf_api_c.c

#ifdef __cplusplus
extern "C"
{
#endif

// C/C++ includes
#include <stdint.h> // uint64_t

// ----------------------------------------------------------------------------
// perf_sample_data
// ----------------------------------------------------------------------------

/** Addresses found in perf sample data. */
typedef uint64_t perf_vma;

/** perf_events callstack frame data. */
typedef struct perf_sample_frame
{
  perf_vma    address;        // virtual memory address for this frame
  const char* module;         // binary file pathname
  const char* symbol;         // symbol/function name, if known
  perf_vma    start_address;  // symbol start address
  perf_vma    end_address;    // symbol end address
} perf_sample_frame;

/** perf_events sample data. */
typedef struct perf_sample_data
{
  const char* sample_file; // pathname of source sample data file
  const char* module;      // "exe" module (outermost non-kernel module path)
  int cpu;                 // CPU id
  int pid;                 // Linux process ID
  int tid;                 // Linux thread ID
  int event_id;            // event ID   (i.e. hardware event ID)
  const char* event_name;  // event name (i.e. hardware event symbolic name)
  int callstack_depth;     // number of callstack frames (0th is sample frame)
  perf_sample_frame* callstack; // callstack frame(s)
} perf_sample_data;


// ----------------------------------------------------------------------------
// perf_sample_callback
// ----------------------------------------------------------------------------

/** Callback for process_sample_file() function. */
typedef int (*process_sample_callback) (perf_sample_data* sample);


// ----------------------------------------------------------------------------
// process_sample_file
// ----------------------------------------------------------------------------

/** Processes sample file */
int process_sample_file(
  const char* sample_file_pathname,
  process_sample_callback sample_callback
);

#ifdef __cplusplus
}
#endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// End of C API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// This is the C++ API built on top of the above C API.

// The C++ api code is defined in: perf_api_cc.cc

#ifdef __cplusplus

// custom includes
#include "Pathname.h"

// ----------------------------------------------------------------------------
// SampleHandler
// ----------------------------------------------------------------------------

/** Interface for sample handlers. */
class SampleHandler
{
  // --- methods ---
public:
  /** Processes a single sample from a sample file. */
  virtual int
  process_sample(perf_sample_data* data) = 0;

};


// ----------------------------------------------------------------------------
// SampleFile
// ----------------------------------------------------------------------------

/** Represents a single perf_events sample file. */
class SampleFile
{
  // --- members ---
protected:
  /** Sample file pathname. */
  Pathname m_pathname;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  SampleFile(const Pathname& pathname);

  /** Copy constructor */
  SampleFile(const SampleFile& obj);

  /** Assignment operator */
  SampleFile& operator=(const SampleFile& obj);

  ~SampleFile();

  
  // --- accessors ---
public:
  /** Gets sample file pathname. */
  const Pathname& get_pathname();


  // --- methods ---
public:
  /** Invokes the specified callback for each sample event in the file. */
  void for_each_sample(SampleHandler* handler);

};


// ifdef __cplusplus
#endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// End of C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// multiple-inclusion guard
#endif
