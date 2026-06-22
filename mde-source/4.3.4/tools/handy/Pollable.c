// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.


// ISSUE: We may need a way to "reset" the registered pollables after a
// "fork()", if the child does not intend to do an "exec()".  This would
// probably include clearing "pollables_array", and taking extra steps if
// "dispatch_events()" was in progress.

// ISSUE: The UART seems to be in "echo" mode, so the monitor and the
// shepherd both end up seeing an extra copy of everything they send.
// HACK: We use different seq chars ranges for each direction, knowing
// that "unexpected" packets will then be silently dropped.

// HACK: If the monitor crashes, and then a new monitor starts talking to
// the shepherd over the UART, the sequence numbers might not line up, so
// to avoid confusion, the monitor sends a special "reset" packet.

// WARNING: It is not "safe" to call "Alarm_remove()" or "Alarm_schedule()"
// inside standard signal handlers.

// TODO: We could replace "g_pollables" by two linked lists, one of normal
// Pollables and one of Pollables with either "readable" or "writable" set,
// removing Pollables from the second list ONLY once both flags become false.


#include "Pollable.h"

#include "message.h"
#include "various.h"

#include "Array.h"

#include <signal.h>


#ifdef __tile__

// HACK: For "SPR_SIM_SOCKET".
#include <arch/spr_def.h>

#endif // __tile__



// The names for the standard signals, using tile/x86 signal numbers.
//
static const char* const signal_names[] = {
  "",
  "SIGHUP",
  "SIGINT",
  "SIGQUIT",
  "SIGILL",
  "SIGTRAP",
  "SIGABRT",
  "SIGBUS",
  "SIGFPE",
  "SIGKILL",
  "SIGUSR1",
  "SIGSEGV",
  "SIGUSR2",
  "SIGPIPE",
  "SIGALRM",
  "SIGTERM",
  "SIGSTKFLT",
  "SIGCHLD",
  "SIGCONT",
  "SIGSTOP",
  "SIGTSTP",
  "SIGTTIN",
  "SIGTTOU",
  "SIGURG",
  "SIGXCPU",
  "SIGXFSZ",
  "SIGVTALRM",
  "SIGPROF",
  "SIGWINCH",
  "SIGIO",
  "SIGPWR",
  "SIGUNUSED"
};


const char*
signal_name(uint sig)
{
  if (sig > 0 && sig < NELEM(signal_names))
    return signal_names[sig];

  return strsignal(sig);
}



void
timeval_now(struct timeval* now)
{
  (void)gettimeofday(now, NULL);
}


// ISSUE: Note also "timersub()".
void
timeval_diff(struct timeval* diff,
             const struct timeval* x,
             const struct timeval* y)
{
  if (x->tv_sec > y->tv_sec && x->tv_usec < y->tv_usec)
  {
    diff->tv_sec = x->tv_sec - 1 - y->tv_sec;
    diff->tv_usec = 1000000 + x->tv_usec - y->tv_usec;
  }
  else if (x->tv_sec > y->tv_sec ||
           (x->tv_sec == y->tv_sec && x->tv_usec > y->tv_usec))
  {
    diff->tv_sec = x->tv_sec - y->tv_sec;
    diff->tv_usec = x->tv_usec - y->tv_usec;
  }
  else
  {
    diff->tv_sec = 0;
    diff->tv_usec = 0;
  }
}


void
timeval_elapsed(struct timeval* elapsed, struct timeval* since)
{
  struct timeval now;
  timeval_now(&now);
  timeval_diff(elapsed, &now, since);
}


void
timeval_add_usecs(struct timeval* x, uint usecs)
{
  x->tv_usec += usecs % 1000000;
  x->tv_sec += usecs / 1000000;

  if (x->tv_usec >= 1000000)
  {
    x->tv_usec -= 1000000;
    x->tv_sec++;
  }
}


void
timeval_add_msecs(struct timeval* x, uint msecs)
{
  x->tv_usec += (msecs % 1000) * 1000;
  x->tv_sec += msecs / 1000;

  if (x->tv_usec >= 1000000)
  {
    x->tv_usec -= 1000000;
    x->tv_sec++;
  }
}


