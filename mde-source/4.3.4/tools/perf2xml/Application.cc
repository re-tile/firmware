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
// Application.cc -- application class
// ============================================================================

#include "Application.h"

// C/C++ includes
#include <stdint.h>  // uint64_t
#include <stdlib.h>  // exit()

// custom includes
#include "MonitorDataFile.h"
#include "Pathname.h"
#include "string_utils.h"  // hash_value()


// ----------------------------------------------------------------------------
// Application
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
Application::Application() :
  m_statistics_index(NULL)
{
}

/** Destructor. */
Application::~Application()
{
  if (m_target_spec != NULL)
  {
    delete m_target_spec;
  }

  if (m_statistics_index != NULL)
  {
    delete m_statistics_index;
  }
  
  FOR_EACH_PAIR(const_iterator, it, Map<hash_key_t COMMA Binary*>, m_binaries)
  {
    Binary* binary = it->second;
    delete binary;
  }
  m_binaries.clear();

  FOR_EACH_PAIR(iterator, it, Map<hash_key_t COMMA Task*>, m_tasks)
  {
    Task* task = it->second;
    delete task;
  }
  m_tasks.clear();
}


// --- methods ---

/** Runs the application. */
int
Application::run(int argc, char** argv)
{
  if (m_verbose)
  {
    printf("Running perf2xml conversion utility...\n");
  }

  // Initial defaults.
  m_profile_pathname = "profile.xml";
  m_monitor_xml_pathname = "monitor.xml";
  m_statistic_xml_pathname = "ProfileMetadata.xml";
  m_vmlinux_pathname = "";
  m_target_spec = NULL;

  m_statistics_index = NULL;

  m_verbose = false;
  m_show_function_names_on_frames = false;
  m_show_samples = false;
  m_demangle_symbols = true;
  m_debug_xml = false;
  m_aggregate_cpus = true;

  // Process command-line arguments.
  int status = process_arguments(argc, argv);
  if (status < 0) return status;

  // Default vmlinux pathname, if needed.
  if (m_vmlinux_pathname.empty())
  {
    // On TILE, look for /boot/vmlinux directly.
    Pathname default_tile_vmlinux_pathname("/boot/vmlinux");
    if (default_tile_vmlinux_pathname.exists())
    {
      m_vmlinux_pathname = default_tile_vmlinux_pathname;
    }
    else
    {
      // On x86 host, look for it in the MDE install tree.
      Pathname default_host_vmlinux_pathname =
        Pathname::get_install_tile_dir_path("/boot/vmlinux");
      if (default_host_vmlinux_pathname.exists())
      {
        m_vmlinux_pathname = default_host_vmlinux_pathname;
      }
    }
  }

  if (m_verbose && ! m_vmlinux_pathname.empty())
  {
    printf("Using vmlinux path: %s\n", m_vmlinux_pathname.c_str());
  }

  // Default monitor metadata pathname, if needed.
  if (! m_monitor_xml_pathname.exists())
  {
    if (m_monitor_xml_pathname.is_relative())
    {
      if (m_sample_file_pathnames.size() > 0)
      {
        Pathname& first_sample_pathname = m_sample_file_pathnames.get(0);
        m_monitor_xml_pathname.make_absolute(
          first_sample_pathname.get_parent_path());
      }
      else
      {
        m_monitor_xml_pathname.make_absolute(
          m_profile_pathname.get_parent_path());
      }
    }
  }

  if (m_verbose && ! m_monitor_xml_pathname.empty())
  {
    printf("Using monitor.xml path: %s\n", m_monitor_xml_pathname.c_str());
  }

  // Default statistic metadata pathname, if needed.
  if (! m_statistic_xml_pathname.exists())
  {
    if (m_statistic_xml_pathname.is_relative())
    {      
      Pathname default_statistic_xml_pathname =
        Pathname(m_profile_pathname.get_parent_path(),
                 m_statistic_xml_pathname);
      if (default_statistic_xml_pathname.exists())
      {
        m_statistic_xml_pathname = default_statistic_xml_pathname;
      }
      else
      {
        m_statistic_xml_pathname =
          Pathname::get_install_etc_dir_path(m_statistic_xml_pathname);
      }
    }
  }

  if (m_verbose && ! m_statistic_xml_pathname.empty())
  {
    printf("Using ProfileMetadata.xml path: %s\n",
           m_statistic_xml_pathname.c_str());
  }

  if (m_verbose)
  {
    printf("Processing metadata...\n");
  }

  // Process ProfileMetadata.xml file, if any, for statistic metadata.
  std::string statistic_xml_errors;
  StatisticDescriptorFile* statistic_xml_file =
    new StatisticDescriptorFile(m_statistic_xml_pathname,
                                statistic_xml_errors);
  if (! statistic_xml_errors.empty())
  {
    printf("Could not read statistic descriptor file: %s\n%s",
           m_statistic_xml_pathname.c_str(),
           statistic_xml_errors.c_str());
    exit(1);
  }
  m_statistics_index = statistic_xml_file->get_statistic_index();
  if (m_statistics_index == NULL)
  {
    printf("Could not load statistic metadata from file: %s\n",
           m_statistic_xml_pathname.c_str());
    exit(1);
  }

  if (m_verbose)
  {
    printf("Processed ProfileMetadata.xml file...\n");
  }

  // Process monitor.xml file, if any, for TILE-specific data.
  std::string monitor_xml_errors;
  MonitorDataFile* monitor_xml_file =
    new MonitorDataFile(m_monitor_xml_pathname, monitor_xml_errors);
  if (! monitor_xml_errors.empty())
  {
    printf("Could not read monitor descriptor file: %s\n%s",
           m_monitor_xml_pathname.c_str(),
           monitor_xml_errors.c_str());
    exit(1);
  }
  m_target_spec = monitor_xml_file->get_target_spec();
  if (m_target_spec == NULL)
  {
    printf("Could not get target description from file: %s\n",
           m_statistic_xml_pathname.c_str());
    exit(1);
  }

  // Get any pathname mappings specified in monitor.xml file.
  PathnameMap mappings;
  if (monitor_xml_file->get_binary_path_map(mappings))
  {
    FOR_EACH_PAIR(const_iterator, it, PathnameMap, mappings)
    {
      const Pathname& tile_path = it->first;
      const Pathname& host_path = it->second;

      // Allow any mappings user specified on command line to "win"
      // over mappings we find in the monitor.xml file.
      if (! m_tile_to_host_pathname_mappings.contains(tile_path))
      {
        m_tile_to_host_pathname_mappings.put(tile_path, host_path);
      }
    }
  }

  if (m_verbose && ! m_tile_to_host_pathname_mappings.empty())
  {
    FOR_EACH_PAIR(const_iterator, it, PathnameMap, 
                  m_tile_to_host_pathname_mappings)
    {
      const Pathname& tile_path = it->first;
      const Pathname& host_path = it->second;
      printf("Symbol mapping: (host) %s \t== (tile) %s\n",
             host_path.c_str(), tile_path.c_str());
    }
  }

  if (m_verbose)
  {
    printf("Processing perf data files...\n");
  }

  // Initialize ID counters.
  m_session_id = 0;
  m_function_id = 0;
  m_task_id = 0;

  // Process each of the supplied files.
  FOR_EACH(const_iterator, it, Array<Pathname>, m_sample_file_pathnames)
  {
    // Each file gets its own session ID.
    ++m_session_id;

    const Pathname& sample_file_pathname = *it;
    if (m_verbose)
    {
      printf("Session #%i: sample file = %s\n",
             m_session_id, sample_file_pathname.c_str());
    }

    // invokes process_sample() for each sample in this sample file
    SampleFile* sample_file = new SampleFile(sample_file_pathname);
    sample_file->for_each_sample((SampleHandler*) this);
    delete sample_file;
  }

  if (m_verbose)
  {
    printf("Collecting event names...\n");
  }

  // Collect the set of event names we actually saw,
  // and prune the statistic descriptors to only include that set
  // plus any parents/dependencies.
  Array<std::string> event_names;
  m_event_names.get_values(event_names);
  m_statistics_index->remove_unused_event_stats(event_names);

  // Now add event IDs to remaining statistic descriptors.
  m_statistics_index->populate_event_ids(m_event_ids);

  if (m_verbose && ! m_event_names.empty())
  {
    printf("Found the following events:\n");
    FOR_EACH_PAIR(const_iterator, it, Map<int COMMA std::string>,
                  m_event_names)
    {
      int event_id = it->first;
      const std::string& event_name = it->second;
      printf("- %s (%i)\n", event_name.c_str(), event_id);
    }
  }

  if (m_verbose)
  {
    printf("Generating output XML...\n");
  }

  // Construct XML document.
  XMLDocument* document = new XMLDocument();
  if (m_debug_xml)
  {
    generate_debug_xml(document);
  }
  else
  {
    generate_xml(document);
  }

  if (m_verbose)
  {
    printf("Writing output file: %s\n", m_profile_pathname.c_str());
  }

  // Write XML document.
  ofstream profile;
  profile.open(m_profile_pathname.c_str());
  profile << document << endl;
  profile.close();

  // Warn if we weren't able to handle some symbols.
  if (m_verbose)
  {
    int unresolved = 0;

    FOR_EACH_PAIR(const_iterator, it, Map<hash_key_t COMMA Binary*>,
                  m_binaries)
    {
      Binary* binary = it->second;
      if (binary->get_pathname().empty())
      {
        unresolved += binary->get_functions().size();
      }
    }
    
    if (unresolved > 0)
    {
      printf("There were %i unresolvable frame addresses in the perf data.\n"
             "These will be displayed as 'pc=0xnnnnn' in the profile.\n",
             unresolved);
    }
  }

  if (m_verbose)
  {
    printf("Cleaning up...\n");
  }

  // Clean up.
  delete document;
  delete statistic_xml_file;
  delete monitor_xml_file;

  if (m_verbose)
  {
    printf("Done.\n");
  }

  return 0;
}

