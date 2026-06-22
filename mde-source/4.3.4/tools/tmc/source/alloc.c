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
// Core routines for allocating pages of memory.

// HACK: Include some handy stuff.
#include "handy.c"

#include "tmc_internal.h"
#include <tmc/alloc.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/syscall.h>

#include <sys/types.h>
#include <sys/mman.h>

#ifndef __NEWLIB__
#include <dirent.h>
#endif

#ifndef MAP_HUGETLB
# define MAP_HUGETLB 0
#endif

PTHREAD_MUTEX_DEFINE(tmc_alloc_lock);

//! Map memory and, if requested, bind it to a specified memory node.
//!
//! This function takes the arguments of both mmap and mbind, since the
//! MAP_POPULATE flag requires interaction between them.
//!
//! If mbind() fails, the function unmaps the bad mapping before returning.
//!
//! @param start Start of mapping if desired, or NULL if it doesn't matter.
//! @param length Length of mapping requested, in bytes.
//! @param mmap_prot Protection to be used for mapping (PROT_xxx).
//! @param mmap_flags Flags for mmap (MAP_xxx).
//! @param fd File descriptor, if MAP_ANONYMOUS is not passed in mmap_flags.
//! @param offset File offset, if MAP_ANONYMOUS is not passed in mmap_flags.
//! @param mbind_policy Memory node policy for mbind (MPOL_xxx).
//! @param mbind_nodemask Bitmask of memory nodes for mbind.  Note that we only
//!   support as many memory controllers as fit in one unsigned long.
//!
//! @return address of allocated memory, or NULL on error.
//!
void*
tmc_alloc_mmap_mbind(void* start, size_t length, int mmap_prot, int mmap_flags,
                     int fd, unsigned long long offset, int mbind_policy,
                     unsigned long mbind_nodemask)
{
  int err = 0;
  void* result = MAP_FAILED;

  // If we are populating the memory, and using non-default mbind
  // arguments, first set up the mbind arguments as our defaults,
  // saving the current values.  We can't save and restore the "next
  // node" for interleaving, so if the current policy is interleave,
  // we will end up restarting on the first node again.
  //
  int policy = 0;  // happy compiler
  unsigned long nmask = 0;
  const int NR_NODES = sizeof(nmask) * 8;
  int use_mempolicy = ((mmap_flags & MAP_POPULATE) != 0 &&
                       (mbind_policy != __TMC_ALLOC_MPOL_DEFAULT));
  if (use_mempolicy)
  {
    if (syscall(SYS_get_mempolicy,
                &policy, &nmask, NR_NODES, 0, 0) != 0)
    {
      err = errno;
      goto done_close;
    }
    if (syscall(SYS_set_mempolicy,\
                mbind_policy, &mbind_nodemask, NR_NODES) != 0)
    {
      err = errno;
      goto done_mempolicy;
    }
  }

  // Actually try to reserve the memory.
  result = mmap64(start, length, mmap_prot, mmap_flags, fd, offset);
  if (result == MAP_FAILED)
  {
    err = errno;
    goto done_mempolicy;
  }

  // If we're not populating, but have a policy, mbind the memory.
  if (!use_mempolicy && mbind_policy != __TMC_ALLOC_MPOL_DEFAULT)
  {
    if (syscall(SYS_mbind,
                result, length, mbind_policy, &mbind_nodemask, NR_NODES, 0)
        != 0)
    {
      err = errno;
      munmap(result, length);
      result = MAP_FAILED;
    }
  }

 done_mempolicy:
  if (use_mempolicy)
    syscall(SYS_set_mempolicy, policy, &nmask, NR_NODES);

 done_close:
  if (result == MAP_FAILED)
    errno = err;
  return result;
}

static const tmc_alloc_t default_tmc_alloc_t = TMC_ALLOC_INIT;

tmc_alloc_t*
tmc_alloc_init(tmc_alloc_t* alloc)
{
  *alloc = (tmc_alloc_t) TMC_ALLOC_INIT;
  return alloc;
}

#ifdef __tile__
int
tmc_alloc_get_hash_default(void)
{
  static int checked, use_hash;
  if (!checked)
  {
    char* env;
    checked = 1;
    if ((env = getenv(MAP_CACHE_HASH_ENV_VAR)) != NULL && *env)
      use_hash = (strncmp(env, "all", 3) == 0);
    else
      use_hash = (access("/proc/sys/tile/hash_default", F_OK) == 0);
  }
  return use_hash;
}
#endif

