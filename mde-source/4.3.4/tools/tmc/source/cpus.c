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

// Allow use of "sched_setaffinity()" and "sched_getaffinity()".
#define _GNU_SOURCE 1


#include <tmc/cpus.h>
#include <tmc/task.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sched.h>

#ifdef SHARED
#define __weak __attribute__((weak))
#else
#define __weak
#endif

void
tmc_cpus_clear(cpu_set_t* s)
{
  CPU_ZERO(s);
}


int
tmc_cpus_add_cpu(cpu_set_t* s, unsigned int cpu)
{
  if (cpu >= TMC_CPUS_MAX_COUNT)
  {
    errno = EINVAL;
    return -1;
  }
  CPU_SET(cpu, s);
  return 0;
}


int
tmc_cpus_remove_cpu(cpu_set_t* s, unsigned int cpu)
{
  if (cpu >= TMC_CPUS_MAX_COUNT)
  {
    errno = EINVAL;
    return -1;
  }
  CPU_CLR(cpu, s);
  return 0;
}


void
tmc_cpus_add_cpus(cpu_set_t* s, const cpu_set_t* cpus)
{
  int i;
  for (i = 0; i < TMC_CPUS_NUM_WORDS; i++)
    s->__bits[i] |= cpus->__bits[i];
}


void
tmc_cpus_remove_cpus(cpu_set_t* s, const cpu_set_t* cpus)
{
  int i;
  for (i = 0; i < TMC_CPUS_NUM_WORDS; i++)
    s->__bits[i] &= ~cpus->__bits[i];
}


void
tmc_cpus_intersect_cpus(cpu_set_t* s, const cpu_set_t* cpus)
{
  int i;
  for (i = 0; i < TMC_CPUS_NUM_WORDS; i++)
    s->__bits[i] &= cpus->__bits[i];
}


int
tmc_cpus_has_cpu(const cpu_set_t* s, unsigned int cpu)
{
  if (cpu >= TMC_CPUS_MAX_COUNT)
    return 0;
  return CPU_ISSET(cpu, s);
}


unsigned int
tmc_cpus_count(const cpu_set_t* s)
{
  unsigned int total = 0;
  for (int i = 0; i < TMC_CPUS_NUM_WORDS; i++)
    total += __builtin_popcountl(s->__bits[i]);
  return total;
}


int
tmc_cpus_find_nth_cpu(const cpu_set_t* s, unsigned int n)
{
  for (int i = 0; i < TMC_CPUS_NUM_WORDS; i++)
  {
    __cpu_mask bits = s->__bits[i];
    while (bits != 0)
    {
      unsigned int bit = __builtin_ctzl(bits);
      if (n-- == 0)
        return i * TMC_CPUS_NUM_BITS + bit;
      bits &= ~(1L << bit);
    }
  }
  return -1;
}


int
tmc_cpus_find_first_cpu(const cpu_set_t* s)
{
  for (int i = 0; i < TMC_CPUS_NUM_WORDS; i++)
  {
    __cpu_mask bits = s->__bits[i];
    if (bits != 0)
      return i * TMC_CPUS_NUM_BITS + __builtin_ctzl(bits);
  }
  return -1;
}


int
tmc_cpus_find_last_cpu(const cpu_set_t* s)
{
  for (int i = TMC_CPUS_NUM_WORDS - 1; i >= 0; i--)
  {
    __cpu_mask bits = s->__bits[i];
    if (bits != 0)
      return
        i * TMC_CPUS_NUM_BITS + (TMC_CPUS_NUM_BITS - 1) - __builtin_clzl(bits);
  }
  return -1;
}



int
tmc_cpus_to_array(const cpu_set_t* s,
                  unsigned int* indices, unsigned int count)
{
  unsigned int size = tmc_cpus_count(s);

  if (size > count)
    return -1;

  unsigned int i = 0;

  for (unsigned int cpu = 0; i < size; cpu++)
  {
    if (tmc_cpus_has_cpu(s, cpu))
    {
      indices[i++] = cpu;
    }
  }

  return i;
}