// ISSUE: Note also "timercmp()".
int
timeval_compare(const struct timeval* x, const struct timeval* y)
{
  if (x->tv_sec < y->tv_sec)
    return -1;

  if (x->tv_sec > y->tv_sec)
    return 1;

  if (x->tv_usec < y->tv_usec)
    return -1;

  if (x->tv_usec > y->tv_usec)
    return 1;

  return 0;
}



//! A linked list of Alarms which will fire in the future.
static Alarm* g_waiting_alarms;

//! A linked list of Alarms which are being fired right now.
static Alarm* g_firing_alarms;


bool
Alarm_scheduled(Alarm* alarm)
{
  return alarm->list != NULL;
}


// Insert an Alarm into the given list, sorted by expiration.
//
// The third argument can just be the same as the second argument, or, for
// extra efficiency, can be "&alarm->next" from a previously inserted Alarm,
// if that Alarm is known to NOT have an earlier expiration.
//
static void
Alarm_insert(Alarm* alarm, Alarm** list, Alarm** nextp)
{
  for (Alarm* scan = *nextp; scan != NULL; scan = *nextp)
  {
    if (timeval_compare(&alarm->when, &scan->when) < 0)
      break;

    nextp = &scan->next;
  }

  alarm->list = list;
  alarm->next = *nextp;
  *nextp = alarm;
}


// Remove an Alarm from its current list, if any.
//
static bool
Alarm_remove(Alarm* alarm)
{
  Alarm** nextp = alarm->list;

  if (nextp != NULL)
  {
    for (Alarm* scan = *nextp; scan != NULL; scan = *nextp)
    {
      if (scan == alarm)
      {
        // Excise.
        *nextp = alarm->next;
        alarm->next = NULL;
        alarm->list = NULL;

        return true;
      }

      nextp = &scan->next;
    }
  }

  return false;
}


bool
Alarm_schedule(Alarm* alarm)
{
  bool scheduled = Alarm_remove(alarm);
  Alarm_insert(alarm, &g_waiting_alarms, &g_waiting_alarms);
  return scheduled;
}


bool
Alarm_cancel(Alarm* alarm)
{
  bool scheduled = Alarm_remove(alarm);
  if (scheduled && alarm->cleanup != NULL)
    (*alarm->cleanup)(alarm);
  return scheduled;
}



// The registered Pollables.
static Array g_pollables;


// Set true whenever a Pollable is unregistered, indicating that
// we need to collapse the NULL entries in "g_pollables".
static bool g_collapse_pollables;


bool
Pollable_valid(Pollable* pollable)
{
  // FIXME: HACK: See "Pollable_destroy".
  assert(pollable->fd >= -1);

  return (pollable->fd >= 0);
}


void
Pollable_set_fd(Pollable* pollable, int fd)
{
  // FIXME: HACK: See "Pollable_destroy".
  assert(pollable->fd >= -1);

  // Detach.
  if (pollable->fd >= 0)
  {
    if (pollable->private_index >= 0)
    {
      g_pollables.data[pollable->private_index] = NULL;
      pollable->private_index = -1;
      pollable->readable = false;
      pollable->writable = false;

      // See "dispatch_events()".
      g_collapse_pollables = true;
    }

    pollable->fd = -1;
  }

  // Attach.
  if (fd >= 0)
  {
    pollable->fd = fd;

    // HACK: Wacky Pollables do not use "select()".
    if (fd >= POLLABLE_WACKY_FD)
      return;

    pollable->private_index = g_pollables.size;

    Array_append(&g_pollables, pollable);
  }
}


void
Pollable_set_handle_readable(Pollable* pollable,
                             Pollable_handler_func handle_readable)
{
  // FIXME: HACK: Help detect logic errors.
  if (!Pollable_valid(pollable) && handle_readable != NULL)
    warn("%s cannot set 'handle_readable'!", pollable->name);

  pollable->handle_readable = handle_readable;
}


void
Pollable_set_handle_writable(Pollable* pollable,
                             Pollable_handler_func handle_writable)
{
  // FIXME: HACK: Help detect logic errors.
  if (!Pollable_valid(pollable) && handle_writable != NULL)
    warn("%s cannot set 'handle_writable'!", pollable->name);

  pollable->handle_writable = handle_writable;
}


