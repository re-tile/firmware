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
// Specifically, a permanent global hardwall is assumed, and "TMC_UDN_INFO"
// is never checked or set.
//

#include <tmc/mem.h>
#include <tmc/sync.h>
#include <tmc/task.h>
#include <tmc/udn.h>

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <asm/hardwall.h>


#include "handy.c"


// Global information about the hardwall.
static uint __tmc_udn_x;
static uint __tmc_udn_y;
static uint __tmc_udn_w;
static uint __tmc_udn_h;
static int __tmc_udn_fd = -1;

// True once "init" has been called.
static bool __tmc_udn_ready;


// Return true if a hardwall is "available".
//
// Analyzes the "initial" value of "TMC_UDN_INFO" exactly once, to
// determine if an inherited hardwall is available.
//
static bool
__tmc_udn_available(void)
{
  static bool done;

  if (!done)
  {
    // Note that getenv/unsetenv are locked internally by libc, so
    // we don't need to provide any explicit locking here.
    // And note that even if two threads race to set up "done",
    // they will both be setting the same values.
    //
    const char* info = getenv("TMC_UDN_INFO");

    uint i1, i2, i3, i4, i5;
    if (info != NULL &&
        sscanf(info, "0:%u:%u:%u:%u:%u", &i1, &i2, &i3, &i4, &i5) == 5)
    {
      __tmc_udn_x = i1;
      __tmc_udn_y = i2;
      __tmc_udn_w = i3;
      __tmc_udn_h = i4;
      __tmc_udn_fd = i5;
    }
    else if (info != NULL)
    {
      // HACK: Clear "broken" info.
      unsetenv("TMC_UDN_INFO");
    }

    // Fence here so that another thread is guaranteed to find the
    // correct values in the __tmc_udn_xxx variables if it sees that
    // "done" is true.
    tmc_mem_fence();

    done = true;
  }

  return (__tmc_udn_fd >= 0);
}


static int
tmc_udn_init_aux(cpu_set_t* cpus)
{
  // Convert NULL to "current affinity".
  cpu_set_t affinity;
  if (cpus == NULL)
  {
    cpus = &affinity;
    if (tmc_cpus_get_my_affinity(cpus) != 0)
      return -1;
  }

  // Analyze the bounds.
  uint x, y, w, h;
  if (tmc_cpus_grid_bounding_rect(cpus, &x, &y, &w, &h) != 0)
    return -1;

  if (__tmc_udn_available())
  {
    // Verify.
    if (x < __tmc_udn_x ||
        y < __tmc_udn_y ||
        x + w > __tmc_udn_x + __tmc_udn_w ||
        y + h > __tmc_udn_y + __tmc_udn_h)
    {
      errno = ENODATA;
      return -1;
    }

    // Already initialized.
    if (__tmc_udn_ready)
    {
      errno = EINVAL;
      return 1;
    }

    // Inherit persisted hardwall.
  }
  else
  {
    // Open the special hardwall file.
    int fd = open("/dev/hardwall/udn", O_RDONLY, 0);
    if (fd < 0)
      return -1;

    // Create a hardwall of the proper size.
    cpu_set_t rectangle;
    tmc_cpus_clear(&rectangle);
    tmc_cpus_grid_add_rect(&rectangle, x, y, w, h);
    if (ioctl(fd, HARDWALL_CREATE(sizeof(rectangle)), &rectangle) < 0)
    {
      close(fd);
      return -1;
    }

    // Save the info.
    __tmc_udn_x = x;
    __tmc_udn_y = y;
    __tmc_udn_w = w;
    __tmc_udn_h = h;

    // Save the fd.
    __tmc_udn_fd = fd;
  }

  // Implicit "persist(false)".
  (void)set_close_on_exec(__tmc_udn_fd, true);
  unsetenv("TMC_UDN_INFO");

  // Fence here so all possible data we've written is visible
  // before we set "ready", since tmc_udn_init() does not take
  // the lock prior to examining __tmc_udn_ready.
  tmc_mem_fence();

  __tmc_udn_ready = true;

  return 0;
}

int
tmc_udn_init(cpu_set_t* cpus)
{
  if (__tmc_udn_ready)
  {
    errno = EINVAL;
    return 1;
  }

  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  PTHREAD_MUTEX_DEFINE(init_lock);
  PTHREAD_MUTEX_LOCK(&init_lock);

  int retval = tmc_udn_init_aux(cpus);

  PTHREAD_MUTEX_UNLOCK(&init_lock);

  return retval;
}

int
tmc_udn_close(void)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (!__tmc_udn_available())
  {
    errno = ENODATA;
    return 1;
  }

  (void)close(__tmc_udn_fd);

  __tmc_udn_fd = -1;

  unsetenv("TMC_UDN_INFO");

  __tmc_udn_ready = false;

  return 0;
}


int
tmc_udn_activate(void)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (!__tmc_udn_available() || !__tmc_udn_ready)
  {
    errno = ENODATA;
    return -1;
  }

  if (ioctl(__tmc_udn_fd, HARDWALL_ACTIVATE) < 0)
    return -1;

  return 0;
}


int
tmc_udn_persist_after_exec(int flag)
{
  // HACK: Handle non-linux.
  if (!__tmc_task_is_linux())
    return 0;

  if (flag < 0)
  {
    errno = EINVAL;
    return -1;
  }

  if (!__tmc_udn_available())
  {
    errno = ENODATA;
    return -1;
  }

  int old = (getenv("TMC_UDN_INFO") != NULL);
  if (old == flag)
    return old;

  (void)set_close_on_exec(__tmc_udn_fd, !flag);

  // Note that setenv/unsetenv are locked internally by libc, so
  // we don't need to provide any explicit locking here.
  if (flag)
  {
    char info[256];
    snprintf(info, sizeof(info),
             "0:%u:%u:%u:%u:%u",
             __tmc_udn_x,
             __tmc_udn_y,
             __tmc_udn_w,
             __tmc_udn_h,
             __tmc_udn_fd);
    (void)setenv("TMC_UDN_INFO", info, 1);
  }
  else
  {
    unsetenv("TMC_UDN_INFO");
  }

  return old;
}



DynamicHeader
tmc_udn_header_from_cpu(int cpu)
{
  unsigned int x = 0, y = 0;
  tmc_cpus_grid_cpu_to_tile(cpu, &x, &y);
  DynamicHeader header = INIT_DYNAMIC_HEADER(x, y, 0, 0);
  return header;
}
