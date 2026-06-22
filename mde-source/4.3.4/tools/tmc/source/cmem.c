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

//! Ensure we can handle large files.
#define _FILE_OFFSET_BITS 64

// HACK: Include some handy stuff.
#include "handy.c"

#include <limits.h>
#include <tmc/cmem.h>
#include <tmc/shmem.h>
#include <tmc/alloc.h>
#include <tmc/mspace.h>

#include "shmem_internal.h"


//! List of maps.
static tmc_shmem_t* s_maps;

//! Lock for modifying the list of maps.
PTHREAD_MUTEX_DEFINE(s_maps_lock);

//! Capacity argument from tmc_cmem_init(), or zero if not yet initialized.
static size_t s_length;

//! Address to use for next arena start address.
static void* s_next_base;

//! The "default" shmem (small pages, no special attributes).
static tmc_shmem_t* s_default_shmem;

//! See "__tmc_cmem_get_mspace()".
static tmc_mspace s_msp;

//! PID that initialized this session (compatibility).
static pid_t s_pid;

//! Lock for setting up and tearing down cmem (guards s_length and s_msp).
PTHREAD_MUTEX_DEFINE(s_init_lock);


static void
add_new_shmem(tmc_shmem_t* shmem)
{
  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  shmem->next = s_maps;
  s_maps = shmem;
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
}


static unsigned long
mmap_mask_flags(unsigned long mmap_flags)
{
#ifdef __tile__
  // The mmap_flags are somewhat tricky to validate, because the way we
  // specify UNCACHED mappings includes the NO_LOCAL flag bits, but for
  // any other home, the NO_LOCAL bits are just extra decoration.
  unsigned long mask = _MAP_CACHE_MKHOME(_MAP_CACHE_HOME_MASK);
  if ((mmap_flags & _MAP_CACHE_MKHOME(_MAP_CACHE_HOME_MASK)) ==
      MAP_CACHE_HOME_NONE)
  {
    if ((mmap_flags & MAP_CACHE_NO_LOCAL) == MAP_CACHE_NO_LOCAL)
      mask |= MAP_CACHE_NO_LOCAL;
    else
      mask |= _MAP_CACHE_INCOHERENT;
  }
  return mask;
#else
  return 0;
#endif
}


static unsigned long
nohome_mmap_flags(unsigned long mmap_flags)
{
  // All of these flags are ignored by shmem.
  unsigned long mask =
    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | \
    MAP_GROWSDOWN | MAP_EXECUTABLE | MAP_DENYWRITE;

  // Add any homecache flags to ignore.
  mask |= mmap_mask_flags(mmap_flags);

  return (mmap_flags & ~mask) | MAP_SHARED | MAP_NORESERVE;
}

static void
set_fd_persist(tmc_shmem_t* shmem, int flag)
{
  int arg = flag ? 0 : FD_CLOEXEC;  // "persist" is the opposite of "cloexec"
  int fd = tmc_shmem_fd(shmem);
  int hugefd = tmc_shmem_hugefd(shmem);

  fcntl(fd, F_SETFD, arg);
  if (hugefd >= 0)
    fcntl(hugefd, F_SETFD, arg);
}

// Prepare a new shmem for this alloc.
static tmc_shmem_t*
new_shmem(const tmc_alloc_t* alloc)
{
  size_t pagesize = tmc_alloc_get_pagesize(alloc);
  s_next_base =
    (void*) (((unsigned long)s_next_base + pagesize - 1) & -pagesize);
  char path[] = "/dev/shm/tmc_cmem_XXXXXX";
  tmc_shmem_t* shmem =
    tmc_shmem_create_temp(path, alloc, s_next_base, s_length);
  if (shmem == NULL)
    return NULL;
  s_next_base = tmc_shmem_address(shmem) + tmc_shmem_maximum_size(shmem);
  tmc_shmem_unlink(path);
  add_new_shmem(shmem);
  return shmem;
}


static tmc_shmem_t*
get_shmem(const tmc_alloc_t* alloc_arg)
{
  // Look up to see if we have already registered a shmem that would
  // work with an alloc with the specified flags, etc.  We disregard
  // the homecache flags since that way we can share a single shmem
  // arena with requests for multiple different homes, just setting
  // homes on individual pages with tmc_shmem_set_page_home().
  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  tmc_shmem_t* shmem;
  tmc_alloc_t alloc = *alloc_arg;
  alloc.mmap_flags = nohome_mmap_flags(alloc.mmap_flags);
  for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
  {
    tmc_alloc_t* shmem_alloc = tmc_shmem_alloc(shmem);
    if (alloc.mmap_prot == shmem_alloc->mmap_prot &&
        alloc.mmap_flags == nohome_mmap_flags(shmem_alloc->mmap_flags) &&
        alloc.mbind_policy == shmem_alloc->mbind_policy &&
        alloc.mbind_nodemask == shmem_alloc->mbind_nodemask &&
        alloc.pagesize == shmem_alloc->pagesize)
      break;
  }
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
  if (shmem != NULL)
    return shmem;

  // Fail if cmem hasn't been initialized yet.
  if (s_length == 0 || s_pid != getpid())
  {
    errno = ENODATA;
    return NULL;
  }

  // Fail if we have already persisted cmem (and presumably done a fork/exec).
  if (s_default_shmem->file->persisted)
  {
    errno = EINVAL;
    return NULL;
  }

  return new_shmem(&alloc);
}


