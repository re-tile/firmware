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

// This file defines the "shepherd" executable.

// The "shepherd" executable is used to instantiate a "shepherd"
// process, which watches other processes using "ptrace".

// The "shepherd" executable can be run with the "--pci" and/or
// "--net" flags, which creates a special "watchdog" process.  The
// watchdog dynamically forks off a "shepherd" process to handle each
// new connection from from "tile-monitor" (one at a time), and a set
// of "fuse daemon" processes to handle fuse "mount" requests.  Since
// all processes watched by this "shepherd" can communicate with the
// "tile-monitor" process, the shepherd is said to be "monitored".

// When the monitor disconnects, it asks the shepherd process to exit.
// When the monitor crashes, the watchdog kills the shepherd.  In both
// cases, the watchdog will then create a new shepherd for the next
// monitor connection.

// The watchdog captures stdout/stderr from the shepherd (and the fuse
// daemons) and forwards it to the monitor.

// On hardware, the watchdog handles the actual communication with the
// monitor (using PCIe or a socket), forking off the shepherd when the
// monitor initiates communication.

// On the simulator, the watchdog forks off the shepherd immediately,
// and the shepherd and the watchdog use special simulated sockets to
// communicate directly with the monitor.  Allowing the shepherd to
// bypass the watchdog provides a major efficiency boost, but causes
// some strange side effects, such as the "run" command finishing
// before all the output from the process has been displayed.

// Each fuse daemon owns a single mount point, which basically mirrors
// some directory on the host.  Operations inside this mount point are
// handled by FUSE by calling various hooks in the fuse daemon, which
// handles them using synchronous RPC queries to the monitor (through
// the watchdog).

// Note that the fuse daemons must communicate through the watchdog,
// not the shepherd, because otherwise, attempts by the shepherd to
// access files managed by a fuse daemon would deadlock.  This also
// means that the watchdog cannot access any FUSE mounted files.

// The shepherd handles each "launch" request by forking the requested
// process.  The resulting process, and all of its "interesting"
// descendents, are considered to be part of a single "application".

// The shepherd informs the monitor about the creation and destruction
// of "interesting" processes, and their threads.

// The "shepherd" executable can be run with arguments which specify
// an executable to run locally.  The resulting "unmonitored" shepherd
// is often referred to as a "local shepherd".

// TODO: Send information about "dataplane" and "dedicated" tiles to
// the IDE, so it can display them somehow.

// ISSUE: Because we do not "reap" a Thread immediately when it dies, we
// could in theory get confused if any new Thread ever uses the same tid.

// ISSUE: Do any more "rsp_xxx()" functions need to be "thread specific"?

// TODO: Handle "gdb crash" by actually detaching from the debugger,
// and clearing all breakpoints, instead of just killing the process.

// TODO: Find a better name for "ptrace_aux()".

// NOTE: The shepherd sends "note_attach" to inform the monitor that a
// process would like to be debugged.  This establishes a virtual RSP
// connection, which stays open until the monitor sends a "magic EOF".

// When a thread dies, we mark it as DEAD, and later, we report its
// destruction, and reap it.  When the last thread in a process has
// been reaped, we mark it as "dead", and notify the debugger.  When
// the debugger has disconnected, and the consoles have been closed,
// we report its death, and then reap the process.  When the last
// process in the app has been reaped, we reap the app, if needed.

// ISSUE: Maybe track "console streams" separately from the "Process"
// which created them, so that "Process" death can be reported before
// the console has closed.  Or, just add a new "reaped" notification.

// ISSUE: In a perfect world, if two threads hit a debugger breakpoint
// at the same time, and then, while the first one is being debugged,
// the breakpoint is removed, the second thread would forget that it
// had hit the breakpoint.  There is an open bug for this.

// ISSUE: Note that "gdb" assigns its own thread ids to each thread in
// the order in which they are "reported".  So if a process creates 3
// threads, A, B, C, and you break in B via a breakpoint, and THEN do
// "info threads", then "gdb" will actually order the threads as B, A,
// C.  We might be able to report some synthetic "pass through" signal
// (SIGALRM, SIGURG, SIGCHLD, SIGIO, SIGVTALRM, SIGPROF, SIGWINCH, or
// the undefined SIGPOLL, SIGWAITING, or SIGLWP) to gdb whenever a
// thread is first detected, giving gdb a chance to notice the thread
// creation, and then "ignoring" the signal when gdb passes it to us.

// When a thread is stepped/continued, and is the only REPORTED
// thread, then we resume it, and mark it as TRAPPING, so other
// threads will remain suspended, but after a short while, when the
// "trapping_alarm" fires, we mark it as ACTIVE, allowing the other
// threads to be resumed (or reported).

// ISSUE: A process launched on multiple tiles will check its FIRST
// tile against "--debug-tile", even if it is actually running on a
// different tile.  This seems somewhat questionable.

// FIXME: We need a way to launch a process, and debug it, without
// automatically debugging its forked children.

// ISSUE: When gdb is multi-stepping a thread, say, to handle "next" in
// the absense of debug info, the state will often be REPORTED, and even
// if we report SIGINT in response to the next "step", gdb will not stop.
// This is extremely annoying!  For example, launch a debuggable thread,
// and tell gdb to "step", and it will often trigger this behavior.

// ISSUE: Support "$qGetTLSAddr:" rsp packets?

// FIXME: The shepherd does not always properly handle a watched
// process being killed by SIGKILL, including an external "kill -9",
// and even its own "kill_process()", and sometimes this can even
// cause the shepherd to "die".

// ISSUE: A process can be killed, via "kill -9", before the very first
// "stop" (and thus before "note_attach" is sent), or when the process
// is "stopped".

// FIXME: If a watched process is killed by an external SIGKILL while
// REPORTED, then we send a "death" rsp packet, which the debugger
// does not want, and later, when the debugger tries to send any rsp
// packet, including a "kill/detach" rsp packet, we ignore it.  This
// causes wackiness!

// NOTE: The tag range 0x0001 - 0x7FFF is reserved for the shepherd.
// NOTE: The tag range 0x8000 - 0xEFFF is reserved for fuse daemons.
// NOTE: The tag range 0xF000 - 0xFFFE is reserved for executors.
// NOTE: The tag 0xFFFF is pseudo-reserved for tasks and executors.

// ISSUE: Document the reserved tag ranges in the other direction too.

// ISSUE: Might "PTRACE_O_TRACEEXIT" be useful in threaded programs?


// Includes "tools/handy/handy.h" and thus "common/include/tilera.h".
#include "common.h"

#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <asm/hardwall.h>

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/sim.h>

#include <tmc/task.h>
#include <tmc/alloc.h>

#include "elf.h"



// Global variables declared in "common.h".

uint16_t g_opt_execute_port;

const char* g_opt_net_port;

const char* g_opt_pci_device;

const char* g_opt_tmfifo_device;

bool g_opt_ssh;

uint g_width;

uint g_height;

Array g_processes;

Pollable g_monitor_socket;

Pollable g_monitor_stdin;

Pollable g_monitor_stdout;

//! HACK: Used only for forwarding "mount_fuse" query on simulator.
static Pollable* g_watchdog_socket;

//! The "online" cpus.
static cpu_set_t g_cpus_online;

//! The "standard" cpus.
static cpu_set_t g_cpus_standard;

//! The "dataplane" cpus.
static cpu_set_t g_cpus_dataplane;

//! The total number of ATTEMPTED launches.
static uint g_total_launches;

//! The next anonymous negative "app_id" to be assigned.
static int g_next_negative_app_id = -2;

//! If true when an application is launched, then we will start debugging
//! any process in the application which receives an "interesting" signal.
static bool g_debug_on_crash;

//! If this is true, then we are "interactive", and, on the simulator,
//! we must frequently poll for monitor traffic.  This is set true
//! whenever a "become_interactive" query is handled, or "note_attach"
//! is sent to the monitor.
static bool g_interactive;

//! Set to non-zero via "awaiting_app" RPC query.  Set to zero when
//! the given application is reaped, or the chip becomes idle.  Note
//! that "-1" can be used to indicate "all apps".  If "g_interactive"
//! is false, and this is non-zero, then on the simulator, we want to
//! check occasionally for a spontaneous "become_interactive" query.
static int g_awaiting_app;

//! If true, send "note_idle" when idle.
static bool g_want_note_idle;

//! If true, exit when idle.
static bool g_exit_when_idle;

//! List of executables to be debugged.
static StringArray g_debug_executables;

//! See "perform_spread_threads".
static bool g_spread_threads;

//! See "perform_debug_next".
static bool g_debug_next;

//! HACK: See "perform_task_greet()".
static bool g_ignore_protocol;

//! HACK: See "perform_count_cycles()".
static bool g_count_cycles;

//! HACK: See "perform_count_cycles()".
static uint64_t g_count_cycles_start;

// Array of "forward" sockets (each one is a Pollable, whose "info"
// is the index in this array, which is used as its "id").
static Array g_forward_sockets;

//! Array of "tunnel" sockets.
static Array g_tunnel_sockets;

//! HACK: See "perform_hacky_hardwall".
static int g_hacky_hardwall = -1;


//! The "name" used by "g_task_listener".
static char* g_task_listener_name;

//! The special "shepherd" listener.
static Pollable g_task_listener;


//! See "check_waitpid_pairs()".
static Array g_handle_waitpid_pairs;


//! See "oom_protect()" and "oom_neglect()".
static bool oom_protected = false;


//! Used to disable OOM by writing OOM_DISABLE to /proc/self/oom_score_adj
void
oom_protect(void)
{
  if (!oom_protected)
  {
    const char protect[] = "-1000\n";
    ssize_t count = append_to_file_boldly("/proc/self/oom_score_adj", protect, 
                                          sizeof(protect) - 1);
    if (count != (sizeof(protect) - 1))
    {
      // Older kernels lack oom_score_adj, which can be relevant if we are
      // trying to deal with partner backports instead of our latest kernel.
      const char old_protect[] = "-17\n";
      count = append_to_file_boldly("/proc/self/oom_adj", old_protect, 
                                    sizeof(old_protect) - 1);
      if (count != (sizeof(old_protect) - 1))
      {
        warn("Unable to make process invulnerable to out-of-memory killer");
        return;
      }
    }
    oom_protected = true;
  }
}


//! Used to reverse the inherited effects of oom_protect()
void
oom_neglect(void)
{
  if (oom_protected)
  {
    const char neglect[] = "0\n";
    ssize_t count = append_to_file_boldly("/proc/self/oom_score_adj", neglect, 
                                          sizeof(neglect) - 1);
    if (count != (sizeof(neglect) - 1))
    {
      count = append_to_file_boldly("/proc/self/oom_adj", neglect, 
                                    sizeof(neglect) - 1);
      if (count != (sizeof(neglect) - 1))
      {
        warn("Unable to make process vulnerable to out-of-memory killer");
        return;
      }
    }
    oom_protected = false;
  }
}


// HACK: For testing.
static void
handle_echo_socket(Pollable* socket)
{
  Buffer* input = &socket->input;

  int result = Pollable_acquire(socket, 0);

  if (result > 0)
  {
    // Echo!
    Pollable_write(socket, input->data, input->size);

    // Consume.
    input->size = 0;
  }
}


// HACK: For testing.
static void
handle_echo_listener(Pollable* pollable)
{
  spew(1, "Accepting echo connection!");

  int fd = simple_accept(pollable->fd);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // ISSUE: This is never reaped.
  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Echo Socket");
  Pollable_open(socket, fd, handle_echo_socket);
}


static void
handle_tunnel_socket(Pollable* socket)
{
  uint id = (uint)(uintptr_t)socket->info;

  Buffer* input = &socket->input;

  // Leave room for packet overhead.
  int result = Pollable_acquire(socket, RPC_HEADER_SIZE + 4 + 4);

  if (result > 0)
  {
    // Multiplex the data.
    do_tunnel_s2m(&g_monitor_socket, id, input->data, input->size);

    // Consume.
    input->size = 0;
  }

  else if (result < 0)
  {
    // Multiplex the EOF.
    // HACK: Use an empty packet to indicate EOF.
    do_tunnel_s2m(&g_monitor_socket, id, NULL, 0);
  }
}


