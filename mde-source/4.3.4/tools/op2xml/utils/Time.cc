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
// op2xml -- OProfile raw data to XML formatter
// ==========================================================================

// header file
#include "Time.h"

// system includes
#include <stdexcept>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>


#define ONE_MILLION (1000000)

// -------------------------------------------------------------------------
// Time
// -------------------------------------------------------------------------


// --- constructors ---

/** Constructor which captures the current time at
    the moment of construction */
Time::Time()
{
  struct timeval time;
  gettimeofday (&time, NULL);
  m_seconds      = (unsigned long) time.tv_sec;
  m_microseconds = (unsigned long) time.tv_usec;
}

/** Constructor which captures the current time at the moment
    of construction */
Time::Time (unsigned long seconds, unsigned long microseconds)
{
  // Make sure any whole seconds portion of "microseconds"
  // is moved into "m_seconds":
  m_seconds      = (microseconds / ONE_MILLION) + seconds;
  m_microseconds =  microseconds % ONE_MILLION;
}


// --- member functions ---

/** Subtracts "startTime" from this Time */
Time Time::operator- (const Time& startTime) const
{
  if (less_than (startTime)) {
    throw std::underflow_error("Can't subtract a time from an earlier time");
  }
  else {
    unsigned long start_secs  = startTime.m_seconds;
    unsigned long start_msecs = startTime.m_microseconds;

    unsigned long end_secs  = m_seconds;
    unsigned long end_msecs = m_microseconds;

    // As with "by-hand" subtraction, "borrow 1" if necessary:
    if (end_msecs < start_msecs) {
      end_msecs += ONE_MILLION;
      end_secs  -= 1;
    }

    return Time (end_secs - start_secs, end_msecs - start_msecs);
  }
}

/** Returns a string of seconds in the format, "seconds.microseconds" */
const string Time::to_string() const
{
  std::stringstream ss;
  ss << m_seconds << '.';

  // Pad the remainder with 0's if necessary:
  char msecs_string[10];
  sprintf (msecs_string, "%lu", m_microseconds);
  for (int i=strlen(msecs_string); i<6; ++i) {
    ss << '0';
  }
  ss << msecs_string;
  return ss.str();
}
