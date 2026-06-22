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
// arguments.cc -- argument processing
// ==========================================================================

// header file
#include "arguments.h"

// application headers
#include "global_options.h"

// OProfile includes
#include <cverb.h>         // verbose

extern verbose vbfd;

// -------------------------------------------------------------------------
// Command Line Options
// -------------------------------------------------------------------------

// Note: we only use the following option patterns:
// -x           -- short option, boolean flag with no value
// -x VALUE     -- short option, following arg is the required value
// --name       -- long option, boolean flag with no value
// --name=VALUE -- long option, required value is automatically collected
// --name VALUE -- long option, required value is automatically collected

// short (-x) options
// Note: the leading "-" in the string is significant;
// it means handle non-option arguments as values of a pseudo-option
// with character code NON_OPTION_ARGUMENT (defined in options.h);
static const char *s_short_options = "-hdocxkpmDRiegOvwstyrfbl";

// long (--name) options
static struct option s_long_options[] =
{
  {"help",               no_argument,        NULL, 'h'},
  {"debug",              no_argument,        NULL, 'd'},
  {"output",             required_argument,  NULL, 'o'},
  {"cd",                 required_argument,  NULL, 'c'},
  {"max-depth",          required_argument,  NULL, 'x'},
  {"split-cpus",         no_argument,        NULL, 'k'},
  {"statistic-xml",      required_argument,  NULL, 'p'},
  {"monitor-xml",        required_argument,  NULL, 'm'},
  {"debuginfo-dir",      required_argument,  NULL, 'D'},
  {"debuginfo-root",     required_argument,  NULL, 'R'},
  {"pid",                required_argument,  NULL, 'i'},
  {"event",              required_argument,  NULL, 'e'},
  {"call_graph",         no_argument,        NULL, 'g'},
  {"oprofile_verbose",   no_argument,        NULL, 'O'},
  {0, 0, 0, 0}
};

// usage string (will be prefixed by name of application)
static string s_usage = 
  " {-bedfghilOrstvwy}"
  " {-c | --cd /base/path}*"
  " -o | --output profile.xml"
  " {-x | --max-depth frames}"
  " {-k | --split-cpus}"
  " {-p | --statistic-xml ProfileMetadata.xml}"
  " {-m | --monitor-xml monitor.xml}"
  " {-D | --debuginfo-dir  /usr/lib/debug }"
  " {-R | --debuginfo-root /path/to/tile }"
  " {/path/to/oprofile/samples/dir}+"
;


// -------------------------------------------------------------------------
// process_arguments()
// -------------------------------------------------------------------------