static Process*
find_process(pid_t pid)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->pid == pid)
      return process;
  }

  return NULL;
}


Thread*
find_thread_with_tid(Process* process, pid_t tid)
{
  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* other = Array_get(&process->threads, i);
    if (other->tid == tid)
      return other;
  }

  return NULL;
}


Thread*
find_thread_with_state(Process* process, ThreadState state)
{
  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* thread = Array_get(&process->threads, i);
    if (thread->state == state)
      return thread;
  }

  return NULL;
}


// Helper function for "make_process".
//
static void
Pollable_init_process(Pollable* pollable, Process* process, const char* what)
{
  Pollable_init(pollable, "%s %s", process->name, what);
  pollable->info = process;
}


static Process*
make_process(pid_t pid, char* exe)
{
  Process* process = calloc_or_die(1, sizeof(Process));

  process->pid = pid;

  snprintf(process->name, sizeof(process->name), "Process %d", process->pid);

  // Pick an initial unique negative "app_id" for the process.
  process->app_id = g_next_negative_app_id--;

  Pollable_init_process(&process->console_stdin, process, "stdin");
  Pollable_init_process(&process->console_stdout, process, "stdout");
  Pollable_init_process(&process->console_stderr, process, "stderr");

  Pollable_init_process(&process->task_socket, process, "task");

  Buffer_init(&process->rsp_buffer);

  Array_append(&g_processes, process);

  spew(2, "%s is using '%s'.", process->name, exe);

  process->executable = exe;

  if (Pollable_valid(&g_monitor_socket))
    do_process_created(&g_monitor_socket, process->pid, exe);

  return process;
}


// Rename a Thread.
//
void
rename_thread(Thread* thread)
{
  Process* process = thread->process;

  if (process->detected_threads == 0)
    snprintf(thread->name, sizeof(thread->name), "Process %d",
             process->pid);
  else if (thread->reported_index == 0)
    snprintf(thread->name, sizeof(thread->name), "Process %d#%d",
             process->pid, thread->detected_index);
  else
    snprintf(thread->name, sizeof(thread->name), "Process %d#%d/%d",
             process->pid, thread->detected_index, thread->reported_index);
}


// Make a new Thread.
//
static Thread*
make_thread(pid_t tid, Process* process, uint tile_id)
{
  Thread* thread = calloc_or_die(1, sizeof(Thread));

  thread->tid = tid;
 
  thread->process = process;

  Array_append(&process->threads, thread);

  thread->tile_id = tile_id;

  uint16_t tile_x = tile_id % g_width;
  uint16_t tile_y = tile_id / g_width;

  rename_thread(thread);

  spew(2, "%s created on tile %u,%u.", thread->name, tile_x, tile_y);

  Pollable* socket = &g_monitor_socket;
  if (Pollable_valid(socket))
    do_thread_created(socket, process->pid, thread->tid, tile_x, tile_y);

  thread->expecting_sigstop = true;
  pti_change_state(thread, THREAD_PAUSING);

  return thread;
}


// Send "note_attach".
//
void
report_attach(Process* process)
{
  process->reported_attach = true;

  spew(2, "%s reporting attach.", process->name);
  do_note_attach(&g_monitor_socket, process->pid);

  g_interactive = true;
}


// Update the "tile_id" field for a thread.
//
void
update_tile_id(Thread* thread, int tile_id)
{
  if (tile_id < 0)
  {
    // Determine the current cpu for the thread, or bail.
    tile_id = tmc_cpus_get_task_current_cpu(thread->tid);
    if (tile_id < 0)
      return;
  }

  if (thread->tile_id == tile_id)
    return;

  thread->tile_id = tile_id;

  uint16_t tile_x = tile_id % g_width;
  uint16_t tile_y = tile_id / g_width;

  Process* process = thread->process;

  spew(2, "%s moved to tile %u,%u.", thread->name, tile_x, tile_y);

  Pollable* socket = &g_monitor_socket;
  if (Pollable_valid(socket))
    do_thread_moved(socket, process->pid, thread->tid, tile_x, tile_y);
}


// Slurp from thread stdout/stderr.
//
// Note that the underlying PTY buffer seems to have room for 4095 bytes,
// and we read them in blocks of 4080 bytes, which may be inefficient.
//
static void
handle_console_traffic(Pollable* pollable)
{
  Process* process = (Process*)pollable->info;

  Buffer* input = &pollable->input;

  // Leave room for packet header plus args.
  Pollable_acquire(pollable, RPC_HEADER_SIZE + 2 + 2 + 4);

  uint8_t* data = input->data;
  uint size = input->size;

  if (size > 0)
  {
    spew_bytes(3, data, size, "%s: ", pollable->name);

    // Forward to monitor.
    if (pollable == &process->console_stdout)
      do_console_stdout(&g_monitor_socket, process->pid, data, size);
    else
      do_console_stderr(&g_monitor_socket, process->pid, data, size);

    // Consume.
    Buffer_excise(input, 0, size);
  }
}


static void
dying_process(Process* process)
{
  if (process->dying)
    return;

  process->dying = true;

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* thread = Array_get(&process->threads, i);
    if (thread->state != THREAD_DEAD)
      pti_change_state(thread, THREAD_DYING);
  }
}


void
kill_process(Process* process, const char* why)
{
  if (process->dying || process->dead)
    return;

  // TODO: Add a "process->ignoring" flag?
  if (find_thread_with_state(process, THREAD_IGNORING) != NULL)
    return;

  if (find_thread_with_state(process, THREAD_REPORTED) != NULL)
  {
    if (why == NULL)
      spew(1, "%s will be killed later!", process->name);
    else
      warn("%s will be killed later due to %s!", process->name, why);

    // HACK: The process will be not actually be killed until the
    // debugger kills/detaches/resumes the process.
    process->killing = true;
  }
  else
  {
    if (process->killing)
      spew(2, "%s can finally be killed.", process->name);
    else if (why == NULL)
      spew(1, "%s killed!", process->name);
    else
      warn("%s killed due to %s!", process->name, why);

    if (kill(process->pid, SIGKILL) != 0)
      warn_with_errno("%s could not be killed", process->name);
    dying_process(process);
  }
}


static void
kill_everyone(const char* why)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    kill_process(process, why);
  }
}


static void
kill_application(int app_id, const char* why)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->app_id == app_id)
      kill_process(process, why);
  }
}


static bool
need_app_id(int app_id)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->app_id == app_id)
      return true;
  }

  return false;
}


static void
really_exit(int code)
{
  spew(1, "Exiting...");

  if (Pollable_valid(&g_monitor_socket))
    Pollable_flush_fully(&g_monitor_socket);

  exit(code);
}


static void
handle_pending_signal(int sig)
{
  // Handle SIGCHLD silently.
  if (sig == SIGCHLD)
    return;

  // Reset to default handler.
  signal(sig, SIG_DFL);

  warn("Handling fatal signal: %s (%d).", strsignal(sig), sig);

  // Kill everyone, even IGNORING/REPORTED threads.
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->dying || process->dead)
      continue;
    if (kill(process->pid, SIGKILL) != 0)
      warn_with_errno("%s could not be killed", process->name);
  }

  really_exit(128 + sig);
}



// Determine the executable for the thread with the given tid,
// and return true, or else return false.
//
bool
determine_executable(char* buf, size_t len, pid_t tid)
{
  char what[PATH_MAX];
  snprintf(what, sizeof(what), "/proc/%d/exe", tid);
  int n = readlink_aux(what, buf, len);
  if (n < 0)
    return false;

  // HACK: Remove any " (deleted)" suffix.
  if (n >= 10 && !strcmp(buf + n - 10, " (deleted)"))
    buf[n - 10] = '\0';

  return true;
}


bool
should_debug(Thread* thread, bool check_tile_id)
{
  Process* process = thread->process;

  if (process->debug_force)
    return true;

  if (check_tile_id &&
      tmc_cpus_has_cpu(&process->debug_tiles, thread->tile_id))
    return true;

  const char* exe = process->executable;
  size_t exe_len = strlen(exe);

  for (int i = 0; i < g_debug_executables.size; i++)
  {
    char* debug_exe = StringArray_get(&g_debug_executables, i);
    size_t debug_exe_len = strlen(debug_exe);

    // Verify suffix.
    if (debug_exe_len > exe_len)
      continue;
    if (strcmp(exe + exe_len - debug_exe_len, debug_exe) != 0)
      continue;

    // Exact match.
    if (exe_len == debug_exe_len)
      return true;

    // Tail match.
    if (exe[exe_len - debug_exe_len - 1] == '/')
      return true;
  }

  return false;
}


//! Handle the "child" side of "launch_app()".
//
static void
launch_app_child(cpu_set_t* cpus, StringArray* argv)
{
  message_prefix = "[shepherd child] ";


  // Apply the affinity.
  if (tmc_cpus_set_my_affinity(cpus) != 0)
    punt_with_errno("Failed to set affinity");


  if (g_hacky_hardwall >= 0)
  {
    if (ioctl(g_hacky_hardwall, HARDWALL_ACTIVATE) != 0)
      punt_with_errno("Failed to activate the hacky hardwall");
  }


  // Let the child connect to the shepherd.
  setenv("TILERA_SHEPHERD_LISTENER", g_task_listener_name, 1);


  // Let the child be ptraced by the shepherd.
  if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0)
    punt_with_errno("Failure in 'ptrace()'");

  // HACK: Let the shepherd use "PTRACE_SETOPTIONS" before we call "execv()".
  // FIXME: We could maybe instead use the EXECING state to predict a SIGTRAP.
  raise(SIGSTOP);

  // Attempt to "execv()".
  StringArray_append(argv, NULL);
  (void)execv(argv->data[0], argv->data);

  punt_with_errno("Failure in 'execv(%s)'", argv->data[0]);
}


static void
handle_task_socket(Pollable* pollable);


//! Handle the "parent" side of "launch_app()".
//
// Note that "exe" will be NULL if called from "launch_app()", and
// non-NULL if called from "become_hacky_shepherd()".
//
static Thread*
launch_app_parent(cpu_set_t* cpus, pid_t pid, char* exe)
{
  Process* process = make_process(pid, exe);

  // Assign a positive "app_id".
  process->app_id = g_total_launches;

  if (Pollable_valid(&g_monitor_socket))
    do_process_joined(&g_monitor_socket, pid, process->app_id);

  // HACK: Pick a representative CPU.
  uint tile_id = tmc_cpus_find_first_cpu(cpus);

  Thread* thread = make_thread(pid, process, tile_id);

  return thread;
}


// Parse a tile specification, and return 0 on success, else -1.
//
static int
parse_tiles_aux(cpu_set_t* cpusp, char* tile)
{
  uint width = g_width;
  uint height = g_height;

  uint total = width * height;

  uint x, y, w, h, a, b, n;

  if (!strcmp(tile, "online") || !strcmp(tile, "all"))
  {
    *cpusp = g_cpus_online;
    return 0;
  }

  if (!strcmp(tile, "dataplane"))
  {
    *cpusp = g_cpus_dataplane;
    return 0;
  }

  if (!strcmp(tile, "standard"))
  {
    *cpusp = g_cpus_standard;
    return 0;
  }

  tmc_cpus_clear(cpusp);

  // ISSUE: This may (accidentally) handle "WxH@X".
  a = b = 0;
  if (sscanf(tile, "%ux%u@%u,%u", &w, &h, &a, &b) == 4 ||
      sscanf(tile, "%ux%u", &w, &h) == 2)
  {
    // Detect some gratuitous weirdness.
    if (a + w > width || b + h > height)
      return -1;

    for (y = b; y < b + h; y++)
    {
      for (x = a; x < a + w; x++)
      {
        n = x + y * width;
        tmc_cpus_add_cpu(cpusp, n);
      }
    }

    return 0;
  }

  if (sscanf(tile, "%u-%u", &a, &b) == 2)
  {
    // Detect some gratuitous weirdness.
    if (a > b || a >= total || b >= total)
      return -1;

    for (n = a; n <= b; n++)
      tmc_cpus_add_cpu(cpusp, n);

    return 0;
  }

  if (sscanf(tile, "%u,%u", &x, &y) == 2)
  {
    if (x >= width || y >= height)
      return -1;

    n = x + y * width;
  }
  else
  {
    char* end;
    n = strtoul(tile, &end, 10);
    if (*end != '\0' || end == tile || n >= total)
      return -1;
  }

  tmc_cpus_add_cpu(cpusp, n);
  return 0;
}


