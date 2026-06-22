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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#ifndef __tile__
#define TILEPCI_HOST
#endif

#if TILE_CHIP < 10
#include <asm/tilepci.h>
#else
#include <asm/tilegxpci.h>
#endif

#ifndef __tile__
#include <sys/utsname.h>
#endif


//! The size of buffers we post to PCI.
#define BUFFER_SIZE TILEPCI_MAX_XFER_LEN


// The stack of free buffers (writer only).
static void* g_buffers[TILEPCI_CMD_SLOTS];

// The size of that stack (writer only).
static uint g_buffers_size;

// The total buffers (writer only).
static uint g_buffers_max;

// The PCI fd.
static int g_pci_fd = -1;

// The fd for input or output.
static int g_fd = -1;

// The pid of the sub-command, if any.
static pid_t g_pid = -1;


// Modeled on "tmc_task_die()".
//
static void
die(const char* format, ...)
  __attribute__((format(printf, 1, 2), noreturn));
static void
die(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  fflush(stderr);
  va_end(args);
  exit(128);
}


static void
die_with_errno(const char* format, ...)
  __attribute__((format(printf, 1, 2), noreturn));
static void
die_with_errno(const char* format, ...)
{
  int err = errno;
  va_list args;
  va_start(args, format);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, ": %s\n", strerror(err));
  fflush(stderr);
  va_end(args);
  exit(128);
}


static int
atoi_or_die(const char* str)
{
  char* end;
  errno = 0;
  long val = strtol(str, &end, 10);
  if (*end != '\0' || end == str || errno == ERANGE ||
      (sizeof(long) > sizeof(int) && (val > INT_MAX || val < INT_MIN)))
  {
    die("Cannot parse int from '%s'.", str);
  }
  return val;
}


static int
close_boldly(int fd)
{
  while (true)
  {
    int result = close(fd);
    if (result == 0 || errno != EINTR)
      return result;
  }
}


static void
close_or_die(int fd)
{
  if (close_boldly(fd) == 0)
    return;

  die_with_errno("Failure in 'close(%d)'", fd);
}


static void
pipe_or_die(int fds[2])
{
  if (pipe(fds) != 0)
    die_with_errno("Failure in 'pipe()'");
}


static int
dup2_or_die(int oldfd, int newfd)
{
  int ret = dup2(oldfd, newfd);
  if (ret >= 0)
    return ret;

  die_with_errno("Failure in 'dup2(%d, %d)'", oldfd, newfd);
}


static int
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


static void
set_close_on_exec_or_die(int fd, bool flag)
{
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags != -1)
  {
    flags = flag ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
    if (fcntl(fd, F_SETFD, flags) == 0)
      return;
  }
  die_with_errno("Failure in 'set_close_on_exec_or_die(%d, %d)'", fd, flag);
}


static pid_t
fork_or_die(void)
{
  // HACK: Flush console.
  fflush(NULL);

  pid_t pid = fork();
  if (pid >= 0)
    return pid;

  die_with_errno("Failure in 'fork()'");
}


static ssize_t
write_some_bytes_or_die(int fd, const void* buf, size_t count)
{
  size_t total = 0;

  while (total < count)
  {
    ssize_t n = write(fd, buf + total, count - total);
    if (n > 0)
    {
      total += n;
    }
    else if (errno == EPIPE ||
             errno == ECONNRESET ||
             // HACK: This happens when the other end of a PTY exits.
             // ISSUE: Actually, this has only been observed for "read()".
             (errno == EIO && isatty(fd)))
    {
      // Treat as EOF.
      if (total != 0)
        break;
      return -1;
    }
    else if (errno == EINTR)
    {
      // Try again.
    }
    else if (errno == EAGAIN)
    {
      // Nothing available.
      break;
    }
    else
    {
      die_with_errno("Failure in 'write(%d, ..., %u)'", fd, (uint)count);
    }
  }

  return total;
}


static void
write_all_bytes_or_die(int fd, const void* buf, size_t count)
{
  if (write_some_bytes_or_die(fd, buf, count) != count)
  {
    die("Unexpected EOF (or EAGAIN).");
  }
}


