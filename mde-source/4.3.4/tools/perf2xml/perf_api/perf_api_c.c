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
// perf_api_c.c -- Perf Events C/C++ Bridge API
// ============================================================================

#include "perf_api.h" // C API declarations

// C includes

// perf_events includes


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// C includes
#include <fcntl.h>   // O_RDONLY
#include <errno.h>
#include <string.h>

// perf includes
#include <perf.h>
#include <session.h>
#include <parse-events.h>
#include <util.h>
//#include <sort.h> // conflicting definition of 'struct option'
extern int sort_dimension__add(const char *);
#include <symbol.h>
#include <evsel.h>
#include <evlist.h>
#include <tool.h>


// ----------------------------------------------------------------------------
// notes on libperf.a sources
// ----------------------------------------------------------------------------

// type/function:        ==> see source file:

// union perf_event      ==> perf/util/event.h
// struct sample_event   ==> perf/util/event.h
// struct perf_sample    ==> perf/util/event.h
// struct addr_location  ==> perf/util/symbol.h
// struct map_symbol     ==> perf/util/symbol.h
// struct map            ==> perf/util/map.h
// struct dso            ==> perf/util/dso.h

// struct perf_evsel     ==> perf/util/evsel.h
// const char *__event_name(int type, u64 config)         ==> perf/util/parse-events.c
// const char *perf_evsel__name(struct perf_evsel *evsel) ==> perf/util/evsel.c

// struct ip_callchain   ==> perf/perf.h
// __thread struct callchain_cursor callchain_cursor ==> perf/util/callchain.h/c
// struct callchain_cursor_node      ==> perf/util/callchain.h
// callchain_append()                ==> perf/util/callchain.c
// machine__resolve_callchain()      ==> perf/util/machine.c

// struct perf_event_header ==>
//   sys/linux/source/include/uapi/linux/perf_event.h

// struct perf_event_attr ==> 
//   sys/linux/source/include/uapi/linux/perf_event.h

// enum perf_event_type { PERF_RECORD_MMAP, ... } ==>
//   sys/linux/source/include/uapi/linux/perf_event.h

// #define PERF_RECORD_MISC_CPUMODE_MASK                (7 << 0)
// #define PERF_RECORD_MISC_USER                        (2 << 0)
//   sys/linux/source/include/uapi/linux/perf_event.h

// static int symbol__get_source_line() ==>
//   perf/util/annotate.c

// perf_evsel__print_ip() ==> perf/util/session.c


// ----------------------------------------------------------------------------
// global state
// ----------------------------------------------------------------------------

/** Current sample file pathname. */
static const char* g_current_sample_file = NULL;

/** Callback, if any, to invoke for each sample. */
static process_sample_callback g_sample_callback = NULL;


// ----------------------------------------------------------------------------
// utilities
// ----------------------------------------------------------------------------

/** String equality test */
static inline bool streql(const char* s1, const char* s2)
{
  return (strcmp(s1, s2) == 0);
}

/** String prefix test */
static inline bool starts_with(const char* s, const char* prefix)
{
  return (strncmp(s, prefix, strlen(prefix)) == 0);
}

#define MAX_PATH_LEN FILENAME_MAX

/** Get parent directory from specified pathname. */
static void parent_dir(char* result, const char* pathname)
{
  char* index = strrchr(pathname, '/');
  result[0] = '\0';
  if (index != NULL)
  {
    // Copy prefix of string up to, but not including, last slash.
    strncat(result, pathname, index - pathname);
  }
  else 
  {
    // Filename with no slashes, return "." as parent directory.
    strcpy(result, ".");
  }
};

/** Append suffix to parent directory of specified pathname. */
static void parent_dir_path(char* result, const char* pathname, const char* suffix)
{
  parent_dir(result, pathname);
  if (suffix != NULL && strlen(suffix) > 0) 
  {
    if (suffix[0] != '/')
    {
      strcat(result, "/");
    }
    strcat(result, suffix);
  }
}