// Parse into "cpusp" an array of tile specifications, removing any
// dedicated/forbidden tiles, and return an error string, or NULL.
//
// Split each "tile" specification on space (and "+") before parsing.
//
// If "launch", default to "standard" tiles, and require a non-empty
// set of tiles, otherwise, default to all known tiles.
//
// TODO: Move this down, nearer to "perform_launch()".
//
static const char*
parse_tiles(cpu_set_t* cpusp, StringArray* tiles, bool launch)
{
  uint width = g_width;
  uint height = g_height;

  uint total = width * height;

  if (launch)
  {
    // Default to the standard tiles.
    *cpusp = g_cpus_standard;
  }
  else
  {
    // Default to all known tiles.
    tmc_cpus_clear(cpusp);
    for (uint n = 0; n < total; n++)
      tmc_cpus_add_cpu(cpusp, n);
  }

  // If the first entry in the array exists and does not start with "^",
  // then default to no tiles (below).
  bool clear = true;

  // Parse requested tiles.
  for (uint i = 0; i < tiles->size; i++)
  {
    char* tile = StringArray_get(tiles, i);
    char* next = tile;

    // Convert "+" to space.
    while ((next = strchr(next, '+')) != NULL)
      *next++ = ' ';

    for ( ; tile != NULL; tile = next)
    {
      // Tokenize on space.
      if ((next = strchr(tile, ' ')) != NULL)
        *next++ = '\0';

      bool invert = false;

      if (tile[0] == '^')
      {
        invert = true;
        tile++;
      }
      else if (clear)
      {
        tmc_cpus_clear(cpusp);
      }

      clear = false;

      cpu_set_t tmp;
      if (parse_tiles_aux(&tmp, tile) < 0)
      {
        warn("Illegal tile '%s'.", tile);
        return "Illegal tile.";
      }

      if (invert)
        tmc_cpus_remove_cpus(cpusp, &tmp);
      else
        tmc_cpus_add_cpus(cpusp, &tmp);
    }
  }

  // ISSUE: Add a way to ensure a certain minimum number of cpus,
  // or to complain about the use of dedicated/forbidden cpus.

  // Remove dedicated/forbidden tiles.
  tmc_cpus_intersect_cpus(cpusp, &g_cpus_online);

  if (launch && tmc_cpus_count(cpusp) == 0)
    return "Empty tile set.";

  return NULL;
}


//! Determine if a given path can (probably) be exec'd.
//!
static bool
can_exec(const char* path)
{
  struct stat sb;
  return (access(path, X_OK) == 0 &&
          stat(path, &sb) == 0 &&
          S_ISREG(sb.st_mode));
}


// Help "launch_app" identify the executable.
//
static char*
launch_app_aux(char* argv0)
{
  char* exe = __tmc_task_canonicalize(argv0, NULL);
  if ((exe != NULL) && can_exec(exe))
    return exe;

  bool okay = false;

  if (strchr(argv0, '/') == NULL)
  {
    // Look in the "PATH" (or a simple default path).
    char* path = strdup_or_die(getenv("PATH") ?: "/bin:/usr/bin");

    char* next = NULL;
    for (char* scan = path; !okay && scan != NULL; scan = next)
    {
      next = strchr(scan, ':');
      if (next != NULL)
        *next++ = '\0';

      // Ignore non-absolute sub-paths (including "." and "").
      if (*scan != '/')
        continue;

      free(exe);
      exe = __tmc_task_canonicalize(argv0, scan);
      okay = (exe != NULL) && can_exec(exe);
    }

    free(path);
  }

  if (okay)
    return exe;

  warn("Cannot execute '%s'.", argv0);
  free(exe);
  return NULL;
}


// Launch a thread using the given "cpus", "argv", and "envp", and return
// the new Thread, or return NULL if the launch fails.
//
// If the executable (that is, "argv->data[0]") appears to exist, then it
// will be canonicalized.  Otherwise, if it does not contain a slash, then
// we will look for it in the PATH.  Otherwise, we will fail.
//
static Thread*
launch_app(cpu_set_t* cpus, StringArray* argv, StringArray* envp)
{
  // Identify the executable.
  assert(argv->size != 0);
  char* exe = launch_app_aux(StringArray_get(argv, 0));
  if (exe == NULL)
    return NULL;

  // HACK: Pick a representative tile.
  int tile_id = tmc_cpus_find_first_cpu(cpus);

  spew(2, "Running '%s' on tile %d.", exe, tile_id);

  pid_t pid = fork();
  if (pid < 0)
    punt_with_errno("Failure in 'fork()'");

  if (pid == 0)
  {
    // Ensure that this process can be killed be OOM.
    oom_neglect();

    // Use the canonicalized executable.
    argv->data[0] = exe;

    // Apply "envp".
    for (int i = 0; i < envp->size; i++)
      putenv(StringArray_get(envp, i));

    // Handle Child.
    launch_app_child(cpus, argv);

    // Never returns.
    abort();
  }

  // Handle Parent.
  Thread* thread = launch_app_parent(cpus, pid, exe);

  pti_change_state(thread, THREAD_EXECING);

  return thread;
}


// Although extremely rare, it is possible, especially under heavy
// load, to detect a SIGSTOP for a newly cloned thread, or a newly
// forked process, before we detect the corresponding special SIGTRAP
// for the "responsible" thread.
//
// Thus, when "handle_waitpid()" detects this situation, it saves the
// "pending" tid/status pair aside in a global list, so this function
// can handle it later, when "handle_clone()" or "handle_fork()" is
// called to handle the corresponding special SIGTRAP.
//
static void
check_waitpid_pairs(Thread* thread)
{
  for (int i = 0; i < g_handle_waitpid_pairs.size; i += 2)
  {
    pid_t tid = (pid_t)(intptr_t)Array_get(&g_handle_waitpid_pairs, i);
    int status = (int)(intptr_t)Array_get(&g_handle_waitpid_pairs, i + 1);
    if (thread->tid == tid)
    {
      Array_excise(&g_handle_waitpid_pairs, i, 2);
      spew(2, "%s handling deferred waitpid status 0x%04x.",
           thread->name, status);
      pti_handle_stopped(thread, status);
      break;
    }
  }
}


// React to "parent" forking a child with the given pid.
//
// NOTE: Compare to "launch_app_parent()".
//
void
handle_fork(Thread* parent, pid_t pid, bool vfork)
{
  spew(1, "%s has %s'd a child with pid %d.",
       parent->name, vfork ? "vfork" : "fork", pid);

  char* exe = strdup_or_die(parent->process->executable);
  Process* process = make_process(pid, exe);

  process->debug_force = parent->process->debug_force;
  process->debug_tiles = parent->process->debug_tiles;
  process->debug_on_crash = parent->process->debug_on_crash;

  process->parent_pid = parent->process->pid;

  process->app_id = parent->process->app_id;

  if (Pollable_valid(&g_monitor_socket))
    do_process_joined(&g_monitor_socket, pid, process->app_id);

  // Inherit "tile_id".
  uint tile_id = parent->tile_id;

  Thread* child = make_thread(pid, process, tile_id);

  if (!parent->watch_forked_children)
  {
    // Ignore boring children.
    pti_change_state(child, THREAD_IGNORING);
  }
  else if (!parent->assume_impending_exec && should_debug(child, false))
  {
    // Debug immediately if appropriate.
    child->process->debugging = true;
    pti_change_state(child, THREAD_STOPPING);
  }

  // Leave the parent PAUSED until the child actually stops, so that
  // if we do not want to watch the child, we can "unptrace" it before
  // resuming the parent.
  child->forker = parent;
  parent->forkee = child;

  check_waitpid_pairs(child);
}


// Possibly rename a thread which has just been detected.
//
static void
detect_thread_if_needed(Thread* thread)
{
  if (thread->detected_index == 0)
  {
    Process* process = thread->process;

    thread->detected_index = ++process->detected_threads;
    rename_thread(thread);
    update_tile_id(thread, -1);
    spew(2, "%s is now a detected thread with tid %u.",
         thread->name, thread->tid);
  }
}


// Possibly rename a thread which is about to be reported to gdb.
//
void
report_thread_if_needed(Thread* thread)
{
  if (thread->reported_index == 0)
  {
    Process* process = thread->process;

    thread->reported_index = ++process->reported_threads;
    rename_thread(thread);

    spew(2, "%s is now a reported thread.", thread->name);
  }
}


// React to "parent" cloning a thread with the given tid.
//
// HACK: This is also called when attaching to a non-launched
// multi-threaded process.
//
void
handle_clone(Thread* parent, pid_t tid)
{
  detect_thread_if_needed(parent);

  Process* process = parent->process;

  uint tile_id = tmc_cpus_get_task_current_cpu(tid);

  Thread* thread = make_thread(tid, process, tile_id);

  detect_thread_if_needed(thread);

  spew(2, "%s is now being ptraced using tid %u.",
       thread->name, thread->tid);

  check_waitpid_pairs(thread);
}


// Note that "/proc/PID" exists for all tasks, but actually scanning "/proc"
// will only show the tasks which are actually processes.
//
static bool
pid_exists_in_slash_proc(pid_t pid)
{
  bool exists = false;

  DIR* dir = opendir("/proc");
  if (dir != NULL)
  {
    char want[128];
    snprintf(want, sizeof(want), "%u", pid);

    struct dirent* val;
    while ((val = readdir(dir)) != NULL)
    {
      if (!strcmp(val->d_name, want))
      {
        exists = true;
        break;
      }
    }
    (void)closedir(dir);
  }

  return exists;
}


// Scan for existing threads.
//
static void
attach_to_pid_scan_for_threads(Thread* thread)
{
  Process* process = thread->process;

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/task", process->pid);
  DIR* dir = opendir(path);
  if (dir != NULL)
  {
    struct dirent* val;
    while ((val = readdir(dir)) != NULL)
    {
      pid_t tid = atoi(val->d_name);

      // Skip "." and "..".
      if (tid == 0)
        continue;

      // Skip known threads (including "thread").
      if (find_thread_with_tid(process, tid) != NULL)
        continue;
           
      spew(2, "%s detected thread %d.", thread->name, tid);

      if (ptrace(PTRACE_ATTACH, tid, NULL, NULL) != 0)
      {
        warn_with_errno("%s failed to attach to thread %d",
                        thread->name, tid);
        continue;
      }

      handle_clone(thread, tid);
    }

    (void)closedir(dir);
  }
}


static Thread*
attach_to_pid_find_thread(pid_t tid)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->dying || process->dead)
      continue;

    Thread* thread = find_thread_with_tid(process, tid);
    if (thread != NULL)
      return thread;
  }

  return NULL;
}


