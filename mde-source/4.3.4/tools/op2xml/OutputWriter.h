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
// OutputWriter -- Writes the "tile-op2xml" output
// ==========================================================================

// multiple-inclusion guard
#ifndef OUTPUT_WRITER_H
#define OUTPUT_WRITER_H

// system includes
#include <iostream>
using std::ostream;

// custom includes
#include "xml.h"
#include "Binary.h"
#include "Process.h"
#include "Frame.h"

// application includes
#include "SampleFile.h"
#include "StatisticDescriptorIndex.h"


// --------------------------------------------------------------------------
// OutputWriter
// --------------------------------------------------------------------------

/**
 * Writes the "tile-op2xml" XML output
 */
class OutputWriter {

  // --- constants ---
 public:
  static const string OUTPUT_FORMAT_VERSION;

 private:
  /** The indentation width */
  static const unsigned int s_indentation_delta = 2;


  // --- members ---
 private:
  /** Output stream */
  ostream& m_out;

  /** Statistic descriptor index loaded from metadata */
  const StatisticDescriptorIndex& m_statistics_index;

  /** Target spec sub-document */
  const XMLDocument* m_target_spec;

  /** Chip width to use when calculating tile coordinates. */
  int m_tile_width;

  /** The list of sample files */
  const SampleFileVector& m_sample_files;

  /** Current indentation space count */
  unsigned int m_current_indentation;

  /** List of binaries */
  BinaryPtrVector m_binaries;

  /** List of processes */
  ProcessPtrVector m_processes;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  OutputWriter(ostream& out,
               const StatisticDescriptorIndex& statistics_index,
               const XMLDocument* target_spec,
               const SampleFileVector& sample_files,
               const BinaryPtrVector& binaries,
               const ProcessPtrVector& processes);


  // --- member functions ---
 public:
  /** Writes the "tile-op2xml" XML output */
  void write();

 private:
  /** Writes the "&lt;properties&gt;" XML element */
  void write_properties(const SampleFileVector& sample_files,
                        const StatisticDescriptorIndex& statistics_index);


  /** Writes description of target, obtained from monitor metadata */
  void write_target_spec();


  /** Writes the "&lt;binaries&gt;" XML element */
  void write_binaries();

  /** Writes a "&lt;binary&gt;" XML element */
  void write_binary(const Binary&  binary);

  /** Writes the "&lt;functions&gt;" XML element */
  void write_functions(const Binary& binary);

  /**
   * Writes the "&lt;function&gt;" XML element for the given SampleLocation,
   * if the function is known for the SampleLocation and if the XML element
   * for the function hasn't already been written
   */
  void write_function_for_location_if_necessary (const SampleLocation& loc);

  /** Writes the "&lt;function&gt;" XML element */
  void write_function(SymbolPtr symbol);


  /** Writes the "&lt;processes&gt;" XML element */
  void write_processes();

  /** Writes a "&lt;process&gt;" XML element */
  void write_process(Process& process);

  /** Writes a "&lt;tiles&gt;" XML element */
  void write_tiles(Process& process);

  /** Writes a "&lt;tile&gt;" XML element */
  void write_tile(int cpu_number);

  /** Writes a "&lt;call_tree&gt;" XML element */
  void write_call_tree(Process& process);

  /**
   * Writes the top-level "&lt;frame&gt;" XML elements (and their children)
   * for the given process' root call graph nodes
   */
  void write_frame_elements (Process& process);

  /** Writes the given "&lt;frame&gt;" XML element and its children,
      if any */
  void write_frame (Frame& frame);


  // --- utilities ---

  /** Returns the current indentation space count */
  inline unsigned int indentation() { return m_current_indentation; };

  /** Returns the current indentation delta in spaces */
  inline unsigned int indentation_delta() { return s_indentation_delta; };

  /** Increases the current indentation one level. */
  inline void indent() { m_current_indentation += s_indentation_delta; };

  /** Decreases the current indentation one level. */
  inline void outdent() { m_current_indentation -= s_indentation_delta; };
};

// multiple-inclusion guard
#endif
