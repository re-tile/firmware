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

//! @file
//!
//! The mspace parameterized heap API.

//! @addtogroup tmc_mspace
//! @{
//!
//! A heap-based memory allocation API that
//! can be used in addition to libc's standard malloc()/free() API.
//! mspaces allow the programmer to create multiple memory allocation
//! heaps, and, if desired, set different memory attributes for each
//! heap.
//!
//! Having multiple memory heaps has several types of advantages for
//! code that can make use of it.
//!
//! First, the Tile Processor&tm; allows explicit control over
//! various aspects of allocated memory, such as which core(s) hold the
//! "home cache" for the memory, from which memory controller(s) the memory
//! should be allocated, and whether or not "huge pages" should be used
//! (to reduce TLB overheads).  The tmc_mspace_create_special() API
//! allows the creation of an mspace that gets its memory from the
//! system using tmc_alloc_map() and a specified tmc_alloc_t.
//!
//! Note that to use tmc_alloc_map(), tmc_alloc_t, and so forth, you must
//! also include <tmc/alloc.h>.
//!
//! For example, to request memory on controller 2, you might do:
//!
//! @code
//! tmc_alloc_t alloc = TMC_ALLOC_INIT;
//! tmc_alloc_set_node_preferred(&alloc, 2);
//! msp = tmc_mspace_create_special(0, 0, &alloc);
//! @endcode
//!
//! Secondly, heaps can provide an efficient way to manage memory
//! allocation in phases.  An application can allocate memory from
//! the heap during the phase, and then release it all at once with
//! a single call to tmc_mspace_destroy().
//!
//! @code
//! msp = tmc_mspace_create(0, 0);
//! p = tmc_mspace_malloc(msp, 100);
//! q = tmc_mspace_malloc(msp, 200);
//! ...
//! tmc_mspace_destroy(msp);
//! @endcode
//!
//! As a general rule, you should use tmc_mspace_create() if you want
//! to allocate a heap with default memory allocation and
//! tmc_mspace_create_special() to control the memory allocation.
//! If you need to provide a pre-allocated region of memory to the
//! allocator, similarly, you should use tmc_mspace_create_with_base()
//! if you want to extend the initial region with memory using
//! the default allocation and tmc_mspace_create_with_base_special()
//! if you want to control the allocation of the additional memory.
//!
//! See the <em>MDE Optimization Guide</em> (UG515) for a
//! discussion of the ucache_hash boot argument and LD_CACHE_HASH
//! environment variable, which allow you to set the defaults for heap
//! allocation to be hash-for-home or locally cached.
//!

#ifndef __TMC_MSPACE_H__
#define __TMC_MSPACE_H__

#include <sys/types.h>
#include <features.h>

__BEGIN_DECLS
  
struct tmc_alloc_t;  // See <tmc/alloc.h>
struct mallinfo;     // See <malloc.h>

//! tmc_mspace is an opaque type representing an independent
//! region of space that supports tmc_mspace_malloc and the like.
typedef void* tmc_mspace;

//! The tmc_mspace_create() family of APIs creates and returns a new
//! independent space with the given initial capacity or, if 0, the
//! default granularity size.  They return NULL if no system
//! memory is available to create the space.  The capacity of the space
//! grows dynamically as needed to service mspace_malloc() requests.
//!
//! Flags can be specified to control some aspects of the new heap's
//! behavior.  The TMC_MSPACE_LOCKED flag should be used if the heap is to be
//! shared between threads (or between processes).  The TMC_MSPACE_NOGROW
//! flag prevents the heap from allocating any additional memory after
//! the initial "capacity".
//!
tmc_mspace tmc_mspace_create(size_t capacity, unsigned int flags);

//! The tmc_mspace_create_special() API takes a third argument that is a
//! pointer to a tmc_alloc_t object to specify the kind of memory that
//! should be allocated from the new mspace.  This functionality
//! effectively allows application code to choose an allocation strategy
//! for a heap, including huge page support as well as memory homing and
//! memory controller selection.  The passed tmc_alloc_t is copied and
//! saved in the mspace.
//!
//! You can use mspace heaps for user-managed shared memory, that is with
//! a tmc_alloc_t with @c tmc_alloc_set_home(TMC_ALLOC_HOME_INCOHERENT).
//! In this case, you must make explict use of cache flush and
//! invalidate instructions, and you must only allocate memory using
//! mspace_memalign(), with the alignment set to CHIP_L2_LINE_SIZE() from
//! <arch/chip.h> or larger, and only on the core where the heap is created.
//! tmc_mspace_malloc(), tmc_mspace_calloc(), and tmc_mspace_realloc() will
//! return NULL, as will tmc_mspace_memalign() if called with too small
//! an alignment. If you use tmc_mspace_memalign() on a different CPU than
//! the one that created the space, you will likely corrupt the heap metadata.
//! Note that TMC_MSPACE_LOCKED cannot be used with incoherent memory.
//!
tmc_mspace tmc_mspace_create_special(size_t capacity, unsigned int flags,
                                     const struct tmc_alloc_t *);

