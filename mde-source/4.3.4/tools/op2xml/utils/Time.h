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
// Time.h -- Represents a non-negative time value in microseconds
// ==========================================================================

// multiple-inclusion guard
#ifndef TIME_H
#define TIME_H

// custom includes
#include "string_utils.h"


// --------------------------------------------------------------------------
// Time
// --------------------------------------------------------------------------

/**
 * Represents a non-negative time value in microseconds
 */
class Time
{
  // --- members ---
 private:
  /** The number of whole seconds */
  unsigned long m_seconds;

  /** The number of microseconds beyond "m_seconds" */
  unsigned long m_microseconds;


  // --- constructors/destructors ---
 public:
  /** Constructor which captures the current time at the
      moment of construction */
  Time();

  /** Constructor */
  Time (unsigned long seconds, unsigned long microseconds);


  // --- member functions ---
 public:
  /** Returns the whole number of seconds */
  const unsigned long seconds() const { return m_seconds; }

  /** Returns the number of microseconds beyond "seconds()" */
  const unsigned long microseconds() const { return m_microseconds; }

  /** Return true iff this time is less than "t" */
  bool less_than (const Time& t) const {
    return (m_seconds <  t.m_seconds)
       || ((m_seconds == t.m_seconds)
           && (m_microseconds < t.m_microseconds));
  }

  /** Adds "startTime" to this Time */
  Time operator+ (const Time& t) const {
    return Time (m_seconds + t.m_seconds, m_microseconds + t.m_microseconds);
  }

  /** Subtracts "startTime" from this Time */
  Time operator- (const Time& startTime) const;

  /** Returns a string of seconds in the format, "seconds.microseconds" */
  const string to_string() const;

  /** Returns a string of seconds in the format, "seconds.microseconds" */
  const char* c_str() const { return to_string().c_str(); }
};

// multiple-inclusion guard
#endif