static ssize_t
read_some_bytes_or_die(int fd, void* buf, size_t count)
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
      // Actual EOF.
      if (total != 0)
        break;
      errno = EPIPE;
      return -1;
    }
    else if (errno == ECONNRESET ||
             // HACK: This happens when the other end of a PTY exits.
             (errno == EIO && isatty(fd)))
    {
      // Treat like EOF.
      if (total != 0)
        break;
      return -1;
    }
    else if (errno == EINTR)
    {
      // Try again.
    }
    else if (errno == EAGAIN)
    {
      // Nothing available.
      break;
    }
    else
    {
      die_with_errno("Failure in 'read(%d, ..., %u)'", fd, (uint)count);
    }
  }

  return total;
}



// Handle incoming buffers (reader only).
//
static void
handle_incoming_receive_buffers(void)
{
  // Read as many completions as possible.
  tilepci_xfer_comp_t comp[TILEPCI_CMD_SLOTS];
  ssize_t num_bytes = read_some_bytes_or_die(g_pci_fd, comp, sizeof(comp));

  // FIXME: Is this possible?
  if (num_bytes < 0)
    die("Lost PCI channel!");

  // Handle incoming data.
  int num = num_bytes / sizeof(comp[0]);
  tilepci_xfer_req_t bufs[num];
  for (int i = 0; i < num; i++)
  {
#if TILE_CHIP < 10
    void* addr = comp[i].addr;
#else
    uintptr_t addr = (uintptr_t)comp[i].addr;
#endif
    uint len = comp[i].len;
#if TILE_CHIP < 10
    uint flags = comp[i].flags;
#else
    uint flags = (uint)comp[i].cookie;
#endif

#if TILE_CHIP < 10
    write_all_bytes_or_die(g_fd, addr, len);
#else
    write_all_bytes_or_die(g_fd, (void*)addr, len);
#endif 

    if (flags & TILEPCI_CPL_EOP)
    {
      close(g_fd);
      g_fd = -1;
      exit(0);
    }

    // Buffer is available again.
    bufs[i] = (tilepci_xfer_req_t) {
      .addr = addr,
      .len = BUFFER_SIZE,
#if TILE_CHIP < 10
      .flags = TILEPCI_RCV_MAY_EOP
#endif 
    };
  }

  // Post available "receive" buffers.
  if (num != 0)
    write_all_bytes_or_die(g_pci_fd, bufs, num * sizeof(bufs[0]));
}


// Advance (writer only).
//
static void
advance(void)
{
  tilepci_xfer_req_t reqs[TILEPCI_CMD_SLOTS];
  int num = 0;

  while (num < TILEPCI_CMD_SLOTS)
  {
    // Stop when done.
    if (g_fd == -1)
      break;

    // Stop when out of buffers.
    if (g_buffers_size == 0)
      break;

    // Access (but do not pop) a free buffer.
    void* addr = g_buffers[g_buffers_size - 1];

    uint len = 0;
    uint flags = 0;

    // Read from source file into "buf"/"len"/"eof".
    int n = read_some_bytes_or_die(g_fd, addr, BUFFER_SIZE);

    // ISSUE: Should we be non-blocking?
    if (n == 0)
      break;

    // HACK: Treat EOF as a (very) short read.
#if TILE_CHIP < 10
    len = (n > 0) ? n : 0;
#else
    len = (n > 0) ? n : 1;
#endif

    // Handle EOF.
    if (n < BUFFER_SIZE)
    {
#if TILE_CHIP < 10
      flags = TILEPCI_SEND_EOP;
#else
      flags = TILEPCI_CPL_EOP;
#endif
      close(g_fd);
      g_fd = -1;
    }

    // Pop the buffer.
    g_buffers_size--;

    // Collect the request.
    reqs[num++] = (tilepci_xfer_req_t) {
#if TILE_CHIP < 10
      .addr = addr,
#else 
      .addr = (uintptr_t)addr, 
#endif
      .len = len, 
#if TILE_CHIP < 10
      .flags = flags
#else
      .cookie = flags
#endif
    };
  }

  if (num != 0)
  {
    write_all_bytes_or_die(g_pci_fd, reqs, num * sizeof(reqs[0]));
  }

  // Exit when done.
  if (g_fd == -1 && g_buffers_size == g_buffers_max)
  {
    exit(0);
  }
}


