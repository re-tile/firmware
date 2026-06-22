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

#include "various.h"

#include "message.h"

#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>

#ifdef __NEWLIB__

// HACK: Normally in "<netinit/tcp.h>".
#define TCP_NODELAY 1

#else

#include <arpa/inet.h>
#include <netinet/tcp.h>

#endif



static void*
verify_alloc(void* ptr, size_t size)
{
  if (ptr == NULL)
  {
    punt_with_errno("Cannot allocate %zu bytes of memory", size);
  }
  return ptr;
}


void*
calloc_or_die(size_t num, size_t size)
{
  return verify_alloc(calloc(num, size), size);
}


void*
malloc_or_die(size_t size)
{
  return verify_alloc(malloc(size), size);
}


void*
realloc_or_die(void* ptr, size_t size)
{
  return verify_alloc(realloc(ptr, size), size);
}


bool
has_prefix(const char* string, const char* other)
{
  while (*other)
    if (*other++ != *string++)
      return false;

  return true;
}


bool
has_suffix(const char* string, const char* other)
{
  size_t other_len = strlen(other);
  size_t string_len = strlen(string);

  if (other_len > string_len)
    return false;

  return !strcmp(string + string_len - other_len, other);
}



int
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


int
readlink_aux_or_die(const char* path, char* buf, size_t size)
{
  int n = readlink_aux(path, buf, size);
  if (n < 0)
    punt_with_errno("Failure in 'readlink(\"%s\")'", path);
  return n;
}


void
get_install_path(char* buf, size_t size, const char* tail)
{
  if (tail[0] != '/' && tail[0] != '\0')
    punt("Cannot handle relative paths in 'get_install_path'.");

  int n = readlink_aux_or_die("/proc/self/exe", buf, size);

  for (int i = n; i >= 0; i--)
  {
    if (!strncmp(buf + i, "/bin/", 5))
    {
      buf[i] = '\0';
      if (i + strlen(tail) > size)
        punt("Insufficient space for '%s%s'.", buf, tail);
      strcpy(buf + i, tail);
      return;
    }
  }

  punt("Cannot get install dir from 'exe' path '%s'.", buf);
}


void
get_install_tile_path(char* buf, size_t size, const char* tail)
{
  if (tail[0] != '/' && tail[0] != '\0')
    punt("Cannot handle relative paths in 'get_install_tile_path'.");

  int n = readlink_aux_or_die("/proc/self/exe", buf, size);

  for (int i = n; i >= 0; i--)
  {
    if (!strncmp(buf + i, "/bin/", 5))
    {
      buf[i] = '\0';
      if (i + 5 + strlen(tail) > size)
        punt("Insufficient space for '%s/tile%s'.", buf, tail);
      strcpy(buf + i, "/tile");
      strcpy(buf + i + 5, tail);
      return;
    }
  }

  punt("Cannot get install dir from 'exe' path '%s'.", buf);
}


// ISSUE: See also "tools/tmc/source/task.c".
//
void
canonicalize_path(Buffer* buf, const char* path, const char* base)
{
  Buffer_clear(buf);

  // Handle tilde paths.
  // FIXME: Handle "~user...".
  if (*path == '~' && (path[1] == '/' || path[1] == '\0'))
  {
    const char* home = getenv("HOME");

    if (!home)
      punt("Failure in getenv(\"HOME\").");

    Buffer_print(buf, home);

    // The slash, if any, will be skipped below.
    path++;
  }

  // Handle relative paths.
  else if (*path != '/')
  {
    if (base != NULL)
    {
      // Recursively canonicalize "base".
      canonicalize_path(buf, base, NULL);
    }
    else
    {
      Buffer_reserve(buf, PATH_MAX);
      char* tmp = (char*)buf->data + buf->size;
      if (!getcwd(tmp, buf->limit - buf->size))
        punt_with_errno("Could not get cwd");
      buf->size += strlen(tmp);
    }
  }

  // Add a slash (if needed).
  // If "path" is absolute, its first slash will be skipped below.
  if (buf->size == 0 || buf->data[buf->size - 1] != '/')
    Buffer_append(buf, '/');

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
    while (*path != '/' && *path != '\0')
      Buffer_append(buf, *path++);

    // Add a slash.
    Buffer_append(buf, '/');
  }

  // Remove any final slash (unless solitary).
  if (buf->size > 1 && buf->data[buf->size - 1] == '/')
    buf->size--;

  // Terminate (sneakily).
  Buffer_append(buf, '\0');
  buf->size--;
}



