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

// system includes
#include <sys/time.h>

// custom includes
#include "io.h"                  // IO streams
#include "string_utils.h"        // C/C++ strings
#include "foreach.h"             // FOR_EACH() macro
#include "Pathname.h"            // Unix pathnames
#include "SharedPointer.h"       // shared pointer class
#include "Vector.h"              // Vector class
#include "Map.h"                 // Map class
#include "utils.h"               // to_string(bool)
#include "xml.h"                 // XML support

// application includes
#include "arguments.h"           // argument processing
#include "global_options.h"      // global flag variables
#include "SampleFilePathname.h"  // OProfile sample file pathname class
#include "SampleFile.h"          // OProfile sample file class
#include "SymbolFile.h"          // binary symbol file
#include "SymbolFileManager.h"   // binary symbol file management
#include "OutputWriter.h"        // output writer
#include "StatisticDescriptorIndex.h" // statistic descriptor class
#include "MonitorDataFile.h"     // monitor extra metadata file
#include "Time.h"                // time abstraction


// -------------------------------------------------------------------------
// process_sample_file()
// -------------------------------------------------------------------------

/** Processes a single OProfile sample file to collect data */
void process_sample_file(const Pathname&     sample_file_path,
                         const int&          session_number,
                         SymbolFileManager&  symbol_file_manager,
                         SampleFileVector&   sample_file_vector)
{
  // Pre-check sample file pathname, to avoid creating unnecessary
  // SampleFile objects
  // [4877] if sample filename is not parseable, we just skip it
  // to avoid issues,
  // for example if profile-capture snags an NFS temp file, etc. by accident
  SampleFilePathname sample_file_pathname(sample_file_path);
  if (! sample_file_pathname.valid()) {
    if (g_show_skipped) {
      cerr << "NOTE: skipping unparseable filename in sample directory: "
           << sample_file_pathname << endl;
    }
    return;
  }

  // check for filtered pathnames
  if (! g_filter_pid.empty()) {
    if (sample_file_pathname.process_id() != g_filter_pid) return;
    if (g_show_skipped) {
      cout << "Skipped sample file because it doesn't match PID '"
           << g_filter_pid << "': "
           << sample_file_pathname;
    }
  }
  if (! g_filter_event.empty()) {
    if (sample_file_pathname.event_name() != g_filter_event) return;
    if (g_show_skipped) {
      cout << "Skipped sample file because it doesn't match event name '"
           << g_filter_pid << "': "
           << sample_file_pathname;
    }
  }

  if (g_show_sample_files) {
    if (sample_file_pathname.is_callgraph_file()) {
      cout << "Sample File (cg):     " << sample_file_pathname << endl;
    }
    else {
      cout << "Sample File (non-cg): " << sample_file_pathname << endl;
    }
    cout << "    Binary:  " << sample_file_pathname.executable_pathname()
         << endl;
    if (sample_file_pathname.is_callgraph_file()) {
       cout << "    From:    " << sample_file_pathname.from_module_pathname()
            << endl;
       cout << "    To:      " << sample_file_pathname.to_module_pathname()
            << endl;
    }
    else {
       cout << "    Path:    " << sample_file_pathname.from_module_pathname()
            << endl;
    }
    cout << "    Event:   " << sample_file_pathname.event_name() << endl;
    cout << "    Process: " << sample_file_pathname.process_id() << endl;
    cout << "    Thread:  " << sample_file_pathname.thread_id() << endl;
    cout << "    CPU:     " << sample_file_pathname.cpu_id() << endl;
  }

  // open sample file
  SampleFile sample_file(sample_file_pathname, session_number,
                         symbol_file_manager);
  if (! sample_file.is_valid()) {
    if (g_show_skipped) {
      cerr << "Could not open sample file: " << sample_file << endl;
    }
    return;
  }

  sample_file_vector.add(sample_file);
}


// -------------------------------------------------------------------------
// process_sample_directory()
// -------------------------------------------------------------------------

