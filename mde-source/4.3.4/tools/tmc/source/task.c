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

#include "handy.c"

#include <limits.h>
#include <sys/syscall.h>
#include <tmc/task.h>

#ifdef __NEWLIB__

// HACK: See "sys/un.h".
struct sockaddr_un {
  unsigned short sun_family;
  char sun_path[108];
};

// HACK: Normally in "<netinit/tcp.h>".
#define TCP_NODELAY 1

// HACK: Normally in "<sys/socket.h>".
#define SOCK_STREAM 1
#define AF_UNIX 1
#define PF_UNIX AF_UNIX
extern int socket (int __domain, int __type, int __protocol);
extern int connect (int __fd, void* __addr, unsigned int __len);
extern int bind (int __fd, void* __addr, unsigned int __len);
extern int listen (int __fd, int __n);

#else

#include <sys/socket.h>

#include <sys/un.h>
#include <netinet/tcp.h>

#include <sys/stat.h>

#endif


// NOTE: Several functions below were adapted from "tools/handy/*".


// HACK: For "QUERY_CODE_TASK_xxx".
// HACK: For "SHEPHERD_PROTOCOL_TASK".
#include "task_rpc.h"


// HACK: Copied from "tools/handy/rpc.h".
// Total Packet Size (uint) + Code (uint16_t) + Tag (uint16_t).
#define RPC_HEADER_SIZE 8

// HACK: Copied from "tools/handy/rpc.h".
// Turn a query code into a reply code.
#define RPC_REPLY(code) ((code) ^ 0xC000)


// Mutex for anything that makes process-wide changes.
// As a rule, all non-trivial public functions should lock and
// unlock this mutex, and all non-trivial static functions
// require that the mutex is locked on entry.
PTHREAD_MUTEX_DEFINE(s_task_mutex);


void
tmc_task_die(const char* format, ...)
{
  // ISSUE: Support various pre/post hooks.

  va_list args;
  va_start(args, format);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  fflush(stderr);
  va_end(args);

  // Note that if we are being watched by a shepherd, this exit code
  // will cause the shepherd to terminate our application.
  exit(128);
}



pid_t
tmc_task_gettid(void)
{
  return syscall(SYS_gettid);
}



const char*
__tmc_task_getexe(void)
{
  static const char* exe;

  // NOTE: We avoid locks here, at the theoretical cost of leaking up
  // to one copy of the executable path per thread.

  if (exe == NULL)
  {
    // Read the "/proc/self/exe" link.
    char path[PATH_MAX + 1];
    int n = readlink_aux("/proc/self/exe", path, sizeof(path));
    if (n < 0)
      return NULL;

    // HACK: If needed, remove the special suffix added by linux when
    // the actual executable has been deleted (or replaced).
    if (n > 10 && !strcmp(path + n - 10, " (deleted)"))
      path[n - 10] = '\0';

    const char* copy = strdup(path);

    // Make sure the path is visible everywhere.
    __sync_synchronize();

    exe = copy;
  }

  return exe;
}



int
__tmc_task_is_linux(void)
{
  static bool ready;
  static bool value;

  if (!ready)
  {
    value = (access("/proc/sys/kernel/version", R_OK) == 0);

    // Make sure "value" is visible before we admit to "ready",
    // since other callers won't be synchronized with us.
    __sync_synchronize();

    ready = true;
  }

  return value;
}



