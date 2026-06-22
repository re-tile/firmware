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
// Application.h -- application class
// ============================================================================

// multiple-inclusion guard
#ifndef APPLICATION_H
#define APPLICATION_H

// C/C++ includes
#include <string>        // std::string

// custom includes
#include "perf_api.h"    // perf_events API
#include "Pathname.h"    // Unix/Linux pathnames
#include "Function.h"    // Function class
#include "Binary.h"      // Binary class
#include "Frame.h"       // Frame class
#include "Task.h"        // Task class
#include "collections.h" // collections & FOR_EACH macro
#include "StatisticDescriptorFile.h"  // statistic metadata file
#include "StatisticDescriptorIndex.h" // statistic metadata index
#include "xml.h"         // XML Document support


// ----------------------------------------------------------------------------
// Application
// ----------------------------------------------------------------------------

/** Application class. */
class Application :
  public SampleHandler
{
  // --- members ---
protected:
  /** List of sample file(s) to process. */
  Array<Pathname> m_sample_file_pathnames;

  /** Pathname of vmlinux file to use in place of [kernel.kallsyms]. */
  Pathname m_vmlinux_pathname;

  /** Monitor metadata XML file pathname. */
  Pathname m_monitor_xml_pathname;

  /** Profile statistic metadata XML file pathname. */
  Pathname m_statistic_xml_pathname;

  /** Remote -> Local pathname mappings, if any. */
  PathnameMap m_tile_to_host_pathname_mappings;

  /** Statistic descriptor index. */
  StatisticDescriptorIndex* m_statistics_index;

  /** Monitor XML target description. */
  XMLElement* m_target_spec;

  /** Session (aka sample file) ID counter. */
  int m_session_id;

  /** Output file pathname. */
  Pathname m_profile_pathname;


  /** Binary executables/libraries (and their contained functions). */
  Map<hash_key_t, Binary*> m_binaries;

  /** Function ID counter. */
  int m_function_id;


  /** Map from event ids to EVENT_NAMEs. */
  Map<int, std::string> m_event_names;

  /** Map from EVENT_NAMES to ids. */
  Map<std::string, int> m_event_ids;

  /** Map from event ids to event display names. */
  Map<int, std::string> m_event_display_names;

  /** Map from event ids to event total counts. */
  Map<int, long> m_event_counts;


  /** Task unique ID counter. */
  int m_task_id;

  /** Map from Task hashcode to Linux Task objects. */
  Map<hash_key_t, Task*> m_tasks;

  /** Whether to display verbose output. */
  bool m_verbose;

  /** Whether to show function names on frames.*/
  bool m_show_function_names_on_frames;

  /** Whether to show samples as we read them.*/
  bool m_show_samples;

  /** Whether to demangle C++ symbols.*/
  bool m_demangle_symbols;

  /** Whether to generate simpler XML output for debugging purposes.*/
  bool m_debug_xml;

  /** Whether to aggregrate stats for same task on multiple cpus. */
  bool m_aggregate_cpus;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Application();

protected:
  /** Copy constructor */
  Application(const Frame& obj)
  {}

  /** Assignment operator */
  Application& operator=(const Application& obj)
  { return *this; }

public:
  /** Destructor. */
  ~Application();


  // --- methods ---
public:
  /** Runs the application. */
  int
  run(int argc, char** argv);

  /** Processes command line arguments. */
  int
  process_arguments(int argc, char **argv);

  /** Processes a single sample from a sample file. */
  int
  process_sample(perf_sample_data* sample);

  /** Displays sample data (for debugging only). */
  void
  display_sample(perf_sample_data* sample);


  // --- helper methods ---
public:

  /** Finds/adds binary file. */
  Binary*
  add_binary(const Pathname& pathname);

  /** Finds/adds function. */
  Function*
  add_function(Binary* binary,
               const std::string& function_name, 
               const Pathname& source_file);

  /** Find matching frame, if any, for specified sample frame data. */
  Frame*
  find_matching_frame(Array<Frame*>& frames,
                      const perf_sample_frame& sample_frame);

  /** Finds/adds frame(s) as needed for current callstack.
      Returns last frame created/found. */
  Frame*
  add_frames(Array<Frame*>& frames,
             perf_sample_frame* callstack,
             int callstack_depth,
             int current_frame = -1);


  // --- debug XML generation methods ---
public:

  /** Generates simpler XML document from collected data, for debugging. */
  void
  generate_debug_xml(XMLDocument* document);

  /** Generates XML for specified tree of frames. */
  void
  generate_debug_frames_xml(XMLDocument* document,
                            XMLElement* parent, Array<Frame*>& frames);


  // --- official XML generation methods ---
public:

  /** Generates XML document from collected data. */
  void
  generate_xml(XMLDocument* document);

  /** Generates XML for specified tree of frames. */
  void
  generate_frames_xml(XMLDocument* document,
                      XMLElement* parent, Array<Frame*>& frames);

};


// multiple-inclusion guard
#endif