void
__tmc_get_huge_file_dir(const tmc_alloc_t* alloc, char* buf)
{
  size_t pagesize = tmc_alloc_get_pagesize(alloc);

  // Assume the standard RHEL mount point for default huge pages.
  if (pagesize == tmc_alloc_get_huge_pagesize())
  {
    strcpy(buf, TMC_ALLOC_DEFAULT_HUGE_MOUNTPOINT);
    return;
  }

  // For now we assume the Tilera defaults for where hugetlbfs is
  // mounted; in principle we could generalize this by examining
  // (e.g.) /proc/self/mounts.
  const char *suffix = "KB";
  size_t num = pagesize / 1024;
  if (num >= 1024)
  {
    num /= 1024;
    suffix = "MB";
    if (num >= 1024)
    {
      num /= 1024;
      suffix = "GB";
    }
  }
  sprintf(buf, "/dev/hugetlbfs/pagesize-%zd%s", num, suffix);
}

void*
tmc_alloc_map_at(const tmc_alloc_t* alloc, void* start, size_t length)
{
  // Provide a default tmc_alloc_t if necessary.
  if (alloc == NULL)
    alloc = &default_tmc_alloc_t;

  // Round up length appropriately.
  size_t pagesize = tmc_alloc_get_pagesize(alloc);
  length = ROUND_UP(length, pagesize);

  // You can't ask for unaligned starting addresses.
  if ((unsigned long) start & (pagesize - 1))
  {
    errno = EINVAL;
    return NULL;
  }

  if (alloc->mmap_mbind_func)
    return alloc->mmap_mbind_func(alloc, start, length);

  // Convert alloc->pagesize to fd and mmap_flags.
  int fd = -1;
  int mmap_flags = alloc->mmap_flags;
  if (pagesize == getpagesize())
  {
    mmap_flags |= MAP_ANONYMOUS;
    mmap_flags &= ~MAP_HUGETLB;
  }
#if MAP_HUGETLB
  else if (pagesize == tmc_alloc_get_huge_pagesize())
  {
    mmap_flags |= MAP_ANONYMOUS | MAP_HUGETLB;
  }
#endif
  else
  {
    // Check that the page size is a valid power of two.
    if (pagesize & (pagesize - 1))
    {
      errno = EINVAL;
      return NULL;
    }

    char buf[100];
    __tmc_get_huge_file_dir(alloc, buf);
    strcat(buf, "/tmc_XXXXXX");
    fd = mkstemp(buf);
    if (fd < 0)
      return NULL;
    unlink(buf);
    mmap_flags &= ~(MAP_ANONYMOUS | MAP_HUGETLB);
  }

  // Adjust mmap_flags since user may have under-specified them.
  if ((mmap_flags & (MAP_PRIVATE|MAP_SHARED)) == 0)
    mmap_flags |= MAP_PRIVATE;

  // Try to perform the requested mapping.
  void* result = tmc_alloc_mmap_mbind(start, length, alloc->mmap_prot,
                                      mmap_flags, fd, 0, alloc->mbind_policy,
                                      alloc->mbind_nodemask);
  if (fd >= 0)
    close(fd);
  if (result == MAP_FAILED)
    return NULL;

  // If the mapping was meant to be at a particular address and the
  // OS couldn't put it there, unmap it and return an error.
  if (start != NULL && result != start)
  {
    munmap(result, length);
    errno = EINVAL;
    return NULL;
  }

  return result;
}
strong_hidden_alias(tmc_alloc_map_at, tmc_alloc_map_at_internal)

// Set of function calls for unmap/remap.
//
struct tmc_alloc_region
{
  struct tmc_alloc_region* next;   // next region on list
  unsigned long start;             // starting address of region
  unsigned long end;               // first byte above region
  tmc_alloc_t* alloc;              // alloc object for this region
  tmc_alloc_munmap_func_t unmap;   // function to do unmaps within the region
  tmc_alloc_mremap_func_t remap;   // function to do remaps within the region
};
static struct tmc_alloc_region default_region;