/** Processes all OProfile sample files in the specified sample directory */
void process_sample_directory(const Pathname&     sample_directory_path,
                              const int&          session_number,
                              const Pathname&     monitor_xml_path,
                              XMLDocument*&       target_spec,
                              SymbolFileManager&  symbol_file_manager,
                              SampleFileVector&   sample_file_vector)
{
  if (sample_directory_path.empty())
  {
    cout << "Processing OProfile data directory "
         << "'/var/lib/oprofile/samples/current'..." << endl;
  }
  else
  {
    cout << "Processing sample directory '"
         << sample_directory_path << "'..." << endl;
  }

  // Default monitor metadata pathname, if needed.
  Pathname monitor_xml_pathname = monitor_xml_path;
  if (! monitor_xml_pathname.exists())
  {
    if (monitor_xml_pathname.is_relative())
    {
      monitor_xml_pathname.make_absolute(sample_directory_path);
    }
  }

  // get monitor metadata
  string errors;
  MonitorDataFile monitor_data(monitor_xml_pathname, errors);
  if (errors != "") {
    cerr << errors;
    return;
  }

  // get chip spec for this run
  // TODO: compare with target_spec from prior run(s) to make sure all sample
  // directories are on same hardware configuration
  target_spec = monitor_data.get_target_spec();
  
  // collect binary remote/local paths, store in symbol_file_manager
  PathnameMap binaries;
  monitor_data.get_binary_path_map(binaries);
  symbol_file_manager.add_path_mappings(binaries);

  // walk sample files in this directory
  Pathname oprofile_sample_directory_path = 
    (starts_with(sample_directory_path, "/var/lib/oprofile/samples")) ?
    sample_directory_path :
    sample_directory_path + "/var/lib/oprofile/samples/current";

  walk_pathname_tree_files(
    oprofile_sample_directory_path, process_sample_file,
    session_number, symbol_file_manager, sample_file_vector);
}


// -------------------------------------------------------------------------
// main()
// -------------------------------------------------------------------------