// Handle completions (writer only).
//
static void
handle_completions(void)
{
  // Read as many completions as possible.
  tilepci_xfer_comp_t comp[TILEPCI_CMD_SLOTS];
  ssize_t num_bytes = read_some_bytes_or_die(g_pci_fd, comp, sizeof(comp));

  if (num_bytes < 0)
    die("Unexpected EOF on channel.");

  // Collect available buffers.
  int num = num_bytes / sizeof(comp[0]);
  for (int i = 0; i < num; i++)
  {
#if TILE_CHIP < 10
    void* addr = comp[i].addr;
    g_buffers[g_buffers_size++] = addr;
#else
    uintptr_t addr = comp[i].addr;
    g_buffers[g_buffers_size++] = (void*)addr;
#endif
  }

  advance();
}



int
main(int argc, char* argv[])
{
  int reader = 0;
  int writer = 0;

#ifndef __tile__

  // Exec "EXE-64" if needed.
  struct utsname un;
  if (sizeof(long) == 4 && uname(&un) == 0 && !strcmp(un.machine, "x86_64"))
  {
    size_t len = strlen(argv[0]) + 3 + 1;
    char exe[len];
    snprintf(exe, len, "%s-64", argv[0]);
    argv[0] = exe;
    (void)execvp(exe, argv);
  }

  int card = 0;

#endif

  int which = 0;

  int num = 1;

  int i = 1;
  while (i < argc)
  {
    const char* arg = argv[i++];

    if (!strcmp(arg, "-r"))
    {
      reader = 1;
    }
    else if (!strcmp(arg, "-w"))
    {
      writer = 1;
    }
#ifndef __tile__
    else if ((!strcmp(arg, "--pci-card") || !strcmp(arg, "-c")) && i < argc)
    {
      card = atoi_or_die(argv[i++]);
    }
#endif
    else if (!strcmp(arg, "-z") && i < argc)
    {
      which = atoi_or_die(argv[i++]);
    }
    else if (!strcmp(arg, "--buffers") && i < argc)
    {
      num = atoi_or_die(argv[i++]);
      if (num <= 0 || num > TILEPCI_CMD_SLOTS)
        die("Invalid buffer count.");
    }
    else if (!strcmp(arg, "--"))
    {
      break;
    }
    else if (arg[0] == '-')
    {
      die("Unknown option '%s'", arg);
    }
    else
    {
      i--;
      break;
    }
  }

  // Default to writing from host to tile.
  if (!reader && !writer)
  {
#ifdef __tile__
    reader = 1;
#else
    writer = 1;
#endif
  }

  if (i == argc || reader == writer)
  {
    fprintf(stderr,
            "Usage: %s [options] [--] CMD\n"
            "  -r -- Run a command which will read remote data\n"
            "  -w -- Run a command which will write remote data\n"
#ifndef __tile__
            "  -c CARD -- Use the given PCI card (default 0)\n"
#endif
            "  -z WHICH -- Use the given PCI ZC channel (default 0)\n"
            "  --buffers N -- Use the given number of buffers (default 1)\n",
            argv[0]);
    exit(1);
  }

  char path[128];

#ifdef __tile__
#if TILE_CHIP < 10
  if (reader)
    snprintf(path, sizeof(path), "/dev/hostpci/h2t/%d", which);
  else
    snprintf(path, sizeof(path), "/dev/hostpci/t2h/%d", which);
#else
  if (reader)
    snprintf(path, sizeof(path), "/dev/trio0-mac0/h2t/%d", which);
  else
    snprintf(path, sizeof(path), "/dev/trio0-mac0/t2h/%d", which);
#endif
#else
#if TILE_CHIP < 10
  if (reader)
    snprintf(path, sizeof(path), "/dev/tilepci%d/t2h/%d", card, which);
  else
    snprintf(path, sizeof(path), "/dev/tilepci%d/h2t/%d", card, which);
#else
  if (reader)
    snprintf(path, sizeof(path), "/dev/tilegxpci%d/t2h/%d", card, which);
  else
    snprintf(path, sizeof(path), "/dev/tilegxpci%d/h2t/%d", card, which);
#endif
#endif 

  while (true)
  {
    g_pci_fd = open(path, O_RDWR | O_NONBLOCK);
    if (g_pci_fd >= 0)
      break;

    if (errno != ENODEV && errno != ENXIO)
      die_with_errno("Unable to open %s", path);

    // ISSUE: Hmmm.
    sleep(1);
  }

  set_close_on_exec_or_die(g_pci_fd, true);

  if (num != 1)
  {
    // Specify the number of command slots.
    if (ioctl(g_pci_fd, TILEPCI_IOC_SET_NCMD, num) != 0)
      die_with_errno("Setting PCIe command slots failed");
  }

  // Allocate some "shared" memory.
  void* mem = mmap(0, BUFFER_SIZE * num,
                   PROT_READ | PROT_WRITE, MAP_SHARED, g_pci_fd, 0);
  if (mem == MAP_FAILED)
    die_with_errno("Unable to mmap PCI buffer");

#ifdef __tile__
#if TILE_CHIP >= 10
  // Register the buffer to IOTLB.
  tilegxpci_buf_info_t buf_info = {
    .va = (uintptr_t) mem,
    .size = BUFFER_SIZE * num,
  };

  int result = ioctl(g_pci_fd, TILEPCI_IOC_REG_BUF, &buf_info);
  assert(result == 0);
#endif
#endif

  if (reader)
  {
    // Prepare initial "receive" buffers.
    tilepci_xfer_req_t bufs[TILEPCI_CMD_SLOTS];
    for (int j = 0; j < num; j++)
    {
      bufs[j] = (tilepci_xfer_req_t) {
#if TILE_CHIP < 10
        .addr = mem + (j * BUFFER_SIZE),
#else
        .addr = (uintptr_t)(mem + (j * BUFFER_SIZE)),
#endif
        .len = BUFFER_SIZE,
#if TILE_CHIP < 10
        .flags = TILEPCI_RCV_MAY_EOP
#endif 
      };
    }

    // Post initial "receive" buffers.
    write_all_bytes_or_die(g_pci_fd, bufs, num * sizeof(bufs[0]));
  }
  else
  {
    // Create initial buffers.
    for (int j = 0; j < num; j++)
    {
      void* addr = mem + (j * BUFFER_SIZE);
      g_buffers[g_buffers_size++] = addr;
    }

    g_buffers_max = g_buffers_size;
  }

  if (1)
  { 
    // Use argv + i as a "popen()" command

    int pipe_pair[2];
    pipe_or_die(pipe_pair);

    g_pid = fork_or_die();

    if (g_pid == 0)
    {
      // Child.
      if (reader)
      {
        close_or_die(pipe_pair[1]);
        dup2_and_close_or_die(pipe_pair[0], STDIN_FILENO);
      }
      else
      {
        close_or_die(pipe_pair[0]);
        dup2_and_close_or_die(pipe_pair[1], STDOUT_FILENO);
      }
      (void)execvp(argv[i], argv + i);
      die_with_errno("Failure in 'execvp(%s)'", argv[i]);
    }
    if (reader)
    {
      close_or_die(pipe_pair[0]);
      g_fd = pipe_pair[1];
    }
    else
    {
      close_or_die(pipe_pair[1]);
      g_fd = pipe_pair[0];
    }
  }
  else
  {
    // FIXME: Open file.
  }

  if (writer)
    advance();

  while (true)
  {
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(g_pci_fd, &readfds);

    // Wait for something to happen.
    int selectable = select(g_pci_fd + 1, &readfds, NULL, NULL, NULL);

    // Handle failures.
    if (selectable <= 0)
    {
      if (errno == EINTR)
        continue;

      die_with_errno("Failure in 'select()'");
    }

    // Paranoia.
    if (!FD_ISSET(g_pci_fd, &readfds))
      continue;

    if (reader)
      handle_incoming_receive_buffers();
    else
      handle_completions();
  }
}