// Register a new region description.
// When shutting down, you may call again with NULL function pointers
// and the region will be unregistered.
// Return 0 for success, or -1 for failure and set errno.
// NOTE: this API is more likely to change than the documented public APIs.
//
int
tmc_alloc_register_address_range(tmc_alloc_t* alloc,
                                 void* start_ptr, size_t length,
                                 tmc_alloc_munmap_func_t unmap,
                                 tmc_alloc_mremap_func_t remap)
{
  unsigned long start = (unsigned long) start_ptr;
  unsigned long end = start + length;

  if (end < start)
  {
    // Bad length caused a wraparound.
    errno = EINVAL;
    return -1;
  }

  int retval = 0;
  PTHREAD_MUTEX_LOCK(&tmc_alloc_lock);
  struct tmc_alloc_region* prev;
  struct tmc_alloc_region* r;
  for (prev = &default_region, r = default_region.next;
       r != NULL;
       prev = r, r = r->next)
  {
    // If matching pointers, delete or update.
    if (r->start == start && r->end == end)
    {
      if (alloc == NULL)
      {
        prev->next = r->next;
        free(r);
      }
      else
      {
        r->alloc = alloc;
      }
      goto done;
    }

    // If overlapping regions, fail.
    if (start < r->end && end > r->start)
    {
      errno = EINVAL;
      retval = -1;
      goto done;
    }

    // Insert here if appropriate.
    if (start < r->start)
      break;
  }

  struct tmc_alloc_region* n = malloc(sizeof(struct tmc_alloc_region));
  prev->next = n;
  n->next = r;
  n->start = start;
  n->end = end;
  n->alloc = alloc;
  n->unmap = unmap;
  n->remap = remap;

 done:
  PTHREAD_MUTEX_UNLOCK(&tmc_alloc_lock);
  return retval;
}

// Return a region descriptor, or NULL if the passed range isn't
// within a single region.  For the default tmc_alloc_mmap_mbind() region,
// we return a pointer to a default tmc_alloc_region with default
// munmap/mremap function pointers, to keep the API simple.
//
// We don't take out the lock here since if we are racing with
// the client claiming it won't handle an address range any more,
// we are in bigger trouble than just having a race as to whether
// we return the client's unmap or the system munmap().
//
static struct tmc_alloc_region*
tmc_alloc_get_region(void* start_ptr, size_t length)
{
  struct tmc_alloc_region* r = default_region.next;

  // Optimize for common case without taking the lock.
  if (r == NULL)
    return &default_region;

  unsigned long start = (unsigned long) start_ptr;
  unsigned long end = start + length;
  PTHREAD_MUTEX_LOCK(&tmc_alloc_lock);
  for (; ; r = r->next)
  {
    // See if we've walked off the list, or the remaining items are above us.
    if (r == NULL || end <= r->start)
    {
      r = &default_region;
      break;
    }

    // See if the request is contained by this region.
    if (start >= r->start && end <= r->end)
      break;

    // See if the request overlaps with this region, which is an error.
    if (start < r->end)
    {
      r = NULL;
      break;
    }
  }

  // We drop the lock here even though in principle another thread could
  // unregister the region we are pointing to; but if such a race exists,
  // we are in trouble anyway, so we don't worry about it.
  PTHREAD_MUTEX_UNLOCK(&tmc_alloc_lock);

  return r;
}