/** Returns true if path exists. */
static int
path_exists(const char* path)
{
  // Verify the file.
  struct stat sbuf;
  return (stat(path, &sbuf) == 0);
}


// ----------------------------------------------------------------------------
// assorted perf-internal global setup
// ----------------------------------------------------------------------------

const char perf_version_string[] = "dummy";
int use_browser = 0;

#include <debug.h>
#include <bfd.h> // bfd_demangle


// ----------------------------------------------------------------------------
// perf_event_type_name()
// ----------------------------------------------------------------------------

/** Converts perf event type to string name for display. */
const char*
perf_event_type_name(int type)
{
  const char* result = "UNKNOWN_EVENT_TYPE";
  // PERF_RECORD_(type) constants are defined in:
  // sys/linux/source/include/uapi/linux/perf_event.h
  switch (type)
  {
  case PERF_RECORD_MMAP:       // 1
    result = "PERF_RECORD_MMAP"; break;
  case PERF_RECORD_LOST:       // 2
    result = "PERF_RECORD_LOST"; break;
  case PERF_RECORD_COMM:       // 3
    result = "PERF_RECORD_COMM"; break;
  case PERF_RECORD_EXIT:       // 4
    result = "PERF_RECORD_EXIT"; break;
  case PERF_RECORD_THROTTLE:   // 5
    result = "PERF_RECORD_THROTTLE"; break;  
  case PERF_RECORD_UNTHROTTLE: // 6
    result = "PERF_RECORD_UNTHROTTLE"; break;
  case PERF_RECORD_FORK:       // 7
    result = "PERF_RECORD_FORK"; break;
  case PERF_RECORD_READ:       // 8
    result = "PERF_RECORD_READ"; break;
  case PERF_RECORD_SAMPLE:     // 9
    result = "PERF_RECORD_SAMPLE"; break;
  }
  return result;
}


// ----------------------------------------------------------------------------
// copy_symbol_name()
// ----------------------------------------------------------------------------

/** Copies name from a symbol into specified buffer. */
int
copy_symbol_name(char* buffer, struct symbol *sym)
{
  int len = 0;
  if (sym != NULL)
  {
    len = sym->namelen;
    strncpy(buffer, sym->name, len);
  }
  buffer[len] = '\0';
  return len;
}


// ----------------------------------------------------------------------------
// copy_event_name()
// ----------------------------------------------------------------------------

/** Gets symbolic name, if any, for event. */
void
copy_event_name(char* buf, int maxlen, struct perf_evsel *evsel)
{
  // NOTE: We would _like_ to just use perf's perf_evsel__name() function,
  // as defined in perf/util/evsel.c, but if we just use the const char*
  // returned by it, we sometimes got an unexplained segfault elsewhere
  // in BFD, with the 2.6.38 version.
  // (No obvious reason, from code inspection event_name()
  // _should_ just return an unchanging string constant.)
  // FIXME: should retest with Linux 3.0.

  const char* event_name = (evsel ? perf_evsel__name(evsel) : "unknown_event");
  strncpy(buf, event_name, maxlen);
  buf[maxlen] = '\0';
}


// ----------------------------------------------------------------------------
// process_sample_event()
// ----------------------------------------------------------------------------

/** Invoked for each PERF_RECORD_SAMPLE event from the file. */
int
process_sample_event(struct perf_tool *tool,
                     union perf_event *event,
                     struct perf_sample *sample,
                     struct perf_evsel *evsel,
                     struct machine *machine)
{
  // Let the perf_event library code process the event for us.
  struct addr_location al;
  if (perf_event__preprocess_sample(event, machine, &al, sample, NULL) < 0)
  {
    fprintf(stderr, "Skipping %s event.\n",
            perf_event_type_name(event->header.type));
    return -1;
  }
  if (al.filtered) return 0;