int
__tmc_task_parse_proc_stat(pid_t tid, unsigned int n)
{
  if (tid == 0)
    tid = tmc_task_gettid();

  char path[128];
  snprintf(path, sizeof(path), "/proc/%u/stat", tid);

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  // Note that 1KB should be enough space.  Normally the file contains less
  // than 200 bytes, plus the size of the executable filename, and filenames
  // are normally limited to 255 characters.
  char buf[1024];
  int rc = read(fd, buf, sizeof(buf));
  close(fd);

  // Error-check the results (we assume the close() succeeded and
  // didn't modify errno, though if it failed it was probably for
  // the same reason the read() failed).
  if (rc < 0)
    return -1;
  if (rc == 0)
  {
    errno = ENODATA;
    return -1;
  }
  if (rc == sizeof(buf))
  {
    // cpu is almost at the end, so it was probably chopped off.
    // Return a unique error (file too large).
    errno = EFBIG;
    return -1;
  }
  buf[rc] = '\0';

  // Parse the file.  The second field is the executable filename, in parens,
  // and may contain spaces and parens, but the last right paren in "buf" is
  // always the end of the second field.
  char* p = strrchr(buf, ')');
  if (p == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  if (n == 1)
  {
    errno = 0;
    return 0;
  }

  if (n == 0)
    p = buf;

  for (int i = 1; i < n; ++i)
  {
    p = strchr(p, ' ');
    if (p == NULL)
    {
      errno = EINVAL;
      return -1;
    }
    ++p;
  }

  errno = 0;
  char* end;
  int val = strtoul(p, &end, 10);
  if (p == end || *end != ' ')
  {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  return val;
}



// Minimal version of "Buffer".
//
typedef struct _buffer_t
{
  char* data;
  size_t limit;
  size_t size;
} buffer_t;


// Append "ch" to "buf", growing if needed, and return false, or, if
// allocation fails, do nothing, and return true,
//
static bool
canonicalize_append(buffer_t* buf, char ch)
{
  size_t limit = buf->limit;
  if (buf->size >= limit)
  {
    limit *= 2;
    char* grow = realloc(buf->data, limit);
    if (grow == NULL)
      return true;
    buf->limit = limit;
    buf->data = grow;
  }
  buf->data[buf->size++] = ch;
  return false;
}


// Using "buf", help canonicalize "path", relative to "dir".  Return
// true on getcwd() or canonicalize_append() failures, else false.
//
static bool
canonicalize_helper(buffer_t* buf, const char* path, const char* dir)
{
  // Handle relative paths.
  if (*path != '/')
  {
    char cwd[PATH_MAX];

    if (dir == NULL)
    {
      if (!getcwd(cwd, sizeof(cwd)))
        return true;

      dir = cwd;
    }

    // Recursively canonicalize "dir".
    if (canonicalize_helper(buf, dir, NULL))
      return true;
  }

  // Add a slash (if needed).
  // If "path" is absolute, its first slash will be skipped below.
  if (buf->size == 0 || buf->data[buf->size - 1] != '/')
    if (canonicalize_append(buf, '/'))
      return true;

  while (*path != '\0')
  {
    // Collapse slashes.
    if (*path == '/')
    {
      path++;
      continue;
    }

    if (*path == '.')
    {
      // Collapse "./" and final ".".
      // The final "/" (if any) will be handled above.
      if (path[1] == '/' || path[1] == '\0')
      {
        path++;
        continue;
      }

      if (path[1] == '.')
      {
        // Collapse "xxx/../" and final "xxx/..".
        // Do not collapse past the initial slash.
        // The final "/" (if any) will be handled above.
        if (path[2] == '/' || path[2] == '\0')
        {
          path += 2;
          if (buf->size > 1)
          {
            buf->size--;
            while (buf->data[buf->size - 1] != '/')
              buf->size--;
          }
          continue;
        }
      }
    }

    // Handle normal path components.
    while (*path != '\0' && *path != '/')
      if (canonicalize_append(buf, *path++))
        return true;

    // Add a slash.
    if (canonicalize_append(buf, '/'))
      return true;
  }

  // Remove any final slash (unless solitary).
  if (buf->size > 1 && buf->data[buf->size - 1] == '/')
    buf->size--;

  // Terminate (sneakily).
  if (canonicalize_append(buf, '\0'))
    return true;
  buf->size--;
  return false;
}


char*
__tmc_task_canonicalize(const char* path, const char* dir)
{
  char* result = NULL;

  buffer_t buf = { .data = malloc(PATH_MAX), .limit = PATH_MAX, .size = 0 };

  if (buf.data != NULL && !canonicalize_helper(&buf, path, dir))
    result = strdup(buf.data);

  int err = errno;
  free(buf.data);
  errno = err;

  return result;
}



static void
write_uint16(uint8_t* p, uint16_t v)
{
  *p++ = (v >> 0);
  *p++ = (v >> 8);
}


static uint16_t
read_uint16(const uint8_t* p)
{
  uint b0 = *p++;
  uint b1 = *p++;
  return (b1 << 8) | (b0);
}


static void
write_uint(uint8_t* p, uint v)
{
  *p++ = (v >> 0);
  *p++ = (v >> 8);
  *p++ = (v >> 16);
  *p++ = (v >> 24);
}


static uint
read_uint(const uint8_t* p)
{
  uint b0 = *p++;
  uint b1 = *p++;
  uint b2 = *p++;
  uint b3 = *p++;
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | (b0);
}


// ISSUE: Perhaps "or die" is not the best semantics for these calls?


static void
write_all_bytes_or_die(int fd, const void* buf, size_t count)
{
  size_t total = 0;

  while (total < count)
  {
    ssize_t n = write(fd, buf + total, count - total);
    if (n > 0)
    {
      total += n;
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in 'write()'");
    }
  }
}


static void
read_all_bytes_or_die(int fd, void* buf, size_t count)
{
  size_t total = 0;

  while (total < count)
  {
    ssize_t n = read(fd, buf + total, count - total);
    if (n > 0)
    {
      total += n;
    }
    else if (n == 0)
    {
      punt("Failure in 'read()': Unexpected EOF.");
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in 'read()'");
    }
  }
}


static void
send_query_or_die(int fd, uint16_t code,
                  uint8_t* query_data, uint query_size)
{
  uint8_t header[RPC_HEADER_SIZE];

  uint16_t tag = -1;

  write_uint(header + 0, RPC_HEADER_SIZE + query_size);
  write_uint16(header + 4, code);
  write_uint16(header + 6, tag);

  write_all_bytes_or_die(fd, header, RPC_HEADER_SIZE);

  if (query_size != 0)
    write_all_bytes_or_die(fd, query_data, query_size);
}


static void
read_reply_or_die(int fd, uint16_t code,
                  uint8_t* reply_data, uint reply_size)
{
  uint8_t header[RPC_HEADER_SIZE];

  uint16_t tag = -1;

  read_all_bytes_or_die(fd, header, RPC_HEADER_SIZE);

  if (read_uint(header + 0) < RPC_HEADER_SIZE)
    punt("Failure reading RPC reply from shepherd (size %u < %u).",
         read_uint(header + 0), RPC_HEADER_SIZE);

  if (read_uint16(header + 4) != RPC_REPLY(code))
    punt("Failure reading RPC reply from shepherd (code 0x%x != 0x%x).",
         read_uint16(header + 4), RPC_REPLY(code));

  if (read_uint(header + 0) != RPC_HEADER_SIZE + reply_size ||
      read_uint16(header + 6) != tag)
  {
    punt("Failure reading RPC reply from shepherd.");
  }

  if (reply_size != 0)
    read_all_bytes_or_die(fd, reply_data, reply_size);
}


#ifndef __NEWLIB__


//! Send a file descriptor on a socket, or die.
static void
send_magic_fd_or_die(int fd, int other)
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

  struct cmsghdr* h = CMSG_FIRSTHDR(&msg);
  h->cmsg_level = SOL_SOCKET;
  h->cmsg_type = SCM_RIGHTS;
  h->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(h), &other, sizeof(other));

  // NOTE: This is normally pointless.
  msg.msg_controllen = h->cmsg_len;

  if (sendmsg(fd, &msg, 0) < 0)
    punt_with_errno("Failure in 'sendmsg()'");
}