int
atoi_or_die(const char* str)
{
  char* end;
  errno = 0;
  long val = strtol(str, &end, 10);
  if (*end != '\0' || end == str || errno == ERANGE ||
      (sizeof(long) > sizeof(int) && (val > INT_MAX || val < INT_MIN)))
  {
    punt("Cannot parse int from '%s'.", str);
  }
  return val;
}


uint16_t
atou16_or_die(const char* str)
{
  char* end;
  unsigned long val = strtoul(str, &end, 10);
  if (*end != '\0' || end == str || val > 65535)
  {
    punt("Cannot parse uint16_t from '%s'.", str);
  }
  return val;
}


double
atod_or_die(const char* str)
{
  char* end;
  errno = 0;
  double val = strtod(str, &end);
  if (*end != '\0' || end == str || errno == ERANGE)
  {
    punt("Cannot parse double from '%s'.", str);
  }
  return val;
}


uint64_t
atou64_with_modifier_or_die(const char* str)
{
  char* leftover = NULL;
  errno = 0;
  double retval = strtod(str, &leftover);
  if ((leftover[0] != '\0' && leftover[1] != '\0') ||
      leftover == str ||
      errno == ERANGE)
  {
    punt("Cannot parse a double plus an optional modifier (G, M, K) from %s",
         str);
  }

  switch (leftover[0])
  {
  case 'G':
  case 'g':
    retval *= 1000.0 * 1000.0 * 1000.0;
    break;
  case 'M':
  case 'm':
    retval *=          1000.0 * 1000.0;
    break;
  case 'K':
  case 'k':
    retval *=                   1000.0;
    break;
  case '\0':
    // No modifier provided.
    break;
  default:
    punt("Unknown modifier '%c'. Allowed modifiers are G, M, and K", 
         leftover[0]);
    break;
  }
  return (uint64_t) retval;
}


void
set_close_on_exec_or_die(int fd, bool flag)
{
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags != -1)
  {
    flags = flag ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
    if (fcntl(fd, F_SETFD, flags) == 0)
      return;
  }
  punt_with_errno("Failure in 'set_close_on_exec_or_die(%d, %d)'", fd, flag);
}


void
set_blocking_or_die(int fd, bool flag)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1)
  {
    flags = !flag ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(fd, F_SETFL, flags) == 0)
      return;
  }
  punt_with_errno("Failure in 'set_blocking_or_die(%d, %d)'", fd, flag);
}


void
set_delaying_or_die(int fd, bool flag)
{
  int optname = IPPROTO_TCP;

  struct protoent* proto = getprotobyname("tcp");
  if (proto != NULL)
  {
    optname = proto->p_proto;
  }

  int val = !flag;
  if (setsockopt(fd, optname, TCP_NODELAY, (void*)&val, sizeof(val)) != 0)
  {
    punt_with_errno("Failure in 'set_delaying_or_die(%d, %d)'", fd, flag);
  }
}


void
set_keep_alive_or_die(int fd, bool flag)
{
  int val = flag;
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&val, sizeof(val)) != 0)
  {
    punt_with_errno("Failure in 'set_keep_alive_or_die(%d, %d)'", fd, flag);
  }
}


void
pipe_or_die(int fds[2])
{
  if (pipe(fds) != 0)
    punt_with_errno("Failure in 'pipe()'");
}


void
socketpair_or_die(int fds[2])
{
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    punt_with_errno("Failure in 'socketpair()'");
}


