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

// This file mostly involves state changes on threads.


// Includes "tools/handy/handy.h" and thus "common/include/tilera.h".
#include "common.h"

#include <limits.h>

#include <sys/syscall.h>

#include <sys/wait.h>

#include <sched.h>

#include <linux/ptrace.h>


//! The names of the ThreadStates.
//
static const char* const thread_state_names[] = {
  "NEW",
  "ACTIVE",
  "TRAPPING",
  "EXECING",
  "IGNORING",
  "PAUSING",
  "STOPPING",
  "FORCING",
  "STOPPED",
  "FORCED",
  "REPORTED",
  "PAUSED",
  "DYING",
  "DEAD"
};


const char*
ThreadState_name(ThreadState state)
{
  return thread_state_names[state];
}



//! Set by "pti_change_state()" and cleared by "pti_mention_states()".
static bool g_mention_states;

//! Used by "pti_change_state()" to trigger "pti_mention_states()".
static Alarm g_mention_states_alarm;


// Mention recent thread state transitions to the monitor.
//
static void
pti_mention_states(Alarm* alarm)
{
  if (!g_mention_states)
    return;

  g_mention_states = false;

  Pollable* socket = &g_monitor_socket;
  if (!Pollable_valid(socket))
    return;

  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);

    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* thread = Array_get(&process->threads, i);

      if (thread->old_state != thread->state)
      {
        do_thread_transitioned(socket, process->pid, thread->tid,
                               thread_state_names[thread->state]);
        thread->old_state = thread->state;
      }
    }
  }
}


// Call "pti_mention_states()" later if needed.
//
// The length of the delay is chosen to avoid annoying human
// observers, so on the (slow) simulator, we use a "zero" delay.
//
static void
pti_mention_states_later(void)
{
  if (g_mention_states)
    return;

  if (!Pollable_valid(&g_monitor_socket))
    return;

  Alarm* alarm = &g_mention_states_alarm;

  // Lazily initialize the alarm.
  alarm->handler = pti_mention_states;

  // Schedule the alarm.
  timeval_now(&alarm->when);
  if (!sim_is_simulator())
    timeval_add_msecs(&alarm->when, 100);
  Alarm_schedule(alarm);

  g_mention_states = true;
}


//! Set by "pti_change_state()" and cleared by "pti_check_states()".
static bool g_changed_states;


void
pti_check_states_later(Process* process)
{
  g_changed_states = true;
}


void
pti_change_state(Thread* thread, ThreadState state)
{
  Process* process = thread->process;

  const char* old_tsn = thread_state_names[thread->state];
  const char* new_tsn = thread_state_names[state];

  spew(2, "%s changing state from %s to %s.", thread->name, old_tsn, new_tsn);

  thread->state = state;

  pti_check_states_later(process);

  pti_mention_states_later();

  // Release "forker", if any.
  Thread* forker = thread->forker;
  if (forker != NULL)
  {
    forker->forkee = NULL;
    thread->forker = NULL;
  }

  switch (state)
  {
  case THREAD_STOPPED:
    // Newly debuggable threads are reported immediately.
    if (!process->reported_attach)
      pti_change_state(thread, THREAD_FORCED);
    break;
  case THREAD_FORCED:
    // Only one thread per process should be FORCED.
    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* other = Array_get(&process->threads, i);
      if (other->state == THREAD_FORCING)
        pti_change_state(other, THREAD_PAUSING);
    }
    break;
  default:
    break;
  }
}



// Simple wrapper around "ptrace()".
//
// Since a thread can die at any time, "ptrace()" failure cannot be fatal.
//
// ISSUE: If the process is "dying", then failure is actually "expected".
// This may have happened in several builds, so I have tweaked the warning
// to help us make sure we understand what is happening.
//
// WARNING: Do not use this function with "PTRACE_PEEK*".
//
int
pti_ptrace(Thread* thread, enum __ptrace_request request,
           void* addr, void* data)
{
  int n = ptrace(request, thread->tid, addr, data);
  if (n != 0)
  {
    warn_with_errno("%s [%s]%s cannot handle 'ptrace(%d)'",
                    thread->name, ThreadState_name(thread->state),
                    thread->process->dying ? " (dying)" : "",
                    (uint)request);
  }
  return n;
}