// Attach to the (possibly unknown) task with the given tid, and
// return NULL, or return an adjective related to the failure.
//
// ISSUE: Currently this allows attaching to processes, and known
// worker threads, but not unknown worker threads.
//
static const char*
attach_to_pid(uint16_t tid, bool debug)
{
  Thread* thread = attach_to_pid_find_thread(tid);

  if (thread != NULL)
  {
    // Attach to a known thread.

    if (!debug)
      return NULL;

    // ISSUE: Could also check "expecting_magic_rsp_eof".
    if (thread->process->debugging || thread->process->reported_attach)
      return "debuggable";

    // ISSUE: Disallow attaching to worker threads?  If not, suggest
    // the use of "info threads" to prevent debugger confusion?

    switch (thread->state)
    {
    case THREAD_ACTIVE:
      pti_pause(thread);
      // Fall through.
    case THREAD_PAUSING:
      pti_change_state(thread, THREAD_STOPPING);
      break;
    case THREAD_PAUSED:
      // See "pti_handle_stopped()".
      thread->detected_signal = SIGTRAP;
      pti_change_state(thread, THREAD_STOPPED);
      break;
    default:
      warn("%s in unexpected state %s for attaching.",
           thread->name, ThreadState_name(thread->state));
      return "strange";
    }
  }
  else
  {
    // Attach to an unknown thread.

    // Acquire the executable, or fail.
    char executable[PATH_MAX];
    if (!determine_executable(executable, sizeof(executable), tid))
      return "undescribable";

    // Disallow attaching to worker threads.
    // ISSUE: Should we allow this, somehow?
    if (!pid_exists_in_slash_proc(tid))
      return "non-primary";

    // Start ptracing the thread, or fail.
    if (ptrace(PTRACE_ATTACH, tid, NULL, NULL) != 0)
      return "unptraceable";

    char* exe = strdup_or_die(executable);
    Process* process = make_process(tid, exe);

    uint tile_id = tmc_cpus_get_task_current_cpu(tid);

    thread = make_thread(tid, process, tile_id);

    // HACK: When the debugger detaches, we will ignore the process.
    process->ignore_on_detach = debug;

    spew(1, "%s is now being ptraced using tid %u.",
         thread->name, thread->tid);

    attach_to_pid_scan_for_threads(thread);

    if (!debug)
      return NULL;

    pti_change_state(thread, THREAD_STOPPING);
  }

  // NOTE: The state was set to STOPPING (or STOPPED) above.

  // Start debugging.
  thread->process->debugging = true;

  spew(1, "%s is now being debugged.", thread->name);

  return NULL;
}



static void
handle_waitpid(pid_t tid, int status);


// Read a magic fd from the given fd.
//
// NOTE: Normally, one would just use a blocking "recvmsg()", but that could
// deadlock if the sender, which we are ptracing, stopped for any reason.  So
// instead we use a non-blocking "recvmsg()", and we call "waitpid()", and
// "handle_waitpid()", which will continue from some stoppages.
//
// ISSUE: This is insufficient.  For example, if a process gets a signal while
// sending a magic fd, it will become PAUSED, and we will deadlock, because it
// will never be resumed.
//
static int
read_magic_fd_or_die(int fd)
{
  int empty = 0;

  // Must send/receive at least one byte.
  struct iovec iov[1] = { { .iov_base = &empty, .iov_len = 1 } };

  char msg_buf[CMSG_SPACE(sizeof(int))];

  struct msghdr msg = {
    .msg_iov = iov,
    .msg_iovlen = 1,
    .msg_control = msg_buf,
    .msg_controllen = sizeof(msg_buf)
  };

#if 0
  // HACK: This should not be necessary, but various web sources claim
  // that it is sometimes required, for example, on RH 5.2 linux.
  {
    struct cmsghdr* h = CMSG_FIRSTHDR(&msg);
    h->cmsg_level = SOL_SOCKET;
    h->cmsg_type = SCM_RIGHTS;
    h->cmsg_len = CMSG_LEN(sizeof(int));
    *((int*)CMSG_DATA(h)) = -1;
  }
#endif

  while (true)
  {
    int n = recvmsg(fd, &msg, 0);
    if (n > 0)
      break;
    if (n == 0)
      punt("Failure in 'recvmsg()'.");
    if (errno != EINTR && errno != EAGAIN)
      punt_with_errno("Failure in 'recvmsg()'");

    // HACK: make sure we unpause threads that told us they were
    // migrating, for example.  This whole loop should be inverted
    // and become part of the event loop in any case (bug 5945).
    pti_check_states();

    int status = 0;
    pid_t tid = waitpid(-1, &status, WNOHANG | __WALL);

    if (tid > 0)
    {
      handle_waitpid(tid, status);
    }
    else if ((tid == 0) || (errno == ECHILD))
    {
      continue;
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in waitpid()");
    }
  }

  struct cmsghdr* h = CMSG_FIRSTHDR(&msg);
  if (h != NULL &&
      h->cmsg_level == SOL_SOCKET &&
      h->cmsg_type == SCM_RIGHTS &&
      h->cmsg_len == CMSG_LEN(sizeof(int)))
  {
    int other;
    memcpy(&other, CMSG_DATA(h), sizeof(other));
    if (other >= 0)
      return other;
  }

  punt("Problem in 'recvmsg()'.");
}


void
perform_request_protocol(RPC rpc)
{
  spew(2, "Monitor asked us for our protocol.");

  reply_request_protocol(rpc, SHEPHERD_PROTOCOL_MONITOR);
}


void
perform_request_exit(RPC rpc)
{
  spew(1, "Monitor asked us to exit.");

  // Exit as soon as we are "idle".
  g_exit_when_idle = true;

  kill_everyone("exit request");

  reply_request_exit(rpc);
}


void
perform_request_size(RPC rpc)
{
  spew(2, "Monitor asked us for note_size.");

  do_note_size(rpc.socket, TILE_CHIP, g_width, g_height);

  reply_request_size(rpc);
}


void
perform_request_idle(RPC rpc)
{
  spew(2, "Monitor asked us for note_idle.");

  g_want_note_idle = true;

  reply_request_idle(rpc);
}


void
perform_set_clock(RPC rpc, uint sec, uint nsec)
{
  spew(2, "Monitor asked us to set the clock.");

  struct timeval tv = { sec, nsec / 1000 };

  if (settimeofday(&tv, NULL) != 0)
  {
    rpc_error(rpc, "Cannot set clock.");
    return;
  }

  // There is no "/dev/misc/rtc" on the simulator.
  // ISSUE: Actually check for "/dev/misc/rtc" existence?
  if (!sim_is_simulator())
  {
    // Attempt to set the hardware clock.
    (void)system("/sbin/hwclock -w -u");
  }

  reply_set_clock(rpc);
}


void
perform_huge_pages(RPC rpc, char* descr)
{
  spew(2, "Monitor asked us to set the number of huge pages.");

#define MAX_CONTROLLERS 4
  int pages[MAX_CONTROLLERS];
  int i = 0;
  char* s;
  for (s = descr; i < MAX_CONTROLLERS && *s != '\0'; )
  {
    char* end;
    int val = strtol(s, &end, 10);
    if (end == s)
    {
      rpc_error(rpc, "Invalid huge_pages request \"%s\"", descr);
      return;
    }
    pages[i++] = val;
    s = end;
    while (*s == ',' || *s == ' ' || *s == '\t')
      ++s;
  }
  if (i == 0)
  {
    rpc_error(rpc, "Empty huge_pages request \"%s\"", descr);
    return;
  }

  if (i == 1)
  {
    const char* path = "/proc/sys/vm/nr_hugepages";
    FILE* f = fopen(path, "w");
    if (f == NULL)
    {
      rpc_error_with_errno(rpc, "Cannot open %s.", path);
      return;
    }
    fprintf(f, "%d\n", pages[0]);
    fclose(f);
  }
  else
  {
    const size_t hpage_kb = tmc_alloc_get_huge_pagesize() / 1024;
    int rc = 0;
    for (; i >= 0; --i)
    {
      char buf[100];
      snprintf(buf, sizeof(buf),
               "/sys/devices/system/node/node%d/hugepages/hugepages-%zdkB/nr_hugepages",
               i, hpage_kb);
      FILE* f = fopen(buf, "w");
      if (f == NULL)
      {
        rc = -1;
        continue;
      }
      fprintf(f, "%d", pages[i]);
      fclose(f);
    }
    if (rc < 0)
    {
      rpc_error_with_errno(rpc, "Error setting per-node huge pages.");
      return;
    }
  }

  reply_huge_pages(rpc);
}


void
perform_launch(RPC rpc, StringArray* argv, StringArray* envp,
               StringArray* tiles, StringArray* debug)
{
  // Count the ATTEMPTED launches.
  g_total_launches++;

  // Latch "debug_next".
  bool debug_next = g_debug_next;
  g_debug_next = false;

  // Latch "spread_threads".
  bool spread_threads = g_spread_threads;
  g_spread_threads = false;

  cpu_set_t cpus;
  const char* estr = parse_tiles(&cpus, tiles, true);
  if (estr != NULL)
  {
    rpc_error(rpc, "Invalid launch tiles: %s", estr);
    return;
  }

  cpu_set_t debug_tiles;
  tmc_cpus_clear(&debug_tiles);
  if (debug->size != 0)
  {
    estr = parse_tiles(&debug_tiles, debug, false);
    if (estr != NULL)
    {
      rpc_error(rpc, "Invalid debug tiles: %s", estr);
      return;
    }
  }

  // Handle "count_cycles".
  if (g_count_cycles && g_count_cycles_start == 0)
    g_count_cycles_start = get_cycle_count();

  Thread* child = launch_app(&cpus, argv, envp);

  if (child != NULL)
  {
    Process* process = child->process;

    // Save some debug info.
    process->debug_force = debug_next;
    process->debug_tiles = debug_tiles;

    // Latch "debug_on_crash".
    // FIXME: Just let this be a global flag.
    process->debug_on_crash = g_debug_on_crash;

    if (spread_threads)
      process->spread_cpus = cpus;

    // ISSUE: Is anybody using this value?
    reply_launch(rpc, child->process->pid);
  }
  else
  {
    rpc_error(rpc, "Could not launch thread.");
  }
}



void
perform_kill_everyone(RPC rpc)
{
  kill_everyone("user request");

  reply_kill_everyone(rpc);
}



void
perform_debug_attach(RPC rpc, int tid)
{
  const char* adj = attach_to_pid(tid, true);

  if (adj != NULL)
  {
    rpc_error(rpc, "Cannot attach to %s process %d!", adj, tid);
  }
  else
  {
    reply_debug_attach(rpc);
  }
}


void
perform_debug_next(RPC rpc)
{
  g_debug_next = true;

  reply_debug_next(rpc);
}


void
perform_debug_executables(RPC rpc, StringArray* executables)
{
  StringArray_free_and_clear(&g_debug_executables);
  for (uint i = 0; i < executables->size; i++)
  {
    char* executable = strdup_or_die(StringArray_get(executables, i));
    StringArray_append(&g_debug_executables, executable);
  }

  reply_debug_executables(rpc);
}


void
perform_debug_executable(RPC rpc, char* executable)
{
  StringArray_append(&g_debug_executables, strdup_or_die(executable));

  reply_debug_executable(rpc);
}


void
perform_watch_process(RPC rpc, int tid)
{
  const char* adj = attach_to_pid(tid, false);

  if (adj != NULL)
  {
    rpc_error(rpc, "Cannot watch %s process %d!", adj, tid);
  }
  else
  {
    reply_watch_process(rpc);
  }
}


void
perform_provide_stdin(RPC rpc, int pid, uint8_t* bytes, size_t bytes_size)
{
  Process* process = find_process(pid);
  if (process == NULL)
  {
    rpc_error(rpc, "Process %u does not exist.", pid);
    return;
  }

  spew(3, "%s can read %zu bytes from the console.",
       process->name, bytes_size);

  Pollable* writer = &process->console_stdin;

  if (bytes_size == 0)
  {
    // HACK: Empty packet indicates EOF.
    Pollable_close(writer);
  }
  else if (Pollable_valid(writer))
  {
    // ISSUE: We do not detect when the process closes "stdin".
    // ISSUE: Test to see what actually happens in this case!
    Pollable_write(writer, bytes, bytes_size);
  }

  reply_provide_stdin(rpc);
}


void
perform_set_cwd(RPC rpc, char* dir)
{
  if (chdir(dir) != 0)
  {
    rpc_error_with_errno(rpc, "Failed to chdir() to '%s'", dir);
    return;
  }

  reply_set_cwd(rpc);
}