int
mkstemp_boldly(char* path_template)
{
  while (true)
  {
    int fd = mkstemp(path_template);
    if (fd >= 0 || errno != EINTR)
      return fd;
  }
}


int
mkstemp_or_die(char* path_template)
{
  int fd = mkstemp_boldly(path_template);
  if (fd >= 0)
    return fd;

  punt_with_errno("Failure in 'mkstemp(\"%s\")'", path_template);
}



int
open_boldly(const char* path, int flags, mode_t mode)
{
  while (true)
  {
    int fd = open(path, flags, mode);
    if (fd >= 0 || errno != EINTR)
      return fd;
  }
}


int
open_or_die(const char* path, int flags, mode_t mode)
{
  int fd = open_boldly(path, flags, mode);
  if (fd >= 0)
    return fd;

  punt_with_errno("Failure in 'open(\"%s\")'", path);
}


int
close_boldly(int fd)
{
  while (true)
  {
    int result = close(fd);
    if (result == 0 || errno != EINTR)
      return result;
  }
}


void
close_or_die(int fd)
{
  if (close_boldly(fd) == 0)
    return;

  punt_with_errno("Failure in 'close(%d)'", fd);
}



int
dup_or_die(int oldfd)
{
  int ret = dup(oldfd);
  if (ret >= 0)
    return ret;

  punt_with_errno("Failure in 'dup(%d)'", oldfd);
}


int
dup2_or_die(int oldfd, int newfd)
{
  int ret = dup2(oldfd, newfd);
  if (ret >= 0)
    return ret;

  punt_with_errno("Failure in 'dup2(%d, %d)'", oldfd, newfd);
}


int
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


pid_t
fork_or_die(void)
{
  // HACK: Flush console.
  fflush(NULL);

  pid_t pid = fork();
  if (pid >= 0)
    return pid;

  punt_with_errno("Failure in 'fork()'");
}


pid_t
waitpid_boldly(pid_t pid, int* status, int options)
{
  while (true)
  {
    pid_t result = waitpid(pid, status, options);
    if (result >= 0 || errno != EINTR)
      return result;
  }
}


pid_t
waitpid_or_die(pid_t pid, int* status, int options)
{
  pid_t result = waitpid_boldly(pid, status, options);
  if (result >= 0)
    return result;

  punt_with_errno("Failure in 'waitpid()'");
}



int
create_directory(const char* path)
{
  // Check for an existing directory (or a symlink to an existing directory).
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
    return 0;

  // Create directory.
  if (mkdir(path, 0777) != 0)
    return -1;
  spew(2, "Created directory '%s'.", path);
  return 0;
}


int
create_ancestors(const char* path)
{
  int result = 0;

  char buf[strlen(path) + 1];
  strcpy(buf, path);

  char *s = buf;

  // Skip leading slashes.
  while (*s == '/')
    s++;

  while (result == 0)
  {
    // Find next slash, or stop.
    s = strchr(s, '/');
    if (s == NULL)
      break;

    // Temporarily terminate.
    *s = '\0';

    // Create directory.
    result = create_directory(buf);

    // Unterminate and advance.
    *s++ = '/';
  }

  return result;
}


size_t
write_all_bytes(int fd, const void* buf, size_t count)
{
  size_t total = 0;

  while (total < count)
  {
    errno = 0;
    ssize_t n = write(fd, buf + total, count - total);
    if (n > 0)
    {
      total += n;
    }
    else if (errno == EINTR)
    {
      // Try again.
    }
    else
    {
      break;
    }
  }

  return total;
}


void
write_all_bytes_or_die(int fd, const void* buf, size_t count)
{
  size_t total = write_all_bytes(fd, buf, count);
  if (total != count)
    punt_with_errno("Only wrote %zu/%zu bytes", total, count);
}