  // Get cpu, process, thread ids.
  u32 cpu = sample->cpu;
  u64 tid = sample->tid;
  u64 pid = sample->pid;
  u64 ip  = sample->ip;

#define MAX_EVENT_NAME_LEN 255
// This needs to be somewhat excessive because
// demangled C++ can be quite voluminous.
#define MAX_FUNCTION_NAME_LEN 1024

  // Get event id / name
  int event_id = evsel->attr.config;
  char event_name_buf[MAX_EVENT_NAME_LEN+1];
  copy_event_name(event_name_buf, MAX_EVENT_NAME_LEN, evsel);

  // Process callchain information.
  // NOTE: we store the sample's own address information
  // as frame 0 in the callchain array.

  // Get module path for this sample.
  // Note: this is the exe/lib in which the sample occurred,
  // which may be different from the top-most module on the stack.
  const char* module_path = "unknown";
  perf_vma    module_start_address = 0;
  if (al.map != NULL)    
  {
    module_start_address = al.map->start;
    if (al.map->dso != NULL)
      module_path = al.map->dso->long_name;
  }

  // Get enclosing symbol (i.e. function name).
  char symbol_name[MAX_FUNCTION_NAME_LEN+1];
  int have_symbol = copy_symbol_name(symbol_name, al.sym);
  if (have_symbol <= 0)
  {
    snprintf(symbol_name, MAX_FUNCTION_NAME_LEN, "pc=0x%llx",
             (unsigned long long) al.addr);
  }

  // NOTE: al.addr, al.sym->start, and al.sym->end are
  // all represented as offsets from al.map->start.
  // We reconstruct the actual vma addresses, since that's
  // what we really want in order to do addr2len lookup, etc.

  perf_vma sample_address = (al.sym == NULL) ? ip :
    al.addr + module_start_address;
  perf_vma start_address  = (al.sym == NULL) ? 0  :
    al.sym->start + module_start_address;
  perf_vma end_address    = (al.sym == NULL) ? 0  :
    al.sym->end + module_start_address;

