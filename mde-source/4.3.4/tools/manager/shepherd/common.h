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

// Define this to support "$X" packets.
#define SUPPORT_WRITE_BINARY_PACKETS

// Define this to support "$Z"/"$z" packets.
// Note that they will only be supported under the simulator!
#define SUPPORT_WATCHPOINT_PACKETS

// Define this to support "$vCont" packets.
#define SUPPORT_VCONT_PACKETS


// Includes "common/include/tilera.h" and various system headers.
#include "tools/handy/handy.h"

#include "tools/manager/shepherd/defs.h"

#include "tools/manager/shepherd/gen_rpc.h"
#include "tools/manager/shepherd/gen_rpc_task.h"


#include <fcntl.h>

#include <signal.h>

// HACK: We include "sys/ptrace.h" AFTER "signal.h", to clean up after
// some incompatible "defines" in "asm/ptrace.h".
#include <sys/ptrace.h>

#ifdef SUPPORT_WATCHPOINT_PACKETS
#include <arch/sim.h>
#endif

#include <tmc/cpus.h>

// For "tile_bundle_bits" and "TILE_BPT_BUNDLE".
#include <arch/opcode.h>


//! Information about a breakpoint.
//!
typedef struct _Breakpoint {

  //! The address of the breakpoint.
  unsigned long addr;

  //! The original instruction.
#ifdef __tilegx__
  tilegx_bundle_bits insn;
#else
  tile_bundle_bits insn;
#endif

} Breakpoint;



//! The various "states" a thread can be in.
//
// WARNING: Must stay in sync with "thread_state_names".
//
// Most states will transition strangely if "gdb" crashes.
// Most states will transition to KILLED in "kill_process()".
// Many states will transition to DEAD on "death"/"detach".
//
typedef enum {

  //! Thread is being initialized.
  //! Will transition to ACTIVE in the local shepherd.
  //! Will transition to PAUSING on "launch" or "thread detected".
  //! Will transition to EXECING on debuggable "launch".
  //! Will transition to STOPPING on "attach".
  THREAD_NEW,

  //! Thread is running normally.
  //! Will transition to PAUSING on "suspend".
  //! Will transition to FORCING on "interrupt".
  //! Will transition to STOPPING on "attach".
  //! Will transition to STOPPED on spontaneous signal/etc.
  //! LATER: Will transition to PAUSED while forking a child.
  THREAD_ACTIVE,

  //! Thread has just been resumed via step/continue.
  //! Will transition to ACTIVE if the trapping alarm expires.
  //! Will transition to STOPPED on spontaneous signal/etc.
  THREAD_TRAPPING,

  //! Thread expects to stop (with SIGTRAP) due to "exec".
  //! Will transition to PAUSED/STOPPED once it actually stops.
  THREAD_EXECING,

  //! Thread expects to stop with SIGSTOP and then be "ignored".
  THREAD_IGNORING,

  //! Thread expects to stop temporarily with SIGSTOP.
  //! Will transition to FORCING on "interrupt".
  //! Will transition to STOPPING on "attach".
  //! Will transition to STOPPED on spontaneous signal.
  //! Will transition to PAUSED once it actually stops.
  THREAD_PAUSING,

  //! Thread expects to stop for debugging due to "attach".
  //! Will transition to FORCING on "interrupt".
  //! Will transition to STOPPED once it actually stops.
  THREAD_STOPPING,

  //! Thread expects to stop for debugging due to "interrupt".
  //! Will transition to FORCED once it actually stops.
  THREAD_FORCING,

  //! Thread has stopped, and is waiting to be debugged.
  //! Will transition to REPORTED once application has quiesced.
  THREAD_STOPPED,

  //! Thread has stopped, and will be reported to the debugger.
  THREAD_FORCED,

  //! Thread has stopped, and has been reported to the debugger.
  //! Will transition to PAUSED due to "step"/"continue".
  THREAD_REPORTED,

  //! Thread has stopped, and is waiting for everyone to be ready.
  //! Will transition to ACTIVE when the application is resumed.
  //! Will transition to STOPPED on "attach".
  //! Will transition to FORCED on "interrupt".
  THREAD_PAUSED,

  //! Process is dying (or been killed), but thread is not yet dead.
  THREAD_DYING,

  //! Thread has died (or been detached).
  THREAD_DEAD

} ThreadState;


//! The total number of ThreadState values.
#define NUM_THREAD_STATES (THREAD_DEAD + 1)


// Forward declarations.
typedef struct _Thread Thread;
typedef struct _Process Process;


// A Thread, which belongs to a Process.
//
// At most one thread can be REPORTED in any multi-threaded process,
// and various RSP packets implicitly refer to this "reportee" thread.
//
struct _Thread
{
  //! The process to which we belong.
  Process* process;

