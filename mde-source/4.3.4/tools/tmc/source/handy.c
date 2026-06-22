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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>



#ifdef __NEWLIB__

//! HACK: Where is "unsetenv()"?
#define unsetenv putenv

#endif


//! Rounds up "N" to the next multiple of "A" (a power of two).
#define ROUND_UP(N, A) (((N) + (A) - 1) & -(A))


//! Determine if "N" is aligned to "A" (a power of two).
#define ALIGNED(N,A) (((size_t)(N) & ((A) - 1)) == 0)


//! Allow unused variables to avoid compiler warnings.
#define UNUSED __attribute__((unused))


//! HACK: Allow "note()" to be enabled remotely.
int tmc_verbosity;


static void UNUSED
note(const char* format, ...)
     __attribute__((format(printf, 1, 2)));

static void
note(const char* format, ...)
{
#if 0
  if (tmc_verbosity == 0)
    return;

  char buf[1024];

  int n = snprintf(buf, sizeof(buf), "[%d] ", getpid());

  va_list args;
  va_start(args, format);
  n += vsnprintf(buf + n, sizeof(buf) - n - 1, format, args);
  va_end(args);

  if (n > sizeof(buf) - 2)
    n = sizeof(buf) - 2;

  buf[n++] = '\n';
  buf[n] = '\0';

  fputs(buf, stderr);
  fflush(stderr);
#endif
}


static void UNUSED
warn(const char* format, ...)
     __attribute__((format(printf, 1, 2)));

static void
warn(const char* format, ...)
{
  char buf[1024];

  int n = snprintf(buf, sizeof(buf), "WARNING: ");

  va_list args;
  va_start(args, format);
  n += vsnprintf(buf + n, sizeof(buf) - n - 1, format, args);
  va_end(args);

  if (n > sizeof(buf) - 2)
    n = sizeof(buf) - 2;

  buf[n++] = '\n';
  buf[n] = '\0';

  fputs(buf, stderr);
  fflush(stderr);
}


// FIXME: Use "tmc_task_die()".

//! HACK: See "__tmc_task_get_fd()".
static void (*message_prefix_hook)(char* prefix, size_t size);


static void UNUSED
punt(const char* format, ...)
     __attribute__((format(printf, 1, 2), noreturn));

static void
punt(const char* format, ...)
{
  if (message_prefix_hook != NULL)
    (*message_prefix_hook)(NULL, 0);

  fprintf(stderr, "ERROR: ");

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fprintf(stderr, "\n");
  fflush(stderr);

  abort();
}


static void UNUSED
punt_with_errno(const char* format, ...)
     __attribute__((format(printf, 1, 2), noreturn));

static void
punt_with_errno(const char* format, ...)
{
  if (message_prefix_hook != NULL)
    (*message_prefix_hook)(NULL, 0);

  fprintf(stderr, "ERROR: ");

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fprintf(stderr, ": (%d) %s.\n", errno, strerror(errno));
  fflush(stderr);

  abort();
}


static void UNUSED
close_or_die(int fd)
{
  if (close(fd) != 0)
    punt("Failure in 'close(%d)': %s", fd, strerror(errno));
}


#ifndef __NEWLIB__

static int UNUSED
dup2_or_die(int oldfd, int newfd)
{
  int ret = dup2(oldfd, newfd);
  if (ret < 0)
    punt("Failure in 'dup2(%d, %d)': %s", oldfd, newfd, strerror(errno));
  return ret;
}


static int UNUSED
dup2_and_close_or_die(int oldfd, int newfd)
{
  int ret = newfd;
  if (oldfd != newfd)
  {
    ret = dup2_or_die(oldfd, newfd);
    close_or_die(oldfd);
  }
  return ret;
}

#endif // !__NEWLIB__


//! Set the "close on exec" flag for "fd" to the value "flag".
//!
//! Return -1 on error, or the previous value of the flag.
//!
static int UNUSED
set_close_on_exec(int fd, int flag)
{
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags == -1)
    return -1;
  int old = (flags & FD_CLOEXEC) ? 1 : 0;
  flags = flag ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
  if (fcntl(fd, F_SETFD, flags) != 0)
    return -1;
  return old;
}


static void UNUSED
set_close_on_exec_or_die(int fd, int flag)
{
  if (set_close_on_exec(fd, flag != 0) < 0)
    punt("Failure in 'set_close_on_exec()': %s", strerror(errno));
}


// A wrapper around "readlink()" that hides some of its flaws.
//
static int UNUSED
readlink_aux(const char* path, char* buf, size_t size)
{
  int n = readlink(path, buf, size);
  if (n < 0)
  {
    return -1;
  }
  if ((size_t)n >= size)
  {
    errno = ENAMETOOLONG;
    return -1;
  }
  buf[n] = '\0';
  return n;
}


static bool UNUSED
has_prefix(const char* string, const char* other)
{
  while (*other)
    if (*other++ != *string++)
      return false;

  return true;
}


static bool UNUSED
has_suffix(const char* string, const char* other)
{
  size_t other_len = strlen(other);
  size_t string_len = strlen(string);

  if (other_len > string_len)
    return false;

  return !strcmp(string + string_len - other_len, other);
}


#ifdef __NEWLIB__

#define PTHREAD_MUTEX_DEFINE(mutex) \
  extern int mutex##_unimpl __attribute__((unused))
#define PTHREAD_MUTEX_LOCK(mutex)
#define PTHREAD_MUTEX_UNLOCK(mutex)
#define PTHREAD_MUTEX_INIT(mutex)

// Newlib doesn't use threads, and doesn't support this attribute.
#define __thread

#else

#include <pthread.h>

#ifndef SHARED
extern typeof(pthread_mutex_lock) pthread_mutex_lock __attribute__((weak));
extern typeof(pthread_mutex_unlock) pthread_mutex_unlock __attribute__((weak));
#endif

static void UNUSED
tmc_pthread_mutex_lock(pthread_mutex_t* mutex)
{
#ifndef SHARED
  if (pthread_mutex_lock == NULL)
    return;
#endif
  int rc = pthread_mutex_lock(mutex);
  if (rc != 0)
    punt("Failure in 'pthread_mutex_lock()': %s", strerror(rc));
}

static void UNUSED
tmc_pthread_mutex_unlock(pthread_mutex_t* mutex)
{
#ifndef SHARED
  if (pthread_mutex_unlock == NULL)
    return;
#endif
  int rc = pthread_mutex_unlock(mutex);
  if (rc != 0)
    punt("Failure in 'pthread_mutex_unlock()': %s", strerror(rc));
}

#define PTHREAD_MUTEX_DEFINE(mutex) \
  static pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#define PTHREAD_MUTEX_INIT(mutex) \
  (*(mutex) = (pthread_mutex_t) PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP)
#define PTHREAD_MUTEX_LOCK tmc_pthread_mutex_lock
#define PTHREAD_MUTEX_UNLOCK tmc_pthread_mutex_unlock

#endif
