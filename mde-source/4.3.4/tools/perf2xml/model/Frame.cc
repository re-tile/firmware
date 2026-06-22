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
// Frame.cc -- stack frame class
// ============================================================================

#include "Frame.h"


// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------

// --- constants ---

const std::string Frame::UNKNOWN_FUNCTION_NAME = "unknown";


// --- constructors/destructors ---

/** Constructor. */
Frame::Frame(uint64_t address,
             Function* function,
             const Pathname& source_file,
             unsigned int source_line) :
  HasStatistics(),
  m_address(address),
  m_function(function),
  m_source_file(source_file),
  m_source_line(source_line)
{
  m_has_debug_info = (! source_file.empty() && source_line > 0);
}

/** Copy constructor */
Frame::Frame(const Frame& obj)
{}

/** Assignment operator */
Frame&
Frame::operator=(const Frame& obj)
{
  if (this != &obj) // handle self-assignment
  {
  }
  return *this;
}

Frame::~Frame()
{
  m_function = NULL;

  FOR_EACH(iterator, it, Array<Frame*>, m_frames)
  {
    Frame* f = *it;
    delete f;
  }
  m_frames.clear();

}


// --- accessors ---


// --- methods ---

/** Compares frame for equality with sample frame data. */
bool
Frame::operator==(const perf_sample_frame& sample_frame) const
{
  uint64_t address   = sample_frame.address;

  return (m_address == address);
}