void
pti_pause(Thread* thread)
{
  if (!thread->expecting_sigstop)
  {
    spew(2, "%s is being paused with SIGSTOP.", thread->name);
    if (syscall(SYS_tkill, thread->tid, SIGSTOP) != 0)
    {
      warn_with_errno("%s refused to be paused with SIGSTOP", thread->name);
      return;
    }
    thread->expecting_sigstop = true;
  }
  pti_change_state(thread, THREAD_PAUSING);
}



// Help transition a thread from STOPPED/FORCED to REPORTED.
//
static void
pti_change_state_reported(Thread* thread)
{
  Process* process = thread->process;

  // HACK: To avoid confusion, we avoid creating a new debugger until
  // the previous debugger has disconnected (at which point we must
  // call "pti_check_states_later()".  This might happen, for example,
  // when a debuggable process calls "exec".
  if (process->expecting_magic_rsp_eof)
  {
    spew(2, "%s cannot be reported yet.", thread->name); 
    return;
  }

  if (process->detected_threads != 0)
  {
    // ISSUE: In theory this cannot happen, due to various bits of logic
    // that ensure that only one thread can be reported at a time.
    if (find_thread_with_state(process, THREAD_REPORTED) != NULL)
    {
      warn("%s cannot be reported yet!", thread->name); 
      return;
    }

    // Activate the thread.
    process->current_thread_g = thread;
  }

  update_tile_id(thread, -1);

  pti_change_state(thread, THREAD_REPORTED);

  if (!process->reported_attach)
    report_attach(process);
  else
    rsp_handle_last_signal(thread);
}



// HACK: When a thread is "stepped" by "pti_resume()", it is expected to
// stop almost immediately, but if the instruction blocks, say, waiting for
// network traffic from a suspended thread, then it might "deadlock".  So,
// we use "trapping_alarm" to detect this situation, and change the thread
// state to ACTIVE, which allows the other threads to resume, in case the
// thread is waiting for action from one of them.  We also use this alarm
// to allow threads to be "continued" briefly before resuming any other
// threads, since "continue" is actually used to implement "step"/"next".
//
// ISSUE: Is "100ms" the optimal timeout for the alarm?
//
// ISSUE: This code has not been tested very much.
//


static void
handle_trapping_alarm(Alarm* alarm)
{
  Thread* thread = (Thread*)(alarm->info);

  int level = thread->resume_step ? 1 : 3;
  spew(level, "%s took too long to step/continue.", thread->name);

  if (thread->state == THREAD_TRAPPING)
    pti_change_state(thread, THREAD_ACTIVE);
}


// Resume a PAUSED thread.  If "trapping" is true, then transition to
// TRAPPING and schedule an alarm to force eventual transition to
// ACTIVE.  Otherwise, just transition to ACTIVE immediately.
//
// ISSUE: Stepping with a signal has never been tested!
//
void
pti_resume(Thread* thread, bool trapping)
{
  assert(thread->state == THREAD_PAUSED);
  assert(!thread->process->killing);

  const char* name = thread->name;

  if (!thread->has_been_spread && thread->detected_index != 0)
  {
    Process* process = thread->process;
    thread->has_been_spread = true;
    int total = tmc_cpus_count(&process->spread_cpus);
    if (total != 0)
    {
      int n = thread->detected_index - 1;
      int cpu = tmc_cpus_find_nth_cpu(&process->spread_cpus, n % total);
      tmc_cpus_set_task_cpu(cpu, thread->tid);
      update_tile_id(thread, cpu);
    }
  }

  // NOTE: Signals only get attempted once.
  int sig = thread->resume_signal;
  void* sigptr = (void*)(intptr_t)(sig);
  thread->resume_signal = 0;

  if (thread->resume_step)
  {
    if (sig != 0)
      spew(2, "%s resuming with step (%s).", name, signal_name(sig));
    else
      spew(2, "%s resuming with step.", name);

    pti_ptrace(thread, PTRACE_SINGLESTEP, NULL, sigptr);
  }
  else
  {
    if (sig != 0)
      spew(2, "%s resuming with continue (%s).", name, signal_name(sig));
    else
      spew(2, "%s resuming with continue.", name);

    pti_ptrace(thread, PTRACE_CONT, NULL, sigptr);
  }

  Alarm* alarm = &thread->trapping_alarm;

  Alarm_cancel(alarm);

  if (trapping)
  {
    pti_change_state(thread, THREAD_TRAPPING);

    // Lazily initialize the alarm.
    alarm->handler = handle_trapping_alarm;
    alarm->info = thread;

    // Schedule the trapping alarm.
    timeval_now(&alarm->when);
    timeval_add_msecs(&alarm->when, 1);
    Alarm_schedule(alarm);
  }
  else
  {
    pti_change_state(thread, THREAD_ACTIVE);
  }
}