#endif // !__NEWLIB__


static void
__tmc_task_get_fd_hack(char* prefix, size_t size)
{
  fprintf(stderr,
          "WARNING: Detected possible version skew with shepherd.\n"
          "WARNING: This may indicate use of an obsolete bootrom.\n");
}


static void
__tmc_task_get_fd_check(const char* what)
{
  if (getenv(what) != NULL)
  {
    fprintf(stderr, "Detected environment variable '%s'\n", what);
    __tmc_task_get_fd_hack(NULL, 0);
  }
}



// HACK: This would be local to "__tmc_task_get_fd()" except
// that we have to support "__tmc_task_fork_local_shepherd()".
static const char* s_name;


// NOTE: must be called with s_task_mutex locked.
//
static int
__tmc_task_get_fd(bool monitor)
{
  static bool name_ready;

  if (!name_ready)
  {
    const char* name = getenv("TILERA_SHEPHERD_LISTENER");
    name_ready = true;

    if (name == NULL)
    {
      __tmc_task_get_fd_check("TILERA_SHEPHERD_SOCKET_PID");
      __tmc_task_get_fd_check("TILERA_WATCHER_PORT");
      __tmc_task_get_fd_check("TILERA_WATCHER_NAME");
    }
    else if (strlen(name) >= 100)
    {
      fprintf(stderr, "Detected illegal shepherd listener '%s'\n", name);
      name = NULL;
    }

    s_name = name;
  }

  const char* name = s_name;

  if (name == NULL || (monitor && strstr(name, "MONITOR") == NULL))
  {
    errno = ENODATA;
    return -1;
  }

  static int w_pid;
  static int w_fd;

  // Initialize and/or reset after fork.
  int pid = getpid();
  if (w_pid != pid)
  {
    w_pid = pid;
    w_fd = -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path + 1, name);
    //--size_t addr_size = sizeof(addr);
    size_t addr_size = sizeof(addr.sun_family) + 1 + strlen(name);

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
      return -1;

    // ISSUE: Perhaps failures below should induce warnings?

    while (true)
    {
      if (connect(fd, (struct sockaddr*) &addr, addr_size) == 0)
        break;
      if (errno == EINTR)
        continue;
      close_or_die(fd);
      return -1;
    }

    // ISSUE: Should also use "close on fork".
    set_close_on_exec_or_die(fd, true);

    // Greet the shepherd, or die.
    // ISSUE: Is "dying" really appropriate?
    // HACK: Use "__tmc_task_get_fd_hack()" to improve death messages.

    uint8_t data[4];

    message_prefix_hook = __tmc_task_get_fd_hack;

    write_uint(data, pid);
    send_query_or_die(fd, QUERY_CODE_TASK_INIT, data, 4);
    read_reply_or_die(fd, QUERY_CODE_TASK_INIT, NULL, 0);

    write_uint(data, SHEPHERD_PROTOCOL_TASK);
    send_query_or_die(fd, QUERY_CODE_TASK_GREET, data, 4);
    read_reply_or_die(fd, QUERY_CODE_TASK_GREET, NULL, 0);

    message_prefix_hook = NULL;

    w_fd = fd;
  }

  return w_fd;
}



