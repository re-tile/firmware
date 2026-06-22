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

#ifndef TOOLS_HANDY_POLLABLE_H
#define TOOLS_HANDY_POLLABLE_H

#include "common.h"

#include "Buffer.h"


//! HACK: If the "fd" for a Pollable is at least this value, then various
//! "Pollable_xxx()" functions will use non-standard implementations.
#define POLLABLE_WACKY_FD 0x10000000


//! If the "fd" for a Pollable is (at least) this value, then all of
//! the "Pollable_xxx()" functions will assume that it is a special
//! socket using the "SPR_SIM_SOCKET" mechanism.  Use with caution!!!
#define SIM_SOCKET_ID 0x40000000

//! If the "fd" for a Pollable is (at least) SIM_SOCKET_ID, and is
//! ORed with this value, then "Pollable_read()" will "freeze" the
//! simulator until data is actually available.
#define SIM_SOCKET_BLOCKING 0x20000000

//! Writing this value to SPR_SIM_SOCKET will flush the actual socket.
#define SIM_SOCKET_FLUSH (-1)

//! Writing this value to SPR_SIM_SOCKET will close the actual socket.
//--#define SIM_SOCKET_CLOSE (-2)

//! Writing this value to SPR_SIM_SOCKET will cause the simulator to
//! freeze until data is available to be read.
#define SIM_SOCKET_WAIT (-3)

//! Writing this value to SPR_SIM_SOCKET will cause the simulator to
//! create a checkpoint image (if needed).
#define SIM_SOCKET_MAYBE_CREATE_IMAGE (-4)


//! Value yielded by SPR_SIM_SOCKET if the socket has no data.
#define SIM_SOCKET_UNREADY (-1)

//! Value yielded by SPR_SIM_SOCKET if the socket is closed.
//--#define SIM_SOCKET_EOF (-2)


BEGIN_EXTERN_C


//! Get the "name" for the given signal.
extern const char*
signal_name(uint sig);



//! Store into "now" the current time, according to "gettimeofday()".
extern void
timeval_now(struct timeval* now);

//! Store into "diff" the value "x - y", or zero if this would be negative.
extern void
timeval_diff(struct timeval* diff,
             const struct timeval* x,
             const struct timeval* y);

//! Store into "elapsed" the amount of time elapsed since "since".
extern void
timeval_elapsed(struct timeval* elapsed, struct timeval* since);

//! Add the given number of usecs to the given timeval.
extern void
timeval_add_usecs(struct timeval* x, uint usecs);

//! Add the given number of msecs to the given timeval.
extern void
timeval_add_msecs(struct timeval* x, uint msecs);

//! Compare "x" and "y", ala "strcmp()".
extern int
timeval_compare(const struct timeval* x, const struct timeval* y);



typedef struct _Alarm Alarm;

typedef void (*Alarm_func)(Alarm*);

struct _Alarm
{
  //! When this Alarm should fire.
  //! WARNING: Do not change this while an Alarm is scheduled.
  struct timeval when;

  //! User specified data.
  void *info;

  //! What to do when this Alarm is fired.
  Alarm_func handler;

  //! What to do after this Alarm is fired or cancelled.  For example,
  //! if the Alarm was dynamically allocated, this could free the Alarm.
  Alarm_func cleanup;

  //! Pointer to an Alarm list pointer, if scheduled, else NULL.
  //! WARNING: For internal use only.
  Alarm** list;

  //! Pointer to the next Alarm in a list of scheduled Alarms, or NULL.
  //! WARNING: For internal use only.
  Alarm* next;
};



//! Determine if an Alarm is currently scheduled.
extern bool
Alarm_scheduled(Alarm* alarm);

//! Reschedule an Alarm, and return true, if it is already scheduled,
//! else schedule it, and return false.
extern bool
Alarm_schedule(Alarm* alarm);

//! Cancel an Alarm, and return true, if it is currently scheduled,
//! else do nothing, and return false.
extern bool
Alarm_cancel(Alarm* alarm);



// FIXME: Need more docs here.


typedef struct _Pollable Pollable;

// ISSUE: Should this just be named "Pollable_handler"?
typedef void (*Pollable_handler_func)(Pollable* pollable);

//! A file descriptor which can handle becoming readable/writable,
//! using a convenient info pointer and some input/output buffers.
struct _Pollable
{
  //! A textual name, for debugging. This is freed on destroy,
  //! and must be explicitly freed before assigning a new value.
  const char* name;

  //! User specified data.
  void* info;

  //! In @ref dispatch_events, this field, if non-NULL, is called
  //! whenever this Pollable is "readable".
  Pollable_handler_func handle_readable;

  //! In @ref dispatch_events, this field, if non-NULL, is called
  //! whenever this Pollable is "writable".
  Pollable_handler_func handle_writable;

  //! An input buffer. This is normally used to hold incoming data.
  Buffer input;

  //! An output buffer. This is normally used to hold outgoing data.
  //! After modification, consider calling "Pollable_flush_later()".
  Buffer output;

  //! A file descriptor. This must NOT be modified directly.
  int fd;

  //! The index in an internal array of registered Pollables.
  //! WARNING: For internal use only.
  int16_t private_index;

  //! WARNING: For internal use only.
  bool readable;

  //! WARNING: For internal use only.
  bool writable;
};