int
tmc_cpus_from_array(cpu_set_t* s,
                    const unsigned int *indices, unsigned int count)
{
  int result = 0;
  tmc_cpus_clear(s);
  for (unsigned int i = 0; i < count; i++)
  {
    if (indices[i] < TMC_CPUS_MAX_COUNT)
      tmc_cpus_add_cpu(s, indices[i]);
    else
      result = -1;
  }
  return result;
}


int
tmc_cpus_to_string(const cpu_set_t* s, char* string, size_t limit)
{
  if (limit <= 0)
    return -1;

  int comma = 0;
  int pos = 0;

  for (int cpu = 0; cpu < TMC_CPUS_MAX_COUNT; cpu++)
  {
    if (tmc_cpus_has_cpu(s, cpu))
    {
      // We avoid "snprintf()" here since this is used in low-level
      // code that may not want to pull in a large chunk of stdio.

      unsigned int ndig = 1;
      for (unsigned int val = cpu; val >= 10; val /= 10)
        ++ndig;

      if (pos + ndig + comma >= limit)
      {
        string[pos] = '\0';
        return -1;
      }

      if (comma)
        string[pos++] = ',';

      pos += ndig;
      char* p = &string[pos];

      for (unsigned int val = cpu; ndig--; val /= 10)
        *--p = (val % 10) + '0';

      comma = 1;
    }
  }

  string[pos] = '\0';

  return pos;
}


int
tmc_cpus_from_string(cpu_set_t* s,  const char* string)
{
  tmc_cpus_clear(s);

  while (*string != '\0')
  {
    char* next;
    unsigned long i = strtoul(string, &next, 10);
    if (next == string || i >= TMC_CPUS_MAX_COUNT)
      return -1;

    // ISSUE: Require *next be one of ',' ';' ' ' '-' or '\0' ?

    if (*next == '-')
    {
      string = next + 1;
      unsigned long end = strtoul(string, &next, 10);
      if (next == string || end >= TMC_CPUS_MAX_COUNT)
        return -1;
      for (; i <= end; ++i)
        tmc_cpus_add_cpu(s, i);
    }
    else
      tmc_cpus_add_cpu(s, i);

    // Accept semicolon since we used to prefer it here.
    if (*next == ',' || *next == ';')
      string = next + 1;
    else
      string = next;
  }

  return 0;
}



int
tmc_cpus_set_task_affinity(const cpu_set_t* s, pid_t tid)
{
  return sched_setaffinity(tid, sizeof(cpu_set_t), s);
}


int
tmc_cpus_get_task_affinity(cpu_set_t* s, pid_t tid)
{
  return sched_getaffinity(tid, sizeof(cpu_set_t), s);
}


int
tmc_cpus_set_task_cpu(int cpu, pid_t tid)
{
  cpu_set_t set;
  tmc_cpus_clear(&set);
  tmc_cpus_add_cpu(&set, cpu);
  return tmc_cpus_set_task_affinity(&set, tid);
}


int
tmc_cpus_get_task_cpu(pid_t tid)
{
  cpu_set_t set;
  if (tmc_cpus_get_task_affinity(&set, tid) == 0)
  {
    if (tmc_cpus_count(&set) == 1)
      return tmc_cpus_find_first_cpu(&set);
    errno = ERANGE;
  }
  return -1;
}


static int
__tmc_cpus_get_task_current_cpu(pid_t tid)
{
  int cpu = tmc_cpus_get_task_cpu(tid);
  if (cpu >= 0)
    return cpu;

  return __tmc_task_parse_proc_stat(tid, 38);
}


int
tmc_cpus_get_my_current_cpu(void)
{
  extern int sched_getcpu() __weak; /* from libc 2.12 */
#if defined(SHARED) && !defined(__tile__)
  if (!sched_getcpu)
    return __tmc_cpus_get_task_current_cpu(0);
#endif
  return sched_getcpu();
}


int
tmc_cpus_get_task_current_cpu(pid_t tid)
{
  if (tid == 0)
    return tmc_cpus_get_my_current_cpu();
  else
    return __tmc_cpus_get_task_current_cpu(tid);
}