  // Copy the remaining frame data from the callchain.
  perf_sample_frame* callstack = NULL;  
  int callstack_depth = 1;
  bool have_callchain = symbol_conf.use_callchain;
  struct ip_callchain* callchain = sample->callchain;
  if (! have_callchain || callchain == NULL)
  {
    // we only have the sampled address,
    // so the "callchain" has only a single frame
    callstack = malloc(sizeof(perf_sample_frame)*callstack_depth);
    memset(callstack,0,sizeof(perf_sample_frame)*callstack_depth);

    // Store the sampled address info as frame[0].
    callstack[0].address       = sample_address;
    callstack[0].module        = strdup(module_path);
    callstack[0].symbol        = strdup(symbol_name);
    callstack[0].start_address = start_address;
    callstack[0].end_address   = end_address;
  }
  else
  {
    // Handle actual callstack information.
    // (This is informed by perf_evsel__print_ip() in perf/util/session.c)

    // The callchain is the sequence of callstack frames,
    // with the sample address frame first.
    machine__resolve_callchain(machine, evsel, al.thread, sample, NULL);

    // The "callchain cursor" iterates over "nodes" in the chain.
    callchain_cursor_commit(&callchain_cursor);

    // have to pick up "nr" from callchain cursor,
    // because cursor may skip "uninteresting" callstack entries
    int nr = callchain_cursor.nr;
    callstack_depth += nr;

    // Allocate space for entire stack (including sample's address info).
    callstack = malloc(sizeof(perf_sample_frame)*callstack_depth);
    memset(callstack,0,sizeof(perf_sample_frame)*callstack_depth);

    // Store the sample's address info as frame[0].
    callstack[0].address       = sample_address;
    callstack[0].module        = strdup(module_path);
    callstack[0].symbol        = strdup(symbol_name);
    callstack[0].start_address = start_address;
    callstack[0].end_address   = end_address;

    struct callchain_cursor_node *cursor_node = NULL;
    cursor_node = callchain_cursor_current(&callchain_cursor);

    for (int i=1; i<=nr && cursor_node; ++i)
    {
      // vma of this callstack frame
      u64 ip = cursor_node->ip;

      // Get the module (if known) for this frame.
      const char* frame_module_path = "unknown";
      perf_vma frame_module_start_address = 0;
      if (cursor_node->map != NULL)
      {
        frame_module_start_address = cursor_node->map->start;
        if (cursor_node->map->dso != NULL)
          frame_module_path = cursor_node->map->dso->long_name;
      }

      // Get the function (if known) for this stack frame.
      char frame_symbol[MAX_FUNCTION_NAME_LEN+1];
      int have_frame_symbol =
        copy_symbol_name(frame_symbol, cursor_node->sym);
      if (have_frame_symbol <= 0)
      {
        snprintf(frame_symbol, MAX_FUNCTION_NAME_LEN, "pc=0x%llx",
                 (unsigned long long) ip);
      }

      // NOTE: cursor_node->sym->start, and cursor_node->sym->end are
      // represented as offsets from cursor_node->map->start.
      // We reconstruct the actual vma addresses, since that's
      // what we really want in order to do addr2len lookup, etc.
      perf_vma frame_address = ip;
      perf_vma start_address = (cursor_node->sym == NULL) ? 0 :
        cursor_node->sym->start + frame_module_start_address;
      perf_vma end_address   = (cursor_node->sym == NULL) ? 0 :
        cursor_node->sym->end + frame_module_start_address;

      callstack[i].address       = frame_address;
      callstack[i].module        = strdup(frame_module_path);
      callstack[i].symbol        = strdup(frame_symbol);
      callstack[i].start_address = start_address;
      callstack[i].end_address   = end_address;

      // Keep track of "topmost" non-kernel module path that we've seen.
      if (! streql(frame_module_path, KERNEL_KALLSYMS_MODULE_NAME) &&
          // HACK: disallow libc/libpthread from "owning" functions
          // from user process/thread executables.
          ! starts_with(frame_module_path, "/lib/libc-") &&
          ! starts_with(frame_module_path, "/lib/libpthread-") &&
          ! streql(frame_symbol, "__libc_start_main"))
      {
        module_path = frame_module_path;
      }

      callchain_cursor_advance(&callchain_cursor);
      cursor_node = callchain_cursor_current(&callchain_cursor);
    }
  }

  // Assemble API data structure from stuff we've found.
  perf_sample_data ps;

  // populate visible fields
  ps.sample_file     = g_current_sample_file;
  // Note: "module" reported for sample as a whole
  // is topmost non-kernel module path seen on callstack.
  ps.module          = module_path;
  ps.cpu             = cpu;
  ps.pid             = pid;
  ps.tid             = tid;
  ps.event_id        = event_id;
  ps.event_name      = event_name_buf;
  ps.callstack_depth = callstack_depth;
  ps.callstack       = callstack;

  // Now that we have everything, call the processing function.
  int result = g_sample_callback(&ps);

  // Clean up.
  ps.module = NULL;
  ps.event_name = NULL;
  for (int i=0; i<ps.callstack_depth; ++i)
  {
    // Free allocated string attributes.
    if (ps.callstack[i].module != NULL) free((void*) ps.callstack[i].module);
    if (ps.callstack[i].symbol != NULL) free((void*) ps.callstack[i].symbol);
  }
  if (ps.callstack != NULL)
  {
    free(ps.callstack);
    ps.callstack = NULL;
  }
  ps.callstack_depth = 0;

  return result;
}


// ----------------------------------------------------------------------------
// process_sample_file()
// ----------------------------------------------------------------------------

