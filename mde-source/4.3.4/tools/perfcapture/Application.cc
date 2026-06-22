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
#include <stdlib.h>  // exit()

// Oprofile includes

// custom includes
#include "string_utils.h" // string utilities
#include "xml.h"          // XML utilities



// ----------------------------------------------------------------------------
// Application
// ----------------------------------------------------------------------------

// --- static members ---

/** Singleton application instance. */
Application* Application::s_app = NULL;


// --- static methods ---

/** Create and run application. */
int Application::main(int argc, char** argv)
{
  return get()->run(argc, argv);
}

/** Create/returns singleton Application instance. */
Application* Application::get()
{
  if (s_app == NULL) 
    s_app = new Application();
  return s_app;
}


// --- constants ---

/** Default OProfile data archive path. */
Pathname Application::DEFAULT_PERF_DATA_PATHNAME =
  Pathname("perf.data");


// --- "global" settings ---

/** Whether to show progress reading/writing data. */
bool Application::s_show_progress;

/** Whether to show progress reading/writing data. */
bool Application::show_progress() { return s_show_progress; }


/** Whether to pretty-print profile XML. */
bool Application::s_pretty_print = false;

/** Whether to pretty-print profile XML. */
bool Application::pretty_print() { return s_pretty_print; }


/** Whether to print samples read from sample files. */
bool Application::s_show_samples = false;

/** Whether to print samples read from sample files. */
bool Application::show_samples() { return s_show_samples; }


// --- constructors/destructors ---

/** Constructor. */
Application::Application() :
  m_verbose(false)
{
}

/** Destructor. */
Application::~Application()
{
}


// --- argument processing ---

/** Processes command line arguments. */
int
Application::process_arguments(int argc, char **argv)
{
  int status = STATUS_OK;
  int n = 0;

  char argname[3];
  argname[0] = '-';
  argname[1] = 'x';
  argname[2] = '\0';

  while (++n<argc)
  {
    char* arg = argv[n];

    if (arg[0] == '-' && arg[1] != '-') // -x argument
    {
      int len = strlen(arg);
      for (int i=1; i<len; ++i)
      {
        argname[1] = arg[i];
        status = process_argument(argname, n, argc, argv);
        if (status != STATUS_OK) break;
      }
    }

    else // --name argument or bare argument
    {
      status = process_argument(arg, n, argc, argv);
    }

    if (status != STATUS_OK) break;
  }

  if (status == STATUS_USAGE)
  {
    display_usage_message(argv[0]);
  }

  return status;
}