/** Processes command-line arguments.
    Returns true if application should continue, false if it should exit.
    Returns exit status value in status.
    Stores parsed results in other arguments.
*/
bool process_arguments(int argc, char** argv, int &status,
		       Pathname&       profile_output_file,
		       Pathname&       monitor_xml_file,
		       Pathname&       statistics_xml_file,
		       PathnameVector& sample_directory_paths)
{
  bool exit  = false;
  bool usage = false;

  // initialize return values
  status = 0;
  profile_output_file = "";
  sample_directory_paths.clear();

  // default directory for relative pathnames
  // initially this is the current directory,
  // but --cd argument can change this
  Pathname current_directory = Pathname::get_cwd();

  // create a context we can bail out of
  do {

    // collect -x and --name options from command line
    OptionList options, rejects;
    if (! collect_options(argc, argv, 
                          s_short_options, s_long_options,
                          options, rejects))
    {
      // if any options are missing or rejected...

      // display rejected options, if any
      FOR_EACH(const_iterator, i, OptionList, rejects) {
        cerr << "Unknown option: " << i->name;
        if (i->value != i->name) {
          cerr << " in argument '" << i->value << "'";
        }
        cerr << endl;
      }

      // display usage string and exit with nonzero status
      exit   = true;
      usage  = true;
      status = -1;
      break;
    }

    // --help
    if (find_option(options, "h") || find_option(options, "help"))
    {
      // if user requests help,
      // display usage string and exit normally
      exit   = true;
      usage  = true;
      status = 0;
      break;
    }

    // process found options
    FOR_EACH(const_iterator, i, OptionList, options) {
      const Option& opt = (*i);

      // useful for debugging argument handling
      // cout << "Processing option: " << opt.name << " = " << opt.value << endl;

      // Note: for long option names with a following value (--name VALUE)
      // where there's a short-option alias (-x VALUE)
      // we need to process the short-option case separately
      // since there we need to step ahead to the next argument,
      // while long option processing captures the following value automatically.

      // long options with single-letter aliases:

      if (opt.name == "help" || opt.name == "h")
      {
        // ignore, we've already handled it above
      }

      else if (opt.name == "debug" || opt.name == "d")
      {
        // enable white-space
        g_minimize_white_space = false;

        // output "location" attributes instead of the more
        // verbose equivalents needed by the IDE
        g_show_location_attributes_on_frame_elements = true;
        
        // set the flag which lets us see line-level samples
        // which correspond to each process' nested <frame>s
        g_show_samples_per_process = true;

        // enable other debug information, variations, etc.
        g_debug = true;
      }

      else if (opt.name == "output")
      {
        // set output pathname
        profile_output_file = opt.value;
        profile_output_file.make_absolute(current_directory);
      }
      else if (opt.name == "o")
      {
        // next argument will be value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set output pathname
        profile_output_file = arg.value;
        profile_output_file.make_absolute(current_directory);
      }

      else if (opt.name == "cd")
      {
        // set base directory for relative pathnames
        current_directory = opt.value;
      }
      else if (opt.name == "c")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set base directory for relative pathnames
        current_directory = arg.value;
      }

      else if (opt.name == "max-depth")
      {
        // set maximum stack depth
      	g_max_stack_depth = atoi(opt.value.c_str());
      }
      else if (opt.name == "x")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set maximum stack depth
      	g_max_stack_depth = atoi(arg.value.c_str());
      }

      else if (opt.name == "split-cpus" || opt.name == "k")
      {
        g_split_across_cpus = true;
      }

      else if (opt.name == "statistic-xml")
      {
        // set output pathname
        statistics_xml_file = opt.value;
        statistics_xml_file.make_absolute(current_directory);
      }
      else if (opt.name == "p")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set output pathname
        statistics_xml_file = arg.value;
        statistics_xml_file.make_absolute(current_directory);
      }

      else if (opt.name == "monitor-xml")
      {
        // set output pathname
        monitor_xml_file = opt.value;
        monitor_xml_file.make_absolute(current_directory);
      }
      else if (opt.name == "m")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set output pathname
        monitor_xml_file = arg.value;
        monitor_xml_file.make_absolute(current_directory);
      }

      else if (opt.name == "debuginfo-dir")
      {
        // set output pathname
        g_debuginfo_directory = opt.value;
      }
      else if (opt.name == "D")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set output pathname
        g_debuginfo_directory = arg.value;
      }

      else if (opt.name == "debuginfo-root")
      {
        // set output pathname
        g_debuginfo_root_directory = opt.value;
      }
      else if (opt.name == "R")
      {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set output pathname
        g_debuginfo_root_directory = arg.value;
      }

      else if (opt.name == "pid") {
        // set PID filter
        g_filter_pid = opt.value;
      }
      else if (opt.name == "i") {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set PID filter
        g_filter_pid = arg.value;
      }

      else if (opt.name == "event") {
        // set event name filter
        g_filter_event = opt.value;
      }
      else if (opt.name == "e") {
        // next argument is value
        if (++i == options.end()) break;
        const Option& arg = (*i);

        // set event name filter
        g_filter_event = arg.value;
      }

      else if (opt.name == "call_graph" || opt.name == "g") {
        g_show_process_call_graph = true;
      }

      else if (opt.name == "oprofile_verbose" || opt.name == "O") {
        // enable oprofile/libutil++ verbose output
        // for bfd image file lookup, etc.
        verbose::setup("bfd");
      }

      // short options with no long-option equivalent:

      else if (opt.name == "v") {
        // verbose execution
        g_show_sample_files = true;
      }

      else if (opt.name == "w") {
        g_show_skipped = true;
      }

      else if (opt.name == "s") {
        g_show_samples = true;
      }

      else if (opt.name == "t") {
        g_show_samples = true;
        g_show_sample_details = true;
      }

      else if (opt.name == "y") {
        g_show_symbols = true;
      }

      else if (opt.name == "r") {
        g_show_root_selection = true;
      }

      else if (opt.name == "f") {
        g_show_frame_generation = true;
      }

      else if (opt.name == "b") {
        g_show_binaries = true;
      }

      else if (opt.name == "l") {
        // This defaults to true on the host, false on tile,
        // so we just need to reverse the default value.
        g_use_remote_local_mappings = ! g_use_remote_local_mappings;
      }

      else if (opt.name == NON_OPTION_ARGUMENT_NAME)
      {
        // non-option arguments are assumed to be sample directory paths

        // collect Oprofile sample data directory path
        Pathname samples_path = opt.value;
        samples_path.make_absolute(current_directory);

        // strip oprofile "samples" directory sub-path,
        // so all sample paths point to the top-level directory
        samples_path.remove_suffix_path("/var/lib/oprofile/samples/current");

        // add it to list of sample paths
        sample_directory_paths.add(samples_path);
      }
    }

    // check that we got what we expected
    if (profile_output_file == "") {
      cerr << "Please specify an output file (-o profile.xml)" << endl;
      exit   = true;
      usage  = false;
      status = -1;
      break;

    }
    else if (sample_directory_paths.empty()) {
      cerr << "Please specify at least one OProfile samples directory"
           << endl;
      exit   = true;
      usage  = true;
      status = -1;
      break;
    }
    else if (! g_debuginfo_directory.is_absolute() ||
             ! g_debuginfo_directory.is_directory()) {
      cerr << "--debuginfo-dir pathname must be an absolute directory path"
           << endl;
      exit   = true;
      usage  = true;
      status = -1;
      break;
    }
    else if (! g_debuginfo_root_directory.is_absolute() ||
             ! g_debuginfo_root_directory.is_directory()) {
      cerr << "--debuginfo-root pathname must be an absolute directory path"
           << endl;
      exit   = true;
      usage  = true;
      status = -1;
      break;
    }

  }
  while (false); // do/while

  // display usage string, if needed
  if (usage) {
    cout << "Usage: " << argv[0] << s_usage << endl;
    cout << " -h|--help               "
             << "-- display this help summary" << endl;
    cout << " -o|--output profile.xml "
             << "-- write output to specified profile.xml path" << endl;
    cout << " --cd path               "
             << "-- change current directory to specified path" << endl;
    cout << " -x|--max-depth frames   "
             << "-- limit process call-tree recursion to specified "
             << "depth in frames" << endl;
    cout << " -k|--split-cpus         "
             << "-- split process/thread data across cpus" << endl;
    cout << " -p|--statistic-xml ProfileMetadata.xml " << endl
             << "                         "
             << "-- override default location of statistics profile metadata file" << endl;
    cout << " -m|--monitor-xml monitor.xml " << endl
             << "                         "
             << "-- override default location of monitor metadata file" << endl;
    cout << " -D|--debuginfo-dir path -- directory for debuginfo lookup "
                                        "(default is /usr/lib/debug)" << endl;
    cout << " -R|--debuginfo-root path -- root directory for debuginfo-dir path" << endl;
    cout << " Internal debugging flags:" << endl;
    cout << " -d|--debug                 "
             << "-- add debug info to generated profile" << endl;
    cout << " -i|--pid PID    -- use only sample files matching specified PID"
             << endl;
    cout << " -e|--event NAME -- use only sample files matching specified "
             << "event name" << endl;
    cout << " -b -- show binary paths" << endl;
#ifdef __tile__
    cout << " -l -- use remote-to-local binary mappings" << endl;
#else
    cout << " -l -- don't use remote-to-local binary mappings" << endl;
#endif
    cout << " -y -- show symbols read from binary files" << endl;
    cout << " -v -- show sample files processed" << endl;
    cout << " -w -- show skipped sample files" << endl;
    cout << " -s -- show sample from/to data" << endl;
    cout << " -t -- show sample details (addresses, kernel flags, etc.)"
             << endl;
    cout << " -r -- show discovered roots of process call-trees" << endl;
    cout << " -f -- show generated call-tree frames" << endl;
    cout << " -g|--call_graph -- show call graph information" << endl;
  }

  return (! exit);
}
