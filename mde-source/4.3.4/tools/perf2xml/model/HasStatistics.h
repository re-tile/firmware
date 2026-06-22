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
// HasStatistics.h -- profiling statistics interface
// ============================================================================

// multiple-inclusion guard
#ifndef HAS_STATISTICS_H
#define HAS_STATISTICS_H

// C/C++ includes
#include "collections.h"  // collections, FOR_EACH macros


// ----------------------------------------------------------------------------
// HasStatistics
// ----------------------------------------------------------------------------

/** An object (frame, etc.) that has associated profile event statistics. */
class HasStatistics
{
  // --- members ---
protected:
  /** Statistics store. */
  Map <int, long> m_statistics;
  

  // --- constructors/destructors ---
public:
  HasStatistics()
  {}

  /** Copy constructor */
  HasStatistics(const HasStatistics& obj) :
    m_statistics(obj.m_statistics)
  {}

  /** Assignment operator */
  HasStatistics& operator=(const HasStatistics& obj)
  {
    if (this != &obj) // handle self-assignment
    {
      m_statistics = obj.m_statistics;
    }
    return *this;
  }

  ~HasStatistics()
  {}


  // --- accessors ---
public:
  /** Sets statistic value in store. */
  void
  set_statistic(int event_id, long value)
  {
    m_statistics.put(event_id, value);
  }

  /** Increments statistic value in store by specified value. */
  void
  add_statistic(int event_id, long value = 1)
  {
    long v = m_statistics.get(event_id, 0);
    v += value;
    m_statistics.put(event_id, v);
  }

  /** Gets statistic value. */
  long
  get_statistic(int event_id)
  {
    return m_statistics.get(event_id, 0);
  }

  /** Gets statistic map. */
  const Map<int, long>&
  get_statistics()
  {
    return m_statistics;
  }


  // --- methods ---
public:

};


// multiple-inclusion guard
#endif