/** Processes command line argument. */
int Application::process_argument(const char* arg, int& n, int argc, char** argv)
{
  int status = STATUS_OK;

  // check for verbosity quickly, so we can start displaying messages
  if (starts_with(arg, "-v") || starts_with(arg, "--v"))
  {
    printf("Verbose mode selected.\n");
    printf("Processing arguments.\n");

    m_verbose = true;
  }

  else if (starts_with(arg, "-h") || starts_with(arg, "--h"))
  {
    status = STATUS_USAGE;
  }

  else if (starts_with(arg, "-c") || starts_with(arg, "--cd"))
  {
    if (n + 1 < argc)
    {
      // Set default directory for pathnames.
      m_current_directory = argv[++n];
    }
    else
    {
      printf("No pathname specified for %s option.\n", arg);
      status = STATUS_USAGE;
    }
  }

  else if (starts_with(arg, "-o") || starts_with(arg, "--output"))
  {
    if (n + 1 < argc)
    {
      // Set profile.xml output pathname.
      m_profile_output_pathname = argv[++n];
    }
    else
    {
      status = STATUS_USAGE;
    }
  }

  else if (starts_with(arg, "-e") || starts_with(arg, "--events"))
  {
    if (n + 1 < argc)
    {
      // Set profile_events.xml input pathname.
      m_profile_events_file_pathname = argv[++n];
    }
    else
    {
      status = STATUS_USAGE;
    }
  }

  else if (starts_with(arg, "-l") || starts_with(arg, "--vmlinux"))
  {
    if (n + 1 < argc)
    {
      // Set vmlinux symbol file path.
      m_vmlinux_pathname = argv[++n];
      m_vmlinux_pathname.make_absolute(m_current_directory);
    }
    else
    {
      printf("No pathname specified for %s option.\n", arg);
      status = STATUS_USAGE;
    }
  }

  else if (starts_with(arg, "-p") || starts_with(arg, "--show-progress"))
  {
    Application::s_show_progress = true;
  }

  else if (starts_with(arg, "-S") || starts_with(arg, "--show-samples"))
  {
    Application::s_show_samples = true;
  }

  else if (starts_with(arg, "-P") || starts_with(arg, "--pretty-print"))
  {
    Application::s_pretty_print = true;
  }

  else if (starts_with(arg, "-"))
  {
    fprintf(stderr, "Unrecognized switch argument: %s\n", arg);
    status = STATUS_USAGE;
  }

  else
  {
    // Bare arguments are assumed to be profile data pathnames.
    if (m_profile_data_pathnames.empty())
    {
      if (m_verbose)
        printf("Profile data file specified: %s\n", arg);
      m_profile_data_pathnames.add(Pathname(arg));
    }
    else
    {
      printf("Multiple profile files specified.\n"
             "Currently only one file can be specified.\n");
      status = STATUS_ERROR;
    }
  }

  /* example of argument with following value.
  else if (starts_with(arg, "-f") || starts_with(arg, "--f"))
  {
    if (n >= argc)
    {
      status = STATUS_ERROR;
    }
    else
    {
      ++n;
    }
  }
  */

  /* else case to reject any unrecognized arguments:
  else
  {
    fprintf(stderr, "Unrecognized argument: %s\n", arg);
    status = STATUS_ERROR;
  }
  */

  return status;
}

/** Displays usage message. */
void Application::display_usage_message(char* argv0)
{
  fprintf(stderr,
          "Usage: %s {-hv} -o profile.xml perf.data ...\n"
          " -h | --help    -- displays this usage information\n"
          " -c | --cd directory -- "
            "sets relative directory for pathnames \n"
          " -o | --output  -- specifies output pathname (defaults to profile.xml)\n"
          " -e | --events  -- specifies profile_events.xml descriptor file to use\n"
          " -l | --vmlinux /path/to/vmlinux -- "
            "kernel file to use for symbols, instead of boot/vmlinux\n"
          " Debugging flags:\n"
          " -v | --verbose -- displays verbose status messages\n"
          " -p | --show-progress -- show progress reading/writing data\n"
          " -S | --show-samples -- show samples read from profile data\n"
          " -P | --pretty-print -- indent generated XML (larger file size)\n"
          , argv0);
}


// --- application execution ---