void
Pollable_open(Pollable* pollable, int fd,
              Pollable_handler_func handle_readable)
{
  Pollable_set_fd(pollable, fd);
  pollable->handle_readable = handle_readable;
}


void
Pollable_close(Pollable* pollable)
{
  int fd = pollable->fd;

  if (!Pollable_valid(pollable))
    return;

  Pollable_set_fd(pollable, -1);

#ifdef __tile__
  if (fd >= SIM_SOCKET_ID)
  {
#ifdef SIM_SOCKET_CLOSE
    __insn_mtspr(SPR_SIM_SOCKET, SIM_SOCKET_CLOSE);
#endif
    return;
  }
#endif // __tile__

  if (fd >= POLLABLE_WACKY_FD)
    return;

  close_or_die(fd);
}



#ifdef __tile__

// Like "read_some_bytes_or_die()", but using SPR_SIM_SOCKET.
//
static ssize_t
read_some_bytes_or_die_spr(int fd, uint8_t* buf, size_t count)
{
#if 0
  // Request a specific socket.
  __insn_mtspr(SPR_SIM_SOCKET, fd);
#endif

  bool blocking = ((fd & SIM_SOCKET_BLOCKING) != 0);

  size_t n = 0;
  while (n < count)
  {
    // Implement "blocking" by freezing the simulator until data is
    // available for the following read.
    if (blocking)
      __insn_mtspr(SPR_SIM_SOCKET, SIM_SOCKET_WAIT);

    int v = __insn_mfspr(SPR_SIM_SOCKET);
    if (v >= 0)
    {
      // Read one byte.
      buf[n++] = v;
    }
    else
    {
      // Normally "v == SIM_SOCKET_UNREADY".

#ifdef SIM_SOCKET_EOF
      if (v == SIM_SOCKET_EOF && n == 0)
      {
        // Immediate EOF.
        errno = EPIPE;
        return -1;
      }
#endif

      // Short read or nothing available yet.
      break;
    }
  }

  // Full read or partial read.
  return n;
}

#endif // __tile__


static ssize_t
Pollable_read_internal(Pollable* pollable, size_t count, 
                       ssize_t (*read_func)(int fd, void* buf, size_t count))
{
  Buffer* input = &pollable->input;

  uint size = input->size;

  // Make sure there is room.
  Buffer_reserve(input, count);

  uint8_t* buf = input->data + size;

  int n;
  int fd = pollable->fd;
  if (fd >= POLLABLE_WACKY_FD)
  {
    n = 0;
#ifdef __tile__
    if (fd >= SIM_SOCKET_ID)
      n = read_some_bytes_or_die_spr(fd, buf, count);
#endif // __tile__
  }
  else
  {
    n = read_func(pollable->fd, buf, count);
  }

  if (n > 0)
  {
    // Full read or short read.
    spew(3, "%s read %u bytes.", pollable->name, n);
    input->size = size + n;
  }
  else if (n < 0)
  {
    // Immediate EOF.
    spew(2, "%s got immediate EOF while reading.", pollable->name);
    Pollable_close(pollable);
  }

  return n;
}


ssize_t
Pollable_read(Pollable* pollable, size_t count)
{
  return Pollable_read_internal(pollable, count, read_some_bytes_or_die);
}


ssize_t
Pollable_read_partial(Pollable* pollable, size_t count)
{
  return Pollable_read_internal(pollable, count, read_uninterrupted_or_die);
}


int
Pollable_acquire(Pollable* pollable, size_t room)
{
  Buffer* input = &pollable->input;

  // Paranoia.
  assert(room <= 1024);

  // If there is room for "4096 - room" bytes, this will have no effect,
  // otherwise, it will expand "limit" to 4096 (or a higher power of two).
  Buffer_reserve(input, 4096 - room);

  // Then we want to fill "input" completely, except for "room" bytes.
  uint want = (input->limit - input->size) - room;

  ssize_t n = Pollable_read(pollable, want);
  return (n <= 0) ? n : ((n < want) ? 1 : 2);
}


