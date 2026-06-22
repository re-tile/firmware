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
// Frame.h -- stack frame class
// ============================================================================

// multiple-inclusion guard
#ifndef FRAME_H
#define FRAME_H

// C/C++ includes
#include <stdint.h>   // uint64_t

// custom includes
#include "Pathname.h"
#include "Function.h"
#include "HasStatistics.h"
#include "perf_api.h"


// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------

/** Represents a single stack frame. */
class Frame : public HasStatistics
{
  // --- constants ---
protected:
  static const std::string UNKNOWN_FUNCTION_NAME;


  // --- members ---
protected:
  /** Address. */
  uint64_t m_address;

  /** Function at this location, if known. */
  Function* m_function;

  /** Whether we have debug info (source file/line). */
  bool m_has_debug_info;

  /** Source file at this location, if known. */
  Pathname m_source_file;

  /** Source file line at this location, if known. */
  unsigned int m_source_line;

  /** List of child stack frame(s). */
  Array<Frame*> m_frames;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Frame(uint64_t address,
        Function* function,
        const Pathname& source_file = "",
        unsigned int source_line = 0);

protected:
  /** Copy constructor */
  Frame(const Frame& obj);

  /** Assignment operator */
  Frame& operator=(const Frame& obj);

public:
  ~Frame();


  // --- accessors ---
public:
  /** Gets address. */
  uint64_t
  get_address()
  {
    return m_address;
  }

  /** Gets function. */
  Function*
  get_function()
  {
    return m_function;
  }

  /** Gets function id. */
  int
  get_function_id()
  {
    return (m_function == NULL) ? 0 : m_function->get_id();
  }

  /** Gets function name. */
  const std::string&
  get_function_name()
  {
    return (m_function == NULL) ?
      UNKNOWN_FUNCTION_NAME :
      m_function->get_name();
  }

  /** Gets whether we have debug info (source file/line). */
  bool
  has_debug_info()
  {
    return m_has_debug_info;
  }

  /** Gets source file pathname. */
  const Pathname&
  get_source_file()
  {
    return m_source_file;
  }

  /** Gets source file line number. */
  unsigned int
  get_source_line()
  {
    return m_source_line;
  }

  /** Returns list of child frames. */
  Array<Frame*>& get_frames()
  {
    return m_frames;
  }

  /** Returns list of child frames. */
  const Array<Frame*>& get_frames() const
  {
    return m_frames;
  }


  // --- methods ---
public:

  /** Compares frame for equality with sample frame data. */
  bool operator==(const perf_sample_frame& sample_frame) const;

};


// multiple-inclusion guard
#endif