void
perform_forbid_tiles(RPC rpc, StringArray* tiles)
{
  cpu_set_t cpus;

  const char* estr = parse_tiles(&cpus, tiles, false);
  if (estr != NULL)
  {
    rpc_error(rpc, "Invalid forbid tiles: %s", estr);
    return;
  }

  tmc_cpus_remove_cpus(&g_cpus_online, &cpus);
  tmc_cpus_remove_cpus(&g_cpus_dataplane, &cpus);
  tmc_cpus_remove_cpus(&g_cpus_standard, &cpus);

  reply_forbid_tiles(rpc);
}


void
perform_unlimit_fds(RPC rpc)
{
  struct rlimit rlim;
  rlim.rlim_cur = 65536;
  rlim.rlim_max = 65536;
  if (setrlimit(RLIMIT_NOFILE, &rlim) != 0)
    rpc_error(rpc, "Failed to unlimit fds.");
  else
    reply_unlimit_fds(rpc);
}


void
perform_spread_threads(RPC rpc)
{
  g_spread_threads = true;

  reply_spread_threads(rpc);
}


void
perform_set_functional(RPC rpc, bool functional)
{
  if (functional)
    sim_enable_functional();
  else
    sim_disable_functional();

  reply_set_functional(rpc);
}


void
perform_shepherd_tiles(RPC rpc, StringArray* tiles)
{
  cpu_set_t cpus;

  const char* estr = parse_tiles(&cpus, tiles, false);
  if (estr != NULL)
  {
    rpc_error(rpc, "Invalid shepherd tiles: %s", estr);
    return;
  }

  if (tmc_cpus_set_my_affinity(&cpus) != 0)
  {
    rpc_error_with_errno(rpc, "Could not set affinity");
    return;
  }

  if (tmc_cpus_set_task_affinity(&cpus, getppid()) != 0)
  {
    rpc_error_with_errno(rpc, "Could not set parent affinity");
    return;
  }

  // ISSUE: Should also affect existing "shepherd-fuse" daemons.

  reply_shepherd_tiles(rpc);
}


// HACK: For testing.
void
perform_echo_port(RPC rpc, uint16_t port)
{
  bool ephemeral = (port == 0);
  int listener_fd = simple_listen(&port, 1);

  // HACK: Let the user know which port they can connect to.
  if (ephemeral)
    note("Echoing on local port %d.", port);

  // HACK: This is never reaped.
  Pollable* pollable = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(pollable, "Echo Listener");
  Pollable_open(pollable, listener_fd, handle_echo_listener);

  reply_echo_port(rpc);
}


// HACK: For testing.
void
perform_hacky_hardwall(RPC rpc)
{
  int fd = open_or_die("/dev/hardwall/udn", O_RDONLY, 0);

  cpu_set_t cpus;
  tmc_cpus_clear(&cpus);
  tmc_cpus_grid_add_all(&cpus);
  int err = ioctl(fd, HARDWALL_CREATE(sizeof(cpus)), &cpus);
  if (err < 0)
  {
    rpc_error(rpc, "Could not create hacky hardwall.");
    return;
  }

  g_hacky_hardwall = fd;

  reply_hacky_hardwall(rpc);
}


// HACK: For testing.
void
perform_ignore_protocol(RPC rpc)
{
  g_ignore_protocol = true;

  reply_ignore_protocol(rpc);
}


// HACK: For testing.
void
perform_count_cycles(RPC rpc)
{
  g_count_cycles = true;
  g_count_cycles_start = 0;

  reply_count_cycles(rpc);
}


// Help support non-interactive mode.
//
// Unlike most other monitor queries, "run" (technically "launch" plus
// "awaiting_app(N)") and "wait" (technically "awaiting_app(-1)") are
// not actually "done" until some future time, and so it is critical
// to avoid freezing the simulator before then.
//
// There would be a race condition right now when the monitor sends
// "do_request_idle()" and then "do_awaiting_app(-1)", if not for
// hacky details involving SPR_SIM_SOCKET.
//
void
perform_awaiting_app(RPC rpc, int app)
{
  spew(3, "Now awaiting app %d.", app);

  if ((app > 0) ? need_app_id(app) : (g_processes.size != 0))
    g_awaiting_app = app;
  else
    g_awaiting_app = 0;

  reply_awaiting_app(rpc);
}


void
perform_become_interactive(RPC rpc)
{
  g_interactive = true;

  reply_become_interactive(rpc);
}



// Handle incoming traffic on a forward socket.
//
static void
handle_forward_socket(Pollable* socket)
{
  uint id = (uint)(uintptr_t)socket->info;

  Buffer* input = &socket->input;

  // Leave room for packet overhead.
  int result = Pollable_acquire(socket, RPC_HEADER_SIZE + 4 + 4);

  if (result > 0)
  {
    spew(4, "%s read %u bytes.", socket->name, input->size);

    // Multiplex the data.
    do_forward_s2m(&g_monitor_socket, id, input->data, input->size);

    // Consume.
    input->size = 0;
  }

  else if (result < 0)
  {
    // Multiplex the EOF.
    // HACK: Use an empty packet to indicate EOF.
    do_forward_s2m(&g_monitor_socket, id, NULL, 0);
  }
}


// ISSUE: If the remote connection fails, then the monitor pretends
// that the connection was successful, but was immediately closed.
// The "proper" way to deal with this would be to defer the "accept"
// until the monitor confirms that the remote connection succeeded.
//
static void
handle_forward_listener(Pollable* pollable)
{
  uint16_t remote_port = (uint16_t)(uintptr_t)pollable->info;

  spew(2, "Accepting forward connection!");

  int fd = simple_accept(pollable->fd);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // Acquire the next available forward id.
  uint id = g_forward_sockets.size;

  // Forward the connection.
  do_forward_connect(&g_monitor_socket, id, remote_port);

  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Forward %u [%u]", id, remote_port);
  socket->info = (void*)(uintptr_t)id;
  Pollable_open(socket, fd, handle_forward_socket);

  Array_append(&g_forward_sockets, socket);
}


void
perform_forward_m2s(RPC rpc, uint id, uint8_t* bytes, size_t size)
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
      do_forward_s2m(&g_monitor_socket, id, NULL, 0);
    }

    // Reap the forward.
    Pollable_destroy(socket);
    free(socket);
    Array_set(&g_forward_sockets, id, NULL);
  }
  else if (Pollable_valid(socket))
  {
    spew(3, "%s writing %zu bytes.", socket->name, size);
    Pollable_write(socket, bytes, size);
  }

  reply_forward_m2s(rpc);
}


void
perform_forward_listen(RPC rpc, uint16_t tile_port, uint16_t host_port)
{
  bool ephemeral = (tile_port == 0);
  int listener_fd = simple_listen(&tile_port, 1);
  if (ephemeral)
    note("Forwarding tile port %u to host port %u.", tile_port, host_port);

  Pollable* pollable = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(pollable, "Forward listener [%u to %u]", tile_port, host_port);
  pollable->info = (void*)(uintptr_t)host_port;
  Pollable_open(pollable, listener_fd, handle_forward_listener);

  reply_forward_listen(rpc);
}


void
perform_tunnel_m2s(RPC rpc, uint id, uint8_t* bytes, size_t size)
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
      do_tunnel_s2m(&g_monitor_socket, id, NULL, 0);
    }

    // Reap.
    Pollable_destroy(socket);
    free(socket);
    Array_set(&g_tunnel_sockets, id, NULL);
  }
  else if (Pollable_valid(socket))
  {
    spew(3, "%s writing %zu bytes.", socket->name, size);
    Pollable_write(socket, bytes, size);
  }

  reply_tunnel_m2s(rpc);
}


void
perform_tunnel_connect(RPC rpc, uint id, uint16_t tile_port)
{
  assert(id == g_tunnel_sockets.size);

  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Tunnel %d [%d]", id, tile_port);
  socket->info = (void*)(uintptr_t)id;

  Array_append(&g_tunnel_sockets, socket);

  int fd = simple_connect_aux(NULL, tile_port);

  if (fd < 0)
  {
    rpc_error_with_errno(rpc, "Could not tunnel to port %u", tile_port);

    // HACK: Generate a synthetic close.
    do_tunnel_s2m(&g_monitor_socket, id, NULL, 0);

    return;
  }

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  Pollable_open(socket, fd, handle_tunnel_socket);

  reply_tunnel_connect(rpc);
}


void
perform_rsp_m2s(RPC rpc, int pid, uint8_t* bytes, size_t size)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->pid == pid)
    {
      rsp_handle_packet(process, bytes, size);
      reply_rsp_m2s(rpc);
      return;
    }
  }

  rpc_error(rpc, "Ignoring rsp traffic for unknown pid %d.", pid);
}


static void
handle_mount_fuse_error(void* info, char* msg)
{
  RPC* defer = (RPC*)info;
  rpc_error(*defer, msg);
  defer->socket = NULL;
}


static void
handle_mount_fuse_reply(void* info)
{
  RPC* defer = (RPC*)info;
  reply_mount_fuse(*defer);
  defer->socket = NULL;
}


void
perform_mount_fuse(RPC rpc, char* host_dir, char* tile_dir)
{
  // HACK: The simulated shepherd forwards this query to the watchdog,
  // and then waits for (and passes through) the reply/error, WITHOUT
  // freezing the simulator.  This allows the watchdog to avoid lots
  // of bizarre and inefficient SPR_SIM_SOCKET logic.
  if (g_watchdog_socket != NULL)
  {
    spew(2, "Asking watchdog to export '%s' as '%s'.", host_dir, tile_dir);

    query_mount_fuse(g_watchdog_socket,
                     handle_mount_fuse_reply, &rpc,
                     handle_mount_fuse_error, &rpc,
                     host_dir, tile_dir);

    // HACK: Wait for reply/error.
    while (rpc.socket != NULL)
      dispatch_events(-1);

    return;
  }

  // The watchdog can handle this query directly.
  watchdog_mount_fuse(rpc, host_dir, tile_dir);
}


void
perform_system(RPC rpc, char* command)
{
  int status = system(command);

  if (status != 0)
  {
    if (status < 0)
    {
      // NOTE: It is not documented whether "errno" is valid here.
      rpc_error_with_errno(rpc, "Failure in 'system(\"%s\")'", command);
    }
    else if (WIFEXITED(status))
    {
      int code = WEXITSTATUS(status);
      rpc_error(rpc, "During 'system(\"%s\")' got exit code %d.",
                command, code);
    }
    else
    {
      int sig = WTERMSIG(status);
      rpc_error(rpc, "During 'system(\"%s\")' got signal %d: %s.",
                command, sig, strsignal(sig));
    }
    return;
  }

  reply_system(rpc);
}


void
perform_ping(RPC rpc)
{
  reply_ping(rpc);
}


void
perform_dump(RPC rpc)
{
  // Dump the cycle count.
  note("Cycle count = %llu.", (unsigned long long)get_cycle_count());

  // Dump all the threads of all the processes.
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    note("%s using '%s'.", process->name, process->executable);
    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* thread = Array_get(&process->threads, i);
      uint16_t x = thread->tile_id % g_width;
      uint16_t y = thread->tile_id / g_width;
      note("%s [%u] at %u,%u (%s).",
           thread->name, thread->tid, x, y, ThreadState_name(thread->state));
    }
  }

  reply_dump(rpc);
}


void
perform_set_verbosity(RPC rpc, int verbosity)
{
  message_verbosity = verbosity;

  reply_set_verbosity(rpc);
}


void
perform_set_debug_on_crash(RPC rpc, bool flag)
{
  g_debug_on_crash = flag;

  reply_set_debug_on_crash(rpc);
}


void
perform_task_greet(RPC rpc, uint magic)
{
  Process* process = (Process*)rpc.socket->info;

  spew(3, "%s sent 'task_greet()'.", process->name);

  if (magic != SHEPHERD_PROTOCOL_TASK && !g_ignore_protocol)
  {
    rpc_error(rpc, "Invalid task_greet!");
    return;
  }

  reply_task_greet(rpc);
}


