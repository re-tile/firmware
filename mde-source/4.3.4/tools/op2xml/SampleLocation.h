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
// SampleLocation.h -- Source location with associated 
//                     symbol/filename/line-num information
// ==========================================================================

// multiple-inclusion guard
#ifndef SAMPLE_LOCATION_H
#define SAMPLE_LOCATION_H

// custom includes
#include "SymbolFile.h"

// OProfile includes
#include <op_bfd.h>             // op_bfd, bfd_vma, etc.


// --------------------------------------------------------------------------
// SampleLocation
// --------------------------------------------------------------------------

/**
 * A SampleLocation is constructed using information read from
 * an OProfile sample file, and it's purpose is to represent
 * the C-source-line location of a PC when possible, or --
 * when debug information isn't known - it represents the location
 * using the VMA.
 */
class SampleLocation {

  // --- members --
 private:
  /** SampleLocation's vma */
  bfd_vma m_vma;

  /** Symbol containing this SampleLocation */
  SymbolPtr m_symbol;

  /** Source line number which corresponds to this SampleLocation */
  unsigned int m_line;

  /** Path of the source file which contains this SampleLocation */
  Pathname m_path;

  /** Cached "to_string()" result */
  string m_as_string;

  /** Cached "to_debug_string()" result */
  string m_as_debug_string;
  

  // --- constructors/destructors ---
 public:
  /** Constructor */
  SampleLocation (const bfd_vma& offset, const SymbolFilePtr& symbol_file,
                  const bool in_kernel, const bfd_vma& start_offset);

  /** Destructor */
  ~SampleLocation()
  { }


  // --- accessors ---
 public:
  /** The vma. */
  bfd_vma vma() const { return m_vma; }

  /** Returns whether this SampleLocation has a known file path
      and line number. */
  bool known_source_file_and_line() const { return ! m_path.empty(); }

  /** The name of the Symbol containing this SampleLocation,
      or NULL if the Symbol wasn't found. */
  string symbol_name() const { 
    return symbol_found() ? m_symbol->name() : NULL; }

  /** The source line number which corresponds to this SampleLocation. */
  unsigned int line() const { return m_line; }

  /** The path of the source file which contains this SampleLocation. */
  const Pathname& path() const { return m_path; }

  /** Returns whether the Symbol containing the SampleLocation was found. */
  bool symbol_found() const { return m_symbol != NULL; }

  /** The Symbol containing this SampleLocation,
      or NULL if the Symbol wasn't found. */
  SymbolPtr symbol() const { return m_symbol; }

  /**
   * Returns a string representation of this SourceLocation.
   * Depending on what's known about the SampleLocation, that representation
   * can take one of three forms:
   *
   *     symName:src.c:lineNum
   *     symName:0xHEXADDR
   *     0xHEXADDR
   */
  const string& to_string() const { return m_as_string; }

  /**
   * Returns a debug string representation of this SourceLocation.
   * Depending on what's known about the SampleLocation, that representation
   * can take one of three forms:
   *
   *     symName:src.c:lineNum (pc, offset=0Xnnn)
   *     symName:0xHEXADDR, offset=0Xnnn
   *     0xHEXADDR, offset=0Xnnn
   */
  const string& to_debug_string() const { return m_as_string; }
};

// multiple-inclusion guard
#endif