int
tmc_cmem_prepare_alloc(const tmc_alloc_t* alloc)
{
  return get_shmem(alloc) == NULL ? -1 : 0;
}


// NOTE: should be tmc_cmem_alloc_mmap_mbind() to more clearly convey
// the purpose of the function, but we are constrained by binary compatibility.
void*
tmc_cmem_mmap_mbind(const tmc_alloc_t* alloc, void* start, size_t length)
{
  if (start != NULL || length == 0)
  {
    errno = EINVAL;
    return NULL;
  }
  tmc_shmem_t* shmem = get_shmem(alloc);
  if (shmem == NULL)
    return NULL;
  void* result;
#ifdef __tile__
  if (alloc->mmap_flags & _MAP_CACHE_HOME)
  {
    int populate = shmem->alloc.mmap_flags & MAP_POPULATE;
    if (populate)
      shmem->alloc.mmap_flags &= ~MAP_POPULATE;
    result = tmc_shmem_grow(shmem, length);
    if (populate)
      shmem->alloc.mmap_flags |= MAP_POPULATE;
    int home = tmc_alloc_get_home(alloc);
    if (tmc_shmem_set_page_home(shmem, result, length, home) < 0)
      result = NULL;
  }
  else
#endif
    result = tmc_shmem_grow(shmem, length);
  return result;
}


tmc_alloc_t*
tmc_cmem_alloc_init(tmc_alloc_t* alloc)
{
  tmc_alloc_init(alloc);
  alloc->mmap_mbind_func = tmc_cmem_mmap_mbind;

  return alloc;
}


size_t
tmc_cmem_get_capacity(void)
{
  size_t capacity = 0;
  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  tmc_shmem_t* shmem;
  for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
    capacity += tmc_shmem_maximum_size(shmem);
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
  return capacity;
}


size_t
tmc_cmem_get_available(void)
{
  size_t available = 0;
  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  tmc_shmem_t* shmem;
  for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
    available += tmc_shmem_maximum_size(shmem) - tmc_shmem_current_size(shmem);
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
  return available;
}


// Compatibility API.  Do not use.
int
tmc_cmem_detach(void)
{
  if (s_length == 0 || s_pid != getpid())
  {
    errno = ENODATA;
    return -1;
  }

  s_pid = 0;

  return 0;
}


static void
close_all_shmems(void)
{
  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  tmc_shmem_t* shmem = s_maps;
  s_maps = NULL;
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);

  while (shmem)
  {
    tmc_shmem_t* next = shmem->next;
    tmc_shmem_close(shmem);
    shmem = next;
  }
}


// Parse the TMC_CMEM_INFO environment variable to extract the
// s_length value, plus fds for each shmem region, and open all
// the fds with the internal tmc_shmem_open_fds() API.
static bool
recreate_cmem_state(void)
{
  tmc_mspace msp = NULL;
  tmc_shmem_t* default_shmem = NULL;

  char* info = getenv("TMC_CMEM_INFO");
  if (info == NULL)
    return false;

  char* end;
  size_t length = strtoul(info, &end, 0);
  if (length == 0)
    return false;

  while (*end)
  {
    if (*end++ != ':')
      goto fail;
    char* p = end;
    int fd = strtol(p, &end, 0);
    if (*end != ',')
      goto fail;
    p = ++end;
    int hugefd = strtol(p, &end, 0);
    tmc_shmem_t* shmem = tmc_shmem_open_fds(fd, hugefd);
    if (shmem == NULL)
      goto fail;
    add_new_shmem(shmem);
    if (shmem->file->mspace)
      msp = shmem->file->mspace;

    // The default (first) shmem is last on this list.
    default_shmem = shmem;
  }

  // Fence here so that another thread is guaranteed to see the
  // correct fields of "s_msp" if it can see "s_msp" itself.
  __sync_synchronize();

  s_default_shmem = default_shmem;
  s_msp = msp;
  s_length = length;

  return true;

 fail:
  close_all_shmems();
  return false;
}


