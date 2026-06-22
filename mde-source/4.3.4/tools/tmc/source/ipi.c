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

// This file attempts to support non-linux operating systems (like bogux),
// where there is no "/dev/hardwall" file, using various hackery.
// Specifically, a permanent global hardwall is assumed, and "TMC_IPI_INFO"
// is never checked or set.
//

#include <arch/interrupts.h>
#include <arch/spr_def.h>

#include <tmc/interrupt.h>
#include <tmc/ipi.h>
#include <tmc/mem.h>
#include <tmc/spin_64.h>
#include <tmc/task.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <asm/hardwall.h>

#include "handy.c"


// Global information about the ipi.
static int __tmc_ipi_fd = -1;

// True once "init" has been called.
static bool __tmc_ipi_ready;

// Per-thread variable that indicates whether ipi has been activated
// on this thread.  Since the ipi can't be activated until the thread
// has been affinitized to a cpu, this lets us check that we're bound
// before allocating an event using the per-thread variable
// __ipi_event_alloced_mask.
static __thread bool __ipi_activated;

// Per-thread variable to keep track of allocated events. 
static __thread uint32_t __ipi_event_alloced_mask;

// Lock for event allocation.  Not technically needed because by the time
// we're allocating an event, we're essentially single-threaded because
// we don't currently have a preemptive kernel.
static __thread tmc_spin_mutex_t __ipi_alloc_lock;

// Return true if ipi is "available".
//
// Analyzes the "initial" value of "TMC_IPI_INFO" exactly once, to
// determine if an inherited hardwall is available.
//
static bool
__tmc_ipi_available(void)
{
  static bool done;

  if (!done)
  {
    // Note that getenv/unsetenv are locked internally by libc, so
    // we don't need to provide any explicit locking here.
    // And note that even if two threads race to set up "done",
    // they will both be setting the same values.
    //
    const char* info = getenv("TMC_IPI_INFO");

    uint i1;
    if (info != NULL &&
        sscanf(info, "0:%u", &i1) == 1)
    {
      __tmc_ipi_fd = i1;
    }
    else if (info != NULL)
    {
      // HACK: Clear "broken" info.
      unsetenv("TMC_IPI_INFO");
    }

    // Fence here so that another thread is guaranteed to find the
    // correct values in the __tmc_ipi_xxx variables if it sees that
    // "done" is true.
    tmc_mem_fence();

    done = true;
  }

  return (__tmc_ipi_fd >= 0);
}


static int
tmc_ipi_init_aux(cpu_set_t* cpus)
{
  // Convert NULL to "current affinity".
  cpu_set_t affinity;
  if (cpus == NULL)
  {
    cpus = &affinity;
    if (tmc_cpus_get_my_affinity(cpus) != 0)
      return -1;
  }

  if (__tmc_ipi_available())
  {
    // Already initialized.
    if (__tmc_ipi_ready)
    {
      errno = EINVAL;
      return 1;
    }

    // Inherit persisted hardwall.
  }
  else
  {
    // Open the special hardwall file.
    int fd = open("/dev/hardwall/ipi", O_RDONLY, 0);
    if (fd < 0)
      return -1;

    // Create a hardwall.
    if (ioctl(fd, HARDWALL_CREATE(sizeof(*cpus)), cpus) < 0)
    {
      close(fd);
      return -1;
    }

    // Save the fd.
    __tmc_ipi_fd = fd;
  }

  // Implicit "persist(false)".
  (void)set_close_on_exec(__tmc_ipi_fd, true);
  unsetenv("TMC_IPI_INFO");

  // Fence here so all possible data we've written is visible
  // before we set "ready", since tmc_ipi_init() does not take
  // the lock prior to examining __tmc_ipi_ready.
  tmc_mem_fence();

  __tmc_ipi_ready = true;

  return 0;
}

int
tmc_ipi_init(cpu_set_t* cpus)
{
  if (__tmc_ipi_ready)
  {
    errno = EINVAL;
    return 1;
  }

  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  PTHREAD_MUTEX_DEFINE(init_lock);
  PTHREAD_MUTEX_LOCK(&init_lock);

  int retval = tmc_ipi_init_aux(cpus);

  PTHREAD_MUTEX_UNLOCK(&init_lock);

  return retval;
}

int
tmc_ipi_close(void)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (!__tmc_ipi_available())
  {
    errno = ENODATA;
    return 1;
  }

  (void)close(__tmc_ipi_fd);

  __tmc_ipi_fd = -1;

  unsetenv("TMC_IPI_INFO");

  __tmc_ipi_ready = false;

  return 0;
}