// NOTE: Currently, "/proc/cpuinfo" contains a group of lines for each cpu,
// with the first line in each group having the form "processor\t: CPU\n",
// with CPU being the cpu.
//
// NOTE: We currently do not allow cpu hotplug in the kernel, so all cpus
// are online from boot onward and don't change.  If we ever allow hotplug,
// we would need to make this function dynamic.
//
// ISSUE: Should this just abort on failure?
//
int
tmc_cpus_get_online_cpus(cpu_set_t* s)
{
  static cpu_set_t online;
  static bool done;

  if (!done)
  {
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp == NULL)
      return -1;

    cpu_set_t cpus;
    tmc_cpus_clear(&cpus);

    const char* processor_prefix = "processor\t";
    size_t processor_len = strlen(processor_prefix);
    const char* cpu_list_prefix = "cpu list\t";
    size_t cpu_list_len = strlen(cpu_list_prefix);

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
      if (strncmp(buf, cpu_list_prefix, cpu_list_len) == 0)
      {
        char* colon = strchr(buf, ':');
        if (colon != NULL && tmc_cpus_from_string(&cpus, colon + 1))
          break;
      }
      if (strncmp(buf, processor_prefix, processor_len) == 0)
      {
        char* colon = strchr(buf, ':');
        if (colon != NULL)
        {
          char* end;
          unsigned long cpu = strtoul(colon + 1, &end, 10);
          if (end != buf && *end == '\n')
            tmc_cpus_add_cpu(&cpus, cpu);
        }
      }
    }

    fclose(fp);

    // Copy complete result to static variable, fence so that we're
    // guaranteed it's visible, then set "done".
    //
    online = cpus;
    __sync_synchronize();
    done = true;
  }

  *s = online;
  return 0;
}


// NOTE: "/sys/devices/system/cpu/dataplane" lists the dataplane cpus
// in standard cpu format (comma-separated or with dash-separated ranges).
//
// NOTE: Currently the dataplane can only be set at boot time.  If this
// changes in the future, we will need to make this routine dynamic.
//
// ISSUE: Should this just abort on failure?
//
int
tmc_cpus_get_dataplane_cpus(cpu_set_t* s)
{
  static cpu_set_t dataplane;
  static bool done;

  if (!done)
  {
    int fd = open("/sys/devices/system/cpu/dataplane", O_RDONLY);
    if (fd < 0 && errno == ENOENT)
    {
      // Try the old name for this file; it just lists all the cpus
      // individually, separated by spaces.
      fd = open("/proc/tile/dataplane", O_RDONLY);
    }
    if (fd < 0)
    {
      if (errno == ENOENT)
      {
        // Kernel must have been built without CONFIG_DATAPLANE.
        done = true;
        *s = dataplane;
        return 0;
      }
      return -1;
    }

    char buf[TMC_CPUS_MAX_COUNT * 8];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0)
      return -1;

    if (buf[n-1] == '\n')
      buf[n-1] = '\0';
    else
      buf[n] = '\0';
    cpu_set_t cpus;
    if (tmc_cpus_from_string(&cpus, buf) != 0)
    {
      errno = EINVAL;
      return -1;
    }

    // Copy complete result to static variable, fence so that we're
    // guaranteed it's visible, then set "done".
    //
    dataplane = cpus;
    __sync_synchronize();
    done = true;
  }

  *s = dataplane;
  return 0;
}



void
tmc_cpus_grid_add_all(cpu_set_t* s)
{
  uint count = tmc_cpus_grid_total();

  uint i;
  for (i = 0; i < count; i++)
    tmc_cpus_add_cpu(s, i);
}


int
tmc_cpus_grid_add_rect(cpu_set_t* s, unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h)
{
  unsigned int grid_width = tmc_cpus_grid_width();
  unsigned int grid_height = tmc_cpus_grid_height();

  // NOTE: The second clause catches wraparound.
  if (x >= grid_width || y >= grid_height || 
      w > grid_width || h > grid_height || 
      x + w > grid_width || y + h > grid_height)
  {
    errno = EINVAL;
    return -1;
  }

  unsigned int x1, y1;
  for (y1 = y; y1 < y + h; y1++)
    for (x1 = x; x1 < x + w; x1++)
      tmc_cpus_add_cpu(s, x1 + y1 * grid_width);
  return 0;
}