//! tmc_mspace_create_with_base() uses the memory supplied as the
//! initial base of a new mspace. Part (less than 256*sizeof(size_t) bytes)
//! of this space is used for bookkeeping, so the capacity must be at least
//! this large. (Otherwise 0 is returned.) When this initial space is
//! exhausted, additional memory is obtained from the system.
//! Destroying this space deallocates all additionally allocated
//! space (if possible), but not the initial base.
//!
tmc_mspace tmc_mspace_create_with_base(void* base, size_t capacity,
                                       unsigned int flags);

//! Create a tmc_mspace object using the specified memory, and
//! allocating new memory using the specified tmc_alloc_t.
//! Once the initial allocation is used up, the specified allocator
//! object will be used to request more.  See tmc_mspace_create_special()
//! for more information on the allocator object.
//!
tmc_mspace tmc_mspace_create_with_base_special(void* base, size_t capacity,
                                               unsigned int flags,
                                               const struct tmc_alloc_t*);

//! TMC_MSPACE_LOCKED specifies that a mutex should be used when accessing
//! the heap metadata so that multiple threads can access the heap safely.
//! The mutex can lock between processes as well.
//!
#define TMC_MSPACE_LOCKED           0x1

//! TMC_MSPACE_NOGROW specifies that no additional memory should be added
//! to the initial "capacity" allocation that is performed; this is most 
//! useful with tmc_mspace_create_with_base() for hand-mapped memory.
//!
#define TMC_MSPACE_NOGROW           0x2

//! TMC_MSPACE_SPINLOCK requests that this mspace use tmc_spin_queued locks
//! for locking; passing this value implies TMC_MSPACE_LOCKED.
//!
//! For BME, spin locks are the default, and this flag bit is ignored.
//!
//! Under Linux, mspace defaults to using tmc_sync locks, which are
//! equivalent to cross-process pthread_mutex locks.  Linux applications
//! will always use tmc_sync locks when initializing a new mspace,
//! or when calling brk() to increase the size of the heap, regardless
//! of the value of this flag bit.
//!
//! Using spinlocks is required for dataplane applications to avoid
//! going into the kernel if a lock is held at the moment the code
//! tries to acquire it.  Initialized dataplane applications using
//! pre-existing mspaces created with TMC_MSPACE_NOGROW will not
//! encounter tmc_sync locking.
//!
#define TMC_MSPACE_SPINLOCK         0x4

//! The tmc_mspace_access_with_base() API allows a program to open a
//! pre-existing mspace that is present in memory.  The mspace must
//! have already been created with tmc_mspace_create_with_base() or
//! tmc_mspace_create_with_base_special().  A typical use case is when
//! a region of memory that has previously been initialized as a
//! tmc_mspace is mmap'ed into some other process's address space, and
//! that process then wants to get the appropriate tmc_mspace pointer.
//!
tmc_mspace tmc_mspace_access_with_base(void* base);

//! The tmc_mspace_destroy() API destroys the given space and attempts to
//! return all of its memory to the system, returning the total number of
//! bytes freed. After destruction, the results of access to all memory
//! used by the space become undefined.
//!
size_t tmc_mspace_destroy(tmc_mspace msp);

//! Return the tmc_alloc_t pointer that is currently in use.
//! You can modify the fields of the object, though if you are doing
//! non-atomic updates to fields, you should ensure that no concurrent
//! mallocs are being performed.  Note that the pointer is for the
//! mspace's own tmc_alloc_t, which is always available even if the
//! tmc_mspace_create_special() was not used to initialize the mspace, and
//! is never a pointer to the user's tmc_alloc_t argument.
//!
struct tmc_alloc_t* tmc_mspace_allocator(tmc_mspace msp);

//! tmc_mspace_malloc() behaves as malloc(), but operates within
//! the given space.
//!
void* tmc_mspace_malloc(tmc_mspace msp, size_t bytes)
  __attribute__((__malloc__));

