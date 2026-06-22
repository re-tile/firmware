/**
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 *
 * Implements system call handlers.
 * @file
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <sys/times.h>

#include <tnslock.h>
#include <hv/hypervisor.h>

#include <arch/cycle.h>

#include "bogux.h"
#include "debug.h"
#include "files.h"
#include "mman.h"
#include "argv.h"
#include "errno.h"
#include "syscall.h"
#include "messaging.h"
#include "mem_layout.h"
#include "intvec.h"


/* Track state for clone, sched_setaffinity, execve pattern */
static struct pt_regs ts_fork_regs _TILESTATE;
static int ts_fork_tile _TILESTATE;
static bool ts_fork_tile_set _TILESTATE;


// Return the registers saved on syscall entry.  Since they are always
// saved at a fixed address, they're easy to find.
static inline struct pt_regs*
syscall_regs()
{
  return (struct pt_regs*) (MEM_STACK_INIT - sizeof(struct pt_regs));
}


int
sys_ni_syscall(int nr)
{
  SYSCALL_TRACE("ni_syscall: syscall number is %d\n", nr);
  return -ENOSYS;
}


int
sys_dummy_syscall()
{
  return 0;
}


int
sys_exit(int status)
{
  if (ts_fork_regs.regs_valid != 0)
  {
    // We are in a clone context, and presumably the execve failed,
    // so we need to jump back to the parent clone.
    *syscall_regs() = ts_fork_regs;
    ts_fork_regs.regs_valid = 0;
    ts_fork_tile = 0;
    ts_fork_tile_set = false;

    // Normally we'd report a pid to the parent, and the pid wouldn't
    // exist any more.  To kind of simulate this, we just report a
    // very large number here, that will fail any kill(pid,0) tests.
    return 1000000;
  }
  else
  {
    do_closeall();
    printf("Application exited with status %d.\n", status);
    nap_user();
    /*NOTREACHED*/
  }
}


VirtualAddress
sys_mmap(VirtualAddress start, size_t length, int prot, int flags, int fd,
         off_t offset)
{
  // Note: 32-bit passes the page offset, 64-bit the byte offset,
  // but Bogux doesn't really use either one internally, so we don't care.
  return do_mmap(start, length, prot, flags, fd, offset,
                 ts_controller, MMAP_LAZY);
}


int
sys_clone()
{
  struct pt_regs* regs = syscall_regs();
  ts_fork_regs = *regs;
  regs->regs_valid = 0;  /* no need to waste time restoring the regs */
  return 0;   /* at first, return in the child */
}


int
sys_sched_getaffinity(int pid, unsigned int len, unsigned long* mask)
{
  /* Do some initial basic validation */
  if (pid != 0 && pid != sys_getpid())
  {
    warn("Invalid sched_getaffinity() call (not for my pid)\n");
    return -EINVAL;
  }
  int tiles = width * height;
  if ((len & (sizeof(long)-1)) != 0 || len < ((tiles + 7) / 8))
  {
    warn("Invalid sched_getaffinity() call (bad len %d)\n", len);
    return -EINVAL;
  }
  if (!is_valid_user_buf(mask, len, PROT_WRITE))
    return -EFAULT;

  len = (tiles + 7) / 8;
  memset(mask, 0, len);
  int tile = my_tile_id();
  mask[tile / (sizeof(long)*8)] = 1UL << (tile & (sizeof(long)*8-1));
  return len;
}