int
tmc_ipi_activate(void)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (!__tmc_ipi_available() || !__tmc_ipi_ready)
  {
    errno = ENODATA;
    return -1;
  }

  if (ioctl(__tmc_ipi_fd, HARDWALL_ACTIVATE) < 0)
    return -1;

  __ipi_activated = true;

  return 0;
}


struct ipi_event_callback {
  tmc_ipi_func_t func;
  void* arg;
};

static __thread
struct ipi_event_callback __ipi_event_callback[TMC_IPI_NUM_EVENTS];

int
tmc_ipi_deactivate(void)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (!__tmc_ipi_available() || !__tmc_ipi_ready)
  {
    errno = ENODATA;
    return -1;
  }

  if (ioctl(__tmc_ipi_fd, HARDWALL_DEACTIVATE) < 0)
    return -1;

  __ipi_activated = false;  
  __ipi_event_alloced_mask = 0;
  for (int i = 0; i < TMC_IPI_NUM_EVENTS; i++)
    __ipi_event_callback[i].func = 0;

  return 0;
}


static void
__ipi0_handler()
{
  unsigned long events = __insn_mfspr(SPR_IPI_EVENT_0);
  __insn_mtspr(SPR_IPI_EVENT_RESET_0, events);

  for (int i = 0; events; i++, events >>= 1)
  {
    struct ipi_event_callback* ecp = &__ipi_event_callback[i];
    if (events & 1)
    {
      if (ecp->func)
      {
        ecp->func(ecp->arg);
      }
    }
  }
}


int
tmc_ipi_event_install(int ipi, int event, tmc_ipi_func_t func, void* arg)
{
  if ((ipi != 0) || (event < 0) || (event > TMC_IPI_MAX_EVENT))
  {
    errno = EINVAL;
    return -1;
  }

  __ipi_event_callback[event].func = func;
  __ipi_event_callback[event].arg = arg;

  if (func)
  {
    tmc_interrupt_c_install(INT_IPI_0, __ipi0_handler);
    __insn_mtspr(SPR_INTERRUPT_MASK_RESET_0, 1ULL << INT_IPI_0);

    __insn_mf();

    __insn_mtspr(SPR_IPI_MASK_RESET_0, 1ULL << event);
  }
  else
  {
    __insn_mtspr(SPR_IPI_MASK_SET_0, 1ULL << event);
  }

  return 0;
}


int
tmc_ipi_event_alloc(int ipi, int event)
{
  if (!__ipi_activated)
  {
    errno = EACCES;
    return -1;
  }

  if (ipi != 0 || event < 0 || event > TMC_IPI_MAX_EVENT)
  {
    errno = EINVAL;
    return -1;
  }

  tmc_spin_mutex_lock(&__ipi_alloc_lock);
  if (__ipi_event_alloced_mask & (1 << event))
  {
    tmc_spin_mutex_unlock(&__ipi_alloc_lock);
    errno = EBUSY;
    return -1;
  }
  else
    __ipi_event_alloced_mask |= 1 << event;

  tmc_spin_mutex_unlock(&__ipi_alloc_lock);

  return event;
}


int
tmc_ipi_event_alloc_first_available(int ipi)
{
  if (!__ipi_activated)
  {
    errno = EACCES;
    return -1;
  }

  if (ipi != 0)
  {
    errno = EINVAL;
    return -1;
  }

  tmc_spin_mutex_lock(&__ipi_alloc_lock);

  int event;
  for (event = 0; event <= TMC_IPI_MAX_EVENT; event++)
  {
    if ((__ipi_event_alloced_mask & (1 << event)) == 0)
      break;
  }

  if (event > TMC_IPI_MAX_EVENT)
  {
    tmc_spin_mutex_unlock(&__ipi_alloc_lock);
    errno = EBUSY;
    return -1;
  }

  __ipi_event_alloced_mask |= 1 << event;
  tmc_spin_mutex_unlock(&__ipi_alloc_lock);

  return event;
}


int
tmc_ipi_event_dealloc(int ipi, int event)
{
  if (!__ipi_activated)
  {
    errno = EACCES;
    return -1;
  }

  if (ipi != 0 || event < 0 || event > TMC_IPI_MAX_EVENT ||
      !(__ipi_event_alloced_mask & (1 << event)))
  {
    errno = EINVAL;
    return -1;
  }

  tmc_spin_mutex_lock(&__ipi_alloc_lock);
  __ipi_event_alloced_mask &= ~(1 << event);
  tmc_spin_mutex_unlock(&__ipi_alloc_lock);

  return 0;
}
