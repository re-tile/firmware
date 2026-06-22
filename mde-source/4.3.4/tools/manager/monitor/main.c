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

// This file defines the "monitor".


// The "shepherd" (see "tools/manager/shepherd/") is a TILE process which
// manages other processes, and the "monitor" (see "tools/manager/monitor/")
// is a HOST process which interacts with the shepherd, and, optionally, the
// "ide" and/or various "gdb" instances.

// Every process has a process id ("pid"), which is used to identify that
// process in various messages. NOTE: The pid is also used as the "tag"
// for special RSP packets related to that process.

// ISSUE: Technically "pid_t" is a 32 bit value, but we assume that all
// legal pids will fit in "uint16_t".

// The monitor can tell the shepherd to "launch" a new process, for example,
// in response to the "launch" or "run" commands. This process, together
// with all of its known descendants, are known as an "application".

// An iLib process can tell the shepherd to help it "spawn" (or "exec")
// some new processes, as part of the same "application".

// The shepherd handles each "launch" request by forking and execing
// the requested process, and sending "process_created" and
// "thread_created" to the monitor.

// The shepherd sends "process_destroyed" to the monitor whenever a
// process "exits" (including when it crashes) and its console, if
// any, has been closed.

// The shepherd sends "application_destroyed" to the monitor whenever
// an application is "reaped" (i.e. all of its processes have exited).

// The monitor can send a "request_idle" query, and the next time there are
// no processes running, the shepherd will send back a "note_idle" query.

// A process can be "debuggable", which means that it can be controlled by
// a "gdb" process via RSP traffic. A process can be created in this state,
// in which case it stops before it actually executes any code, or it can
// be "attached" to, which will stop its execution, or "--debug-on-crash"
// can be used to make it become debuggable at the moment it would otherwise
// crash due to certain signals.

// In any case, once a process becomes "debuggable", the shepherd sends
// "note_attach", so the monitor (or IDE) can associate a "gdb" process
// with the debuggable process.  When the "gdb" session is complete, an
// empty RSP packet will be sent to the shepherd, informing it that a
// new gdb process can be requested via "note_attach".

// The "debug-tile" command causes processes in the application created by
// the next "run" which end up on the designated tile to be "debuggable".

// The monitor can be given "--debug-exe" arguments, which cause any
// process "spawned" with the given executable to be "debuggable".

// The monitor can send a "debug_next" query, which will cause the next
// spawned process, and any of its descendants, to be "debuggable". This
// is used by the "debug" command.

// When the monitor learns that a debuggable process has been spawned, it
// creates a listener that will accept a single connection from some "gdb"
// process, and associates both with the new process.

// The monitor informs the "ide" of everything it observes.

// The monitor will initiate a clean exit when (1) it gets a Ctrl+D from
// stdin or (2) it is not using stdin and the shepherd becomes idle. This
// involves telling the shepherd to "quit", and then exiting when it says
// that it is about to exit.

// NOTE: The IDE is not allowed to send (normal) packets whose tag has the
// high bit set. This will allow the monitor to use such tags for its own
// private communication with the shepherd, while being able to pass other
// packets directly between the ide and the shepherd.


// The "peer" mechanism allows two monitors to synchronize with each other,
// using "peer-barrier" to wait for each other to reach a given point in
// the command line, and "quitting" in a synchronized manner.

// The "peer" mechanism is mainly used to allow two simulated chips to
// communicate with each other over simulated shims, and to allow such
// simulators to "auto-synchronize" using a "heartbeat" mechanism, in
// which each simulator only runs for a certain number of cycles, and
// then "freezes" until the other simulator has also run for a certain
// number of cycles.

// The "heartbeat" mechanism must interact very carefully with the
// "quit" mechanism, to avoid either simulator getting "stranded".
// Worse, we must also avoid errors due to trying to send a heartbeat
// to a peer after that peer has exited.  This is complicated by the
// fact that once a heartbeat has been started, you cannot "cancel"
// it, and instead, must wait for it to finish.

// (1) Once "handle_command_quit()" is called, "g_quitting" is set,
// and we send "peer_quit" to our peer.  From this moment on we will
// never send a heartbeat to our peer, and we will ignore heartbeats
// from our peer.  If we were waiting for a heartbeat from our peer,
// then we "perma-start" our own heartbeat.  Otherwise, we will do
// this when our current heartbeat finishes.

// (2) Once we receive "peer_quit", "g_peer_quitting" is set, and we
// will never send a heartbeat to our peer, and we will never get any
// heartbeats from our peer.  If we were waiting for a heartbeat from
// our peer, then we "perma-start" our own heartbeat.  Otherwise, we
// will do this when our current heartbeat finishes.


// TODO: The monitor could implicitly generate RSP "acks" for "gdb", and
// ignore them from "gdb", and then the shepherd could just pretend that
// there were no "acks".

// ISSUE: If "request_exit_if_needed()" somehow gets called after we have
// already closed the shepherd socket (consider SIGTERM), then something
// bad will presumably happen in "do_request_exit".

// NOTE: If the shepherd crashes, then the monitor will not notice, if it
// is using "--pci" with actual hardware, or if it is using the simulator
// and there are any other processes keeping the simulator alive. We could
// add a new "exit unless process is a fresh child" query, and/or maybe a
// "you are no longer a fresh child" query. And/or add "keepalive" checks.


// ISSUE: Consider tracking "most recently sent reply/error tag" (and
// clearing it before handling any query just to be safe), so that we
// can throw an error if a "duplicate" reply/error is sent.

// TODO: Find a cleaner way to handle the "upload.c" and "download.c" files.

// We use the term "simulator" to describe the actual "tile-sim" process.

// ISSUE: If the monitor tries to "exit" when "gdb" processes are still
// attached, then the shutdown mechanism often gets confused.

// ISSUE: When using PCI, occasionally "ping" the shepherd, to detect
// when the shepherd (or linux) crashes.

// ISSUE: Perhaps "debug_tiles" should only affect spawned children,
// because otherwise, "run" on a 1x1 tile would be affected.

// NOTE: In a "pipe_pair" (from "pipe()"), you always read from the first
// pipe, and write to the second pipe.

// TODO: Add command line commands to the "readline history"?

// TODO: Strip "duplicate" commands from the "readline history"?

// ISSUE: Should any more "dispatch_events(-1)" calls be "handle_events()"?
// What about vice versa?

// ISSUE: On exit, we should call "expire"/"destroy" on all ConsoleStreams,
// after first pausing for a second to handle any pending traffic.

// ISSUE: Should even more commands be made "synchronous"?

// ISSUE: The current handling of the "env" command, where a naked "var"
// tells the monitor to forget about any override of the variable "var",
// instead of sending it to the shepherd, means that there is no way to
// actually tell the shepherd to "undefine" an environment variable.

// ISSUE: Need some documentation about "--local-gdb".

// NOTE: Apparently "gdb" completely ignores SIGQUIT.

// NOTE: If "gdb" is "waiting", SIGTERM will cause a "$k#XX" RSP packet
// to be sent, even though this is not supposed to be legal.

// NOTE: Sending SIGHUP to "gdb" will cause it to exit (without resetting
// the terminal).  This will not happen until "gdb" has stopped "waiting",
// for example, by reacting to SIGINT by interrupting the remote process.

// XXXXX: FIXME: Interrupting commands which use the result of "querify",
// such as "cd", will yield a monitor/shepherd state inconsistency!

// FIXME: Mention threads (and locations) in SIGQUIT backtraces.

// ISSUE: Should "note_crash" include "tid"?

// When FORCING a multi-threaded process, make sure to report the
// first thread, so "info threads" can be used "usefully".

// ISSUE: Add support for "--strip" (?) to strip binaries during upload.

// TODO: Add a "--profile-run" command that wraps "--profile-start" and
// "--profile-stop" around "--run".


#include "tools/handy/handy.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <utime.h>

// For "openpty()" and "login_tty()" in "-lutil".
#include <pty.h>
#include <utmp.h>

// For "tcgetattr()" (currently unused).
//--#include <termios.h>

// For "dlopen()" and "dlsym()" in "-ldl".
#include <dlfcn.h>

#include <elf.h>

#include <arch/chip.h>

#include <net/if.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/statvfs.h>

#include "tools/manager/monitor/gen_rpc.h"

#include "tools/manager/shepherd/defs.h"


//! HACK: The max chip width.
#define MAX_WIDTH 16

//! HACK: The max chip height.
#define MAX_HEIGHT 16


//! Help avoid memory leaks.
#define FREESET(VAR,VAL) do { free((void*)VAR); VAR = (VAL); } while (0)


//! Values for "auto_bt" in "Process".
//!
typedef enum {
  AUTO_BT_NONE = 0,
  AUTO_BT_QUERY = 1,
  AUTO_BT_READY = 2,
  AUTO_BT_ACTIVE = 3,
  AUTO_BT_DUMPING = 4
} auto_bt_t;


//! Shorthand.
typedef struct _ConsoleStream ConsoleStream;

//! Shorthand.
typedef struct _Debugger Debugger;

//! Shorthand.
typedef struct _Process Process;

//! Shorthand.
typedef struct _Thread Thread;


//! Represents a console stream.
//
struct _ConsoleStream
{
  //! A process (if this is "stdout_stream" or "stderr_stream" for that
  //! process), or NULL.
  Process* process;

  //! A debugger (if this is "local_gdb_stream" for that debugger), or NULL.
  Debugger* debugger;

  //! A file descriptor (STDOUT_FILENO or STDERR_FILENO).
  int fd;

  //! A tag for each line (or NULL).
  const char* tag;

  //! Standard "prompts" (if any).
  StringArray prompts;

  //! Most recent prompt, or NULL.
  const char* prompted;

  //! True if partial line already emitted.
  bool partial;

  //! Incoming data (a pending partial line).
  Buffer pending;

  //! Alarm used to flush partial lines.
  Alarm alarm;
};


//! Information about a "debugger" session.
//!
struct _Debugger
{
  const char* name;

  // A local executable, or a hacky remote executable, or NULL.
  const char* executable;

  // For a "process" debugger, the "process", else NULL.
  Process* process;

  // For a "tile" debugger, the tile coordinates, else undefined.
  uint16_t x;
  uint16_t y;


  // Commands to be sent to "gdb".
  StringArray gdb_commands;


  // Listener for gdb.  Open only until socket has been accepted.
  Pollable gdb_listener;

  // Socket to gdb.
  Pollable gdb_socket;


  // The local gdb pid (or -1).
  pid_t local_gdb_pid;

  // The local gdb PTY.
  Pollable local_gdb_pty;

  // The local gdb console stream.
  ConsoleStream local_gdb_stream;

  // True if expecting a local gdb prompt.
  bool local_gdb_expecting_prompt;


  // If local gdb stdin interaction is allowed, 'g' or 't', else '\0'.
  char stdin_mode;

  // For a "process" debugger, "process->pid", else "100 * x + y".
  int stdin_arg;
};


//! Represents a remote process.
//
struct _Process
{
  // The process "name" (i.e. "Process NNN").
  const char* name;

  // The pid.
  pid_t pid;

  // True when dead.
  bool dead;

  // The actual threads.
  Array threads;

  // The remote executable.
  const char* executable;

  // The "stdout" for the process.
  ConsoleStream stdout_stream;

  // The "stderr" for the process.
  ConsoleStream stderr_stream;

  // The "debugger", if being debugged.
  Debugger* debugger;

  // See "auto_bt_advance()".
  auto_bt_t auto_bt;
};


//! Represents a remote thread.
//
struct _Thread
{
  pid_t tid;

  uint16_t x;
  uint16_t y;

  const char* state;
};



//! Shorthand.
typedef struct _Child Child;

//! Represents a local child process.
//
struct _Child
{
  int pid;
};



#ifndef __tile__

// Basic command for "--config CONFIG --raw-mode".
static char* simulator_argv_for_raw_mode[] = {
  "@INSTALL@/bin/tile-sim",
  "--sim-socket-port", "@SHEPHERD_PORT@",
  "--sim-socket-port", "@WATCHDOG_PORT@",
  "--reporter-port", "@REPORTER_PORT@",
  "--config", "@CONFIG@",
  "--extra-shim-args", "@BOOT_SHIM@",
  NULL
};

// Basic command for "--config CONFIG --bootrom-file".
static char* simulator_argv_for_bootrom_file[] = {
  "@INSTALL@/bin/tile-sim",
  "--sim-socket-port", "@SHEPHERD_PORT@",
  "--sim-socket-port", "@WATCHDOG_PORT@",
  "--reporter-port", "@REPORTER_PORT@",
  "--config", "@CONFIG@",
  "--naked-boot",
  "--no-magic-mshim-lookup-table",
  "--extra-shim-args", "@BOOT_SHIM@",
  NULL
};

// Basic command for "--config CONFIG --magic-hypervisor".
static char* simulator_argv_for_magic_hypervisor[] = {
  "@INSTALL@/bin/tile-sim",
  "--sim-socket-port", "@SHEPHERD_PORT@",
  "--sim-socket-port", "@WATCHDOG_PORT@",
  "--reporter-port", "@REPORTER_PORT@",
  "--magic-supervisor", "false",
  "--magic-hypervisor", "true",
  "--magic-no-input", "true",
  "--config", "@CONFIG@",
  "--repeat-binary",
  "--binary", "@VMLINUX@",
  NULL
};

// Basic command for "--image-file" (or "--image IMAGE").
static char* simulator_argv_for_image_file[] = {
  "@INSTALL@/bin/tile-sim",
  "--sim-socket-port", "@SHEPHERD_PORT@",
  "--sim-socket-port", "@WATCHDOG_PORT@",
  "--reporter-port", "@REPORTER_PORT@",
  "--load-from-checkpoint", "@IMAGE_FILE@",
  "--rshim-restore-output", "@CONSOLE@",
  NULL
};

#endif


//! The standard XML intro.
static const char g_xml_intro[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";


//! The "--config" argument, if any.
static char* g_opt_config;

//! If true, avoid booting.
static bool g_opt_resume;

//! If true, capture the console.
static bool g_opt_console;

//! If true, failure to open the console is not fatal.
static bool g_opt_try_console;

//! If true, run simulator raw mode.
static bool g_opt_raw_mode = false;

//! If non-null, send console output here.
static char* g_opt_console_out;

//! If true, the monitor communicates with the shepherd on the networked host
//! through a ssh tunnel.
static bool g_opt_ssh;

//! The argument to use to unlock the console on exit, if necessary.
static char* g_unlock_console;

//! The device "name" ("/pci*", "/gxpci*", "/usb*", "@HOST", "@HOST#NODE"),
//! or NULL (for simulator).  See "tile-dev" for more info.
static char* g_dev_name;

//! The device "driver" ("tilepci" for "/pci*", "tilegxpci" for "/gxpci*",
//! or "tileusb" for "/usb*"), or NULL.
static char* g_dev_driver;

//! The device "devdir" ("/dev/tilepci*[-link1]" for "/pci*",
//! "/dev/tilegxpci*[-link1]" for "/gxpci*", "/dev/tileusb*" for "/usb*"),
//! or NULL.
static char* g_dev_devdir;

//! True if "g_dev_name" starts with "/pci" or "/gxpci".
static bool g_dev_is_pci;

#if TILE_CHIP >= 10
//! String containing the PCIe endpoint port's global link index.
static char *g_pci_link_index;

//! String containing the PCIe endpoint port's P2P NIC#0's IP address.
static char *g_pci_nic_addr;

//! Flag indicating whehter the PCIe endpoint port's P2P NIC is configured and
//! brought up automatically.
static uint g_pci_nic_config;
#endif

//! Remote BMC host (or NULL).
//! This is non-NULL only if "g_dev_name" has the form "@host#node",
//! which requests the use of the given host/node BMC pair.
static char* g_dev_bmc_host;

//! Remote BMC port.
static int g_dev_bmc_port;

//! If true, "--self" was specified.
static bool g_opt_self = false;

//! Timeout for greeting the shepherd, in seconds.
static int g_greet_timeout = 200;

//! Timeout for pushing in the boot stream, in seconds.
static int g_boot_timeout = 200;

//! If true, ignore shepherd protocols.
static bool g_ignore_protocol;

//! If true, allow "shepherd --execute".
static bool g_allow_execute;

//! If true, "readline" is "desired" but not yet initialized.
static bool g_readline_desired;

//! If true, "readline" is "desired" and successfully initialized.
static bool g_readline_ready;

//! If true, create local gdb sessions.
static bool g_local_gdb;

//! If non-zero, use as the next "gdb port".
static uint g_gdb_port;

//! Extra commands to feed to "gdb".
static StringArray g_gdb_commands;

//! If true, emit a special prefix before stdout/stderr from remote
//! processes, and before stdout from local gdb children.
static bool g_tag_console;

//! True if a new prompt may be needed.
static bool g_prompt_needed;

//! True if the new prompt must be redrawn.
static bool g_prompt_redraw;

//! Used by "display_prompt_if_needed()".
static Buffer g_prompt_buffer;

//! True if we have finished greeting the shepherd and such.
static bool g_greeted;

//! True if we have finished processing "commands" from the command
//! line, and have entered into the normal event loop.
static bool g_running;

//! Activate various "testing" support.
static bool g_testing;

//! Width of the chip, in tiles.
static uint g_width;

//! Height of the chip, in tiles.
static uint g_height;

//! Target info (if known).
static char* g_target_info;

//! Array of processes.
static Array g_processes;

//! Array of local children.
static Array g_children;

//! The tile debuggers.
static Debugger* g_tile_debuggers[MAX_WIDTH][MAX_HEIGHT];

//! Array of killed debuggers waiting to be reaped.
static Array g_killed_debuggers;

//! Alarm for shepherd greeting.
static Alarm g_greet_alarm;

// Functions looked up in "libedit_dll".
static void (*rl_callback_handler_install)(char* prompt, void (*hook)(char*));
static void (*rl_callback_handler_remove)(void);
static void (*rl_callback_read_char)(void);
static void (*rl_forced_update_display)(void);
static void (*add_history)(char* cmd);


// Socket to the shepherd.
static Pollable g_shepherd_socket;

// Socket to the watchdog (simulator only).
// No queries are sent directly to this socket.
static Pollable g_watchdog_socket;

// Socket to the reporter (simulator only).
static Pollable g_reporter_socket;

// Socket to our "peer" (another monitor).
static Pollable g_peer_socket;

// Socket to the ide.
static Pollable g_ide_socket;

// Optional command-line socket for driving tile-monitor externally.
static Pollable g_external_command_socket;


// A special console, normally a PTY used by a spawned simulator,
// or a serial device attached to a PCI card.
static Pollable g_console;

// Collect output from "g_console".
static ConsoleStream g_console_stream;


// The "stdout" from the shepherd (and sub-processes).
static ConsoleStream g_shepherd_stdout_stream;

// The "stderr" from the shepherd (and sub-processes).
static ConsoleStream g_shepherd_stderr_stream;

// The console output polling thread.
static pthread_t g_console_tid;

// Pollable for stdin.
static Pollable g_stdin_reader;

// The "redirection mode" for stdin.
static char g_stdin_mode;

// The "redirection argument" for stdin.
static int g_stdin_arg = -1;

// HACK: A magic "stdin prefix" for stdin.
// ISSUE: Should this affect all stdin modes?
static const char* g_stdin_prefix;

//! Array of "forward" sockets.
static Array g_forward_sockets;

// Array of "tunnel" sockets (each one is a Pollable, whose "info"
// is the index in this array, which is used as its "id").
static Array g_tunnel_sockets;

//! The desired exit code.
static uint g_exit_code;

//! Total number of launches.
static uint g_total_launches;

//! HACK: The app whose reaping we await.
static uint g_awaiting_reap;

//! Set true when waiting, and set false by SIGINT.
static bool g_waiting;

//! Count interrupts.
static uint g_signal_count;

//! True if we can just exit on (most) signals.
static bool g_signal_immediate;

//! True if we want to stop processing command line commands.
static bool g_stop_processing_commands;

//! True if we want to quit.
static bool g_want_quit;

//! True if "handle_command_quit()" has been called.
static bool g_quitting;

//! True if we have asked the shepherd to exit.
static bool g_requested_exit;

//! True if "note_idle" has been observed but not yet handled.
static bool g_pending_note_idle;

//! True if "become_interactive" has been sent to the shepherd.
static bool g_become_interactive_sent;

// The number of times we have sent "peer_barrier".
static uint g_peer_barrier_sent;

// The number of times we have received "peer_barrier".
static uint g_peer_barrier_received;

// True if "peer_quit" query has been received.
static bool g_peer_quitting;

// The number of times we have received "peer_heartbeat".
static uint g_peer_heartbeats;

// The number of times we have received "mon_heartbeat_done".
static uint g_self_heartbeats;

// Protocol sent by the shepherd.
static int g_shepherd_protocol;

//! Shepherd's working directory.
static char* g_shepherd_cwd;

//! Monitor's working directory.
static char* g_monitor_cwd;

//! HACK: See "--bench-stats".
static char* g_bench_stats;

#ifndef __tile__
static StringArray g_prefix_simulator_args;
static StringArray g_extra_simulator_args;
#endif

// The location of the hypervisor binary files (may be relative), if any.
static char* g_hv_bin_dir;

// The location of the Linux binary files (may be relative), if any.
// Note that "--boot-dir" sets both g_boot_dir and g_hv_bin_dir.
static char* g_boot_dir;

// The location of the "classifier" file (may be relative), if any.
static char* g_classifier_file;

// The location of the "vmlinux" file (may be relative), if any.
static char* g_vmlinux_file;

// Set to true if no vmlinux file is requested.
static bool g_no_vmlinux;

// The location of the initramfs file (may be relative), if any.
static char* g_initramfs_file;

// Set to true if no initramfs file is requested.
static bool g_no_initramfs;

static char* g_image_file;
static char* g_bootrom_file;

//! A bootrom file to be deleted at some point, if non-NULL.
static char* g_unlink_bootrom;

//! The pid of the simulator, or zero.
static pid_t g_simulator_pid;

//! Pairs of host-path and tile-path for which "symbol-map" was done,
//! and for which "host-path" was actually a "tile binary".
static StringArray g_symbol_map_paths;

//! Pairs of host-dir and tile-dir for which "mount" was done.
static StringArray g_symbol_map_dirs;

//! The "envp" array for every "launch".
static StringArray g_envp;

//! The "tiles" array for the next "launch".
static StringArray g_tiles;

//! The "debug" array for the next "launch".
static StringArray g_debug;


//! The "--bme" arguments.
static StringArray g_bme_args;


//! Extra suffix for "opcontrol --start".
static char* g_profile_start_kernel;

//! Extra suffix for "opcontrol --start".
static char* g_profile_start_events;

//! Extra suffix for "opcontrol --start".
static char* g_profile_start_flags;

// True to use "oprofile", false to use "perf events".
// Default to using "oprofile".
static bool g_profile_with_oprofile = true;

// True iff user has invoked profile-start but not profile-capture.
// If, for example, they invoke profile-tool while this is true,
// they'll lose whatever data they've captured with the current tool.
static bool g_profile_start_without_capture_warning;


// HACK: If non-empty, at exit, display this message.
static char g_exit_msg[1024];

// If true, display the hostname at exit.
static bool g_display_host_at_exit;

// If non-zero, kill this process at exit.
static pid_t g_kill_pid_at_exit;


// HACK: Support "--mount-tile /sbin".
static bool g_mounted_anything;


// Forward declaration.
static void
auto_bt_advance(void);



// Help parse "-+- ... -+-" style delimiters.
//
static bool
parse_varargs(StringArray* array, char** argv, int* ip)
{
  int i = *ip;

  char* delim = argv[i++];

  if (delim == NULL)
    return false;

  if (has_prefix(delim, "-"))
  {
    while (argv[i] != NULL && strcmp(argv[i], delim) != 0)
      StringArray_append(array, argv[i++]);

    if (argv[i] == NULL)
      return false;

    *ip = i + 1;

    return true;
  }

  return false;
}



static bool
parse_port(uint16_t* valp, const char* str)
{
  char* end;
  unsigned long val = strtoul(str, &end, 10);
  if (*end == '\0' && end != str && val < 65536)
  {
    *valp = val;
    return true;
  }
  struct servent *ent = getservbyname(str, "tcp");
  if (ent != NULL)
  {
    *valp = ntohs(ent->s_port);
    return true;
  }
  return false;
}


static bool
parse_port_or_warn(uint16_t* port, const char* str, bool allow_zero)
{
  if (parse_port(port, str) && (*port != 0 || allow_zero))
    return true;

  warn("Invalid port '%s'.", str);
  return false;
}


// Initialize an abstract unix domain socket for the given name,
// and return the resulting "addr_size".
//
// A tile-monitor-specific prefix is prepended to the name to
// avoid conflicting with other tools.
//
// If the name is too long, this punts.
//
// ISSUE: The caller uses "unix_domain_socket_name", not "name".
//
static size_t
init_abstract_socket(struct sockaddr_un* addr, const char* name)
{
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;

  // Use a special prefix (skipping the trailing '\0') so we don't
  // conflict with abstract sockets from other unrelated programs.
  static const char prefix[] = "\0tile-monitor-peer:";
  const size_t prefix_size = sizeof(prefix) - 1;

  const size_t max_len = sizeof(addr->sun_path) - prefix_size;
  size_t len = strlen(name);
  if (len > max_len)
  {
    punt("Socket name '%s' is too long; max length %zu characters.",
         name, max_len);
  }

  memcpy(addr->sun_path, prefix, prefix_size);
  memcpy(&addr->sun_path[prefix_size], name, len);

  return sizeof(addr->sun_family) + prefix_size + len;
}


// HACK: A copy of "simple_accept()" but without setting "TCP_NODELAY",
// since that is not legal on unix-domain sockets.
//
// FIXME: Move this to "handy", or just make "simple_accept()" smarter.
//
static int
simple_accept_aux(int listener_fd)
{
  int fd;

  while (true)
  {
    fd = accept(listener_fd, NULL, NULL);

    if (fd >= 0)
      break;

    if (errno == EINTR)
      continue;

    punt_with_errno("Failure in 'accept()'");
  }

  set_blocking_or_die(fd, false);

  return fd;
}


static int
listen_and_accept_from_unix_peer(const char* unix_domain_socket_name)
{
  // Create a listener.
  int listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener_fd < 0)
    punt_with_errno("Failure in 'socket()'");

  struct sockaddr_un addr;
  size_t addr_size = init_abstract_socket(&addr, unix_domain_socket_name);

  if (bind(listener_fd, (struct sockaddr*) &addr, addr_size) != 0)
    punt_with_errno("Failure in 'bind()'");

  int backlog = 1;
  if (listen(listener_fd, backlog) != 0)
    punt_with_errno("Failure in 'listen()'");

  int fd = simple_accept_aux(listener_fd);

  set_close_on_exec_or_die(fd, true);

  close_or_die(listener_fd);

  return fd;
}


static int
connect_to_unix_peer(const char* abstract_socket_name)
{
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    punt_with_errno("Failure in 'socket()");

  struct sockaddr_un addr;
  size_t addr_size = init_abstract_socket(&addr, abstract_socket_name);

  for (int i = 60; i >= 0; i--)
  {
    if (connect(fd, (struct sockaddr*) &addr, addr_size) == 0)
      break;

    if (errno == EINTR)
      continue;

    if (i == 0)
      punt_with_errno("Unable to connect to peer");

    // Retry for a while.
    sleep(1);
  }

  set_blocking_or_die(fd, false);

  set_close_on_exec_or_die(fd, true);

  return fd;
}


static int
listen_and_accept_from_inet_peer(uint16_t host_port)
{
  bool ephemeral = (host_port == 0);
  int listener_fd = simple_listen(&host_port, 1);

  // Cause 'accept' to block.
  set_blocking_or_die(listener_fd, true);

  if (ephemeral)
    note("Listening for peer on port %u.", host_port);

  int fd = simple_accept(listener_fd);
  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  close_or_die(listener_fd);

  return fd;
}


// Canonicalize a path, returning a pointer into a static buffer.
//
static char*
canonify_host(const char* path)
{
  static Buffer g_canon;
  canonicalize_path(&g_canon, path, g_monitor_cwd);
  return (char*)g_canon.data;
}


// Canonicalize a path, returning a pointer into a static buffer.
//
static char*
canonify_tile(const char* path)
{
  static Buffer g_canon;
  canonicalize_path(&g_canon, path, g_shepherd_cwd);
  return (char*)g_canon.data;
}


// Determine if a local file exists.
//
// Note that if file is a symbolic link, we test the existence
// of the link, not the file (if any) that it points to,
// since we may, for example, be uploading a link that refers
// to something in the tile virtual file system, and hence
// its target doesn't "exist" until it's uploaded.
//
static bool
file_exists(const char* path)
{
  // Verify the file.
  struct stat sbuf;
  return (lstat(path, &sbuf) == 0);
}

// Determine if a local file represents a "tile binary".
//
static bool
file_is_tile_binary(const char* path)
{
  // Verify the file.
  struct stat sbuf;
  if (stat(path, &sbuf) != 0)
    return false;
  if (!S_ISREG(sbuf.st_mode) || (sbuf.st_mode & S_IXUSR) == 0)
    return false;

  // Open the file.
  FILE* fp = fopen(path, "rb");
  if (fp == NULL)
    return false;

  bool match = false;

  // Acquire the first piece of the elf header.
  unsigned char e_ident[EI_NIDENT];

  if (fread(e_ident, EI_NIDENT, 1, fp) == 1 &&
      (e_ident[EI_MAG0] == ELFMAG0 ||
       e_ident[EI_MAG1] == ELFMAG1 ||
       e_ident[EI_MAG2] == ELFMAG2 ||
       e_ident[EI_MAG3] == ELFMAG3))
  {
    switch (e_ident[EI_CLASS])
    {
    case ELFCLASS32:
      {
        Elf32_Ehdr header;
        size_t num_read =
          fread((char*)&header + EI_NIDENT, sizeof(header) - EI_NIDENT, 1, fp);
        match =
          (num_read == 1 &&
           header.e_ehsize >= sizeof header &&
           (header.e_machine == CHIP_ELF_TYPE() ||
            header.e_machine == CHIP_COMPAT_ELF_TYPE()));
      }
      break;
    case ELFCLASS64:
      {
        Elf64_Ehdr header;
        size_t num_read =
          fread((char*)&header + EI_NIDENT, sizeof(header) - EI_NIDENT, 1, fp);
        match =
          (num_read == 1 &&
           header.e_ehsize >= sizeof header &&
           (header.e_machine == CHIP_ELF_TYPE() ||
            header.e_machine == CHIP_COMPAT_ELF_TYPE()));
      }
      break;
    }
  }

  fclose(fp);
  return match;
}


// Update "g_monitor_cwd".
//
static void
update_monitor_cwd(void)
{
  char cwd[PATH_MAX];

  if (!getcwd(cwd, sizeof(cwd)))
    punt("Cannot acquire current working directory!");

  FREESET(g_monitor_cwd, strdup_or_die(cwd));
}



// Handle incoming traffic on a tunnel socket.
//
static void
handle_tunnel_socket(Pollable* socket)
{
  uint id = (uint)(uintptr_t)socket->info;

  Buffer* input = &socket->input;

  // Leave room for packet overhead.
  int result = Pollable_acquire(socket, RPC_HEADER_SIZE + 4 + 4);

  if (result > 0)
  {
    spew(4, "%s read %u bytes.", socket->name, input->size);

    // Multiplex the data.
    do_tunnel_m2s(&g_shepherd_socket, id, input->data, input->size);

    // Consume.
    input->size = 0;
  }

  else if (result < 0)
  {
    // Multiplex the EOF.
    // HACK: Use an empty packet to indicate EOF.
    do_tunnel_m2s(&g_shepherd_socket, id, NULL, 0);
  }
}


// ISSUE: If the remote connection fails, then the shepherd pretends
// that the connection was successful, but was immediately closed.
// The "proper" way to deal with this would be to defer the "accept"
// until the shepherd confirms that the remote connection succeeded.
//
static void
handle_tunnel_listener(Pollable* pollable)
{
  uint16_t remote_port = (uint16_t)(uintptr_t)pollable->info;

  spew(2, "Accepting tunnel connection!");

  int fd = simple_accept(pollable->fd);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // Acquire the next available tunnel id.
  uint id = g_tunnel_sockets.size;

  // Tunnel the connection.
  do_tunnel_connect(&g_shepherd_socket, id, remote_port);

  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Tunnel %u [%u]", id, remote_port);
  socket->info = (void*)(uintptr_t)id;
  Pollable_open(socket, fd, handle_tunnel_socket);

  Array_append(&g_tunnel_sockets, socket);
}



// Find the process with the given pid, or else return NULL.
//
static Process*
find_process_by_pid(pid_t pid)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->pid == pid)
      return process;
  }
  return NULL;
}


// Find the process with the given pid, or else die.
//
static Process*
find_process_by_pid_or_die(pid_t pid, const char* who)
{
  Process* process = find_process_by_pid(pid);
  if (process != NULL)
    return process;
  punt("There is no process with pid %d (%s)!", pid, who);
}


// Find the thread with the given pid and tid, or else die.
//
static Thread*
find_thread_or_die(pid_t pid, pid_t tid, const char* who)
{
  Process* process = find_process_by_pid_or_die(pid, who);

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* thread = Array_get(&process->threads, i);
    if (thread->tid == tid)
      return thread;
  }

  punt("%s has no thread with tid %d (%s)!", process->name, tid, who);
}


static Debugger*
find_debugger_for_stdin(void)
{
  Debugger* debugger = NULL;

  if (g_stdin_mode == 'g')
  {
    Process* process = find_process_by_pid(g_stdin_arg);
    if (process != NULL)
      debugger = process->debugger;
  }
  else if (g_stdin_mode == 't')
  {
    uint x = g_stdin_arg / 100;
    uint y = g_stdin_arg % 100;
    debugger = g_tile_debuggers[x][y];
  }

  if (debugger != NULL && debugger->stdin_mode == g_stdin_mode)
    return debugger;

  return NULL;
}


// If needed, erase the current prompt (and user input), and remember
// to redraw the prompt during the next round of event processing.
//
// Call this before any "spontaneous" writes to stdout/stderr.
//
static void
erase_prompt(void)
{
  if (g_readline_ready && !g_prompt_redraw && !g_prompt_needed)
  {
    // HACK: Erase the current line with a magic escape sequence.
    // ISSUE: There is no official "libedit" function to do this!
    printf("\r\e[0J");
    fflush(stdout);

    // Redraw the prompt during the next event loop.
    g_prompt_redraw = true;
  }
}


// Interrupt the current synchronous command, stop processing command
// line commands, and tell the shepherd to "become interactive".
//
static void
become_interactive(void)
{
  // Interrupt any active synchronous command.
  g_waiting = false;

  // Stop processing command line commands.
  g_stop_processing_commands = true;

  // Tell the shepherd to become interactive.
  // ISSUE: What if the shepherd has not yet been greeted?
  if (!g_become_interactive_sent)
  {
    do_become_interactive(&g_shepherd_socket);
    g_become_interactive_sent = true;
  }
}


static void
send_command_to_local_gdb(Debugger* debugger, const char* cmd)
{
  // ISSUE: Flush "local_gdb_stream"?

  // Implicitly terminate any prompt.
  debugger->local_gdb_stream.partial = false;

  // Expect a new prompt.
  debugger->local_gdb_expecting_prompt = true;

  // Send the command (which must include a final newline).
  Pollable_write(&debugger->local_gdb_pty, cmd, strlen(cmd));
}



// Handle some "flushed" console traffic.
//
// Note that "bytes" always points into "stream->pending", and will contain
// no "newline" except possibly a final newline at "bytes[bytes_size - 1]".
//
// Normally only complete lines are "flushed", but if a known "prompt"
// is seen, or sufficient time passes, a partial line will be flushed,
// causing "stream->partial" to become true, and stay true until this
// function sees a final newline, or user input is detected.  In this
// mode, partial lines will be flushed as soon as they arrive.
//
// The "auto_bt" hackery stifles "boring" lines, including:
//
// Reading symbols from /u/benh/Tile/xxx/threads.tilexe...done.
// Remote debugging using lab-8.internal.tilera.com:43214
// [New Thread 533]
// [Switching to Thread 533]
// 0x00018450 in __internal_syscall4 () at libc/[...]/__syscall.S:64
// 64      SYSCALL(4)
// Current language:  auto; currently asm
// [New Thread 534]
// [New Thread 535]
// [New Thread 536]
// [New Thread 537]
// [New Thread 538]
// 6 Thread 538  0x000181d0 in __syscall2 ()
//   at libc/sysdeps/linux/tile/__syscall.S:40
// 5 Thread 537  0x000181d0 in __syscall2 ()
//   at libc/sysdeps/linux/tile/__syscall.S:40
// 4 Thread 536  0x000181d0 in __syscall2 ()
//   at libc/sysdeps/linux/tile/__syscall.S:40
// 3 Thread 535  0x000181d0 in __syscall2 ()
//   at libc/sysdeps/linux/tile/__syscall.S:40
// 2 Thread 534  0x000181d0 in __syscall2 ()
//   at libc/sysdeps/linux/tile/__syscall.S:40
// 1 Thread 533  0x00018450 in __internal_syscall4 ()
//   at libc/sysdeps/linux/tile/__syscall.S:64
//
// Note that the the last 12 lines come only if "info threads" is sent.
//
// ISSUE: The threads are backtraced in reverse creation order.
//
static void
ConsoleStream_flush(ConsoleStream* stream,
                    const uint8_t* bytes, size_t bytes_size)
{
  // Paranoia.
  if (bytes_size == 0)
    return;

  // Update the "partial" flag.
  bool old_partial = stream->partial;
  bool new_partial = (bytes[bytes_size - 1] != '\n');
  stream->partial = new_partial;

  Debugger* debugger = stream->debugger;

  // Handle "local gdb".
  if (debugger != NULL)
  {
    // Handle new debugger "prompts" (including unknown ones).
    // ISSUE: Technically a prompt could be received in pieces.
    if (new_partial && !old_partial)
    {
      // Erase the prompt in case it changes.
      erase_prompt();

      if (stream->prompted == NULL)
        warn("Unrecognized debugger prompt '%.*s'.", (int)bytes_size, bytes);
      else if (!debugger->local_gdb_expecting_prompt)
        warn("Unexpected debugger prompt '%.*s'.", (int)bytes_size, bytes);

      // No longer expecting a prompt.
      debugger->local_gdb_expecting_prompt = false;

      // Send pending "commands" as user input.
      if (debugger->gdb_commands.size != 0)
      {
        StringArray* commands = &debugger->gdb_commands;
        char* cmd = commands->data[0];
        StringArray_excise(commands, 0, 1);
        send_command_to_local_gdb(debugger, cmd);
        free(cmd);
        return;
      }

      // Mention new user interaction possibilities.
      if (debugger->stdin_mode != '\0' &&
          (g_stdin_mode != debugger->stdin_mode ||
           g_stdin_arg != debugger->stdin_arg))
      {
        become_interactive();

        note("Type '#%c%d' to interact with local gdb.",
             debugger->stdin_mode, debugger->stdin_arg);
      }
    }

    // ISSUE: Is this right?
    if (new_partial)
      return;

    // Handle "auto_bt".
    if (debugger->process != NULL &&
        debugger->process->auto_bt != AUTO_BT_NONE)
    {
      // NOTE: Although "bytes" is NOT nul terminated, it is legal to
      // use "has_prefix(bytes, str)" if "str" does not contain "\n",
      // because we know "bytes" ends with a "\n".

      // Start dumping at the first "interesting" line.
      if (debugger->process->auto_bt == AUTO_BT_ACTIVE &&
          (bytes[0] == '#' || has_prefix((char*)bytes, "Thread ")))
        debugger->process->auto_bt = AUTO_BT_DUMPING;

      // Dump "backtrace" lines to stderr.  HACK: We stifle the blank
      // lines that appear before each "Thread N (Thread TID)".
      if (debugger->process->auto_bt == AUTO_BT_DUMPING && bytes_size > 1)
      {
        erase_prompt();

        // TODO: Add "x,y" info to "Thread N (Thread TID)" lines.
        if (has_prefix((char*)bytes, "Thread "))
        {
          fprintf(stderr, "=== %.*s ===\n", (int)bytes_size - 1, bytes);
        }
        else if (has_prefix((char*)bytes, "Backtrace stopped: ") ||
                 has_prefix((char*)bytes, "Ending remote debugging."))
        {
          // Stifle boring spew.
        }
        else
        {
          write_all_bytes_or_die(STDERR_FILENO, bytes, bytes_size);
        }
      }

      return;
    }

    // HACK: Stifle boring spew while initializing local debugger.
    // Note that "Remote debugging using ..." is not stifled.
    if (debugger->gdb_commands.size != 0)
      return;
  }

#if 0
  if (stream == &g_console_stream && g_stdin_mode == 'c')
  {
    // Handle new "prompt" on the console.
    if (new_partial)
    {
      // Erase the prompt in case it changes.
      // ISSUE: Is this right?
      if (!old_partial)
        erase_prompt();
    }
  }
#endif

  bool have_ide = Pollable_valid(&g_ide_socket);

  if (have_ide)
  {
    // Forward to the IDE.
    pid_t pid = (stream->process != NULL) ? stream->process->pid : 0;
    if (stream->fd == STDOUT_FILENO)
      do_console_stdout(&g_ide_socket, pid, bytes, bytes_size);
    else
      do_console_stderr(&g_ide_socket, pid, bytes, bytes_size);
  }

  if (!have_ide || getenv("TILERA_IDE_TEE") != NULL)
  {
    // Emit directly.
    erase_prompt();
    if (stream->tag != NULL && !old_partial)
      write_all_bytes_or_die(stream->fd, stream->tag, strlen(stream->tag));
    write_all_bytes_or_die(stream->fd, bytes, bytes_size);
  }

  // HACK: Detect kernel bugs and such.
  if (g_testing && stream == &g_console_stream)
  {
    static const char msg[] = "Client requested halt.";
    const uint len = sizeof(msg) - 1;
    if (bytes_size >= len && memcmp(bytes, msg, len) == 0)
    {
      g_exit_msg[0] = '\0';
      g_display_host_at_exit = true;
      punt("Detected system halt on console!");
    }
  }
}


// Expire pending console traffic.
//
static void
ConsoleStream_expire(ConsoleStream* stream)
{
  Buffer* pending = &stream->pending;

  uint8_t* data = pending->data;
  uint size = pending->size;

  if (size != 0)
  {
    spew(3, "Expiring %d bytes of console output!", size);

    ConsoleStream_flush(stream, data, size);

    // Consume.
    Buffer_excise(pending, 0, size);

    Alarm_cancel(&stream->alarm);
  }
}


// Check for standard prompts.
//
static bool
ConsoleStream_check_for_prompt(ConsoleStream* stream,
                               const uint8_t* bytes, size_t bytes_size)
{
  for (int i = 0; i < stream->prompts.size; i++)
  {
    char* prompt = StringArray_get(&stream->prompts, i);
    if (strlen(prompt) == bytes_size && memcmp(prompt, bytes, bytes_size) == 0)
    {
      stream->prompted = prompt;
      return true;
    }
  }

  return false;
}


// Handle incoming console traffic.
//
static void
ConsoleStream_write(ConsoleStream* stream,
                    const uint8_t* bytes, size_t bytes_size)
{
  Buffer* pending = &stream->pending;

  Buffer_write(pending, bytes, bytes_size);

  uint head = 0;

  stream->prompted = NULL;

  while (head < pending->size)
  {
    const uint8_t* data = pending->data + head;
    uint size = pending->size - head;

    const uint8_t* newline = memchr(data, '\n', size);

    if (newline != NULL)
    {
      // Flush each complete line.
      uint consume = newline + 1 - data;
      ConsoleStream_flush(stream, data, consume);
      head += consume;
    }
    else if (stream->partial || size >= 4000 ||
             ConsoleStream_check_for_prompt(stream, data, size))
    {
      // Flush any additional output once a partial line has been flushed.
      // HACK: Flush "long" partial lines (to avoid excessive allocation).
      // Flush partial lines matching any standard prompt for the stream.
      spew(3, "Flushing %d bytes of partial output!", size);
      ConsoleStream_flush(stream, data, size);
      head += size;
      break;
    }
    else
    {
      // Defer new short incomplete lines.
      spew(4, "Deferring %d bytes of console output!", size);
      break;
    }
  }

  // Consume flushed data.
  Buffer_excise(pending, 0, head);

  // Give new partial lines up to one half second to "complete"
  // (measured from when the first char was seen).

  Alarm* alarm = &stream->alarm;

  if (pending->size == 0)
  {
    Alarm_cancel(alarm);
  }
  else if (!Alarm_scheduled(alarm))
  {
    timeval_now(&alarm->when);
    timeval_add_msecs(&alarm->when, 500);
    Alarm_schedule(alarm);
  }
}


// Expire pending console traffic.
//
static void
handle_alarm_for_console_stream(Alarm* alarm)
{
  ConsoleStream* stream = (ConsoleStream*)(alarm->info);

  ConsoleStream_expire(stream);
}


static void
ConsoleStream_init(ConsoleStream* stream, int fd)
{
  memset(stream, 0, sizeof(*stream));
  stream->fd = fd;
  stream->alarm.info = stream;
  stream->alarm.handler = handle_alarm_for_console_stream;
}


static void
ConsoleStream_destroy(ConsoleStream* stream)
{
  // ISSUE: This is inappropriate.
  ConsoleStream_expire(stream);

  StringArray_free_and_clear(&stream->prompts);
  StringArray_destroy(&stream->prompts);

  free((void*)stream->tag);
  Buffer_destroy(&stream->pending);
  Alarm_cancel(&stream->alarm);
}


// Function for the console output polling thread.
//
static void *
console_out_func(void *arg)
{
  long fd = (long)arg;
  Pollable_open(&g_console, fd, NULL);

  while (Pollable_valid(&g_console))
  {
    // Dump the console spew to stdout.
    if (Pollable_read_partial(&g_console, 4096) > 0)
    {
      ConsoleStream_write(&g_console_stream,
                          g_console.input.data, g_console.input.size);
      g_console.input.size = 0;
      ConsoleStream_expire(&g_console_stream);
    }
  }

  return (void *)NULL;
}


// Create the console output polling thread.
//
static void
init_console_stdout_handler(long fd)
{
  if (g_console_tid != 0)
    punt("Console thread already running!\n");

  int rc = pthread_create(&g_console_tid, NULL, console_out_func, (void*)fd);
  if (rc != 0)
    punt("Failed to create console output polling thread:%s.\n",
         strerror(rc));

  spew(2, "Spawned the console output polling thread %lu.",
       (long)g_console_tid);
}


// Cancel the console output polling thread.
//
static void
cancel_console_stdout_handler(void)
{
  if (g_console_tid != 0)
  {
    int rc = pthread_cancel(g_console_tid);
    if (rc != 0 && rc != ESRCH)
      fprintf(stderr, "Warning: canceling console thread failed: %d\n", rc);
    rc = pthread_join(g_console_tid, NULL);
    if (rc)
      fprintf(stderr, "Warning: joining console thread failed: %d\n", rc);
    g_console_tid = 0;
    if (Pollable_valid(&g_console))
      Pollable_close(&g_console);
  }
}


static void
forward_rsp(Debugger* debugger, uint8_t* data, uint len)
{
  if (debugger->process != NULL)
    do_rsp_m2s(&g_shepherd_socket, debugger->process->pid, data, len);
  else
    do_sim_bmd_rsp(&g_reporter_socket, debugger->x, debugger->y, data, len);
}


static void
Pollable_init_debugger(Pollable* pollable,
                       Debugger* debugger, const char* what)
{
  Pollable_init(pollable, "%s %s", debugger->name, what);
  pollable->info = debugger;
}


static Debugger*
make_debugger(Process* process, uint16_t x, uint16_t y)
{
  Debugger* debugger = (Debugger*)calloc_or_die(1, sizeof(Debugger));

  debugger->process = process;

  debugger->x = x;
  debugger->y = y;

  if (process != NULL)
    debugger->name = strfmt_or_die("%s gdb", process->name);
  else
    debugger->name = strfmt_or_die("Tile %d,%d gdb", x, y);

  Pollable_init_debugger(&debugger->gdb_listener, debugger, "listener");
  Pollable_init_debugger(&debugger->gdb_socket, debugger, "socket");

  Pollable_init_debugger(&debugger->local_gdb_pty, debugger, "pty");

  // Emit local gdb stdout/stderr to stdout.
  ConsoleStream_init(&debugger->local_gdb_stream, STDOUT_FILENO);

  debugger->local_gdb_pid = -1;

  return debugger;
}


static void
free_debugger(Debugger* debugger)
{
  free((void*)debugger->name);
  free((void*)debugger->executable);

  StringArray_destroy(&debugger->gdb_commands);

  Pollable_destroy(&debugger->gdb_listener);
  Pollable_destroy(&debugger->gdb_socket);

  Pollable_destroy(&debugger->local_gdb_pty);

  ConsoleStream_destroy(&debugger->local_gdb_stream);

  // Free the debugger.
  free(debugger);
}


static void
kill_debugger(Debugger* debugger)
{
  // ISSUE: Verify this logic.

  if (Pollable_valid(&debugger->gdb_listener))
  {
    note("%s is no longer necessary.", debugger->name);

    Pollable_close(&debugger->gdb_listener);

    // ISSUE: We could send a synthetic "detach" RSP packet to the
    // shepherd, so it does not warn about the debugger "crashing",
    // but then we would have to handle the resulting "ACK" packet.
  }
  else if (Pollable_valid(&debugger->gdb_socket))
  {
    if (debugger->local_gdb_pid >= 0)
    {
      // ISSUE: The SIGINT is only needed if waiting for a prompt.
      warn("%s being interrupted and disconnected.", debugger->name);
      kill(debugger->local_gdb_pid, SIGINT);

      // ISSUE: We could send a "detach" command instead of just
      // closing the socket below, but there is no need.
    }

    Pollable_close(&debugger->gdb_socket);
  }
  else
  {
    // Handle EOF in "handle_gdb_socket()".

    if (debugger->stdin_mode != '\0')
      note("%s has disconnected.", debugger->name);
  }

  // Induce an implicit "#." if needed.
  if (debugger->stdin_mode != '\0' &&
      (g_stdin_mode == debugger->stdin_mode &&
       g_stdin_arg == debugger->stdin_arg))
  {
    g_stdin_mode = '\0';
    g_stdin_arg = -1;

    erase_prompt();
  }

  if (debugger->process != NULL)
  {
    Process* process = debugger->process;

    if (process->auto_bt != AUTO_BT_NONE)
    {
      process->auto_bt = AUTO_BT_NONE;
      auto_bt_advance();
    }

    process->debugger = NULL;
  }
  else
  {
    g_tile_debuggers[debugger->x][debugger->y] = NULL;
  }

  // HACK: Use an empty packet to indicate EOF.
  forward_rsp(debugger, NULL, 0);

  // Reap the debugger later.
  Array_append(&g_killed_debuggers, debugger);

  Pollable_close(&debugger->local_gdb_pty);

  debugger->local_gdb_expecting_prompt = false;

  debugger->stdin_mode = '\0';
  debugger->stdin_arg = 0;
}


static bool
kill_some_debuggers_aux(Debugger* debugger)
{
  if (debugger == NULL)
    return 0;

  // Kill pending debugger.
  if (Pollable_valid(&debugger->gdb_listener))
  {
    kill_debugger(debugger);
    return 0;
  }

  // Kill local debugger.
  if (debugger->local_gdb_pid >= 0)
  {
    kill_debugger(debugger);
    return 0;
  }

  return 1;
}


// Kill all the pending/local debuggers.
//
static void
kill_some_debuggers(void)
{
  int others = 0;

  // Kill pending/local process debuggers.
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    Debugger* debugger = process->debugger;
    others += kill_some_debuggers_aux(debugger);
  }

  // Kill pending/local tile debuggers.
  for (uint x = 0; x < MAX_WIDTH; x++)
  {
    for (uint y = 0; y < MAX_HEIGHT; y++)
    {
      Debugger* debugger = g_tile_debuggers[x][y];
      others += kill_some_debuggers_aux(debugger);
    }
  }

  // Warn about other debuggers.
  if (others != 0)
    warn("Please shut down remaining debuggers.");
}


// Forget about a debugger which is still active when we are about to
// create a new one. ISSUE: This code has never actually been tested.
// The shepherd may actually prevent it from ever happening.
//
static void
forget_debugger(Debugger* debugger)
{
  warn("%s is being replaced!", debugger->name);
  kill_debugger(debugger);
}


static void
ConsoleStream_init_process(ConsoleStream* stream, int fd,
                           char kind, Process* process)
{
  ConsoleStream_init(stream, fd);
  stream->process = process;
  if (g_tag_console)
    stream->tag = strfmt_or_die("[%c%d] ", kind, process->pid);
}


static Process*
make_process(pid_t pid, char* exe)
{
  Process* process = (Process*)calloc_or_die(1, sizeof(Process));

  process->name = strfmt_or_die("Process %d", pid);

  process->pid = pid;

  process->executable = exe;

  ConsoleStream_init_process(&process->stdout_stream,
                             STDOUT_FILENO, 'p', process);
  ConsoleStream_init_process(&process->stderr_stream,
                             STDERR_FILENO, 'e', process);

  Array_append(&g_processes, process);

  return process;
}


static void
kill_process(Process* process, int status)
{
  // Expire partial console traffic.
  ConsoleStream_expire(&process->stdout_stream);
  ConsoleStream_expire(&process->stderr_stream);

  if (WIFEXITED(status))
  {
    int code = WEXITSTATUS(status);
    spew(1, "%s exited with code %d.", process->name, code);

    if (g_exit_code == 0 && code != 0)
      g_exit_code = 124;
  }
  else //--if (WIFSIGNALED(status))
  {
    int sig = WTERMSIG(status);
    if (WCOREDUMP(status))
      spew(1, "%s dumped core due to signal %d: %s.",
           process->name, sig, strsignal(sig));
    else
      spew(1, "%s terminated by signal %d: %s.",
           process->name, sig, strsignal(sig));

    if (g_exit_code == 0)
      g_exit_code = 125;
  }

  // Note process death.
  process->dead = true;

  // Induce an implicit "#.".
  if (g_stdin_mode == 'p' && g_stdin_arg == process->pid)
  {
    g_stdin_mode = '\0';
    g_stdin_arg = -1;

    erase_prompt();
  }
}


static void
free_process(Process* process)
{
  if (process->debugger != NULL)
    free_debugger(process->debugger);

  free((void*)process->name);

  free((void*)process->executable);

  Array_destroy(&process->threads);

  ConsoleStream_destroy(&process->stdout_stream);
  ConsoleStream_destroy(&process->stderr_stream);

  free(process);
}



// HACK: Slurp "gdb" traffic from a socket.
//
static void
handle_gdb_socket(Pollable* socket)
{
  Debugger* debugger = (Debugger*)socket->info;

  Buffer* input = &socket->input;

  int result = Pollable_acquire(socket, 0);

  if (result > 0)
  {
    uint head = input->head;

    spew(4, "Increased buffer size to %d (head %d)", input->size, head);

    while (head < input->size)
    {
      uint size = input->size - head;
      uint8_t* data = input->data + head;

      uint len = 1;

      uint8_t c = data[0];

      if (c == '$')
      {
        // Handle real packet.
        uint8_t* pound = (uint8_t*)memchr(data, '#', size);

        // Wait for a complete RSP packet.
        if ((pound == NULL) || (pound + 2 > data + size))
          break;

        len = pound + 1 + 2 - data;

        // Verify the checksum.
        uint8_t checksum = 0;
        for (uint i = 1; i < len - 3; i++)
          checksum += data[i];
        int h1 = hex_char_to_int(pound[1]);
        int h2 = hex_char_to_int(pound[2]);
        assert(checksum == 16 * h1 + h2);

        spew_bytes(3, data, len, "%s read: ", socket->name);

        // Send an ACK.
        uint8_t ack = '+';
        Pollable_write(socket, &ack, 1);

        // Forward to the shepherd.
        forward_rsp(debugger, data, len);
      }
      else if (c == '\x03')
      {
        // Handle "interrupt" (encoded as Ctrl+C).

        spew(3, "%s read interrupt.", socket->name);

        // Forward to the shepherd.
        forward_rsp(debugger, data, len);
      }
      else
      {
        // Handle mini-packet ("+", "-").

        if (c == '-')
          warn("%s ignoring unexpected NAK!", socket->name);
        else if (c != '+')
          punt("%s got unknown mini-packet (0x%x).", socket->name, c);
      }

      // Consume.
      head += len;
    }

    // Slide occasionally.
    if (head >= input->size / 2)
    {
      Buffer_excise(input, 0, head);
      head = 0;
    }
    input->head = head;
  }

  else if (result < 0)
  {
    // Handle EOF.
    kill_debugger(debugger);
  }
}


static void
handle_gdb_listener(Pollable* listener)
{
  Debugger* debugger = (Debugger*)listener->info;

  spew(2, "%s accepting gdb connection!", listener->name);

  int fd = simple_accept(listener->fd);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // Accept only one connection.
  Pollable_close(listener);

  Pollable_open(&debugger->gdb_socket, fd, handle_gdb_socket);

  if (debugger->stdin_mode != '\0')
    note("%s has connected.", debugger->name);
}



static void
handle_gdb_stdout(Pollable* pty)
{
  Debugger* debugger = (Debugger*)pty->info;

  int n = Pollable_read(pty, 4096);

  if (n > 0)
  {
    Buffer* input = &pty->input;

    ConsoleStream_write(&debugger->local_gdb_stream,
                        input->data, input->size);

    // Consume.
    input->size = 0;
  }
}


// Construct an "add-symbol-file PATH ADDR" command.
//
// Here are some of the lines from "tile-readelf -SW .../hv".
// [ 0]                   NULL       00000000 000000 000000 00      0   0  0
// [ 1] .intrpt2          NOBITS     fe000000 010000 0040e0 00  AX  0   0  8
// [ 2] .text             NOBITS     fe0040e0 010000 08c210 00  AX  0   0 64
// [ 3] .dbg_text         NOBITS     ff000000 010000 0032c0 00  AX  0   0  8
// [ 4] .rodata           NOBITS     fe110000 010000 00ab54 00   A  0   0  4
// [ 5] .data             NOBITS     fe11ab54 010000 00222c 00  WA  0   0  4
// [ 6] .driver_table     NOBITS     fe11cd80 010000 000440 00  WA  0   0  4
// [ 7] .ipp_driver_table NOBITS     fe11d1c0 010000 000200 00  WA  0   0  4
// [ 8] .bss              NOBITS     fe11d3c0 010000 00884c 00  WA  0   0  8
// [ 9] .comment          PROGBITS   00000000 000094 000600 00      0   0  1
//
// Here are the "X" lines from "tile-readelf -SW .../vmlinux":
//
// [ 1] .intrpt1          PROGBITS   fd000000 010000 003fd8 00  AX  0   0  8
// [ 2] .text             PROGBITS   fd020000 020000 3d21e0 00  AX  0   0 64
// [ 3] .text.futex       PROGBITS   fd3f21e0 3f21e0 000130 00  AX  0   0  8
// [ 4] .text.memcpy_comm PROGBITS   fd3f2340 3f2340 000380 00  AX  0   0 64
// [ 5] .text.__ll_mul    PROGBITS   fd3f26c0 3f26c0 000060 00  AX  0   0  8
// [ 6] .text.__netio_fas PROGBITS   fd3f2720 3f2720 000018 00  AX  0   0  8
// [ 7] .init.text        PROGBITS   fd400000 400000 026570 00  AX  0   0  8
//
// NOTE: The lines above have had some extra whitespace collapsed.
//
// We do NOT need to use the "-s <SECT> <SECT_ADDR>" syntax with
// "add-symbol-file", because "gdb" automatically handles sections
// which are properly located relative to the ".text" section.
//
// ISSUE: The "tile-readelf" path below will not work natively!
//
static char*
request_gdb_aux_asf(const char* asf, const char* path)
{
  char* result = NULL;

  char temp[PATH_MAX];
  get_install_path(temp, sizeof(temp), "/bin/tile-readelf");

  // A command to find the address of the ".text" section.
  char cmd[PATH_MAX * 2 + 128];
  snprintf(cmd, sizeof(cmd),
           "%s -SW %s | sed -n -e "
           "'s/.* \\.text  *[A-Z]*BITS * \\([0-9a-f][0-9a-f]*\\) .*/0x\\1/p'",
           temp, path);

  FILE* fff = popen(cmd, "r");
  if (fff != NULL)
  {
    char* addr = fgets(temp, sizeof(temp), fff);
    if (addr != NULL && has_suffix(addr, "\n"))
    {
      // Trim the final newline.
      addr[strlen(addr) - 1] = '\0';

      result = strfmt_or_die("%s %s %s\n", asf, path, addr);
    }

    pclose(fff);
  }

  return result;
}


// Initiate debugging.
//
// For tile debugging, the simulator sends us an array of "symbol table"
// info, each one being the path to a binary file and the base address at
// which the symbols in that binary file were loaded, if known, else "0x0".
// Note that "0x0" is provided for everything except shared libraries.
//
// Note that "add-symbol-file" verification in "tools/gdb/gdb/symfile.c"
// prevents command pasting, but we can define and use the "asf" macro
// instead, since macros apparently bypass verification.  Note that the
// IDE already bypasses verification by not using a TTY.
//
static void
request_gdb_aux(Debugger* debugger, StringArray* path_addr_pairs)
{
  Process* process = debugger->process;
  uint16_t x = debugger->x;
  uint16_t y = debugger->y;

  const char* debug_host = fullhostname_or_die();

  uint16_t debug_port = (g_gdb_port != 0) ? g_gdb_port++ : 0;
  int listener_fd = simple_listen(&debug_port, 1);

  Pollable_open(&debugger->gdb_listener, listener_fd, handle_gdb_listener);

  char* cmd;
  StringArray* commands = &debugger->gdb_commands;

#ifdef __tile__
  char gdb_exe[] = "gdb";
#else
  char gdb_exe[PATH_MAX];
  get_install_path(gdb_exe, sizeof(gdb_exe), "/bin/tile-gdb");
#endif

  // Compute the "commands".

  // HACK: Avoid annoying "Type <return> to continue" pseudo-prompts.
  StringArray_insert(commands, 0, strdup_or_die("set height -1\n"));

  bool unknown_binary = false;

  if (process != NULL)
  {
    // NOTE: A "process" debugger always has an executable.
    cmd = strfmt_or_die("file %s\n", debugger->executable);
    StringArray_append(commands, cmd);

    unknown_binary = !file_is_tile_binary(debugger->executable);
  }
  else
  {
    char* asf = "add-symbol-file";

    // No need for "asf" macro when using IDE.
    if (!Pollable_valid(&g_ide_socket))
    {
      // NOTE: We properly recognize the ">" sub-prompt.
      StringArray_append(commands, strdup_or_die("define asf\n"));
      StringArray_append(commands, strfmt_or_die("%s $arg0 $arg1\n", asf));
      StringArray_append(commands, strdup_or_die("end\n"));
      asf = "asf";
    }

    for (int i = 0; i < path_addr_pairs->size; i++)
    {
      char* pair = StringArray_get(path_addr_pairs, i);
      char* addr = strrchr(pair, '@');
      assert(addr != NULL);
      *addr++ = '\0';

      char* path = canonify_host(pair);

      // ISSUE: The simulator sorts the "hv_bin_dir" files and "vmlinux"
      // before the actual executable, causing us to use "file hv_lhboot",
      // which is apparently legal, but kind of silly.

      if (strcmp(addr, "0x0") != 0)
        cmd = strfmt_or_die("%s %s %s\n", asf, path, addr);
      else if (i != 0)
        cmd = request_gdb_aux_asf(asf, path);
      else
        cmd = strfmt_or_die("file %s\n", path);

      if (cmd != NULL)
        StringArray_append(&debugger->gdb_commands, cmd);

      // ISSUE: What if "cmd == NULL"?
      unknown_binary = unknown_binary || !file_is_tile_binary(path);
    }
  }


#ifndef __tile__
  char sysroot[PATH_MAX];
  get_install_path(sysroot, sizeof(sysroot), "/tile");

  StringArray_append(commands, strfmt_or_die("set sysroot %s\n", sysroot));
#endif


  for (int i = 0; i < g_gdb_commands.size; i++)
  {
    cmd = StringArray_get(&g_gdb_commands, i);
    StringArray_append(commands, strdup_or_die(cmd));
  }


#ifndef __tile__
  char srcdir[PATH_MAX];
  get_install_path(srcdir, sizeof(srcdir), "/src");

  cmd = strfmt_or_die("dir %s\n", srcdir);
  StringArray_append(commands, cmd);

  cmd = strfmt_or_die("dir %s/tools/glibc\n", srcdir);
  StringArray_append(commands, cmd);
#endif


  cmd = strfmt_or_die("target remote %s:%u\n", debug_host, debug_port);
  StringArray_append(commands, cmd);


  // ISSUE: Allow "auto_bt" with the IDE.
  // Notify the IDE if needed.
  if (Pollable_valid(&g_ide_socket))
  {
    if (process != NULL)
      do_request_debug_process(&g_ide_socket, process->pid, commands);
    else
      do_request_debug_tile(&g_ide_socket, x, y, commands);
    return;
  }


  bool use_auto_bt = (process != NULL && process->auto_bt != AUTO_BT_NONE);

  if (use_auto_bt)
  {
    //--StringArray_append(commands, strdup_or_die("info threads\n"));

    // HACK: If the process is single-threaded, "thread apply all bt"
    // will have no effect.  We approximate this as "has one thread".
    if (process->threads.size <= 1)
      StringArray_append(commands, strdup_or_die("bt\n"));
    else
      StringArray_append(commands, strdup_or_die("thread apply all bt\n"));

    // Detach when done.
    StringArray_append(commands, strdup_or_die("detach\n"));
  }


  if (!(use_auto_bt || (g_local_gdb && !unknown_binary)))
  {
    // NOTE: We use "stderr" to match the related "note()" messages.

    erase_prompt();

    fprintf(stderr, "Paste the following lines into another window:\n");

    fprintf(stderr, "\n");
    fprintf(stderr, "%s -q\n", gdb_exe);
    for (int i = 0; i < commands->size; i++)
      fprintf(stderr, "%s", commands->data[i]);
    fprintf(stderr, "\n");

    StringArray_free_and_clear(commands);

    // ISSUE: This warning does not do the IDE any good.
    if (unknown_binary)
      warn("Replace any remote binaries above with local binaries!");

    return;
  }


  // Create a pty for the local gdb stdin/stdout (and also stderr).
  // NOTE: See "openpty()" comments in "tools/tmc/source/task.c".
  int pty_master;
  int pty_slave;
  struct termios term;
  memset(&term, 0, sizeof(term));
  term.c_cc[VMIN] = 1;
  if (openpty(&pty_master, &pty_slave, NULL, &term, NULL) != 0)
    punt_with_errno("Failure in 'openpty()'");

  // Create a local gdb.
  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    close_or_die(pty_master);

    if (login_tty(pty_slave) != 0)
      punt_with_errno("Failure in 'login_tty'");

    char* argv[] = { gdb_exe, "-q", NULL };

    (void)execv(argv[0], argv);

    punt_with_errno("Failed to exec '%s'", argv[0]);
  }


  // Parent.

  spew(2, "Spawned gdb at pid %d.", pid);

  close_or_die(pty_slave);

  set_blocking_or_die(pty_master, false);
  set_close_on_exec_or_die(pty_master, true);

  // Start out expecting a prompt.
  debugger->local_gdb_expecting_prompt = true;

  debugger->local_gdb_pid = pid;

  if (process != NULL)
  {
    debugger->stdin_mode = 'g';
    debugger->stdin_arg = process->pid;
  }
  else
  {
    debugger->stdin_mode = 't';
    debugger->stdin_arg = 100 * x + y;
  }

  ConsoleStream* stream = &debugger->local_gdb_stream;

  // Save debugger.
  stream->debugger = debugger;

  // The normal prompt.
  StringArray_append(&stream->prompts, strdup_or_die("(gdb) "));

  // The continuation prompt for "define".
  StringArray_append(&stream->prompts, strdup_or_die(">"));

  // ISSUE: This was the prompt for "gdb 6.8".
  // HACK: The verification prompt for "quit".
  const char* str = "The program is running.  Exit anyway? (y or n) ";
  StringArray_append(&stream->prompts, strdup_or_die(str));

  // HACK: The verification prompt for "quit".
  // NOTE: This follows some other lines.
  const char* str2 = "Quit anyway? (y or n) ";
  StringArray_append(&stream->prompts, strdup_or_die(str2));

  if (g_tag_console)
  {
    stream->tag =
      strfmt_or_die("[%c%u] ", debugger->stdin_mode, debugger->stdin_arg);
  }

  if (use_auto_bt)
    debugger->stdin_mode = '\0';

  Pollable_open(&debugger->local_gdb_pty, pty_master, handle_gdb_stdout);
}


// Initiate creation of a "gdb" session for the given process.
//
static void
request_gdb(Process* process)
{
  if (process->auto_bt == AUTO_BT_NONE && !Pollable_valid(&g_ide_socket))
  {
    note("%s would like to be debugged.", process->name);
  }

  // FIXME: Make sure the IDE is not trying to repeat any of this work.

  // FIXME: Try to determine if "process->executable" is actually fine
  // as is, and/or was mounted over FUSE, etc.  See the simulator hacks.

  const char* local_exe = NULL;
  for (int i = 0; i < g_symbol_map_paths.size; i += 2)
  {
    char* host_path = StringArray_get(&g_symbol_map_paths, i);
    char* tile_path = StringArray_get(&g_symbol_map_paths, i + 1);
    if (!strcmp(tile_path, process->executable))
    {
      local_exe = host_path;
      break;
    }
  }

  Debugger* debugger = process->debugger;

  if (local_exe != NULL)
  {
    debugger->executable = strdup_or_die(local_exe);
  }
  else
  {
    debugger->executable = strdup_or_die(process->executable);
  }

  request_gdb_aux(debugger, NULL);
}



// Forward declaration.
static void
display_prompt_if_needed(void);


// Forward declaration.
static void
handle_events(void);


static void
handle_unexpected_simulator_death(int status)
{
  g_exit_msg[0] = '\0';

  if (WIFEXITED(status))
  {
    int code = WEXITSTATUS(status);
    punt("Simulator exited with code %d.", code);
  }
  else //--if (WIFSIGNALED(status))
  {
    int sig = WTERMSIG(status);
    punt("Simulator terminated by: %s (%d).", strsignal(sig), sig);
  }
}


// Handle signals before we are up and running.
//
static void
handle_early_signal(int sig)
{
  g_exit_msg[0] = '\0';
  punt("Exiting due to early signal: %s.", strsignal(sig));
}


static void
really_exit(void)
{
  // ISSUE: Should we wait for any final console traffic?

  // Note that the watchdog explicitly shuts down the simulator after
  // sending us a "note_quit" query, so we don't have to "kill" it.

  if (g_simulator_pid != 0)
  {
    spew(2, "Waiting for simulator to exit.");

    // FIXME: Use "handle_events()" somehow?

    // HACK: We must not do anything when these Pollables are closed due to
    // the death of the simulator, but we cannot actually "close" them, or
    // the simulator will complain, so we simply "abandon" them.  We will,
    // however, process other events, such as simulator console traffic.
    Pollable_set_fd(&g_shepherd_socket, -1);
    Pollable_set_fd(&g_watchdog_socket, -1);
    Pollable_set_fd(&g_reporter_socket, -1);

    while (true)
    {
      int status = 0;
      if (waitpid_or_die(g_simulator_pid, &status, WNOHANG) != 0)
      {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
          handle_unexpected_simulator_death(status);
        break;
      }

      dispatch_events(-1);
    }
  }

  // ISSUE: This was probably only necessary when "request_exit" could
  // be sent before greeting the shepherd.
  g_exit_msg[0] = '\0';

  exit(g_exit_code);
}


// Forward declaration.
static void
handle_string(const char* str);



typedef struct
{
  // True while waiting for error/reply.
  bool waiting;

  // True if reply arrived while waiting.
  bool success;

  // Handle timeouts.
  Alarm alarm;

} querify_info_t;


static void
querify_free(querify_info_t* ip)
{
  Alarm_cancel(&ip->alarm);

  free(ip);
}


static void
querify_alarm(Alarm* alarm)
{
  warn("Interrupting command which has taken too long.");

  // Stop waiting.
  g_waiting = false;
}


static void
querify_error(void* info, char* msg)
{
  querify_info_t* ip = info;

  if (ip->waiting)
  {
    ip->waiting = false;

    ip->success = (msg == NULL);

    if (msg != NULL)
      warn("Error: %s", msg);
  }
  else
  {
    if (msg != NULL)
      warn("Interrupted command error: %s", msg);
    else
      warn("Interrupted command finished.");

    querify_free(ip);
  }
}


static void
querify_reply(void* info)
{
  querify_error(info, NULL);
}


static bool
querify_with_timeout(AnswerHandler* ah, int msecs)
{
  querify_info_t* ip = calloc_or_die(1, sizeof(*ip));

  if (msecs > 0)
  {
    Alarm* alarm = &ip->alarm;
    alarm->handler = querify_alarm;
    timeval_now(&alarm->when);
    timeval_add_msecs(&alarm->when, msecs);
    Alarm_schedule(alarm);
  }

  ah->reply_handler = querify_reply;
  ah->reply_handler_info = ip;

  ah->error_handler = querify_error;
  ah->error_handler_info = ip;

  g_waiting = ip->waiting = true;
  while (g_waiting && ip->waiting)
    handle_events();
  bool waiting = ip->waiting;
  bool success = ip->success;
  g_waiting = ip->waiting = false;
  if (waiting)
    warn("Interrupted command now running asynchronously.");
  else
    querify_free(ip);

  return success;
}


static bool
querify(AnswerHandler* ah)
{
  return querify_with_timeout(ah, -1);
}

static int
opimport_all_samples(const char* dir)
{
  char* abi = strfmt_or_die("%s/var/lib/oprofile/abi", dir);
  char command[PATH_MAX], option[PATH_MAX];

  // ISSUE: This will not work natively.
  get_install_path(command, sizeof(command), "/bin/tile-opimport");

  snprintf(option, sizeof(option), " -f -a %s %s -o %s", abi, dir, dir);
  option[sizeof(option) - 1] = 0;

  if ((strlen(command) + strlen(option)) < sizeof(command))
  {
    strcat(command, option);
  }
  else
  {
    warn("During opimporting, the command is too long: %s%s\n",
         command, option);
    return -1;
  }

  if (system(command))
    warn("Failure in 'system(\"%s\")'.", command);

  free(abi);
  return 0;
}

static void
handle_command_quit(void)
{
  spew(1, "Quitting.");

  if (g_quitting)
    return;

  g_quitting = true;

  // Perma-start heartbeat if waiting for a heartbeat from our peer.
  if (Pollable_valid(&g_reporter_socket) &&
      g_self_heartbeats == g_peer_heartbeats + 1)
  {
    g_peer_heartbeats++;
    do_sim_finished_commands(&g_reporter_socket);
  }

  if (Pollable_valid(&g_peer_socket))
  {
    // Tell the peer we are ready to quit.
    do_peer_quit(&g_peer_socket);

    // HACK: Make sure the peer knows!
    Pollable_flush(&g_peer_socket);

    // Wait until the peer is ready to quit.
    while (!g_peer_quitting)
      handle_events();
  }

  if (!g_requested_exit)
  {
    g_requested_exit = true;

    if (g_greeted)
      do_request_exit(&g_shepherd_socket);

    kill_some_debuggers();

    // Paranoia?
    g_local_gdb = false;

    // HACK: Avoid having an extra prompt appear.
    // ISSUE: Just spin in "handle_events()"?
    if (g_readline_ready)
      rl_callback_handler_install("", NULL);
  }

  // HACK: If shepherd not greeted, just exit, and let simulator die
  // when the shepherd/reporter connections close.  ISSUE: What if
  // one peer has done this and the other has not?
  if (!g_greeted)
  {
    // Avoid annoying the simulator.
    if (Pollable_valid(&g_reporter_socket))
      handle_string("sim quit");

    exit(g_exit_code);
  }

  // FIXME: Somehow, the shepherd should check for one last round of
  // output from programs.  See, for example, the "--run -+- true -+-"
  // hack in "tools/examples/mpipe/ping/Makefile".
}


static void
handle_command_cd(const char* dir)
{
  char* cwd = canonify_tile(dir);

  spew(2, "Setting shepherd cwd to '%s'.", cwd);

  if (querify(ask_set_cwd(&g_shepherd_socket, cwd)))
  {
    FREESET(g_shepherd_cwd, strdup_or_die(cwd));
  }
}


static void
handle_command_mount(const char* host_dir, const char* tile_dir)
{
  host_dir = canonify_host(host_dir);
  tile_dir = canonify_tile(tile_dir);

  struct stat sbuf;
  if (stat(host_dir, &sbuf) != 0 || !S_ISDIR(sbuf.st_mode))
  {
    warn("Directory '%s' does not exist.", host_dir);
    return;
  }

  // HACK: Warn if the user tries to induce bug 9709.
  if (g_mounted_anything && !strcmp(tile_dir, "/sbin"))
    warn("Mounting /sbin after anything else is dangerous!");

  if (!querify(ask_file_mkdir(&g_shepherd_socket, tile_dir)))
  {
    warn("Could not mkdir '%s'.", tile_dir);
    return;
  }

  if (querify(ask_mount_fuse(&g_shepherd_socket, host_dir, tile_dir)))
  {
    g_mounted_anything = true;

    if (Pollable_valid(&g_reporter_socket))
      do_sim_map_dir(&g_reporter_socket, host_dir, tile_dir);

    StringArray_append(&g_symbol_map_dirs, strdup_or_die(host_dir));
    StringArray_append(&g_symbol_map_dirs, strdup_or_die(tile_dir));
  }
}


static void
handle_command_mount_same(const char* dir)
{
  handle_command_mount(dir, dir);
}


#ifndef __tile__

static void
handle_command_mount_tile(const char* dir)
{
  if (dir[0] != '/')
  {
    warn("Cannot use non-absolute dir '%s' with 'mount-tile'.", dir);
    return;
  }

  char tmp[PATH_MAX];
  get_install_tile_path(tmp, sizeof(tmp), dir);

  handle_command_mount(tmp, dir);
}

#endif


static void
handle_command_here(void)
{
  handle_command_mount_same(g_monitor_cwd);
  handle_command_cd(g_monitor_cwd);
}


static void
handle_command_rmdir(const char* dir)
{
  querify(ask_file_rmdir(&g_shepherd_socket, dir));
}


static void
handle_command_unlink(const char* path)
{
  querify(ask_file_unlink(&g_shepherd_socket, path));
}


// ISSUE: This should do a recursive delete.
static void
handle_command_delete(const char* path)
{
  querify(ask_file_unlink(&g_shepherd_socket, path));
}


static void
handle_command_mkdir(const char* dir)
{
  querify(ask_file_mkdir(&g_shepherd_socket, dir));
}


static void
handle_command_symlink(const char* path, const char* target)
{
  querify(ask_file_symlink(&g_shepherd_socket, path, target));
}


static void
handle_command_rename(const char* path, const char* newpath)
{
  querify(ask_file_rename(&g_shepherd_socket, path, newpath));
}



//--#define UPLOAD_TIME 1


// Include "upload" support.
#include "tools/manager/monitor/upload.c"


// Include "download" support.
#include "tools/manager/monitor/download.c"



// Asynchronously upload "host_path" to "tile_path".
//
static void
handle_command_upload_async(const char* host_path, const char* tile_path)
{
  handle_upload_start(canonify_host(host_path), canonify_tile(tile_path));
}


// Cancel any asynchronous uploads.
//
static void
handle_command_upload_cancel(void)
{
  handle_upload_cancel();
}


// Synchronously upload "host_path" to "tile_path", deferring any new
// interactive commands, and "stopping" if any fatal signals occur.
//
// ISSUE: Should interrupt call "handle_command_upload_cancel()",
// like it does in "handle_command_root()"?
//
static void
handle_command_upload_wait(void)
{
  g_waiting = true;
  while (g_waiting && upload_paths.size != 0)
    dispatch_events(-1);
  if (upload_paths.size != 0)
    warn("Uploading will continue asynchronously.");
  g_waiting = false;
}


// Synchronously upload "host_path" to "tile_path", deferring any new
// interactive commands, and "stopping" if any fatal signals occur.
//
static void
handle_command_upload(const char* host_path, const char* tile_path)
{
  struct timeval ts;
  timeval_now(&ts);

  handle_command_upload_async(host_path, tile_path);

  handle_command_upload_wait();

  struct timeval td;
  timeval_elapsed(&td, &ts);
  if (td.tv_sec != 0)
  {
    spew(2, "Elapsed time %ld.%06lds.", td.tv_sec, td.tv_usec);
  }
}


// Convenient shorthand for "--upload <path> <path>".
//
static void
handle_command_upload_same(const char* path)
{
  handle_command_upload(path, path);
}


// Convenient wrapper around "handle_command_upload", which uploads
// ".../tile<path>" to "<path>".
//
static void
handle_command_upload_tile(const char* path)
{
  if (path[0] != '/')
  {
    warn("Cannot 'upload-tile' non-absolute path '%s'.", path);
    return;
  }

  char tmp[PATH_MAX];
  get_install_tile_path(tmp, sizeof(tmp), path);

  handle_command_upload(tmp, path);
}

#ifndef __tile__

static int
handle_command_upload_tile_libs_aux(const char* stem, const char* tile_dir)
{
  int total = 0;

  char host_dir[PATH_MAX];
  get_install_tile_path(host_dir, sizeof(host_dir), tile_dir);

  DIR* dir = opendir(host_dir);
  if (dir == NULL)
    return 0;

  int stem_len = strlen(stem);

  struct dirent* val;
  while ((val = readdir(dir)) != NULL)
  {
    const char* name = val->d_name;

    if (!strcmp(name, ".") || !strcmp(name, ".."))
      continue;

    // Require "libXXX*".
    if (!has_prefix(name, "lib") || !has_prefix(name + 3, stem))
      continue;

    // Normally require "libXXX.so[.*]".
    // HACK: Also allow "libXXX-M.N.so[.*]"
    const char* suffix = name + 3 + stem_len;
    if (!strcmp(suffix, ".so") || has_prefix(suffix, ".so.") ||
        (suffix[0] == '-' && isdigit(suffix[1]) && strstr(suffix, ".so")))
    {
      char tile_path[PATH_MAX];
      char host_path[PATH_MAX];
      snprintf(tile_path, sizeof(tile_path), "%s/%s", tile_dir, name);
      snprintf(host_path, sizeof(host_path), "%s/%s", host_dir, name);
      handle_command_upload(host_path, tile_path);
      total++;
    }
  }

  if (closedir(dir) != 0)
    punt_with_errno("Failure in 'closedir()'");

  return total;
}

// Basically "--upload-tile" on all appropriate "[/usr]/lib[32]/*XXX*"
// (ignoring "*.a" files).
static void
handle_command_upload_tile_libs(const char* stem)
{
  if (handle_command_upload_tile_libs_aux(stem, "/lib") +
#if TILE_CHIP >= 10
      handle_command_upload_tile_libs_aux(stem, "/lib32") +
      handle_command_upload_tile_libs_aux(stem, "/usr/lib32") +
#endif
      handle_command_upload_tile_libs_aux(stem, "/usr/lib") +
      // HACK: Allow "perl" to find "libperl.so".
      handle_command_upload_tile_libs_aux(stem, "/usr/lib/perl5/CORE") == 0)
    warn("Could not find any dlls matching '%s'.", stem);
}

#endif

// Asynchronously download "tile_path" to "host_path".
//
static void
handle_command_download_async(const char* tile_path, const char* host_path)
{
  handle_download_start(canonify_tile(tile_path), canonify_host(host_path));
}


// Synchronously download "tile_path" to "host_path", deferring any new
// interactive commands, and "stopping" if any fatal signals occur.
//
static void
handle_command_download(const char* tile_path, const char* host_path)
{
  struct timeval ts;
  timeval_now(&ts);

  handle_command_download_async(tile_path, host_path);

  g_waiting = true;
  while (g_waiting && download_paths.size != 0)
    dispatch_events(-1);
  if (download_paths.size != 0)
    warn("Downloading will continue asynchronously.");
  g_waiting = false;

  struct timeval td;
  timeval_elapsed(&td, &ts);
  if (td.tv_sec != 0)
  {
    spew(2, "Elapsed time %ld.%06lds.", td.tv_sec, td.tv_usec);
  }
}


// Convenient shorthand for "--download <path> <path>".
//
static void
handle_command_download_same(const char* path)
{
  handle_command_download(path, path);
}



#ifndef __tile__


// For efficiency, during "--root", instead of uploading the paths
// below, we create symlinks into the special "/tile" FUSE mount.
// On Gx Wildcats using USB, this saves 0.7 (out of 1.0) seconds.
//
// NOTE: The small tile tarball has no "/var/lib/texmf/web2c".
//
const char* expensive[] = {
  "/etc/gconf",
  "/etc/pki",
  "/etc/selinux",
  "/etc/tile-thd",
  "/var/lib/rpm",
  "/var/lib/sfcb",
  "/var/lib/texmf/web2c",
};


// Basically, mount the standard "$TILERA_ROOT/tile/* subdirs on "/".
//
// Upload "/etc" because stuff like "/etc/mtab" is writable.
//
// Upload "/var" because stuff like "/var/lib/oprofile/" is writable.
//
// FIXME: Mounting "/sbin" can cause "ps" to hang, and can cause hangs
// at exit.  But simply uploading it isn't valid either, because some
// of its programs are linked against "/lib" or "/usr/lib", which go
// away when the monitor exits.  So we upload it to "/sbin.tile", and
// then "swap" it into place, and then the shepherd unswaps it later.
//
// Actually, mounting "/sbin" seems to work if it is mounted BEFORE any
// other fuse mount is done, perhaps because the shepherd unmounts the
// mounts in the same order, and thus unmounting "/sbin" first allows
// the real "/sbin" to be used while unmounting the other mounts.  So,
// for now, we let "--root" try this, if nothing has been mounted yet.
//
// ISSUE: The mounts should be "readonly", or somehow "shadowed" with
// local writable directories (which might work for "/var" too).
//
// Rumor has it that order matters below.
//
// ISSUE: Technically we should add "/sbin" (and "/etc" and "/var")
// to "g_symbol_map_dirs", if this function succeeds, since we disable
// the implicit upload mapping since "/sbin.tile" is a temporary path.
//
static void
handle_command_root(void)
{
  char tile[PATH_MAX];
  get_install_tile_path(tile, sizeof(tile), "");

  static bool done;

  if (done)
  {
    warn("Ignoring duplicate 'root' attempt.");
    return;
  }

  // Flush pending uploads.
  handle_command_upload_wait();

  if (upload_paths.size != 0)
  {
    warn("Interrupted 'root' while flushing pending uploads.");
    return;
  }

  // HACK: Make sure "/etc" is "tmpfs", and is thus NOT a "persistent" disk.
  if (!querify(ask_system(&g_shepherd_socket, "df -P /etc | grep -q tmpfs")))
  {
    warn("Ignoring 'root' attempt with non-tmpfs '/etc'.");
    return;
  }

  // ISSUE: To avoid the problems described in Bug 9709, if anything has
  // already been mounted, then "/sbin" must be uploaded, not mounted.
  bool upload_sbin = g_mounted_anything;

  struct timeval start;
  timeval_now(&start);

  // Create a temp file.  Should not fail.
  char tarball[] = "/tmp/monitor-tile.XXXXXX";
  int fd = mkstemp_or_die(tarball);
  close(fd);

  int okay = false;

  Buffer compress;
  Buffer_init(&compress);

  Buffer extract;
  Buffer_init(&extract);

  // Delete any old upload directories.
  const char* prep =
    "rm -rf /.tile /.tile.tar "
    "/etc.tile /var.tile /sbin.tile "
    "/etc.orig /var.orig /sbin.orig && "
    "mkdir /.tile";
  if (!querify(ask_system(&g_shepherd_socket, prep)))
    goto done;

  // Add "/etc" and "/var" to the tarball (as "root").
  Buffer_printf(&compress, "tar --owner=0 --group=0 -C %s -cf %s etc var",
                tile, tarball);

  // Add "/sbin" if needed.
  if (upload_sbin)
    Buffer_printf(&compress, " sbin");

  // Exclude the "expensive" subdirs (skip the leading slash).
  for (int i = 0; i < NELEM(expensive); i++)
    Buffer_printf(&compress, " --exclude %s", expensive[i] + 1);

  if (system((char*)(compress.data)) != 0)
    goto done;

  // Upload the tarball.
  handle_command_upload(tarball, "/.tile/tile.tar");

  // Detect interrupts.  ISSUE: If this happens, we will delete the
  // tarball while it is still being uploaded.
  if (upload_paths.size != 0)
    goto done;

  // Extract the tarball.
  Buffer_printf(&extract, "tar -C /.tile -xf /.tile/tile.tar && ");

  // Create symlinks for the "expensive" subdirs.
  for (int i = 0; i < NELEM(expensive); i++)
  {
    // Create the subdir containing the symlink.
    Buffer_printf(&extract, "mkdir -p /.tile%s", expensive[i]);
    while (extract.data[extract.size] != '/')
      extract.size--;

    // Create the actual symlink.
    Buffer_printf(&extract, " && ln -s /tile%s /.tile%s && ",
                  expensive[i], expensive[i]);
  }

  // HACK: Create an appropriate "/etc/mtab" file.
  // NOTE: The "/etc/fstab" file is apparently harmless.
  Buffer_printf(&extract, "ln -sf /proc/self/mounts /.tile/etc/mtab && ");

  // ISSUE: Fix permissions on "/etc/shadow"?

  // Copy any existing "/etc/localtime" and "/etc/resolv.conf" into new /etc.
  Buffer_printf(&extract, "tar -C / -cf /files.tile.tar"
                " etc/localtime etc/resolv.conf 2> /dev/null; ");

  Buffer_printf(&extract, "mv /.tile/etc /etc.tile && ");
  Buffer_printf(&extract, "mv /.tile/var /var.tile && ");
  if (upload_sbin)
    Buffer_printf(&extract, "mv /.tile/sbin /sbin.tile &&");
  Buffer_printf(&extract, "rm -rf /.tile");

  if (!querify(ask_system(&g_shepherd_socket, (char*)(extract.data))))
    goto done;

  okay = true;

 done:

  Buffer_destroy(&extract);

  Buffer_destroy(&compress);

  (void)unlink(tarball);

  // Mention the elapsed time, especially if at least 1 second.
  struct timeval td;
  timeval_elapsed(&td, &start);
  spew((td.tv_sec >= 1) ? 1 : 2,
       "Preparing the 'root' subdirs took %u.%06u seconds.",
       (uint)td.tv_sec, (uint)td.tv_usec);

  if (!okay)
  {
    warn("Failed to prepare the 'root' subdirs.");
    return;
  }

  // At this point, we are basically committed.
  done = true;

  timeval_now(&start);

  // ISSUE: Use a special query to perform this whole operation,
  // taking the canonicalized "install" dir as an argument?

  // Swap the "/xxx.tile" subdirs in for the "/xxx" subdirs.
  // HACK: The shepherd "undoes" this whole operation on exit.
  const char* swap_etc =
    "mv /etc /etc.orig; mv /etc.tile /etc";
  if (!querify(ask_system(&g_shepherd_socket, swap_etc)))
  {
    warn("Could not swap out '/etc' for '/etc.tile'.");
    return;
  }
  const char* swap_files =
    "tar -C / -xf /files.tile.tar && rm -f /files.tile.tar";
  if (!querify(ask_system(&g_shepherd_socket, swap_files)))
  {
    warn("Could not untar saved '/etc' files.");
    return;
  }
  const char* swap_var =    
    "mv /var /var.orig; mv /var.tile /var";
  if (!querify(ask_system(&g_shepherd_socket, swap_var)))
  {
    warn("Could not swap out '/var' for '/var.tile'.");
    return;
  }

  // Manage the fact that busybox is /sbin/busybox in newer MDEs,
  // so when we try to swap it out, we break everything that links to it.
  // Instead make sure we have /sbin.orig in our path, and invoke
  // the busybox "mv" applet explicitly to move /sbin.tile back to /sbin.
  const char* swap_sbin =
    "mv /sbin /sbin.orig; PATH=$PATH:/sbin.orig busybox mv /sbin.tile /sbin";
  if (upload_sbin && !querify(ask_system(&g_shepherd_socket, swap_sbin)))
  {
    warn("Could not swap out '/sbin' for '/sbin.tile'.");
    return;
  }

  // ISSUE: These "mount" commands take two seconds on Gx over USB.
  // But "mount -o bind /tile/boot /boot" takes more than a second!

  if (!upload_sbin)
    handle_command_mount_tile("/sbin");

  // ISSUE: Does order matter here?
  handle_command_mount_tile("/boot");
  handle_command_mount_tile("/usr");
#if TILE_CHIP >= 10
  handle_command_mount_tile("/lib32");
#endif
  handle_command_mount_tile("/lib");
  handle_command_mount_tile("/bin");

  // Mount "$TILERA_ROOT/tile" itself as "/tile", for use by the
  // symlinks in "/etc" and "/var" created above.
  handle_command_mount(tile, "/tile");

  // Mention the elapsed time, especially if at least 3 seconds.
  timeval_elapsed(&td, &start);
  spew((td.tv_sec >= 3) ? 1 : 2,
       "Creating the 'root' FUSE mounts took %u.%06u seconds.",
       (uint)td.tv_sec, (uint)td.tv_usec);
}

#endif



// React to successful uploads of executable files which appear to be
// "TILE" executables or shared libraries by tracking the mapping, and
// reporting it to the "reporter".
//
// ISSUE: We do NOT check if the file actually contains any symbols.
//
// ISSUE: The arguments are not necessarily "absolute" paths!
//
static void
handle_command_symbol_map(const char* host_path, const char* tile_path)
{
  if (file_is_tile_binary(host_path))
  {
    char* host_file = canonify_host(host_path);
    char* tile_file = canonify_tile(tile_path);

    spew(2, "Mapping '%s' to '%s'.", host_file, tile_file);

    StringArray_append(&g_symbol_map_paths, strdup_or_die(host_file));
    StringArray_append(&g_symbol_map_paths, strdup_or_die(tile_file));

    if (Pollable_valid(&g_reporter_socket))
      do_sim_map_file(&g_reporter_socket, host_file, tile_file);
  }
  // If file exists and is _not_ a binary, that's okay,
  // since a symbol mapping is useless for non-binary files.
  // A non-existent file may indicate a usage error, so warn.
  // This should only happen when the user invokes symbol-map directly,
  // since upload complains if there's no source file/directory.
  else if (!file_exists(host_path))
  {
    warn("Ignoring symbol mapping for non-existent host path '%s'.",
         host_path);
  }
}



// Handle "request_protocol" reply.
//
static void
handle_request_protocol_reply(void* info, int protocol)
{
  g_shepherd_protocol = protocol;

  if (protocol != SHEPHERD_PROTOCOL_MONITOR && !g_ignore_protocol)
  {
    punt("Shepherd protocol mismatch (0x%x != 0x%x)!",
         protocol, SHEPHERD_PROTOCOL_MONITOR);
  }
}


// Handle "request_protocol" error.
//
static void
handle_request_protocol_error(void* info, char* msg)
{
  // NOTE: This can only happen with a VERY old (pre-1.3) shepherd.
  punt("Shepherd protocol is not available!  Upgrade your shepherd!");
}


// Handle "request_protocol" timeout.
//
// Basically, this can only happen with PCI/USB.
//
static void
handle_request_protocol_alarm(Alarm* alarm)
{
  if (g_opt_resume)
  {
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that a reboot is necessary.\n\n");
  }
  else
  {
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that booting is still in progress, "
             "or failed, or did\n"
             "not launch a compatible 'shepherd'.  Try again, using '--greet-timeout 300',\n"
             "and watching the console for boot failures.\n\n");
  }
  punt("Cannot contact shepherd!");
}


// Handle boot timeout.
//
static void
handle_boot_timeout_alarm(int sig)
{
  snprintf(g_exit_msg, sizeof(g_exit_msg),
           "This indicates that booting is taking much, much longer than "
           "expected.\n"
           "This could indicate a corrupted bootrom, a fatal software "
           "failure very\n"
           "early in the boot process, or perhaps hardware problems with "
           "the system\n"
           "being booted.  If you have some reason to believe that it "
           "should be taking\n"
           "a very long time, you can increase the boot timeout with "
           "the '--boot-timeout'\n"
           "option, or eliminate it with '--boot-timeout 0'.\n\n");
  punt("Boot timed out!");
}


static void
thaw_simulator_if_needed(void)
{
  static bool thawed;

  if (thawed)
    return;

  thawed = true;

  if (!Pollable_valid(&g_reporter_socket))
    return;

  if (Pollable_valid(&g_peer_socket))
    do_sim_heartbeat_start(&g_reporter_socket);
  else
    do_sim_finished_commands(&g_reporter_socket);
}


static void
setup_greet_alarm_if_needed(void)
{
  if (Alarm_scheduled(&g_greet_alarm))
    return;

  if (!Pollable_valid(&g_reporter_socket))
  {
    g_greet_alarm.handler = handle_request_protocol_alarm;
    // Basically "now + 200s".
    g_greet_alarm.when.tv_sec = time(NULL) + g_greet_timeout;
    Alarm_schedule(&g_greet_alarm);
  }
}


static void
greet_shepherd_if_needed(void)
{
  if (g_greeted)
  {
    if (Alarm_scheduled(&g_greet_alarm))
      Alarm_cancel(&g_greet_alarm);
    return;
  }

  setup_greet_alarm_if_needed();

  thaw_simulator_if_needed();

  // HACK: Tell the shepherd to become interactive.
  // ISSUE: What if the shepherd has not yet been greeted?
  if (Pollable_valid(&g_peer_socket) && !g_become_interactive_sent)
  {
    do_become_interactive(&g_shepherd_socket);
    g_become_interactive_sent = true;
  }

  // Greet the shepherd.
  spew(2, "Greeting the shepherd...");
  snprintf(g_exit_msg, sizeof(g_exit_msg),
           "This normally indicates that the version of "
           "'tile-monitor' which is being\n"
           "used is incompatible with the version of the "
           "'shepherd' which is contained\n"
           "in your 'vmlinux' and 'bootrom' files.\n\n");

  query_request_protocol(&g_shepherd_socket,
                         handle_request_protocol_reply, NULL,
                         handle_request_protocol_error, NULL);
  while (g_shepherd_protocol == 0)
    dispatch_events(-1);
  if (Alarm_scheduled(&g_greet_alarm))
    Alarm_cancel(&g_greet_alarm);
  g_exit_msg[0] = '\0';
  spew(2, "Greeting the shepherd... done.");

  // Maybe ignore protocols.
  if (g_ignore_protocol)
    do_ignore_protocol(&g_shepherd_socket);

  // Make sure the shepherd has the expected CWD.
  do_set_cwd(&g_shepherd_socket, g_shepherd_cwd);

  // Request chip size.
  do_request_size(&g_shepherd_socket);

  // Wait for chip size, which will set "g_greeted".
  spew(3, "Waiting for size...");
  while (!g_greeted)
    dispatch_events(-1);
  spew(3, "Waiting for size... done.");
}



static void
handle_command_env(const char* arg1)
{
  free(modify_envp(&g_envp, strdup_or_die(arg1)));
}


static void
handle_command_huge_pages(char* arg1)
{
  querify(ask_huge_pages(&g_shepherd_socket, arg1));
}


#ifndef __tile__

static void
handle_command_raw_wait(void)
{
  if (!g_opt_raw_mode)
  {
    warn("Cannot 'raw-wait' without '--raw-mode'.");
    return;
  }

  spew(2, "Waiting for raw simulator to exit.");

  thaw_simulator_if_needed();

  while (true)
  {
    int status = 0;
    if (waitpid_or_die(g_simulator_pid, &status, WNOHANG) != 0)
    {
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        handle_unexpected_simulator_death(status);
      break;
    }

    dispatch_events(-1);
  }

  exit(0);
}

#endif


static void
handle_command_peer_barrier(void)
{
  if (!Pollable_valid(&g_peer_socket))
  {
    warn("There is no peer (for peer_barrier).");
    return;
  }

  g_peer_barrier_sent++;

  do_peer_barrier(&g_peer_socket);

  // Wait for the barrier.
  g_waiting = true;
  while (g_waiting && g_peer_barrier_received < g_peer_barrier_sent)
    handle_events();
  if (g_peer_barrier_received < g_peer_barrier_sent)
    warn("The peer has not actually reached the barrier!");
  g_waiting = false;
}


// ISSUE: Deprecate "peer-quit".
static void
handle_command_peer_quit(void)
{
  handle_command_quit();
}


static void
handle_command_rtc(void)
{
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0)
  {
    warn("Could not determine current time.");
    return;
  }
  if (!querify(ask_set_clock(&g_shepherd_socket,
                             tv.tv_sec, tv.tv_usec * 1000)))
  {
    warn("Could not set current time.");
    return;
  }

  // Upload "/etc/localtime", which may be a symlink.
  struct stat st;
  if (stat("/etc/localtime", &st) == 0)
  {
    // In practice, "/etc/localtime" is always under 10K.
    unsigned char buf[16384];
    size_t n = read_from_file_or_die("/etc/localtime", buf, sizeof(buf));
    if (!querify(ask_file_write(&g_shepherd_socket, "/etc/localtime", buf, n)))
      warn("Could not set timezone using '/etc/localtime'.");
  }
}


static void
handle_command_verbosity(const char* arg1)
{
  message_verbosity = atoi(arg1);
}


static void
handle_command_shepherd_verbosity(const char* arg1)
{
  do_set_verbosity(&g_shepherd_socket, atoi(arg1));
}


static void
handle_command_ping(void)
{
  note("Pinging the shepherd...");
  if (querify_with_timeout(ask_ping(&g_shepherd_socket), 5000))
    note("Shepherd responded to ping.");
}


static void
handle_command_dump(void)
{
  querify_with_timeout(ask_dump(&g_shepherd_socket), 5000);
}


static void
handle_command_unlimit_fds(void)
{
  do_unlimit_fds(&g_shepherd_socket);
}


static void
handle_command_spread_threads(void)
{
  do_spread_threads(&g_shepherd_socket);
}


#ifndef __tile__

static void
handle_command_set_functional(const char* arg1)
{
  if (!strcmp(arg1, "0"))
    querify(ask_set_functional(&g_shepherd_socket, false));
  else if (!strcmp(arg1, "1"))
    querify(ask_set_functional(&g_shepherd_socket, true));
  else
    warn("Illegal argument '%s' for '--set-functional' (use 0/1).", arg1);
}

#endif


static void
handle_command_server(void)
{
  spew(3, "Entering server mode...");

  // Unfreeze the shepherd.
  become_interactive();
  thaw_simulator_if_needed();

  // Handle events.
  g_waiting = true;
  while (g_waiting)
    handle_events();

  warn("Leaving server mode.");
}


static void
handle_command_shepherd_tiles(char* arg1)
{
  StringArray tiles;
  StringArray_init(&tiles);
  tokenize(&tiles, arg1);
  do_shepherd_tiles(&g_shepherd_socket, &tiles);
  StringArray_destroy(&tiles);
}



#ifndef __tile__

static void
handle_command_sim(StringArray* args)
{
  if (!Pollable_valid(&g_reporter_socket))
  {
    warn("There is no simulator.");
    return;
  }

  querify(ask_sim_handle_command(&g_reporter_socket, args));
}

#endif


static void
handle_command_forbid_tiles(char* arg1)
{
  StringArray tiles;
  StringArray_init(&tiles);
  tokenize(&tiles, arg1);
  do_forbid_tiles(&g_shepherd_socket, &tiles);
  StringArray_destroy(&tiles);
}


static void
handle_command_gdb_command(char* arg1)
{
  StringArray_append(&g_gdb_commands, strfmt_or_die("%s\n", arg1));
}


static void
handle_command_debug_tile(const char* tile)
{
  StringArray_append(&g_debug, strdup_or_die(tile));
}


static void
handle_command_debug_executable(const char* path)
{
  do_debug_executable(&g_shepherd_socket, path);
}


static void
handle_command_debug_executables(StringArray* paths)
{
  do_debug_executables(&g_shepherd_socket, paths);
}


static void
handle_command_debug_next(void)
{
  do_debug_next(&g_shepherd_socket);
}


static void
handle_command_debug_on_crash(void)
{
  do_set_debug_on_crash(&g_shepherd_socket, true);
}


#ifndef __tile__

static void
handle_command_bm_debug_attach_tile(char* tile)
{
  if (!Pollable_valid(&g_reporter_socket))
  {
    warn("Cannot handle 'bm-debug-attach-tile' without simulator!");
    return;
  }

  unsigned int x, y;
  if (sscanf(tile, "%u,%u", &x, &y) != 2)
  {
    warn("Cannot parse tile '%s'.", tile);
    return;
  }

  do_sim_bmd_attach(&g_reporter_socket, x, y);
}


static void
handle_command_bm_debug_on_panic(void)
{
  handle_string("sim set bm-debug-on-panic 1");
}

#endif


static void
handle_command_attach(const char* pid)
{
  do_debug_attach(&g_shepherd_socket, atoi(pid));
}


static void
handle_command_watch(const char* pid)
{
  do_watch_process(&g_shepherd_socket, atoi(pid));
}


static void
handle_command_tile(const char* tile)
{
  StringArray_append(&g_tiles, strdup_or_die(tile));
}


static void
handle_command_tiles(StringArray* tiles)
{
  StringArray_free_and_clear(&g_tiles);
  for (int i = 0; i < tiles->size; i++)
    StringArray_append(&g_tiles, strdup_or_die(tiles->data[i]));
}


static void
handle_launch_reply(void* info, int pid)
{
  int app = (int)(intptr_t)info;

  spew(2, "Launch #%d yielded pid %u.", app, pid);
}


static void
handle_launch_error(void* info, char* msg)
{
  int app = (int)(intptr_t)info;

  warn("Launch #%d failed: %s", app, msg);

  // Stop waiting if needed.
  if (g_awaiting_reap == app)
    g_awaiting_reap = 0;

  if (g_exit_code == 0)
    g_exit_code = 126;
}


static void
handle_command_launch(StringArray* args)
{
  int app = ++g_total_launches;
  spew(2, "Launch #%d initiated.", app);
  query_launch(&g_shepherd_socket,
               handle_launch_reply, (void*)(intptr_t)app,
               handle_launch_error, (void*)(intptr_t)app,
               args, &g_envp, &g_tiles, &g_debug);
  StringArray_free_and_clear(&g_tiles);
  StringArray_free_and_clear(&g_debug);
}


static void
handle_command_run(StringArray* args)
{
  handle_command_launch(args);

  spew(3, "Waiting for the app to be reaped...");

  g_awaiting_reap = g_total_launches;

  // Let the shepherd know we are waiting.
  do_awaiting_app(&g_shepherd_socket, g_awaiting_reap);

  // Handle events until reaped.
  g_waiting = true;
  while (g_waiting && g_awaiting_reap != 0)
    handle_events();
  if (g_awaiting_reap != 0)
    warn("The launched app is still running.");
  g_waiting = false;
}


static void
handle_command_debug(StringArray* args)
{
  do_debug_next(&g_shepherd_socket);
  handle_command_launch(args);
}


static void
handle_command_kill_everyone(void)
{
  spew(1, "Killing everyone.");

  do_kill_everyone(&g_shepherd_socket);
}


// Wait until the chip is "idle".
//
// ISSUE: Should this also wait for pending uploads/downloads to finish?
//
static void
handle_command_wait(void)
{
  spew(3, "Waiting for the chip to be idle...");

  g_pending_note_idle = false;

  // Request "note_idle".
  do_request_idle(&g_shepherd_socket);

  // Let the shepherd know we are waiting.
  do_awaiting_app(&g_shepherd_socket, -1);

  // Handle events until idle.
  g_waiting = true;
  while (g_waiting && !g_pending_note_idle)
    handle_events();
  if (!g_pending_note_idle)
    warn("The chip is not actually idle.");
  g_waiting = false;

  spew(3, "Waiting for the chip to be idle... done.");
}


static void
handle_command_profile_tool(const char* toolname)
{
  // TODO: Detect whether user is currently using a different tool,
  // and react to that helpfully.  For now we leave it up to the user
  // to know what they're doing if they switch tools in a run.

  // Determine which profile tool is wanted.
  if (!strcmp(toolname, "oprofile"))
  {
    g_profile_with_oprofile = true;
    spew(1, "Profiling using OProfile.");
  }
  else if (!strcmp(toolname, "perf"))
  {
    g_profile_with_oprofile = false;
    spew(1, "Profiling using Linux perf events.");
  }
  else
  {
    warn("'%s' is not recognized as a profiling tool. ", toolname);
    return;
  }

  // Warn the user if they're changing tools but
  // (a) they have done profile-start, and
  // (b) not yet done profile-capture
  // since this basically means they're throwing away state.
  if (g_profile_start_without_capture_warning)
  {
    warn("You have invoked profile-start but not profile-capture.");

    fprintf(stderr,
            "By changing tools at this point you are abandoning "
            "whatever profiling\n"
            "state (if any) you have obtained with the current "
            "tool.  If this is not\n"
            "your intent, just use profile-tool again to re-select "
            "the old tool and\n"
            "continue working with it.\n");

    // Don't warn the user again until they've done another profile-start.
    g_profile_start_without_capture_warning = false;
  }
}


static void
handle_command_profile_init(void)
{
  bool need_upload = true;

  // HACK: Warn about a common mis-use.
  for (uint i = 0; i < g_symbol_map_dirs.size; i += 2)
  {
    char* tile_dir = StringArray_get(&g_symbol_map_dirs, i + 1);
    if (!strcmp(tile_dir, "/usr"))
    {
      warn("The '/usr' directory is mounted from a host-side directory.\n"
           "We will assume the profiler executables/libraries, etc. already exist there,\n"
           "and not upload/overwrite them.\n"
           "Note: this also means you'll be accessing these files over a FUSE mount,\n"
           "which can make profiler startup/shutdown extremely slow.\n"
           "It's highly recommended that you not mount /usr when doing profiling.");
      need_upload = false;
      break;
    }
  }

  if (g_profile_with_oprofile)
  {
    if (need_upload)
    {
      // Upload commands/libraries required by OProfile.
      handle_command_upload_tile("/usr/bin/opjitconv");
      handle_command_upload_tile("/usr/bin/oprofiled");
      handle_command_upload_tile("/usr/bin/opcontrol");
      handle_command_upload_tile("/usr/bin/ophelp");

#ifndef __tile__
      handle_command_upload_tile_libs("popt");
      handle_command_upload_tile_libs("z");
#endif

      // NOTE: This is a directory.
      handle_command_upload_tile("/usr/share/oprofile/tile");
    }

    // FIXME: avoid leaking strings if user switches tools.

    // Default to no kernel profiling.
    FREESET(g_profile_start_kernel, strdup_or_die("--no-vmlinux"));

    // Select default events/intervals.
#if CHIP_PERFORMANCE_COUNTERS() >= 4
    // Choose default OProfile event counts based on chip type.
    // These want to be around 100 samples per second,
    // assuming the "average" speed of the chip in normal use.
#if TILE_CHIP >= 10
    // TileGx, assume 1200Mhz "average" speed.
    FREESET(g_profile_start_events,
            strdup_or_die("--event=ONE:12000000 "
                          "--event=INSTRUCTION_BUNDLE:12000000 "
                          "--event=INSTRUCTION_STALL:12000000 "
                          "--event=LOAD_STALL:12000000"));
#else
    // TilePro, assume 800Mhz "average" speed.
    FREESET(g_profile_start_events,
            strdup_or_die("--event=ONE:8000000 "
                          "--event=MP_BUNDLE_RETIRED:8000000 "
                          "--event=MP_DATA_CACHE_STALL:8000000 "
                          "--event=MP_INSTRUCTION_CACHE_STALL:8000000"));
#endif
#else
    // Tile64, assume 600Mhz "average" speed.
    // FIXME: we're not building for Tile64 any more,
    // so at some point this case should go away.
    FREESET(g_profile_start_events,
            strdup_or_die("--event=ONE:6000000 "
                          "--event=MP_BUNDLE_RETIRED:6000000"));
#endif

    // Default to using 20 as the stack frame depth for each sample.
    FREESET(g_profile_start_flags, strdup_or_die("--callgraph=20"));
  }
  else
  {
    if (need_upload) {
      // Upload commands/libraries required by perf events
      handle_command_upload_tile("/usr/bin/perf");
      handle_command_upload_tile("/usr/lib/perl5/CORE/libperl.so");

#ifndef __tile__
      handle_command_upload_tile_libs("elf");
      handle_command_upload_tile_libs("python2.6");
      handle_command_upload_tile_libs("z");
      handle_command_upload_tile_libs("m");
#endif
    }

    // FIXME: avoid leaking strings if user switches tools.

    // Default to no kernel profiling.
    FREESET(g_profile_start_kernel, strdup_or_die(""));

    // Select default events/intervals.
#if CHIP_PERFORMANCE_COUNTERS() >= 4
    // Choose default event counts based on chip type.
    // These want to be around 100 samples per second,
    // assuming the "average" speed of the chip in normal use.
#if TILE_CHIP >= 10
    // TileGx, assume 1200Mhz "average" speed.
    FREESET(g_profile_start_events,
            strdup_or_die("-c 12000000 "
                          "-e ONE,INSTRUCTION_BUNDLE,INSTRUCTION_STALL,LOAD_STALL"));
#else
    // TilePro, assume 800Mhz "average" speed.
    FREESET(g_profile_start_events,
            strdup_or_die("-c 8000000 "
                          "-e ONE,MP_BUNDLE_RETIRED,MP_DATA_CACHE_STALL,MP_INSTRUCTION_CACHE_STALL"));
#endif
#else
    // Tile64, assume 600Mhz "average" speed.
    // FIXME: we're not building for Tile64 any more,
    // so at some point this case should go away.
    FREESET(g_profile_start_events,
            strdup_or_die("-c 6000000 "
                          "-e ONE,MP_BUNDLE_RETIRED"));
#endif

    // Default to enabling collection of callgraph information
    FREESET(g_profile_start_flags, strdup_or_die("-g"));
  }
}


static void
handle_command_profile_vmlinux_oprofile(const char* kernel)
{
  // ISSUE: This will not work natively.
  char objdump_buf[PATH_MAX];
  get_install_path(objdump_buf, sizeof(objdump_buf), "/bin/tile-objdump");

  char tmp[PATH_MAX * 2 + 32];
  snprintf(tmp, sizeof(tmp), "%s -h %s", objdump_buf, kernel);

  // The important line from "objdump -h VMLINUX" looks like:
  //
  //   2 .text         002a4998  fd010000  00010000  00004000  2**3
  //

  unsigned long long kernel_start = 0;
  unsigned long long kernel_end = 0;

  FILE* fp = popen(tmp, "r");
  if (fp != NULL)
  {
    while (fgets(tmp, sizeof(tmp), fp) != NULL)
    {
      char* s = tmp;

      // Only process "*.text*" sections.
      if (strstr(s, ".text") == NULL)
        continue;

      // Advance to the first word (section number).
      while (*s == ' ')
        s++;

      // Skip the first word.
      while (*s && *s != ' ')
        s++;

      // Advance to the second word (section name).
      while (*s == ' ')
        s++;

      // Skip the second word.
      while (*s && *s != ' ')
        s++;

      // Advance to the third word (size).
      while (*s == ' ')
        s++;

      // Parse the third word, and advance.
      unsigned long long w3 = strtoull(s, &s, 16);
      if (*s != ' ')
        break;

      // Advance to the fourth word (start).
      while (*s == ' ')
        s++;

      // Parse the fourth word, and advance.
      unsigned long long w4 = strtoull(s, &s, 16);
      if (*s != ' ')
        continue;

      // Success.
      if (kernel_start == 0)
        kernel_start = w4;
      kernel_end = w4 + w3;
    }

    (void)pclose(fp);
  }

  if (kernel_start == 0)
  {
    warn("Cannot obtain start/end from '%s'.", kernel);
    return;
  }

  char* kernel_path = "/var/lib/oprofile-support/vmlinux";
  handle_command_upload(kernel, kernel_path);

  // Upload the real one if vmlinux is a symbolic link.
  struct stat stat_buf;
  int link_len;

  if ((lstat(kernel, &stat_buf) == 0) &&
      S_ISLNK(stat_buf.st_mode) &&
      (link_len = readlink(kernel, tmp, sizeof(tmp) - 1)) > 0)
  {
    tmp[link_len] = 0;

    // Get the host path of the real vmlinux file.
    char host_path[PATH_MAX];
    char* c;

    if (tmp[0] != '/')
    {
      strncpy(host_path, kernel, sizeof(host_path));
      host_path[sizeof(host_path) - 1] = 0;
      c = strrchr(host_path, '/');
      if (c != NULL)
        c++;
      else
        c = host_path;
      *c = 0;
      strncat(host_path, tmp, sizeof(host_path));
      host_path[sizeof(host_path) - 1] = 0;
    }
    else
    {
      strncpy(host_path, tmp, sizeof(host_path));
      host_path[sizeof(host_path) - 1] = 0;
    }

    char tile_path[PATH_MAX] = "/var/lib/oprofile-support/";

    if ((lstat(host_path, &stat_buf) == 0) &&
        S_ISREG(stat_buf.st_mode))
    {
      // Get the tile path of the real vmlinux file.
      c = strrchr(host_path, '/');
      if (c != NULL)
        c++;
      else
        c = host_path;
      strncat(tile_path, c, sizeof(tile_path));
      tile_path[sizeof(tile_path) - 1] = 0;

      // Upload the real vmlinux file.
      handle_command_upload(host_path, tile_path);
    }
    else
      warn_with_errno("Failed to upload vmlinux symlink target %s -> %s (%s)\n",
                      kernel, tmp, host_path); 
  }

  // Note that "--kernel-range" must precede "--vmlinux"!
  FREESET(g_profile_start_kernel,
          strfmt_or_die("--kernel-range=%llx,%llx --vmlinux=%s",
                        kernel_start, kernel_end, kernel_path));

  // Create a semi-convenient, incomplete, "opcontrol-kernel" script,
  // encoding "g_profile_start_kernel".  ISSUE: Is anyone using this?

  char temp[] = "/tmp/opcontrol-kernel.XXXXXX";
  int fd = mkstemp(temp);
  FILE* fff = (fd >= 0) ? fdopen(fd, "w") : NULL;
  if (fff == NULL)
  {
    warn("Could not create '%s': %s", temp, strerror(errno));
    return;
  }

  fprintf(fff, "#!/bin/sh\n");
  fprintf(fff, "exec opcontrol \"$@\" %s\n", g_profile_start_kernel);

  (void)fclose(fff);

  (void)chmod(temp, 0555);

  handle_command_upload(temp,
                        "/var/lib/oprofile-support/bin/opcontrol-kernel");

  (void)unlink(temp);
}


static void
handle_command_profile_vmlinux_perf_events(const char* kernel)
{
  char* kernel_path = "/var/lib/perf-support/vmlinux";
  handle_command_upload(kernel, kernel_path);

  FREESET(g_profile_start_kernel,
          strfmt_or_die("-v %s", kernel_path));

  // NOTE: Unlike with oprofile, the kernel argument is used
  // when analyzing the results, not when running the profiler.
}


static void
handle_command_profile_vmlinux(const char* kernel)
{
  if (g_profile_with_oprofile)
    handle_command_profile_vmlinux_oprofile(kernel);
  else
    handle_command_profile_vmlinux_perf_events(kernel);
}


static void
handle_command_profile_kernel(void)
{
  handle_command_profile_vmlinux(g_vmlinux_file);
}


static void
handle_command_profile_events(const char* flags)
{
  FREESET(g_profile_start_events, strdup_or_die(flags));
}


static void
handle_command_profile_flags(const char* flags)
{
  FREESET(g_profile_start_flags, strdup_or_die(flags));
}


static void
handle_command_profile_extra(const char* str)
{
  warn("Use '--profile-events' and/or '--profile-flags'!");
}


static void
handle_command_profile_start_daemon(void)
{
  // TODO: Induce "--profile-init" if not done yet.

  if (g_profile_with_oprofile)
  {
    char* command =
      strfmt_or_die("opcontrol --start-daemon "
                    "--separate=kernel,thread,cpu %s %s %s",
                    g_profile_start_kernel,
                    g_profile_start_events,
                    g_profile_start_flags);
    querify(ask_system(&g_shepherd_socket, command));
    free(command);
  }
}


static void
handle_command_profile_shutdown(void)
{
  if (g_profile_with_oprofile)
  {
    // Ask opcontrol to shut down the daemon, if any.
    querify(ask_system(&g_shepherd_socket, "opcontrol --shutdown"));
  }
}


static void
handle_command_profile_start(void)
{
  char* command;

  // TODO: Induce "--profile-init" if not done yet.

  // Track whether user has done "profile_start" but
  // not done "profile_capture" yet.
  g_profile_start_without_capture_warning = true;

  if (g_profile_with_oprofile)
  {
    // Ask opcontrol to start profiling.
    command =
      strfmt_or_die("opcontrol --start "
                    "--separate=kernel,thread,cpu %s %s %s",
                    g_profile_start_kernel,
                    g_profile_start_events,
                    g_profile_start_flags);
  }
  else
  {
    // We run "perf record" in the background, logging to "/tmp/perf.out",
    // with the following options:
    //   -a to select "system-wide" profiling
    //   any event (-c/-e) and flag (-g) arguments
    //   -o specifies output data file path
    //   -f forces overwrite of the output data file, if needed
    //   a dummy sleep command that effectively runs forever
    // We capture this process's PID in a file so we can SIGINT it later.
    command =
      strfmt_or_die("perf record -a %s %s -f -o /tmp/perf.data -- "
                    "sleep 10000d >& /tmp/perf.out &"
                    " echo $! > /tmp/perf.pid",
                    g_profile_start_events,
                    g_profile_start_flags);
  }

  spew(1, "Starting profiler using: %s", command);

  querify(ask_system(&g_shepherd_socket, command));
  free(command);
}


static void
handle_command_profile_stop(void)
{
  if (g_profile_with_oprofile)
  {
    // Ask opcontrol to stop profiling.
    querify(ask_system(&g_shepherd_socket, "opcontrol --stop"));
  }
  else
  {
    // We just need to interrupt the process we ran in profile-start.
    // Perf will automatically "clean up" and record its profile data.
    // NOTE: We need to wait a second or so after the profiled command
    // finishes, to allow perf to finish updating its data structures,
    // before we kill it.
    // ALSO: We need to leave a little time after killing it to allow
    // perf to finish cleaning up, or there can be errors in the
    // downloaded perf.data file due to the file being grabbed too soon.
    char* command =
      strfmt_or_die("sleep 1 && kill -INT `cat /tmp/perf.pid` && sleep 5");
    querify(ask_system(&g_shepherd_socket, command));
    free(command);
  }
}


static void
handle_command_profile_dump(void)
{
  if (g_profile_with_oprofile)
  {
    // Ask opcontrol to dump the data, if any.
    querify(ask_system(&g_shepherd_socket, "opcontrol --dump"));
  }
}


static void
handle_command_profile_reset(void)
{
  if (g_profile_with_oprofile)
  {
    // Tell opcontrol to clear any pending data.
    querify(ask_system(&g_shepherd_socket, "opcontrol --reset"));
  }
  else
  {
    // Delete the perf events output data file, if any.
    querify(ask_system(&g_shepherd_socket, "rm -rf /tmp/perf.data"));
  }
}


// Help "handle_command_profile_capture" analyze the given "oprofile" path,
// recursing on directories, and collecting into "binaries" the paths to all
// referenced binaries.  Returns true iff "path" is a directory.
//
static bool
handle_command_profile_capture_aux(StringArray* binaries, const char* path)
{
  DIR* dir = opendir(path);
  if (dir == NULL)
    return false;

  // Extract "/xxx" from ".../{root}/xxx/{dep}".
  if (has_suffix(path, "/{dep}"))
  {
    const char* binary = strstr(path, "/{root}/");
    if (binary != NULL)
    {
      binary += strlen("/{root}");
      int len = strlen(binary) - strlen("/{dep}");
      StringArray_append(binaries, strfmt_or_die("%.*s", len, binary));
    }
  }

  bool has_subdirs = false;

  struct dirent* val;
  while ((val = readdir(dir)) != NULL)
  {
    const char* name = val->d_name;

    if (!strcmp(name, ".") || !strcmp(name, ".."))
      continue;

    // Ignore the "{kern}" subdirs, for efficiency.
    if (!strcmp(name, "{kern}"))
      continue;

    // Recurse, counting subdirs in this directory.
    char* next = strfmt_or_die("%s/%s", path, name);
    has_subdirs |= handle_command_profile_capture_aux(binaries, next);
    free(next);
  }

  if (closedir(dir) != 0)
    punt_with_errno("Failure in 'closedir()'");

  if (!has_subdirs)
  {
    // Extract "/yyy" from ".../{dep}/{root}/yyy".
    // Ignore ".../{root}/xxx/{dep}/{root}/xxx" duplicates.
    const char* cg = strstr(path, "/{cg}/{root}/");
    if (cg != NULL)
    {
      cg += strlen("/{cg}/{root}");
      if (StringArray_lookup(binaries, cg) < 0)
        StringArray_append(binaries, strdup_or_die(cg));
    }
    else
    {
      // Extract "/yyy" from ".../{dep}/{root}/yyy".
      // Ignore ".../{root}/xxx/{dep}/{root}/xxx" duplicates.
      const char* binary = strstr(path, "/{dep}/{root}/");
      if (binary != NULL)
      {
        binary += strlen("/{dep}/{root}");
        if (StringArray_lookup(binaries, binary) < 0)
          StringArray_append(binaries, strdup_or_die(binary));
      }
    }
  }

  // Return true if "path" is an "interesting" subdir.
  return (!has_suffix(path, "/{cg}"));
}


// Find the local path for the given binary, if known, and "interesting".
//
// We handle "uploaded" files, and "mounted" directories, and a subset of
// "standard" binaries (i.e. whose paths match paths in "install/tile").
//
// HACK: We only handle standard binaries in "/lib" and "/usr/lib", in
// an attempt to handle system libraries, but not "boring" executables.
//
// ISSUE: If you "upload" a file, and then "mount" any ancestor directory
// of that file, we will incorrectly make use of the "uploaded" file.
//
// ISSUE: If you sneakily place a file into "/lib" or "/usr/lib" (without
// uploading or mounting), which exists in "install/tile", then we will
// make use of the file in "install/tile", instead of ignoring the file.
//
// ISSUE: There is no easy way to "ignore" binaries, so, for example, if
// you mount "/bin", you cannot avoid profiling the "shepherd".
//
// NOTE: This function returns a string which must be freed.
//
static char*
handle_command_profile_capture_local(const char* binary)
{
  // Handle "uploaded" files.
  for (uint i = 0; i < g_symbol_map_paths.size; i += 2)
  {
    char* host_path = StringArray_get(&g_symbol_map_paths, i);
    char* tile_path = StringArray_get(&g_symbol_map_paths, i + 1);
    if (!strcmp(binary, tile_path))
      return strdup_or_die(host_path);
  }

  // Handle "mounted" files.
  for (uint i = 0; i < g_symbol_map_dirs.size; i += 2)
  {
    char* host_dir = StringArray_get(&g_symbol_map_dirs, i);
    char* tile_dir = StringArray_get(&g_symbol_map_dirs, i + 1);
    size_t tile_len = strlen(tile_dir);
    if (has_prefix(binary, tile_dir) && binary[tile_len] == '/')
      return strfmt_or_die("%s%s", host_dir, binary + tile_len);
  }

  // HACK: Handle "standard" files in "/lib" or "/usr/lib".
  if (has_prefix(binary, "/lib") || has_prefix(binary, "/usr/lib"))
  {
    char host_path[PATH_MAX];
    get_install_tile_path(host_path, sizeof(host_path), binary);
    if (file_is_tile_binary(host_path))
      return strdup_or_die(host_path);
  }

  return NULL;
}


// Generates monitor.xml file at specified path.
// If profile_capture_directory is non-null, looks in it for
// binary pathnames, and generates <binary> remote-local mappings
// for them, if any.
static bool
generate_monitor_xml_file(char* monitor_file_path, StringArray* binaries)
{
  int result = false;

  FILE* mf = fopen(monitor_file_path, "w");
  if (mf != NULL)
  {
    char* end = strstr(g_target_info, "</target>");
    fprintf(mf, "%.*s", (int)(end - g_target_info), g_target_info);

    fprintf(mf, "  <binaries>\n");
    for (uint i = 0; i < binaries->size; i++)
    {
      const char* binary = StringArray_get(binaries, i);

      // Get the local (host-side) path mapping, if any.
      char* local = handle_command_profile_capture_local(binary);
      if (local != NULL)
      {
        fprintf(mf, "    <binary local=\"%s\" remote=\"%s\"/>\n",
                local, binary);
        free(local);
      }
    }
    fprintf(mf, "  </binaries>\n");

    fprintf(mf, "%s", end);
    fclose(mf);
    result = true;
  }

  return result;
}


static void
handle_command_profile_capture_oprofile(char* dir)
{
  char* copy = NULL;

  // HACK: Default directory.
  if (!strcmp(dir, ""))
    dir = "~/.tilera/profile";

  // Handle "~/...".
  if (dir[0] == '~' && dir[1] == '/')
  {
    // Handle leading "~/".
    char* home = getenv("HOME");
    if (home == NULL)
    {
      warn("Cannot determine home directory.");
      return;
    }

    dir = copy = strfmt_or_die("%s/%s", home, dir + 2);
  }

  // ISSUE: Do we care if "dir" already exists?

  // Download the directory (synchronously).
  char* dvlo = strfmt_or_die("%s/var/lib/oprofile", dir);
  handle_command_download("/var/lib/oprofile", dvlo);
  free(dvlo);

  // Convert sample files one by one to host abi using opimport.
  opimport_all_samples(dir);

  // Identify all of the remote binaries for which samples exist.
  StringArray binaries;
  StringArray_init(&binaries);
  char* dvlosc = strfmt_or_die("%s/var/lib/oprofile/samples/current", dir);
  handle_command_profile_capture_aux(&binaries, dvlosc);
  free(dvlosc);

  // Create a "monitor.xml" file, by adding info about the "binaries"
  // to the "target info", just before the final "</target>" line.
  char* mp = strfmt_or_die("%s/monitor.xml", dir);
  if (!generate_monitor_xml_file(mp, &binaries))
  {
    warn("Failed to open '%s'.", mp);
  }
  free(mp);

  StringArray_free_and_clear(&binaries);

  free(copy);
}


static void
handle_command_profile_capture_perf_events(char* dir)
{
  char* copy = NULL;

  // HACK: Default directory.
  if (!strcmp(dir, ""))
    dir = "~/.tilera/profile";

  // Handle "~/...".
  if (dir[0] == '~' && dir[1] == '/')
  {
    // Handle leading "~/".
    char* home = getenv("HOME");
    if (home == NULL)
    {
      warn("Cannot determine home directory.");
      return;
    }

    dir = copy = strfmt_or_die("%s/%s", home, dir + 2);
  }

  // ISSUE: Do we care if "dir" already exists?

  // Download the perf data file (synchronously).
  char* dvlo = strfmt_or_die("%s/perf.data", dir);
  handle_command_download("/tmp/perf.data", dvlo);
  free(dvlo);

  // Download /proc/kallsyms, since this is needed by
  // tile-perf2xml when processing kernel symbols.
  dvlo = strfmt_or_die("%s/kallsyms", dir);
  handle_command_download("/proc/kallsyms", dvlo);
  free(dvlo);

  // When generating a monitor.xml file for perf,
  // we'll just parrot back any symbol-map mappings we've been told.
  StringArray binaries;
  StringArray_init(&binaries);
  for (uint i = 0; i < g_symbol_map_paths.size; i += 2)
  {
    char* tile_path = StringArray_get(&g_symbol_map_paths, i + 1);
    StringArray_append(&binaries, tile_path);
  }

  // Create a "monitor.xml" file, by adding info about the "binaries"
  // to the "target info", just before the final "</target>" line.
  char* mp = strfmt_or_die("%s/monitor.xml", dir);
  if (!generate_monitor_xml_file(mp, &binaries))
  {
    warn("Failed to open '%s'.", mp);
  }
  free(mp);

  StringArray_free_and_clear(&binaries);

  free(copy);
}


static void
handle_command_profile_capture(char* dir)
{
  // Track whether user has done "profile_start" but has not done
  // "profile_capture".  Even just the attempt to use profile_capture
  // is sufficient to clear this flag, since it means the user didn't
  // forget.
  g_profile_start_without_capture_warning = false;

  if (g_profile_with_oprofile)
    handle_command_profile_capture_oprofile(dir);
  else
    handle_command_profile_capture_perf_events(dir);
}


static void
handle_command_generate_monitor_xml(char* path)
{
  // When generating an "on demand" monitor.xml file,
  // we'll just parrot back any symbol-map mappings we've been told.
  StringArray binaries;
  StringArray_init(&binaries);
  for (uint i = 0; i < g_symbol_map_paths.size; i += 2)
  {
    char* tile_path = StringArray_get(&g_symbol_map_paths, i + 1);
    StringArray_append(&binaries, tile_path);
  }

  // Attempt to generate the monitor.xml file.
  if (!generate_monitor_xml_file(path, &binaries))
  {
    warn("Failed to open '%s'.", path);
  }

  StringArray_free_and_clear(&binaries);
}


static void
handle_command_profile_analyze_oprofile(char* dir)
{
  char exe[PATH_MAX];
  get_install_path(exe, sizeof(exe), "/bin/tile-op2xml");

  if (access(exe, X_OK) != 0)
  {
    warn("Cannot execute '%s'.", exe);
    return;
  }

  uint count = 0;

  DIR* scanner = opendir(dir);
  if (scanner == NULL)
  {
    warn_with_errno("Cannot analyze directory '%s'", dir);
    return;
  }

  char profile_pathname[PATH_MAX];
  snprintf(profile_pathname, PATH_MAX, "%s/profile.xml", dir);

  Buffer command;
  Buffer_init(&command);
  Buffer_printf(&command, "%s -o '%s'", exe, profile_pathname);

  Buffer path;
  Buffer_init(&path);

  struct dirent* val;
  while ((val = readdir(scanner)) != NULL)
  {
    const char* name = val->d_name;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
      continue;

    // Try the subdir.
    Buffer_printf(&path, "%s/%s/monitor.xml", dir, name);
    if (close(open((char*)path.data, O_RDONLY)) == 0)
    {
      count++;
      Buffer_printf(&command, " '%s/%s'", dir, name);
    }

    Buffer_clear(&path);
  }

  if (closedir(scanner) != 0)
    punt_with_errno("Failure in 'closedir()'");

  // Try "dir" itself.
  Buffer_printf(&path, "%s/monitor.xml", dir);
  if (close(open((char*)path.data, O_RDONLY)) == 0)
  {
    if (count != 0)
      warn("Analyzing '%s' but also %u of its subdirs.", dir, count);

    Buffer_printf(&command, " '%s'", dir);
  }

  Buffer_destroy(&path);

  spew(1, "Analyzing profile data using: %s", (char*)(command.data));

  if (system((char*)(command.data)) != 0)
    warn("Failure in 'system(\"%s\")'.", (char*)(command.data));

  if (Pollable_valid(&g_ide_socket))
  {
    // report location of new profile.xml to IDE, if any
    do_profile_file_generated(&g_ide_socket, profile_pathname);
  }

  Buffer_destroy(&command);
}


static void
handle_command_profile_analyze_perf_events(char* dir)
{
  char exe[PATH_MAX];
  get_install_path(exe, sizeof(exe), "/bin/tile-perf2xml");

  if (access(exe, X_OK) != 0)
  {
    warn("Cannot execute '%s'.", exe);
    return;
  }

  uint count = 0;

  DIR* scanner = opendir(dir);
  if (scanner == NULL)
  {
    warn_with_errno("Cannot analyze directory '%s'", dir);
    return;
  }

  char profile_pathname[PATH_MAX];
  snprintf(profile_pathname, PATH_MAX, "%s/profile.xml", dir);

  Buffer command;
  Buffer_init(&command);

  Buffer_printf(&command, "%s -o '%s'", exe, profile_pathname);

  Buffer path;
  Buffer_init(&path);

  struct dirent* val;
  while ((val = readdir(scanner)) != NULL)
  {
    const char* name = val->d_name;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
      continue;

    // Try the subdir.
    Buffer_printf(&path, "%s/%s/monitor.xml", dir, name);
    if (close(open((char*)path.data, O_RDONLY)) == 0)
    {
      count++;
      Buffer_printf(&command, " '%s/%s/perf.data'", dir, name);
    }

    Buffer_clear(&path);
  }

  if (closedir(scanner) != 0)
    punt_with_errno("Failure in 'closedir()'");

  // Try "dir" itself.
  Buffer_printf(&path, "%s/monitor.xml", dir);
  if (close(open((char*)path.data, O_RDONLY)) == 0)
  {
    if (count != 0)
      warn("Analyzing '%s' but also %u of its subdirs.", dir, count);

    Buffer_printf(&command, " '%s/perf.data'", dir);
  }

  Buffer_destroy(&path);

  spew(1, "Analyzing profile data using: %s", (char*)(command.data));

  if (system((char*)(command.data)) != 0)
    warn("Failure in 'system(\"%s\")'.", (char*)(command.data));

  if (Pollable_valid(&g_ide_socket))
  {
    // report location of new profile.xml to IDE, if any
    do_profile_file_generated(&g_ide_socket, profile_pathname);
  }

  Buffer_destroy(&command);
}


static void
handle_command_profile_analyze(char* dir)
{
  if (g_profile_with_oprofile)
    handle_command_profile_analyze_oprofile(dir);
  else
    handle_command_profile_analyze_perf_events(dir);
}


static void
handle_command_system(const char* command)
{
  do_system(&g_shepherd_socket, command);
}


static void
handle_command_local_system(const char* command)
{
  // try to invoke local command string, and wait for it to return
  if (system(command) != 0)
    warn("Failure in 'system(\"%s\")'.", command);
}


static void
handle_command_forward(const char* arg1, const char* arg2)
{
  uint16_t tile_port;
  if (!parse_port_or_warn(&tile_port, arg1, true))
    return;

  uint16_t host_port;
  if (!parse_port_or_warn(&host_port, arg2, false))
    return;

  do_forward_listen(&g_shepherd_socket, tile_port, host_port);
}


static void
handle_command_tunnel(const char* arg1, const char* arg2)
{
  uint16_t host_port;
  if (!parse_port_or_warn(&host_port, arg1, true))
    return;

  uint16_t tile_port;
  if (!parse_port_or_warn(&tile_port, arg2, false))
    return;

  bool ephemeral = (host_port == 0);
  int listener_fd = simple_listen(&host_port, 1);
  if (ephemeral)
    note("Tunneling from host port %u to tile port %u.", host_port, tile_port);

  Pollable* pollable = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(pollable, "Tunnel listener [%u to %u]", host_port, tile_port);
  pollable->info = (void*)(uintptr_t)tile_port;
  Pollable_open(pollable, listener_fd, handle_tunnel_listener);
}


static void
handle_command_hack_child(StringArray* args)
{
  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Handle Child.

    // HACK: Ensure a terminator.
    StringArray_append(args, NULL);

    // Attempt to "exec()".
    (void)execvp(args->data[0], args->data);
    punt_with_errno("Failure in 'execvp(%s)'", args->data[0]);
  }

  // Handle Parent.

  Child* child = (Child*)calloc_or_die(1, sizeof(*child));

  child->pid = pid;

  Array_append(&g_children, child);
}


static void
handle_command_hack_echo(const char* arg1)
{
  uint16_t port;
  if (!parse_port_or_warn(&port, arg1, false))
    return;

  do_echo_port(&g_shepherd_socket, port);
}


// Forward Declaration.
static void
handle_packet(RPC rpc);


static void
handle_hack_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_packet) < 0)
  {
    warn("Lost connection to hack socket!");
  }
}


static void
handle_hack_listener(Pollable* pollable)
{
  warn("Accepting hack connection.");

  int fd = simple_accept(pollable->fd);

  Pollable_close(pollable);

  //--free(pollable);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  Pollable* socket = calloc(1, sizeof(Pollable));
  Pollable_init(socket, "Hacky Socket");
  Pollable_open(socket, fd, handle_hack_socket);
}


static void
handle_command_hack_port(const char* arg1)
{
  uint16_t port;
  if (!parse_port_or_warn(&port, arg1, false))
    return;
  Pollable* listener = calloc(1, sizeof(Pollable));
  Pollable_init(listener, "Hacky listener");
  int fd = simple_listen(&port, 1);
  warn("Listening on port %u.", port);
  Pollable_open(listener, fd, handle_hack_listener);
}


static void
handle_command_hacky_hardwall(void)
{
  do_hacky_hardwall(&g_shepherd_socket);
}


static void
handle_command_count_cycles(void)
{
  do_count_cycles(&g_shepherd_socket);
}



static const char handle_command_help_text[] =
"\n"
"Usage: tile-monitor [options] [commands] [-- <exe> [<arg> ...]]\n"
"\n"
"Use '--help-options' to see a list of the legal options.\n"
"Use '--help-commands' to see a list of the legal commands.\n"
"Use '--help-examples' to see some simple examples.\n"
"Use '--help-stdin' to see some info about interactive mode.\n"
"Use '--help-all' to see all of the available help.\n"
"\n"
"The special '--' command-line syntax is basically shorthand for:\n"
"  --run -+- <exe> <arg> ... -+- --quit.\n"
"\n"
"Note that '-+-', which is mentioned in the descriptions of commands which\n"
"take a variable number of arguments, can be replaced by any fixed string\n"
"starting with a dash, including '-' itself, or '-$$-', so you can pick a\n"
"string which will not match any of the actual arguments.\n"
"\n"
"In the absense of any options such as '--dev', '--image', or '--config',\n"
"'tile-dev' will be used to identify the 'default' system (e.g. '/pci0')\n"
"to be booted (and used).\n"
"\n";

static void
handle_command_help(void)
{
  printf("%s", handle_command_help_text);
  fflush(stdout);
}


// Forward declaration.
static void
handle_command_help_commands(void);


static const char handle_command_help_examples_text[] =
"\n"
#ifdef __tile__
"To reboot the current system, after installing a bootrom:\n"
"  tile-monitor --self --reflash\n"
"\n"
#endif
"To boot the default system, and then handle interactive commands:\n"
"  tile-monitor\n"
"\n"
"To boot the second PCIe card, and then handle interactive commands:\n"
"  tile-monitor --dev /pci1\n"
"\n"
"To boot a remote networked system, after installing a new bootrom:\n"
"  tile-monitor --reflash --net <hostname> [...]\n"
"\n"
#ifndef __tile__
"To boot the simulator from a standard image file:\n"
"  tile-monitor --image tile64 [...]\n"
"\n"
"To boot the simulator, and create a new image file:\n"
"  tile-monitor --config 3x3 [...] --create-image 3x3.image\n"
"\n"
"To boot the simulator from that created image file:\n"
"  tile-monitor --image-file 3x3.image [...]\n"
"\n"
#endif
"To boot the default system, run a simple program, and then quit:\n"
"  tile-monitor -- ls\n"
"\n"
"To reconnect to the default system, and then handle interactive commands:\n"
"  tile-monitor --resume\n"
"\n"
"To reconnect to the default system, download a directory, and then quit:\n"
"  tile-monitor --resume --download <tile-dir> <host-dir> --quit\n"
"\n"
"To upload some files and/or directories:\n"
"  [...] --upload <host-path> <tile-path> [...]\n"
"\n"
"To run two programs sequentially:\n"
"  [...] --run -+- mkdir x -+- --run -+- mkdir x/y -+- [...]\n"
"\n"
"To run two programs simultaneously:\n"
"  [...] --launch - sleep 5 - --launch - sleep 3 - --wait [...]\n"
"\n";

static void
handle_command_help_examples(void)
{
  printf("%s", handle_command_help_examples_text);
  fflush(stdout);
}


static const char handle_command_help_options_text[] =
"\n"
"Basic Options:\n"
"  --dev <name> = Boot (and use) a specific system (see 'tile-dev').\n"
"  --hvc <file> = Process primary hypervisor configuration file.\n"
"  --hvd <var>[=<value>] = Define a variable for hvc files.\n"
"  --hvh <file> = Process secondary hypervisor configuration file.\n"
"  --hvi <dir> = Add include directory for hvc files.\n"
"  --hvx <arg> = Extend definition of the 'XARGS' hvc variable.\n"
"  --net <addr> = Boot (and use) a specific networked system.\n"
"  --resume = Skip the boot step.\n"
"  --reflash = Reboot a networked system with a new bootrom.\n"
"  --rootfs <dev> = Use the specified device as the root filesystem.\n"
"  --ssh [user@]<addr> = Connect to a specific networked system over ssh.\n"
#ifdef __tile__
"  --self = Reboot the current system (after installing a bootrom).\n"
#endif
"\n"
#ifndef __tile__
"Simulator Options:\n"
"  --config <type> = Boot the simulator (WxH, tile64, etc).\n"
"  --create-image <path> = Create an image file and exit.\n"
"  --functional = Pass '--functional' to the simulator.\n"
"  --image <name> = Use the image file with the given name.\n"
"  --image-file <path> = Use the image file with the given path.\n"
"  --sim-arg <arg> = Add an extra simulator argument.\n"
"  --sim-args -+- <arg> ... -+- = Add some extra simulator arguments.\n"
"\n"
#endif
"Advanced Options:\n"
"  --batch-mode = Append final '--wait' and '--quit' commands.\n"
"  --bme <name>[=<path>] = Use the specified BME executable.\n"
"  --boot-dir <path> = Get various boot files from the given directory.\n"
"  --boot-only = Exit after booting, without contacting shepherd.\n"
"  --bootrom-file <path> = Use the bootrom with the given path.\n"
#if TILE_CHIP >= 10
"  --classifier <path> = Use the specified 'classifier' file.\n"
#endif
"  --console = Use the 'standard' console for the platform.\n"
"  --try-console = Like --console, but just warn if not available.\n"
"  --console-device <device> = Use the given console device.\n"
"  --console-out <path> = Write console output to the given file.\n"
"  --try-console-out <path> = As above, but Warn if console not available.\n"
"  --create-bootrom <path> = Create a bootrom file and exit.\n"
"  --create-hvc <path> = Create a preprocessed hvc file and exit.\n"
"  --external-command-port <nnnn> = Open a port for remote commands.\n"
"  --gdb-port <port> = Assign gdb ports starting from this value.\n"
"  --boot-timeout <secs> = How long to wait for the bootstream to be "
"consumed.\n"
"  --greet-timeout <secs> = How long to wait for shepherd to respond.\n"
"  --hv-bin-dir <dir> = Use the HV binary files in the given directory.\n"
"  --initramfs <cpio_file> = Use the specified 'initramfs' file.\n"
"  --local-gdb = Create local gdb sessions for process debugging.\n"
"  --mkboot-args -+- <arg> ... -+- = Extra args for 'tile-mkboot'.\n"
#if TILE_CHIP >= 10
"  --no-classifier = Do not use any 'classifier' file.\n"
#endif
"  --no-dev = Do not use any device whatsoever.\n"
"  --no-hv-bin-dir = Do not use any 'hv' binary files.\n"
"  --no-initramfs = Suppress use of the default initramfs file.\n"
"  --no-readline = Forbid use of 'readline' in interactive mode.\n"
"  --no-rtc = Do not specify '--rtc' during PCI/USB boot.\n"
"  --no-vmlinux = Do not use any 'vmlinux' file.\n"
#ifndef __tile__
#if TILE_CHIP < 10
"  --pci-link <number> = Use a specific PCI link.\n"
"  --pci-stream <number> = Use a specific PCI stream.\n"
#endif
#endif
"  --peer-connect <what> = Connect to a 'peer' monitor.\n"
"  --peer-listen <what> = Listen for a 'peer' monitor.\n"
"  --readline = Force use of 'readline' in interactive mode.\n"
"  --shepherd-port <port> = Use a non-standard shepherd port.\n"
#ifndef __tile__
"  --sim-prefix <arg> = Add an extra simulator prefix.\n"
#endif
"  --tag-console = Add a prefix to process stdout/stderr.\n"
"  --verbose = Increase monitor verbosity.\n"
"  --vmlinux <path> = Use the specified 'vmlinux' file.\n"
"\n";

static void
handle_command_help_options(void)
{
  printf("%s", handle_command_help_options_text);
  fflush(stdout);
}


static const char handle_command_help_stdin_text[] =
"\n"
"Normally, once all command-line options and commands have been processed,\n"
"tile-monitor enters interactive mode, in which a special 'Command:' prompt\n"
"will be shown, at which you can use standard emacs-mode editing and history\n"
"recall mechanisms to enter lines on stdin.\n"
"\n"
"Normally, these lines will be processed as interactive commands, which are\n"
"just like command-line commands, but they drop any leading '--' prefix, and\n"
"any '-+-' delimiters.  Use 'help-commands' for a list of legal commands.\n"
"\n"
"However, lines starting with '#' are treated specially:\n"
"  #p       Use virtual console for next process.\n"
"  #pPID    Use virtual console for process PID.\n"
"  #g       Use virtual console for next local gdb.\n"
"  #gPID    Use virtual console for local gdb PID.\n"
"  #c       Use virtual console for the console.\n"
"  #!       Close the current virtual console.\n"
"  #.       Stop using a virtual console.\n"
"  #?       List all of the known processes.\n"
"  ##...    Handle '#...' as raw input.\n"
"  #/...    Handle '...' as a raw command.\n"
"  #+...    Prefix future lines with '...'.\n"
"\n"
"While using a virtual console, the prompt will change to indicate that\n"
"lines will be sent to the specified virtual console.\n"
"\n";

static void
handle_command_help_stdin(void)
{
  printf("%s", handle_command_help_stdin_text);
  fflush(stdout);
}


static void
handle_command_help_all(void)
{
  handle_command_help();

  printf("=== help-options ===\n");
  handle_command_help_options();

  printf("=== help-commands ===\n");
  handle_command_help_commands();

  printf("=== help-examples ===\n");
  handle_command_help_examples();

  printf("=== help-stdin ===\n");
  handle_command_help_stdin();
}


// Union of all supported handling function types.
//
typedef union {

  void (*f0)(void);

  void (*f1)(char*);
  void (*f1c)(const char*);

  void (*f2)(char*, char*);
  void (*f2c)(const char*, const char*);

  void (*fn)(StringArray*);

} command_handler;


// Information about a specific command.
//
struct command_info
{
  // The textual command.
  const char* cmd;

  // The suffix, for usage.
  const char* suffix;

  // The purpose, for help.
  const char* purpose;

  // The handler.
  command_handler handler;

  // The number of arguments (with -N meaning "at least N - 1").
  int16_t num;

  // Whether this command requires greeting the shepherd.
  bool greet;
};


// We encode "section headers" using an empty "cmd", and a non-empty
// "purpose" as a section description, or an empty "purpose" to hide
// undocumented commands.
//
#define SECTION(STR) { "", "", (STR), { NULL }, 0, false }

// The standard commands.
//
static struct command_info g_commands[] = {

  SECTION("Help Commands"),

  { "help", "",
    "Display some basic help.",
    { .f0 = handle_command_help }, 0, false },

  { "help-all", "",
    "Display all of the help.",
    { .f0 = handle_command_help_all }, 0, false },

  { "help-commands", "",
    "Describe the legal commands.",
    { .f0 = handle_command_help_commands }, 0, false },

  { "help-examples", "",
    "Display some basic examples.",
    { .f0 = handle_command_help_examples }, 0, false },

  { "help-options", "",
    "Describe the legal options.",
    { .f0 = handle_command_help_options }, 0, false },

  { "help-stdin", "",
    "Describe interactive mode.",
    { .f0 = handle_command_help_stdin }, 0, false },


  SECTION("Basic Commands"),

  { "cd", " <dir>",
    "Set the shepherd's working directory.",
    { .f1c = handle_command_cd }, 1, true },

  { "debug-on-crash", "",
    "Debug all processes which might otherwise crash.",
    { .f0 = handle_command_debug_on_crash }, 0, true },

  { "env", " <var>[=<value>]",
    "Modify the environment used by 'run'.",
    { .f1c = handle_command_env }, 1, true },

  { "launch", " <executable> [<arg> ...]",
    "Launch an application.",
    { .fn = handle_command_launch }, -2, true },

  { "quit", "",
    "Shut down everything, and exit when done.",
    { .f0 = handle_command_quit }, 0, false },

  { "run", " <executable> [<arg> ...]",
    "Run an application.",
    { .fn = handle_command_run }, -2, true },

  { "tile", " <tile>",
    "Modify the set of tiles to be used by 'run'.",
    { .f1c = handle_command_tile }, 1, true },

  { "tiles", " [<tile> ...]",
    "Modify the set of tiles to be used by 'run'.",
    { .fn = handle_command_tiles }, -1, true },

  { "wait", "",
    "Wait for all launched apps to exit.",
    { .f0 = handle_command_wait }, 0, true },


  SECTION("File Commands"),

  { "delete", " <path>",
    "Delete a file (or symlink).",
    { .f1c = handle_command_delete }, 1, true },

  { "download", " <tile-path> <host-path>",
    "Download a file/directory.",
    { .f2c = handle_command_download }, 2, true },

  { "download-same", " <path>",
    "Download '<path>' to '<path>'.",
    { .f1c = handle_command_download_same }, 1, true },

  { "here", "",
    "Export, and then cd to, the current working directory.",
    { .f0 = handle_command_here }, 0, true },

  { "mkdir", " <dir>",
    "Create a directory (and its ancestors).",
    { .f1c = handle_command_mkdir }, 1, true },

  { "mount", " <host-dir> <tile-dir>",
    "Mount a host directory remotely via FUSE.",
    { .f2c = handle_command_mount }, 2, true },

  { "mount-same", " <dir>",
    "Mount '<dir>' as '<dir>' via FUSE.",
    { .f1c = handle_command_mount_same }, 1, true },

#ifndef __tile__
  { "mount-tile", " <dir>",
    "Mount '.../tile<dir>' as '<dir>' via FUSE.",
    { .f1c = handle_command_mount_tile }, 1, true },
#endif

  { "rename", " <path> <newpath>",
    "Rename a directory or file or symlink.",
    { .f2c = handle_command_rename }, 2, true },

  { "rmdir", " <dir>",
    "Remove an empty directory.",
    { .f1c = handle_command_rmdir }, 1, true },

#ifndef __tile__
  { "root", "",
    "Mount/upload some useful '.../tile/' subdirs.",
    { .f0 = handle_command_root }, 0, true },
#endif

  { "unlink", " <path>",
    "Delete a file (or symlink).",
    { .f1c = handle_command_unlink }, 1, true },

  { "upload", " <host-path> <tile-path>",
    "Upload a file/directory.",
    { .f2c = handle_command_upload }, 2, true },

  { "upload-same", " <path>",
    "Upload '<path>' to '<path>'.",
    { .f1c = handle_command_upload_same }, 1, true },

#ifndef __tile__
  { "upload-tile", " <path>",
    "Upload '.../tile<path>' to '<path>'.",
    { .f1c = handle_command_upload_tile }, 1, true },

  { "upload-tile-libs", " <name>",
    "Upload all shared libs matching '<name>'.",
    { .f1c = handle_command_upload_tile_libs }, 1, true },
#endif


  SECTION("Advanced Commands"),

  { "attach", " <pid>",
    "Debug an existing process.",
    { .f1c = handle_command_attach }, 1, true },

#ifndef __tile__
  { "bm-debug-attach-tile", " <tile>",
    "Attach a BM debugger to the given tile.",
    { .f1 = handle_command_bm_debug_attach_tile }, 1, false },

  { "bm-debug-on-panic", "",
    "Attach a BM debugger whenever a 'panic' occurs.",
    { .f0 = handle_command_bm_debug_on_panic }, 0, false },
#endif

  { "debug", " <executable> [<arg> ...]",
    "Like 'launch', but with debugging.",
    { .fn = handle_command_debug }, -2, true },

  { "debug-exe", " <exe>",
    "Specify another executable to be debugged.",
    { .f1c = handle_command_debug_executable }, 1, true },

  { "debug-exes", " [<exe> ...]",
    "Specify the executables to be debugged.",
    { .fn = handle_command_debug_executables }, -1, true },

  { "debug-next", "",
    "Debug the next process and its children.",
    { .f0 = handle_command_debug_next }, 0, true },

  { "debug-tile", " <tile>",
    "Debug all processes locked to the given tile.",
    { .f1c = handle_command_debug_tile }, 1, true },

  { "dump", "",
    "Dump out some shepherd state.",
    { .f0 = handle_command_dump }, 0, true },

  { "forbid-tiles", " <tiles>",
    "Forbid the use of the given tiles.",
    { .f1 = handle_command_forbid_tiles }, 1, true },

  { "forward", " <tile-port> <host-port>",
    "Forward connections from tile to host.",
    { .f2c = handle_command_forward }, 2, true },

  { "gdb-command", " <cmd>",
    "Add an extra command for new 'gdb' sessions.",
    { .f1 = handle_command_gdb_command }, 1, false },

  { "huge-pages", " <N>|<A,B,C,D>",
    "Request the given number(s) of huge pages.",
    { .f1 = handle_command_huge_pages }, 1, true },

  { "local-system", " <command>",
    "Ask the monitor to perform a system call.",
    { .f1c = handle_command_local_system }, 1, true },

  { "peer-barrier", "",
    "Wait for 'peer' monitor to do a peer-barrier.",
    { .f0 = handle_command_peer_barrier }, 0, false },

  { "ping", "",
    "Ping the shepherd.",
    { .f0 = handle_command_ping }, 0, true },

  { "rtc", "",
    "Synchronize the remote clock with the host clock.",
    { .f0 = handle_command_rtc }, 0, true },

  { "shepherd-tiles", " <tiles>",
    "Set the shepherd affinity to the given tiles.",
    { .f1 = handle_command_shepherd_tiles }, 1, true },

  { "shepherd-verbosity", " <verbosity>",
    "Set the shepherd verbosity.",
    { .f1c = handle_command_shepherd_verbosity }, 1, true },

#ifndef __tile__
  { "sim", " <cmd> [<arg> ...]",
    "Send a command to the simulator.",
    { .fn = handle_command_sim }, -1, false },
#endif

  { "spread-threads", "",
    "Spread threads across available cpus.",
    { .f0 = handle_command_spread_threads }, 0, true },

  { "symbol-map", " <host-file> <tile-file>" ,
    "Specify a symbol mapping.",
    { .f2c = handle_command_symbol_map }, 2, false },

  { "system", " <command>",
    "Ask the shepherd to perform a system call.",
    { .f1c = handle_command_system }, 1, true },

  { "tunnel", " <host-port> <tile-port>",
    "Tunnel connections from host to tile.",
    { .f2c = handle_command_tunnel }, 2, true },

  { "verbosity", " <verbosity>",
    "Set the monitor verbosity.",
    { .f1c = handle_command_verbosity }, 1, false },


  SECTION("Profiling Commands"),

  { "profile-tool", " [ oprofile | perf ]",
    "Selects profiling tool for subsequent profile commands.",
    { .f1c = handle_command_profile_tool }, 1, true },

  { "profile-init", "",
    "Upload some profiling support files.",
    { .f0 = handle_command_profile_init }, 0, true },

  { "profile-kernel", "",
    "Profile with the default 'vmlinux' kernel.",
    { .f0 = handle_command_profile_kernel }, 0, true },

  { "profile-vmlinux", " <vmlinux>",
    "Profile with the specified kernel.",
    { .f1c = handle_command_profile_vmlinux }, 1, true },


  { "profile-events", " <flags>",
    "Specify some event flags for 'profile-start'.",
    { .f1c = handle_command_profile_events }, 1, true },

  { "profile-flags", " <flags>",
    "Specify some generic flags for 'profile-start'.",
    { .f1c = handle_command_profile_flags }, 1, true },


  { "profile-reset", "",
    "Reset the profiling data.",
    { .f0 = handle_command_profile_reset }, 0, true },


  { "profile-start-daemon", "",
    "Start the profiling daemon.",
    { .f0 = handle_command_profile_start_daemon }, 0, true },

  { "profile-start", "",
    "Start collecting profiling data.",
    { .f0 = handle_command_profile_start }, 0, true },


  { "profile-stop", "",
    "Stop collecting profiling data.",
    { .f0 = handle_command_profile_stop }, 0, true },

  { "profile-dump", "",
    "Dump the profiling data.",
    { .f0 = handle_command_profile_dump }, 0, true },

  { "profile-shutdown", "",
    "Shut down the profiling daemon.",
    { .f0 = handle_command_profile_shutdown }, 0, true },


  { "profile-capture", " <dir>",
    "Download the current profiling data into '<dir>'.",
    { .f1 = handle_command_profile_capture }, 1, true },

  { "profile-analyze", " <dir>",
    "Analyze the profiling data downloaded into '<dir>'.",
    { .f1 = handle_command_profile_analyze }, 1, false },


  { "generate-monitor-xml", " <path>",
    "Generates a monitor.xml metadata file to the specified '<path>'.",
    { .f1 = handle_command_generate_monitor_xml }, 1, true },



  // HACK: Undocumented commands start here.

  SECTION(""),

  { "count-cycles", "",
    "Count cycles from next launch until idle.",
    { .f0 = handle_command_count_cycles }, 0, true },

  { "download-async", " <tile-path> <host-path>",
    "Download in the background.",
    { .f2c = handle_command_download_async }, 2, true },

  { "export", " <dir>",
    "Deprecated alias for 'mount-same'.",
    { .f1c = handle_command_mount_same }, 1, true },

  { "hack-child", " <exe> [<arg> ...]",
    "Create a local child.",
    { .fn = handle_command_hack_child }, -1, false },

  { "hack-echo", " <port>",
    "Create a remote 'echo' server.",
    { .f1c = handle_command_hack_echo }, 1, true },

  { "hack-port", " <port>",
    "Accept an RPC connection.",
    { .f1c = handle_command_hack_port }, 1, false },

  { "hacky-hardwall", "",
    "Pass a hacky hardwall to all processes.",
    { .f0 = handle_command_hacky_hardwall }, 0, true },

  { "kill-everyone", "",
    "Kill all launched processes.",
    { .f0 = handle_command_kill_everyone }, 0, true },

  { "peer-quit", "",
    "Deprecated in 3.1.",
    { .f0 = handle_command_peer_quit }, 0, false },

  { "profile-extra", " <extra>",
    "DEPRECATED!",
    { .f1c = handle_command_profile_extra }, 1, true },

#ifndef __tile__
  { "raw-wait", "",
    "Wait for the simulator to exit.",
    { .f0 = handle_command_raw_wait }, 0, false },

  { "set-functional", " <flag>",
    "Set the 'functional' flag for the simulator.",
    { .f1c = handle_command_set_functional }, 1, true },
#endif

  { "symlink", " <path> <target>",
    "Create a symlink from <path> to <target>.",
    { .f2c = handle_command_symlink }, 2, true },

  { "unlimit-fds", "",
    "Allow unlimited fds for spawned processes.",
    { .f0 = handle_command_unlimit_fds }, 0, true },

  { "upload-async", " <host-path> <tile-path>",
    "Upload in the background.",
    { .f2c = handle_command_upload_async }, 2, true },

  { "upload-cancel", "",
    "Cancel pending asynchronous uploads.",
    { .f0 = handle_command_upload_cancel }, 0, true },

  { "upload-wait", "",
    "Wait for asynchronous uploads to complete.",
    { .f0 = handle_command_upload_wait }, 0, true },

  { "server", "",
    "Handle events, like a server.",
    { .f0 = handle_command_server }, 0, false },

  { "watch", " <pid>",
    "Watch an existing process.",
    { .f1c = handle_command_watch }, 1, true },

  SECTION("")
};

#undef SECTION


static void
handle_command_help_commands(void)
{
  for (int k = 0; k < NELEM(g_commands); k++)
  {
    struct command_info* ci = &g_commands[k];

    // Section header.
    if (!strcmp(ci->cmd, ""))
    {
      printf("\n");

      // HACK: Undocumented commands.
      if (!strcmp(ci->purpose, ""))
        break;

      printf("%s:\n", ci->purpose);

      continue;
    }

    const char* intro = (!g_running) ? "--" : "";
    const char* delim = (!g_running && ci->num < 0) ? " -+-" : "";

    printf("  %s%s%s%s%s = %s\n",
           intro, ci->cmd, delim, ci->suffix, delim, ci->purpose);
  }

  fflush(stdout);
}


// Handle a "command".
//
static void
handle_command(StringArray* args)
{
  // Ignore empty commands and empty opcodes.
  if (args->size == 0 || !strcmp(args->data[0], ""))
    return;

  char* cmd = args->data[0];

  for (int k = 0; k < NELEM(g_commands); k++)
  {
    struct command_info* ci = &g_commands[k];
    if (!strcmp(ci->cmd, cmd))
    {
      char* cmd = args->data[0];

      // Verify argument count.
      bool varargs = (ci->num < 0);
      if (varargs ? (args->size < -ci->num) : (args->size != ci->num + 1))
      {
        warn("Usage: '%s%s'.", cmd, ci->suffix);
        return;
      }

      if (ci->greet)
        greet_shepherd_if_needed();

      if (ci->num < 0)
      {
        StringArray_excise(args, 0, 1);
        (*ci->handler.fn)(args);
      }
      else
      {
        switch (ci->num)
        {
        case 0:
          (*ci->handler.f0)();
          break;
        case 1:
          (*ci->handler.f1)(args->data[1]);
          break;
        case 2:
          (*ci->handler.f2)(args->data[1], args->data[2]);
          break;
        }
      }

      return;
    }
  }

  warn("Unknown command '%s'. Try 'help'.", cmd);
}


static void
handle_string_aux(char* str)
{
  spew(2, "Processing command '%s'.", str);

  StringArray args;
  StringArray_init(&args);

  int n = tokenize(&args, str);

  if (n < 0)
    warn("Handling badly formed command.");

  for (int i = 0; i < args.size; i++)
    spew(4, "Token %d = '%s'.", i, args.data[i]);

  handle_command(&args);

  StringArray_destroy(&args);
}


static void
handle_string(const char* str)
{
  char* cpy = strdup_or_die(str);
  handle_string_aux(cpy);
  free(cpy);
}



// Handle a normal line on "stdin".
//
static void
handle_stdin_line_normal(char* str, int len)
{
  if (g_stdin_mode == 'c')
  {
    // Terminate any partial prompt.
    g_console_stream.partial = false;

    str[len] = '\n';
    // ISSUE: What if "g_console" is not valid?
    Pollable_write(&g_console, str, len + 1);
    str[len] = '\0';
  }
  else if (g_stdin_mode == 'p')
  {
    Process* process = find_process_by_pid(g_stdin_arg);

    // Paranoia.
    if (process == NULL)
      return;

    // HACK: Terminate any partial prompt.
    process->stdout_stream.partial = false;

    str[len] = '\n';
    do_provide_stdin(&g_shepherd_socket,
                     process->pid, (const uint8_t*)str, len + 1);
    str[len] = '\0';
  }
  else if (g_stdin_mode == 'g' || g_stdin_mode == 't')
  {
    Debugger* debugger = find_debugger_for_stdin();

    // Paranoia.
    if (debugger == NULL)
      return;

    if (debugger->local_gdb_expecting_prompt)
    {
      warn("Previous debugger command is still in progress.");
      warn("Use Ctrl+C to interrupt the debugger itself.");
      return;
    }

    // Send the command.
    char* cmd = strfmt_or_die("%.*s\n", len, str);
    send_command_to_local_gdb(debugger, cmd);
    free(cmd);

    // Wait for the "reply".  This must not call "handle_events()",
    // because that might cause "debugger" to get freed.  The user
    // can use one "Ctrl+C" to stop waiting, showing a prompt, and
    // then a second one to send a SIGINT to the underlying gdb.
    g_waiting = true;
    while (g_waiting && debugger->local_gdb_expecting_prompt)
      dispatch_events(-1);
    if (debugger->local_gdb_expecting_prompt)
    {
      warn("Debugger command will continue asynchronously.");
      warn("Use Ctrl+C to interrupt the debugger itself.");
    }
    g_waiting = false;
  }
  else if (*str != '\0')
  {
    if (g_stdin_prefix != NULL)
    {
      char* str2 = strfmt_or_die("%s %s", g_stdin_prefix, str);
      handle_string_aux(str2);
      free(str2);
    }
    else
    {
      handle_string_aux(str);
    }
  }
}


// Handle a special "#!" line on "stdin".
//
static void
handle_stdin_line_close(char* str)
{
  if (g_stdin_mode == 'c')
  {
    // ISSUE: This is not very useful.
    cancel_console_stdout_handler();
  }
  else if (g_stdin_mode == 'p')
  {
    Process* process = find_process_by_pid(g_stdin_arg);

    // Paranoia.
    if (process == NULL)
      return;

    // HACK: Empty packet indicates EOF.
    do_provide_stdin(&g_shepherd_socket,
                     process->pid, NULL, 0);
  }
  else if (g_stdin_mode == 'g' || g_stdin_mode == 't')
  {
    Debugger* debugger = find_debugger_for_stdin();

    // Paranoia.
    if (debugger == NULL)
      return;

    kill_debugger(debugger);
  }
  else
  {
    warn("Ignoring '%s'.", str);
  }
}


static void
dump_monitor_state(void)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    note("%s is using '%s'.", process->name, process->executable);
    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* thread = Array_get(&process->threads, i);
      note("%s thread %d at %d,%d (%s).",
           process->name, thread->tid, thread->x, thread->y, thread->state);
    }
  }
}


// Handle a line from stdin.
//
static void
handle_stdin_line(char* buf)
{
  size_t len = strlen(buf);

  // The prompt has been consumed.
  g_prompt_needed = true;

  if (buf[0] != '#')
  {
    // Normal input (including empty lines).
    handle_stdin_line_normal(buf, len);
  }
  else if (buf[1] == '#')
  {
    // Escaped pound.
    handle_stdin_line_normal(buf + 1, len - 1);
  }
  else if (buf[1] == '/')
  {
    // Raw command.
    handle_string_aux(buf + 2);
  }
  else if (buf[1] == '!')
  {
    // HACK: Synthetic "Ctrl+D".
    handle_stdin_line_close(buf);
  }
  else if (buf[1] == ' ' || buf[1] == '\0')
  {
    // HACK: Ignore lines which look like "pasted comments".
  }
  else if (buf[1] == '?')
  {
    // Dump monitor state.
    dump_monitor_state();
  }
  else if (buf[1] == '+')
  {
    // Set (or clear) "prefix".
    FREESET(g_stdin_prefix, (buf[2] != '\0') ? strdup_or_die(buf + 2) : NULL);
  }
  else if (buf[1] == '.')
  {
    // Reset focus.
    g_stdin_mode = '\0';
    g_stdin_arg = -1;
  }
  else if (buf[1] == 'c')
  {
    // Switch focus to console.
    // ISSUE: What if there is no console?
    g_stdin_mode = 'c';
    g_stdin_arg = -1;
  }
  else if (buf[1] == 'p' || buf[1] == 'g')
  {
    // Switch focus to remote process or local gdb.
    // Takes a pid, or finds the next appropriate pid.
    int arg = (buf[2] != '\0' ? atoi(buf + 2) : 0);
    if (arg == 0)
    {
      int old = (g_stdin_mode == buf[1]) ? g_stdin_arg : 0;
      int first = 0;
      for (uint k = 0; k < g_processes.size; k++)
      {
        Process* process = Array_get(&g_processes, k);

        // Skip inappropriate processes.
        if (buf[1] == 'p' && process->dead)
          continue;
        if (buf[1] == 'g' &&
            !(process->debugger != NULL &&
              process->debugger->stdin_arg != 'g'))
          continue;

        pid_t pid = process->pid;
        if (pid > old && (arg == 0 || pid < arg))
          arg = pid;
        if (pid >= 0 && (first == 0 || first > pid))
          first = pid;
      }
      if (arg == 0)
        arg = first;
    }
    if (find_process_by_pid(arg) != NULL)
    {
      g_stdin_mode = buf[1];
      g_stdin_arg = arg;
    }
    else
    {
      warn("There is no such process.");
    }
  }
  else if (buf[1] == 't')
  {
    if (buf[2] != '\0')
    {
      uint arg = atoi(buf + 2);
      uint x = arg / 100;
      uint y = arg % 100;
      if (x < MAX_WIDTH && y < MAX_HEIGHT &&
          g_tile_debuggers[x][y] != NULL)
      {
        g_stdin_mode = buf[1];
        g_stdin_arg = arg;
      }
      else
      {
        warn("There is no such tile debugger.");
      }
    }
    else
    {
      // FIXME: Find the "next" tile debugger.
      warn("Use '#tNNN'.");
    }
  }
  else
  {
    warn("Ignoring '%s'.", buf);
  }
}


// ISSUE: Normally, when you paste several lines into the Command prompt,
// each command appears one at a time on the screen.  But if, while these
// commands are being processed, you paste again, they show up immediately,
// and then once again as they are processed.  It seems possible that hacks
// (like the ones below) in "handle_stdin_readline()" could "fix" this.

#ifdef FUNKY_READLINE

#include <termios.h>

static tcflag_t g_readline_old_lflag;
static cc_t g_readline_old_vtime;
static struct termios g_readline_term;

void hacky_start()
{
  if (tcgetattr(STDIN_FILENO, &g_readline_term) != 0)
    punt_with_errno("Failure in 'tcgetattr()'");

  // Adjust the terminal slightly before the handler is installed.
  // Disable canonical mode processing and set the input character
  // time flag to be non-blocking.
  g_readline_old_lflag = g_readline_term.c_lflag;
  g_readline_old_vtime = g_readline_term.c_cc[VTIME];
  g_readline_term.c_lflag &= ~ICANON;
  g_readline_term.c_cc[VTIME] = 1;

  // ISSUE: Try commenting out this line, and then typing while
  // "tile-monitor" is ignoring "stdin" for a while.
  if (tcsetattr(STDIN_FILENO, TCSANOW, &g_readline_term) != 0)
    punt_with_errno("Failure in 'tcsetattr()'");
}

void hacky_finish()
{
  g_readline_term.c_lflag = g_readline_old_lflag;
  g_readline_term.c_cc[VTIME] = g_readline_old_vtime;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &g_readline_term) != 0)
    punt_with_errno("Failure in 'tcsetattr()'");
}

#endif


static void
handle_stdin_readline(char* buf)
{
  if (buf == NULL)
  {
    // This gets displayed right after the (visible) "^D".
    printf("<QUIT>\n");
    fflush(stdout);

    handle_command_quit();

    // WARNING: Calling "Pollable_close(&g_stdin_reader)" here will
    // cause the terminal to be left in a bad state, possibly due to
    // the underlying "close(STDIN_FD)" call.

    return;
  }

  // Save non-empty lines in the history.
  if (buf[0] != '\0')
  {
    if (add_history != NULL)
      add_history(buf);
  }

  handle_stdin_line(buf);

  free(buf);

  // Update prompt, before returning from this function.
  display_prompt_if_needed();
}


// Handle incoming traffic on "stdin".
//
static void
handle_stdin(Pollable* pollable)
{
  // Reset the SIGINT counter.
  g_signal_count = 0;

  // Stop handling stdin for now.
  Pollable_set_handle_readable(pollable, NULL);

  if (g_readline_ready)
  {
    // Read one char, and call "handle_stdin_readline()" when a
    // complete line is ready.  That, or the main event loop, will
    // call "display_prompt_if_needed()", to resume handling stdin.
    rl_callback_read_char();
    return;
  }

  char buf[1024];
  if (!fgets(buf, sizeof(buf), stdin))
  {
    note("Handling Ctrl+D.");

    handle_command_quit();
    Pollable_close(pollable);
    return;
  }

  size_t len = strlen(buf);
  if (len < 1 || buf[len - 1] != '\n')
  {
    warn("Ignoring command without final newline: '%s'", buf);
    return;
  }

  // Strip final newline.
  buf[--len] = '\0';

  handle_stdin_line(buf);
}



// HACK: See "forward_packet".
static uint g_forward_packet_start;


// HACK: If the given socket is valid, and the current packet came from
// anybody but that socket, forward the packet to that socket, and return
// true, and otherwise, return false.
//
static bool
forward_packet(RPC rpc, Pollable* socket)
{
  if (Pollable_valid(socket) && socket != rpc.socket)
  {
    Buffer* input = &rpc.socket->input;
    const uint packet = g_forward_packet_start;
    spew(3, "Forwarding to %s from %s [@%u].",
         socket->name, rpc.socket->name, packet);
    Pollable_write(socket, input->data + packet, input->size - packet);
    return true;
  }
  return false;
}



// Does the IDE actually use this?
//
// ISSUE: Add an explicit RPC for "quit" and use that instead?
//
void
perform_request_exit(RPC rpc)
{
  handle_command_quit();

  reply_request_exit(rpc);
}


// ISSUE: We must intercept the "launch" RPC from the IDE, and forward it
// explicitly to the shepherd, so that we can update our internal concept
// of how many launches have been requested.
//
void
perform_launch(RPC rpc, StringArray* argv, StringArray* envp,
               StringArray* tiles, StringArray* debug)
{
  int app = ++g_total_launches;
  spew(2, "Launch #%d initiated from IDE.", app);
  (void)forward_packet(rpc, &g_shepherd_socket);
}


void
perform_handle_command(RPC rpc, char* command)
{
  // Prevent re-entrant handling of IDE queries.
  Pollable_handler_func handle_readable = g_ide_socket.handle_readable;
  Pollable_set_handle_readable(&g_ide_socket, NULL);

  handle_string_aux(command);

  // Resume handling of IDE queries.
  Pollable_set_handle_readable(&g_ide_socket, handle_readable);

  reply_handle_command(rpc);
}



void
perform_note_quit(RPC rpc)
{
  if (g_requested_exit)
    spew(2, "Shepherd exited.");
  else
    warn("Shepherd crashed!");

  // Reply early.
  reply_note_quit(rpc);

  really_exit();
}


void
perform_note_idle(RPC rpc)
{
  if (forward_packet(rpc, &g_ide_socket))
    return;

  spew(2, "The shepherd is currently idle.");

  // Note that we have seen a "note_idle".
  g_pending_note_idle = true;

  reply_note_idle(rpc);
}


void
perform_note_size(RPC rpc, uint version, uint16_t width, uint16_t height)
{
  if (version != TILE_CHIP)
  {
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that the version of"
             " 'tile-monitor' which is being used\n"
             "is incompatible with the remote shepherd.\n\n");
    punt("Incompatible chip version %d from shepherd.", version);
  }

  spew(2, "Chip size is %dx%d", width, height);

  g_width = width;
  g_height = height;

  g_greeted = true;

  reply_note_size(rpc);
}


void
perform_note_crash(RPC rpc, int pid, int signal)
{
  Process* process = find_process_by_pid_or_die(pid, "note_crash");

  spew(1, "%s almost crashed with signal %d.", process->name, signal);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_note_crash(rpc);
}


void
perform_note_attach(RPC rpc, int pid)
{
  spew(3, "Got note_attach for %d.", pid);

  Process* process = find_process_by_pid_or_die(pid, "note_attach");

  // ISSUE: Can this actually happen?
  if (process->debugger != NULL)
    forget_debugger(process->debugger);

  // Lazily create a debugger.
  process->debugger = make_debugger(process, -1, -1);

  if (process->auto_bt != AUTO_BT_NONE)
  {
    process->auto_bt = AUTO_BT_READY;
    auto_bt_advance();
    reply_note_attach(rpc);
    return;
  }

  request_gdb(process);

  reply_note_attach(rpc);
}



typedef struct _HackyChild HackyChild;

struct _HackyChild
{
  // The original "rpc" info.
  RPC rpc;

  // The stdin/stdout/stderr pipes.
  Pollable console[3];

  // The spawned pid, or zero when dead.
  pid_t pid;

  // The final status, once dead.
  int status;
};


static Array g_hacky_children;


static void
handle_hacky_console(Pollable* pollable)
{
  Buffer* input = &pollable->input;
  Pollable_acquire(pollable, 0);
  spew_bytes(3, input->data, input->size, "%s read: ", pollable->name);
}


// A special "handle_writable" hook that is like "Pollable_flush()", but
// with non-sliding consumption, and calling "Pollable_close()" when all
// data is consumed.
//
static void
handle_hacky_writable(Pollable* pollable)
{
  Buffer* output = &pollable->output;

  int fd = pollable->fd;

  uint head = output->head;
  uint size = output->size - head;
  uint8_t* data = output->data + head;

  ssize_t len = write_some_bytes_or_die(fd, data, size);

  if (len < 0)
  {
    // Forget.
    output->head = size;
  }
  else
  {
    // Consume.
    output->head = head + len;
  }

  // Close when done.
  if (output->head == output->size)
  {
    Pollable_close(pollable);
  }
}


// HACK: Allow synchronous remote execution of commands.
//
void
perform_hacky_execute(RPC rpc, char* cwd,
                      StringArray* argv, StringArray* envp,
                      uint8_t* in_data, size_t in_size)
{
  if (!g_allow_execute)
  {
    warn("Run with '--allow-execute' to allow use of 'shepherd --execute'.");
    rpc_error(rpc, "Forbidden to use 'shepherd --execute'.");
    return;
  }

  spew(3, "Handling hacky_execute (%s).", argv->data[0]);

  int pipe_pair[3][2];
  for (int cfd = 0; cfd <= 2; cfd++)
  {
    pipe_or_die(pipe_pair[cfd]);
  }

  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    message_prefix = "";

    for (int cfd = 0; cfd <= 0; cfd++)
    {
      close_or_die(pipe_pair[cfd][1]);
      dup2_and_close_or_die(pipe_pair[cfd][0], cfd);
    }
    for (int cfd = 1; cfd <= 2; cfd++)
    {
      close_or_die(pipe_pair[cfd][0]);
      dup2_and_close_or_die(pipe_pair[cfd][1], cfd);
    }

    chdir(cwd);

    char* exe = argv->data[0];

    char* path = getenv("PATH");
    if (path != NULL)
    {
      // HACK: Use local "PATH" instead of any "remote" PATH.
      for (int i = 0; i < envp->size; i++)
      {
        if (has_prefix(envp->data[i], "PATH="))
        {
          envp->data[i] = strfmt_or_die("PATH=%s", path);
          break;
        }
      }

      // HACK: Check local "PATH" if exe is a non-executable relative path.
      // WARNING: The executed program will use the PATH (if any) in "envp",
      // so, for example, executing "which" will yield misleading results.
      if (strchr(exe, '/') == NULL && access(exe, X_OK) != 0)
      {
        char* scan;
        char* next;
        for (scan = path = strdup_or_die(path); scan != NULL; scan = next)
        {
          next = strchr(scan, ':');
          if (next != NULL)
            *next++ = '\0';
          char* maybe = strfmt_or_die("%s/%s", scan, exe);
          if (access(maybe, X_OK) == 0)
          {
            exe = maybe;
            break;
          }
          free(maybe);
        }
        free(path);
      }
    }

    // HACK: Ensure terminators.
    StringArray_append(argv, NULL);
    StringArray_append(envp, NULL);

    (void)execve(exe, argv->data, envp->data);
    punt_with_errno("Failure in 'execve(%s)'", exe);
  }

  spew(3, "Spawned process %d to handle hacky_execute.", pid);

  HackyChild* child = (HackyChild*)calloc(1, sizeof(HackyChild));

  child->rpc = rpc;

  child->pid = pid;

  for (int cfd = 0; cfd <= 0; cfd++)
  {
    close_or_die(pipe_pair[cfd][0]);

    int fd = pipe_pair[cfd][1];
    set_blocking_or_die(fd, false);
    Pollable* pollable = &child->console[cfd];
    Pollable_init(pollable, "Console %u", cfd);
    Pollable_open(pollable, fd, NULL);

    // Start writing to stdin.
    Buffer_write(&pollable->output, in_data, in_size);
    Pollable_set_handle_writable(pollable, handle_hacky_writable);
  }
  for (int cfd = 1; cfd <= 2; cfd++)
  {
    close_or_die(pipe_pair[cfd][1]);

    int fd = pipe_pair[cfd][0];
    set_blocking_or_die(fd, false);
    Pollable* pollable = &child->console[cfd];
    Pollable_init(pollable, "Console %u", cfd);
    Pollable_open(pollable, fd, handle_hacky_console);
  }

  int i = Array_find_or_append(&g_hacky_children, NULL);
  Array_set(&g_hacky_children, i, child);
}



void
perform_application_created(RPC rpc, int app)
{
  spew(3, "App %d created.", app);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_application_created(rpc);
}


void
perform_application_destroyed(RPC rpc, int app)
{
  spew(3, "App %d destroyed.", app);

  // HACK: See "handle_command_run".
  if (g_awaiting_reap == app)
    g_awaiting_reap = 0;

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_application_destroyed(rpc);
}



void
perform_process_created(RPC rpc, int pid, char* exe)
{
  spew(1, "Process %d created using '%s'.", pid, exe);

  (void)make_process(pid, strdup_or_die(exe));

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_process_created(rpc);
}


void
perform_process_destroyed(RPC rpc, int pid, int status)
{
  spew(3, "Process %d destroyed with status 0x%x.", pid, status);

  Process* process = find_process_by_pid_or_die(pid, "process_destroyed");

  kill_process(process, status);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_process_destroyed(rpc);
}


void
perform_process_joined(RPC rpc, int pid, int app)
{
  spew(2, "Process %d joined app %d.", pid, app);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_process_joined(rpc);
}


void
perform_process_execed(RPC rpc, int pid, char* exe)
{
  spew(1, "Process %d execed '%s'.", pid, exe);

  Process* process = find_process_by_pid_or_die(pid, "process_execed");

  FREESET(process->executable, strdup_or_die(exe));

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_process_execed(rpc);
}


void
perform_thread_created(RPC rpc, int pid, int tid, uint16_t x, uint16_t y)
{
  spew(2, "Process %d thread %d created on %u,%u.", pid, tid, x, y);

  Process* process = find_process_by_pid_or_die(pid, "thread_created");

  Thread* thread = calloc(1, sizeof(*thread));

  thread->tid = tid;
  thread->x = x;
  thread->y = y;

  Array_append(&process->threads, thread);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_thread_created(rpc);
}


void
perform_thread_destroyed(RPC rpc, int pid, int tid)
{
  spew(2, "Process %d thread %d destroyed.", pid, tid);

  Process* process = find_process_by_pid_or_die(pid, "thread_destroyed");

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* thread = Array_get(&process->threads, i);
    if (thread->tid == tid)
    {
      Array_excise(&process->threads, i, 1);
      free((void*)thread->state);
      free(thread);
      break;
    }
  }

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_thread_destroyed(rpc);
}


void
perform_thread_moved(RPC rpc, int pid, int tid, uint16_t x, uint16_t y)
{
  spew(2, "Process %d thread %d moved to %u,%u.", pid, tid, x, y);

  Thread* thread = find_thread_or_die(pid, tid, "thread_moved");

  thread->x = x;
  thread->y = y;

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_thread_moved(rpc);
}


void
perform_thread_transitioned(RPC rpc, int pid, int tid, char* state)
{
  spew(2, "Process %d thread %d transitioned to %s.", pid, tid, state);

  Thread* thread = find_thread_or_die(pid, tid, "thread_transitioned");

  FREESET(thread->state, strdup_or_die(state));

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_thread_transitioned(rpc);
}



static void
handle_console_traffic(int pid, int cfd, uint8_t* bytes, size_t bytes_size)
{
  ConsoleStream* stream;

  bool so = (cfd == STDOUT_FILENO);

  // NOTE: Process "zero" is the shepherd and its descendants.

  if (pid == 0)
  {
    stream = so ? &g_shepherd_stdout_stream : &g_shepherd_stderr_stream;
  }

  else
  {
    spew(3, "Process %u wrote to the console.", pid);
    Process* process = find_process_by_pid_or_die(pid, "console");
    stream = so ? &process->stdout_stream : &process->stderr_stream;
  }

  ConsoleStream_write(stream, bytes, bytes_size);
}


void
perform_console_stdout(RPC rpc, int pid, uint8_t* bytes, size_t bytes_size)
{
  handle_console_traffic(pid, STDOUT_FILENO, bytes, bytes_size);
  reply_console_stdout(rpc);
}


void
perform_console_stderr(RPC rpc, int pid, uint8_t* bytes, size_t bytes_size)
{
  handle_console_traffic(pid, STDERR_FILENO, bytes, bytes_size);
  reply_console_stderr(rpc);
}


void
perform_note_cycles(RPC rpc, uint64_t cycles)
{
  if (g_bench_stats != NULL)
  {
    FILE* fff = fopen(g_bench_stats, "w");
    if (fff != NULL)
    {
      fprintf(fff,
              "%s\n"
              "<statistics>\n"
              "<chip\n"
              " x_tiles=\"%d\"\n"
              " y_tiles=\"%d\"\n"
              " cycles=\"%llu\"\n"
              ">\n"
              "</chip>\n"
              "</statistics>\n",
              g_xml_intro, g_width, g_height, (long long)cycles);
      fclose(fff);
    }
    else
    {
      warn("Could not write to '%s'.", g_bench_stats);
    }
  }

  reply_note_cycles(rpc);
}


void
perform_forward_s2m(RPC rpc, uint id, uint8_t* bytes, size_t size)
{
  Pollable* socket = Array_get(&g_forward_sockets, id);

  assert(socket != NULL);

  if (size == 0)
  {
    // HACK: Empty packet indicates EOF.
    spew(2, "%s closed remotely.", socket->name);

    if (Pollable_valid(socket))
    {
      // ISSUE: This may not flush completely.
      Pollable_flush(socket);
      Pollable_close(socket);

      // Confirm that the socket has been closed.
      // HACK: Use an empty packet to indicate EOF.
      do_forward_m2s(&g_shepherd_socket, id, NULL, 0);
    }

    // Reap.
    Pollable_destroy(socket);
    free(socket);
    Array_set(&g_forward_sockets, id, NULL);
  }
  else if (Pollable_valid(socket))
  {
    spew(3, "%s writing %zu bytes.", socket->name, size);
    Pollable_write(socket, bytes, size);
  }

  reply_forward_s2m(rpc);
}


static void
handle_forward_socket(Pollable* socket)
{
  uint id = (uint)(uintptr_t)socket->info;

  Buffer* input = &socket->input;

  // Leave room for packet overhead.
  int result = Pollable_acquire(socket, RPC_HEADER_SIZE + 4 + 4);

  if (result > 0)
  {
    // Multiplex the data.
    do_forward_m2s(&g_shepherd_socket, id, input->data, input->size);

    // Consume.
    input->size = 0;
  }

  else if (result < 0)
  {
    // Multiplex the EOF.
    // HACK: Use an empty packet to indicate EOF.
    do_forward_m2s(&g_shepherd_socket, id, NULL, 0);
  }
}


void
perform_forward_connect(RPC rpc, uint id, uint16_t host_port)
{
  assert(id == g_forward_sockets.size);

  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Forward %d [%d]", id, host_port);
  socket->info = (void*)(uintptr_t)id;

  Array_append(&g_forward_sockets, socket);

  int fd = simple_connect_aux(NULL, host_port);

  if (fd < 0)
  {
    rpc_error_with_errno(rpc, "Could not forward to port %u", host_port);

    // HACK: Generate a synthetic close.
    do_forward_m2s(&g_shepherd_socket, id, NULL, 0);

    return;
  }

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  Pollable_open(socket, fd, handle_forward_socket);

  reply_forward_connect(rpc);
}


void
perform_tunnel_s2m(RPC rpc, uint id, uint8_t* bytes, size_t size)
{
  Pollable* socket = Array_get(&g_tunnel_sockets, id);

  assert(socket != NULL);

  if (size == 0)
  {
    // HACK: Empty packet indicates EOF.
    spew(2, "%s closed remotely.", socket->name);

    if (Pollable_valid(socket))
    {
      // ISSUE: This may not flush completely.
      Pollable_flush(socket);
      Pollable_close(socket);

      // Confirm that the socket has been closed.
      // HACK: Use an empty packet to indicate EOF.
      do_tunnel_m2s(&g_shepherd_socket, id, NULL, 0);
    }

    // Reap the tunnel.
    Pollable_destroy(socket);
    free(socket);
    Array_set(&g_tunnel_sockets, id, NULL);
  }
  else if (Pollable_valid(socket))
  {
    spew(3, "%s writing %zu bytes.", socket->name, size);
    Pollable_write(socket, bytes, size);
  }

  reply_tunnel_s2m(rpc);
}


void
perform_rsp_s2m(RPC rpc, int pid, uint8_t* bytes, size_t size)
{
  Process* process = find_process_by_pid(pid);
  if (process == NULL || process->debugger == NULL)
  {
    rpc_error(rpc, "Ignoring rsp traffic for process %d.", pid);
    return;
  }

  Pollable* socket = &process->debugger->gdb_socket;

  // NOTE: We do not implement magic EOF on this end.
  // NOTE: We always get complete RSP packets.
  spew_bytes(3, bytes, size, "%s writing: ", socket->name);

  Pollable_write(socket, bytes, size);

  reply_rsp_s2m(rpc);
}


void
perform_target_info(RPC rpc, char* info)
{
  spew(3, "Got target info:\n%s", info);

  g_target_info = strdup_or_die(info);

  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_target_info(rpc);
}


// Invoked when simulator (or rather, the Reporter plugin)
// reports profile_file_generated event. */
void
perform_profile_file_generated(RPC rpc, char* pathname)
{
  spew(3, "Profile file generated by simulator:\n%s", pathname);

  // Forward event to IDE, if present.
  if (forward_packet(rpc, &g_ide_socket))
    return;

  reply_profile_file_generated(rpc);
}


void
perform_mon_heartbeat_done(RPC rpc)
{
  assert(rpc.socket == &g_reporter_socket);
  assert(Pollable_valid(&g_reporter_socket));

  if (g_quitting || g_peer_quitting)
  {
    // Perma-start heartbeat.
    do_sim_finished_commands(&g_reporter_socket);
  }
  else
  {
    assert(Pollable_valid(&g_peer_socket));

    do_peer_heartbeat(&g_peer_socket);

    g_self_heartbeats++;
    if (g_self_heartbeats == g_peer_heartbeats)
      do_sim_heartbeat_start(&g_reporter_socket);
  }

  reply_mon_heartbeat_done(rpc);
}


void
perform_mon_bmd_attach(RPC rpc, uint16_t x, uint16_t y,
                       StringArray* path_addr_pairs)
{
  spew(3, "Got mon_bmd_attach for %d,%d.", x, y);

  if (x >= MAX_WIDTH || y >= MAX_HEIGHT)
  {
    rpc_error(rpc, "Illegal BMD tile %d,%d.", x, y);
    return;
  }

  // ISSUE: Can this actually happen?
  if (g_tile_debuggers[x][y] != NULL)
    forget_debugger(g_tile_debuggers[x][y]);

  Debugger* debugger = make_debugger(NULL, x, y);
  g_tile_debuggers[x][y] = debugger;

  if (!Pollable_valid(&g_ide_socket))
    note("Attaching to tile %u,%u.", x, y);

  request_gdb_aux(debugger, path_addr_pairs);

  reply_mon_bmd_attach(rpc);
}


void
perform_mon_bmd_rsp(RPC rpc, uint16_t x, uint16_t y,
                    uint8_t* data, size_t data_size)
{
  if (x >= MAX_WIDTH || y >= MAX_HEIGHT || g_tile_debuggers[x][y] == NULL)
  {
    rpc_error(rpc, "Illegal BMD tile %d,%d.", x, y);
    return;
  }

  Debugger* debugger = g_tile_debuggers[x][y];

  Pollable* socket = &debugger->gdb_socket;

  // NOTE: We do not implement magic EOF on this end.
  // NOTE: We always get complete RSP packets.
  spew_bytes(3, data, data_size, "%s writing: ", socket->name);

  Pollable_write(socket, data, data_size);

  reply_mon_bmd_rsp(rpc);
}


// Attempt to prevent forbidden use of FUSE.
//
// FIXME: Forbid "sneaky" use of "symlinks", including subtle uses
// like "/okay-path/sneaky-symlink/forbidden-file".  Are there any
// cases where a user might WANT this to be allowed?  See bug 5875.
//
static bool
forbid_fuse(RPC rpc, char* path)
{
  for (uint i = 0; i < g_symbol_map_dirs.size; i += 2)
  {
    char* host_dir = StringArray_get(&g_symbol_map_dirs, i);
    if (has_prefix(path, host_dir))
    {
      char end = path[strlen(host_dir)];
      if ((end == '\0') || (end == '/'))
      {
        // Forbid obvious sneaky path hackery.
        if (strstr(path, "/../") || has_suffix(path, "/.."))
          break;

        return false;
      }
    }
  }

  rpc_error(rpc, "-%u", EPERM);
  return true;
}


void
perform_fuse_getattr(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  struct stat sb;
  if (lstat(path, &sb) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

#ifdef st_atime
  // If "st_atime" is a #define, that means the actual host struct stat
  // uses "struct timespec", so we add "st_atimensec" (etc) defines.
#define st_atimensec st_atim.tv_nsec
#define st_mtimensec st_mtim.tv_nsec
#define st_ctimensec st_ctim.tv_nsec
#endif

  // HACK: Convert current user to "root".

  reply_fuse_getattr(rpc,
                     sb.st_dev,
                     sb.st_ino,
                     sb.st_mode,
                     sb.st_nlink,
                     (sb.st_uid != getuid()) ? sb.st_uid : 0,
                     (sb.st_gid != getgid()) ? sb.st_gid : 0,
                     sb.st_rdev,
                     sb.st_size,
                     sb.st_blksize,
                     sb.st_blocks,
                     sb.st_atime,
                     sb.st_atimensec,
                     sb.st_mtime,
                     sb.st_mtimensec,
                     sb.st_ctime,
                     sb.st_ctimensec);
}


void
perform_fuse_statvfs(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  struct statvfs sf;
  if (statvfs(path, &sf) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_statvfs(rpc,
                     sf.f_bsize,
                     sf.f_frsize,
                     sf.f_blocks,
                     sf.f_bfree,
                     sf.f_bavail,
                     sf.f_files,
                     sf.f_ffree,
                     sf.f_favail,
                     sf.f_fsid,
                     sf.f_flag,
                     sf.f_namemax);
}


void
perform_fuse_access(RPC rpc, char* path, int mask)
{
  if (forbid_fuse(rpc, path))
    return;

  if (access(path, mask) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_access(rpc);
}


void
perform_fuse_readlink(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  char target[PATH_MAX];

  if (readlink_aux(path, target, sizeof(target)) < 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_readlink(rpc, target);
}


void
perform_fuse_readdir(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  DIR* dir = opendir(path);
  if (dir == NULL)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  StringArray children;
  StringArray_init(&children);

  struct dirent* val;
  while ((val = readdir(dir)) != NULL)
  {
    StringArray_append(&children, strdup_or_die(val->d_name));
  }

  if (closedir(dir) != 0)
  {
    rpc_error(rpc, "-%u", errno);
  }
  else
  {
    reply_fuse_readdir(rpc, &children);
  }

  StringArray_free_and_clear(&children);
  StringArray_destroy(&children);
}


void
perform_fuse_mknod(RPC rpc, char* path, uint mode, uint64_t rdev)
{
  if (forbid_fuse(rpc, path))
    return;

  if ((S_ISFIFO(mode) ? mkfifo(path, mode) : mknod(path, mode, rdev)) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_mknod(rpc);
}


void
perform_fuse_mkdir(RPC rpc, char* path, uint mode)
{
  if (forbid_fuse(rpc, path))
    return;

  if (mkdir(path, mode) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_mkdir(rpc);
}


void
perform_fuse_rmdir(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  if (rmdir(path) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_rmdir(rpc);
}


void
perform_fuse_unlink(RPC rpc, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  if (unlink(path) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_unlink(rpc);
}


void
perform_fuse_rename(RPC rpc, char* path, char* newpath)
{
  if (forbid_fuse(rpc, path) || forbid_fuse(rpc, newpath))
    return;

  if (rename(path, newpath) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_rename(rpc);
}


void
perform_fuse_link(RPC rpc, char* path, char* newpath)
{
  if (forbid_fuse(rpc, path) || forbid_fuse(rpc, newpath))
    return;

  if (link(path, newpath) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_link(rpc);
}


void
perform_fuse_symlink(RPC rpc, char* target, char* path)
{
  if (forbid_fuse(rpc, path))
    return;

  if (symlink(target, path) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_symlink(rpc);
}


void
perform_fuse_chmod(RPC rpc, char* path, uint mode)
{
  if (forbid_fuse(rpc, path))
    return;

  if (chmod(path, mode) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_chmod(rpc);
}


void
perform_fuse_chown(RPC rpc, char* path, uint uid, uint gid)
{
  if (forbid_fuse(rpc, path))
    return;

  // HACK: Convert "root" to current user.
  if (uid == 0)
    uid = getuid();
  if (gid == 0)
    gid = getgid();

  if (lchown(path, uid, gid) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_chown(rpc);
}


void
perform_fuse_truncate(RPC rpc, char* path, uint64_t size)
{
  if (forbid_fuse(rpc, path))
    return;

  if (truncate(path, size) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_truncate(rpc);
}


void
perform_fuse_utimens(RPC rpc, char* path, uint s0, uint n0, uint s1, uint n1)
{
  if (forbid_fuse(rpc, path))
    return;

  struct utimbuf ub = { s0, s1 };
  int res = utime(path, &ub);
  if (res != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  reply_fuse_utimens(rpc);
}


void
perform_fuse_create(RPC rpc, char* path, int flags, uint mode)
{
  if (forbid_fuse(rpc, path))
    return;

  // ISSUE: Decline to support "O_APPEND"?

  int fd = open(path, flags, mode);
  if (fd < 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  close_or_die(fd);

  reply_fuse_create(rpc);
}


void
perform_fuse_open(RPC rpc, char* path, int flags)
{
  if (forbid_fuse(rpc, path))
    return;

  // ISSUE: Decline to support "O_APPEND"?

  int fd = open(path, flags);
  if (fd < 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  close_or_die(fd);

  reply_fuse_open(rpc);
}


void
perform_fuse_read(RPC rpc, char* path, uint64_t offset, uint size)
{
  if (forbid_fuse(rpc, path))
    return;

  int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  uint8_t buf[size];
  ssize_t n = pread(fd, buf, size, offset);
  if (n < 0)
  {
    rpc_error(rpc, "-%u", errno);
    close_or_die(fd);
    return;
  }

  reply_fuse_read(rpc, buf, n);

  close_or_die(fd);
}


void
perform_fuse_write(RPC rpc, char* path, uint64_t offset,
                   uint8_t* buf, size_t size)
{
  if (forbid_fuse(rpc, path))
    return;

  struct stat stbuf;

  if (stat(path, &stbuf) != 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  // HACK: Make sure we can write.
  if ((stbuf.st_mode & S_IWUSR) == 0)
    (void)chmod(path, stbuf.st_mode | S_IWUSR);

  int fd = open(path, O_WRONLY);

  // HACK: Make sure we can write.
  if ((stbuf.st_mode & S_IWUSR) == 0)
    (void)chmod(path, stbuf.st_mode);

  if (fd < 0)
  {
    rpc_error(rpc, "-%u", errno);
    return;
  }

  ssize_t n = pwrite(fd, buf, size, offset);
  if (n < 0)
  {
    rpc_error(rpc, "-%u", errno);
    close_or_die(fd);
    return;
  }

  reply_fuse_write(rpc, n);

  close_or_die(fd);
}



// See "handle_command_quit()".
//
void
perform_peer_quit(RPC rpc)
{
  // Peer wants to quit.
  g_peer_quitting = true;

  // Perma-start heartbeat if waiting for a heartbeat from our peer.
  if (Pollable_valid(&g_reporter_socket) &&
      g_self_heartbeats == g_peer_heartbeats + 1)
  {
    g_peer_heartbeats++;
    do_sim_finished_commands(&g_reporter_socket);
  }

  reply_peer_quit(rpc);
}


// See "handle_command_peer_barrier()".
//
void
perform_peer_barrier(RPC rpc)
{
  g_peer_barrier_received++;

  reply_peer_barrier(rpc);
}


void
perform_peer_heartbeat(RPC rpc)
{
  assert(rpc.socket == &g_peer_socket);
  assert(Pollable_valid(&g_peer_socket));

  if (!g_quitting)
  {
    assert(Pollable_valid(&g_reporter_socket));
    //--assert(!g_peer_quitting);

    g_peer_heartbeats++;
    if (g_self_heartbeats == g_peer_heartbeats)
      do_sim_heartbeat_start(&g_reporter_socket);
  }

  reply_peer_heartbeat(rpc);
}


// Facilitate passing data from one simulated shim to another.
//
void
perform_shim_traffic(RPC rpc, char* shim, uint8_t* bytes, size_t bytes_size)
{
  if (!Pollable_valid(&g_peer_socket))
  {
    rpc_error(rpc, "There is no peer.");
    return;
  }

  if (!Pollable_valid(&g_reporter_socket))
  {
    rpc_error(rpc, "There is no simulator.");
    return;
  }

  if (rpc.socket != &g_peer_socket)
  {
    // Forward traffic to our peer.
    do_shim_traffic(&g_peer_socket, shim, bytes, bytes_size);
  }
  else
  {
    // Forward traffic to our simulator.
    do_shim_traffic(&g_reporter_socket, shim, bytes, bytes_size);
  }

  reply_shim_traffic(rpc);
}



// Handle a packet.
//
static void
handle_packet(RPC rpc)
{
  Buffer* input = &rpc.socket->input;

  // HACK: Save actual start of packet for use by "forward_packet",
  // in case "input->head" gets modified by "dispatch_packet".
  g_forward_packet_start = input->head - RPC_HEADER_SIZE;

  if (!dispatch_packet(rpc) &&
      !forward_packet(rpc, &g_ide_socket) &&
      !forward_packet(rpc, &g_shepherd_socket))
  {
    if (rpc.code >= 0xC000)
    {
      // Unexpected error packet.
      warn("Got unknown packet code 0x%04x: %s",
           rpc.code, input->data + input->head);
    }
    else
    {
      // Unexpected packet.
      warn("Got unknown packet code 0x%04x.", rpc.code);
    }
  }
}

// Determine if a given IDE query is allowed, based on context.
// While processing command-line options, a restricted set of
// RPC query codes are allowed, to avoid conflict with
// tile-monitor's own command-line processing.
static bool
allowed_non_interactive_ide_query(uint16_t code)
{
  bool result = false;

  static uint16_t allowed_async_rpc_codes[] = {
    QUERY_CODE_PING,
    QUERY_CODE_REQUEST_EXIT,
    QUERY_CODE_KILL_EVERYONE,
    QUERY_CODE_DEBUG_ATTACH,
    0
  };

  for (int i = 0; allowed_async_rpc_codes[i] != 0; ++i)
  {
    if (code == allowed_async_rpc_codes[i])
    {
      result = true;
      break;
    }
  }

  return result;
}

// Handle an RPC packet from the IDE.
// Includes check for whether tile-monitor is currently processing
// commands from its command line.
static void
handle_ide_packet(RPC rpc)
{
  if (g_running || allowed_non_interactive_ide_query(rpc.code))
  {
    // Invoke the normal handling.
    handle_packet(rpc);
  }
  else
  {
    // Warn and drop the packet.
    warn("This RPC query is not allowed "
         "while processing tile-monitor command line: 0x%04x.", rpc.code);
    rpc_error(rpc,
              "This RPC query is not allowed "
              "while processing tile-monitor command line: 0x%04x.", rpc.code);
  }
}

// Handle incoming traffic on the shepherd/watchdog/reporter socket.
//
// HACK: If the simulator exits, these sockets may close even without
// us seeing a "note_quit" query.
//
static void
handle_shepherd_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_packet) < 0 &&
      !g_opt_raw_mode)
    really_exit();
}


// Handle incoming traffic on the peer socket.
//
// NOTE: After receiving "peer_quit" query, disconnection is "expected".
//
static void
handle_peer_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_packet) < 0)
  {
    if (!g_peer_quitting && !g_opt_raw_mode)
      punt("Lost connection to peer.");
  }
}


// Handle incoming traffic on the IDE socket.
//
static void
handle_ide_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_ide_packet) < 0)
  {
    punt("Lost connection to ide!");
  }
}

// Handle external command socket connection.
static void
handle_external_command_socket_connection(Pollable* pty)
{
  // Avoid re-entrant handling of socket commands
  Pollable_handler_func handle_readable = pty->handle_readable;
  Pollable_set_handle_readable(pty, NULL);

  // Attempt to read input into the connection's buffer.
  // Note: this will automatically close the socket
  // when we reach end of stream.
  int n = Pollable_read(pty, 4096);

  // If we got something new...
  if (n > 0)
  {
    Buffer* input = &pty->input;

    // Look for complete command lines to process.
    int newline;
    while ((newline = Buffer_find(input, '\n')) >= 0)
    {
      // Ignore blank lines.
      if (newline > 0)
      {
        // We're going to discard characters up to and
        // including the newline anyway, so we can overwrite
        // the newline character to create a C string.
        char* command = (char*) input->data;
        command[newline] = '\0';

        // Display command, so it's visible to user.
        fprintf(stdout, "\nCommand> %s\n", command);
        fflush(stdout);

        // Handle the command, as if it was typed at the command prompt.
        // (Would like to use handle_stdin(), but that frees the buffer.)
        if (add_history != NULL) add_history(command);
        handle_stdin_line(command);

        // Note: handle_stdin_line() scribbles NULs in the buffer
        // while tokenizing the command, but does not go beyond
        // the terminating NUL of the characters we're discarding,
        // so this is okay here.

        // Write reply to socket, and flush to be sure it's sent.
        const char* reply = "OK\n";
        Pollable_write(pty, reply, 3);
        Pollable_flush(pty);

        // Update prompt.
        display_prompt_if_needed();
      }

      // Consume input up to and including newline.
      Buffer_excise(input, 0, newline+1);
    }
  }

  // When connection has closed, dispose of it.
  if (! Pollable_valid(pty))
  {
    // Reap the connection object
    Pollable_destroy(pty);
    free(pty);
    return;
  }
  else
  {
    // Resume handling of commands from socket.
    Pollable_set_handle_readable(pty, handle_readable);
  }
}

// Handle incoming connection on external command socket.
static void
handle_external_command_socket(Pollable* pty)
{
  spew(1, "Accepting connection on command port.");

  // Update prompt
  display_prompt_if_needed();

  int fd = simple_accept(g_external_command_socket.fd);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  Pollable* socket = calloc(1, sizeof(Pollable));
  Pollable_init(socket, "Command socket connection");
  Pollable_open(socket, fd, handle_external_command_socket_connection);
}


static void
handle_waitpid(pid_t pid, int status)
{
  // Handle simulator.
  if (g_simulator_pid == pid)
  {
    // NOTE: It is theoretically possible for us to detect the death
    // of the simulator before we have seen the "note_quit" query, or
    // detected the closing of any sockets.  This has been seen once.
    // Note that this is not subject to "batch mode" determinism!

    // NOTE: The simulator is allowed to exit "spontaneously" without
    // the shepherd ever having sent a "note_quit" query.

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      handle_unexpected_simulator_death(status);

    exit(g_exit_code);
  }

  // Handle local gdb for process debugging.
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    Debugger* debugger = process->debugger;

    if (debugger != NULL && debugger->local_gdb_pid == pid)
    {
      if (status != 0)
        warn("%s had final status 0x%x.", debugger->name, status);
      else
        spew(3, "%s had final status 0x%x.", debugger->name, status);
      debugger->local_gdb_pid = -1;
      return;
    }
  }

  // Handle local gdb for tile debugging.
  for (uint x = 0; x < MAX_WIDTH; x++)
  {
    for (uint y = 0; y < MAX_HEIGHT; y++)
    {
      Debugger* debugger = g_tile_debuggers[x][y];
      if (debugger != NULL && debugger->local_gdb_pid == pid)
      {
        if (status != 0)
          warn("%s had final status 0x%x.", debugger->name, status);
        else
          spew(3, "%s had final status 0x%x.", debugger->name, status);
        debugger->local_gdb_pid = -1;
        return;
      }
    }
  }

  // Handle "killed" debuggers.
  for (int i = 0; i < g_killed_debuggers.size; i++)
  {
    Debugger* debugger = (Debugger*)Array_get(&g_killed_debuggers, i);
    if (debugger->local_gdb_pid == pid)
    {
      if (status != 0)
        warn("%s had final status 0x%x (after socket closed).",
             debugger->name, status);
      else
        spew(3, "%s had final status 0x%x (after socket closed).",
             debugger->name, status);
      debugger->local_gdb_pid = -1;
      return;
    }
  }

  // FIXME: Unify "Child" and "HackyChild".

  // Handle "Child".
  for (int i = 0; i < g_children.size; i++)
  {
    Child* child = (Child*)Array_get(&g_children, i);
    if (child->pid == pid)
    {
      spew(3, "Process %d got final status %d.", pid, status);
      Array_excise(&g_children, i, 1);
      return;
    }
  }

  // Handle "HackyChild".
  for (int i = 0; i < g_hacky_children.size; i++)
  {
    HackyChild* child = (HackyChild*)Array_get(&g_hacky_children, i);
    if (child != NULL && child->pid == pid)
    {
      spew(3, "Process %d got hacky_execute status %d.", pid, status);

      child->pid = 0;
      child->status = status;
      return;
    }
  }

  warn("Unknown process %d got waitpid status: 0x%04x.", pid, status);
}


// HACK: Call "erase_prompt()" before any "messages" are displayed.
//
// ISSUE: This is not really the intention of "message_prefix_hook()",
// it would be cleaner to support a simple "message_erase_hook()".
//
static void
message_prefix_func(char* buf, size_t len)
{
  erase_prompt();
  snprintf(buf, len, "%s", message_prefix);
}


// We use a very small part of "libedit.so" to support interactive
// mode, and we access it dynamically, so we don't have to cross-build
// it, and we don't have to put it in the recovery bootrom.


// The name of the "libedit" dll.
static const char* libedit_name = "libedit.so.0";

// The actual "libedit" dll.
static void* libedit_dll;


// Help initialize "libedit".
//
static void*
libedit_sym(const char* name)
{
  if (libedit_dll == NULL)
    return NULL;
  void* sym = dlsym(libedit_dll, name);
  if (sym == NULL)
    warn("Could not find '%s' in '%s'.", name, libedit_name);
  return sym;
}


// Initialize "libedit".
//
static void
libedit_init_if_needed(void)
{
  if (!g_readline_desired)
    return;

  // Only initialize once.
  g_readline_desired = false;

  // Open the libedit DLL.
  libedit_dll = dlopen(libedit_name, RTLD_NOW);
  if (libedit_dll == NULL)
    warn("Cannot open '%s'.", libedit_name);

  // Look up some function symbols.
  rl_callback_handler_install = libedit_sym("rl_callback_handler_install");
  rl_callback_handler_remove = libedit_sym("rl_callback_handler_remove");
  rl_callback_read_char = libedit_sym("rl_callback_read_char");
  rl_forced_update_display = libedit_sym("rl_forced_update_display");
  add_history = libedit_sym("add_history");

  // Look up a variable.
  char** rl_readline_name = libedit_sym("rl_readline_name");

  // Handle failure to open the DLL or look up any symbols.
  if (libedit_dll == NULL)
  {
    warn("Interactive command editing will not be available.");
    return;
  }

  // Allow per-app "~/.inputrc" settings.
  (*rl_readline_name) = "tile-monitor";

  // Install the hook above.
  message_prefix_hook = message_prefix_func;

  g_readline_ready = true;
}


// Emit a prompt.
//
static void
emit_prompt(char* prompt)
{
  libedit_init_if_needed();

  if (g_readline_ready)
  {
    // Install the line handler, and/or reset the prompt.
    // NOTE: This calls "read_prepare()", which calls "sig_set()",
    // which installs "sig_handler()" for various signals, which
    // will call "tty_cookedmode()", and then re-installs the
    // original handler and re-raises the signal.  There appears
    // to be no way to undo the "tty_cookedmode()" call, and so
    // we simply reinstall our own signal handlers.  ISSUE: Why
    // are we bothering to do this for SIGTERM and SIGHUP?
    rl_callback_handler_install(prompt, handle_stdin_readline);
    if (signal(SIGQUIT, dispatch_events_handle_signal) == SIG_ERR ||
        signal(SIGINT, dispatch_events_handle_signal) == SIG_ERR ||
        signal(SIGTERM, dispatch_events_handle_signal) == SIG_ERR ||
        signal(SIGHUP, dispatch_events_handle_signal) == SIG_ERR)
    {
      punt("Failed to re-register signal handlers.");
    }
  }
  else
  {
    printf("%s", prompt);
    fflush(stdout);
  }

  if (g_prompt_redraw)
  {
    g_prompt_redraw = false;

    if (g_readline_ready)
      rl_forced_update_display();
  }
}


// NOTE: If this is called from inside "rl_callback_read_char()", then it
// does not actually display the prompt, but it prepares to do so.
//
static void
display_prompt_if_needed(void)
{
  // Paranoia?
  if (g_waiting)
    return;

  // Do nothing if exiting.
  if (g_requested_exit)
    return;

  // Do nothing if using the IDE.
  if (Pollable_valid(&g_ide_socket))
    return;

  // Prepare stdin, if needed.
  if (!Pollable_valid(&g_stdin_reader))
  {
    // Prepare to process stdin.
    Pollable_open(&g_stdin_reader, STDIN_FILENO, NULL);

    // A prompt is needed.
    g_prompt_needed = true;
  }

  // Show a prompt, if needed.
  if (g_prompt_redraw || g_prompt_needed)
  {
    g_prompt_needed = false;

    Buffer* prompt = &g_prompt_buffer;
    Buffer_clear(prompt);

    if (g_stdin_mode == '\0')
    {
      if (g_stdin_prefix != NULL)
        Buffer_printf(prompt, "Command: [%s] ", g_stdin_prefix);
      else
        Buffer_print(prompt, "Command: ");
    }
    else if (g_stdin_arg < 0)
    {
      Buffer_printf(prompt, "Command (%c): ", g_stdin_mode);
    }
    else
    {
      Buffer_printf(prompt, "Command (%c%u): ", g_stdin_mode, g_stdin_arg);
    }

    ConsoleStream* prompter = NULL;

    // Determine the active console stream, if any.
    if (g_stdin_mode == 'g' || g_stdin_mode == 't')
    {
      Debugger* debugger = find_debugger_for_stdin();
      if (debugger != NULL)
        prompter = &debugger->local_gdb_stream;
    }
#if 0
    else if (g_stdin_mode == 'c')
    {
      prompter = &g_console_stream;
    }
#endif

    // Add in the console prompt if appropriate.
    if (prompter != NULL && prompter->prompted != NULL)
      Buffer_print(prompt, prompter->prompted);

    Buffer_append(prompt, '\0');

    // Emit the prompt.
    emit_prompt((char*)prompt->data);
  }

  // Handle stdin if appropriate.
  if (Pollable_valid(&g_stdin_reader))
    Pollable_set_handle_readable(&g_stdin_reader, handle_stdin);
}


static void
handle_events(void)
{
  // Wait for something to happen.
  dispatch_events(-1);

  // Handle deaths.
  while (true)
  {
    int status = 0;
    pid_t pid = waitpid(-1, &status, WNOHANG);

    if (pid > 0)
    {
      handle_waitpid(pid, status);
    }
    else if ((pid == 0) || (errno == ECHILD))
    {
      // ISSUE: I think I saw "ECHILD" happen once.
      break;
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in waitpid()");
    }
  }

  // Reap dead processes.
  for (int i = g_processes.size - 1; i >= 0; i--)
  {
    Process* process = Array_get(&g_processes, i);

    if (process->dead && process->debugger == NULL)
    {
      spew(3, "%s reaped.", process->name);
      Array_excise(&g_processes, i, 1);
      free_process(process);
    }
  }

  // Reap "killed" debuggers.
  for (int i = g_killed_debuggers.size - 1; i >= 0; i--)
  {
    Debugger* debugger = (Debugger*)Array_get(&g_killed_debuggers, i);
    if (debugger->local_gdb_pid < 0)
    {
      spew(3, "%s reaped.", debugger->name);
      Array_excise(&g_killed_debuggers, i, 1);
      free_debugger(debugger);
    }
  }

  // ISSUE: What about "g_children"?

  // Reap dead hacky children.
  for (int i = 0; i < g_hacky_children.size; i++)
  {
    HackyChild* child = (HackyChild*)Array_get(&g_hacky_children, i);
    if (child != NULL && child->pid == 0 &&
        !Pollable_valid(&child->console[0]) &&
        !Pollable_valid(&child->console[1]) &&
        !Pollable_valid(&child->console[2]))
    {
      Buffer* input1 = &child->console[1].input;
      Buffer* input2 = &child->console[2].input;

      spew(3, "Sending %d+%d hacky_execute bytes.",
           input1->size, input2->size);

      reply_hacky_execute(child->rpc, child->status,
                          input1->data, input1->size,
                          input2->data, input2->size);

      for (int cfd = 0; cfd <= 2; cfd++)
        Pollable_destroy(&child->console[cfd]);

      free(child);

      Array_set(&g_hacky_children, i, NULL);
    }
  }

  // See "handle_pending_signal".
  if (g_want_quit)
    handle_command_quit();
}


#ifndef __tile__

static void
handle_simulator_listener(Pollable* pollable)
{
  spew(3, "%s accepting connection.", pollable->name);

  int fd = simple_accept(pollable->fd);

  Pollable_close(pollable);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  Pollable* socket = (Pollable*)pollable->info;
  Pollable_open(socket, fd, handle_shepherd_socket);

  spew(2, "%s accepted connection.", pollable->name);
}


static uint16_t
spawn_simulator_listen(Pollable* listener, Pollable* socket, const char* who)
{
  uint16_t port = 0;
  Pollable_init(listener, "%s listener", who);
  int fd = simple_listen(&port, 1);
  spew(3, "Listening for %s on port %u.", who, port);
  Pollable_open(listener, fd, handle_simulator_listener);
  listener->info = socket;
  return port;
}


// Spawn a local "shepherd", and accept a connection from it.
// Also accept a "reporter" connection if appropriate.
//
static void
spawn_simulator(char** simulator_argv, bool make_pty)
{
  int pty_master;
  int pty_slave;
  if (make_pty)
  {
    // ISSUE: Just use pipes instead of a PTY?
    // NOTE: See "openpty()" comments in "tools/tmc/source/task.c".

    // Create a pty for stdin/stdout.
    struct termios term;
    memset(&term, 0, sizeof(term));
    term.c_cc[VMIN] = 1;
    if (openpty(&pty_master, &pty_slave, NULL, &term, NULL) != 0)
      punt_with_errno("Failure in 'openpty()'");
  }

  Pollable shepherd_listener;
  uint16_t shepherd_port =
    spawn_simulator_listen(&shepherd_listener, &g_shepherd_socket, "Shepherd");

  Pollable watchdog_listener;
  uint16_t watchdog_port =
    spawn_simulator_listen(&watchdog_listener, &g_watchdog_socket, "Watchdog");

  Pollable reporter_listener;
  uint16_t reporter_port =
    spawn_simulator_listen(&reporter_listener, &g_reporter_socket, "Reporter");

  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    // Detach from our controlling terminal, so keyboard signals like SIGINT
    // will not (directly) affect us. Note that "login_tty()" would do this
    // for us, but it would also dup the PTY onto stdin, stdout, AND stderr.
    (void)setsid();

    if (make_pty)
    {
      close_or_die(pty_master);
      dup2_or_die(pty_slave, STDIN_FILENO);
      dup2_and_close_or_die(pty_slave, STDOUT_FILENO);
    }
#if 0
    else
    {
      // Redirect "stdin" from "/dev/null".
      int file = open_or_die("/dev/null", O_RDONLY, 0);
      dup2_and_close_or_die(file, STDIN_FILENO);
    }
#endif

    char* console =
      g_opt_console ? (g_opt_console_out ?: "-") : "/dev/null";

    StringArray argv;
    StringArray_init(&argv);

    // Append prefix_simulator_args.
    for (uint i = 0; i < g_prefix_simulator_args.size; i++)
    {
      char* arg = StringArray_get(&g_prefix_simulator_args, i);
      StringArray_append(&argv, arg);
    }

    // Collect shepherd arguments, applying replacements.
    // ISSUE: Technically, we need a way to escape "@" chars.
    for (char** argp = simulator_argv; *argp; ++argp)
    {
      char *arg = *argp;

      char tmp[2 * PATH_MAX + 100];

      if (has_prefix(arg, "@INSTALL@"))
      {
        char* tail = arg + (sizeof("@INSTALL@") - 1);
        get_install_path(tmp, sizeof(tmp), tail);
        StringArray_append(&argv, strdup_or_die(tmp));
      }
      else if (!strcmp(arg, "@SHEPHERD_PORT@"))
      {
        StringArray_append(&argv, strfmt_or_die("%u", shepherd_port));
      }
      else if (!strcmp(arg, "@WATCHDOG_PORT@"))
      {
        StringArray_append(&argv, strfmt_or_die("%u", watchdog_port));
      }
      else if (!strcmp(arg, "@REPORTER_PORT@"))
      {
        StringArray_append(&argv, strfmt_or_die("%u", reporter_port));
      }
      else if (!strcmp(arg, "@CONFIG@"))
      {
        StringArray_append(&argv, g_opt_config);
      }
      else if (!strcmp(arg, "@BOOT_SHIM@"))
      {
        const char* input = g_opt_raw_mode ? "/dev/null" : g_bootrom_file;
        char* boot_shim = strfmt_or_die("rshim:%s:%s", input, console);
        StringArray_append(&argv, boot_shim);
      }
      else if (!strcmp(arg, "@IMAGE_FILE@"))
      {
        StringArray_append(&argv, g_image_file);
      }
      else if (!strcmp(arg, "@CONSOLE@"))
      {
        StringArray_append(&argv, console);
      }
      else if (!strcmp(arg, "@VMLINUX@"))
      {
        StringArray_append(&argv, g_vmlinux_file);
      }
      else
      {
        StringArray_append(&argv, arg);
      }
    }

    // Append extra_simulator_args.
    for (uint i = 0; i < g_extra_simulator_args.size; i++)
    {
      char* arg = StringArray_get(&g_extra_simulator_args, i);
      StringArray_append(&argv, arg);
    }

    // Terminate.
    StringArray_append(&argv, NULL);

    (void)execv(argv.data[0], argv.data);

    punt_with_errno("Failed to exec '%s'", argv.data[0]);
  }


  // Parent.

  spew(3, "Spawned simulator at pid %d.", pid);

  g_simulator_pid = pid;

  if (make_pty)
  {
    close_or_die(pty_slave);

    set_close_on_exec_or_die(pty_master, true);

    init_console_stdout_handler(pty_master);
  }

  // Wait for connections (or shepherd death).
  while (Pollable_valid(&shepherd_listener) ||
         Pollable_valid(&watchdog_listener) ||
         Pollable_valid(&reporter_listener))
  {
    dispatch_events(-1);

    int status = 0;
    if (waitpid_or_die(g_simulator_pid, &status, WNOHANG) != 0)
      handle_unexpected_simulator_death(status);
  }

  Pollable_destroy(&shepherd_listener);
  Pollable_destroy(&watchdog_listener);
  Pollable_destroy(&reporter_listener);
}

#endif


static void
auto_bt_advance(void)
{
  Process* ready = NULL;

  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    switch (process->auto_bt)
    {
    case AUTO_BT_NONE:
      // Ignore.
      break;
    case AUTO_BT_QUERY:
    case AUTO_BT_ACTIVE:
    case AUTO_BT_DUMPING:
      // Wait for everyone to be ready.
      return;
    case AUTO_BT_READY:
      if (ready == NULL)
        ready = process;
    }
  }

  if (ready != NULL)
  {
    Process* process = ready;
    erase_prompt();
    fprintf(stderr, "=== %s (%s) ===\n",
            process->name, process->executable);
    process->auto_bt = AUTO_BT_ACTIVE;
    request_gdb(process);
    return;
  }

  erase_prompt();
  fprintf(stderr, "=== Backtraces complete ===\n");
}


static void
auto_bt_handle_debug_attach_error(void* info, char* msg)
{
  pid_t pid = (pid_t)(intptr_t)info;

  warn("Cannot backtrace process %d: %s", pid, msg);

  Process* process = find_process_by_pid(pid);
  if (process != NULL)
  {
    process->auto_bt = AUTO_BT_NONE;
    auto_bt_advance();
  }
}


static void
handle_pending_signal_sigquit_pci(void)
{
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/dev/tile%s/debug/char_stream",
           g_dev_name + 1);

  fprintf(stderr, "=== %s ===\n", path);

  char cmd[PATH_MAX];
  int size = snprintf(cmd, sizeof(cmd), "grep -v ': 0$' %s 1>&2", path);
  if (size < sizeof(cmd))
    (void)system(cmd);
}


static void
handle_pending_signal_sigquit(void)
{
  static bool busy;

  if (g_simulator_pid != 0)
  {
    // ISSUE: Just send a special sim command?
    warn("Passing SIGQUIT to simulator.");
    kill(g_simulator_pid, SIGQUIT);
  }
  else
  {
    //--erase_prompt();

    warn("Handling SIGQUIT on '%s'.", fullhostname_or_die());

    if (!g_greeted)
    {
      warn("Cannot ping or backtrace before greeting shepherd.");
      if (g_dev_is_pci)
        handle_pending_signal_sigquit_pci();
      return;
    }

    bool oops = busy;
    for (uint k = 0; k < g_processes.size; k++)
    {
      Process* process = Array_get(&g_processes, k);
      oops = oops || (process->auto_bt != AUTO_BT_NONE);
    }
    if (oops)
    {
      warn("Ignoring SIGQUIT while handling SIGQUIT.");
      return;
    }

    busy = true;

    note("Making sure the shepherd is still alive...");

    // HACK: Ping the shepherd, with a five second timeout, preserving
    // "g_waiting" to avoid interrupting any synchronous command which
    // is currently in progress.
    bool old_waiting = g_waiting;
    bool pinged = querify_with_timeout(ask_ping(&g_shepherd_socket), 5000);
    g_waiting = old_waiting;

    // Do this after "ping" for optimal results.
    dump_monitor_state();

    if (!pinged)
    {
      warn("The shepherd appears to be non-responsive!");

      // Dump out some PCI diagnostics.
      if (g_dev_is_pci)
        handle_pending_signal_sigquit_pci();

      busy = false;
      return;
    }

    note("Asking the shepherd for backtraces...");

    // Backtrace processes.
    for (uint k = 0; k < g_processes.size; k++)
    {
      Process* process = Array_get(&g_processes, k);

      if (process->debugger != NULL)
      {
        spew(2, "%s (%s) is already being debugged.",
             process->name, process->executable);
        continue;
      }

      process->auto_bt = AUTO_BT_QUERY;
      pid_t pid = process->pid;
      query_debug_attach(&g_shepherd_socket,
                         NULL, NULL,
                         auto_bt_handle_debug_attach_error,
                         (void*)(intptr_t)pid,
                         pid);
    }

    busy = false;

    auto_bt_advance();
  }
}


// Handle the "signal" Alarm.
//
static void
handle_alarm_for_signal(Alarm* alarm)
{
  snprintf(g_exit_msg, sizeof(g_exit_msg),
           "This normally indicates that a launched process"
           " is hung and refuses to exit,\n"
           "but might also indicate a chip wedge, kernel bug,"
           " or 'shepherd' crash.\n\n");
  punt("The shepherd refused to exit!");
}


// Handle a signal "safely", during "dispatch_events()".
//
// Handle various signals. It is dangerous to do anything interesting
// inside a signal handler, especially things that affect global state,
// including Pollables, so normally we just set special flags which
// indicate our desire to do something later, in the main event loop.
//
static void
handle_pending_signal(int sig)
{
  // Handle SIGCHLD silently.
  if (sig == SIGCHLD)
    return;

  // Handle SIGUSR2 by becoming interactive.
  if (sig == SIGUSR2)
  {
    warn("Handling SIGUSR2 by requesting interactive mode.");

    become_interactive();
  }

  // Handle SIGQUIT by dumping some "state".
  else if (sig == SIGQUIT)
  {
    handle_pending_signal_sigquit();
  }

  // Handle most other signals by setting "g_want_quit", so we will try to
  // exit cleanly, and scheduling an alarm to make sure we will exit after
  // ten seconds have passed, in case the shepherd (or the chip) is wedged.
  // Note that this should be plenty of time for "booting" to complete, so
  // we don't have to worry about confusing the PCI card.
  //
  // However, SIGINT is normally used to interrupt code which performs a
  // long "synchronous" operation (often by calling "dispatch_events()" or
  // "handle_events()" to wait for some external event).  Note that
  // this does not actually stop the operation, but makes it asynchronous,
  // by giving the user a prompt.
  //
  // NOTE: Synchronous operations should thus explicitly check "g_waiting".
  //
  else
  {
    bool old_waiting = g_waiting;

    // Stop waiting.
    g_waiting = false;

    // Count signals.
    g_signal_count++;

    // Normally SIGINT is handled as an interrupt (or passed to a
    // "local gdb"), and if not, it is ignored, to prevent annoying
    // race conditions involving an attempted interrupt.
    if (sig == SIGINT && g_running && g_signal_count == 1)
    {
      Debugger* debugger = find_debugger_for_stdin();

      if (old_waiting)
      {
        warn("Handling SIGINT as an interrupt.");
        g_signal_count = 0;
      }
      else if (debugger != NULL && debugger->local_gdb_pid >= 0)
      {
        // Forward SIGINT to any local gdb.
        warn("%s is being sent a SIGINT.", debugger->name);
        kill(debugger->local_gdb_pid, SIGINT);
        g_signal_count = 0;

        // A SIGINT acts much like a gdb command.
        debugger->local_gdb_stream.partial = false;
        debugger->local_gdb_expecting_prompt = true;
      }
      else
      {
        warn("Ignoring SIGINT (try again to quit).");
      }

      return;
    }

    if (g_signal_count >= 3 || g_signal_immediate)
    {
      g_exit_msg[0] = '\0';
      punt("Handling %s by exiting immediately.", signal_name(sig));
    }

    if (g_signal_count >= 2)
      warn("Handling %s by quitting (try again to exit).", signal_name(sig));
    else
      warn("Handling %s by quitting.", signal_name(sig));

    // Indicate signal.
    if (g_exit_code == 0)
      g_exit_code = 128 + sig;

    // Stop processing command line commands.
    g_stop_processing_commands = true;

    // Try to quit, and punt if it takes too long.
    g_want_quit = true;
    static Alarm signal_alarm;
    if (!Alarm_scheduled(&signal_alarm))
    {
      // Basically "now + 10s".
      signal_alarm.when.tv_sec = time(NULL) + 10;
      signal_alarm.handler = &handle_alarm_for_signal;
      Alarm_schedule(&signal_alarm);
    }
  }
}


// Define the setgid program to run to handle setting up lockfiles.
#define LOCKDEV "/usr/sbin/lockdev"

// Try to lock the specified device, if possible.  If there is no
// locking support on the system, or if the device doesn't exist or is
// an unreadable symbolic link, the function returns zero; if it
// successfully locks the device, it sets "*unlock_name_p" to a
// strdup'ed name to use for unlocking, and returns "1".
// If the device is locked, it calls punt().
//
static int
lock_console_device(const char* in_device, char** unlock_name_p)
{
  // Is lockdev installed on this system?
  if (access(LOCKDEV, X_OK) != 0)
    return 0;

  // Dereference any symlinks, etc.
  char *device = realpath(in_device, NULL);
  if (device == NULL)
    return 0;

  // Create and run the command for lockdev.
  // It identifies the process based on the parent pid of lockdev;
  // as it happens, system("foo") does run "foo" as a direct child.
  char buf[PATH_MAX + sizeof(LOCKDEV) + 8];
  snprintf(buf, sizeof(buf), "%s -l \"%s\"", LOCKDEV, device);
  int rc = system(buf);

  // If locking fails, treat it as fatal, unless we used --try-console.
  if (rc != 0)
  {
    if (g_opt_try_console)
    {
      warn("Another process has locked the console device (%s).", device);
      *unlock_name_p = NULL;
      return 1;
    }
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "Another process has locked the console device"
             " (%s).\n"
             "Exit from the other process, or remove the"
             " --console option from tile-monitor.\n",
             device);
    punt("Could not lock '%s'!", device);
  }

  // Record the filename for later unlocking.  If we fail to unlock
  // (e.g. a crash or kill -9), a later locker can deal with it properly,
  // but it's better to clean up after ourselves.
  *unlock_name_p = device;
  return 1;
}

// Unlock the specified name.
static void
unlock_console_device(const char* device)
{
  char buf[PATH_MAX + sizeof(LOCKDEV) + 8];
  snprintf(buf, sizeof(buf), "%s -u \"%s\"", LOCKDEV, device);
  if (system(buf))
    warn("Error unlocking console device %s!\n", device);
}


// HACK: Prevent wackiness after "fork()".
//
static pid_t g_handle_exit_pid;


static void
handle_exit(void)
{
  if (g_handle_exit_pid != getpid())
    return;

  if (g_exit_msg[0] != '\0')
  {
    fprintf(stderr, "\n%s", g_exit_msg);
  }

  if (g_exit_msg[0] != '\0' || g_display_host_at_exit)
  {
    // When testing, report the hostname.
    if (g_testing)
    {
      fprintf(stderr, "The current hostname is '%s'.\n\n",
              fullhostname_or_die());
    }
  }

  if (g_kill_pid_at_exit != 0)
    kill(g_kill_pid_at_exit, SIGTERM);

  cancel_console_stdout_handler();

  if (g_unlock_console)
    unlock_console_device(g_unlock_console);

  if (g_unlink_bootrom != NULL)
    (void)unlink(g_unlink_bootrom);

  if (g_readline_ready)
    rl_callback_handler_remove();

  // ISSUE: This should NOT be needed for BMC.
  if ((g_dev_devdir != NULL || g_dev_bmc_host != NULL) && g_greeted)
  {
    // HACK: Simulate "close".
    do_request_close(&g_shepherd_socket);

    // FIXME: This needs a timeout!
    Pollable_flush_fully(&g_shepherd_socket);
  }

#if 0
  // Clean up if running "valgrind".
  if (getenv("VALGRIND") == NULL)
    return;

  Pollable_destroy(&g_console);
  Pollable_destroy(&g_shepherd_socket);
  Pollable_destroy(&g_watchdog_socket);
  Pollable_destroy(&g_reporter_socket);
  Pollable_destroy(&g_ide_socket);
  Pollable_destroy(&g_external_command_socket);
  Pollable_destroy(&g_stdin_reader);

  Buffer_destroy(&g_prompt_buffer);

  free(g_shepherd_cwd);
  free(g_monitor_cwd);
  free(g_hv_bin_dir);
  free(g_classifier_file);
  free(g_vmlinux_file);
  free(g_bootrom_file);
  free(g_profile_start_flags);
  free(g_profile_start_kernel);
  free(g_profile_start_events);
  free(g_target_info);
#endif
}



// HACK: Simplify argument verification.
//
#define verify_arg(A, C) \
  do { if (!(C)) punt("Insufficient args for '%s'.", (A)); } while (0)


// Allow at most one of "--config", "--image", "--dev", etc.

static const char* primary_arg = NULL;

static void
verify_primary_arg(const char* arg)
{
  if (primary_arg != NULL)
    punt("Cannot combine '%s' and '%s'.", primary_arg, arg);
  primary_arg = arg;
}


// Allow at most one of "--image", "--image-file", and "--bootrom-file",
// and forbid the use of "--hvc" with these options.

static const char* secondary_arg = NULL;

static void
verify_secondary_arg(const char* arg)
{
  if (secondary_arg != NULL)
    punt("Cannot combine '%s' and '%s'.", secondary_arg, arg);
  secondary_arg = arg;
}


// Process the command at index "i", and return the new value of "i".
// Note that each complete command is followed by a literal NULL.
//
static int
process_command(StringArray* args, int i)
{
  StringArray scratch;
  StringArray_init(&scratch);

  while (true)
  {
    // ISSUE: Sometimes "arg" is "read only".
    char* arg = StringArray_get(args, i++);

    if (arg == NULL)
      break;

    StringArray_append(&scratch, arg);
  }

  if (g_stop_processing_commands)
  {
    Buffer msg;
    Buffer_init(&msg);
    for (int i = 0; i < scratch.size; i++)
      Buffer_printf(&msg, " %s", StringArray_get(&scratch, i));
    warn("Ignoring command '%s'.", msg.data + 1);
    Buffer_destroy(&msg);
  }
  else
  {
    handle_command(&scratch);
  }

  StringArray_destroy(&scratch);

  return i;
}



// Look up the given key in the given array of "key=val" defs, and return
// the value, or punt mentioning "path" if it is non-NULL, or return NULL.
//
static char*
defs_lookup(StringArray* defs, const char* key, const char* path)
{
  int len = strlen(key);

  for (int i = 0; i < defs->size; i++)
  {
    char* pair = StringArray_get(defs, i);
    if (has_prefix(pair, key) && pair[len] == '=')
      return pair + len + 1;
  }

  if (path != NULL)
    punt("Cannot find '%s' in '%s'.", key, path);

  return NULL;
}


// Open a new connection to the tilemon_proxy, set blocking mode, send
// the given "cmd", read back the "ACK" handshake, and return the fd,
// or die.
// 
// If "err" is non-NULL, and five characters long, and the proxy sends
// back "NAK", plus "err", then close the connection, and return -1,
// instead of dying.
//
static int
tilemon_proxy_helper(const char* cmd, const char* err)
{
  // Connect to the tilemon_proxy.
  int fd = simple_connect_aux(g_dev_bmc_host, g_dev_bmc_port);
  if (fd < 0)
    punt("BMC '%s' tilemon_proxy unavailable!", g_dev_name);

  // Send 'info' query.
  set_blocking_or_die(fd, true);
  write_all_bytes_or_die(fd, cmd, strlen(cmd));

  // Verify 'info' reply.
  char ack[4];
  bzero(ack, sizeof(ack));
  if (read_some_bytes_or_die(fd, ack, 3) != 3)
    punt("BMC '%s' tilemon_proxy ignored '%s'!", g_dev_name, cmd);

  if (memcmp(ack, "ACK", 3) == 0)
    return fd;

  if (memcmp(ack, "NAK", 3) != 0)
    punt("BMC '%s' tilemon_proxy had unexpected reply '%s' for '%s'!",
         g_dev_name, ack, cmd);

  if (err != NULL && strlen(err) == 5)
  {
    char msg[6];
    bzero(msg, sizeof(msg));
    if (read_some_bytes_or_die(fd, msg, 5) <= 0 ||
        memcmp(msg, err, 5) != 0)
    {
      // Probably usb driver issue on BMC.
      punt("BMC '%s' tilemon_proxy had unexpected failure '%s' for '%s'.",
           g_dev_name, msg, cmd);
    }
    close(fd);
    return -1;
  }

  punt("BMC '%s' tilemon_proxy rejected '%s'!", g_dev_name, cmd);
}


// Make sure that the card exists, and that the driver is up to date, and
// extract all the key/value pairs in the "info" file, and verify that
// the chip version is correct, and initialize "g_width" and "g_height".
//
// For PCI, we added the "info" file in MDE 2.0.0, with "CHIP_VERSION",
// "CHIP_WIDTH", "CHIP_HEIGHT", "BOARD_HAS_PLX", "BOARD_LINKS", and
// "HOST_LINK_INDEX" all supported in the official MDE 2.0.0 release.
//
// For USB, we added the "info" file in MDE 4.0, with "CHIP_VERSION",
// "CHIP_WIDTH", and "CHIP_HEIGHT".
//
// We assume that every line in the "info" file is a space separated
// key/value pair, with no spaces in the key or the value.  As of
// MDE 2.0.0, all of the values are single digit non-negative integers,
// and in MDE 4.0, some multi-digit non-negative integers are possible.
//
// NOTE: This is also used for BMC.
//
static void
verify_devdir(StringArray* defs)
{
  char path[PATH_MAX];
  FILE* fp = NULL;

  if (g_dev_devdir != NULL)
  {
    get_install_path(path, sizeof(path), "/bin");

    char more_info[PATH_MAX + 1024];
    snprintf(more_info, sizeof(more_info),
             "For more information about installing the TILE system "
             "and the '%s' driver,\n"
             "see the Tilera MDE Getting Started Guide, the "
             "User's Guide for your system,\n"
             "and/or '%s/install-drivers'.\n\n", g_dev_driver, path);

    struct stat sbuf;
    if (stat(g_dev_devdir, &sbuf) != 0 || !S_ISDIR(sbuf.st_mode))
    {
      snprintf(g_exit_msg, sizeof(g_exit_msg),
               "This normally indicates that the '%s' driver "
               "(or the TILE system itself)\n"
               "on this machine has not been installed properly.\n"
               "\n%s", g_dev_driver, more_info);
      punt("Cannot access '%s/'.", g_dev_devdir);
    }

    // Describe any problems involving the "info" file.
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that the '%s' driver "
             "on this machine is too old,\n"
             "and must be updated.\n"
             "\n%s", g_dev_driver, more_info);

    snprintf(path, sizeof(path), "%s/info", g_dev_devdir);

    fp = fopen(path, "r");
    if (fp == NULL)
      punt("Cannot open '%s'.", path);
  }

  else
  {
    // Initiate an 'info' tilemon query.
    int fd = tilemon_proxy_helper("TILE-MONITOR-INFO", NULL);

    // Prepare to read info from the socket.
    fp = fdopen(fd, "r");
  }

  StringArray args;
  StringArray_init(&args);

  char tmp[1024];
  while (fgets(tmp, sizeof(tmp), fp) != NULL)
  {
    if (tokenize(&args, tmp) != 2)
      punt("Cannot parse '%s'.", path);

    char* key = StringArray_get(&args, 0);
    char* val = StringArray_get(&args, 1);

    StringArray_append(defs, strfmt_or_die("%s=%s", key, val));

    StringArray_clear(&args);
  }

  StringArray_destroy(&args);

  fclose(fp);

  // Acquire, and analyze, various keys.
  int chip_version = atoi_or_die(defs_lookup(defs, "CHIP_VERSION", path));
  g_width = atoi_or_die(defs_lookup(defs, "CHIP_WIDTH", path));
  g_height = atoi_or_die(defs_lookup(defs, "CHIP_HEIGHT", path));

  if (g_dev_is_pci)
  {
#if TILE_CHIP < 10
    // Acquire the "BOARD_HAS_PLX" key.
    (void)defs_lookup(defs, "BOARD_HAS_PLX", path);
#else
    g_pci_link_index = defs_lookup(defs, "HOST_LINK_INDEX", path);

    // Acquire the auto configuration flag of the PCIe P2P NIC interface.
    g_pci_nic_config =
      atoi(defs_lookup(defs, "HOST_P2P_VNIC_AUTO_CONFIG", path));

    // Acquire the IP address of the PCIe P2P NIC interface.
    // Note that by calling defs_lookup(), we always get the 1st NIC interface
    // back, i.e. tilep2pN.
    if (g_pci_nic_config)
	   g_pci_nic_addr = defs_lookup(defs, "REM_NETWORK_ADDR", path);
#endif
  }

  g_exit_msg[0] = '\0';

  // Verify "CHIP_VERSION".
  if (chip_version != TILE_CHIP)
  {
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that the version of"
             " 'tile-monitor' which is being used\n"
             "is incompatible with the chosen TILE system.\n\n");
    punt("Incompatible CHIP_VERSION in '%s'.", path);
  }
}


// Handle failure to open a critical "PCI" file.
//
// HACK: If "TILERA_PCI_PANIC" is defined, run it.
//
static void
devdir_panic(const char* path)
{
  warn_with_errno("Failure in 'open(\"%s\")'", path);

  if (g_dev_is_pci)
  {
#if TILE_CHIP < 10
    fprintf(stderr, "=== /proc/driver/tilepci_last_err on %s ===\n",
            fullhostname_or_die());
    (void)system("cat /proc/driver/tilepci_last_err 1>&2");
#else
    if (errno == EPERM)
      warn("Incompatible PCI drivers detected, "
           "run 'dmesg | grep tilegxpci' for details.");
#endif

    // ISSUE: Apply to USB as well?
    const char* panic = getenv("TILERA_PCI_PANIC");
    if (panic != NULL)
      (void)system(panic);
  }

  punt("Failure in 'open(\"%s\")'.", path);
}


// FIXME: Make a special error message for "net" too.
//
static void
open_console_device(const char* device)
{
  spew(3, "Opening console device '%s'.", device);

  int rc = lock_console_device(device, &g_unlock_console);

  // If g_opt_try_console made us return after failure, just give up.
  if (rc && g_unlock_console == NULL)
    return;

  if (g_dev_devdir != NULL)
  {
    char path[PATH_MAX];

    get_install_path(path, sizeof(path), "/bin");

    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that the driver installer"
             " script was not run with\n"
             "an appropriate serial device, or that '%s'\n"
             "is not configured properly.\n\n"
             "For more information about installing the TILE system"
             " and the '%s' driver,\n"
             "including specifying the serial device which is"
             " associated with each system,\n"
             "see the Tilera MDE Getting Started Guide, the"
             " User's Guide for your TILE system,\n"
             "and/or '%s/install-drivers'.\n\n",
             device, g_dev_driver, path);
  }
  else
  {
    snprintf(g_exit_msg, sizeof(g_exit_msg),
             "This normally indicates that the device '%s'\n"
             "is not configured properly.\n", device);
  }

  int fd = open_boldly(device, O_RDWR, 0);
  if (fd < 0)
  {
    if (g_opt_try_console)
    {
      warn("Couldn't open specified console (%s).", device);
      return;
    }
    punt_with_errno("Failure in 'open(\"%s\")'", device);
  }

  struct termios term;

#if 0
  if (tcgetattr(fd, &term) != 0)
    punt_with_errno("Failure in 'tcgetattr()'");

  warn("Old attrs 0x%x 0x%x 0x%x 0x%x.",
       term.c_iflag, term.c_oflag, term.c_cflag, term.c_lflag);

#if 0
  // NOTE: Previous values.
  term.c_iflag = IXON | ICRNL;
  term.c_oflag = OPOST | ONLCR;
  term.c_cflag = B115200 | CS8 | CLOCAL | CREAD | HUPCL | PARENB | CSTOPB;
  term.c_lflag = ISIG | ICANON | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
#endif

#endif

  memset(&term, 0, sizeof(term));

  term.c_iflag = IGNBRK | IGNPAR;
  //--term.c_oflag = 0;
  term.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
#ifdef IBSHIFT
  term.c_cflag |= (B115200 << IBSHIFT);
#endif
  //--term.c_lflag = 0;
  term.c_cc[VMIN] = 1;

  if (tcsetattr(fd, TCSAFLUSH, &term) != 0)
    punt_with_errno("Failure in 'tcsetattr()'");

  g_exit_msg[0] = '\0';

  set_close_on_exec_or_die(fd, true);

  // The standard "upstart" login prompt.
  // ISSUE: You can also do an "upstart" boot over USB/PCI/BMC.
  // FIXME: Should just check for any line ending in "login: ".
  if (g_dev_name != NULL && g_dev_name[0] == '@' && g_dev_bmc_host == NULL)
  {
    StringArray_append(&g_console_stream.prompts,
                       strfmt_or_die("%s login: ", g_dev_name + 1));
  }

#if 0
  // The standard "upstart" login prompt (after failing).
  StringArray_append(&g_console_stream.prompts, strdup_or_die("login: "));
#endif

  // The standard "upstart" shell prompt.
  StringArray_append(&g_console_stream.prompts, strdup_or_die("-sh-4.1# "));

  // Unless "TLR_INTERACTIVE=y", this is pointless, but harmless.
  StringArray_append(&g_console_stream.prompts, strdup_or_die("# "));

  init_console_stdout_handler(fd);
}

//! Fork off a child and redirect the stdin and stdout to the socket
//! of the socketpair.
//! @param argv child argv.
//! @param fdp pointer to returned socket file descriptor.
//! @return pid pid of the child process.
static pid_t
socketed_child(char* argv[], int* fdp)
{
  int fds[2];

  socketpair_or_die(fds);
  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    close_or_die(fds[1]);
    set_blocking_or_die(fds[0], false);
    set_keep_alive_or_die(fds[0], true);
    set_close_on_exec_or_die(fds[0], true);
    dup2_or_die(fds[0], STDIN_FILENO);
    dup2_or_die(fds[0], STDOUT_FILENO);
    close_or_die(fds[0]);

    execvp(argv[0], argv);
    punt_with_errno("Failure in 'execvp(%s)'", argv[0]);
  }

  // Parent.

  close_or_die(fds[0]);
  set_blocking_or_die(fds[1], false);
  set_keep_alive_or_die(fds[1], true);
  set_close_on_exec_or_die(fds[1], true);
  *fdp = fds[1];

  return pid;
}


//! Start shepherd on the remote host over ssh tunnel.
//! @param user user name to log in to the remote host, or NULL if default.
//! @param addr address or hostname of the remote host.
//! @param fdp pointer to returned socket file descriptor.
//! @return the pid of the child ssh process.
static int
shepherd_over_ssh(char* user, char* addr, int* fdp)
{
  char* argv[7];

  int i = 0;
  argv[i++] = "ssh";
  if (user)
  {
    argv[i++] = "-l";
    argv[i++] = user;
  }
  argv[i++] = addr;
  argv[i++] = "/bin/shepherd";
  argv[i++] = "--ssh";
  argv[i++] = NULL;

  return socketed_child(argv, fdp);
}


static void
connect_shepherd(const char* who, uint16_t shepherd_port, int do_warn)
{
  int fd;

  spew(2, "Connecting to shepherd at '%s:%d'...", who, shepherd_port);

  setup_greet_alarm_if_needed();
  for (int try = 1; true; try++)
  {
    fd = simple_connect_aux(who, shepherd_port);
    if (fd >= 0)
      break;

    // ISSUE: The network now comes up BEFORE the shepherd is launched,
    // and in an upstart boot, 25 seconds pass between these moments,
    // so we only warn every five seconds.
    if (do_warn && (try % 5) == 0)
      warn("Trying to connect to shepherd at '%s:%d'...", who, shepherd_port);

    dispatch_events(1000);
   }

  spew(2, "Connected to shepherd at '%s:%d'...", who, shepherd_port);

  set_keep_alive_or_die(fd, true);
  set_close_on_exec_or_die(fd, true);

  Pollable_open(&g_shepherd_socket, fd, handle_shepherd_socket);
}


static int
verify_bootrom(const char* path)
{
  snprintf(g_exit_msg, sizeof(g_exit_msg),
           "This normally indicates that the bootrom file"
           " is corrupt, or that you are\n"
           "running a version of 'tile-monitor' which is not"
           " compatible with the bootrom\n"
           "file ('%s').\n\n", g_bootrom_file);

  int fd1 = open_or_die(path, O_RDONLY, 0);

#if TILE_CHIP >= 10
  // Verify bytes 40 thru 55 of the bootrom file.
  uint8_t bytes[16];
  static const uint8_t bootrom_bytes[16] = {
    0x00, 0x02, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xaf, 0x00, 0x90, 0x01, 0x00, 0x00, 0x24
  };
  if (pread(fd1, bytes, 16, 40) != 16)
    punt("Invalid size in bootrom file '%s'.", g_bootrom_file);
  for (int i = 0; i < sizeof(bytes); i++)
  {
    if (bytes[i] != bootrom_bytes[i] && i != 9)
      punt("Invalid header in bootrom file '%s'.", g_bootrom_file);
  }
  if (bytes[9] != 0x0f + (TILE_CHIP << 4))
    punt("Invalid chip version in bootrom file '%s'.", g_bootrom_file);
#else
  // Verify bytes 20 thru 31 of the bootrom file.
  uint8_t bytes[12];
  static const uint8_t bootrom_bytes[12] = {
    0x00, 0x02, 0x00, 0x80, 0xff, 0x0f, 0x00, 0x20, 0x00, 0x38, 0x00, 0x50
  };
  if (pread(fd1, bytes, 12, 20) != 12)
    punt("Invalid size in bootrom file '%s'.", g_bootrom_file);
  for (int i = 0; i < sizeof(bytes); i++)
  {
    if (bytes[i] != bootrom_bytes[i] && i != 5)
      punt("Invalid header in bootrom file '%s'.", g_bootrom_file);
  }
  if (bytes[5] != 0x0f + (TILE_CHIP << 4))
    punt("Invalid chip version in bootrom file '%s'.", g_bootrom_file);
#endif

  g_exit_msg[0] = '\0';

  return fd1;
}

int
main(int argc, char* argv[])
{
  char path[PATH_MAX];
  char temp[PATH_MAX];

  // Use line buffered stderr.
  setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

  message_prefix = "[monitor] ";

  // Prepare to handle signals safely.
  dispatch_events_expect_signals(handle_pending_signal);

  // Handle some "simple" signals.
  if (signal(SIGCHLD, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGQUIT, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGUSR2, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGPIPE, SIG_IGN) == SIG_ERR)
  {
    punt("Failed to register signal handlers.");
  }

  // Handle some "scary" signals.
  if (signal(SIGINT, handle_early_signal) == SIG_ERR ||
      signal(SIGTERM, handle_early_signal) == SIG_ERR ||
      signal(SIGHUP, handle_early_signal) == SIG_ERR)
  {
    punt("Failed to register signal handlers.");
  }

  // Clean up at exit.
  // ISSUE: We assume that "punt()" calls "exit()" not "abort()".
  // If this is not true, then we also need "atpunt()", or some such.
  g_handle_exit_pid = getpid();
  if (atexit(handle_exit) != 0)
    punt("Failed to register atexit handler.");

  // Avoid conflicting with "ide" tags.
  rpc_set_tag_range(0x8000, 0xFFFF);

  // Determine the current working directory.
  update_monitor_cwd();

  // These are set by profile-init based on the tool selected.
  g_profile_start_kernel = strdup_or_die("");
  g_profile_start_events = strdup_or_die("");
  g_profile_start_flags  = strdup_or_die("");

  // See also "--console-out".
  ConsoleStream_init(&g_console_stream, STDOUT_FILENO);

  // Handle shepherd stdout/stderr (on hardware only).
  ConsoleStream_init(&g_shepherd_stdout_stream, STDOUT_FILENO);
  ConsoleStream_init(&g_shepherd_stderr_stream, STDERR_FILENO);

  Pollable_init(&g_console, "Console");

  Pollable_init(&g_shepherd_socket, "Shepherd socket");
  Pollable_init(&g_watchdog_socket, "Watchdog socket");
  Pollable_init(&g_reporter_socket, "Reporter socket");

  Pollable_init(&g_peer_socket, "Monitor peer");

  Pollable_init(&g_ide_socket, "IDE socket");

  Pollable_init(&g_external_command_socket, "Command socket");

  Pollable_init(&g_stdin_reader, "Monitor stdin");

  // HACK: Most using a TTY want "readline", but it does not work well
  // under "emacs shell", which sets "TERM" to "dumb".
  char* term = getenv("TERM");
  g_readline_desired =
    isatty(STDIN_FILENO) && (term == NULL || strcmp(term, "dumb") != 0);

  // Check for IDE-specific environment variable settings.
#ifndef __tile__
  char* opt_ide_port = getenv("TILERA_IDE_PORT");
  char* opt_ide_launch_id = getenv("TILERA_IDE_LAUNCH_ID");
#endif

  // External command port number, if specified by the user.
  char* opt_external_command_port = NULL;

  bool opt_simulator = false;

  // ISSUE: Rename this?
  g_opt_resume = (getenv("TILERA_PCI_RESUME") != NULL);

  char* opt_dev = NULL;

  const char* dev_stream = "0";

#if TILE_CHIP < 10
  const char* opt_pci_link = NULL;
#endif

  bool opt_lockless = false;

  bool opt_prepare_reboot = false;

  bool opt_boot_only = false;

  bool opt_reflash = false;

  bool opt_no_dev = false;

  char* opt_create_hvc = NULL;
  char* opt_create_bootrom = NULL;

#ifndef __tile__
  char* opt_create_image = NULL;

  bool opt_magic_hypervisor = false;

  char** simulator_argv = NULL;
#endif

  bool opt_console_raw = false;

  const char* opt_console_device = NULL;

  bool opt_batch_mode = false;

  bool opt_no_rtc = false;

  uint16_t shepherd_port = 963;

  int num_hvc = 0;

  StringArray mkboot_defs;
  StringArray_init(&mkboot_defs);

  StringArray mkboot_args;
  StringArray_init(&mkboot_args);

  StringArray commands;
  StringArray_init(&commands);

#if TILE_CHIP >= 10
  // Default "--classifier".
#ifdef __tile__
  g_classifier_file = strdup_or_die("/boot/classifier");
#else
  get_install_path(path, sizeof(path), "/tile/boot/classifier");
  g_classifier_file = strdup_or_die(path);
#endif
#endif

  // Default "--hv-bin-dir".
#ifdef __tile__
  g_hv_bin_dir = strdup_or_die("/boot");
  g_boot_dir = strdup_or_die("/boot");
#else
  get_install_path(path, sizeof(path), "/tile/boot");
  g_hv_bin_dir = strdup_or_die(path);
  g_boot_dir = strdup_or_die(path);
#endif

  // Directory for simulator images.
  get_install_path(path, sizeof(path), "/lib/boot");
  char* lib_boot = strdup_or_die(path);

  // Directory for hypervisor configuration files.
#ifdef __tile__
  char* tile_etc_hvc = strdup_or_die("/etc/hvc");
#else
  get_install_path(path, sizeof(path), "/tile/etc/hvc");
  char* tile_etc_hvc = strdup_or_die(path);
#endif

  // Initialize shepherd's CWD.
  g_shepherd_cwd = strdup_or_die("/");

  // React to successful file uploads.
  upload_callback = handle_command_symbol_map;

  // Process argc/argv.
  int i = 1;
  while (i < argc)
  {
    char* arg = argv[i++];

    // ISSUE: The original plan was for "tile-monitor exe arg" to act
    // just like "tile-monitor -- exe arg", but bug 4861 requests that
    // the "--" be required, to help catch certain kinds of user error.

    if (!has_prefix(arg, "-"))
    {
      if (!strcmp(argv[i - 2], "--console"))
        punt("Option '--console' has been replaced by '--console-out'.");

      punt("Unexpected positional argument '%s'.", arg);
    }

    else if (!strcmp(arg, "--"))
    {
      // Remaining arguments indicate a program to spawn, and its arguments.
      break;
    }


    else if (!strcmp(arg, "--verbose"))
    {
      message_verbosity++;
    }


    else if (!strcmp(arg, "--message-prefix"))
    {
      // NOTE: Undocumented experimental option.
      verify_arg(arg, i + 1 <= argc);
      message_prefix = argv[i++];
    }

    else if (!strcmp(arg, "--external-commands"))
    {
      opt_external_command_port = ""; // Automatically pick a port.
    }

    else if (!strcmp(arg, "--external-command-port"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_external_command_port = argv[i++];
    }

#ifndef __tile__
    else if (!strcmp(arg, "--ide-port"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_ide_port = argv[i++];
    }


    else if (!strcmp(arg, "--config"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      g_opt_config = argv[i++];
    }


    else if (!strcmp(arg, "--image"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      verify_secondary_arg(arg);
      char* arg1 = argv[i++];
      // FIXME: Delete this.
      if (!strcmp(arg1, "gx36"))
      {
        warn("Please use 'gx8036', not 'gx36'.");
        arg1 = "gx8036";
      }
      if (strchr(arg1, '/') != NULL)
        punt("Argument to '--image' may not contain slashes.");
      g_image_file = strfmt_or_die("%s/%s.image", lib_boot, arg1);
    }

    else if (!strcmp(arg, "--image-file"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      verify_secondary_arg(arg);
      g_image_file = argv[i++];
    }
#endif

    else if (!strcmp(arg, "--bootrom-file"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_secondary_arg(arg);
      char* arg1 = argv[i++];
      g_bootrom_file = strdup_or_die(arg1);
    }


#ifndef __tile__
    else if (!strcmp(arg, "--raw-mode"))
    {
      // NOTE: Undocumented experimental option.

      if (g_opt_config == NULL)
        punt("Must use '--config' before '%s'.", arg);

      g_opt_raw_mode = true;
    }


    else if (!strcmp(arg, "--magic-hypervisor"))
    {
      // NOTE: Undocumented internal option.

      if (g_opt_config == NULL)
        punt("Must use '--config' before '%s'.", arg);

      opt_magic_hypervisor = true;
    }
#endif


    else if (!strcmp(arg, "--hvc"))
    {
      num_hvc++;

      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];

      StringArray_append(&mkboot_args, arg);
      StringArray_append(&mkboot_args, arg1);
    }


    else if (!strcmp(arg, "--hvh"))
    {
      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];

      StringArray_append(&mkboot_args, "--hvc");
      StringArray_append(&mkboot_args, arg1);
    }


    else if (!strcmp(arg, "--hvd") ||
             !strcmp(arg, "--hvi") ||
             !strcmp(arg, "--hvx"))
    {
      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];

      StringArray_append(&mkboot_args, arg);
      StringArray_append(&mkboot_args, arg1);
    }


    else if (!strcmp(arg, "--rootfs"))
    {
      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];
      char* mkboot_arg = alloca(strlen("TLR_ROOT=") + strlen(arg1) + 1);

      if (strncmp(arg1, "LABEL=", 6) == 0 ||
          strncmp(arg1, "UUID=", 5) == 0)
      {
        // Use the initramfs to boot up so we can discover the
        // per-filesystem label/uuid information.
        sprintf(mkboot_arg, "TLR_ROOT=%s", arg1);
      }
      else
      {
        // Don't need the initramfs, so boot straight to the argument.
        sprintf(mkboot_arg, "root=%s", arg1);
        g_no_initramfs = true;
      }

      StringArray_append(&mkboot_args, "--hvx");
      StringArray_append(&mkboot_args, mkboot_arg);
    }


    else if (!strcmp(arg, "--initramfs"))
    {
      verify_arg(arg, i + 1 <= argc);
      char *arg1 = argv[i++];

      FREESET(g_initramfs_file, strdup_or_die(arg1));
    }

    else if (!strcmp(arg, "--no-initramfs"))
    {
      FREESET(g_initramfs_file, NULL);
      g_no_initramfs = true;
    }

    else if (!strcmp(arg, "--mkboot-args"))
    {
      if (!parse_varargs(&mkboot_args, argv, &i))
        punt("Usage: '--mkboot-args -+- arg1 ... argN -+-'.");
    }


    else if (!strcmp(arg, "--bme"))
    {
      verify_arg(arg, i + 1 <= argc);
      StringArray_append(&g_bme_args, argv[i++]);
    }


    else if (!strcmp(arg, "--hv-bin-dir"))
    {
      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];

      FREESET(g_hv_bin_dir, strdup_or_die(arg1));
    }

    else if (!strcmp(arg, "--no-hv-bin-dir"))
    {
      FREESET(g_hv_bin_dir, NULL);
    }


#if TILE_CHIP >= 10

    else if (!strcmp(arg, "--classifier"))
    {
      verify_arg(arg, i + 1 <= argc);
      char *arg1 = argv[i++];

      FREESET(g_classifier_file, strdup_or_die(arg1));
    }

    else if (!strcmp(arg, "--no-classifier"))
    {
      FREESET(g_classifier_file, NULL);
    }

#endif


    else if (!strcmp(arg, "--vmlinux"))
    {
      verify_arg(arg, i + 1 <= argc);
      char *arg1 = argv[i++];

      FREESET(g_vmlinux_file, strdup_or_die(arg1));
    }

    else if (!strcmp(arg, "--no-vmlinux"))
    {
      FREESET(g_vmlinux_file, NULL);
      g_no_vmlinux = true;
    }


    else if (!strcmp(arg, "--boot-dir"))
    {
      verify_arg(arg, i + 1 <= argc);
      char* arg1 = argv[i++];

      FREESET(g_boot_dir, strdup_or_die(arg1));
      FREESET(g_hv_bin_dir, strdup_or_die(arg1));
#if TILE_CHIP >= 10
      FREESET(g_classifier_file, strfmt_or_die("%s/classifier", arg1));
#endif
    }


    else if (!strcmp(arg, "--peer-listen"))
    {
      verify_arg(arg, i + 1 <= argc);
      const char* port_str = argv[i++];

      int fd;

      if (port_str[0] == '@')
      {
        fd = listen_and_accept_from_unix_peer(port_str + 1);
      }
      else
      {
        uint16_t port = 0;
        if (!parse_port(&port, port_str))
          punt("Invalid port in '%s'.", port_str);
        fd = listen_and_accept_from_inet_peer(port);
      }

      Pollable_open(&g_peer_socket, fd, handle_peer_socket);
    }

    else if (!strcmp(arg, "--peer-connect"))
    {
      verify_arg(arg, i + 1 <= argc);
      const char* port_str = argv[i++];

      int fd;

      if (port_str[0] == '@')
      {
        fd = connect_to_unix_peer(port_str + 1);
      }
      else
      {
        // ISSUE: Unlike "connect_to_unix_peer()" above, this
        // does not keep trying for a minute.
        fd = simple_connect_string(port_str);
      }

      Pollable_open(&g_peer_socket, fd, handle_peer_socket);
    }


    else if (!strcmp(arg, "--no-dev"))
    {
      opt_no_dev = true;
    }


    else if (!strcmp(arg, "--dev"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      opt_dev = argv[i++];
#if TILE_CHIP >= 10
      if (strstr(opt_dev, "pci") && !strstr(opt_dev, "gxpci"))
        warn("Please use '--dev gxpciN' instead of '--dev pciN' for Gx.");
#endif
    }

#ifndef __tile__

    // FIXME: Deprecate this?
#if TILE_CHIP < 10
    else if (!strcmp(arg, "--pci"))
    {
      verify_primary_arg(arg);
      opt_dev = "pci0";
      //--warn("Please use '--dev pci0' instead of '--pci'.");
    }

    else if (!strcmp(arg, "--pci-card"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      opt_dev = strfmt_or_die("pci%s", argv[i++]);
      warn("Please use '--dev pciN' instead of '--pci-card N'.");
    }

    else if (!strcmp(arg, "--pci-link"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_pci_link = argv[i++];
      if (!strcmp(opt_pci_link, "0"))
        opt_pci_link = NULL;
    }

    // ISSUE: Rename this?
    else if (!strcmp(arg, "--pci-stream"))
    {
      verify_arg(arg, i + 1 <= argc);
      dev_stream = argv[i++];
    }
#endif

    else if (!strcmp(arg, "--simulator"))
    {
      // Undocumented historical option.
      opt_simulator = true;
    }

#endif

    else if (!strcmp(arg, "--net"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      opt_dev = strfmt_or_die("@%s", argv[i++]);
    }

#ifdef __tile__
    else if (!strcmp(arg, "--self"))
    {
      verify_primary_arg(arg);
      g_opt_self = true;
    }
#endif


    else if (!strcmp(arg, "--shepherd-port"))
    {
      verify_arg(arg, i + 1 <= argc);
      shepherd_port = atoi_or_die(argv[i++]);
    }

    else if (!strcmp(arg, "--greet-timeout"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_greet_timeout = atoi_or_die(argv[i++]);
    }

    else if (!strcmp(arg, "--boot-timeout"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_boot_timeout = atoi_or_die(argv[i++]);
    }

    else if (!strcmp(arg, "--boot-only"))
    {
      opt_boot_only = true;
    }

    else if (!strcmp(arg, "--prepare-reboot"))
    {
      opt_prepare_reboot = true;
    }

    else if (!strcmp(arg, "--resume"))
    {
      g_opt_resume = true;
    }

    else if (!strcmp(arg, "--reflash"))
    {
      opt_reflash = true;
    }

    else if (!strcmp(arg, "--reboot"))
    {
      // We may want to add support for this option at some point.
    }

    else if (!strcmp(arg, "--lockless"))
    {
      // NOTE: Undocumented experimental option.
      opt_lockless = true;
    }


    else if (!strcmp(arg, "--create-hvc"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_create_hvc = argv[i++];
    }

    else if (!strcmp(arg, "--create-bootrom"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_create_bootrom = argv[i++];
    }

#ifndef __tile__
    else if (!strcmp(arg, "--create-image"))
    {
      verify_arg(arg, i + 1 <= argc);
      opt_create_image = argv[i++];
    }
#endif


    else if (!strcmp(arg, "--console"))
    {
      g_opt_console = true;
    }

    else if (!strcmp(arg, "--try-console"))
    {
      g_opt_console = true;
      g_opt_try_console = true;
    }

    else if (!strcmp(arg, "--console-device"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_opt_console = true;
      opt_console_device = argv[i++];
    }

    else if (!strcmp(arg, "--console-out"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_opt_console = true;
      g_opt_console_out = argv[i++];
    }

    else if (!strcmp(arg, "--try-console-out"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_opt_console = true;
      g_opt_console_out = argv[i++];
      g_opt_try_console = true;
    }

    else if (!strcmp(arg, "--console-raw"))
    {
      // NOTE: Undocumented experimental option.
      // HACK: Do not run the simulator in a PTY.
      opt_console_raw = true;
    }

    else if (!strcmp(arg, "--testing"))
    {
      // NOTE: Undocumented testing option.
      g_testing = true;
    }

    else if (!strcmp(arg, "--ignore-protocol"))
    {
      // NOTE: Undocumented development option.
      g_ignore_protocol = true;
    }

    else if (!strcmp(arg, "--allow-execute"))
    {
      // NOTE: See "perform_hacky_execute()".
      g_allow_execute = true;
    }

    else if (!strcmp(arg, "--no-rtc"))
    {
      opt_no_rtc = true;
    }

    else if (!strcmp(arg, "--readline"))
    {
      g_readline_desired = true;
    }

    else if (!strcmp(arg, "--no-readline"))
    {
      g_readline_desired = false;
    }

#ifndef __tile__
    else if (!strcmp(arg, "--sim-prefix"))
    {
      verify_arg(arg, i + 1 <= argc);
      StringArray_append(&g_prefix_simulator_args, argv[i++]);
    }

    else if (!strcmp(arg, "--sim-arg"))
    {
      verify_arg(arg, i + 1 <= argc);
      StringArray_append(&g_extra_simulator_args, argv[i++]);
    }

    else if (!strcmp(arg, "--sim-args"))
    {
      if (!parse_varargs(&g_extra_simulator_args, argv, &i))
        punt("Usage: '--sim-args -+- arg1 ... argN -+-'.");
    }

    else if (!strcmp(arg, "--functional"))
    {
      // Pass through to "tile-sim".
      StringArray_append(&g_extra_simulator_args, arg);
    }

    else if (!strcmp(arg, "--shim"))
    {
      // Pass through to "tile-sim".
      verify_arg(arg, i + 1 <= argc);
      StringArray_append(&g_extra_simulator_args, arg);
      StringArray_append(&g_extra_simulator_args, argv[i++]);
    }

    else if (!strcmp(arg, "--symbols") ||
             !strcmp(arg, "--xml-profile") ||
             !strcmp(arg, "--xml-stats"))
    {
      // HACK: Undocumented testing options.
      // FIXME: Stop supporting these!
      if (!g_testing)
        warn("Please use '--sim-args' to specify '%s'.", arg);

      // Pass through to "tile-sim".
      verify_arg(arg, i + 1 <= argc);
      StringArray_append(&g_extra_simulator_args, arg);
      StringArray_append(&g_extra_simulator_args, argv[i++]);
    }

    else if (!strcmp(arg, "--bench-stats") && g_testing)
    {
      // NOTE: Undocumented testing option.
      verify_arg(arg, i + 1 <= argc);
      g_bench_stats = argv[i++];
      StringArray_append(&commands, "count-cycles");
      StringArray_append(&commands, NULL);
    }

    else if (!strcmp(arg, "--semi-functional") && g_testing)
    {
      // ISSUE: Undocumented (and maybe unused) testing option.

      // ISSUE: Just get rid of this, and instead require explicit use
      // of "--functional" plus "--sim - set functional 0 -"?

      // HACK: Pass through "--functional" to "tile-sim".
      StringArray_append(&g_extra_simulator_args, "--functional");

      // Remember to disable functional mode later.
      StringArray_append(&commands, "sim");
      StringArray_append(&commands, "set");
      StringArray_append(&commands, "functional");
      StringArray_append(&commands, "0");
      StringArray_append(&commands, NULL);
    }
#endif

    else if (!strcmp(arg, "--batch-mode"))
    {
      opt_batch_mode = true;
    }

    else if (!strcmp(arg, "--exit"))
    {
      // NOTE: Undocumented deprecated option.
      // NOTE: This was previously mentioned in "--help-examples".
      warn("Please use '--wait --quit' instead of '--exit'.");

      opt_batch_mode = true;
    }

    else if (!strcmp(arg, "--when-idle"))
    {
      // NOTE: Undocumented deprecated option.
      // NOTE: This was previously mentioned in "--help-examples".
      warn("Please use '--wait' instead of '--when-idle'.");

      StringArray_append(&commands, "wait");
      StringArray_append(&commands, NULL);
    }

    else if (!strcmp(arg, "--standard-symbols"))
    {
      // NOTE: Undocumented deprecated option.
      // NOTE: This was previously pretty much required.
    }

    else if (!strcmp(arg, "--local-gdb"))
    {
      g_local_gdb = true;
    }

    else if (!strcmp(arg, "--gdb-port"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_gdb_port = atoi_or_die(argv[i++]);
    }

    else if (!strcmp(arg, "--tag-console"))
    {
      g_tag_console = true;
    }

    else if (!strcmp(arg, "--ssh"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      opt_dev = strfmt_or_die("@%s", argv[i++]);
      g_opt_ssh = true;
    }

    else
    {
      bool okay = false;

      if (arg[0] == '-' && arg[1] == '-')
      {
        //--assert(arg[2] != '\0');

        int next_index = commands.size;

        char* cmd = arg + 2;

        for (int k = 0; k < NELEM(g_commands); k++)
        {
          struct command_info* ci = &g_commands[k];
          if (!strcmp(ci->cmd, cmd))
          {
            StringArray_append(&commands, cmd);
            if (ci->num < 0)
            {
              if (!parse_varargs(&commands, argv, &i))
                punt("Usage: '%s -+-%s -+-'.", arg, ci->suffix);
            }
            else
            {
              verify_arg(arg, i + ci->num <= argc);
              for (int j = 0; j < ci->num; j++)
                StringArray_append(&commands, argv[i++]);
            }
            StringArray_append(&commands, NULL);

            // HACK: Handle "help" commands immediately (to avoid complaints
            // about not specifying "--pci", for example), and then exit.
            if (has_prefix(cmd, "help"))
            {
              process_command(&commands, next_index);
              exit(g_exit_code);
            }

            okay = true;
            break;
          }
        }
      }

      if (!okay)
        punt("Unknown option '%s'.", arg);
    }
  }


  if (i < argc)
  {
    // Use an actual "run" command.
    StringArray_append(&commands, "run");
    while (i < argc)
      StringArray_append(&commands, argv[i++]);
    StringArray_append(&commands, NULL);

    // Assume "--batch-mode".
    opt_batch_mode = true;
  }


  if (opt_no_dev)
  {
    if (primary_arg != NULL)
      punt("Cannot use '--no-dev' with '%s'.", primary_arg);

    if (opt_create_hvc == NULL && opt_create_bootrom == NULL)
      punt("Cannot use '--no-dev' without '--create-bootrom'/'--create-hvc'.");
  }


  // Handle "--dev DEV", and default to "--dev ''", unless "--no-dev" used.
  if (opt_dev != NULL || (primary_arg == NULL && !opt_no_dev))
  {
    // Run "tile-dev DEV".
    get_install_path(path, sizeof(path), "/bin/tile-dev");
    int len = strlen(path);
    snprintf(path + len, sizeof(path) - len, " '%s'", opt_dev ?: "");
    FILE* fp = popen(path, "r");
    if (fp == NULL || fgets(temp, sizeof(temp), fp) == NULL || pclose(fp) != 0)
    {
#ifdef __tile__
      punt("Specify '--self', '--dev', etc.");
#else
      punt("Specify '--dev', '--image', '--config', etc.");
#endif
    }

    // Strip final newline.
    int tlen = strlen(temp);
    if (temp[tlen - 1] == '\n')
      temp[--tlen] = '\0';

    g_dev_name = strdup_or_die(temp);
  }

  if (g_dev_name != NULL)
  {
#if TILE_CHIP < 10
    if (has_prefix(g_dev_name, "/pci"))
    {
      if (opt_pci_link != NULL)
        g_dev_devdir =
          strfmt_or_die("/dev/tile%s-link%s", g_dev_name + 1, opt_pci_link);
      else
        g_dev_devdir = strfmt_or_die("/dev/tile%s", g_dev_name + 1);

      g_dev_driver = "tilepci";
      g_dev_is_pci = true;
    }
#else
    if (has_prefix(g_dev_name, "/gxpci"))
    {
      g_dev_devdir = strfmt_or_die("/dev/tile%s", g_dev_name + 1);

      g_dev_driver = "tilegxpci";
      g_dev_is_pci = true;
    }
    else if (has_prefix(g_dev_name, "/usb"))
    {
      g_dev_devdir = strfmt_or_die("/dev/tile%s", g_dev_name + 1);
      g_dev_driver = "tileusb";
    }
    else if (has_prefix(g_dev_name, "@"))
    {
      char* p = strchr(g_dev_name, '#');

      // Handle BMC device.
      if (p != NULL)
      {
        *p = '\0';
        g_dev_bmc_host = strdup_or_die(g_dev_name + 1);
        *p = '#';

        // Convert node to port number.  Note that the first node is node
        // 1; the first CPU on that node is at port 1200.  Subsequent nodes
        // are at successive port numbers, while subsequent CPUs on the
        // same node are 16 ports apart; thus, node 2 cpu b is on port 1217.
        char* endp;
        int node;

        node = strtol(++p, &endp, 10);

        if (node <= 0 || node > 15 || (*endp && (!isalpha(*endp) || endp[1])))
          punt("Invalid node #%s", p);

        int cpu = (*endp) ? tolower(*endp) - 'a' : 0;

        g_dev_bmc_port = 1200 + node - 1 + 16 * cpu;
        spew(1, "Using BMC port number %d", g_dev_bmc_port);
      }
    }
#endif

    if (g_dev_devdir != NULL)
    {
      // ISSUE: Rename this?
      if (getenv("TILERA_PCI_FLAKY") != NULL)
      {
        g_testing = true;
        if (!g_opt_console)
        {
          g_opt_console = true;
          g_opt_console_out = "/dev/null";
        }
      }

      // ISSUE: Rename this?
      const char* str = getenv("TILERA_PCI_UPDATE");
      if (!g_opt_resume && str != NULL && system(str) != 0)
        punt("Failed to apply 'TILERA_PCI_UPDATE'.");
    }
  }

  if (mkboot_args.size != 0 && !g_testing &&
      (g_opt_resume && !opt_prepare_reboot))
  {
    warn("Ignoring boot options due to '--resume'.");
    if (message_verbosity > 0)
    {
      for (int i = 0; i < mkboot_args.size; i++)
        warn("Ignoring '%s'.", StringArray_get(&mkboot_args, i));
    }
  }

  // Set up default vmlinux and initramfs if necessary, and append the
  // "initramfs=FILE" argument to the mkboot args.
  if (!g_no_vmlinux)
  {
    if (g_vmlinux_file == NULL)
      g_vmlinux_file = strfmt_or_die("%s/vmlinux", g_boot_dir);

    // Determine based on "--initramfs" / "--no-initramfs" (and implicitly
    // "--rootfs") whether or not to add an initramfs.
    if (g_initramfs_file != NULL || !g_no_initramfs)
    {
      if (g_initramfs_file == NULL)
        g_initramfs_file = strfmt_or_die("%s/initramfs.cpio.xz", g_boot_dir);

      const char initramfs_name[] = "initramfs";
      char* mkboot_arg =
        alloca(strlen(initramfs_name) + 1 + strlen(g_initramfs_file) + 1);
      sprintf(mkboot_arg, "%s=%s", initramfs_name, g_initramfs_file);

      StringArray_append(&mkboot_args, mkboot_arg);
    }
  }

  // Historical paranoia.
  if (opt_simulator && (g_opt_config == NULL && g_image_file == NULL))
    punt("Cannot use '--simulator' without '--config' or '--image'.");

  opt_simulator = (g_opt_config != NULL || g_image_file != NULL);

  if (g_opt_resume && opt_simulator)
    punt("Cannot use '--resume' with the simulator.");

  if (opt_prepare_reboot && opt_simulator)
    punt("Cannot use '--prepare-reboot' with the simulator.");


  if (g_opt_config != NULL)
  {
    // FIXME: This information is duplicated in the simulator.

    if (sscanf(g_opt_config, "%ux%u", &g_width, &g_height) == 2)
      /* Okay */;
#if TILE_CHIP >= 10
    else if (has_prefix(g_opt_config, "gx8100"))
      g_width = g_height = 10;
    else if (has_prefix(g_opt_config, "gx8072"))
    {
      g_width  = 8;
      g_height = 9;
    }
    else if (has_prefix(g_opt_config, "gx8064"))
      g_width = g_height = 8;
    else if (has_prefix(g_opt_config, "gx8036"))
      g_width = g_height = 6;
    else if (has_prefix(g_opt_config, "gx8016"))
      g_width = g_height = 4;
    // FIXME: Nuke this.
    else if (has_prefix(g_opt_config, "gx36"))
      g_width = g_height = 6;
    // FIXME: Nuke this.
    else if (has_prefix(g_opt_config, "tile64"))
      g_width = g_height = 8;
#else
    else if (has_prefix(g_opt_config, "tile64"))
      g_width = g_height = 8;
    else if (has_prefix(g_opt_config, "tile36"))
      g_width = g_height = 6;
#endif
    else
      g_width = g_height = 0;

    if (g_width == 0 || g_height == 0)
      warn("Unknown simulator config '%s'.", g_opt_config);

    StringArray* defs = &mkboot_defs;
    StringArray_append(defs, strfmt_or_die("CHIP_VERSION=%d", TILE_CHIP));
    StringArray_append(defs, strfmt_or_die("CHIP_WIDTH=%d", g_width));
    StringArray_append(defs, strfmt_or_die("CHIP_HEIGHT=%d", g_height));
  }

  else if (g_dev_devdir != NULL || g_dev_bmc_host != NULL)
  {
    // Verify the chip and driver, set "g_width" and "g_height", and
    // collect definitions for "tile-mkboot".
    verify_devdir(&mkboot_defs);

    // FIXME: Allow use with USB and PCI?
    // If so, add "--compress" and "--fixed".
    if (opt_prepare_reboot)
      punt("Cannot use '--prepare-reboot' with USB/PCI boot.");

    // Data transfer from the BMC to the target chip is over very slow
    // full-speed USB, so compressing the bootrom is a win.  For high-speed
    // USB, such as you'd have with a direct connction, it isn't.
    if (g_dev_bmc_host != NULL)
    {
      StringArray_append(&mkboot_args, "--compress");
      StringArray_append(&mkboot_args, "--fixed");
    }
  }

  else if (g_dev_name != NULL || g_opt_self || opt_no_dev)
  {
    // NOTE: We assume "--no-dev" is being used to create bootroms for
    // later installation via "sbim" (or "tile-reboot").

#if TILE_CHIP < 10
    StringArray* defs = &mkboot_defs;

    // FIXME: Get "CHIP_VERSION", "CHIP_WIDTH", and "CHIP_HEIGHT" from
    // the card.  And verify the chip version.  See bug 7260.

    // NOTE: For Gx, we don't bother to "fake" this info.

    g_width = 8;
    g_height = 8;

    StringArray_append(defs, strfmt_or_die("CHIP_VERSION=%d", TILE_CHIP));
    StringArray_append(defs, strfmt_or_die("CHIP_WIDTH=%d", g_width));
    StringArray_append(defs, strfmt_or_die("CHIP_HEIGHT=%d", g_height));
#endif

    // Compress the bootrom.
    StringArray_append(&mkboot_args, "--compress");
    StringArray_append(&mkboot_args, "--fixed");
  }

#if TILE_CHIP >= 10
  // For Gx PCIe, use g_dev_name to get the port's host link index and
  // pass it to the Shepherd with TLR_SHEPHERD_PCIE_MAC.
  if (g_dev_is_pci)
  {
    char *mkboot_arg;

    mkboot_arg = alloca(strlen("TLR_SHEPHERD_PCIE_MAC=") + 4 + 1);

    sprintf(mkboot_arg, "TLR_SHEPHERD_PCIE_MAC=%s", g_pci_link_index);

    // Append "--hvx TLR_SHEPHERD_PCIE_MAC=N".
    StringArray_append(&mkboot_args, "--hvx");
    StringArray_append(&mkboot_args, mkboot_arg);
  }
#endif

  // Cannot use "--hvc" with "--image", "--image-file", or "--bootrom-file".
  // Also, any explicit "--hvc" bypasses the "default_hvc" code below.
  // ISSUE: Could also forbid "--hvh", "--hvd", "--hvi", "--hvx", etc.
  if (num_hvc != 0)
    verify_secondary_arg("--hvc");

  if (g_bootrom_file != NULL && g_opt_raw_mode)
    punt("Cannot use '--bootrom-file' with '--raw-mode'.");


  char* default_hvc = NULL;

  if (secondary_arg == NULL &&
      (!g_opt_resume || opt_prepare_reboot) && !g_opt_raw_mode)
  {
    char* hvc = NULL;

#if TILE_CHIP >= 10
    // FIXME: Absorb "vmlinux-sim.hvc" as well.
    if (g_opt_config != NULL)
      hvc = "vmlinux-sim.hvc";
    else
      hvc = "vmlinux.hvc";
#else
    if (g_opt_config != NULL)
      hvc = "vmlinux-sim.hvc";
    else if (g_dev_is_pci)
      hvc = "vmlinux-pci.hvc";
    else if (g_dev_name != NULL || g_opt_self)
      hvc = "vmlinux-net.hvc";
#endif

    if (hvc != NULL)
    {
      default_hvc = strfmt_or_die("%s/%s", tile_etc_hvc, hvc);

      num_hvc++;
      StringArray_append(&mkboot_args, "--hvc");
      StringArray_append(&mkboot_args, default_hvc);
    }
  }


  if (num_hvc != 0 && (!g_opt_resume || opt_prepare_reboot))
  {
    if (opt_create_bootrom != NULL)
    {
      g_bootrom_file = strdup_or_die(opt_create_bootrom);
    }
    else
    {
      g_bootrom_file = strdup_or_die("/tmp/monitor-bootrom-XXXXXX");
      close_or_die(mkstemp_or_die(g_bootrom_file));
    }

    StringArray args;
    StringArray_init(&args);

    // Get the path to "tile-mkboot".
    char tile_mkboot_exe[PATH_MAX];
#ifdef __tile__
    get_install_path(tile_mkboot_exe, sizeof(tile_mkboot_exe),
                     "/sbin/tile-mkboot");
#else
    get_install_path(tile_mkboot_exe, sizeof(tile_mkboot_exe),
                     "/bin/tile-mkboot");
#endif
    StringArray_append(&args, tile_mkboot_exe);

    if (opt_create_hvc != NULL)
    {
      // Create an "hvc" file.
      StringArray_append(&args, "--dump-hvc");
      StringArray_append(&args, "-o");
      StringArray_append(&args, opt_create_hvc);
    }
    else
    {
      // Create a "bootrom" file.
      StringArray_append(&args, "-o");
      StringArray_append(&args, g_bootrom_file);
    }

    // Add "--hvd" arguments from "verify_devdir()".
#if TILE_CHIP >= 10
    // This is not needed for TILEGx devices for PCIe boot.
    if (g_dev_is_pci != true)
#endif
    for (int i = 0; i < mkboot_defs.size; i++)
    {
      StringArray_append(&args, "--hvd");
      StringArray_append(&args, StringArray_get(&mkboot_defs, i));
    }

    // Append normal args.
    for (int i = 0; i < mkboot_args.size; i++)
      StringArray_append(&args, StringArray_get(&mkboot_args, i));

    if (g_hv_bin_dir != NULL)
    {
      StringArray_append(&args, "--hv-bin-dir");
      StringArray_append(&args, g_hv_bin_dir);
    }

    if (g_classifier_file != NULL)
    {
      StringArray_append(&args, strfmt_or_die("classifier=%s",
                                              g_classifier_file));
    }

    if (g_vmlinux_file != NULL)
    {
      StringArray_append(&args, strfmt_or_die("vmlinux=%s", g_vmlinux_file));
    }

    for (int i = 0; i < g_bme_args.size; i++)
    {
      StringArray_append(&args, StringArray_get(&g_bme_args, i));
    }

    // Terminate.
    StringArray_append(&args, NULL);

    if (message_verbosity >= 1)
    {
      Buffer buf;
      Buffer_init(&buf);
      for (int i = 0; i < args.size - 1; i++)
      {
        char* arg = StringArray_get(&args, i);
        if (strchr(arg, ' '))
          Buffer_printf(&buf, " '%s'", arg);
        else
          Buffer_printf(&buf, " %s", arg);
      }
      spew(1, "Running:%s", buf.data);
      Buffer_destroy(&buf);
    }

    pid_t pid = fork_or_die();
    if (pid == 0)
    {
      // Handle Child.

      // Exec or die.
      char** data = args.data;
      (void)execv(data[0], data);
      punt_with_errno("Failure in 'execv(%s)'", data[0]);
    }

    StringArray_destroy(&args);

    // Wait for completion.
    g_kill_pid_at_exit = pid;
    int status;
    (void)waitpid_or_die(pid, &status, 0);
    g_kill_pid_at_exit = 0;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      punt("Unexpected status 0x%x from 'tile-mkboot'.", status);

    // Exit if done.
    if (opt_create_hvc != NULL || opt_create_bootrom != NULL)
      exit(0);

    g_unlink_bootrom = g_bootrom_file;
  }

  StringArray_free_and_clear(&mkboot_defs);
  StringArray_destroy(&mkboot_defs);

  StringArray_destroy(&mkboot_args);


  const char* file = NULL;

#ifdef __tile__

  if (g_bootrom_file != NULL)
  {
    file = g_bootrom_file;

    if (!(g_dev_name != NULL || g_opt_self))
      punt("Cannot use '--bootrom-file' without '--dev', '--self', etc.");
  }

#else

  if (g_image_file != NULL)
  {
    file = g_image_file;

    simulator_argv = simulator_argv_for_image_file;
  }

  else if (g_bootrom_file != NULL)
  {
    file = g_bootrom_file;

    if (g_opt_config != NULL)
      simulator_argv = simulator_argv_for_bootrom_file;
    else if (!(g_dev_name != NULL || g_opt_self))
      punt("Cannot use '--bootrom-file' without '--dev', '--config', etc.");
  }

  else if (g_opt_raw_mode)
  {
    simulator_argv = simulator_argv_for_raw_mode;
  }

  else if (opt_magic_hypervisor)
  {
    file = g_vmlinux_file;
    simulator_argv = simulator_argv_for_magic_hypervisor;
  }

#endif

  if (file != NULL && close(open(file, O_RDONLY)) != 0)
    punt("Required file '%s' does not exist.", file);


#ifndef __tile__

  if (opt_create_image != NULL)
  {
    if (g_opt_config == NULL)
      punt("Cannot use '--create-image' without '--config'.");

    if (commands.size != 0)
      punt("Cannot use commands with '--create-image'.");
  }


  if (g_bench_stats != NULL && opt_simulator)
  {
    StringArray_append(&g_extra_simulator_args, "--xml-stats");
    StringArray_append(&g_extra_simulator_args, g_bench_stats);
    g_bench_stats = NULL;
  }


  // ISSUE: Pre-canonicalize all the paths sent to the simulator?

  // ISSUE: Using "--symbol" with "--image[-file]" causes a crash.

  if (opt_simulator && g_image_file == NULL)
  {
    // Add in symbols for the 'hv' binary files if needed.
    if (g_hv_bin_dir != NULL && !opt_magic_hypervisor)
    {
      static const char* const names[] = { "hv_lhboot", "hv_l1boot", "hv" };
      for (size_t j = 0; j < NELEM(names); j++)
      {
        const char* name = names[j];
        // NOTE: This path is leaked.
        char* arg = strfmt_or_die("%s/%s", g_hv_bin_dir, name);
        StringArray_append(&g_extra_simulator_args, "--symbols");
        StringArray_append(&g_extra_simulator_args, arg);
      }
    }

    // Add in symbols for the 'vmlinux' file if needed.
    if (g_vmlinux_file != NULL)
    {
      StringArray_append(&g_extra_simulator_args, "--symbols");
      StringArray_append(&g_extra_simulator_args, g_vmlinux_file);
    }
  }


  // ISSUE: Just support "--bme" in the simulator?

  if (opt_simulator)
  {
    for (int i = 0; i < g_bme_args.size; i++)
    {
      char* arg = StringArray_get(&g_bme_args, i);
      char* eq = strchr(arg, '=');
      StringArray_append(&g_extra_simulator_args, "--upload");
      // NOTE: This path is leaked.
      char* what;
      if (eq != NULL)
        what = strfmt_or_die("%s,%.*s", eq + 1, (int)(eq - arg), arg);
      else
        what = strfmt_or_die("%s,%s", arg, arg);
      StringArray_append(&g_extra_simulator_args, what);
    }
  }


  if (opt_ide_port != NULL)
  {
    spew(3, "Connecting to IDE...");
    int fd = simple_connect_string(opt_ide_port);
    set_close_on_exec_or_die(fd, true);
    set_keep_alive_or_die(fd, true);

    // Prepare to process IDE queries.
    Pollable_open(&g_ide_socket, fd, NULL);
  }


  if (opt_create_image != NULL)
  {
    note("Creating '%s'...", opt_create_image);

    if (access(opt_create_image, R_OK) == 0)
      unlink(opt_create_image);

    StringArray_append(&g_extra_simulator_args, "--creating-image");

    StringArray_append(&g_extra_simulator_args, "--checkpoint-file");
    StringArray_append(&g_extra_simulator_args, opt_create_image);

    spawn_simulator(simulator_argv, false);

    thaw_simulator_if_needed();

    // HACK: Manually flush reporter socket.
    Pollable_flush_fully(&g_reporter_socket);

    // Block until simulator exits.
    int status;
    (void)waitpid_or_die(g_simulator_pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      handle_unexpected_simulator_death(status);

    if (access(opt_create_image, R_OK) != 0)
    {
      snprintf(g_exit_msg, sizeof(g_exit_msg),
               "This normally indicates that there is a problem"
               " with the 'bootrom' file.\n"
               "Use '--console' to see any errors that occur"
               " during booting.\n\n");
      punt("Could not create '%s'!", opt_create_image);
    }

    note("Creating '%s'... done.", opt_create_image);

    exit(g_exit_code);
  }


  // Send monitor command-line arguments to IDE,
  // so the IDE can display command line in console.
  if (Pollable_valid(&g_ide_socket))
  {
    // Report monitor hostname/pid to IDE.
    const char* monitor_host = fullhostname_or_die();
    int pid = getpid();
    do_monitor_info(&g_ide_socket, monitor_host, pid);

    // Report monitor command line to IDE.
    StringArray monitor_args;
    StringArray_init(&monitor_args);
    for (int i = 0; i < argc; i++)
      StringArray_append(&monitor_args, argv[i]);
    do_monitor_command_line(&g_ide_socket, &monitor_args);
    StringArray_destroy(&monitor_args);

    // Report project/launch arguments to IDE.
    do_ide_info(&g_ide_socket,
                (opt_ide_launch_id != NULL) ? opt_ide_launch_id : "");
  }

#endif

  // If user requested command port, open it and start accepting connections.
  if (opt_external_command_port != NULL)
  {
    spew(3, "Opening command port...");

    bool okay = true;
    uint16_t port = 0; // 0 means automatically pick a port

    // If user specified port number, try to parse it.
    if (opt_external_command_port[0] != '\0')
    {
      okay = parse_port_or_warn(&port, opt_external_command_port, false);
    }

    if (okay)
    {
      // Note: this writes back the port number we got into port var.
      int fd = simple_listen(&port, 1);
      note("Listening for commands on port %u.", port);
      Pollable_open(&g_external_command_socket,
                    fd, handle_external_command_socket);
    }
  }

  if (g_dev_devdir != NULL && !opt_lockless)
  {
    snprintf(path, sizeof(path), "%s/lock", g_dev_devdir);

    spew(3, "Locking via '%s'.", path);

    while (true)
    {
      int fd = open(path, O_WRONLY, 0);

      if (fd >= 0)
      {
        set_close_on_exec_or_die(fd, true);
        break;
      }

      if (errno == EINTR)
        continue;

      if (errno == EBUSY)
      {
        snprintf(g_exit_msg, sizeof(g_exit_msg),
                 "This normally indicates that another instance"
                 " of 'tile-monitor' is using the\n"
                 "chosen system.  This instance may belong to"
                 " another user, and/or may be\n"
                 "a backgrounded or zombie process.\n\n");
        punt("The file '%s' is currently locked!", path);
      }

      punt_with_errno("Failure in 'open(\"%s\")' on '%s'",
                      path, fullhostname_or_die());
    }

    // NOTE: Leave the file open (and thus locked) until we exit.
  }

  else if (g_dev_bmc_host != NULL && !opt_lockless)
  {
    spew(3, "Locking via BMC '%s'.", g_dev_name);

    // Open the lock file.
    int fd = tilemon_proxy_helper("TILE-MONITOR-LOCK", "EBUSY");

    if (fd < 0)
    {
      snprintf(g_exit_msg, sizeof(g_exit_msg),
               "This normally indicates that another instance "
               "of 'tile-monitor' is using the\n"
               "chosen system. This instance may belong to "
               "another user, and/or may be\n"
               "a backgrounded or zombie process.\n\n");

      punt("BMC '%s' is currently locked!", g_dev_name);
    }

    set_close_on_exec_or_die(fd, true);

    // NOTE: Leave the file open (and thus locked) until we exit.
  }

  // Handle "--console-out".
  // NOTE: The simulator handles "--console-out" specially.
  if (g_opt_console_out != NULL && !opt_simulator)
  {
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    g_console_stream.fd = open_or_die(g_opt_console_out, flags, 0666);
  }

  if (opt_console_device != NULL)
  {
    if (opt_simulator)
      punt("Cannot use '--console-device' with '--simulator'.");
    open_console_device(opt_console_device);
  }
  else if (g_opt_console && g_dev_name != NULL)
  {
    // FIXME: Use "ssh consoleCPU@HOST" like "tile-console".
    if (g_dev_bmc_host != NULL)
      punt("Cannot use '--console'.");

    // Run "tile-dev --key serial DEV".
    get_install_path(path, sizeof(path), "/bin/tile-dev");
    int len = strlen(path);
    snprintf(path + len, sizeof(path) - len, " %s serial '%s'",
             g_testing ? "--optional-key" : "--key", g_dev_name);
    FILE* fp = popen(path, "r");
    if (fp == NULL)
      punt_with_errno("popen \"%s\": failed to run", path);
    temp[0] = 0;
    if (fgets(temp, sizeof(temp), fp) == NULL && ferror(fp))
      punt_with_errno("popen \"%s\": error reading output", path);
    if (pclose(fp) == -1)
      punt_with_errno("pclose \"%s\"", path);

    // See if we got a serial device.
    int tlen = strlen(temp);
    if (tlen == 0)
    {
      if (!g_opt_try_console)
        punt("Cannot use '--console'.");
    }
    else
    {
      // Strip final newline.
      if (temp[tlen - 1] == '\n')
        temp[--tlen] = '\0';

      open_console_device(temp);
    }
  }
  else if (g_opt_console && g_opt_self)
  {
    punt("Cannot use '--console' with '--self'.");
  }


  if (g_dev_devdir != NULL)
  {
    if (!g_opt_resume)
    {
      // Boot the card.

      snprintf(path, sizeof(path), "%s/boot", g_dev_devdir);

      spew(3, "Booting via '%s'.", path);

      int fd1 = verify_bootrom(g_bootrom_file);

      struct stat st;
      stat(g_bootrom_file, &st);

      // Like "open_or_die()" but with a special panic.
      int fd2;
      while (true)
      {
        fd2 = open(path, O_WRONLY, 0);
        if (fd2 >= 0)
          break;

        if (errno != EINTR)
          devdir_panic(path);
      }

      sighandler_t old_sigalrm = signal(SIGALRM,
                                        handle_boot_timeout_alarm);
      if (old_sigalrm == SIG_ERR)
        punt("Failed to register alarm signal handler.");

      unsigned int old_alarm = alarm(g_boot_timeout);

      char buf[16 * 1024];
      while (true)
      {
        int n = read_some_bytes_or_die(fd1, buf, sizeof(buf));
        if (n <= 0)
          break;
        write_some_bytes_or_die(fd2, buf, n);
      }

      alarm(old_alarm);
      signal(SIGALRM, old_sigalrm);

      close_or_die(fd2);
      close_or_die(fd1);
    }

    if (!opt_boot_only)
    {
#if TILE_CHIP >= 10
      if (g_dev_is_pci && g_pci_nic_config)
      {
        // Set up the connection over IP tunnel.
        char path[PATH_MAX], who[PATH_MAX];
        struct ifreq ifr;

        // Create a socket to perform ioctl().
        int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0)
          punt_with_errno("Failure in 'socket()'");

        snprintf(path, sizeof(path), "tilep2p%s", g_pci_link_index);
        strncpy(ifr.ifr_name, path, sizeof(ifr.ifr_name));

        setup_greet_alarm_if_needed();
        while (1)
        {
          // Get the flags of the PCIe P2P NIC interface.
          int result = ioctl(sock_fd, SIOCGIFFLAGS, &ifr);
          if (result < 0)
            punt_with_errno("Failure in 'ioctl()'");

          // Break out immediately if the PCIe P2P NIC interface becomes UP.
          if (ifr.ifr_flags & IFF_UP)
            break;

          // Wait for awhile before next check.
          dispatch_events(100);
        }

        spew(2, "Talking to shepherd via '%s'.", path);
        snprintf(who, sizeof(who), "%s", g_pci_nic_addr);
        g_dev_devdir = NULL;

        connect_shepherd(who, shepherd_port, 0);

        goto shepherd_connected;
      }
#endif

      // Open the stream.
      snprintf(path, sizeof(path), "%s/%s", g_dev_devdir, dev_stream);
      spew(2, "Talking to shepherd via '%s'.", path);

      int fd = -1;

      setup_greet_alarm_if_needed();
      while (1)
      {
        fd = open(path, O_RDWR, 0);
        if (fd >= 0)
          break;

        if (errno == EPERM)
          devdir_panic(path);

        dispatch_events(100);
      }

      set_blocking_or_die(fd, false);
      set_close_on_exec_or_die(fd, true);

      Pollable_open(&g_shepherd_socket, fd, handle_shepherd_socket);
    }
  }

  else if (g_dev_bmc_host != NULL)
  {
    // FIXME: Watch the console!

    if (!g_opt_resume)
    {
      // Boot.

      spew(3, "Booting via BMC '%s'.", g_dev_name);

      int fd1 = verify_bootrom(g_bootrom_file);

      struct stat st;
      stat(g_bootrom_file, &st);

      int fd2 = tilemon_proxy_helper("TILE-MONITOR-BOOT", NULL);

      // ISSUE: Leave "blocking" mode?

      sighandler_t old_sigalrm = signal(SIGALRM,
                                        handle_boot_timeout_alarm);
      if (old_sigalrm == SIG_ERR)
        punt("Failed to register alarm signal handler.");

      unsigned int old_alarm = alarm(g_boot_timeout);

      char buf[4096];
      while (true)
      {
        int n = read_some_bytes_or_die(fd1, buf, sizeof(buf));
        if (n <= 0)
          break;
        write_some_bytes_or_die(fd2, buf, n);
      }

      // We might exit right after boot, and in that case, we'd like to be
      // sure that the entire boot stream has been sent to the BMC.
      // Ideally we'd wait for some sort of ACK, but it doesn't send one.
      // Instead we'll shut down the write side of the socket, then read
      // from it.  When the BMC finishes reading the boot stream and sees
      // that we've closed it, it'll exit, closing its end of the socket.
      // Then our read will complete will complete with 0 bytes read.  (If
      // we get some sort of read error, then that's fine, we'll just
      // proceed.)  Note that if the BMC vanishes, and thus never closes
      // the socket, we'll eventually hit our boot timeout and exit.
      if (shutdown(fd2, SHUT_WR))
        warn("Could not shut down boot socket: %s", strerror(errno));

      char dummy[1];
      read(fd2, dummy, sizeof(dummy));

      close_or_die(fd2);
      close_or_die(fd1);

      alarm(old_alarm);
      signal(SIGALRM, old_sigalrm);
    }

    // Open the stream, if we're going to use one.

    if (!opt_boot_only)
    {
      setup_greet_alarm_if_needed();
      int fd = tilemon_proxy_helper("TILE-MONITOR-DEV0", NULL);

      set_blocking_or_die(fd, false);
      set_close_on_exec_or_die(fd, true);

      Pollable_open(&g_shepherd_socket, fd, handle_shepherd_socket);
    }
  }

  else if (g_dev_name != NULL || g_opt_self)
  {
    const char* who;
    const char* who_or_self;

    char* user = NULL;
    if (g_opt_self)
    {
      who = "localhost";
      who_or_self = "--self";
    }
    else if (g_opt_ssh)
    {
      who = g_dev_name + 1;
      char* at = strchr(who, '@');
      if (at)
      {
        user = strndup(who, at - who);
        who = at + 1;
      }
      who_or_self = who;
    }
    else
    {
      who = who_or_self = g_dev_name + 1;
    }

    if (!g_opt_resume || opt_prepare_reboot)
    {
      // Boot the card.

      close_or_die(verify_bootrom(g_bootrom_file));

      spew(2, "Rebooting '%s'...", who);

      // ISSUE: It's a little silly to fork a child for "--later", and
      // extremely silly to do it for "--self" (without "--resume").
      // The "Failed to reboot." errors are misleading for "--later".

      int booter = fork_or_die();
      if (booter == 0)
      {
        char cmd[PATH_MAX * 4];

        // Reboot the machine, and wait for rebooting to finish.
        get_install_path(path, sizeof(path), "/bin/tile-reboot");
        snprintf(cmd, sizeof(cmd), "%s %s%s %s --bootrom %s %s",
                 path, (message_verbosity >= 2) ? "--verbose" : "",
                 opt_reflash ? " --reflash" : "",
                 opt_prepare_reboot ? "--later": " --wait",
                 g_bootrom_file,
                 who_or_self);
        exit(system(cmd) != 0);
      }

      // Wait for completion, watching the console.
      // ISSUE: Timeout eventually?
      g_kill_pid_at_exit = booter;
      while (true)
      {
        int status;
        if (waitpid_or_die(booter, &status, WNOHANG) != 0)
        {
          if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            punt("Failed to reboot '%s'.", who);
          break;
        }
        dispatch_events(-1);
      }
      g_kill_pid_at_exit = 0;

      if (g_opt_self && !opt_prepare_reboot)
        punt("Failed to reboot.");
    }

    int fd;
    if (g_opt_ssh)
    {
      spew(2, "Starting shepherd on '%s' over ssh", who);
      shepherd_over_ssh(user, (char *)who, &fd);
      if (user)
        free(user);

      set_keep_alive_or_die(fd, true);
      set_close_on_exec_or_die(fd, true);

      Pollable_open(&g_shepherd_socket, fd, handle_shepherd_socket);
    }
    else
      connect_shepherd(who, shepherd_port, 1);
  }

  else
  {
    assert(opt_simulator);
  }

 shepherd_connected:

  // Handle some "scary" signals.
  if (signal(SIGINT, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGTERM, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGHUP, dispatch_events_handle_signal) == SIG_ERR)
  {
    punt("Failed to register signal handlers.");
  }


#ifndef __tile__

  if (opt_simulator)
  {
    spew(2, "Spawning simulator and accepting a connection...");

    spawn_simulator(simulator_argv, !opt_console_raw);
  }

#endif


  // Implicit peer barrier, if needed.
  if (Pollable_valid(&g_peer_socket))
    handle_command_peer_barrier();


  // HACK: Once the simulator has connected to us, we can exit immediately
  // on signals, and the simulator will exit automatically.  This was added
  // because some people are unable to wait a few seconds for a clean exit.
  g_signal_immediate = opt_simulator;

  // Handle pending quit (see "handle_pending_signal()") and "--boot-only".
  if (g_want_quit || opt_boot_only)
    exit(g_exit_code);


  // HACK: Avoid "small reads" from the shepherd socket.
  Buffer_reserve(&g_shepherd_socket.input, 32768);


  // HACK: Discard any pending PCI/USB traffic.
  // ISSUE: Is this appropriate for BMC?
  if (g_dev_devdir != NULL || g_dev_bmc_host != NULL)
  {
    while (Pollable_read(&g_shepherd_socket, 4096) > 0)
    {
      Buffer* input = &g_shepherd_socket.input;
      spew_bytes(0, input->data, input->size, "Discarding old bytes: ");

      Buffer_clear(&g_shepherd_socket.input);
    }
  }


  if (Pollable_valid(&g_reporter_socket))
  {
    // Note that the resulting "target_info" query will be forwarded
    // to the IDE if needed.
    do_sim_request_target_info(&g_reporter_socket);
    spew(3, "Waiting for target info...");
    while (g_target_info == NULL)
      dispatch_events(-1);
    spew(3, "Waiting for target info... done.");
  }
  else
  {
    // Obviously the shepherd will not respond until the chip has
    // finished booting.  This can take more several seconds.  When
    // using "--net", we need to greet the shepherd to learn the chip
    // size, from which we derive the target info.  ISSUE: We should
    // be using "/bin/shepherd --dump-board-info", see bug 7260.
    greet_shepherd_if_needed();

#ifdef __tile__
    // The native version doesn't currently support being connected to via
    // the IDE.  We make up an empty target definition in case there's
    // other code which depends upon having one.
    char def[1024];
    snprintf(def, sizeof(def), "%s\n<target version=\"2.0.0\"></target>",
             g_xml_intro);
    g_target_info = strdup_or_die(def);
#else
    char tail[1024];
    snprintf(tail, sizeof(tail), "/etc/info/chip/%u/%ux%u.xml",
             TILE_CHIP, g_width, g_height);
    get_install_path(path, sizeof(path), tail);

    int fd = open_or_die(path, O_RDONLY, 0);

    Buffer info;
    Buffer_init(&info);

    Buffer_printf(&info, "%s\n", g_xml_intro);
    Buffer_printf(&info, "<target version=\"2.0.0\">\n");

    char buf[4096];
    while (true)
    {
      int n = read_some_bytes_or_die(fd, buf, sizeof(buf));
      if (n <= 0)
        break;
      Buffer_write(&info, buf, n);
    }

    close_or_die(fd);

    Buffer_printf(&info, "</target>\n");

    Buffer_append(&info, '\0');

    g_target_info = strdup_or_die((char*)info.data);

    spew(3, "Got target info:\n%s", g_target_info);

    Buffer_destroy(&info);
#endif

    // Send a "target_info" query to the IDE if needed.
    if (Pollable_valid(&g_ide_socket))
      do_target_info(&g_ide_socket, g_target_info);
  }

  // Clean up.
  if (g_unlink_bootrom != NULL)
  {
    (void)unlink(g_unlink_bootrom);
    g_unlink_bootrom = NULL;
  }

  // Clean up.
  free(tile_etc_hvc);
  free(lib_boot);
  free(default_hvc);

  // HACK: Set the clock if booting over PCI/USB or BMC.
  if ((g_dev_devdir != NULL || g_dev_bmc_host != NULL) &&
      !g_opt_resume && !opt_no_rtc)
  {
    handle_command_rtc();
  }

  // Start handling a limited subset of IDE queries now,
  // while we're still processing commands.
  if (Pollable_valid(&g_ide_socket))
    Pollable_set_handle_readable(&g_ide_socket, handle_ide_socket);

  // Process commands.
  // ISSUE: Warnings while processing commands should be "fatal".
  for (int i = 0; i < commands.size; )
    i = process_command(&commands, i);

  StringArray_destroy(&commands);

  if (opt_batch_mode && !g_want_quit)
    handle_command_wait();

  if (opt_batch_mode || g_want_quit)
    handle_command_quit();

  // Note that "handle_command_quit()" sets "g_requested_exit",
  // and exits immediately if the shepherd has not been greeted.

  // Tell the shepherd to become interactive.
  // ISSUE: What if the shepherd has not yet been greeted?
  if (!g_requested_exit)
    do_become_interactive(&g_shepherd_socket);

  thaw_simulator_if_needed();

  // We are now "running".
  g_running = true;

  while (true)
  {
    display_prompt_if_needed();

    handle_events();
  }

  // Unreachable.
  //--return 0;
}