int
sys_sched_setaffinity(int pid, unsigned int len, unsigned long* mask)
{
  /* Do some initial basic validation */
  if (pid != 0 && pid != sys_getpid())
  {
    warn("Invalid sched_setaffinity() call (not for my pid)\n");
    return -EINVAL;
  }
  if ((len & (sizeof(long)-1)) != 0)
  {
    warn("Invalid sched_setaffinity() call (bad len %d)\n", len);
    return -EINVAL;
  }
  if (!is_valid_user_buf(mask, len, PROT_READ))
    return -EFAULT;

  /* Find the unique set bit in the mask */
  int index = -1;
  int shift = 0;
  len /= sizeof(long);  /* convert to words */
  for (int i = 0; i < len; ++i)
  {
    int bit = __builtin_ctzl(mask[i]);
    if (bit != sizeof(long)*8)
    {
      if (index == -1 && __builtin_popcountl(mask[i]) == 1)
      {
        index = i;
        shift = bit;
      }
      else
      {
        warn("Invalid sched_setaffinity() call (multiple set bits)\n");
        index = -1;
        break;
      }
    }
  }
  unsigned int remote = index * sizeof(long) * 8 + shift;
  unsigned int tiles = width * height;
  if (remote >= tiles)
  {
    warn("Invalid sched_setaffinity() call (invalid tile %d)\n", remote);
    return -EINVAL;
  }
  if (!is_avail_tile(remote))
  {
    warn("Invalid sched_setaffinity() call (reserved tile %d)\n", remote);
    return -EINVAL;
  }
  if (remote == my_tile_id())
  {
    // Tolerate a no-op call to set affinity to where we are.
    if (ts_fork_regs.regs_valid == 0)
      return 0;
    else
    {
      warn("Invalid sched_setaffinity() call (self tile %d)\n", remote);
      return -EINVAL;
    }
  }

  ts_fork_tile = remote;
  ts_fork_tile_set = true;
  return 0;
}

int
sys_execve(const char* filename, char** argv, char** envp)
{
  int retval;

  // See if this is the simple same-tile exec case
  if (ts_fork_regs.regs_valid == 0)
  {
    // Allocate and populate some memory with the passed data
    ExecData data;
    retval = build_exec_data(&data, true, false, filename, argv, envp);
    if (retval < 0)
      return retval;

    // Try to run the exec
    return do_execve(&data);
  }

  if (!ts_fork_tile_set)
  {
    warn("Invalid execve() call after clone() without setaffinity()\n");
    retval = -EINVAL;
  }

  // See if target tile is available
  else if (!unidle_tile(ts_fork_tile))
    retval = -EBUSY;

  else
  {
    // Allocate and populate some memory with the passed data
    SMsg_Exec msg;
    retval = build_exec_data(&msg.data, true, true, filename, argv, envp);
    if (retval >= 0)
    {
      // Send the message to the target.
      HV_Recipient recip;
      recip.x = ts_fork_tile % width;
      recip.y = ts_fork_tile / width;
      recip.state = HV_TO_BE_SENT;
      msg.tag = SMSG_EXEC;
      __insn_mf();   // in case we are using the magic hypervisor
      int rc = hv_send_message(&recip, 1, (VirtualAddress) &msg, sizeof(msg));
      if (rc != 1)
      {
        panic("hv_send_message returned unexpected result %d, state %d\n",
              -rc, recip.state);  // FIXME
      }
      retval = tile_to_pid(ts_fork_tile);
    }
  }

  // Return to the site of the original fork, and clear our remote-exec state
  *syscall_regs() = ts_fork_regs;
  ts_fork_regs.regs_valid = 0;
  ts_fork_tile = 0;
  ts_fork_tile_set = false;
  return retval;
}


int
sys_readlinkat(int dfd, const char* path, char* buf, size_t buflen)
{
  if (!is_valid_user_string(path, PROT_READ) ||
      !is_valid_user_buf(buf, buflen, PROT_WRITE))
    return -EFAULT;

  SYSCALL_TRACE("readlink: path is %s\n", path);

  if (strcmp(path, "/proc/self/exe") == 0)
  {
    size_t len = strlen(ts_exec_path);   // do not include trailing NUL
    if (len > buflen)
      len = buflen;
    memcpy(buf, ts_exec_path, len);
    return len;
  }

  return -EINVAL;
}