int
tmc_alloc_unmap(void* start, size_t length)
{
  struct tmc_alloc_region *r = tmc_alloc_get_region(start, length);
  if (start == NULL || r == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  tmc_alloc_t* alloc = r->alloc;
  if (alloc)
    return r->unmap(alloc, start, length);
  else
    return munmap(start, length);
}
strong_hidden_alias(tmc_alloc_unmap, tmc_alloc_unmap_internal)

void*
tmc_alloc_remap(void *addr, size_t old_len, size_t new_len, int flags, ...)
{
  struct tmc_alloc_region *r = tmc_alloc_get_region(addr, old_len);
  if (addr == NULL || r == NULL)
  {
    errno = EINVAL;
    return MAP_FAILED;
  }

  void* new_addr = 0;  // happy compiler
  if (flags & MREMAP_FIXED)
  {
    va_list ap;
    va_start(ap, flags);
    new_addr = va_arg(ap, void*);
    va_end(ap);
    if (new_addr == NULL || tmc_alloc_get_region(new_addr, new_len) != r)
    {
      errno = EINVAL;
      return MAP_FAILED;
    }
  }

  tmc_alloc_t* alloc = r->alloc;
  if (alloc)
    return r->remap(alloc, addr, old_len, new_len, flags, new_addr);
  else
    return mremap(addr, old_len, new_len, flags, new_addr);
}
strong_hidden_alias(tmc_alloc_remap, tmc_alloc_remap_internal)

unsigned long
tmc_alloc_get_available_nodes(void)
{
  static unsigned long available;

  if (!available)
  {
    int fd = open("/sys/devices/system/node/online", O_RDONLY);
    if (fd < 0)
      return 0;

    // Read in the contents of the file.
    char buf[1024];
    int rc = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (rc < 0)
      return 0;
    buf[rc] = '\0';

    // Parse kernel's bitmap_scnlistprintf() format, which
    // is comma-separated "n" or "n-m" inclusive ranges.
    unsigned long mask = 0;
    char* p = buf;
    while (*p != '\0' && *p != '\n')
    {
      char* q;
      int start = strtoul(p, &q, 10);
      if (p == q)
      {
        errno = EINVAL;
        return 0;
      }
      int end;
      if (*q == '-')
      {
        p = q+1;
        end = strtoul(p, &q, 10);
        if (p == q)
        {
          errno = EINVAL;
          return 0;
        }
      }
      else
      {
        end = start;
      }
      p = q;
      if (*p == ',')
        ++p;
      for (int i = start; i <= end; ++i)
        mask |= (1UL << i);
    }      

    if (mask == 0)
    {
      errno = EINVAL;
      return 0;
    }

    available = mask;
  }

  return available;
}

size_t
tmc_alloc_get_huge_pagesize(void)
{
  static size_t huge_pagesize;

  if (__builtin_expect(huge_pagesize != 0, 1))
    return huge_pagesize;

  char buf[4096];
  int fd = open("/proc/meminfo", O_RDONLY);
  if (fd < 0)
    punt_with_errno("tmc_alloc_get_huge_pagesize: open /proc/meminfo");
  int rc = read(fd, buf, sizeof(buf)-1);
  close(fd);
  if (rc < 0)
    punt_with_errno("tmc_alloc_get_huge_pagesize: read /proc/meminfo");
  buf[rc] = '\0';
  char *p = strstr(buf, "Hugepagesize:");
  if (p == NULL)
    punt("tmc_alloc_get_huge_pagesize: no Hugepagesize in /proc/meminfo");
  p += strlen("Hugepagesize:");
  while (*p == ' ')
    ++p;
  size_t val = 0;
  while (*p >= '0' && *p <= '9')
    val = (val * 10) + (*p++ - '0');
  huge_pagesize = val * 1024;
  return huge_pagesize;
}

/* Read /sys to find hugepages directories, and return their sizes. */
unsigned long
tmc_alloc_get_pagesizes(void)
{
  static unsigned long pagesizes;
  if (pagesizes == 0)
  {
    /* Start with the basic page sizes for completeness. */
    unsigned long ps = getpagesize() | tmc_alloc_get_huge_pagesize();

#ifndef __NEWLIB__
    DIR *d = opendir("/sys/kernel/mm/hugepages");
    if (d)
    {
      struct dirent *ent;
      while ((ent = readdir(d)) != NULL)
      {
        if (strncmp(ent->d_name, "hugepages-", strlen("hugepages-")) != 0)
          continue;
        char *base = ent->d_name + strlen("hugepages-");
        char *suffix;
        unsigned long num = strtoul(base, &suffix, 10);
        if (suffix == base || strcmp(suffix, "kB") != 0 ||
            (num & (num - 1)) != 0)
          continue;
        ps |= (num << 10);
      }
      closedir(d);
    }
#endif

    pagesizes = ps;
  }

  return pagesizes;
}

tmc_alloc_t*
tmc_alloc_set_pagesize_exact(tmc_alloc_t* alloc, size_t pagesize)
{
  if (pagesize == getpagesize())
  {
    return tmc_alloc_clear_huge(alloc);
  }
  if (pagesize == tmc_alloc_get_huge_pagesize())
  {
    return tmc_alloc_set_huge(alloc);
  }
  if ((pagesize & (pagesize - 1)) == 0 &&
      (tmc_alloc_get_pagesizes() & pagesize) != 0)
  {
    alloc->mmap_flags |= __TMC_ALLOC_MAP_HUGETLB;
    alloc->pagesize = pagesize;
    return alloc;
  }
  return NULL;
}

tmc_alloc_t*
tmc_alloc_set_pagesize(tmc_alloc_t* alloc, size_t size)
{
  unsigned long pagesize, mask = tmc_alloc_get_pagesizes();
  do
  {
    pagesize = 1UL << __builtin_ctzl(mask);
    if (size <= pagesize)
      return tmc_alloc_set_pagesize_exact(alloc, pagesize);
    mask &= ~pagesize;
  } while (mask);

  /* Use the largest pagesize. */
  tmc_alloc_set_pagesize_exact(alloc, pagesize);
  return NULL;
}