void
perform_task_watch_forked_children(RPC rpc, int tid, int flag)
{
  Process* process = (Process*)rpc.socket->info;
  Thread* thread = find_thread_with_tid(process, tid);
  if (thread == NULL)
  {
    rpc_error(rpc, "%s has no thread with tid %u.", process->name, tid);
    return;
  }
  spew(2, "%s sent 'watch_forked_children(%d)'.", thread->name, flag);
  int old = thread->watch_forked_children;
  thread->watch_forked_children = (flag != 0);
  reply_task_watch_forked_children(rpc, old);
}


void
perform_task_assume_impending_exec(RPC rpc, int tid, int flag)
{
  Process* process = (Process*)rpc.socket->info;
  Thread* thread = find_thread_with_tid(process, tid);
  if (thread == NULL)
  {
    rpc_error(rpc, "%s has no thread with tid %u.", process->name, tid);
    return;
  }
  spew(2, "%s sent 'assume_impending_exec(%d)'.", thread->name, flag);
  int old = thread->assume_impending_exec;
  thread->assume_impending_exec = (flag != 0);
  reply_task_assume_impending_exec(rpc, old);
}


void
perform_task_terminate_app(RPC rpc)
{
  Process* process = (Process*)rpc.socket->info;

  spew(1, "%s sent 'task_terminate_app()'.", process->name);

  // Kill application (including caller).
  kill_application(process->app_id, NULL);

  // HACK: No reply needed, as caller has been "killed".
}


void
perform_task_console_streams(RPC rpc)
{
  Process* process = (Process*)rpc.socket->info;

  spew(2, "%s sent 'task_console_streams()'.", process->name);

  int fd0 = read_magic_fd_or_die(rpc.socket->fd);
  set_blocking_or_die(fd0, false);
  set_close_on_exec_or_die(fd0, true);
  Pollable_open(&process->console_stdin, fd0, NULL);

  int fd1 = read_magic_fd_or_die(rpc.socket->fd);
  set_blocking_or_die(fd1, false);
  set_close_on_exec_or_die(fd1, true);
  Pollable_open(&process->console_stdout, fd1, handle_console_traffic);

  int fd2 = read_magic_fd_or_die(rpc.socket->fd);
  set_blocking_or_die(fd2, false);
  set_close_on_exec_or_die(fd2, true);
  Pollable_open(&process->console_stderr, fd2, handle_console_traffic);

  reply_task_console_streams(rpc);
}


void
perform_task_init(RPC rpc, int pid)
{
  spew(2, "Somebody sent 'task_init(%d)'.", pid);

  Process* process = find_process(pid);

  if (process == NULL)
  {
    const char* adj = attach_to_pid(pid, false);

    if (adj != NULL)
    {
      rpc_error(rpc, "Cannot attach to %s process %d!", adj, pid);
      Pollable_flush_fully(rpc.socket);
      return;
    }

    process = find_process(pid);
  }

  // ISSUE: What if "process->dying" or "process->dead"?

  // HACK: Allow closing "rpc.socket" below.
  int fd = dup_or_die(rpc.socket->fd);

  // ISSUE: This should never happen.
  if (Pollable_valid(&process->task_socket))
  {
    warn("%s replacing task socket!", process->name);
    Pollable_close(&process->task_socket);
  }

  spew(2, "%s acquired task socket!", process->name);
  Pollable_open(&process->task_socket, fd, handle_task_socket);
  reply_task_init(rpc);

  Pollable_flush_fully(rpc.socket);

  // HACK: Close the socket.
  Pollable_close(rpc.socket);
  // ISSUE: It might be simpler to do this in "handle_task_packet_early()".
  // FIXME: Bug 4364: We cannot destroy and free "rpc.socket" here.
  //--Pollable_destroy(rpc.socket);
  //--free(rpc.socket);
}



// HACK: Handle a packet.
//
static void
handle_monitor_packet(RPC rpc)
{
  if (!dispatch_packet(rpc))
  {
    warn("Ignoring unexpected packet code 0x%04x.", rpc.code);
  }
}


//! Handle traffic on the monitor socket.
//
static void
handle_monitor_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_monitor_packet) < 0)
  {
    spew(1, "Lost monitor socket: %s.", strerror(errno));
  }
}


// HACK: Handle a packet on a task socket.
//
static void
handle_task_packet(RPC rpc)
{
  // ISSUE: Ignore "QUERY_CODE_TASK_INIT"?
  if (!dispatch_task_packet(rpc))
  {
    warn("Ignoring unexpected task packet code 0x%04x.", rpc.code);
  }
}


//! Handle traffic on a task socket.
//
// HACK: We use "handle_packets_slowly()" to avoid consuming the magic fds
// sent to "perform_task_console_xxx()", which must be read using the special
// "read_magic_fd_or_die()" function.
//
static void
handle_task_socket(Pollable* socket)
{
  Process* process = (Process*)(socket->info);
  if (handle_packets_slowly(socket, handle_task_packet) < 0)
  {
    spew(2, "%s lost task socket: %s.", process->name, strerror(errno));
  }
}



//! The desired final exit code for a local shepherd.
static int g_exit_code;


// This is called with an actual status whenever "waitpid()" detects
// that a thread has exited or been terminated, and with -1 whenever
// we detach from a "boring" (or "ignore_on_detach") process.
//
// ISSUE: If thread is REPORTED, this will cause horrible confusion,
// as gdb will think it can send us packets, but we will just warn and
// ignore them.  In theory this can happen only due to SIGKILL from
// some external source.  ISSUE: Try to test this situation.
//
void
handle_death(Thread* thread, int status)
{
  Process* process = thread->process;

  // Reap later.
  pti_change_state(thread, THREAD_DEAD);

  // No longer "reported".
  if (thread->reported_index != 0)
  {
    thread->reported_index = 0;
    rename_thread(thread);
  }

  // Paranoia.
  process->current_thread_b = NULL;

  if (process->current_thread_g == thread)
  {
    spew(4, "%s lost its current 'g' thread.", thread->name);
    process->current_thread_g = NULL;
  }

  // Update the process "status" if needed.
  if (process->status == 0 && status > 0)
    process->status = status;

  if (Pollable_valid(&g_monitor_socket))
    do_thread_destroyed(&g_monitor_socket, process->pid, thread->tid);
}


static Thread*
handle_waitpid_find_thread(pid_t tid)
{
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    Thread* thread = find_thread_with_tid(process, tid);
    if (thread != NULL)
      return thread;
  }

  return NULL;
}


// Handle a positive return value from "waitpid()".
//
static void
handle_waitpid(pid_t tid, int status)
{
  Thread* thread = handle_waitpid_find_thread(tid);

  if (thread == NULL)
  {
    if (WIFSTOPPED(status))
    {
      // See "check_waitpid_pairs()".
      spew(2, "Unknown thread %d deferring waitpid status 0x%04x.",
           tid, status);
      Array_append(&g_handle_waitpid_pairs, (void*)(intptr_t)tid);
      Array_append(&g_handle_waitpid_pairs, (void*)(intptr_t)status);
    }
    else
    {
      // ISSUE: If a process is killed while it is creating threads, we
      // can detect the termination of one of the newly created threads,
      // even though we (apparently) never actually detect its creation.
      warn("Unknown thread %d got waitpid status 0x%04x.", tid, status);
    }
    return;
  }

  Process* process = thread->process;

  // Note that when a thread calls "exit()", or terminates due to a
  // signal, then all of the threads exit/terminate, in semi-random
  // order.  But when a worker thread calls "pthread_exit()", that
  // thread alone exits (and always with a code of zero).

  if (WIFEXITED(status))
  {
    int code = WEXITSTATUS(status);

    if (process->dying || code < 128)
      spew(1, "%s exited with code %d.", thread->name, code);
    else
      warn("%s exited with code %d.", thread->name, code);

    if (g_exit_code == 0)
      g_exit_code = code;

    handle_death(thread, status);

    if (code != 0)
    {
      // Assume the process is dying.
      dying_process(process);

      // Maybe kill other processes in the same app.
      if (code >= 128)
        kill_application(process->app_id, "death");
    }
  }

  else if (WIFSIGNALED(status))
  {
    int sig = WTERMSIG(status);

    if (process->dying)
    {
      spew(1, "%s terminated: %s (%d).", thread->name, strsignal(sig), sig);
      status = 0;
    }
    else
    {
      warn("%s terminated: %s (%d).", thread->name, strsignal(sig), sig);

      if (g_exit_code == 0)
        g_exit_code = 128 + sig;
    }

    handle_death(thread, status);

    // Assume the process is dying.
    dying_process(process);

    // Kill other processes in the same app.
    kill_application(process->app_id, "crash");
  }

  else //-- if (WIFSTOPPED(status))
  {
    spew(3, "%s got waitpid status 0x%04x.", thread->name, status);
    pti_handle_stopped(thread, status);
  }
}



// Free a dead, excised, process.
//
static void
free_process(Process* process)
{
  free((void*)process->executable);

  Pollable_destroy(&process->console_stdin);
  Pollable_destroy(&process->console_stdout);
  Pollable_destroy(&process->console_stderr);

  Pollable_destroy(&process->task_socket);

  Buffer_destroy(&process->rsp_buffer);

  Array_free_and_clear(&process->breakpoints);

  free(process);
}



// Free a dead, excised, thread.
//
static void
free_thread(Thread* thread)
{
  Alarm_cancel(&thread->trapping_alarm);

  free(thread);
}


// Deal with "dead" children.
//
// Note that no blocking "dispatch_events()" call may appear between the
// preceding "dispatch_events(-1)" call, and the "waitpid()" loop below,
// or a SIGCHLD might get "deferred".
//
// Since the "console" is often inherited by children, we do not "reap" a
// process until its console is no longer being used.
//
// Note that use of the "task socket" by children and/or threads is not
// supported.
//
// We avoid reaping any process (and its app) to which the debugger is still
// attached, to avoid warnings when the debugger disconnects.
//
static void
handle_dead_threads(void)
{
  // Handle deaths and signals.
  while (true)
  {
    int status = 0;
    pid_t tid = waitpid(-1, &status, WNOHANG | __WALL);

    if (tid > 0)
    {
      handle_waitpid(tid, status);
    }
    else if ((tid == 0) || (errno == ECHILD))
    {
      // ISSUE: I think I saw "ECHILD" happen once.
      break;
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in waitpid()");
    }
  }

  // Handle reaping.
  for (int k = g_processes.size - 1; k >= 0; k--)
  {
    Process* process = Array_get(&g_processes, k);

    for (int i = process->threads.size - 1; i >= 0; i--)
    {
      Thread* thread = Array_get(&process->threads, i);

      if (thread->state == THREAD_DEAD)
      {
        spew(1, "%s being reaped.", thread->name);

        Array_excise(&process->threads, i, 1);

        free_thread(thread);
      }
    }

    if (process->threads.size == 0 && !process->dead)
    {
      spew(2, "%s is now dead.", process->name);

      // Reap once debugger detached and console is closed.
      process->dead = true;

      // Close the "task socket" now.
      Pollable_close(&process->task_socket);

      rsp_handle_death(process);
    }

    if (process->dead &&
        !process->reported_attach &&
        !Pollable_valid(&process->console_stdout) &&
        !Pollable_valid(&process->console_stderr))
    {
      Array_excise(&g_processes, k, 1);

      spew(2, "%s is being reaped.", process->name);

      // XXX: ISSUE: Report actual process "death" earlier?

      spew(2, "%s reporting exit.", process->name);
      if (Pollable_valid(&g_monitor_socket))
        do_process_destroyed(&g_monitor_socket, process->pid, process->status);

      // Reap application if needed.
      int app_id = process->app_id;
      if (app_id >= 0 && !need_app_id(app_id))
      {
        spew(3, "App %d is being reaped.", app_id);

        if (Pollable_valid(&g_monitor_socket))
          do_application_destroyed(&g_monitor_socket, app_id);

        // No longer awaiting this app.
        if (g_awaiting_app == app_id)
          g_awaiting_app = 0;
      }

      free_process(process);
    }
  }
}


