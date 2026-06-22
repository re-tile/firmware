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
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH
#include "Pathname.h"           // Pathname class

#include "perf_api.h"           // perf_events API, perf_vma

#include "Event.h"              // Event
#include "Binary.h"             // Binary
#include "Task.h"               // Task
#include "Location.h"           // Location
#include "SampleData.h"         // SampleData


// ----------------------------------------------------------------------------
// Application
// ----------------------------------------------------------------------------

/** Application class. */
class Application :
  public SampleHandler
{
  // --- static members ---
protected:
  /** Singleton application instance. */
  static Application* s_app;


  // --- static methods ---
public:
  /** Create and run application. */
  static int main(int argc, char** argv);

  /** Returns singleton Application instance. */
  static Application* get();


  // --- constants ---
public:
  /** Status returned on successful completion. */
  static const int STATUS_OK = 0;

  /** Status returned on argument usage error. */
  static const int STATUS_USAGE = 1;

  /** Status returned on error. */
  static const int STATUS_ERROR = -1;

  /** Default OProfile data archive path. */
  static Pathname DEFAULT_PERF_DATA_PATHNAME;


  // --- "global" settings ---
protected:
  /** Whether to show progress reading/writing data. */
  static bool s_show_progress;
public:
  /** Whether to show progress reading/writing data. */
  static bool show_progress();

protected:
  /** Whether to pretty-print profile XML. */
  static bool s_pretty_print;
public:
  /** Whether to pretty-print profile XML. */
  static bool pretty_print();

protected:
  /** Whether to print samples read from sample files. */
  static bool s_show_samples;
public:
  /** Whether to print samples read from sample files. */
  static bool show_samples();


  // --- members ---
protected:
  /** Whether to display verbose messages. */
  bool m_verbose;

  /** Profile output pathname. */
  Pathname m_profile_output_pathname;

  /** Events descriptor file pathname. */
  Pathname m_profile_events_file_pathname;

  /** Perf data file path(s). */
  Array<Pathname> m_profile_data_pathnames;

  /** Pathname of vmlinux file to use in place of [kernel.kallsyms]. */
  Pathname m_vmlinux_pathname;

  /** Default directory to use for relative pathnames. */
  Pathname m_current_directory;


  /** List of events, as read from event descriptor file. */
  Array<Event*> m_event_descriptors;

  /** Event descriptors by name. */
  Map<std::string, Event*> m_event_descriptors_by_name;

  /** Event descriptors by hardware event name (for those that have one). */
  Map<std::string, Event*> m_event_descriptors_by_hardware_event_name;


  /** List of events actually used in data files. */
  Array<Event*> m_events;

  /** Events by name. */
  Map<std::string, Event*> m_events_by_name;

  /** Events by hardware event name (for those that have one). */
  Map<std::string, Event*> m_events_by_hardware_event_name;


  /** Binaries found in sample data. */
  Map<std::string, Binary*> m_binaries;

  /** Unique IDs assigned to binaries. */
  Map<Binary*, int> m_binary_ids;

  /** ID to binary mapping. */
  Map<int, Binary*> m_id_to_binary;

  /** Next binary ID. */
  int m_next_binary_id;


  /** Symbols found in binary files. */
  Map<std::string, BinarySymbol*> m_binary_symbols;

  /** Unique IDs assigned to binary symbols. */
  Map<BinarySymbol*, int> m_binary_symbol_ids;

  /** Next binary symbol ID. */
  int m_next_binary_symbol_id;


  /** Tasks found in sample data. */
  Map<long, Task*> m_tasks;

  /** Unique IDs assigned to binaries. */
  Map<Task*, int> m_task_ids;

  /** Next binary ID. */
  int m_next_task_id;


  /** Locations found in sample data. */
  Map<std::string, Location*> m_locations;

  /** Unique IDs assigned to locations. */
  Map<Location*, int> m_location_ids;

  /** Next binary ID. */
  int m_next_location_id;


  /** Sample data "records" found in sample data. */
  Array<SampleData*> m_samples;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Application();

protected:
  /** Copy constructor */
  Application(const Application& obj)
  {}

  /** Assignment operator */
  Application& operator=(const Application& obj)
  { return *this; }

public:
  /** Destructor. */
  ~Application();


  // --- argument processing ---
protected:
  /** Processes command line arguments. */
  int
  process_arguments(int argc, char **argv);

  /** Processes command line argument.
   *  arg is current argument ("-x", "--name", or "value")
   *  n is the current argument index, and should be incremented
   *  if an argument consumes subsequent values in argv.
   *  Return value is status:
   *    0 = argument and any value(s) were parsed successfully
   *   <0 = error
   *   >0 = display usage and exit
   */
  int process_argument(const char* arg, int& n, int argc, char** argv);

  /** Displays usage message. */
  void display_usage_message(char* argv0);


  // --- application execution ---
public:
  /** Runs the application. */
  int
  run(int argc, char** argv);


  // --- archive processing ---
protected:

  /** Processes perf sample data file. */
  void
  process_perf_data_file(const Pathname& pathname);

  /** Processes a single sample from a sample file. */
  int
  process_sample(perf_sample_data* sample);

  /** Displays sample data (for debugging only). */
  void
  display_sample(perf_sample_data* sample);


  // --- file type detection support ---
protected:
  /** Determines file type of pathname. */
  std::string
  get_file_type(const std::string& pathname);


  // --- event handling ---
public:
  /** Loads event descriptor file. */
  int
  load_event_descriptors(const Pathname& event_descriptor_pathname);

  /** Caches event, returns assigned ID. */
  int
  cache_event(const std::string hardware_event_name, long interval, int mask);

  /** Finalizes list of events. */
  void
  finalize_event_list();


  // --- other state cacheing ---
protected:
  /** Caches binary, returns assigned ID. */
  int
  cache_binary(const std::string pathname);

  /** Gets binary with specified ID. */
  Binary*
  get_binary(int binary_id);

  /** Caches symbol, returns assigned ID. */
  int
  cache_symbol(int binary_id,
               Binary* binary,
               const std::string& symbol_name,
               perf_vma start_address,
               perf_vma size,
               perf_vma end_address);

  /** Caches task, returns assigned ID. */
  int
  cache_task(int pid, int tid, int binary_id);

  /** Caches location, returns assigned ID. */
  int
  cache_location(int binary_id,
                 const std::string& address,
                 int symbol_id,
                 const std::string& source_file,
                 int source_line);


  // --- XML output generation ---
protected:
  /** Generates XML output file. */
  int
  generate_xml_output(const Pathname& pathname);


  // --- utilities ---
protected:
  /** Prints current timestamp. */
  void
  print_current_time();

};



// multiple-inclusion guard
#endif