ssize_t
write_some_bytes_or_die(int fd, const void* buf, size_t count)
{
  size_t total = write_all_bytes(fd, buf, count);

  if (count != total)
  {
    if (errno == EAGAIN)
    {
      // Nothing available.
    }
    else if (errno == EPIPE ||
             errno == ECONNRESET ||
             // HACK: This happens when the other end of a PTY exits.
             // ISSUE: Actually, this has only been observed for "read()".
             (errno == EIO && isatty(fd)))
    {
      // Treat as EOF.
      if (total == 0)
        return -1;
    }
    else
    {
      punt_with_errno("Failure in 'write(%d, ..., %u)'", fd, (uint)count);
    }
  }

  return total;
}


ssize_t 
append_to_file_boldly(const char* path, const void* buf, size_t count)
{
  ssize_t retval = 0;
  int fd = open_boldly(path, O_WRONLY | O_APPEND, 0);
  if (fd >= 0) 
  {
    retval = write_all_bytes(fd, buf, count);
    if (close_boldly(fd) != 0)
    {
      retval = 0;
    }
  }
  return retval;
}


void
append_to_file_or_die(const char* path, const void* buf, size_t count)
{
  int fd = open_or_die(path, O_WRONLY | O_APPEND, 0);
  write_all_bytes_or_die(fd, buf, count);
  close_or_die(fd); 
}


ssize_t
read_uninterrupted_or_die(int fd, void* buf, size_t count)
{
  // Keep retrying until signals stop arriving.
  ssize_t n;
  do
    n = read(fd, buf, count);
  while (n < 0 && errno == EINTR);

  // Did we get some data?
  if (n > 0)
    return n;

  // Actual EOF?  Set errno artificially to distinguish this case.
  if (n == 0)
  {
    errno = EPIPE;
    return -1;
  }

  // Other cases like EOF; EIO is from when the other end of a PTY exits.
  if (errno == ECONNRESET || (errno == EIO && isatty(fd)))
    return -1;

  // We return zero if and only if this was a non-blocking read.
  if (errno == EAGAIN)
    return 0;

  punt_with_errno("Failure in 'read(%d, ..., %u)'", fd, (uint)count);
}


ssize_t
read_some_bytes_or_die(int fd, void* buf, size_t count)
{
  size_t total = 0;

  while (total < count)
  {
    ssize_t n = read_uninterrupted_or_die(fd, buf + total, count - total);
    if (n == 0)
      break;
    if (n < 0)
    {
      if (total != 0)
        break;
      return -1;
    }
    total += n;
  }

  return total;
}


void
read_all_bytes_or_die(int fd, void* buf, size_t count)
{
  if (read_some_bytes_or_die(fd, buf, count) != count)
  {
    punt("Unexpected EOF (or EAGAIN).");
  }
}


size_t
read_from_file_or_die(const char* path, void* buf, size_t count)
{
  int fd = open_or_die(path, O_RDONLY, 0);
  ssize_t n = read_some_bytes_or_die(fd, buf, count);
  close_or_die(fd); 
  return (n >= 0) ? n : 0;
}


void
write_uint64(uint8_t* p, uint64_t v)
{
  *p++ = (v >> 0);
  *p++ = (v >> 8);
  *p++ = (v >> 16);
  *p++ = (v >> 24);
  *p++ = (v >> 32);
  *p++ = (v >> 40);
  *p++ = (v >> 48);
  *p++ = (v >> 56);
}


uint64_t
read_uint64(const uint8_t* p)
{
  return read_uint(p) | (((uint64_t)read_uint(p + 4)) << 32);
}



void
write_uint(uint8_t* p, uint v)
{
  *p++ = (v >> 0);
  *p++ = (v >> 8);
  *p++ = (v >> 16);
  *p++ = (v >> 24);
}


uint
read_uint(const uint8_t* p)
{
  uint b0 = *p++;
  uint b1 = *p++;
  uint b2 = *p++;
  uint b3 = *p++;
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | (b0);
}



void
write_uint16(uint8_t* p, uint16_t v)
{
  *p++ = (v >> 0);
  *p++ = (v >> 8);
}


uint16_t
read_uint16(const uint8_t* p)
{
  uint b0 = *p++;
  uint b1 = *p++;
  return (b1 << 8) | (b0);
}