static void
handle_execute_reply(void* info, int status,
                     uint8_t* out_data, size_t out_size,
                     uint8_t* err_data, size_t err_size)
{
  spew(3, "Got status %d for hacky_execute.", status);

  fflush(NULL);

  write(STDOUT_FILENO, out_data, out_size);
  write(STDERR_FILENO, err_data, err_size);

  if ((status & 0x7F) != 0)
  {
    // Termination by signal.
    exit(128 + (status & 0x7F));
  }
  else
  {
    exit(status >> 8);
  }
}


static void
handle_execute_error(void* info, char* msg)
{
  spew(3, "Failed to execute: %s", msg);

  exit(128);
}


static void
handle_execute(char** args)
{
  message_prefix = "[executer] ";

  Buffer in;
  Buffer_init(&in);

  // HACK: Read in "available" stdin.
  // ISSUE: Perhaps this should be optional?
  // ISSUE: Perhaps this should be (optionally) non-blocking?
  if (true)
  {
    int fd = STDIN_FILENO;
    set_blocking_or_die(fd, false);
    while (true)
    {
      char buf[4096];
      int n = read_some_bytes_or_die(fd, buf, sizeof(buf));
      if (n <= 0)
        break;
      Buffer_write(&in, buf, n);
    }
  }

  int port = atoi(*args++);
  if (port == 0)
    punt("Usage: shepherd --execute PORT program [arg ...].");

  // HACK: Connect to the shepherd.
  int fd = simple_connect(NULL, port);
  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // Prepare to handle reply/error.
  Pollable socket;
  Pollable_init(&socket, "Execute socket");
  Pollable_open(&socket, fd, handle_monitor_socket);

  // Acquire cwd.
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd)))
    punt_with_errno("Could not get cwd");

  // Collect argv.
  StringArray argv;
  StringArray_init(&argv);
  while (*args != NULL)
    StringArray_append(&argv, *args++);

  // Collect envp.
  StringArray envp;
  StringArray_init(&envp);
  for (char** env = environ; *env != NULL; env++)
    StringArray_append(&envp, *env);

  // HACK: Use a special tag.
  rpc_set_tag_range(0xFFFF, 0xFFFF);

  spew(3, "Sending execute query...");

  // Initiate query.
  query_hacky_execute(&socket,
                      handle_execute_reply, NULL,
                      handle_execute_error, NULL,
                      cwd, &argv, &envp, in.data, in.size);

  // Wait for reply, and then exit.
  while (true)
    dispatch_events(-1);
}



// HACK: Simplify argument verification.
//
#define verify_arg(A, C) \
  do { if (!(C)) punt("Insufficient args for '%s'", (A)); } while (0)


static const char* primary_arg = NULL;

static void
verify_primary_arg(const char* arg)
{
  if (primary_arg != NULL)
    punt("Cannot combine '%s' and '%s'.", primary_arg, arg);
  primary_arg = arg;
}


//! Handle packet on a task socket before "perform_task_init()" has
//! been handled.
//!
static void
handle_task_packet_early(RPC rpc)
{
  if (rpc.code != QUERY_CODE_TASK_INIT || !dispatch_task_packet(rpc))
  {
    warn("Ignoring unexpected task packet code 0x%04x.", rpc.code);
    // FIXME: Bug 4364: We cannot destroy and free "rpc.socket" here.
    //--Pollable_destroy(rpc.socket);
    //--free(rpc.socket);
  }
}


//! Handle traffic on a task socket before "perform_task_init()" has
//! been handled.
//!
// NOTE: Uses "handle_packets_slowly()" to allow reassignment of "fd"
// by "perform_task_init()".
//
static void
handle_task_socket_early(Pollable* socket)
{
  if (handle_packets_slowly(socket, handle_task_packet_early) < 0)
  {
    spew(2, "Somebody lost task socket: %s.", strerror(errno));
  }
}


static void
handle_task_listener(Pollable* pollable)
{
  spew(2, "Accepting task socket!");

  // Like "simple_accept()", but without "set_delaying_or_die()",
  // which is not legal on PF_UNIX sockets.

  int fd;

  while (true)
  {
    fd = accept(pollable->fd, NULL, NULL);
    if (fd >= 0)
      break;
    if (errno == EINTR)
      continue;
    punt_with_errno("Failure in 'accept()'");
  }

  set_blocking_or_die(fd, false);

  set_close_on_exec_or_die(fd, true);
  set_keep_alive_or_die(fd, true);

  // FIXME: This is never reaped (see "perform_task_init()").
  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Task Socket");
  Pollable_open(socket, fd, handle_task_socket_early);
}


static void
task_listener_init(int fd)
{
  Pollable_init(&g_task_listener, "Task Listener");
  Pollable_open(&g_task_listener, fd, handle_task_listener);
}


static int
task_listener_create(bool monitor)
{
  g_task_listener_name =
    strfmt_or_die("TILERA_%s_%u_%lu",
                  monitor ? "MONITOR" : "SHEPHERD", getpid(), time(NULL));

  int fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    punt_with_errno("Failure in 'socket()'");

  // HACK: See bug 5292.  We explicitly tell "bind()" to skip the trailing
  // nuls in "addr.sun_path", to avoid annoying warnings from "netstat -a".
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path + 1, g_task_listener_name);
  //--size_t addr_size = sizeof(addr);
  size_t addr_size =
    sizeof(addr.sun_family) + 1 + strlen(g_task_listener_name);
  if (bind(fd, (struct sockaddr*) &addr, addr_size) != 0)
    punt_with_errno("Failure in 'bind()'");

  int backlog = 5;
  if (listen(fd, backlog) != 0)
    punt_with_errno("Failure in 'listen()'");

  return fd;
}




// This is a slimmed down copy of the normal shepherd event loop.
//
static void
mini_event_loop(void)
{
  while (true)
  {
    pti_check_states();

    dispatch_events(-1);

    handle_dead_threads();

    // Exit when idle.
    if (g_processes.size == 0)
      exit(g_exit_code);
  }
}


// Spawn a child and become a local shepherd.
//
static void
become_local_shepherd(cpu_set_t* cpus, char** args)
{
  message_prefix = "";

  StringArray argv;
  StringArray_init(&argv);

  while (*args != NULL)
    StringArray_append(&argv, *args++);

  StringArray envp;
  StringArray_init(&envp);

  // HACK: Avoid using app_id zero.
  g_total_launches++;

  Thread* child = launch_app(cpus, &argv, &envp);

  // NOTE: A "warning" will have already been emitted.
  if (child == NULL)
    exit(1);

  StringArray_destroy(&argv);
  StringArray_destroy(&envp);

  mini_event_loop();
}


static void
become_hacky_shepherd(cpu_set_t* cpus, char** args)
{
  message_prefix = "";

  g_task_listener_name = args[0];
  int wl_fd = atoi_or_die(args[1]);
  int pid = atoi_or_die(args[2]);

  task_listener_init(wl_fd);

  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0)
    punt("Local shepherd cannot attach to pid %d.", pid);

  // HACK: Avoid using app_id zero.
  g_total_launches++;

  char* exe;
  char exe_buf[PATH_MAX];
  if (determine_executable(exe_buf, sizeof(exe_buf), pid))
    exe = strdup_or_die(exe_buf);
  else
    exe = strdup_or_die("<unknown>");

  (void)launch_app_parent(cpus, pid, exe);

  mini_event_loop();
}


#if 0

// A hook for "message_output" which writes directly to the console.
//
static void
message_output_console(char* str)
{
  // HACK: Replace the final '\0' with '\n'.
  uint8_t* data = (uint8_t*)str;
  uint size = strlen(str);
  data[size++] = '\n';

  int fd = open_or_die("/dev/console", O_WRONLY, 0);
  write(fd, data, size);
  close(fd);
}

#endif


#ifdef MESSAGE_OUTPUT_SIMULATOR

// A hook for "message_output" which writes through the simulator.
//
static void
message_output_simulator(char* str)
{
  for (int i = 0; str[i] != '\0'; i++)
  {
    __insn_mtspr(SPR_SIM_CONTROL,
                 SIM_CONTROL_PUTC | (str[i] << _SIM_CONTROL_OPERATOR_BITS));
  }
  __insn_mtspr(SPR_SIM_CONTROL,
               SIM_CONTROL_PUTC | ('\n' << _SIM_CONTROL_OPERATOR_BITS));
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
               (SIM_PUTC_FLUSH_BINARY << _SIM_CONTROL_OPERATOR_BITS));
}

#endif

