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
// SampleData.h -- SampleData class
// ============================================================================

// multiple-inclusion guard
#ifndef SAMPLEDATA_H
#define SAMPLEDATA_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH
#include "Frame.h"              // Frame class


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class SampleData;

/** Array of SampleData instances. */
typedef Array<SampleData> SampleDataArray;


// ----------------------------------------------------------------------------
// SampleData
// ----------------------------------------------------------------------------

/** SampleData class. */
class SampleData
{
  // --- members ---
protected:
  /** Event id. */
  int m_event_id;

  /** Task id. */
  int m_task_id;

  /** CPU id. */
  int m_cpu_id;

  /** Frame list. */
  Array<Frame*> m_frames;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  SampleData(int event_id, int task_id, int cpu_id);

  /** Copy constructor. */
  SampleData(const SampleData& sampledata);

  /** Copy constructor. */
  SampleData(const SampleData* sampledata);

  /** Assignment operator. */
  const SampleData& operator=(const SampleData& sampledata);

  /** Assignment operator. */
  const SampleData& operator=(const SampleData* sampledata);

  /** Destructor. */
  ~SampleData();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const SampleData& sampledata) const;

  /** Equality operator. */
  bool operator==(const SampleData& sampledata) const;


  // --- accessors ---
public:
  /** Gets event id. */
  int
  get_event_id() const
  {
    return m_event_id;
  }

  /** Gets task id. */
  int
  get_task_id() const
  {
    return m_task_id;
  }

  /** Gets cpu id. */
  int
  get_cpu_id() const
  {
    return m_cpu_id;
  }

  /** Gets list of frames. */
  Array<Frame*>&
  get_frames()
  {
    return m_frames;
  }


  // --- methods ---
public:

  /** Creates and adds stack frame. */
  Frame*
  add_frame(int location_id, int count);

};

// multiple-inclusion guard
#endif
