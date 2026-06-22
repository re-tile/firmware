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
SampleFilePathname::SampleFilePathname(const std::string& path) : Pathname(path)
{
  init();
}

/** Constructor */
SampleFilePathname::SampleFilePathname(const Pathname& path) : Pathname(path)
{
  init();
}


/** Init method */
void
SampleFilePathname::init()
{
  m_valid = false;
  try {
    m_parsed_path = parse_filename(to_string(), extra_images());
    m_valid = true;
  }
  catch (...) {
  }
  if (m_valid) {
    m_event_interval = to_long(m_parsed_path.count);
    m_event_mask     = to_int(m_parsed_path.unitmask);
  }
}


// --- accessors ---

/** Whether filename was successfully parsed */
const bool
SampleFilePathname::is_valid() const {
  return m_valid;
}

/** The full path of the sample file, as used to construct this instance */
const std::string
SampleFilePathname::get_pathname() const
{
  return to_string();
}

/** For all sample files, the executing binary */
const std::string&
SampleFilePathname::get_executable_pathname() const
{
  return m_parsed_path.image;
}


/** Whether this is a normal or call-graph sample file */
bool
SampleFilePathname::is_callgraph_file() const
{
  return (! m_parsed_path.cg_image.empty());
}


/** For non-call-graph sample files, the target binary
    that sample addresses are defined in */
const std::string&
SampleFilePathname::get_module_pathname() const
{
  return m_parsed_path.lib_image;
}


/** For call-graph sample files, the target binary
    that "from" addresses are defined in */
const std::string&
SampleFilePathname::get_from_module_pathname() const
{
  return m_parsed_path.lib_image;
}

/** For call-graph sample files, the target binary
    that "to" addresses are defined in */
const std::string&
SampleFilePathname::get_to_module_pathname() const
{
  return m_parsed_path.cg_image;
}


/** Event name */
const std::string&
SampleFilePathname::get_event_name() const
{
  return m_parsed_path.event;
}

/** Event sampling interval */
const long&
SampleFilePathname::get_event_interval() const
{
  return m_event_interval;
}

/** Event mask */
const int&
SampleFilePathname::get_event_mask() const
{
  return m_event_mask;
}

/** Process ID */
const std::string&
SampleFilePathname::get_pid() const
{
  return m_parsed_path.tgid;
}

/** Thread ID */
const std::string&
SampleFilePathname::get_tid() const
{
  return m_parsed_path.tid;
}

/** CPU ID */
const std::string&
SampleFilePathname::get_cpu_id() const
{
  return m_parsed_path.cpu;
}