/** Processes sample file */
int process_sample_file(
  const char* sample_file_pathname,
  process_sample_callback sample_event_callback
)
{
  int status = -1;

  // initialize global var(s) used internally by perf code
  page_size = sysconf(_SC_PAGE_SIZE);

  // Set buildid dir. (Typically defaults to $HOME/.debug)
  set_buildid_dir();

  // See if there's a "kallsyms" file in the same directory as the sample data.
  char kallsyms_path[MAX_PATH_LEN];
  parent_dir_path(kallsyms_path, sample_file_pathname, "kallsyms");
  if (path_exists(kallsyms_path))
  {
    // If so, we'll use it to look up OS symbols, etc.
    symbol_conf.kallsyms_name = strdup(kallsyms_path);
  }
  else
  {
    // Otherwise, see if there's a /proc/kallsyms we can use.
    symbol_conf.kallsyms_name = "/proc/kallsyms";
  }

  // Set vmlinux path to use. (Note: have to do this _before_ symbol_init().)
  symbol_conf.vmlinux_name = "/boot/vmlinux";
  symbol_conf.try_vmlinux_path = false; // don't bother searching for it

  // Initialize symbol-processing layer.
  if (symbol__init() < 0) return status;
  symbol_conf.exclude_other = false;

  // Initialize sorting.
  sort_dimension__add("pid");
  sort_dimension__add("dso");
  sort_dimension__add("symbol");

  // Set up list of callbacks for sample "events" in file.
  // Most of these just call internal boilerplate handlers.
  // The exceptions are starred below.
  static struct perf_tool tool;
  memset(&tool, 0, sizeof(tool));
  tool.sample       = process_sample_event; // (* sample handler)
  tool.mmap         = perf_event__process_mmap;
  tool.comm         = perf_event__process_comm;
  tool.exit         = perf_event__process_exit;
  tool.fork         = perf_event__process_fork;
  tool.lost         = perf_event__process_lost;
  tool.read         = NULL;                 // (* ? not needed)
  tool.attr         = perf_event__process_attr;
  tool.event_type   = perf_event__process_event_type;
  tool.tracing_data = perf_event__process_tracing_data;
  tool.build_id     = perf_event__process_build_id;
  tool.ordered_samples = true;
  tool.ordering_requires_timestamps = true;

  // Open the sample file.
  struct perf_session *session =
    perf_session__new(
      sample_file_pathname, O_RDONLY, false, false, &tool);
  if (session == NULL)
  {
    printf("Could not open sample file '%s'\n", sample_file_pathname);
    return status;
  }

  // Check whether sample data has callgraph (-g) information.
  u64 sample_type = perf_evlist__sample_type(session->evlist);
  if (sample_type & PERF_SAMPLE_CALLCHAIN)
    symbol_conf.use_callchain = true;
  else
    symbol_conf.use_callchain = false;

  // Set state used by process_sample_event function.
  g_current_sample_file = sample_file_pathname;
  g_sample_callback = sample_event_callback;

  // Iterate over sample file "events".
  // This calls process_sample_event() for each sample.
  perf_session__process_events(session, &tool);

  // Clear state used by process_sample_event function.
  g_sample_callback = NULL;
  g_current_sample_file = NULL;

  // Close the sample file
  perf_session__delete(session);

  status = 0;

  return status;
}

// ----------------------------------------------------------------------------
// miscellaneous
// ----------------------------------------------------------------------------

// HACK: Provide definitions for these functions from builtin-diff.o,
// which are not included in libperf.a, etc.
// We don't use them, so we don't need them to work.

int perf_diff__formula(struct hist_entry *he, struct hist_entry *pair,
		       char *buf, size_t size)
{
  return -1;
}

double perf_diff__compute_delta(struct hist_entry *he, struct hist_entry *pair)
{
  return 0.0;
}

double perf_diff__compute_ratio(struct hist_entry *he, struct hist_entry *pair)
{
  return 0.0;
}

s64 perf_diff__compute_wdiff(struct hist_entry *he, struct hist_entry *pair)
{
  return 0;
}

double perf_diff__period_percent(struct hist_entry *he, u64 period)
{
  return 0.0;
}
