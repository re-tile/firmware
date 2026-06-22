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
// The malloc replacement library using tmc_mspace.

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <tmc/malloc.h>
#include <tmc/mspace.h>
#include <tmc/alloc.h>
#include <tmc/cpus.h>
#include <tmc/task.h>

// Provide easy macros to create aliases.
// These are overkill for newlib, which only has the "primary" names
// (e.g. "malloc", etc.) but should be harmless.
#define weak_alias(name, aliasname) _weak_alias (name, aliasname)
#define _weak_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((weak, alias (#name)));
#define strong_alias(name, aliasname) _strong_alias (name, aliasname)
#define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));

// Pull in pthread support, but tag the functions we use as "weak",
// so we can dynamically discover if they are linked in.
#ifndef __NEWLIB__
#include <pthread.h>
__typeof__(pthread_setspecific) pthread_setspecific __attribute__((weak));
__typeof__(pthread_key_create) pthread_key_create __attribute__((weak));
#endif

// Private APIs used in mspace.c.
extern void tmc_mspace_mark_free(tmc_mspace msp, int cpu);
extern tmc_mspace tmc_mspace_unmark_free(int cpu);

// This API could in principle be exposed in <tmc/mspace.h>, but it's
// such poor practice to use that I'm not in a hurry to expose it.
// We provide it here just to support glibc.
extern size_t tmc_mspace_usable_size(void* mem);

// Allocator to use when creating new heaps.
static tmc_alloc_t alloc = TMC_ALLOC_INIT;

// Global mspace (only valid if !per_thread).
static tmc_mspace global_mspace;

// Whether we have called tmc_malloc_init() yet.
// We use the same name as the equivalent symbol in libc.
int __libc_malloc_initialized = 0;

#ifdef __NEWLIB__
// Newlib never has threads.
#define per_thread 0
#else
// Are we doing per-thread heaps?
static bool per_thread;

// Current per-thread mspace.
static __thread tmc_mspace thread_mspace;

// TLS key to trigger destructor to free mspace.
static pthread_key_t mspace_key;

// Forward reference.
static void free_thread_mspace(void* arg);
#endif

// Satisfy a user request to force the use of this library.
void
tmc_malloc_require_library(void)
{
  /* Nothing */
}

// Initialize the overall state, but don't set up any mspaces yet.
// This is called lazily the first time we create an mspace.
//
static tmc_mspace
tmc_malloc_init()
{
  // If we're already initialized, do nothing.
  if (__libc_malloc_initialized)
    return global_mspace;
  __libc_malloc_initialized = 1;

  // Initialize the allocator for huge pages if needed.
  if (getenv("HUGETLB_MORECORE"))
  {
    // If "hugectl --heap" is in effect for this application, honor
    // it by using huge pages by default.  The environment variable
    // is either "yes" (if no "size" was specified for "hugectl --heap")
    // or the actual size argument; we don't care and just enable
    // huge pages if the environment variable is set at all.
    tmc_alloc_set_huge(&alloc);
  }

#ifndef __NEWLIB__
  // Decide if we're doing per-thread heaps.  If libpthread isn't
  // loaded when we start up, we won't be able to do per-thread
  // heaps, since we can't readily re-resolve the pthread_key_create()
  // and pthread_setspecific() symbols.  So if we can't find them
  // now, we just switch over to using a single global heap.
  if (pthread_key_create == NULL)
    per_thread = false;
  else
    per_thread = !tmc_alloc_get_huge(&alloc);

  if (per_thread)
  {
    int err = pthread_key_create(&mspace_key, free_thread_mspace);
    if (err)
      tmc_task_die("tmc_malloc_init: can't create TLS: %d", err);
  }
#endif

#ifdef __tile__
  // Initialize the allocator for hash-for-home or heap-follows-task.
  if (!tmc_alloc_get_hash_default() && per_thread)
    tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_TASK);
#endif

  // Set up the global mspace if we're not doing per-thread ones.
  if (!per_thread)
    global_mspace = tmc_mspace_create_special(0, TMC_MSPACE_LOCKED, &alloc);
  
  return global_mspace;
}

#ifndef __NEWLIB__

// Allocate a suitable mspace for this thread.
static tmc_mspace
alloc_thread_mspace(void)
{
  // If we're not initialized yet, go do it.
  if (!__libc_malloc_initialized)
    (void) tmc_malloc_init();

  // Share the global mspace if so requested.
  if (!per_thread)
  {
    thread_mspace = global_mspace;
    return global_mspace;
  }

  // See if there's a "free" mspace that's already allocated,
  // ideally last used on this cpu.
  tmc_mspace msp = tmc_mspace_unmark_free(tmc_cpus_get_my_current_cpu());

  // If we didn't find a free one, allocate a new one.
  if (!msp)
    msp = tmc_mspace_create_special(0, TMC_MSPACE_LOCKED, &alloc);

  // Assign this before calling pthread_setspecific() in case it calls
  // back into malloc.
  thread_mspace = msp;

  // Associate mspace with this thread, so the "free_thread_mspace"
  // destructor will fire properly when this thread exits.
  pthread_setspecific(mspace_key, msp);

  return msp;
}

// Invoked by the destructor for the TSD mspace_key.
// We mark this mspace as no longer in active use (though it may well
// contain allocated data that other threads are still using).
// This thread could, in principle, still allocate from it even
// though it's been tagged as free, but we don't expect this to happen,
// since we're just running destructors now.
static void 
free_thread_mspace(void *msp)
{
  tmc_mspace_mark_free(msp, tmc_cpus_get_my_current_cpu());
}