void
pti_check_states_aux(Array* threads, int app_id)
{
  bool states[NUM_THREAD_STATES];

  // Collect the threads and their states.
  memset(states, 0, sizeof(states));
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);

    if (process->app_id != app_id)
      continue;

    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* thread = Array_get(&process->threads, i);
      Array_append(threads, thread);
      states[thread->state] = true;
    }
  }


  // Suspend ACTIVE threads if needed.
  if (states[THREAD_ACTIVE] &&
      (states[THREAD_EXECING] ||
       states[THREAD_STOPPING] ||
       states[THREAD_FORCING] ||
       states[THREAD_STOPPED] ||
       states[THREAD_FORCED] ||
       states[THREAD_REPORTED]))
  {
    for (uint i = 0; i < threads->size; i++)
    {
      Thread* thread = Array_get(threads, i);

      if (thread->state == THREAD_ACTIVE)
        pti_pause(thread);
    }

    // Wait for quiescence.
    return;
  }


  // Wait for quiescence.
  if (states[THREAD_PAUSING] ||
      states[THREAD_IGNORING] ||
      states[THREAD_EXECING] ||
      states[THREAD_TRAPPING] ||
      states[THREAD_STOPPING] ||
      states[THREAD_FORCING] ||
      states[THREAD_DYING] ||
      states[THREAD_DEAD])
    return;


  // Report all FORCED threads.
  if (states[THREAD_FORCED])
  {
    for (uint i = 0; i < threads->size; i++)
    {
      Thread* thread = Array_get(threads, i);

      if (thread->state == THREAD_FORCED)
        pti_change_state_reported(thread);
    }

    // Wait for debugging.
    return;
  }

  // Wait for debugging.
  if (states[THREAD_REPORTED])
    return;


  // ISSUE: When multiple threads are STOPPED, only the "oldest" one
  // is ever reported to the debugger.  It might be better to report
  // the one which has been stopped the longest.

  // Report one STOPPED thread.
  if (states[THREAD_STOPPED])
  {
    for (uint i = 0; i < threads->size; i++)
    {
      Thread* thread = Array_get(threads, i);

      if (thread->state == THREAD_STOPPED)
      {
        pti_change_state_reported(thread);
        return;
      }
    }
  }


  // Resume all PAUSED threads.
  if (states[THREAD_PAUSED])
  {
    for (uint i = 0; i < threads->size; i++)
    {
      Thread* thread = Array_get(threads, i);

      // Wait until "forkee" stops.
      if (thread->forkee != NULL)
        continue;

      if (thread->state == THREAD_PAUSED)
        pti_resume(thread, false);
    }
  }
}


// Perform all possible automatic state transitions.
//
// NOTE: This function does not try to be very efficient.
//
void
pti_check_states(void)
{
  while (g_changed_states)
  {
    g_changed_states = false;

    Array threads;
    Array_init(&threads);

    int last_app_id = 0;
    for (uint k = 0; k < g_processes.size; k++)
    {
      Process* process = Array_get(&g_processes, k);
      if (last_app_id != process->app_id)
      {
        last_app_id = process->app_id;
        pti_check_states_aux(&threads, last_app_id);
        Array_clear(&threads);
      }
    }

    Array_destroy(&threads);
  }
}


static bool
pti_breakpoint_check(Thread* thread, ulong addr)
{
  Array* breakpoints = &thread->process->breakpoints;
  for (unsigned int i = 0; i < breakpoints->size; i++)
  {
    Breakpoint* breakpoint = Array_get(breakpoints, i);
    if (breakpoint->addr == addr)
      return true;
  }
  return false;
}