void
Pollable_flush(Pollable* pollable)
{
  Buffer* output = &pollable->output;

  int fd = pollable->fd;

  if (!Pollable_valid(pollable))
    punt("%s cannot be flushed!", pollable->name);

  uint head = output->head;
  uint size = output->size - head;
  uint8_t* data = output->data + head;

#ifdef __tile__

  if (fd >= SIM_SOCKET_ID)
  {
    // Write.
    for (int i = 0; i < size; i++)
    {
      __insn_mtspr(SPR_SIM_SOCKET, data[i]);
    }

    // Flush.
    __insn_mtspr(SPR_SIM_SOCKET, SIM_SOCKET_FLUSH);

    // Forget.
    Buffer_clear(output);

    // Fully flushed.
    Pollable_set_handle_writable(pollable, NULL);

    return;
  }

#endif // __tile__

  ssize_t len = write_some_bytes_or_die(fd, data, size);

  spew(3, "%s flushed %zd/%u bytes.", pollable->name, len, size);

  if (len > 0)
  {
    // Consume.
    head += len;
    if (head >= output->size / 2)
    {
      Buffer_excise(output, 0, head);
      head = 0;
    }
    output->head = head;
  }

  // Reset "handle_writable" if needed.
  Pollable_handler_func handle_writable =
    (pollable->output.size > 0) ? Pollable_flush : NULL;
  Pollable_set_handle_writable(pollable, handle_writable);
}


void
Pollable_flush_later(Pollable* pollable)
{
  Pollable_set_handle_writable(pollable, Pollable_flush);

  if (pollable->fd >= POLLABLE_WACKY_FD)
  {
    // ISSUE: Wacky Pollables have no "handle_writable" support in
    // "dispatch_events()".  HACK: We can at least flush once now.
    Pollable_flush(pollable);
  }
}


void
Pollable_flush_fully(Pollable* pollable)
{
  Pollable_flush(pollable);

  if (pollable->fd >= POLLABLE_WACKY_FD)
  {
    // HACK: Wacky Pollables cannot be fully flushed.

    if (pollable->output.size != 0)
      warn("%s cannot be fully flushed!", pollable->name);

    return;
  }

  while (pollable->output.size != 0)
  {
    spew(1, "%s is not yet fully flushed!", pollable->name);

    int fd = pollable->fd;

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);

    // Wait for up to one second.
    struct timeval timeout = { 1, 0 };
    int selectable = select(fd + 1, NULL, &writefds, NULL, &timeout);
    if (selectable < 0 && errno != EINTR)
      punt_with_errno("Failure in 'select()'");

    Pollable_flush(pollable);
  }
}


void
Pollable_write(Pollable* pollable, const void* data, size_t count)
{
  Buffer_write(&pollable->output, data, count);
  Pollable_flush_later(pollable);
}



void
Pollable_init(Pollable* pollable, const char* format, ...)
{
  memset(pollable, 0, sizeof(Pollable));

  va_list args;
  va_start(args, format);
  pollable->name = vstrfmt_or_die(format, args);
  va_end(args);

  pollable->private_index = -1;
  pollable->fd = -1;

  Buffer_init(&pollable->input);
  Buffer_init(&pollable->output);
}


void
Pollable_destroy(Pollable* pollable)
{
  Pollable_close(pollable);

  free((void*)pollable->name);

  Buffer_destroy(&pollable->input);
  Buffer_destroy(&pollable->output);

  memset(pollable, 0, sizeof(Pollable));

  // FIXME: HACK: Make sure nobody is using destroyed Pollables.
  pollable->fd = -2;
}


// Collapse NULL entries while preserving original order.
//
static void
collapse_pollables(void)
{
  uint n = 0;
  for (uint i = 0; i < g_pollables.size; i++)
  {
    Pollable* pollable = g_pollables.data[i];
    if (pollable != NULL)
    {
      pollable->private_index = n;
      g_pollables.data[n++] = pollable;
    }
  }
  g_pollables.size = n;
  g_collapse_pollables = false;
}