/** Runs the application. */
int
Application::run(int argc, char** argv)
{
  int status = STATUS_OK;

  if (Application::show_progress())
  {
    print_current_time();
    fflush(stdout);
  }

  // Initial default for profile output file.
  m_profile_output_pathname = Pathname::get_cwd_pathname("profile.xml");

  // Initial default for vmlinux path.
  m_vmlinux_pathname = "";

  // Initial default for profile events input file.
  // First, check the current execution directory.
  m_profile_events_file_pathname = Pathname::get_cwd_pathname("profile_events.xml");

  // Next, try the same directory as the executable.
  if (! m_profile_events_file_pathname.exists()) {
    m_profile_events_file_pathname = Pathname::get_exe_dir_pathname("profile_events.xml");

    // Next, assume we're on TILE and look in the normal place.
    // TODO: support more than Gx here.
    if (! m_profile_events_file_pathname.exists()) {
      m_profile_events_file_pathname = "/usr/share/tilera/info/chip/10/profile_events.xml";
    }
  }

  // default directory for relative pathnames
  // initially this is the current directory,
  // but --cd argument can change this
  m_current_directory = Pathname::get_cwd();

  if (Application::show_progress())
  {
    printf("Processing command-line arguments...\n");
    fflush(stdout);
  }

  // Process command line arguments.
  status = process_arguments(argc, argv);
  if (status != STATUS_OK)
  {
    return status;
  }

  // Default profile data pathname
  if (m_profile_data_pathnames.empty())
  {
    if (m_verbose)
    {
      printf("No profiling data pathnames specified, adding default path: %s\n",
             DEFAULT_PERF_DATA_PATHNAME.c_str());
    }

    m_profile_data_pathnames.add(DEFAULT_PERF_DATA_PATHNAME);
  }

  // Default vmlinux pathname, if needed.
  if (m_vmlinux_pathname.is_empty())
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
        Pathname::get_install_tile_dir_pathname("/boot/vmlinux");
      if (default_host_vmlinux_pathname.exists())
      {
        m_vmlinux_pathname = default_host_vmlinux_pathname;
      }
    }
  }

  if (m_verbose && ! m_vmlinux_pathname.is_empty())
  {
    printf("Using vmlinux path: %s\n", m_vmlinux_pathname.c_str());
  }

  if (Application::show_progress())
  {
    printf("Initializing...\n");
    fflush(stdout);
  }

  // Load event descriptors.
  status = load_event_descriptors(m_profile_events_file_pathname);
  if (status != STATUS_OK)
  {
    fprintf(stderr, "Could not load event descriptor file: %s\n",
            m_profile_events_file_pathname.c_str());
    return status;
  }

  // Initialize state before processing profile data.
  m_next_binary_id = 0;
  m_next_binary_symbol_id = 0;
  m_next_task_id = 0;
  m_next_location_id = 0;

  if (Application::show_progress())
  {
    printf("Processing profile data files...\n");
    fflush(stdout);
  }

  // Iterate over profile data files
  int data_files_found = 0;
  FOR_EACH(const_iterator, it, Array<Pathname>, m_profile_data_pathnames)
  {
    const Pathname& pathname = *it;

    if (! pathname.is_file())
    {
      fprintf(stderr, "Warning: the specified perf data file doesn't exist or isn't a file: %s\n",
                      pathname.c_str());
      continue;
    }

    ++data_files_found;

    // process data file
    if (m_verbose || Application::show_progress())
    {
      printf("Processing data file: %s\n", pathname.c_str());
      print_current_time();
      fflush(stdout);
    }

    process_perf_data_file(pathname);

    if (m_verbose || Application::show_progress())
    {
      print_current_time();
      printf("Data file processed: %s\n", pathname.c_str());
      fflush(stdout);
    }
  }
  
  if (data_files_found == 0)
  {
    fprintf(stderr, "No profile data files found, cannot generate XML file.\n");
    status = STATUS_ERROR;
    return status;
  }

  // Add any additional events we can interpolate from
  // the ones we've read, and sort the final event list.
  finalize_event_list();

  // Generate the output XML.
  if (Application::show_progress())
  {
    printf("Generating XML output...\n");
    print_current_time();
    fflush(stdout);
  }

  status = generate_xml_output(m_profile_output_pathname);

  if (Application::show_progress())
  {
    print_current_time();
    printf("Done.\n");
    fflush(stdout);
  }

  return status;
}


// --- archive processing ---

/** Processes perf sample data file. */
void
Application::process_perf_data_file(const Pathname& pathname)
{
  // invokes process_sample() for each sample in this sample file
  SampleFile* sample_file = new SampleFile(pathname);
  sample_file->for_each_sample((SampleHandler*) this);
  delete sample_file;
}

