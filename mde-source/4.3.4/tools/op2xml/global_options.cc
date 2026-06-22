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
// globals -- Global variables (esp. flags) used by op2xml
// ==========================================================================

// -------------------------------------------------------------------------
// globals
// -------------------------------------------------------------------------

#include "global_options.h"

/** Whether to show sample file paths as we process them */
bool g_show_sample_files = false;

/** Whether to show samples as we read them from files */
bool g_show_samples = false;

/** Whether to show details of samples as we read them from files */
bool g_show_sample_details = false;

/** Whether to show symbols as we read them from files */
bool g_show_symbols = false;

/** Whether to show sample files we skipped */
bool g_show_skipped = false;

/** Whether to produce XML comments listing samples associated
    with <process> elements */
bool g_show_samples_per_process = false;

/** Whether to show call graph information generated for each process */
bool g_show_process_call_graph = false;

/** Whether to show callgraph roots as we find them */
bool g_show_root_selection = false;

/** Whether to show frames generated for each process */
bool g_show_frame_generation = false;

/** Whether to show the list of collected binary pathnames */
bool g_show_binaries = false;

//If we're running natively, no need for remote/local mappings.
#ifdef __tile__
/** Whether to use remote-to-local mappings */
bool g_use_remote_local_mappings = false;
#else
bool g_use_remote_local_mappings = true;
#endif

/** Whether to split processes across cpus */
bool g_split_across_cpus = false;

/** Whether to produce "location" (debugging) XML attributes
    in <frame> elements */
bool g_show_location_attributes_on_frame_elements = false;

/** Whether to minimize white space by not indenting,
    and by not wrapping attributes */
bool g_minimize_white_space = true;

/** Whether the "--debug" command line option was specified */
bool g_debug = false;

/** Maximum stack frame depth, if any (0 means no limit) */
int g_max_stack_depth = 1000;

/** Whether to limit samples to a specified pid */
std::string g_filter_pid;

/** Whether to limit samples to a specified event_name */
std::string g_filter_event;

/** Root directory for debuginfo lookup */
Pathname g_debuginfo_root_directory;

/** Directory to use for debuginfo lookup */
Pathname g_debuginfo_directory;