//! The process in which "dispatch_events_expect_signals()" was called.
//! This is used mostly to prevent weirdness after "fork()".
static pid_t g_signal_owner;

//! The special pipe.
static int g_signal_pipes[2] = { -1 , -1 };

//! The handler.
static void (*g_signal_handler)(int);


void
dispatch_events_expect_signals(void (*handler)(int))
{
  // Save the handler.
  g_signal_handler = handler;

  // Do nothing else if already called in this process.
  if (g_signal_owner == getpid())
    return;

  // Create the pipes.
  pipe_or_die(g_signal_pipes);

  // Close on exec.
  set_close_on_exec_or_die(g_signal_pipes[0], true);
  set_close_on_exec_or_die(g_signal_pipes[1], true);

  // Do not block.
  set_blocking_or_die(g_signal_pipes[0], false);
  set_blocking_or_die(g_signal_pipes[1], false);

  g_signal_owner = getpid();
}


void
dispatch_events_ignore_signals(void)
{
  // Do nothing unless "dispatch_events_expect_signals()" was called.
  if (g_signal_owner == 0)
    return;

  // Forget the handler.
  g_signal_handler = NULL;

  // Close the pipes.
  close_or_die(g_signal_pipes[0]);
  close_or_die(g_signal_pipes[1]);

  // Be polite.
  g_signal_pipes[0] = -1;
  g_signal_pipes[1] = -1;

  g_signal_owner = 0;
}


void
dispatch_events_handle_signal(int sig)
{
  // Do nothing in child after "fork()".
  if (g_signal_owner != getpid())
    return;

  uint8_t byte = (uint8_t)sig;

  while (true)
  {
    // Write one byte to the pipe.
    if (write(g_signal_pipes[1], &byte, 1) == 1)
      break;

    // HACK: Silently drop signals if the pipe fills up.
    if (errno == EAGAIN)
      break;

    // ISSUE: Technically this is not legal inside a signal handler,
    // but we are crashing anyway, so hopefully it will be okay.
    if (errno != EINTR)
      punt_with_errno("Failure in 'dispatch_events_handle_signal()'");
  }
}


// The current depth inside "dispatch_events()".
static int g_dispatch_depth;


// Pairs of function and argument which have been "deferred" until the
// outermost "dispatch_events()" is complete.
static Array g_dispatch_deferred;


void
dispatch_events_defer_call(void (*call)(void*), void* arg)
{
  if (g_dispatch_depth != 0)
  {
    Array_append(&g_dispatch_deferred, (void*)call);
    Array_append(&g_dispatch_deferred, arg);
  }
  else
  {
    (*call)(arg);
  }
}


// The various handlers called by "dispatch_events()" are allowed to call
// "dispatch_events()", destroy Pollables, cancel Alarms, create Pollables,
// schedule Alarms, etc.