/** Processes a single sample from a sample file. */
int
Application::process_sample(perf_sample_data* sample)
{
  // display sample data, if requested
  if (Application::show_samples())
  {
    display_sample(sample);
  }

  // Cache event for this sample, get id
  char buffer[256];
  strcpy(buffer, sample->event_name);
  const std::string hardware_event_name = buffer;
  // FIXME: get interval, mask for event
  const long interval = 0;
  const int  mask     = 0;
  // Note: event id here is an internal unique ID, not the hardware event id
  int event_id = cache_event(hardware_event_name, interval, mask);

  // Get top-level executable module for this sample
  std::string module = sample->module;

  // Replace "kallsyms" placeholder binary with our vmlinux file, if specified.
  // (KERNEL_KALLSYMS_MODULE_NAME is defined in perf_api.h)
  bool kernel_module = module == KERNEL_KALLSYMS_MODULE_NAME;
  if (kernel_module && ! m_vmlinux_pathname.is_empty())
  {
    module = m_vmlinux_pathname;
  }

  // Cache executable for this sample, get id
  int binary_id = cache_binary(module);

  // Cache task (binary/pid/tid), get id
  int pid = sample->pid;
  int tid = sample->tid;
  int task_id = cache_task(pid, tid, binary_id);

  // Create sample entry.
  int cpu_id = sample->cpu;
  SampleData* sampledata = new SampleData(event_id, task_id, cpu_id);
  m_samples.add(sampledata);

  // Process sample frames.
  int frame_count = sample->callstack_depth;
  perf_sample_frame* frames = sample->callstack;

  // A perf sample captures:
  // (1) the sampled frame, with an implicit "self" count of 1
  // (2) zero or more parent frame(s), with an implicit "transition" count of 1 for each frame

  // So the resulting SampleData entry will have:
  // (1) the sampled frame with a 1 self count
  // (2) the parent frame(s) if any, with a 1 count for each one

  // TODO: IWBN if we could merge sample entries with the same location
  // and parent call stack, but this may entail more processing time
  // than the resulting space savings would be worth.

  // Implicit sample count for each frame.
  int value = 1;

  for (int i=0; i<frame_count; ++i)
  {
    perf_sample_frame& frame = frames[i];

    // cache frame module pathname, get id
    Pathname frame_module = frame.module;
    // Replace "kallsyms" placeholder binary with our vmlinux file
    if (frame_module == KERNEL_KALLSYMS_MODULE_NAME && ! m_vmlinux_pathname.is_empty())
    {
      frame_module = m_vmlinux_pathname;
    }
    int frame_binary_id = cache_binary(frame_module);
    Binary* frame_binary = m_id_to_binary.get(binary_id, NULL);

    // collect other information from frame
    perf_vma frame_address = frame.address;
    std::string frame_function_name = frame.symbol;

    // convert address to hex string
    std::stringstream as;
    as << "0x" << in_hex(frame_address);
    std::string frame_address_inhex = as.str();

    // get function start/end/size info
    perf_vma start_address = frame.start_address;
    perf_vma end_address   = frame.end_address;
    perf_vma size          = end_address - start_address + 1;

    // collect debug information, if we can
    std::string  debug_function_name = "";
    Pathname     debug_source_file = "";
    unsigned int debug_source_line = 0;
    if (frame_binary != NULL) {
      frame_binary->addr2line(frame_address,
                              debug_function_name,
                              debug_source_file,
                              debug_source_line,
                              true); // demangle names
    }

    if (! debug_function_name.empty()) {
      // TODO: do we want to use this, or keep what perf found?
      //frame_function_name = debug_function_name;
    }

    // Cache stack frame symbol (function), get id.
    int frame_symbol_id = 
      cache_symbol(frame_binary_id,
                   frame_binary,
                   frame_function_name,
                   start_address,
                   size,
                   end_address);

    // Cache stack frame location, get id.
    int frame_location_id =
      cache_location(frame_binary_id,
                     frame_address_inhex,
                     frame_symbol_id,
                     debug_source_file,
                     debug_source_line);

    // Add stack frame entry.
    sampledata->add_frame(frame_location_id, value);
  }

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


// --- file type detection support ---

/** Determines file type of pathname. */
std::string
Application::get_file_type(const std::string& pathname)
{
  std::string result = "unknown";

  if (ends_with(pathname, ".so") ||
      string_contains(pathname, ".so.") ||
      ends_with(pathname, ".o") ||
      ends_with(pathname, ".a"))
  {
    result = "lib";
  }
  else if (ends_with(pathname, "/vmlinux"))
  {
    result = "kernel";
  }
  else if (ends_with(pathname, ".jo"))
  {
    result = "jvm";
  }
  else if (Pathname(pathname).is_executable())
  {
    result = "exe";
  }
  else
  {
    result = "other";
  }

  return result;
}


// --- event handling ---

/** Loads event descriptor file. */
int
Application::load_event_descriptors(const Pathname& event_descriptor_pathname)
{
  int status = STATUS_OK;

  std::string errors;
  XMLDocument* event_descriptor_document =
    XMLReader::read(event_descriptor_pathname, errors);
  if (event_descriptor_document == NULL)
  {
    printf("Could not read event descriptor file: %s\nReason: %s\n",
           event_descriptor_pathname.c_str(), errors.c_str());
    return (status = STATUS_ERROR);
  }

  XMLElementArray event_elements;
  event_descriptor_document->
    get_elements_by_path("events", "event", event_elements);

  // Special case: we pre-assign IDs to events in the order
  // we read them from the events list, rather than as we cache
  // them when encountering them in sample files, so events
  // are presented in a consistent order.
  int next_event_id = 1;

  FOR_EACH(const_iterator, it, XMLElementArray, event_elements)
  {
    XMLElement& e = **it;

    std::string name = e.get_attribute("name");
    std::string display_name = e.get_attribute("display");
    std::string description = e.get_attribute("description");
    std::string hardware_event_name = e.get_attribute("event");
    std::string method = e.get_attribute("method");
    std::string type = e.get_attribute("type");
    std::string categories = e.get_attribute("categories");

    Event* event_descriptor = new Event(name, display_name, description, hardware_event_name,
                                        method, type, categories);
    int event_id = next_event_id++;
    event_descriptor->set_id(event_id);

    m_event_descriptors.add(event_descriptor);
    m_event_descriptors_by_name.put(name, event_descriptor);
    if (! hardware_event_name.empty())
      m_event_descriptors_by_hardware_event_name.put(hardware_event_name, event_descriptor);
  }

  return status;
}

/** Caches event, returns assigned ID. */
int
Application::cache_event(const std::string hardware_event_name, long interval, int mask)
{
  Event* event = m_events_by_hardware_event_name.get(hardware_event_name, NULL);
  if (event == NULL)
  {
    Event* event_descriptor = m_event_descriptors_by_hardware_event_name.get(hardware_event_name, NULL);
    event = new Event(event_descriptor);
    event->set_interval(interval);
    event->set_mask(mask);

    m_events.add(event);
    m_events_by_name.put(event->get_name(), event);
    m_events_by_hardware_event_name.put(hardware_event_name, event);
  }

  return event->get_id();
}

/** Finalizes list of events. */
void
Application::finalize_event_list()
{
  // The m_events list now contains entries for the hardware events
  // that we've seen. We need to add to this list any additional
  // "calculated" events that are supported by events we have.

  // Collect the list of non-hardware events to start with.
  Array<Event*> candidates;
  FOR_EACH(iterator, it, Array<Event*>, m_event_descriptors)
  {
    Event* event = *it;
    if (event->get_hardware_event_name().empty()) candidates.add(event);
  }

  // Calculated events can depend on other calculated events,
  // so we need to keep checking until we can't find any more.
  Array<Event*> found;
  while (true)
  {
    // Look for additional events we can add, based on what we have.
    found.clear();
    FOR_EACH(iterator, it, Array<Event*>, candidates)
    {
      Event* event = *it;
      Array<std::string> dependencies = event->get_method_arguments();
      bool okay = true;
      FOR_EACH(iterator, it2, Array<std::string>, dependencies)
      {
        const std::string& event_name = *it2;
        if (m_events_by_name.get(event_name, NULL) == NULL)
        {
          okay = false;
          break;
        }
      }
      if (okay) found.add(event);
    }

    // When we can't find any more, we're done.
    if (found.empty()) break;

    // Add any we've found to the events list.
    FOR_EACH(iterator, it, Array<Event*>, found)
    {
      Event* event = *it;
      candidates.remove(event);
      m_events.add(event);
      m_events_by_name.put(event->get_name(), event);
    }
  }

  // Finally, sort events by assigned event ID,
  // so they're in the same order as we originally read them
  // from the event descriptor file, for consistent presentation.
  m_events.sort(&Event_pointer_less_than);

}


// --- other state cacheing ---

/** Caches binary, returns assigned ID. */
int
Application::cache_binary(const std::string pathname)
{
  std::string key = pathname;
  Binary* binary = m_binaries.get(key, NULL);
  if (binary == NULL)
  {
    // Create a new entry
    std::string type = get_file_type(pathname);
    binary = new Binary(type, pathname);

    // Assign it an ID and cache it
    m_binaries.put(key, binary);
    int id = ++m_next_binary_id;
    m_binary_ids.put(binary, id);
    m_id_to_binary.put(id, binary);
  }

  return m_binary_ids.get(binary, -1);
}

/** Gets binary with specified ID. */
Binary*
Application::get_binary(int binary_id)
{
  return m_id_to_binary.get(binary_id, NULL);
}

/** Caches symbol, returns assigned ID. */
int
Application::cache_symbol(int binary_id,
                          Binary* binary,
                          const std::string& symbol_name,
                          perf_vma start_address,
                          perf_vma size,
                          perf_vma end_address)
{
  std::string key = to_string(binary_id) + "|" + symbol_name;
  BinarySymbol* binary_symbol = m_binary_symbols.get(key, NULL);
  if (binary_symbol == NULL)
  {
    // Create a new entry
    binary_symbol = new BinarySymbol(symbol_name, start_address, size, end_address);

    // Assign it an ID and cache it
    m_binary_symbols.put(key, binary_symbol);
    m_binary_symbol_ids.put(binary_symbol, ++m_next_binary_symbol_id);

    // Store a pointer with the binary, for XML generation.
    if (binary != NULL) binary->add_symbol(binary_symbol);
  }

  return m_binary_symbol_ids.get(binary_symbol, -1);
}

/** Caches task, returns assigned ID. */
int
Application::cache_task(int pid, int tid, int binary_id)
{
  long key = pid * 100000 + tid * 100000 + binary_id;
  Task* task = m_tasks.get(key, NULL);
  if (task == NULL)
  {
    // Create a new entry
    task = new Task(pid, tid, binary_id);

    // Assign it an ID and cache it
    m_tasks.put(key, task);
    m_task_ids.put(task, ++m_next_task_id);
  }

  return m_task_ids.get(task, -1);
}

/** Caches location, returns assigned ID. */
int
Application::cache_location(int binary_id,
                            const std::string& address,
                            int symbol_id,
                            const std::string& source_file,
                            int source_line)
{
  std::string key = to_string(binary_id) + "|" + address;
  Location* location = m_locations.get(key, NULL);
  if (location == NULL)
  {
    // Create a new entry
    location = new Location(binary_id, address, symbol_id, source_file, source_line);

    // Assign it an ID and cache it
    m_locations.put(key, location);
    m_location_ids.put(location, ++m_next_location_id);
  }

  return m_location_ids.get(location, -1);
}


// --- XML output generation ---

/** Generates XML output file. */
int
Application::generate_xml_output(const Pathname& pathname)
{
  int status = STATUS_OK;
  int n = 0;

  // Generate output XML file
  XMLDocument* document = new XMLDocument();

  // Create root element
  XMLElement* root_element = document->create_root("profile");
  root_element->set_attribute("version", "1.0");

  int events_count = m_events.size();
  if (Application::show_progress())
  {
    printf("Generating event descriptors (%i) ...\n", events_count);
    fflush(stdout);
    n = 0;
  }

  // add events
  XMLElement* events_element = root_element->create_element("events");
  FOR_EACH(const_iterator, e, Array<Event*>, m_events)
  {
    if (Application::show_progress())
    {
      ++n;
      if (n % 10 == 0)
      {
        printf("%i of %i...\n", n, events_count);
        fflush(stdout);
      }
    }

    const Event* event = *e;

    XMLElement* event_element = events_element->create_element("event");
    event_element->set_int_attribute("id",        event->get_id());
    event_element->set_attribute("name",          event->get_name());
    event_element->set_attribute("display",       event->get_display_name());
    event_element->set_attribute("description",   event->get_description());
    if (! event->get_hardware_event_name().empty())
      event_element->set_attribute("event",         event->get_hardware_event_name());
    if (event->get_mask() >= 0)
      event_element->set_int_attribute("mask",      event->get_mask());
    if (event->get_interval() >= 0)
      event_element->set_long_attribute("interval", event->get_interval());
    if (! event->get_type().empty())
      event_element->set_attribute("type",          event->get_type());
    if (! event->get_method().empty())
      event_element->set_attribute("method",        event->get_method());
  }

  int binaries_count = m_binaries.size();
  if (Application::show_progress())
  {
    printf("Generating binary file/symbol entries (%i) ...\n", binaries_count);
    fflush(stdout);
    n = 0;
  }

  // add binaries
  XMLElement* binaries_element = root_element->create_element("binaries");
  FOR_EACH_PAIR(const_iterator, p, Map<std::string COMMA Binary*>, m_binaries)
  {
    if (Application::show_progress())
    {
      ++n;
      if (n % 10 == 0)
      {
        printf("%i of %i...\n", n, binaries_count);
        fflush(stdout);
      }
    }

    Binary* const binary = p->second;

    XMLElement* binary_element = binaries_element->create_element("binary");

    int binary_id = m_binary_ids.get(binary, 0);
    binary_element->set_int_attribute("id", binary_id);

    binary_element->set_attribute("type",         binary->get_type());
    binary_element->set_attribute("path",         binary->get_pathname());

    if (! binary->get_symbols().empty())
    {
      // add symbols found for this binary
      XMLElement* symbols_element = binary_element->create_element("symbols");
      FOR_EACH(const_iterator, b, Array<BinarySymbol*>, binary->get_symbols())
      {
        BinarySymbol* const binary_symbol = *b;

        XMLElement* binary_symbol_element = symbols_element->create_element("symbol");

        int binary_symbol_id = m_binary_symbol_ids.get(binary_symbol, 0);
        binary_symbol_element->set_int_attribute("id", binary_symbol_id);

        binary_symbol_element->set_attribute("name",  binary_symbol->get_name());
        binary_symbol_element->set_attribute("start", to_hex_string(binary_symbol->get_start_address()));
        binary_symbol_element->set_attribute("size",  to_hex_string(binary_symbol->get_size()));
        binary_symbol_element->set_attribute("end",   to_hex_string(binary_symbol->get_end_address()));
      }
    }
  }

  int tasks_count = m_tasks.size();
  if (Application::show_progress())
  {
    printf("Generating process/thread entries (%i) ...\n", tasks_count);
    fflush(stdout);
    n = 0;
  }

  // add tasks
  XMLElement* tasks_element = root_element->create_element("tasks");
  FOR_EACH_PAIR(const_iterator, p, Map<long COMMA Task*>, m_tasks)
  {
    if (Application::show_progress())
    {
      ++n;
      if (n % 100 == 0)
      {
        printf("%i of %i...\n", n, tasks_count);
        fflush(stdout);
      }
    }

    Task* const task = p->second;

    XMLElement* task_element = tasks_element->create_element("task");

    int task_id = m_task_ids.get(task, 0);
    task_element->set_int_attribute("id", task_id);

    task_element->set_int_attribute("pid",        task->get_pid());
    task_element->set_int_attribute("tid",        task->get_tid());
    task_element->set_int_attribute("binary",     task->get_binary_id());
  }

  int locations_count = m_locations.size();
  if (Application::show_progress())
  {
    printf("Generating sample location entries (%i) ...\n", locations_count);
    fflush(stdout);
    n = 0;
  }

  // add locations
  XMLElement* locations_element = root_element->create_element("locations");
  FOR_EACH_PAIR(const_iterator, p, Map<std::string COMMA Location*>, m_locations)
  {
    if (Application::show_progress())
    {
      ++n;
      if (n % 1000 == 0)
      {
        printf("%i of %i...\n", n, locations_count);
        fflush(stdout);
      }
    }

    Location* const location = p->second;

    XMLElement* location_element = locations_element->create_element("location");

    int location_id = m_location_ids.get(location, 0);
    location_element->set_int_attribute("id", location_id);

    location_element->set_int_attribute("binary", location->get_binary_id());
    location_element->set_attribute("address",    location->get_address());
    if (location->get_symbol_id() > 0)
    {
      location_element->set_int_attribute("symbol", location->get_symbol_id());
      if (! location->get_source_file().empty())
        location_element->set_attribute("file",       location->get_source_file());
      if (location->get_source_line() != 0)
        location_element->set_int_attribute("line",   location->get_source_line());
    }
  }

  int sample_count = m_samples.size();
  if (Application::show_progress())
  {
    printf("Generating sample entries (%i) ...\n", sample_count);
    fflush(stdout);
    n = 0;
  }

  // add sample data
  XMLElement* samples_element = root_element->create_element("samples");
  FOR_EACH(const_iterator, it, Array<SampleData*>, m_samples)
  {
    if (Application::show_progress())
    {
      ++n;
      if (n % 1000 == 0)
      {
        printf("%i of %i...\n", n, sample_count);
        fflush(stdout);
      }
    }

    SampleData* const sampledata = *it;

    XMLElement* sample_element = samples_element->create_element("sample");

    sample_element->set_int_attribute("event", sampledata->get_event_id());
    sample_element->set_int_attribute("task",  sampledata->get_task_id());
    sample_element->set_int_attribute("cpu",   sampledata->get_cpu_id());

    // add frames
    if (! sampledata->get_frames().empty())
    {
      XMLElement* frames_element = sample_element->create_element("frames");
      FOR_EACH(iterator, it2, Array<Frame*>, sampledata->get_frames())
      {
        Frame* frame = *it2;

        XMLElement* frame_element = frames_element->create_element("frame");

        frame_element->set_int_attribute("loc",   frame->get_location_id());
        frame_element->set_int_attribute("count", frame->get_count());
      }
    }
  }

  if (Application::show_progress())
  {
    printf("Writing XML document to file...\n");
    fflush(stdout);
  }

  // write the document to the output pathname
  bool okay = XMLPrinter::print(*document, pathname);
  if (! okay)
  {
    fprintf(stderr, "Could not write XML to output path: %s", pathname.c_str());
    status = STATUS_ERROR;
  }

  if (Application::show_progress())
  {
    printf("Finished writing XML document.\n");
    fflush(stdout);
  }

  return status;
}


// --- utilities ---

/** Prints current timestamp. */
void
Application::print_current_time()
{
  time_t now;
  time(&now);
  printf("%s", ctime(&now));
}

