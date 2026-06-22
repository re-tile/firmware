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
// Frame.cc -- Frame class
// ============================================================================

#include "Frame.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Frame::Frame(int location_id, int count) :
  m_location_id(location_id),
  m_count(count)
{
}

/** Copy constructor. */
Frame::Frame(const Frame& frame) :
  m_location_id(frame.m_location_id),
  m_count(frame.m_count)
{
}

/** Copy constructor. */
Frame::Frame(const Frame* frame) :
  m_location_id(frame->m_location_id),
  m_count(frame->m_count)
{
}

/** Assignment operator. */
const Frame&
Frame::operator=(const Frame& frame)
{
  if (&frame != this) // self-assignment guard
  {
    m_location_id = frame.m_location_id;
    m_count       = frame.m_count;
  }
  return *this;
}

/** Assignment operator. */
const Frame&
Frame::operator=(const Frame* frame)
{
  return operator=(*frame);
}

/** Destructor. */
Frame::~Frame()
{}


// --- object methods ---

/** Equality test. */
bool
Frame::equals(const Frame& frame) const
{
  return(
    m_location_id == frame.m_location_id &&
    m_count       == frame.m_count
  );
}

/** Equality operator. */
bool
Frame::operator==(const Frame& frame) const
{
  return equals(frame);
}


// --- accessors ---



// --- methods ---