int
sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
  if (tv)
  {
    if (!is_valid_user_buf(tv, sizeof(*tv), PROT_WRITE))
      return -EFAULT;

    uint64_t cycles_per_sec = hv_sysconf(HV_SYSCONF_CPU_SPEED);
    uint64_t cycles = get_cycle_count();
    tv->tv_sec = cycles / cycles_per_sec;
    tv->tv_usec = ((cycles % cycles_per_sec) * 1000000ULL) / cycles_per_sec;
  }

  if (tz)
  {
    // Nobody uses this, but return EDT anyway.
    if (!is_valid_user_buf(tz, sizeof(*tz), PROT_WRITE))
      return -EFAULT;
    tz->tz_minuteswest = 240;
    tz->tz_dsttime =  0;
  }

  return 0;
}


int
sys_times(struct tms* t)
{
  if (!is_valid_user_buf(t, sizeof(*t), PROT_WRITE))
    return -EFAULT;
  memset(t, 0, sizeof(*t));
  uint64_t now = get_cycle_count();
  t->tms_stime = t->tms_cstime = 0;
  // CLK_TCK = 100 on TILE
  t->tms_utime = t->tms_cutime =
    (now - ts_user_start_cycle) / (CHIP_CLOCK_SPEED / 100);
  return (int) now;
}


int
sys_getpid()
{
  return tile_to_pid(my_tile_id());
}


int
sys_getcpu(int *cpup, int *nodep)
{
  int err = 0;
  if (cpup)
  {
    if (!is_valid_user_buf(cpup, sizeof(*cpup), PROT_WRITE))
      err = -EFAULT;
    else
      *cpup = my_tile_id();
  }
  if (nodep)
  {
    if (!is_valid_user_buf(nodep, sizeof(*nodep), PROT_WRITE))
      err = -EFAULT;
    else
      *nodep = ts_controller;
  }
  return err;
}


int
sys_kill(pid_t pid, int sig)
{
  if (sig == 0)
  {
    return pid_active(pid) ? 0 : -ESRCH;
  }
  else if (pid == tile_to_pid(my_tile_id()))
  {
    printf("Exiting on signal %u\n", sig);
    exit(1);
  }
  else
  {
    return -EINVAL;
  }
}

time_t
sys_time(time_t* p)
{
  uint64_t cycles_per_sec = hv_sysconf(HV_SYSCONF_CPU_SPEED);
  uint64_t cycles = get_cycle_count();
  time_t sec = cycles / cycles_per_sec;

  if (p != NULL)
  {
    if (!is_valid_user_buf(p, sizeof(*p), PROT_WRITE))
      return -EFAULT;
    *p = sec;
  }
  return sec;
}


static int atomic_lock _LOCKS;

static int
lock_atomic(int* mem)
{
  if (!is_valid_user_buf(mem, sizeof(*mem), PROT_WRITE) ||
      (((uintptr_t) mem) & 3) != 0) {
    return 0;
  }

  tnslock_rawlock(&atomic_lock);
  return 1;
}

static void
unlock_atomic(int* mem)
{
  __insn_mf();
  atomic_lock = 0;
}

int
sys_atomic(int* mem, int oldval, int newval, int which)
{
  if (!lock_atomic(mem))
    return -EFAULT;  /* API requires a signal, but we don't have them! */
  int was = *mem;
  if (which == __NR_FAST_cmpxchg)
  {
    if (was == oldval)
      *mem = newval;
  }
  else
  {
    int result = (was & oldval) + newval;
    if (result != was)
      *mem = result;
  }
  unlock_atomic(mem);
  return was;
}

struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

int
sys_uname(void* _uname)
{
  struct utsname *uname = _uname;
  if (!is_valid_user_buf(uname, sizeof(*uname), PROT_WRITE))
    return -EFAULT;
  strcpy(uname->sysname, "Linux");  // OK, not exactly.
  strcpy(uname->nodename, "localhost");
  strcpy(uname->release, "2.6.36");   // See tools/glibc/Makefile.tilera.in
  strcpy(uname->version, "#1");
  strcpy(uname->machine, "tilegx");
  strcpy(uname->domainname, "");
  return 0;
}