/** Processes command line. */
int
Application::process_arguments(int argc, char **argv)
{
  int status = 0;
  int usage = 0;

#define SHOW_USAGE           usage=1;
#define SHOW_USAGE_INTERNAL  usage=2;
#define USAGE_ERROR          usage=1; status=-1
#define OTHER_ERROR          status=-1

  /** usage string (%s will be replaced by argv[0]). */
  std::string usage_text = 
    "Usage: %s"
    " {options}"
    " {-cd directory}"
    " {{-i} perf.data}+"
    " ..."
    "\n"
  ;

  /** Help text (will be preceded by usage string above). */
  std::string usage_detail_text = 
    " -h|--help                     -- displays this help text \n"
    " -v|--verbose                  -- displays verbose progress output \n"
    " -c|--cd     directory         -- "
                                 "sets relative directory for pathnames \n"
    " {-i|--input} perf.data        -- "
                        "specifies perf.data session file(s) to process \n"
    " -o|--output profile.xml       -- specifies output file path \n"
    " -p|--statistic-xml ProfileMetadata.xml \n"
    "        -- specifies profile statistic metadata descriptor file \n"
    " -m|--monitor-xml monitor.xml  -- "
                                 "specifies monitor.xml descriptor file \n"
    " -s|--symbol-map host_path tile_path \n"
    "        -- specifies host-side file to use when looking up\n"
    "           symbols for tile-side file \n"
    "           (NOTE: this is in addition to any mappings specified\n"
    "                  specified by monitor.xml.) \n"
    " -l|--vmlinux /path/to/vmlinux -- "
                                "vmlinux file to use for kernel symbols \n"
    " -k|--split-cpus               -- "
                      "split stats for same linux task on different cpus\n";

  /** Additional help text for internal options. */
  std::string usage_detail_internal_text = 
    " -H|--help-internal            -- displays internal/debugging options \n"
    " -d|--debug                    -- "
                              "generate simpler XML (for debugging only) \n"
    " -n|--no-demangle             -- don't demangle C++ symbols \n"
    " -r|--raw-samples             -- "
                         "prints raw perf sample data read from data files \n";

  // default directory for relative pathnames
  // initially this is the current directory,
  // but --cd argument can change this
  Pathname current_directory = Pathname::get_cwd();

  // Argument state tracking.
  bool have_monitor_xml = false;

  // Process arguments.
  for (int i=1; i<argc; ++i)
  {
    std::string arg = argv[i];

    if (arg == "-h" || arg == "--help")
    {
      SHOW_USAGE;
      break;
    }

    else if (arg == "-H" || arg == "--help-internal")
    {
      SHOW_USAGE_INTERNAL;
      break;
    }

    else if (arg == "-c" || arg == "--cd")
    {
      if (i >= argc-1)
      {
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      // Set base directory for relative pathnames.
      current_directory = argv[++i];
    }

    else if (arg == "-d" || arg == "--debug")
    {
      m_debug_xml = true;
    }

    else if (arg == "-v" || arg == "--verbose")
    {
      m_verbose = true;
    }

    else if (arg == "-r" || starts_with(arg, "--raw"))
    {
      m_show_samples = true;
    }

    else if (arg == "-n" || arg == "--no-demangle")
    {
      m_demangle_symbols = false;
    }

    else if (arg == "-m" || starts_with(arg, "--mon"))
    {
      if (i >= argc-1)
      {
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      if (have_monitor_xml)
      {
        printf("Multiple monitor metadata files specified.\n");
        USAGE_ERROR;
        break;
      }

      // Set monitor.xml file to get TILE-specific metadata from.
      m_monitor_xml_pathname = argv[++i];
      m_monitor_xml_pathname.make_absolute(current_directory);
      have_monitor_xml = true;
    }

    else if (arg == "-s" || arg == "--symbol-map")
    {
      if (i >= argc-2)
      {
        printf("Missing one or both pathnames for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      // Set monitor.xml file to get TILE-specific metadata from.
      Pathname host_path = argv[++i];
      Pathname tile_path = argv[++i];
      m_tile_to_host_pathname_mappings.put(tile_path, host_path);
    }

    else if (arg == "-p" || starts_with(arg, "--stat"))
    {
      if (i >= argc-1)
      {
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      // Set ProfileMetadata.xml file to get TILE-specific stat metadata from.
      m_statistic_xml_pathname = argv[++i];
      m_statistic_xml_pathname.make_absolute(current_directory);
    }

    else if (arg == "-l" || arg == "--vmlinux")
    {
      if (i >= argc-1)
      {
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      // Set vmlinux path.
      m_vmlinux_pathname = argv[++i];
      m_vmlinux_pathname.make_absolute(current_directory);
    }

    else if (arg == "-i" || arg == "--input")
    {
      if (i >= argc-1)
      { 
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      Pathname sample_file_path = arg;
      sample_file_path.make_absolute(current_directory);
      m_sample_file_pathnames.add(sample_file_path);
    }

    else if (arg == "-o" || arg == "--output")
    {
      if (i >= argc-1)
      { 
        printf("No pathname specified for %s option.\n", arg.c_str());
        USAGE_ERROR;
        break;
      }

      // set output pathname
      m_profile_pathname = argv[++i];
      m_profile_pathname.make_absolute(current_directory);
    }

    else if (arg == "-k" || arg == "--split-cpus")
    {
      m_aggregate_cpus = false;
    }

    else if (starts_with(arg, "-"))
    {
      FOR_EACH(const_iterator, it, std::string, arg)
      {
        char c = *it;

        switch (c)
        {
        case '-':
          // ignore leading hyphen
          break;

        case 'd':
          m_debug_xml = true;
          break;

        case 'p':
          m_show_samples = true;
          break;

        case 'n':
          m_demangle_symbols = false;
          break;

        case 'f':
          m_show_function_names_on_frames = true;
          break;

        case 'k':
          m_aggregate_cpus = false;
          break;

        case 'v':
          m_verbose = true;
          break;

        default:
          printf("Unrecognized option: -%c\n", c);
          USAGE_ERROR;
          break;
        }

        if (status < 0) break;
      }
    }

    else
    {
      // Non-option arguments are assumed to be sample input paths.
      Pathname sample_file_path = arg;
      sample_file_path.make_absolute(current_directory);
      m_sample_file_pathnames.add(sample_file_path);
    }
  }

  // Check that we got what we expected.
  if (usage == 0 && status == 0)
  {
    if (! m_vmlinux_pathname.empty() &&
        ! m_vmlinux_pathname.exists())
    {
      printf("Specified vmlinux file '%s' does not exist.\n",
             m_vmlinux_pathname.c_str());
      OTHER_ERROR;
    }

    if (m_sample_file_pathnames.size() == 0)
    {
      printf("You must specify at least one perf.data sample file.\n");
      OTHER_ERROR;
    }
    else
    {
      FOR_EACH(const_iterator, it, Array<Pathname>, m_sample_file_pathnames)
      {
        const Pathname& sample_file_pathname = *it;
        if (! sample_file_pathname.exists())
        {
          printf("Sample file '%s' does not exist.\n",
                 sample_file_pathname.c_str());
          OTHER_ERROR;
          break;
        }
      }
    }
  }

  if (usage > 0)
  {
    Pathname command = argv[0];
    std::string command_name = command.get_name();
    printf(usage_text.c_str(), command_name.c_str());
    printf(usage_detail_text.c_str());
    if (usage > 1)
    {
      printf(usage_detail_internal_text.c_str());
    }
    status = -1;
  }
  else
  {
    // Display any warning notices, etc.

    if (m_debug_xml)
    {
      printf("Simpler debug XML output selected -- "
             "this will not be readable by the IDE.\n");
    }

    if (m_show_function_names_on_frames)
    {
      printf("Adding function names to frames in the output file.\n");
    }
  }

  return status;
}


/** Processes a single sample from a sample file. */
int
Application::process_sample(perf_sample_data* sample)
{
  // display sample data, if requested
  if (m_show_samples)
  {
    display_sample(sample);
  }

  // Collect event name/id pairs.
  int event_id = sample->event_id;
  if (! m_event_names.contains(event_id))
  {
    char buffer[256];
    strcpy(buffer, sample->event_name);
    const std::string& event_name = buffer;
    m_event_names.put(event_id, event_name);
    m_event_ids.put(event_name, event_id);

    const StatisticDescriptor* event_statistic =
      m_statistics_index->event_statistic(event_name);
    if (event_statistic != NULL)
    {
      m_event_display_names.put(event_id, event_statistic->get_name());
    }
  }

  // Update global event totals.
  long value = m_event_counts.get(event_id, 0);
  ++value;
  m_event_counts.put(event_id, value);

  // Collect task info.
  int cpu = sample->cpu;
  int pid = sample->pid;
  int tid = sample->tid;

  // Top-most non-kernel module in which this sample was taken.
  const char* module = sample->module;

  // Replace "kallsyms" placeholder binary with our vmlinux file, if specified.
  bool kernel_module = streql(module, KERNEL_KALLSYMS_MODULE_NAME);
  if (kernel_module && ! m_vmlinux_pathname.empty())
  {
    module = m_vmlinux_pathname.c_str();
  }

  // Get/create task object.
  hash_key_t task_hash = 0;
  if (! m_aggregate_cpus)
  {
    // hash value includes cpu id, so we split cpu data into separate Tasks
    task_hash = (((m_session_id * 65536) + cpu * 65536) + pid * 65536) + tid;
  }
  else
  {
    // hash value omits cpu id, so we aggregate cpu data into one Task
    task_hash = ((m_session_id * 65536) + pid * 65536) + tid;
  }

  Task* task = m_tasks.get(task_hash, NULL);
  if (task == NULL)
  {
    task = new Task(++m_task_id,
                    m_session_id,
                    cpu,
                    pid,
                    tid,
                    module);
    m_tasks.put(task_hash, task);
  }
  else
  {
    // Add cpu to list of cpus this task entry was seen on.
    task->add_cpu(cpu);

    // Task's executable is the "topmost" non-kernel module seen.
    if (! kernel_module &&
        ! streql(sample->callstack[sample->callstack_depth-1].symbol,
                 "__libc_start_main") &&
        module != NULL && module[0] != '\0')
    {
      task->set_pathname(module);
    }
  }

  int callstack_depth = sample->callstack_depth;
  perf_sample_frame* callstack = sample->callstack;

  // Get/create current frame (and any parent frames).
  Frame* frame = add_frames(task->get_frames(), callstack, callstack_depth);

  // Increment count for event on sample frame.
  frame->add_statistic(event_id);

  return 0;
}


/** Displays sample data (for debugging only). */
void
Application::display_sample(perf_sample_data* sample)
{
  printf("Sample: module='%s'\n",
         sample->module);
  printf("        cpu=%i, pid=%i, tid=%i\n",
         sample->cpu, sample->pid, sample->tid);
  printf("        event: %s (%i)\n",
         sample->event_name, sample->event_id);
  int depth = sample->callstack_depth;
  for (int i=0; i<depth; ++i)
  {
    printf("  [%02i] %p == fn=%s, module=%s\n",
           i,
           (void*) sample->callstack[i].address,
           sample->callstack[i].symbol,
           sample->callstack[i].module);
  }
}


// --- helper methods ---

/** Finds/adds binary file. */
Binary*
Application::add_binary(const Pathname& pathname)
{
  hash_key_t hash = hash_value(pathname.c_str());
  Binary* result = m_binaries.get(hash, NULL);
  if (result == NULL)
  {
    // Map tile->host symbol pathnames, if we have a mapping.
    // If we don't have a mapping, we just use the tile pathname as-is.
    Pathname symbol_pathname =
      m_tile_to_host_pathname_mappings.get(pathname, pathname);

    result = new Binary(pathname, symbol_pathname);
    m_binaries.put(hash, result);

    if (! result->is_valid() && ! pathname.empty())
    {
      printf("Warning: could not load symbol file: '%s'\n",
             symbol_pathname.c_str());
    }      
  }

  return result;
}


/** Finds/adds function. */
Function*
Application::add_function(Binary* binary,
                          const std::string& function_name,
                          const Pathname& source_file)
{
  Function* result = NULL;

  FOR_EACH_PAIR(const_iterator, it, Map<int COMMA Function*>,
                binary->get_functions())
  {
    Function& function = *(it->second);
    if (function.get_name() == function_name)
    {
      result = &function;
      break;
    }
  }

  if (result == NULL)
  {
    int id = ++m_function_id;
    result = new Function(binary, id, function_name, source_file);
    binary->add_function(result);
  }

  return result;
}


/** Find matching frame, if any, for specified sample frame data. */
Frame*
Application::find_matching_frame(Array<Frame*>& frames,
                                 const perf_sample_frame& sample_frame)
{
  Frame* result = NULL;

  FOR_EACH(iterator, it, Array<Frame*>, frames)
  {
    Frame& f = **it;
    if (f == sample_frame)
    {
      result = &f;
      break;
    }
  }

  return result;
}


/** Finds/adds frame(s) as needed for current callstack.
    Returns last frame created/found. */
Frame*
Application::add_frames(Array<Frame*>& frames,
                        perf_sample_frame* callstack,
                        int callstack_depth, int current_frame)
{
  // Callstack frames are stored from deepest to shallowest
  // so on the initial call start at the shallow end.
  if (current_frame < 0) current_frame = callstack_depth-1;

  // Get current frame data.
  const perf_sample_frame& sample_frame = callstack[current_frame];

  // Check the list of existing frames at this level for a match.
  Frame* frame = find_matching_frame(frames, sample_frame);

  // If not found, add a frame at this level.
  if (frame == NULL)
  {
    // Get module and address for this stack frame.
    const std::string& module        = sample_frame.module;
    const uint64_t& address          = sample_frame.address;
    const std::string& function_name = sample_frame.symbol;

    // Get Binary entry for module path.
    Binary* binary = NULL;
    if (module == KERNEL_KALLSYMS_MODULE_NAME && ! m_vmlinux_pathname.empty())
    {
      // Replace "kallsyms" placeholder binary with our vmlinux file,
      // if specified.
      binary = add_binary(m_vmlinux_pathname);
    }
    else
    {
      binary = add_binary(module);
    }

    // Get debug information.
    std::string debug_function_name;
    Pathname source_file;
    unsigned int source_line = 0;
    bool have_debug_info =
      binary->addr2line(address,
                        debug_function_name,
                        source_file, source_line,
                        m_demangle_symbols);

    // Get/create function object.
    Function* function = NULL;
    if (have_debug_info)
    {
      function = add_function(binary, debug_function_name, source_file);
    }
    else 
    {
      function = add_function(binary, function_name, source_file);
    }

    // Add frame.
    frame = new Frame(address, function, source_file, source_line);
    frames.add(frame);
  }

  // If we've reached the deepest frame in the callstack data, we're done.
  if (current_frame == 0) return frame;

  // Otherwise, recurse to find the next deepest frame.
  return add_frames(frame->get_frames(),
                    callstack,
                    callstack_depth,
                    current_frame - 1);
}


// --- Debug XML generation methods ---

/** Generates simpler XML document from collected data, for debugging. */
void
Application::generate_debug_xml(XMLDocument* document)
{
  // <profile> node
  XMLElement* profile_element = document->create_root("profile");

  // <events> node
  XMLElement* events_element = profile_element->create_element("events");
  FOR_EACH_PAIR(const_iterator, it, Map<int COMMA std::string>, m_event_names)
  {
    // <event> node
    int event_id = it->first;
    const std::string& event_name = it->second;
    int event_count = m_event_counts.get(event_id, 0);

    events_element->create_element("event",
                                   "id",    ::to_string(event_id),
                                   "name",  event_name,
                                   "count", ::to_string(event_count));
  }

  // <sample_files> node
  XMLElement* sample_files_element =
    profile_element->create_element("sample_files");
  FOR_EACH(const_iterator, it, Array<Pathname>, m_sample_file_pathnames)
  {
    // <sample_file> node
    const Pathname& sample_file_pathname = *it;
    sample_files_element->create_element("sample_file",
                                         "path", sample_file_pathname);
  }

  // <binaries> node
  XMLElement* binaries_element = profile_element->create_element("binaries");
  FOR_EACH_PAIR(const_iterator, it, Map<hash_key_t COMMA Binary*>, m_binaries)
  {
    // <binary> node
    Binary* binary = it->second;
    XMLElement* binary_element =
      binaries_element->create_element("binary",
                                       "path", binary->get_pathname());

    // <functions> node
    XMLElement* functions_element =
      binary_element->create_element("functions");
    FOR_EACH_PAIR(const_iterator, it2, Map<int COMMA Function*>,
                  binary->get_functions())
    {
      // <function> node
      Function* function = it2->second;
      functions_element->create_element("function",
                                        "function_id",
                                           ::to_string(function->get_id()),
                                        "name", function->get_name(),
                                        "path", function->get_source_file());
    }
  }

  // <tasks> node
  XMLElement* tasks_element = profile_element->create_element("tasks");
  FOR_EACH_PAIR(iterator, it, Map<hash_key_t COMMA Task*>, m_tasks)
  {
    Task* task = it->second;

    // <task> node
    XMLElement* task_element =
      tasks_element->create_element("task",
                                    "path",    task->get_pathname(),
                                    "session", 
                                         ::to_string(task->get_session_id()),
                                    "id",      ::to_string(task->get_id()),
                                    "pid",     ::to_string(task->get_pid()),
                                    "tid",     ::to_string(task->get_tid()));

    // Get usable tile width (may be smaller than actual grid width).
    //int x_tiles = m_target_spec->get_int_attribute("chip_width", 6);

    XMLElement* cpus_element = 
      task_element->create_element("cpus");
    
    // <tile> node
    FOR_EACH(const_iterator, it, Set<int>, task->get_cpus())
    {
      int cpu = *it;
      cpus_element->create_element("cpu", "id", ::to_string(cpu));
    }

    // <frames> node
    XMLElement* frames_element = task_element->create_element("frames");

    // <frame> node tree
    generate_debug_frames_xml(document, frames_element, task->get_frames());
  }
}

/** Generates XML for specified tree of frames. */
void
Application::generate_debug_frames_xml(XMLDocument* document,
                                       XMLElement* parent,
                                       Array<Frame*>& frames)
{
  FOR_EACH(const_iterator, it, Array<Frame*>, frames)
  {
    // <frame> element
    Frame* frame = *it;
    XMLElement* frame_element =
      parent->create_element(
        "frame",
        "function_id", ::to_string(frame->get_function_id()),
        "address",     ::to_hex_string(frame->get_address()));

    if (m_show_function_names_on_frames)
    {
      frame_element->set_attribute("function_name",
                                   ::to_string(frame->get_function_name()));
    }

    if (frame->has_debug_info())
    {
      frame_element->set_attribute("file", frame->get_source_file().c_str());
      frame_element->set_attribute("line",
                                   ::to_string(frame->get_source_line()));
    }

    // Generate stats for this frame, if any.
    const Map<int, long>& statistics = frame->get_statistics();
    FOR_EACH_PAIR(const_iterator, it, Map<int COMMA long>, statistics)
    {
      int  event_id    = it->first;
      long event_value = it->second;
      const std::string& event_name =
        m_event_names.get(event_id, "UNKNOWN_EVENT");
      frame_element->set_attribute(event_name, ::to_string(event_value));
    }

    // Generate children, if any.
    generate_debug_frames_xml(document, frame_element, frame->get_frames());
  }
}


// --- official XML generation methods ---

/** Generates XML document from collected data. */
void
Application::generate_xml(XMLDocument* document)
{
  // <statistics> node
  XMLElement* root_element =
    document->create_root("statistics",
                          "version", "2.0.0");

  // <properties>/<statistic> nodes
  m_statistics_index->to_xml(root_element);

  // <chip> node (copied from monitor.xml file)
  root_element->add_element(new XMLElement(m_target_spec));

  // <binaries> node
  XMLElement* binaries_element = root_element->create_element("binaries");
  FOR_EACH_PAIR(const_iterator, it, Map<hash_key_t COMMA Binary*>, m_binaries)
  {
    // <binary> node
    Binary* binary = it->second;
    XMLElement* binary_element =
      binaries_element->create_element("binary",
                                       "label",      binary->get_pathname(),
                                       "path",       binary->get_pathname(),
                                       "local_path", binary->get_pathname());

    // <functions> node
    XMLElement* functions_element = 
      binary_element->create_element("functions");
    FOR_EACH_PAIR(const_iterator, it2, Map<int COMMA Function*>, 
                  binary->get_functions())
    {
      // <function> node
      Function* function = it2->second;
      functions_element->create_element("function",
                                        "id",  
                                           ::to_string(function->get_id()),
                                        "name", function->get_name());
    }
  }


  // <processes> node
  XMLElement* processes_element = root_element->create_element("processes");
  FOR_EACH_PAIR(iterator, it, Map<hash_key_t COMMA Task*>, m_tasks)
  {
    Task* task = it->second;

    // <process> node
    XMLElement* process_element =
      processes_element->create_element(
        "process",
        "id",               ::to_string(task->get_id()),
        "linux_process_id", ::to_string(task->get_pid()),
        "linux_thread_id",  ::to_string(task->get_tid()),
        "session",          ::to_string(task->get_session_id()),
        "path",             task->get_pathname());
    
    // Get usable tile width (may be smaller than actual grid width).
    int x_tiles = m_target_spec->get_int_attribute("chip_width", 6);

    // <tiles> node
    XMLElement* tiles_element = 
      process_element->create_element("tiles");
    
    // <tile> node
    FOR_EACH(const_iterator, it, Set<int>, task->get_cpus())
    {
      int cpu = *it;
      int tile_y = cpu / x_tiles;
      int tile_x = cpu % x_tiles;
      tiles_element->create_element("tile",
                                    "x", ::to_string(tile_x),
                                    "y", ::to_string(tile_y));
    }

    // <call_tree> node
    XMLElement* call_tree_element =
      process_element->create_element("call_tree");

    // <frame> node tree
    generate_frames_xml(document, call_tree_element, task->get_frames());
  }
}

/** Generates XML for specified tree of frames. */
void
Application::generate_frames_xml(XMLDocument* document,
                                 XMLElement* parent,
                                 Array<Frame*>& frames)
{
  const std::string FRAME         = "frame";
  const std::string PC            = "pc";
  const std::string FUNCTION_ID   = "function_id";
  const std::string FUNCTION_NAME = "function_name";
  const std::string FILE          = "file";
  const std::string LINE          = "line";

  const std::string PROCESSOR     = "processor";
  const std::string STALLS        = "stalls";
  const std::string USER_EVENTS   = "user_events";
  const std::string CACHE         = "cache";
  const std::string IMESH         = "imesh";

  const std::string UNKNOWN_EVENT = "unknown_event";

  FOR_EACH(const_iterator, itf, Array<Frame*>, frames)
  {
    // <frame> element
    Frame* frame = *itf;
    XMLElement* frame_element =
      parent->create_element(
        FRAME,
        PC,          ::to_hex_string(frame->get_address()),
        FUNCTION_ID, ::to_string(frame->get_function_id()));
    if (m_show_function_names_on_frames)
    {
      frame_element->set_attribute(FUNCTION_NAME,
                                   ::to_string(frame->get_function_name()));
    }

    if (frame->has_debug_info())
    {
      frame_element->set_attribute(FILE, frame->get_source_file().c_str());
      frame_element->set_attribute(LINE,
                                   ::to_string(frame->get_source_line()));
    }

    // Generate stats for this frame, if any.

    // Add <frame> stats, if any.
    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        frame_element->set_long_attribute(stat->get_name(), count);
      }
    }

    // Add <frame><processor><stall> stats, if any.
    XMLElement* processor_element = NULL;
    XMLElement* stalls_element    = NULL;

    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_processor_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        if (processor_element == NULL)
          processor_element = frame_element->create_element(PROCESSOR);
        processor_element->set_long_attribute(stat->get_name(), count);
      }
    }

    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_stall_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        if (processor_element == NULL)
          processor_element = frame_element->create_element(PROCESSOR);
        if (stalls_element == NULL)
          stalls_element = processor_element->create_element(STALLS);
        stalls_element->set_long_attribute(stat->get_name(), count);
      }
    }

    // Add <frame><processor><user_events> stats, if any.
    XMLElement* user_events_element = NULL;
    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_user_event_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        if (processor_element == NULL)
          processor_element = frame_element->create_element(PROCESSOR);
        if (user_events_element == NULL)
          user_events_element = processor_element->create_element(USER_EVENTS);
        user_events_element->set_long_attribute(stat->get_name(), count);
      }
    }

    // Add <frame><cache> stats, if any.
    XMLElement* cache_element = NULL;
    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_cache_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        if (cache_element == NULL)
          cache_element = frame_element->create_element(CACHE);
        cache_element->set_long_attribute(stat->get_name(), count);
      }
    }

    // Add <frame><imesh> stats, if any.
    XMLElement* imesh_element = NULL;
    FOR_EACH(const_iterator, it, StatisticDescriptorArray,
             m_statistics_index->get_frame_imesh_statistics())
    {
      const StatisticDescriptor* stat = *it;
      int event_id = stat->get_event_id();
      unsigned long count = frame->get_statistic(event_id);
      if (count > 0)
      {
        if (imesh_element == NULL)
          imesh_element = frame_element->create_element(IMESH);
        imesh_element->set_long_attribute(stat->get_name(), count);
      }
    }

    // Generate children, if any.
    generate_frames_xml(document, frame_element, frame->get_frames());
  }
}