// ISSUE: Should this actually contact the shepherd, or just determine
// if one is theoretically available?
//
// FIXME: This causes the process to become "watched".
//
int
tmc_task_has_monitor(void)
{
  PTHREAD_MUTEX_LOCK(&s_task_mutex);
  int w_fd = __tmc_task_get_fd(true);
  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);
  return (w_fd >= 0);
}


// ISSUE: Should this actually contact the shepherd, or just determine
// if one is theoretically available?  
//
// FIXME: This causes the process to become "watched".
//
int
tmc_task_has_shepherd(void)
{
  PTHREAD_MUTEX_LOCK(&s_task_mutex);
  int w_fd = __tmc_task_get_fd(false);
  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);
  return (w_fd >= 0);
}


// ISSUE: Should we add a function to check if we are being watched,
// and/or a function to specify whether or not we should be watched?


int
tmc_task_watch_forked_children(int flag)
{
  int retval = -1;

  PTHREAD_MUTEX_LOCK(&s_task_mutex);

  int w_fd = __tmc_task_get_fd(false);
  if (w_fd >= 0 && flag >= 0)
  {
    uint8_t data[8];
    write_uint(data, tmc_task_gettid());
    write_uint(data + 4, flag);
    send_query_or_die(w_fd, QUERY_CODE_TASK_WATCH_FORKED_CHILDREN, data, 8);
    read_reply_or_die(w_fd, QUERY_CODE_TASK_WATCH_FORKED_CHILDREN, data, 4);
    retval = read_uint(data);
  }

  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);

  return retval;
}