/** main function */
int main(int argc, char **argv)
{
  int status = 0;


  // --- process command line arguments ---

  // storage for command line arguments
  Pathname       profile_output_file;
  Pathname       monitor_xml_pathname  = "monitor.xml";
  Pathname       profile_metadata_path = "ProfileMetadata.xml";
  PathnameVector sample_directory_paths;

  // process the command line arguments
  if (! process_arguments(argc, argv, status,
                          profile_output_file,
                          monitor_xml_pathname,
                          profile_metadata_path,
                          sample_directory_paths))
  {
    // process_arguments displays its own error messages
    return status;
  }

  if (g_split_across_cpus) {
    printf("Splitting processes across cpus...\n");
  }

#ifdef __tile__
  if (g_use_remote_local_mappings) {
    printf("Note: using remote/local mappings by user request.\n");
    printf("This is normally not required when running natively.\n");
  }
#else
  if (! g_use_remote_local_mappings) {
    printf("Note: disabling remote/local mappings by user request.\n");
    printf("Symbols will not be found for files not found locally.\n");
  }
#endif


  // --- initialize debuginfo directory/root paths ---

  // Tell OProfile's BFD wrapper what directory paths to use
  // when looking up GDB debuginfo (.debug) files for libraries, etc.

#ifdef __tile__

  // On TILE:
  // - We can default to the "normal" /usr/lib/debug directory.
  //   OProfile does this already.
  // - We don't need to set an alternate root directory
  //   since we're using the "normal" /usr/lib/debug directory.

#else

  // On x86 (cross-profiling):
  // - We can default to the "normal" /usr/lib/debug directory,
  //   OProfile does this already.
  // - We default to current MDE "tile" directory as root for debuginfo,
  //   unless user has explictly set something else.
  if (g_debuginfo_root_directory.empty()) {
    g_debuginfo_root_directory = Pathname::get_install_tile_dir_path();
  }

#endif

  if (! g_debuginfo_directory.empty()) {
    set_debuginfo_directory(g_debuginfo_directory.to_string());
  }

  if (! g_debuginfo_root_directory.empty()) {
    set_debuginfo_root_directory(g_debuginfo_root_directory.to_string());
  }


  // --- open output file ---

  // attempt to open output file, complain if we can't
  ofstream fout(profile_output_file.c_str());
  if (! fout) {
    cerr << "Could not open output file path for writing:" << endl;
    cerr << profile_output_file << endl;
    return (status = -1);
  }


  // --- load statistic metadata ---

  // Default statistic metadata pathname, if needed.
  if (! profile_metadata_path.exists())
  {
    if (profile_metadata_path.is_relative())
    {      
      // Try defaulting to same directory as profile file.
      Pathname default_profile_metadata_path =
        Pathname(profile_output_file.parent_path(),
                 profile_metadata_path);
      if (default_profile_metadata_path.exists())
      {
        profile_metadata_path = default_profile_metadata_path;
      }
      // Try defaulting to /etc directory next to command's /bin directory.
      else
      {
        Pathname sibling_etc_path = 
          Pathname::get_install_etc_dir_path(profile_metadata_path);
        if (sibling_etc_path.exists())
        {
          profile_metadata_path = sibling_etc_path;
        }
#ifdef __tile__
        else {
          // Try defaulting to root /etc directory, if we're on tile.
          Pathname root_etc_path = Pathname("/etc", profile_metadata_path);
          if (root_etc_path.exists())
          {
            profile_metadata_path = root_etc_path;
          }
        }
#endif
      }
    }
  }

  StatisticDescriptorIndex index;

  // load statistics metadata file into statistic descriptor index
  string errors;
  bool okay = index.load_statistics(profile_metadata_path, errors);
  if (! okay) {
    cerr << "Could not read statistics metadata from file '"
         << profile_metadata_path << "'" << endl;
    cerr << errors;
    return (status = -1);
  }


  // --- process sample directories ---

  // storage for data we collect from the OProfile directories

  // target description (i.e. "<chip>" node and friends)
  XMLDocument*             target_spec = NULL; // might not be found

  // list of binary symbol files referenced by OProfile data
  SymbolFileManager        symbol_file_manager;

  // list of sample files from OProfile data
  SampleFileVector         sample_file_vector;  

  // walk sample directories, collecting monitor data
  // and processing sample files
  cout << "Processing sample directories..." << endl;
  Time startTime;
  int session_number = 0;
  FOR_EACH(const_iterator, i, PathnameVector, sample_directory_paths)
  {
    const Pathname& sample_directory_path = (*i);
    process_sample_directory(sample_directory_path, ++session_number,
                             monitor_xml_pathname, 
                             target_spec, symbol_file_manager,
                             sample_file_vector);
  }

  // If we didn't find a monitor.xml file anywhere,
  // something's wrong, either it wasn't generated or user
  // didn't supply one.
  if (target_spec == NULL) {
    cout << "No monitor.xml file found in any sample directory.\n";
    cout << "Cannot generate a proper profile XML file.\n";
    cout << "(Hint: when running this command natively,\n"
         << "use -m to specify a monitor.xml file created by\n"
         << "tile-monitor's --generate-monitor-xml command.)\n";
    exit(-1);
  }

  // preprocess the data to collect binary and process information
  BinaryPtrVector binaries;
  find_binaries_in_sample_files(sample_file_vector, binaries);

  ProcessPtrVector processes;
  find_processes_in_sample_files(sample_file_vector, processes);

  Time processSamplesTime = Time() - startTime;
  cout << "Processed all sample directories." << endl;


  // --- construct and write output profile.xml file ---

  // Write the output, timing how long that takes:
  cout << "Writing profile output to "
       << "'" << profile_output_file << "'..." << endl;
  startTime = Time();
  OutputWriter outputWriter(fout, index, target_spec,
                            sample_file_vector, binaries, processes);
  outputWriter.write();
  Time writeOutputTime = Time() - startTime;
  cout << "Profile output completed." << endl;


  // --- wrap up ---

  // If we're running with --debug option, report how long things took:
  if (g_debug) {
    cout << endl;
    cout << "Timing (secs):" << endl;
    cout << "    To process all samples:    "
         << processSamplesTime.to_string() << endl;
    cout << "    To write output:           "
         << writeOutputTime.to_string() << endl;
  }

  return status;
}
