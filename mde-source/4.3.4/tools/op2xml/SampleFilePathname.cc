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
// SampleFilePathname.cc -- OProfile sample file pathname
// ==========================================================================

// header file
#include "SampleFilePathname.h"


// --------------------------------------------------------------------------
// SampleFilePathname
// --------------------------------------------------------------------------

/** OProfile sample file pathname */

// --- constuctors/destructors ---

/** Constructor */
SampleFilePathname::SampleFilePathname()
{
}

/** Constructor */
SampleFilePathname::SampleFilePathname(const char* path) : Pathname(path)
{
  init();
}

/** Constructor */
SampleFilePathname::SampleFilePathname(const string& path) : Pathname(path)
{
  init();
}

/** Constructor */
SampleFilePathname::SampleFilePathname(const Pathname& path) : Pathname(path)
{
  init();
}


/** Init method */
void SampleFilePathname::init()
{
  m_valid = false;
  try {
    m_parsed_path = parse_filename(to_string(), extra_images());
    m_valid = true;
  }
  catch (...) {
  }
  if (m_valid) {
    m_event_count = to_long(m_parsed_path.count);
  }
}


// --- accessors ---

/** Whether filename was successfully parsed */
const bool SampleFilePathname::valid() const {
  return m_valid;
}

/** The full path of the sample file, as used to construct this instance */
const string SampleFilePathname::pathname() const
{
  return to_string();
}

/** For all sample files, the executing binary */
const string& SampleFilePathname::executable_pathname() const
{
  return m_parsed_path.image;
}


/** Whether this is a normal or call-graph sample file */
bool SampleFilePathname::is_callgraph_file() const
{
  return (! m_parsed_path.cg_image.empty());
}


/** For non-call-graph sample files, the target binary
    that sample addresses are defined in */
const string& SampleFilePathname::module_pathname() const
{
  return m_parsed_path.lib_image;
}


/** For call-graph sample files, the target binary
    that "from" addresses are defined in */
const string& SampleFilePathname::from_module_pathname() const
{
  return m_parsed_path.lib_image;
}

/** For call-graph sample files, the target binary
    that "to" addresses are defined in */
const string& SampleFilePathname::to_module_pathname() const
{
  return m_parsed_path.cg_image;
}


/** Event name */
const string& SampleFilePathname::event_name() const
{
  return m_parsed_path.event;
}

const long& SampleFilePathname::event_interval() const
{
  return m_event_count;
}

/** Process ID */
const string& SampleFilePathname::process_id() const
{
  return m_parsed_path.tgid;
}

/** Thread ID */
const string& SampleFilePathname::thread_id() const
{
  return m_parsed_path.tid;
}

/** CPU ID */
const string& SampleFilePathname::cpu_id() const
{
  return m_parsed_path.cpu;
}
