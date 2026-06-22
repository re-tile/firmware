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
// SampleData.cc -- SampleData class
// ============================================================================

#include "SampleData.h"

// C/C++ includes

// custom includes


// ----------------------------------------------------------------------------
// SampleData
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
SampleData::SampleData(int event_id, int task_id, int cpu_id) :
  m_event_id(event_id),
  m_task_id(task_id),
  m_cpu_id(cpu_id)
{
}

/** Copy constructor. */
SampleData::SampleData(const SampleData& sampledata) :
  m_event_id(sampledata.m_event_id),
  m_task_id(sampledata.m_task_id),
  m_cpu_id(sampledata.m_cpu_id),
  m_frames(sampledata.m_frames)
{
}

/** Copy constructor. */
SampleData::SampleData(const SampleData* sampledata) :
  m_event_id(sampledata->m_event_id),
  m_task_id(sampledata->m_task_id),
  m_cpu_id(sampledata->m_cpu_id),
  m_frames(sampledata->m_frames)
{
}

/** Assignment operator. */
const SampleData&
SampleData::operator=(const SampleData& sampledata)
{
  if (&sampledata != this) // self-assignment guard
  {
    m_event_id  = sampledata.m_event_id;
    m_task_id   = sampledata.m_task_id;
    m_cpu_id    = sampledata.m_cpu_id;
    m_frames    = sampledata.m_frames;
  }
  return *this;
}

/** Assignment operator. */
const SampleData&
SampleData::operator=(const SampleData* sampledata)
{
  return operator=(*sampledata);
}

/** Destructor. */
SampleData::~SampleData()
{}


// --- object methods ---

/** Equality test. */
bool
SampleData::equals(const SampleData& sampledata) const
{
  return(
    m_event_id  == sampledata.m_event_id &&
    m_task_id   == sampledata.m_task_id &&
    m_cpu_id    == sampledata.m_cpu_id &&
    m_frames.size() == sampledata.m_frames.size()
  );
}

/** Equality operator. */
bool
SampleData::operator==(const SampleData& sampledata) const
{
  return equals(sampledata);
}


// --- accessors ---



// --- methods ---

/** Creates and adds stack frame. */
Frame*
SampleData::add_frame(int location_id, int count)
{
  Frame* frame = new Frame(location_id, count);
  m_frames.add(frame);
  return frame;
}