int
main(int argc, char* argv[])
{
  // Use line buffered stderr.
  setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

  message_prefix = "[shepherd] ";

#ifdef MESSAGE_OUTPUT_SIMULATOR
  message_output_hook = message_output_simulator;
#endif

  // Prevent crashes due to writes to closed sockets.
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    punt("Failed to ignore SIGPIPE.");

  // Determine the "online" tiles.
  (void)tmc_cpus_get_online_cpus(&g_cpus_online);

  // Acquire the chip size.
  int width = tmc_cpus_grid_width();
  int height = tmc_cpus_grid_height();
  if (width < 0 || height < 0)
  {
    int last_cpu;
    if ((last_cpu = tmc_cpus_find_last_cpu(&g_cpus_online)) >= 0)
    {
      /* Guess based on the hardware sizes we support. */
      if (last_cpu >= 36)
        width = height = 8;
      else
        width = height = 6;
      warn("cpu grid info unavailable: guessing %dx%d grid", width, height);
    }
    else
    {
      warn("No cpu info available: reporting 1x1 grid size\n");
      width = height = 1;
    }
  }
  g_width = width;
  g_height = height;

  oom_protect();

  // FIXME: Do not share this with "server.c".
  Pollable_init(&g_monitor_socket, "Monitor socket");

  const char* opt_pci_link = NULL;
  const char* opt_pci_stream = NULL;

  const char* opt_tmfifo_stream = NULL;

  int opt_local = 0;
  int opt_hacky = 0;

  const char* opt_monitored_shepherd = NULL;

  const char* secondary_arg = NULL;

  StringArray tiles;
  StringArray_init(&tiles);


  // Process args.
  for (int i = 1; i < argc; )
  {
    const char* arg = argv[i++];

    if (arg[0] != '-')
    {
      opt_local = i - 1;
      break;
    }

    else if (!strcmp(arg, "--") && i < argc)
    {
      opt_local = i;
      break;
    }

    // HACK: Ask the monitor to "execute" a command.
    else if (!strcmp(arg, "--execute") && i < argc)
    {
      handle_execute(argv + i);
      abort();
    }

#if 0

    // FIXME: Implement this!
    else if (!strcmp(arg, "--help"))
    {
    }

    // FIXME: Implement this.
    else if (!strcmp(arg, "--whole-app-debug"))
    {
      g_whole_app_debug = true;
    }

    // FIXME: Apply this to new processes.
    else if (!strcmp(arg, "--watch-all"))
    {
      g_watch_forked_children = true;
    }

#endif

    else if (!strcmp(arg, "--tile") && i < argc)
    {
      StringArray_append(&tiles, argv[i++]);
    }

    else if (!strcmp(arg, "--verbose"))
    {
      message_verbosity++;
    }

    // NOTE: Used only by "__tmc_task_fork_local_shepherd()".
    else if (!strcmp(arg, "--local"))
    {
      verify_arg(arg, i + 3 <= argc);
      verify_primary_arg(arg);
      opt_hacky = i;
      i = argc;
    }

    // NOTE: Used only by "fork_shepherd()".
    else if (!strcmp(arg, "--monitored-shepherd"))
    {
      verify_arg(arg, i + 1 <= argc);
      verify_primary_arg(arg);
      opt_monitored_shepherd = argv[i++];
    }

    else if (!strcmp(arg, "--net"))
    {
      secondary_arg = arg;
      g_opt_net_port = "963";
    }

    else if (!strcmp(arg, "--net-port"))
    {
      // ISSUE: Document this?
      verify_arg(arg, i + 1 <= argc);
      secondary_arg = arg;
      g_opt_net_port = argv[i++];
    }

    else if (!strcmp(arg, "--listen"))
    {
      // ISSUE: Deprecated flag.
      verify_arg(arg, i + 1 <= argc);
      secondary_arg = arg;
      g_opt_net_port = argv[i++];
      if (!strcmp(g_opt_net_port, "default"))
      {
        g_opt_net_port = "963";
        warn("Use '--net' instead of '--listen default'.");
      }
      else
      {
        warn("Use '--net-port PORT' instead of '--listen PORT'.");
      }
    }

    else if (!strcmp(arg, "--ssh"))
    {
      Pollable_init(&g_monitor_stdin, "Shepherd stdin");
      set_blocking_or_die(STDIN_FILENO, false);
      Pollable_open(&g_monitor_stdin, STDIN_FILENO, handle_monitor_socket);

      Pollable_init(&g_monitor_stdout, "Shepherd stdout");
      Pollable_set_fd(&g_monitor_stdout, STDOUT_FILENO);

      spew(2, "Started shepherd through ssh tunnel");

      secondary_arg = arg;
      g_opt_ssh = true;
    }

    else if (!strcmp(arg, "--pci"))
    {
      secondary_arg = arg;
      if (opt_pci_stream == NULL)
        opt_pci_stream = "0";
    }

    else if (!strcmp(arg, "--pci-link"))
    {
      verify_arg(arg, i + 1 <= argc);
      if (opt_pci_stream == NULL)
        opt_pci_stream = "0";
      secondary_arg = arg;
      opt_pci_link = argv[i++];
      if (!strcmp(opt_pci_link, "0"))
        opt_pci_link = NULL;
    }

    else if (!strcmp(arg, "--pci-stream"))
    {
      verify_arg(arg, i + 1 <= argc);
      secondary_arg = arg;
      opt_pci_stream = argv[i++];
    }

    else if (!strcmp(arg, "--tmfifo"))
    {
      secondary_arg = arg;
      opt_tmfifo_stream = "0";
    }

    else if (!strcmp(arg, "--tmfifo-stream"))
    {
      verify_arg(arg, i + 1 <= argc);
      secondary_arg = arg;
      opt_tmfifo_stream = argv[i++];
    }

    else if (!strcmp(arg, "--execute-port"))
    {
      verify_arg(arg, i + 1 <= argc);
      g_opt_execute_port = atoi(argv[i++]);
    }

    else
    {
      punt("Unknown option '%s'.", arg);
    }
  }


  if (secondary_arg != NULL)
    verify_primary_arg(secondary_arg);

  if (opt_local)
  {
    if (primary_arg != NULL)
      punt("Cannot combine '%s' and positional arguments.", primary_arg);
  }


  if (opt_monitored_shepherd != NULL)
  {
    // See below.
  }

  else if (opt_local != 0 || opt_hacky != 0)
  {
    // See below.
  }

  else if (g_opt_net_port != NULL || opt_pci_stream != NULL ||
           opt_tmfifo_stream != NULL || sim_is_simulator() || g_opt_ssh)
  {

#ifdef __tilegx__

    if (opt_pci_stream != NULL)
    {
      char buf[128];

      int trio_idx = -1;
      int mac_idx = -1;

      // Get PCIe ep link info.
      // Normally this looks like "246 trio0-mac0 1", but sometimes,
      // it can actually be empty if the PCIe link doesn't come up.

      const char* pdgmml = "/proc/driver/gxpci_ep_major_mac_link";
      buf[read_from_file_or_die(pdgmml, buf, sizeof(buf) - 1)] = '\0';

      char* trio_str = strstr(buf, "trio");
      if (trio_str != NULL)
        trio_idx = atoi(trio_str + strlen("trio"));

      char* mac_str = strstr(buf, "mac");
      if (mac_str != NULL)
        mac_idx = atoi(mac_str + strlen("mac"));

      if (trio_idx < 0 || mac_idx < 0)
        punt("Cannot extract trio/mac info from '%s'.", pdgmml);

      // On Gx, if a user specify a "pci-link", this link number 
      // will be used as the MAC number that the shepherd monitors.
      if (opt_pci_link != NULL) 
      {
        g_opt_pci_device =
          strfmt_or_die("/dev/%s/%s", opt_pci_link, opt_pci_stream);
      }
      else
      {
        g_opt_pci_device =
          strfmt_or_die("/dev/trio%d-mac%d/%s",
                        trio_idx, mac_idx, opt_pci_stream);
      }
    }

    if (opt_tmfifo_stream != NULL)
    {
      g_opt_tmfifo_device =
        strfmt_or_die("/dev/tmfifo/%s", opt_tmfifo_stream);
    }

#else

    if (opt_pci_stream != NULL)
    {
      if (opt_pci_link != NULL) 
      {
        g_opt_pci_device =
          strfmt_or_die("/dev/hostpci-link%s/%s",
                        opt_pci_link, opt_pci_stream);
      }
      else
      {
        g_opt_pci_device =
          strfmt_or_die("/dev/hostpci/%s", opt_pci_stream);
      }
    }

#endif 

    become_watchdog();

    // Unreachable.
    abort();
  }

  else
  {
    // NOTE: This can happen if shepherd is run with no arguments.
    punt("Usage: shepherd [...] [--] exe [args ...]");
  }


  // HACK: Force stdout to be line buffered in our descendants.
  // NOTE: The shepherd itself never writes directly to stdout.
  struct stat s;
  if (fstat(STDOUT_FILENO, &s) != 0)
    punt_with_errno("Failed to stat stdout");
  char buf[128];
  snprintf(buf, sizeof(buf), "%lu:%lu",
           (unsigned long) s.st_dev, (unsigned long) s.st_ino);
  setenv("STDIO_LBF_DEV_INO", buf, 1);


  // HACK: Use a specific portion of the "tag" space.
  rpc_set_tag_range(0x0001, 0x7FFF);


  // Register a handler for deferred signals.
  dispatch_events_expect_signals(handle_pending_signal);

  // Handle signals.
  if (signal(SIGCHLD, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGINT, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGTERM, dispatch_events_handle_signal) == SIG_ERR ||
      signal(SIGHUP, dispatch_events_handle_signal) == SIG_ERR)
  {
    punt("Failed to register signal handlers.");
  }


  // Determine the "dataplane" tiles.
  (void)tmc_cpus_get_dataplane_cpus(&g_cpus_dataplane);

  // Determine the "standard" tiles.
  g_cpus_standard = g_cpus_online;
  tmc_cpus_remove_cpus(&g_cpus_standard, &g_cpus_dataplane);

  // Determine the "initial" tiles.
  cpu_set_t cpus_initial;
  (void)tmc_cpus_get_my_affinity(&cpus_initial);

  // Determine the "favored" tiles.
  cpu_set_t cpus_favored = cpus_initial;
  tmc_cpus_remove_cpus(&cpus_favored, &g_cpus_dataplane);

  // HACK: Temporarily move to the last "favored" tile.
  // NOTE: If "cpus_favored" is empty, this will have no effect.
  (void)tmc_cpus_set_my_cpu(tmc_cpus_find_last_cpu(&cpus_favored));
  (void)tmc_cpus_set_my_affinity(&cpus_initial);


  if (opt_hacky != 0)
  {
    if (opt_hacky + 3 != argc)
      punt("Somebody is trying to run an old-style local shepherd!");

    become_hacky_shepherd(&cpus_initial, argv + opt_hacky);
    abort();
  }


  // Create a listener to which descendants can connect.
  task_listener_init(task_listener_create(!opt_local));

  if (opt_local)
  {
    // NOTE: The child will default to running on the "standard"
    // tiles, regardless of the shepherd's initial affinity.
    cpu_set_t cpus;
    const char* estr = parse_tiles(&cpus, &tiles, true);
    if (estr != NULL)
      punt("Invalid launch tiles: %s", estr);

    StringArray_destroy(&tiles);

    become_local_shepherd(&cpus, argv + opt_local);
    abort();
  }


  // ISSUE: Make sure tiles is empty, or use it for something.
  StringArray_destroy(&tiles);


  // NOTE: Only a monitored shepherd can get here.

  int fd = atoi_or_die(opt_monitored_shepherd);
  spew(2, "Talking to watchdog via fd %u.", fd);
  set_close_on_exec_or_die(fd, true);

  Pollable* socket = &g_monitor_socket;

  if (sim_is_simulator())
  {
    // Use the linux socket to talk to the watchdog.
    g_watchdog_socket = calloc_or_die(1, sizeof(*g_watchdog_socket));
    Pollable_init(g_watchdog_socket, "Watchdog socket");
    Pollable_open(g_watchdog_socket, fd, handle_monitor_socket);

    // Use a special socket to talk to the monitor.
    fd = SIM_SOCKET_ID + 0;
    __insn_mtspr(SPR_SIM_SOCKET, fd);

    // HACK: Create an image file if needed.
    // ISSUE: This should use "SPR_SIM_CONTROL".
    __insn_mtspr(SPR_SIM_SOCKET, SIM_SOCKET_MAYBE_CREATE_IMAGE);
  }

  Pollable_open(socket, fd, handle_monitor_socket);

  // HACK: Reduce initial churning due to "upload" queries.
  Buffer_reserve(&socket->input, 32768);

  while (true)
  {
    // Perform state transitions.
    // ISSUE: Move this below "dispatch_events()"?
    pti_check_states();

    // HACK: Avoid pointless interruptions.
    if (socket->output.size != 0)
      Pollable_flush(socket);

    int msecs = -1;

    bool did_something = false;

    if (sim_is_simulator())
    {
      // Since SPR_SIM_SOCKET has no effect on "dispatch_events()", we
      // must poll it directly, and then call "dispatch_events()" with
      // a small timeout, so we can poll it again.
      //
      // With SIM_SOCKET_BLOCKING, "handle_packets_slowly()" will
      // freeze the simulator until a packet has actually been read,
      // helping us to avoid non-determinism.
      //
      // ISSUE: Is "100ms" too slow (in human time), or too "fast"?
      //

      if (!g_interactive && g_awaiting_app == 0)
      {
        spew(3, "Freezing until a monitor packet is ready.");
        socket->fd |= SIM_SOCKET_BLOCKING;
      }

      // Similar to "handle_monitor_socket()".
      int handled = handle_packets_slowly(socket, handle_monitor_packet);

      socket->fd &= ~SIM_SOCKET_BLOCKING;

      if (handled != 0)
      {
        // Handle corrupt packets.
        if (handled < 0)
          punt("Lost SPR_SIM_SOCKET to monitor!");

        if (socket->output.size != 0)
          Pollable_flush(socket);

        // Avoid yielding.
        did_something = true;
      }

      // When awaiting, only check monitor socket occasionally.
      // Otherwise, check for events, but without actually waiting.
      msecs = (!g_interactive && g_awaiting_app != 0) ? 100 : 0;
    }

    // Handle events.
    did_something = dispatch_events(msecs) || did_something;

    handle_dead_threads();

    if (g_processes.size == 0)
    {
      if (g_count_cycles && g_count_cycles_start != 0)
      {
        uint64_t cycles = get_cycle_count() - g_count_cycles_start;

        do_note_cycles(socket, cycles);

        g_count_cycles = false;
        g_count_cycles_start = 0;
      }

      if (g_want_note_idle)
      {
        g_want_note_idle = false;
        do_note_idle(socket);
        spew(2, "Sent note_idle.");
      }

      // No longer awaiting any app.
      g_awaiting_app = 0;

      if (g_exit_when_idle)
        really_exit(0);
    }

    if (sim_is_simulator())
    {
      // We must flush explicitly.
      if (socket->output.size != 0)
        Pollable_flush_fully(socket);

      // Give other threads a chance if necessary.
      else if (!did_something)
        sched_yield();
    }
  }
}