  //! The thread id.  This can be used with many functions that claim
  //! to take a "pid", including all the "cpus" and "ptrace" functions.
  //! Note that the first thread of every process has a tid equal to the
  //! pid of the process, and will not exit until the whole process exits.
  //! Note that "/proc/<tid>/" exists for all threads, but a directory
  //! listing of "/proc" shows only "/proc/<pid>/" for actual processes.
  pid_t tid;

  //! The name (for debug spew).
  char name[64];

  //! The (original) tile "id".
  uint tile_id;

  // The "state" of the thread.
  ThreadState state : 8;

  // The last reported "state" for the thread.
  ThreadState old_state : 8;

  // We are expecting a SIGSTOP.  This is set true whenever something causes
  // us to expect a SIGSTOP, and remains true until the SIGSTOP is detected.
  // This flag will be true when the state is PAUSING, STOPPING, or FORCING.
  // It can also be true if, for example, we transition to STOPPED because
  // a spontaneous signal was detected, and then later transition to ACTIVE.
  bool expecting_sigstop;

  // Set true once "PTRACE_SETOPTIONS" has been used.
  bool called_ptrace_setoptions;

  //! Used to detect when TRAPPING should change to ACTIVE.
  Alarm trapping_alarm;

  //! If true, resume using "step" (instead of "continue").
  bool resume_step;

  //! If non-zero, resume using this signal.
  uint8_t resume_signal;

  //! The signal that caused the current stoppage.
  // ISSUE: We should probably track "siginfo" as well.
  uint8_t detected_signal;

  //! The active "forker", if any.  Cleared when the forkee changes
  //! state.  Used to help manage the "forkee" field of the "forker".
  Thread* forker;

  //! The active "forkee", if any.  While non-null, this thread cannot
  //! be resumed.  Cleared when the forkee changes state.
  Thread* forkee;

#ifdef SUPPORT_WATCHPOINT_PACKETS

  // The watchpoint involved in the current stoppage, if any.
  const char* watchpoint_type;
  ulong watchpoint_addr;

#endif

  // ISSUE: We may want to have an array of "pending actions", including
  // reinserting the creation breakpoint, delivering a signal, etc.

  //! If the process is multi-threaded, the creation order, else zero.
  uint detected_index;

  //! If the process is multi-threaded, the index used by the current
  //! debugger, if any, else zero.
  uint reported_index;

  //! The "watch forked children" flag.
  bool watch_forked_children;

  //! The "assume impending exec" flag.
  bool assume_impending_exec;

  //! True if "spread_threads" has been applied.
  bool has_been_spread;
};


// A Process, which contains Threads.
//
struct _Process
{
  //! The threads which we contain.
  Array threads;

  //! The process id.
  uint pid;

  // The name (for debug spew).
  char name[32];

  // The first non-zero exit/crash status of any thread.
  int status;

  // Set when a "note_attach" query is sent to the monitor, and
  // cleared when the monitor sends back a magic EOF packet.  This
  // flag can thus be true after "debugging" has become false.
  bool reported_attach;

  // Set when "death" is reported to the debugger, or the debugger
  // sends us a kill/detach rsp packet, and cleared when the monitor
  // sends back a magic EOF packet.  Threads cannot be REPORTED
  // while in this state.  If a magic EOF packet arrives while this
  // field is false, then gdb must have "crashed" (or been "killed"
  // by the monitor).
  bool expecting_magic_rsp_eof;

  // True if this process should be ignored (treated as "dead") when
  // the debugger detaches from it.  This is used only when attaching
  // to a previously unknown process.  FIXME: Is this really needed?
  bool ignore_on_detach;

  //! True if we will kill this process (with SIGKILL) as soon we get
  //! a kill/detach/resume rsp packet for its REPORTED thread.
  bool killing;

  //! True if process is dying, due to being killed, or detection of
  //! pending process termination (non-zero thread exit, or signal).
  //! When true, all threads will be DYING (or DEAD).
  bool dying;

  //! True if all threads have been reaped.  Normally this would mean
  //! that the process could be reaped, but the "console" might hold
  //! us alive.
  bool dead;

  // True if we are debugging this process.  Possibly set true when a
  // process is created, or attached to, or calls "exec".  Set false
  // when we send a "death" notification to gdb, when gdb sends us a
  // kill/detach rsp packet, or the monitor sends us an unexpected
  // magic EOF packet.
  bool debugging;

  // True if we should start debugging whenever an "interesting"
  // signal (i.e. a potential "crash") is detected by "ptrace",
  // instead of simply passing along the signal.
  bool debug_on_crash;

  // True if we should debug this process and all of its descendants.
  bool debug_force;

  //! We will start debugging whenever "exec" is called and the first
  //! cpu in the current affinity includes one of these tiles.
  cpu_set_t debug_tiles;

  //! If non-empty, each time a thread is detected, we will migrate it
  //! to the next available cpu in this set, wrapping as needed.
  cpu_set_t spread_cpus;

  //! A non-zero id shared by all processes in an "application".  This
  //! is positive for launched apps, and negative for all other apps.
  //! Note that "application" is poorly defined for non-launched apps.
  int app_id;