//! tmc_mspace_free() behaves as free(), but looks up the appropriate
//! tmc_mspace to free the memory back to.
//!
void tmc_mspace_free(void* mem);

//! tmc_mspace_realloc() behaves as realloc(), but operates within
//! the given space.  Like with libc's realloc(), if "mem" is NULL,
//! memory is allocated from the given mspace, and if "newsize" is zero,
//! the passed pointer is freed.
//!
void* tmc_mspace_realloc(tmc_mspace msp, void* mem, size_t newsize)
  __attribute__((__malloc__,__warn_unused_result__));

//! tmc_mspace_calloc() behaves as calloc(), but operates within
//! the given space.
//!
void* tmc_mspace_calloc(tmc_mspace msp, size_t n_elements, size_t elem_size)
  __attribute__((__malloc__));

//! tmc_mspace_memalign() behaves as memalign(), but operates within
//! the given space.
//!
void* tmc_mspace_memalign(tmc_mspace msp, size_t alignment, size_t bytes)
  __attribute__((__malloc__));

//! tmc_mspace_mallinfo() behaves as mallinfo(), but reports properties of
//! the given space.  Note that we use the same structure here as
//! defined in <malloc.h>.
//!
struct mallinfo tmc_mspace_mallinfo(tmc_mspace msp);

//! mspace_trim() behaves as malloc_trim(), but
//! operates within the given space.
//!
int tmc_mspace_trim(tmc_mspace msp, size_t pad);

//! Sets tunable parameters (see mallopt() documentation).
//! This globally affects all current and future tmc_mspaces.
//! Note that only two values are implemented:
//!
//! <pre>
//! Symbol            param #  default    allowed param values
//! M_TRIM_THRESHOLD     -1   2*1024*1024   any   (MAX_SIZE_T disables)
//! M_MMAP_THRESHOLD     -3      256*1024   any   (or 0 if no MMAP support)
//! </pre>
//!
int tmc_mspace_mallopt(int, int);

//! Change the default granularity of tmc_mspace growth; must be set to
//! a power-of-two multiple of the page size.  Existing spaces are not
//! affected, but new spaces will default to the specified granularity.
//! Returns true if the passed value was valid.
//!
int tmc_mspace_set_granularity(size_t value);

//! mspace_statistics() returns the statistics for a given space
//! that malloc_stats() prints for the standard malloc.
//!
void tmc_mspace_statistics(tmc_mspace msp,
                           size_t* maxfp, size_t* fp, size_t* used);

//! Freeze the space and all the memory allocated from the space.  The
//! memory allocated from the space is now read-only, which allows it
//! to be cached locally by each thread for increased performance.  The
//! space can no longer be used to allocate or free memory.  If the
//! memory is written, a SIGSEGV results.
//!
//! Returns -1 on error, 0 otherwise.
//!
int tmc_mspace_freeze(tmc_mspace msp);

//! Unfreeze a previously frozen mspace.
//!
//! Returns -1 on error, 0 otherwise.
//!
int tmc_mspace_unfreeze(tmc_mspace msp);

#ifdef TMC_MSPACE_PROVIDE_DLMALLOC_NAMES
// Provide compatibility with 2.x MDE releases and open-source dlmalloc.
#define mspace tmc_mspace
#define create_mspace_with_attr tmc_mspace_create_special
#define create_mspace tmc_mspace_create
#define create_mspace_with_base_and_attr tmc_mspace_create_with_base_special
#define create_mspace_with_base tmc_mspace_create_with_base
#define MSPACE_LOCKED TMC_MSPACE_LOCKED
#define MSPACE_NOGROW TMC_MSPACE_NOGROW
#define destroy_mspace tmc_mspace_destroy
#define alloc_attr_t tmc_alloc_t
#define mspace_malloc tmc_mspace_malloc
#define mspace_free(msp,p) tmc_mspace_free(p)
#define mspace_realloc tmc_mspace_realloc
#define mspace_calloc tmc_mspace_calloc
#define mspace_memalign tmc_mspace_memalign
#define mspace_mallinfo tmc_mspace_mallinfo
#define mspace_trim tmc_mspace_trim
#define mspace_mallopt tmc_mspace_mallopt
#define mspace_statistics tmc_mspace_statistics
#define mspace_freeze tmc_mspace_freeze
#endif

__END_DECLS

/// @}

#endif // __TMC_MSPACE_H__