// NOTE: Signals noticed during a call to "dispatch_events()" may not be
// handled until the next call to "dispatch_events()", or some future call.
//
// NOTE: Alarms scheduled during a call to "dispatch_events()" will not be
// fired until the next call to "dispatch_events()".
//
// ISSUE: On x86, the timeout for "select()" is apparently not very accurate
// (100000us yielded 99973us, 299999us yielded 298987us, 299391us yielded
// 299969us, 22us yielded 1001us, 9us yielded 995us, etc).  Note that most
// timeouts expire a few microseconds early, and very small (but non-zero)
// timeouts seem to round up to about 980us.
//
// HACK: So, if we are waiting for an Alarm, and "select()" returns too
// early, we wait again, unless the Alarm is "almost" ready to fire, in
// which case we just let it fire now.
//
// WARNING: Reentrant calls to "dispatch_events()" present several issues.
//
// The event handler loop must latch the readable/writable flags for each
// Pollable, and allow them to be handled by reentrant calls, to help avoid
// firing event handlers which are no longer accurate, which could confuse
// user code, especially when "blocking" read/writes are involved.
//
// Only the outer "dispatch_events()" call may call "collapse_pollables()",
// or else the event handler loop below will get confused, or even crash.
// Likewise with various other deferred calls, such as "free()".
//
// Alarms must be fired very carefully, in case a reentrant call modifies
// the list of Alarms.  We assume that the "cleanup" hook will be legal
// after the "handler" hook is called, even if the "handler" hook happens
// to reschedule the Alarm.
//
static bool
dispatch_events_aux(int msecs)
{
  struct timeval now;

  struct timeval timeout_msecs;
  struct timeval timeout_alarm;

  struct timeval* timeout = NULL;

  Alarm* fuzzy = NULL;

  // Handle "msecs".
  if (msecs >= 0)
  {
    timeout_msecs.tv_sec = msecs / 1000;
    timeout_msecs.tv_usec = (msecs - (timeout_msecs.tv_sec * 1000)) * 1000;
    timeout = &timeout_msecs;
  }

  // Check alarms.
  if (g_firing_alarms != NULL)
  {
    timeout_alarm.tv_sec = 0;
    timeout_alarm.tv_usec = 0;
    timeout = &timeout_alarm;
  }
  else if (g_waiting_alarms != NULL)
  {
    Alarm* alarm = g_waiting_alarms;

    timeval_now(&now);
    timeval_diff(&timeout_alarm, &alarm->when, &now);
    if (timeout == NULL || timeval_compare(&timeout_alarm, timeout) < 0)
    {
      timeout = &timeout_alarm;

      // HACK: See below.
      fuzzy = alarm;
    }
  }

  if (timeout != NULL)
    spew(9, "Waiting for up to %ld.%06ld seconds...",
         timeout->tv_sec, timeout->tv_usec);
  else
    spew(9, "Waiting forever...");

  int signal_pipe = (g_signal_owner == getpid()) ? g_signal_pipes[0] : -1;

  int maxfd;
  fd_set readfds;
  fd_set writefds;

  int selectable;

  while (true)
  {
    maxfd = 0;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    // Check "signal_pipe".
    if (signal_pipe != -1)
    {
      maxfd = signal_pipe;
      FD_SET(signal_pipe, &readfds);
    }

    // Check pollables.
    for (uint i = 0; i < g_pollables.size; i++)
    {
      Pollable* pollable = (Pollable*)Array_get(&g_pollables, i);

      // Handle unregistration.
      if (pollable == NULL)
        continue;

      int fd = pollable->fd;
      if (fd >= 0)
      {
        if (maxfd < fd)
          maxfd = fd;

        if (pollable->handle_readable != NULL)
          FD_SET(fd, &readfds);

        if (pollable->handle_writable != NULL)
          FD_SET(fd, &writefds);
      }
    }

    // Wait for something to happen.
    selectable = select(maxfd + 1, &readfds, &writefds, NULL, timeout);

    // Get the current time, if needed.
    if (g_waiting_alarms != NULL)
      timeval_now(&now);

    // Stop waiting if something happened.
    if (selectable != 0)
      break;

    // Stop waiting unless "fuzzy" is still pending.
    if (fuzzy == NULL || timeval_compare(&fuzzy->when, &now) <= 0)
      break;

    // Update "timeout".
    timeval_diff(timeout, &fuzzy->when, &now);

    // HACK: If an Alarm is "about" to fire, let it fire now.
    if (timeout->tv_sec == 0 && timeout->tv_usec < 50)
    {
      spew(3, "Jumping by %ld.%06lds.", timeout->tv_sec, timeout->tv_usec);
      now = fuzzy->when;
      break;
    }

    spew(3, "Pausing for %ld.%06lds.", timeout->tv_sec, timeout->tv_usec);
  }

  spew(9, "Got %d from 'select()'.", selectable);

  // Handle failures.
  if (selectable < 0)
  {
    if (errno == EINTR)
      return true;

    punt_with_errno("Failure in 'select()'");
  }

  // Latch the expired alarms.
  if (g_waiting_alarms != NULL)
  {
    Alarm** firep = &g_firing_alarms;

    Alarm** nextp = &g_waiting_alarms;
    for (Alarm* alarm = *nextp; alarm != NULL; alarm = *nextp)
    {
      if (timeval_compare(&alarm->when, &now) > 0)
        break;

      // Excise.
      *nextp = alarm->next;
      alarm->next = NULL;
      alarm->list = NULL;

      // Insert, advancing the minimal possible insertion point.
      Alarm_insert(alarm, &g_firing_alarms, firep);
      firep = &alarm->next;
    }
  }

  bool dispatched = false;

  if (selectable > 0)
  {
    dispatched = true;

    // Latch the initial size, in case new Pollables get registered.
    const uint initial_size = g_pollables.size;

    // Latch readable/writable.
    for (uint i = 0; i < initial_size; i++)
    {
      Pollable* pollable = (Pollable*)Array_get(&g_pollables, i);

      // Handle unregistration.
      if (pollable == NULL)
        continue;

      pollable->readable = (FD_ISSET(pollable->fd, &readfds) != 0);
      pollable->writable = (FD_ISSET(pollable->fd, &writefds) != 0);
    }

    // Process signal_pipe.
    if (signal_pipe != -1 && FD_ISSET(signal_pipe, &readfds))
    {
      errno = 0;

      uint8_t byte = SIGCHLD;
      int n = read(signal_pipe, &byte, 1);
      int sig = byte;

      int level = (sig == SIGCHLD) ? 4 : 3;
      spew(level, "Checking signal pipe yielded %d,%d [%d].", n, errno, sig);

      if (n > 0)
      {
        int level = (sig == SIGCHLD) ? 4 : 2;
        spew(level, "Handling deferred signal %s.", signal_name(sig));

        if (*g_signal_handler != NULL)
          (*g_signal_handler)(sig);
      }
      else
      {
        if (n == 0)
          punt("Unexpected close of signal pipe.");

        if (errno == EINTR)
          return true;

        punt_with_errno("Failure to read from signal pipe");
      }
    }

    // Whenever a Pollable gets unregistered, its entry will get set to
    // NULL, and so we must be careful to check for this aggressively.
    // Passing this check implies "Pollable_valid(pollable)".
    for (uint i = 0; i < initial_size; i++)
    {
      Pollable* pollable = (Pollable*)Array_get(&g_pollables, i);

      // Handle unregistration (by some previous Pollable).
      if (pollable == NULL)
        continue;

      if (pollable->writable)
      {
        pollable->writable = false;
        if (pollable->handle_writable != NULL)
        {
          spew(5, "%s handle_writable.", pollable->name);
          pollable->handle_writable(pollable);

          // Handle unregistration (by "handle_writable").
          if ((Pollable*)Array_get(&g_pollables, i) == NULL)
            continue;
        }
      }

      if (pollable->readable)
      {
        pollable->readable = false;
        if (pollable->handle_readable != NULL)
        {
          // HACK: Avoid spew related to "stdin".
          if (pollable->fd != STDIN_FILENO)
            spew(5, "%s handle_readable.", pollable->name);
          pollable->handle_readable(pollable);
        }
      }
    }
  }

  // Fire expired alarms. 
  while (g_firing_alarms != NULL)
  {
    Alarm* alarm = g_firing_alarms;

    // Excise.
    g_firing_alarms = alarm->next;
    alarm->next = NULL;
    alarm->list = NULL;

    if (alarm->handler != NULL)
      (*alarm->handler)(alarm);

    if (alarm->cleanup != NULL)
      (*alarm->cleanup)(alarm);

    dispatched = true;
  }

  return dispatched;
}


bool
dispatch_events(int msecs)
{
  spew(9, "Enter 'dispatch_events(%d)' [%d].",
       msecs, g_dispatch_depth);

  g_dispatch_depth++;

  bool dispatched = dispatch_events_aux(msecs);

  if (--g_dispatch_depth == 0)
  {
    for (int i = 0; i < g_dispatch_deferred.size; i += 2)
    {
      void* call = Array_get(&g_dispatch_deferred, i);
      void* arg = Array_get(&g_dispatch_deferred, i + 1);
      (*(void (*)(void*))call)(arg);
    }
    Array_clear(&g_dispatch_deferred);

    if (g_collapse_pollables)
      collapse_pollables();
  }

  spew(9, "Leave 'dispatch_events(%d)' [%d] = %d.",
       msecs, g_dispatch_depth, dispatched);

  return dispatched;
}