static bool
handle_exec(Thread* thread)
{
  Process* process = thread->process;

  spew(1, "%s has called exec.", thread->name);

  if (process->detected_threads != 0)
  {
    // HACK: When a thread calls "execve()", all the worker threads "die",
    // and are detected as dead, and then the initial thread reports the
    // "execve()".  However, if a worker thread called "execve()", its
    // death is not actually detected (and, in fact, calling "waitpid()"
    // on it will yield ECHILD).  To deal with this bizarre situation, we
    // silently "unptrace", and simulate the death of, all the (living)
    // threads, except "thread" itself (which will be the initial thread).
    // The "unptrace" should never succeed, in theory.
    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* other = Array_get(&process->threads, i);
      if (other != thread && other->state != THREAD_DEAD)
      {
        spew(1, "%s was presumably abandoned.", other->name);
        (void)ptrace(PTRACE_DETACH, other->tid, NULL, NULL);
        handle_death(other, -1);
      }
    }
  }

  if (thread->state != THREAD_EXECING)
  {
    update_tile_id(thread, -1);
  }

  process->detected_threads = 0;
  process->reported_threads = 0;

  thread->detected_index = 0;
  thread->reported_index = 0;

  thread->has_been_spread = false;

  rename_thread(thread);

  // Simulate process death.
  rsp_handle_death(process);

  // Forget breakpoints.
  Array_free_and_clear(&process->breakpoints);

  // Check for a new executable.
  char executable[PATH_MAX];
  if (determine_executable(executable, sizeof(executable), thread->tid))
  {
    free((void*)process->executable);
    process->executable = strdup_or_die(executable);
    spew(1, "%s is now using '%s'.", thread->name, process->executable);
  }

  if (thread->state != THREAD_EXECING)
  {
    if (Pollable_valid(&g_monitor_socket))
    {
      do_process_execed(&g_monitor_socket, process->pid,
                        process->executable);
    }
  }

  // Debug if appropriate.
  if (should_debug(thread, true))
  {
    process->debugging = true;
    thread->detected_signal = SIGTRAP;
    pti_change_state(thread, THREAD_STOPPED);
    return true;
  }

  return false;
}