int
tmc_task_assume_impending_exec(int flag)
{
  int retval = -1;

  PTHREAD_MUTEX_LOCK(&s_task_mutex);

  int w_fd = __tmc_task_get_fd(false);
  if (w_fd >= 0 && flag >= 0)
  {
    uint8_t data[8];
    write_uint(data, tmc_task_gettid());
    write_uint(data + 4, flag);
    send_query_or_die(w_fd, QUERY_CODE_TASK_ASSUME_IMPENDING_EXEC, data, 8);
    read_reply_or_die(w_fd, QUERY_CODE_TASK_ASSUME_IMPENDING_EXEC, data, 4);
    retval = read_uint(data);
  }

  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);

  return retval;
}


int
tmc_task_terminate_app(void)
{
  int retval = -1;

  PTHREAD_MUTEX_LOCK(&s_task_mutex);

  int w_fd = __tmc_task_get_fd(false);
  if (w_fd >= 0)
  {
    send_query_or_die(w_fd, QUERY_CODE_TASK_TERMINATE_APP, NULL, 0);
    read_reply_or_die(w_fd, QUERY_CODE_TASK_TERMINATE_APP, NULL, 0);
    retval = 0;
  }

  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);

  return retval;
}


#ifndef __NEWLIB__


//! Supply console streams for the current process to the shepherd.
//!
//! Does nothing unless tmc_task_has_monitor() is true.
//!
//! Note that the file descriptors must be the other end of the pipes
//! or ptys which will be dup'd onto the standard console streams.
//!
//! WARNING: This function is only intended for use by advanced users!
//!
//! @param fd0 The stdin stream.
//! @param fd1 The stdout stream.
//! @param fd2 The stderr stream.
//!
static void
tmc_task_console_streams(int fd0, int fd1, int fd2)
{
  // NOTE: Requires a monitor.
  int w_fd = __tmc_task_get_fd(true);
  if (w_fd < 0)
    return;

  send_query_or_die(w_fd, QUERY_CODE_TASK_CONSOLE_STREAMS, NULL, 0);
  send_magic_fd_or_die(w_fd, fd0);
  send_magic_fd_or_die(w_fd, fd1);
  send_magic_fd_or_die(w_fd, fd2);
  read_reply_or_die(w_fd, QUERY_CODE_TASK_CONSOLE_STREAMS, NULL, 0);
}


static int
tmc_task_monitor_console_aux(void)
{
  // NOTE: Requires a "monitor".
  int w_fd = __tmc_task_get_fd(true);
  if (w_fd < 0)
    return -1;

  // Only allow use once per process.
  static int pid;
  if (pid != 0 && pid != getpid())
  {
    errno = EINVAL;
    return 1;
  }
  pid = getpid();

  int stdin_pp[2];
  int stdout_pp[2];
  int stderr_pp[2];

  // Make pipe pairs.
  if (pipe(stdin_pp) != 0)
    goto fail_stderr;
  if (pipe(stdout_pp) != 0)
    goto fail_stdout;
  if (pipe(stderr_pp) != 0)
  {
    close(stdout_pp[0]);
    close(stdout_pp[1]);
  fail_stdout:
    close(stdin_pp[0]);
    close(stdin_pp[1]);
  fail_stderr:
    return -1;
  }

  // Transmit one end of the pipe pairs.
  tmc_task_console_streams(stdin_pp[1], stdout_pp[0], stderr_pp[0]);

  // Use other end of the pipe pairs.
  close_or_die(stdin_pp[1]);
  dup2_and_close_or_die(stdin_pp[0], STDIN_FILENO);
  close_or_die(stdout_pp[0]);
  dup2_and_close_or_die(stdout_pp[1], STDOUT_FILENO);
  close_or_die(stderr_pp[0]);
  dup2_and_close_or_die(stderr_pp[1], STDERR_FILENO);

  // Force stdout to be line buffered.
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  // HACK: Force stdout to be line buffered if inherited.
  struct stat s;
  if (fstat(STDOUT_FILENO, &s) != 0)
    punt_with_errno("Failed to stat stdout");
  char buf[128];
  snprintf(buf, sizeof(buf), "%lu:%lu",
           (unsigned long) s.st_dev, (unsigned long) s.st_ino);
  setenv("STDIO_LBF_DEV_INO", buf, 1);

  return 0;
}