  //! The total number of threads which have ever been "detected".
  //! This is used to initialize "Thread::detected_index".  If this is
  //! non-zero then the process is (or was) "multi-threaded".
  uint detected_threads;

  //! The total number of threads which have been "reported" to the
  //! debugger.  This is used to initialize "Thread::reported_index".
  uint reported_threads;

  // The number of threads reported via "$qsThreadInfo".
  uint scan_query_threads;

  // See the "thread creation breakpoint" code.
  Thread* current_thread_b;

  // The current "general" thread, if any.
  Thread* current_thread_g;

  //! The breakpoints, if any.
  Array breakpoints;

  //! The executable, if known.
  const char* executable;

  //! The parental pid, or zero if not known.
  pid_t parent_pid;

  // The "stdin" provided by "tmc_task_monitor_console()".
  Pollable console_stdin;

  // The "stdout" provided by "tmc_task_monitor_console()".
  Pollable console_stdout;

  // The "stderr" provided by "tmc_task_monitor_console()".
  Pollable console_stderr;

  // The task socket.
  Pollable task_socket;

  // A buffer for constructing RSP packets.
  Buffer rsp_buffer;
};


//! Value from "--execute-port".
extern uint16_t g_opt_execute_port;

//! The "--net-port" argument (or "963" for "--net"), if any.
extern const char* g_opt_net_port;

//! The "--pci" device, if any.
extern const char* g_opt_pci_device;

//! The "--tmfifo" device, if any.
extern const char* g_opt_tmfifo_device;

//! Width of the chip, in tiles.
extern uint g_width;

//! Height of the chip, in tiles.
extern uint g_height;

//! Array of processes.
extern Array g_processes;

//! Connection to the monitor (often indirectly through the watchdog).
extern Pollable g_monitor_socket;

//! The "--ssh" argument.
//! In case of --ssh, the shepherd stdin and stdout are redirected to the
//! ssh tunnel between the monitor.
extern bool g_opt_ssh;

//! Shepherd stdin, connect to monitor through ssh tunnel.
extern Pollable g_monitor_stdin;

//! Shepherd stdout, connect to monitor through ssh tunnel.
extern Pollable g_monitor_stdout;


// From "main.c".

extern Thread*
find_thread_with_tid(Process* process, pid_t tid);

extern Thread*
find_thread_with_state(Process* process, ThreadState state);

extern void
rename_thread(Thread* thread);

extern void
report_attach(Process* process);

extern void
update_tile_id(Thread* thread, int tile_id);

extern void
kill_process(Process* process, const char* why);

extern bool
determine_executable(char* buf, size_t len, pid_t tid);

extern bool
should_debug(Thread* thread, bool check_tile_id);

extern void
handle_fork(Thread* parent, pid_t pid, bool vfork);

extern void
report_thread_if_needed(Thread* thread);

extern void
handle_clone(Thread* parent, pid_t tid);

extern void
handle_death(Thread* thread, int status);


// From "ptrace.c".

extern bool
ptrace_read_byte(pid_t tid, ulong addr, uint8_t* bytep);

extern size_t
ptrace_read_bytes(pid_t tid, ulong addr, void* bytes, size_t num);

extern bool
ptrace_write_byte(pid_t tid, ulong addr, uint8_t byte);

extern size_t
ptrace_write_bytes(pid_t tid, ulong addr, const void* bytes, size_t num);


// From "threads.c".

const char*
ThreadState_name(ThreadState state);

extern void
pti_check_states_later(Process* process);

extern void
pti_change_state(Thread* thread, ThreadState state);

extern int
pti_ptrace(Thread* thread, enum __ptrace_request request,
           void* addr, void* data);

extern void
pti_pause(Thread* thread);

extern void
pti_resume(Thread* thread, bool trapping);

extern void
pti_check_states(void);

extern void
pti_handle_stopped(Thread* thread, int status);


// From "rsp.c".

//! Call "rsp_note_bytes(D, S, ...)", if @ref message_verbosity >= "V".
#define rsp_spew_bytes(V, D, S, ...) \
  do { \
    if (message_verbosity >= (V)) \
      rsp_note_bytes(D, S, __VA_ARGS__); \
  } while (0)

//! Basically, call "note(format, ...)", but appending the first "size"
//! bytes of data, escaping certain bytes with backslash, and decoding
//! run-length encoded sequences.
extern void
rsp_note_bytes(const void* data, size_t size, const char* format, ...)
  __attribute__((format(printf, 3, 4)));

extern void
rsp_handle_last_signal(Thread* reportee);

extern void
rsp_handle_packet(Process* process, const uint8_t* data, size_t size);

extern void
rsp_handle_death(Process* process);


// From "server.c".

extern void
watchdog_mount_fuse(RPC rpc, char* host_dir, char* tile_dir);

extern void
become_watchdog(void);