static inline tmc_mspace
get_mspace(void)
{
  tmc_mspace msp = thread_mspace;
  if (__builtin_expect(msp == NULL, 0))
    msp = alloc_thread_mspace();
  return msp;
}

#else

static inline tmc_mspace
get_mspace(void)
{
  tmc_mspace msp = global_mspace;
  if (__builtin_expect(msp == NULL, 0))
    msp = tmc_malloc_init();
  return msp;
}

#endif

void *
__malloc(size_t size)
{
  return tmc_mspace_malloc(get_mspace(), size);
}
strong_alias(__malloc, __libc_malloc)
weak_alias(__malloc, malloc)

void *
__calloc(size_t nmemb, size_t size)
{
  return tmc_mspace_calloc(get_mspace(), nmemb, size);
}
strong_alias(__calloc, __libc_calloc)
weak_alias(__calloc, calloc)

void *
__realloc(void *ptr, size_t size)
{
  return tmc_mspace_realloc(get_mspace(), ptr, size);
}
strong_alias(__realloc, __libc_realloc)
strong_alias(__realloc, realloc)

void
__free(void *ptr)
{
  return tmc_mspace_free(ptr);
}
strong_alias(__free, __cfree)
strong_alias(__free, __libc_free)
strong_alias(__free, free)
weak_alias(__free, cfree)

void *
__memalign(size_t alignment, size_t size)
{
  return tmc_mspace_memalign(get_mspace(), alignment, size);
}
strong_alias(__memalign, __libc_memalign)
weak_alias(__memalign, memalign)

int
__posix_memalign(void **memptr, size_t alignment, size_t size)
{
  int old_errno = errno;
  void* ptr = tmc_mspace_memalign(get_mspace(), alignment, size);
  if (ptr)
  {
    *memptr = ptr;
    return 0;
  }
  int err = errno;
  errno = old_errno;
  return err;
}
weak_alias(__posix_memalign, posix_memalign)

void *
__valloc(size_t size)
{
  return tmc_mspace_memalign(get_mspace(), getpagesize(), size);
}
strong_alias(__valloc, __libc_valloc)
weak_alias(__valloc, valloc)

void *
__pvalloc(size_t size)
{
  size = (size + getpagesize() - 1) & -getpagesize();
  return tmc_mspace_memalign(get_mspace(), getpagesize(), size);
}
strong_alias(__pvalloc, __libc_pvalloc)
weak_alias(__pvalloc, pvalloc)

struct mallinfo
__mallinfo(void)
{
  return tmc_mspace_mallinfo(get_mspace());
}
strong_alias(__mallinfo, __libc_mallinfo)
weak_alias(__mallinfo, mallinfo)

int
__mallopt(int param, int val)
{
  // Only two options are supported by mspace malloc.
  // Note that these are global options, not per-mspace.
  switch (param)
  {
  case M_TRIM_THRESHOLD:
  case M_MMAP_THRESHOLD:
    return tmc_mspace_mallopt(param, val);
  default:
    return 0;
  }
}
strong_alias(__mallopt, __libc_mallopt)
weak_alias(__mallopt, mallopt)

int
__malloc_trim(size_t pad)
{
  return tmc_mspace_trim(get_mspace(), pad);
}
weak_alias(__malloc_trim, malloc_trim)

size_t
__malloc_usable_size(void *ptr)
{
  return tmc_mspace_usable_size(ptr);
}
weak_alias(__malloc_usable_size, malloc_usable_size)

// Much simpler than the glibc version.
void
__malloc_stats(void)
{
  size_t maxfp, fp, used;
  tmc_mspace_statistics(get_mspace(), &maxfp, &fp, &used);
  fprintf(stderr, "max system bytes = %10zu\n", maxfp);
  fprintf(stderr, "system bytes     = %10zu\n", fp);
  fprintf(stderr, "in use bytes     = %10zu\n", used);
}
weak_alias(__malloc_stats, malloc_stats)

// No support for printing malloc information like glibc does.
int
malloc_info(int options, FILE *fp)
{
  return EINVAL;
}

// No support for capturing the state of the malloc arena.
void *
__malloc_get_state(void)
{
  return NULL;
}
weak_alias(__malloc_get_state, malloc_get_state)

// No support for restoring the state of the malloc arena.
int
__malloc_set_state(void *ptr)
{
  return 0;
}
weak_alias(__malloc_set_state, malloc_set_state)

#ifndef __NEWLIB__
// Allow link warnings.
#define link_warning(symbol, msg) \
  asm(".section .gnu.warning." #symbol "\n\t.previous");        \
  static const char __evoke_link_warning_##symbol[]     \
    __attribute__ ((used, section (".gnu.warning." #symbol "\n\t#"))) \
    = msg;

// We don't support turning on malloc checking in "mspace mode",
// since we don't have the tests for __malloc_hook, etc.
// Rather than silently failing, we mark this symbol as problematic.
void __malloc_check_init() {}
link_warning(__malloc_check_init, \
  "the -ltmc_malloc library does not support __malloc_check_init.")

// We don't use sbrk(), so using __morecore is a bad sign.
__typeof(__morecore) __morecore = 0;
link_warning(__morecore, \
  "the -ltmc_malloc library does not support setting __morecore.")
#endif