//! Returns true iff @ref pollable has a non-negative "fd".
extern bool
Pollable_valid(Pollable* pollable);

//! Associate @param pollable with the file descriptor @param fd, after
//! first disassociating it from any previously associated file descriptor.
extern void
Pollable_set_fd(Pollable* pollable, int fd);

//! Shorthand for "pollable->handle_readable = handle_readable".
// FIXME: Get rid of this function.
extern void
Pollable_set_handle_readable(Pollable* pollable,
                             Pollable_handler_func handle_readable);

//! Shorthand for "pollable->handle_writable = handle_writable".
// FIXME: Get rid of this function.
extern void
Pollable_set_handle_writable(Pollable* pollable,
                             Pollable_handler_func handle_writable);

//! Associate @param pollable with the file descriptor @param fd (using
//! "Pollable_set_fd(pollable, fd)"), and set "pollable->handle_readable"
//! @param handle_readable.
extern void
Pollable_open(Pollable* pollable, int fd,
              Pollable_handler_func handle_readable);

//! Close "pollable->fd", and disassociate @param pollable from its file
//! descriptor (using "Pollable_set_fd(pollable, -1)"), if needed.
extern void
Pollable_close(Pollable* pollable);

//! Using @ref read_some_bytes_or_die, read up to @param count bytes into
//! the "input" buffer of @param pollable, and return the number of bytes
//! read, or return -1 after closing the Pollable.
extern ssize_t
Pollable_read(Pollable* pollable, size_t count);

//! Using @ref read_uninterrupted_or_die, read up to @param count bytes into
//! the "input" buffer of @param pollable, and return the number of bytes
//! read, or return -1 after closing the Pollable.
extern ssize_t
Pollable_read_partial(Pollable* pollable, size_t count);

//! Using @ref Pollable_read, read up to "(limit - size) - room" bytes,
//! after using @ref Buffer_reserve to make room for at least "4096 - room"
//! bytes. Returns -1 on immediate EOF (treated as EPIPE) after calling
//! "Pollable_close()", 0 if nothing was available, 1 if a short read was
//! done, and 2 if a full read was done (more bytes MAY be available).
//! Dies on various errors.
extern int
Pollable_acquire(Pollable* pollable, size_t room);

//! Attempt to flush any pending writes for @param pollable, and sets
//! "handle_writable" to @ref Pollable_flush if there is more to flush
//! later, or to NULL if fully flushed.
extern void
Pollable_flush(Pollable* pollable);

//! Call @ref Pollable_flush on @param pollable during the next call to
//! @ref dispatch_events, by setting "handle_writable" to @ref Pollable_flush.
extern void
Pollable_flush_later(Pollable* pollable);

//! Call @ref Pollable_flush on @param pollable repeatedly until it is fully
//! flushed, sleeping between each call, and set "handle_writable" to NULL.
extern void
Pollable_flush_fully(Pollable* pollable);

//! Write @param count bytes, starting at @param data, to @param pollable,
//! and remember to flush it later.
extern void
Pollable_write(Pollable* pollable, const void* data, size_t count);

//! Initialize @ref pollable, setting "name" to "strfmt_or_die(format, ...)".
extern void
Pollable_init(Pollable* pollable, const char* format, ...);

//! Destroy @ref pollable, calling @ref Pollable_close, destroying "input"
//! and "output", freeing "name", and zeroing all fields, except "fd" and
//! "private_index", which are set to "-1".
extern void
Pollable_destroy(Pollable* pollable);


//! See "dispatch_events_handle_signal()".
extern void
dispatch_events_expect_signals(void (*handler)(int));

//! See "dispatch_events_handle_signal()".
extern void
dispatch_events_ignore_signals(void);

//! Once "dispatch_events_expect_signals()" has been called, with a handler
//! function, and until "dispatch_events_ignore_signals()" or "fork()" has
//! been called, "dispatch_events_handle_signal()" can be used as a signal
//! handler (or can be called by signal handlers).  It will write the signal
//! (as a byte) to a special pipe, which will cause the "select()" loop in
//! "dispatch_events()" to wake up, if necessary.  The byte will be read by
//! "dispatch_events()" from the special pipe, and the handler, if non-NULL,
//! will be called with that signal.  Normally, "dispatch_events()" would
//! have woken up anyway, due to EINTR, but this pipe hackery avoids certain
//! very nasty race conditions.
//!
//! WARNING: Technically, if "dispatch_events_handle_signal()" is called too
//! often before "dispatch_events()" drains the pipe, signals may be "lost".
//!
extern void
dispatch_events_handle_signal(int sig);


//! Defer an arbitrary call as long as "dispatch_events()" is running.
extern void
dispatch_events_defer_call(void (*call)(void*), void* arg);

//! Defer a "free()" call as long as "dispatch_events()" is running.
#define dispatch_events_defer_free(A) \
  dispatch_events_defer_call(free, (A))


//! Wait for up to @ref msecs milliseconds (or "forever" if "msecs < 0") for
//! a signal to be noticed, something to happen to a registered Pollable, or
//! a scheduled Alarm to fire.  Then, handle any noticed signals, handle any
//! readable/writable events for registered Pollables, and fire any expired
//! Alarms.  Then, return true if anything happened, else false.
//!
extern bool
dispatch_events(int msecs);


END_EXTERN_C

#endif /* !TOOLS_HANDY_POLLABLE_H */
