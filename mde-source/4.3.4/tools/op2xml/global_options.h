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

#include <string>
#include "Pathname.h"

/** Whether to show sample file paths as we process them */
extern bool g_show_sample_files;

/** Whether to show samples as we read them from files */
extern bool g_show_samples;

/** Whether to show details of samples as we read them from files */
extern bool g_show_sample_details;

/** Whether to show symbols as we read them from files */
extern bool g_show_symbols;

/** Whether to show sample files we skipped */
extern bool g_show_skipped;

/** Whether to produce XML comments listing samples
    associated with <process> elements */
extern bool g_show_samples_per_process;

/** Whether to show call graph information generated for each process */
extern bool g_show_process_call_graph;

/** Whether to show callgraph roots as we find them */
extern bool g_show_root_selection;

/** Whether to show frames generated for each process */
extern bool g_show_frame_generation;

/** Whether to show the list of collected binary pathnames */
extern bool g_show_binaries;

/** Whether to use remote-to-local mappings */
extern bool g_use_remote_local_mappings;

/** Whether to split processes across cpus */
extern bool g_split_across_cpus;

/** Whether to produce "location" (debugging) XML attributes
    in <frame> elements */
extern bool g_show_location_attributes_on_frame_elements;

/** Whether to minimize white space by not indenting,
    and by not wrapping attributes */
extern bool g_minimize_white_space;

/** Whether the "--debug" command line option was specified */
extern bool g_debug;

/** Maximum stack frame depth, if any (0 means no limit) */
extern int g_max_stack_depth;

/** Whether to limit samples to a specified pid */
extern std::string g_filter_pid;

/** Whether to limit samples to a specified event_name */
extern std::string g_filter_event;

/** Root directory for debuginfo lookup */
extern Pathname g_debuginfo_root_directory;

/** Directory to use for debuginfo lookup */
extern Pathname g_debuginfo_directory;