int
tmc_cmem_init(size_t capacity)
{
  int rc = -1;

  if (capacity == 0)
    capacity = TMC_CMEM_DEFAULT_CAPACITY;

  PTHREAD_MUTEX_LOCK(&s_init_lock);

  // Check if cmem already initialized, and fail if so.
  // The same behavior is fine for a forked child process.
  if (s_length)
  {
    if (capacity > s_length)
      errno = ENODATA;
    else
    {
      errno = EINVAL;
      rc = (s_pid == getpid()) ? 1 : 0;
      s_pid = getpid();
    }
    goto done;
  }

  // See if there is a persisted cmem that we should join.
  if (recreate_cmem_state())
  {
    if (capacity > s_length)
      errno = ENODATA;
    else
    {
      s_pid = getpid();
      rc = 0;
    }
    goto done;
  }

  // Initialize for a new cmem region.
  unsetenv("TMC_CMEM_INFO");

  // Set s_next_base appropriately.
  char *base = getenv("TMC_CMEM_START");
  if (base != NULL)
    s_next_base = (void*) strtoul(base, NULL, 0);
  else
  {
#ifdef _LP64
    s_next_base = (void*) 0x200000000;
#else
    s_next_base = (void*) 0x40000000;
#endif
  }
  
  s_length = capacity;

  // Create a default-size arena.
  tmc_alloc_t alloc = TMC_CMEM_ALLOC_INIT;
  alloc.mmap_flags = nohome_mmap_flags(alloc.mmap_flags);
  s_default_shmem = new_shmem(&alloc);
  if (s_default_shmem == NULL)
  {
    s_length = 0;
    goto done;
  }

  s_pid = getpid();
  rc = 0;

 done:
  PTHREAD_MUTEX_UNLOCK(&s_init_lock);
  return rc;
}


int
tmc_cmem_close(void)
{
  if (s_length == 0 || s_pid != getpid())
  {
    errno = ENODATA;
    return 1;
  }

  PTHREAD_MUTEX_LOCK(&s_init_lock);

  close_all_shmems();
  unsetenv("TMC_CMEM_INFO");

  s_msp = NULL;
  s_default_shmem = false;
  s_length = 0;

  PTHREAD_MUTEX_UNLOCK(&s_init_lock);
  return 0;
}


int
tmc_cmem_persist_after_exec(int flag)
{
  tmc_shmem_t* shmem;

  if (flag < 0)
  {
    errno = EINVAL;
    return -1;
  }

  if (s_length == 0)
  {
    errno = ENODATA;
    return -1;
  }

  bool old = s_default_shmem->file->persisted;
  if (flag)
  {
    // Create the TMC_CMEM_INFO environment variable containing the
    // s_length value, plus fds for each shmem region.
    PTHREAD_MUTEX_LOCK(&s_maps_lock);
    int nmaps = 0;
    for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
      ++nmaps;
    char* buf = alloca(10 + (20 * nmaps));
    char* p = buf + sprintf(buf, "%#zx", s_length);
    for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
      p += sprintf(p, ":%d,%d", tmc_shmem_fd(shmem), tmc_shmem_hugefd(shmem));
    PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
    (void)setenv("TMC_CMEM_INFO", buf, 1);
  }
  else
  {
    unsetenv("TMC_CMEM_INFO");
  }

  PTHREAD_MUTEX_LOCK(&s_maps_lock);
  for (shmem = s_maps; shmem != NULL; shmem = shmem->next)
    set_fd_persist(shmem, flag);
  PTHREAD_MUTEX_UNLOCK(&s_maps_lock);
  
  s_default_shmem->file->persisted = flag;

  return old;
}


static tmc_mspace
__tmc_cmem_get_mspace(void)
{
  if (!s_msp)
  {
    if (s_default_shmem == NULL || s_pid != getpid())
      return NULL;

    s_msp = s_default_shmem->file->mspace;

    if (!s_msp)
    {
      // Create default small-page arena for methods below.
      tmc_alloc_t alloc = TMC_CMEM_ALLOC_INIT;
      tmc_mspace msp = tmc_mspace_create_special(0, TMC_MSPACE_LOCKED, &alloc);

      // Fence here so that another thread is guaranteed to see the
      // correct fields of "s_msp" if it can see "s_msp" itself.
      __sync_synchronize();

      // Use compare-and-exchange to update the pointer.
      if (!arch_atomic_bool_compare_and_exchange(&s_default_shmem->file->mspace,
                                                 NULL, msp))
      {
        tmc_mspace_destroy(msp);
        msp = s_default_shmem->file->mspace;
      }

      s_msp = msp;
    }
  }

  return s_msp;
}


void*
tmc_cmem_malloc(size_t size)
{
  tmc_mspace* m = __tmc_cmem_get_mspace();
  return m ? tmc_mspace_malloc(m, size) : NULL;
}


void*
tmc_cmem_calloc(size_t num, size_t size)
{
  tmc_mspace* m = __tmc_cmem_get_mspace();
  return m ? tmc_mspace_calloc(m, num, size) : NULL;
}


void*
tmc_cmem_realloc(void *ptr, size_t size)
{
  tmc_mspace* m = __tmc_cmem_get_mspace();
  return m ? tmc_mspace_realloc(m, ptr, size) : NULL;
}


void*
tmc_cmem_memalign(size_t boundary, size_t size)
{
  tmc_mspace* m = __tmc_cmem_get_mspace();
  return m ? tmc_mspace_memalign(m, boundary, size) : NULL;
}


void
tmc_cmem_free(void* ptr)
{
  tmc_mspace_free(ptr);
}
