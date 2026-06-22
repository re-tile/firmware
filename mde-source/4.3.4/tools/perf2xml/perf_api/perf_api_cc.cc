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
// perf_api_cc.cc -- Perf Events C/C++ Bridge API
// ============================================================================

#include "perf_api.h" // C++ API declarations

// custom includes


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// ----------------------------------------------------------------------------
// SampleHandler
// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------
// sample_file_sample_callback
// ----------------------------------------------------------------------------

/** Current SampleFile, used by sample_file_sample_callback function. */
SampleFile* g_current_sample_file = NULL;

/** Current SampleHandler, used by sample_file_sample_callback function. */
SampleHandler* g_current_sample_handler = NULL;

/** Internal callback, invoked from for_each_sample() method. */
int sample_file_sample_callback(perf_sample_data* data)
{
  // Invoke C++ handler on C++ sample data.
  return g_current_sample_handler->process_sample(data);
}


// ----------------------------------------------------------------------------
// SampleFile
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
SampleFile::SampleFile(const Pathname& pathname) :
  m_pathname(pathname)
{}

/** Copy constructor */
SampleFile::SampleFile(const SampleFile& obj) :
  m_pathname(obj.m_pathname)
{}

/** Assignment operator */
SampleFile&
SampleFile::operator=(const SampleFile& obj)
{
  if (this != &obj) // handle self-assignment
  {
    m_pathname = obj.m_pathname;
  }
  return *this;
}

SampleFile::~SampleFile()
{
}


// --- accessors ---

/** Gets sample file pathname. */
const Pathname&
SampleFile::get_pathname()
{
  return m_pathname;
}


// --- methods ---

/** Invokes the specified callback for each sample event in the file. */
void SampleFile::for_each_sample(SampleHandler* handler)
{
  // Set global state used by callback.
  g_current_sample_handler = handler;

  // Use C API to open sample file and walk its events.
  process_sample_file(m_pathname.c_str(),
                      &sample_file_sample_callback);

  // Clear global state used by callback.
  g_current_sample_handler = NULL;
}