int
tmc_cpus_grid_remove_rect(cpu_set_t* s, unsigned int x, unsigned int y,
                          unsigned int w, unsigned int h)
{
  unsigned int grid_width = tmc_cpus_grid_width();
  unsigned int grid_height = tmc_cpus_grid_height();

  // NOTE: The second clause catches wraparound.
  if (x >= grid_width || y >= grid_height || 
      w > grid_width || h > grid_height || 
      x + w > grid_width || y + h > grid_height)
  {
    errno = EINVAL;
    return -1;
  }

  unsigned int x1, y1;
  for (y1 = y; y1 < y + h; y1++)
    for (x1 = x; x1 < x + w; x1++)
      tmc_cpus_remove_cpu(s, x1 + y1 * grid_width);
  return 0;
}


int
tmc_cpus_grid_bounding_rect(const cpu_set_t* cpus,
                            unsigned int* xp, unsigned int* yp,
                            unsigned int* wp, unsigned int* hp)
{
  uint width = tmc_cpus_grid_width();
  uint height = tmc_cpus_grid_height();

  uint x1 = width;
  uint x2 = 0;
  uint y1 = height;
  uint y2 = 0;

  for (uint i = 0, y = 0; y < height; y++)
  {
    for (uint x = 0; x < width; x++, i++)
    {
      if (tmc_cpus_has_cpu(cpus, i))
      {
        if (x1 > x)
          x1 = x;
        if (x2 < x)
          x2 = x;
        if (y1 > y)
          y1 = y;
        if (y2 < y)
          y2 = y;
      }
    }
  }

  if (x1 > x2 || y1 > y2)
  {
    errno = EINVAL;
    return -1;
  }

  *xp = x1;
  *yp = y1;
  *wp = (x2 - x1) + 1;
  *hp = (y2 - y1) + 1;

  return 0;
}

// The actual grid width, height, and total tiles.
//
// All of these variables are zero until "tmc_cpus_grid_prepare()" is called.
//
static unsigned int grid_width;
static unsigned int grid_height;
static unsigned int grid_total;


// Initialize grid bounds, or abort.
static int
tmc_cpus_grid_prepare_new(unsigned int *width, unsigned int *height)
{
  char buf[32];
  int fd = open("/sys/devices/system/cpu/chip_width", O_RDONLY);
  int rc = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (rc < 0)
    return -1;
  buf[rc] = '\0';
  *width = strtoul(buf, NULL, 10);

  fd = open("/sys/devices/system/cpu/chip_height", O_RDONLY);
  rc = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (rc < 0)
    return -1;
  buf[rc] = '\0';
  *height = strtoul(buf, NULL, 10);

  return 0;
}

// Note that the old-style /proc file contains "WIDTH\tHEIGHT\n".
static int
tmc_cpus_grid_prepare_old(unsigned int *width, unsigned int *height)
{
  int fd = open("/proc/tile/grid", O_RDONLY);
  if (fd < 0)
    return -1;

  char buf[32];
  int rc = read(fd, buf, sizeof(buf) - 1);
  (void)close(fd);
  if (rc <= 0)
    return -1;
  buf[rc] = '\0';

  char* s1;
  char* s2;

  *width = strtoul(buf, &s1, 10);
  *height = strtoul(s1, &s2, 10);
  if (s1 == s2)
  {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

static int
tmc_cpus_grid_prepare(void)
{
  unsigned int width, height;

  if (tmc_cpus_grid_prepare_new(&width, &height) != 0 &&
      tmc_cpus_grid_prepare_old(&width, &height) != 0)
    return -1;

  if (width >= 1024 || height >= 1024)
  {
    errno = EINVAL;
    return -1;
  }

  grid_width = width;
  grid_height = height;
  grid_total = width * height;
  return 0;
}


int
tmc_cpus_grid_width(void)
{
  if (grid_width == 0 && tmc_cpus_grid_prepare() < 0)
    return -1;
  return grid_width;
}


int
tmc_cpus_grid_height(void)
{
  if (grid_height == 0 && tmc_cpus_grid_prepare() < 0)
    return -1;
  return grid_height;
}


int
tmc_cpus_grid_total(void)
{
  if (grid_total == 0 && tmc_cpus_grid_prepare() < 0)
    return -1;
  return grid_total;
}
