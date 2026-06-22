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
// Frame.h -- Frame class
// ============================================================================

// multiple-inclusion guard
#ifndef FRAME_H
#define FRAME_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class Frame;

/** Array of frame instances. */
typedef Array<Frame> FrameArray;


// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------

/** Frame class. */
class Frame
{
  // --- members ---
protected:
  /** Location id. */
  int m_location_id;

  /** Sample count. */
  int m_count;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Frame(int location_id, int count = 0);

  /** Copy constructor. */
  Frame(const Frame& frame);

  /** Copy constructor. */
  Frame(const Frame* frame);

  /** Assignment operator. */
  const Frame& operator=(const Frame& frame);

  /** Assignment operator. */
  const Frame& operator=(const Frame* frame);

  /** Destructor. */
  ~Frame();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const Frame& frame) const;

  /** Equality operator. */
  bool operator==(const Frame& frame) const;


  // --- accessors ---
public:
  /** Gets location id. */
  int
  get_location_id() const
  {
    return m_location_id;
  }
  /** Sets id. */
  void
  set_id(int location_id)
  {
    m_location_id = location_id;
  }

  /** Gets sample count. */
  int
  get_count() const
  {
    return m_count;
  }
  /** Sets sample count. */
  void
  set_count(int count)
  {
    m_count = count;
  }
  /** Adds to sample count. */
  void
  add_count(int count)
  {
    m_count += count;
  }


  // --- methods ---
public:


};

// multiple-inclusion guard
#endif