int
tmc_task_monitor_console(void)
{
  PTHREAD_MUTEX_LOCK(&s_task_mutex);
  int retval = tmc_task_monitor_console_aux();
  PTHREAD_MUTEX_UNLOCK(&s_task_mutex);
  return retval;
}


#if 0

#include <pty.h>

// ISSUE: Share "only once" and "line buffered stdout" code with
// "tmc_task_monitor_console()".
//
// NOTE: if this task is promoted to be a public function, it will
// need to be locked with s_task_mutex like other public functions.
//
static int
tmc_task_console_pty(void)
{
  int w_fd = __tmc_task_get_fd(true);
  if (w_fd < 0)
    return -1;

  // Make a pty for stdin/stdout.  By default, on x86, we get the flags
  // 0x500, 0x5, 0xbf, 0x8a3b, aka 02400, 05, 0277, 0105073, which means
  // IXON, ICRNL, ONLCR, OPOST, CREAD, CS8, B38400, IEXTEN, ECHOKE, ECHOCTL,
  // ECHOK, ECHOE, ECHO, ICANON, ISIG.  The "ONLCR" converts "\n" to "\r\n".
  // By default, on x86, we get ctrl chars of 0x3, 0x1c, 0x7f, 0x15, 0x4, 0,
  // 0x1, 0, 0x11, 0x13, 0x1a, 0, 0x12, 0xf, 0x17, 0x16, 0, 0, 0, ....
  // Providing all zeros actually turns the flags into 0x0, 0x0, 0xb0, 0x0,
  // aka 0, 0, 0260, 0, meaning CREAD (enable receiver), CS8 (8 bit chars).
  // WARNING: You must specify "ICANON", or set "VMIN", or the child will
  // see "EOF" as soon as there is no input available when reading "stdin".

  int pty_master, pty_slave;

  struct termios term;
  memset(&term, 0, sizeof(term));

  term.c_cc[VMIN] = 1;

  // Make a pty for stdin/stdout.
  if (openpty(&pty_master, &pty_slave, NULL, &term, NULL) != 0)
    punt_with_errno("Failure in 'openpty()'");

  // ISSUE: Just let the caller do this?
#if 0
  // Make process a session group leader.  Note that "login_tty()"
  // would do this, and also dup onto stdin, stdout, AND stderr.
  (void)setsid();
  if (ioctl(pty_slave, TIOCSCTTY, (char *)NULL) == -1)
    punt_with_errno("Failure in 'ioctl(TIOCSCTTY)'");
#endif

  // Make pipe pair for stderr.
  int stderr_pipe_pair[2];
  if (pipe(stderr_pipe_pair) != 0)
    punt("Failure in 'pipe()': %s", strerror(errno));

  tmc_task_console_streams(pty_master, pty_master, stderr_pipe_pair[0]);

  close_or_die(pty_master);
  dup2_or_die(pty_slave, STDIN_FILENO);
  dup2_and_close_or_die(pty_slave, STDOUT_FILENO);

  close_or_die(stderr_pipe_pair[0]);
  dup2_and_close_or_die(stderr_pipe_pair[1], STDERR_FILENO);

  return 0;
}

#endif


#endif // !__NEWLIB__