void
pti_handle_stopped(Thread* thread, int status)
{
  const char* name = thread->name;

  Process* process = thread->process;

  int sig = WSTOPSIG(status);


  // HACK: Pass "pthreads" signals through to the thread.
  // FIXME: Avoid hard-coding the values "32" and "33".
  // ISSUE: Should this ever use "PTRACE_SINGLESTEP"?  That is, should
  // this just be treated as a spontaneous, non-debuggable, signal?
  if (sig == 32 || sig == 33)
  {
    void* sigptr = (void*)(intptr_t)(sig);
    spew(2, "%s being passed along pthreads signal %d.", name, sig);
    pti_ptrace(thread, PTRACE_CONT, NULL, sigptr);
    return;
  }


  // NOTE: This will be a "PTRACE_EVENT_xxx" value, or zero.
  uint st_what = (sig == SIGTRAP) ? (status >> 16) : 0;

  // NOTE: This will be a "tid" (for "clone") or a "pid" (for "fork")
  // or an exit code (?).
  ulong st_msg = 0;

  // See below.
  int st_code = 0;
  ulong st_addr = 0;
  uint st_pid = 0;

  if (st_what != 0)
  {
    pti_ptrace(thread, PTRACE_GETEVENTMSG, 0, &st_msg);
    spew(2, "%s got a special SIGTRAP (%d, msg %lu).", name, st_what, st_msg);
  }
  else if (sig == SIGTRAP)
  {
    // NOTE: For "special SIGTRAP", the results of "PTRACE_GETSIGINFO"
    // appear to be completely undefined, so we handle it specially above.
    // When using "PTRACE_O_TRACEEXEC", the old "exec" SIGTRAP is unused.

    // NOTE: For "exec", "si_code" is 0, and "si_pid" is the calling "pid".
    // NOTE: For "exec", the pc is the address of "_start" (often 0x10180).

    // NOTE: For "kill", "si_code <= 0", and "si_pid" is the sending "pid".

    // NOTE: For "breakpoints" and "single step", "si_code" is TRAP_BRKPT,
    // and "si_addr" is the pc (even though this is not documented).

    siginfo_t si;
    if (pti_ptrace(thread, PTRACE_GETSIGINFO, 0, &si) == 0)
    {
      st_code = si.si_code;
      if (st_code <= 0)
        st_pid = (uint)si.si_pid;
      else
        st_addr = (ulong)(uintptr_t)si.si_addr;
    }

    if (st_code <= 0)
      spew(2, "%s got a SIGTRAP (code %d) from %d.", name, st_code, st_pid);
    else if (st_code != TRAP_BRKPT)
      spew(2, "%s got a SIGTRAP (code %d) at 0x%08lx.",
           name, st_code, st_addr);
    else if (pti_breakpoint_check(thread, st_addr))
      spew(2, "%s hit a real breakpoint at 0x%08lx.", name, st_addr);
    else
      spew(2, "%s got a fake breakpoint at 0x%08lx.", name, st_addr);
  }
  else
  {
    spew(2, "%s got a %s.", name, signal_name(sig));
  }


  if (thread->forker != NULL)
  {
    // Remove inherited breakpoints once child actually exists.
    Array* breakpoints = &thread->forker->process->breakpoints;
    for (int i = 0; i < breakpoints->size; i++)
    {
      Breakpoint* breakpoint = Array_get(breakpoints, i);
      ulong addr = breakpoint->addr;
      if (addr != -1UL)
      {
        spew(2, "%s inherited a breakpoint at 0x%lx", process->name, addr);
        pid_t pid = process->pid;
        size_t len = sizeof(breakpoint->insn);
        if (ptrace_write_bytes(pid, addr, &breakpoint->insn, len) != len)
          warn("%s inherited an unremovable breakpoint at 0x%lx!",
               process->name, addr);
      }
    }
  }


  if (thread->state == THREAD_IGNORING)
  {
    // HACK: We must handle "migrate" notifications that happen before
    // the initial SIGSTOP.
    if (st_what == PTRACE_EVENT_MIGRATE)
    {
      spew(2, "%s being resumed in expectation of being ignored.", name);
      pti_ptrace(thread, PTRACE_CONT, NULL, NULL);
      return;
    }

    // ISSUE: Is this possible?
    if (sig != SIGSTOP)
      warn("%s ignoring unexpected signal %d.", name, sig);

    // Ignore the thread.
    // HACK: We do not pass "sig" along, even if it is not "SIGSTOP".
    spew(1, "%s will now be ignored.", name);
    pti_ptrace(thread, PTRACE_DETACH, NULL, NULL);
    handle_death(thread, -1);
    return;
  }


  // FIXME: HACK: Bug 7400 describes a bug in linux which is allowing
  // a "migrate" notification to arrive after a "death" notification,
  // which we work around here.
  if (thread->state == THREAD_DYING && st_what == PTRACE_EVENT_MIGRATE)
  {
    spew(2, "%s ignoring migration notification while dying.", name);
    pti_ptrace(thread, PTRACE_CONT, NULL, NULL);
    return;
  }


#if 0
  // Update tile.  Unfortunately, this can break the implicit assumption
  // encoded by "--debug-tile", which relies on "thread->tile_id" being
  // the "assumed" tile (the first tile in the initial affinity set).
  update_tile_id(thread, -1);
#endif


  // Request notification about various system calls and migration.
  // In theory, this is not needed for cloned threads and forked
  // children which inherit these flags from their creator.
  if (!thread->called_ptrace_setoptions)
  {
    uint flags =
      (PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
       PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC |
       PTRACE_O_TRACEMIGRATE);
    pti_ptrace(thread, PTRACE_SETOPTIONS, NULL, (void*)(intptr_t)flags);
    thread->called_ptrace_setoptions = true;
  }


  // HACK: Deal with launched process.
  if (thread->state == THREAD_EXECING && sig == SIGSTOP)
  {
    spew(2, "%s being resumed in expectation of 'exec'.", name);
    pti_ptrace(thread, PTRACE_CONT, NULL, NULL);
    thread->expecting_sigstop = false;
    return;
  }


#ifdef SUPPORT_WATCHPOINT_PACKETS

  // FIXME: Should this check "st_code"?
  thread->watchpoint_type = NULL;
  thread->watchpoint_addr = 0;
  if (sig == SIGTRAP && st_what == 0 && sim_is_simulator())
  {
    struct SimQueryWatchpointStatus ws =
      sim_query_watchpoint(thread->tid);
    if (ws.syscall_status == 0)
    {
      thread->watchpoint_type = (const char*)ws.user_data;
      thread->watchpoint_addr = ws.address;
    }
  }

#endif


  thread->detected_signal = 0;

  Alarm_cancel(&thread->trapping_alarm);

  // Detect when "stepping" is complete.
  if (thread->resume_step && st_addr != 0)
    thread->resume_step = false;


  int expected_signal = -1;

  switch (thread->state)
  {
  case THREAD_ACTIVE: 
  case THREAD_TRAPPING:
  case THREAD_PAUSING:
    expected_signal = 0;
    break;
  case THREAD_STOPPING:
  case THREAD_FORCING:
    expected_signal = SIGSTOP;
    break;
  case THREAD_EXECING:
    expected_signal = SIGTRAP;
    break;
  default:
    warn("%s in unexpected state %s for handling signal %d (%d).",
         name, thread_state_names[thread->state], sig, st_what);
    break;
  }

  if (thread->expecting_sigstop && sig == SIGSTOP)
  {
    thread->expecting_sigstop = false;

    if (expected_signal == 0)
    {
      spew(2, "%s paused as expected with %s.", name, signal_name(sig));
      pti_change_state(thread, THREAD_PAUSED);
      return;
    }
  }

  if (expected_signal == 0)
  {
    spew(2, "%s stopped spontaneously with %s.", name, signal_name(sig));
  }
  else if (expected_signal == sig)
  {
    spew(2, "%s stopped as expected with %s.", name, signal_name(sig));
  }
  else
  {
    // This often happens when a spontaneous breakpoint SIGTRAP arrives
    // while we are waiting for a SIGSTOP.  This could also happen if a
    // signal is raised while stepping.  See bug 3387 for more info.
    spew(1, "%s stopped with %s not %s.",
         name, signal_name(sig), signal_name(expected_signal));
  }


  if (st_what != 0)
  {
    if (st_what == PTRACE_EVENT_MIGRATE)
    {
      int cpu = st_msg;
      spew(2, "%s has migrated to cpu %d from cpu %d.",
           name, cpu, thread->tile_id);
      if (thread->state != THREAD_EXECING)
        update_tile_id(thread, cpu);
    }

    else if (st_what == PTRACE_EVENT_CLONE)
    {
      pid_t tid = st_msg;
      spew(1, "%s has clone'd thread %d.", name, tid);
      handle_clone(thread, tid);
    }

    else if (st_what == PTRACE_EVENT_FORK || st_what == PTRACE_EVENT_VFORK)
    {
      handle_fork(thread, st_msg, st_what == PTRACE_EVENT_VFORK);
    }

    else if (st_what == PTRACE_EVENT_EXEC)
    {
      // NOTE: It seems that "st_msg" has no useful meaning.

      if (handle_exec(thread))
        return;
    }

    // Ignore "special SIGTRAP".
    pti_change_state(thread, THREAD_PAUSED);
    return;
  }


  if (!process->debugging)
  {
    // Handle debug on crash.
    if (process->debug_on_crash)
    {
      if (sig == SIGSTOP ||
          sig == SIGQUIT || sig == SIGILL || sig == SIGABRT ||
          sig == SIGFPE || sig == SIGSEGV || sig == SIGBUS ||
          sig == SIGTRAP || sig == SIGXCPU || sig == SIGXFSZ)
      {
        process->debugging = true;

        // ISSUE: The debugger never mentions the initial signal, perhaps
        // because it expects to ignore the first signal (which is usually
        // a SIGTRAP or SIGSTOP), so we must do so, on its behalf.
        warn("%s caught %s.", thread->name, signal_name(sig));
        do_note_crash(&g_monitor_socket, process->pid, sig);
      }
    }

    // Unless debugging, pass along signals.
    if (!process->debugging)
    {
      spew(2, "%s being passed along a %s.", name, signal_name(sig));
      thread->resume_signal = sig;
      pti_change_state(thread, THREAD_PAUSED);
      return;
    }
  }


  ThreadState new_state = THREAD_STOPPED;

  if (sig == SIGSTOP)
  {
    switch (thread->state)
    {
    case THREAD_STOPPING:
      // Pretend that a SIGTRAP was detected, so that gdb will not ask
      // for a SIGSTOP to be delivered when the thread is resumed.
      sig = SIGTRAP;
      break;
    case THREAD_FORCING:
      // Pretend that a SIGINT was detected, so that gdb will act like
      // an actual "interrupt" occurred.
      sig = SIGINT;
      new_state = THREAD_FORCED;
      break;
    }
  }

  thread->detected_signal = sig;

  pti_change_state(thread, new_state);
}
