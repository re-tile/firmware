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
//! Additional cmem ("common memory") APIs for tmc_shmem.

//! @addtogroup tmc_cmem
//! @{
//!
//! Additional cmem ("common memory") APIs for tmc_shmem.
//! Three additional sets of functionality are provided.
//!
//!
//! @section tmc_cmem_dynamic Dynamic Use of tmc_alloc_t
//!
//! The cmem API allows you to set up a tmc_alloc_t object and modify
//! its attributes on the fly, then call the allocation routines.  The
//! cmem code will check the tmc_alloc_t attributes on each allocation,
//! and dynamically create new shmem arenas as needed to support new
//! attributes.
//!
//! To allocate memory with cmem, create a tmc_alloc_t initialized to
//! TMC_CMEM_ALLOC_INIT, and then modify it as needed to control cache
//! homing, memory controllers, or use of huge pages.  Then, use this
//! tmc_alloc_t directly with tmc_alloc_map(), or pass it to
//! tmc_mspace_create_special() and use the resulting mspace with
//! tmc_mspace_malloc() and the like.  For example:
//!
//! @code
//! tmc_alloc_t alloc = TMC_CMEM_ALLOC_INIT;
//! tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_HERE);
//! thing_t* thing = tmc_alloc_map(&alloc, sizeof(*thing));
//! @endcode
//!
//! The cmem implementation typically only creates a new arena type to
//! support different page sizes; any page size can be specified with
//! cmem, and each allocation with a different page size will cause a
//! new, separate shmem arena to be created.
//!
//! There are some uncommon scenarios that also can lead to additional
//! arenas being created, enumerated here for completeness:
//!
//! - Specifying different NUMA memory configurations (mbind policy/nodemask).
//! - Specifying different mmap_flags: MAP_POPULATE, MAP_NONBLOCK, or
//!   MAP_LOCKED, or the Tilera-specific MAP_CACHE_NO_L1, MAP_CACHE_NO_L2,
//!   or MAP_CACHE_PRIORITY.
//! - Specifying PROT_EXEC in mmap_prot.
//!
//! Note in particular that specifying the Tilera homecache flags
//! (MAP_CACHE_HOME(n), MAP_CACHE_HOME_HASH, MAP_CACHE_HOME_NONE,
//! MAP_CACHE_HOME_HERE, MAP_CACHE_HOME_TASK, MAP_CACHE_HOME_SINGLE) does
//! not cause additional arenas to be allocated, since the shmem API
//! allows setting those attributes on a page-by-page basis.
//!
//!
//! @section tmc_cmem_private Private Unnamed Arenas
//!
//! As shmem arenas are created, they are immediately unlinked, and passed
//! to child processes only by using @c /proc/PID/fd/FD pathnames as stored
//! in an environment variable, so applications don't need to think about
//! the names used by the arenas.  All the participating processes normally
//! share some common ancestor that created the cmem region using
//! tmc_cmem_init(), and must themselves have called tmc_cmem_init().
//!
//! By default, the shmem arenas are allocated contiguously starting at
//! a virtual address designed to keep them far away from other shared
//! objects; the starting address can be overridden by specifying
//! the @c TMC_CMEM_START environment variable.
//!
//! The initial tmc_cmem_init() call sets up the capacity (maximum size)
//! for each shmem arena that is created on demand.  As a result, the
//! total reserved capacity is the result of multiplying the number of
//! different arenas by the tmc_cmem_init() capacity argument.  This value
//! is returned by the tmc_cmem_get_capacity() call.
//!
//! Once the application does a fork() (and possibly an exec()) it is no
//! longer possible for newly-created shmem arenas to be visible to all
//! the processes.  Accordingly, all shmem arenas must be created prior
//! to calling fork or exec.  Once that is done, the application should
//! call tmc_cmem_persist_after_exec(1) to freeze the set of available
//! arenas, and set up the @c TMC_CMEM_INFO environment variable with
//! the information needed by an exec'ed program to reload the cmem set
//! of shmem arenas, by calling tmc_cmem_init(0).
//! 
//! After the set of available arenas is frozen, any new cmem allocation
//! that would require a new arena to be created will fail.  To avoid such
//! failures, the application should ensure that it allocates at least once
//! with each type of allocation that would cause a new arena to be created,
//! or if an actual allocation is unwanted, it should call
//! tmc_cmem_prepare_alloc() with a suitable tmc_alloc_t, which will
//! create the appropriate new shmem arena.
//!
//! It is possible to later unfreeze the cmem arenas by calling
//! tmc_cmem_persist_after_exec(0).  However, if this is done in a child
//! process, and new arenas are then created, they will not be shared with
//! the parent process or other sibling processes.  The child may, however,
//! call tmc_cmem_persist_after_exec(1) again later and then any new
//! arenas will be shared with its own children or exec'ed programs.
//! Alternately, any process can call tmc_cmem_close() and then
//! tmc_cmem_init() to lose access to the current cmem state and start afresh.
//!
//! Most exec() functions pass the current environment to the newly
//! exec'ed command.  However, if you are passing the environment
//! programmatically (via execve or execle) you must ensure that you pass
//! the @c TMC_CMEM_INFO environment variable explicitly.  This variable
//! provides the internal cmem state to the new command.  Note that you
//! can also hand the @c TMC_CMEM_INFO environment variable to another
//! process that is not a parent or child, and as long as it can read the
//! @c /proc/PID/fd/FD files, it can join the cmem state.
//!
//! Various cmem functions set errno when they fail, and unless they are
//! simply passing through errno from a sub-function, then by convention,
//! they use ENODATA if cmem has not been initialized or persisted properly,
//! and otherwise EINVAL.
//!
//!
//! @section tmc_cmem_mspace Default Shared Mspace
//!
//! The cmem API includes routines like tmc_cmem_malloc(), etc., which use
//! a single mspace in the default-attribute small page shmem arena.  See
//! tmc_cmem_malloc(), tmc_cmem_calloc(), tmc_cmem_realloc(),
//! tmc_cmem_memalign(), and tmc_cmem_free().  All participating processes
//! share a single mspace.


#ifndef __TMC_CMEM_H__
#define __TMC_CMEM_H__

#include <features.h>

__BEGIN_DECLS

// For "tmc_alloc_t".
#include <tmc/alloc.h>



//! The default capacity for tmc_cmem_init(), used if a capacity of
//! zero is requested.  This applies separately to each shmem arena
//! that ends up being requested.
//!
#define TMC_CMEM_DEFAULT_CAPACITY (256 * 1024 * 1024)


//! Initialize cmem with at least the given "capacity"
//! (maximum amount of memory that can be allocated), in bytes.
//!
//! If the "capacity" is zero, TMC_CMEM_DEFAULT_CAPACITY is used instead.
//!
//! The function acts as follows:
//!
//! - If cmem has already been initialized in this process
//!   (either directly or by an ancestor process across fork() calls)
//!   this function will return 1 if the existing capacity is sufficient,
//!   else fail and return -1.
//!
//! - Otherwise, if cmem was persisted by a parent process, then, if the
//!   persisted capacity is sufficient, the persisted cmem will be used,
//!   otherwise, this function will fail, returning -1.
//!
//! - Otherwise, a new cmem region will be created, using the requested
//!   capacity.
//!
//! tmc_cmem_init() is multi-thread safe and will only initialize once.
//!
//! None of the memory-allocating functions listed below will work
//! prior to calling tmc_cmem_init().  Similarly, you can not allocate
//! memory from a tmc_alloc_t initialized with tmc_cmem_alloc_init()
//! or TMC_CMEM_ALLOC_INIT prior to calling tmc_cmem_init().
//!
//! @param capacity The requested capacity (in bytes), or zero to use
//! the default capacity.
//!
//! @return -1 (setting errno) on error, 1 (setting errno to EINVAL)
//! if already initialized properly, or zero.
//!
extern int
tmc_cmem_init(size_t capacity);


//! Stop using any created (or inherited) cmem.
//!
//! tmc_cmem_close() can be called after tmc_cmem_init() has been called
//! and cmem has been set up, used, and is no longer necessary.
//! It can also be called WITHOUT having called tmc_cmem_init(), or
//! after fork() and/or exec() have been called, to ensure that any
//! inherited cmem is ignored.
//!
//! The tmc_cmem_close() function should only be called when no other
//! thread can be calling tmc_cmem functions or accessing cmem.
//!
//! @return 1 (setting errno to ENODATA) if there is nothing to close, or zero.
//!
extern int
tmc_cmem_close(void);


//! Get the "capacity" of cmem.
//!
//! Capacity is defined as the total amount of virtual address space
//! that has been reserved, not the actual amount of cmem
//! available for allocation.
//!
//! @return Number of bytes (or zero if not initialized).
//!
extern size_t
tmc_cmem_get_capacity(void);

//! Get the maximum amount of cmem available for allocation.
//!
//! This quantity is the maximum amount of cmem available for allocation.
//!
//! @return Number of bytes (or zero if not initialized).
//!
extern size_t
tmc_cmem_get_available(void);

//! Set whether or not cmem should be available after exec().
//!
//! The passed flag value is recorded in the shmem arena itself and
//! therefore made visible to all participating processes.  When the
//! passed flag is non-zero, the current set of shmem arenas is
//! recorded in an environment variable, and no further arena creation
//! is allowed (tmc_cmem functions that would require a new arena
//! instead just fail).  To avoid those subsequent failures, the process
//! should do at least one of any necessary type of allocation beforehand,
//! or if allocation is not desired, call tmc_cmem_prepare_alloc()
//! with appropriate tmc_alloc_t pointers, prior to calling
//! tmc_cmem_persist_after_exec(1).  When the flag is zero, the
//! environment variable is cleared and new arena creation is allowed.
//!
//! Note that the environment variable is set up with a reference to
//! the current process's @c /proc/PID directory, so if that process
//! disappears, it's no longer possible for tmc_cmem_init() to join
//! new programs to the cmem arenas.  In this case, some other process
//! still holding the cmem arenas open should again call
//! tmc_cmem_persist_after_exec(1) to re-create the environment
//! variable relative to that other process's @c /proc/PID directory.
//!
//! @param flag The desired flag value.
//! @return -1 (setting errno) on error, or the previous value (0 or 1).
//!
extern int
tmc_cmem_persist_after_exec(int flag);

//! Ensure that a suitable arena has been created.
//!
//! This is only required if a particular dynamic allocation type will be
//! performed after fork or exec, since all arenas must be in place prior
//! to fork or exec.
//!
//! @param alloc A tmc_alloc_t pointer to the type of expected allocation.
//! @return -1 (setting errno) on error, or zero.
//!
extern int
tmc_cmem_prepare_alloc(const tmc_alloc_t* alloc);

//! Initialize a tmc_alloc_t so it allocates cmem.
//!
//! @param alloc The "tmc_alloc_t" to initialize.
//! @return The initialized "tmc_alloc_t".
//!
extern tmc_alloc_t*
tmc_cmem_alloc_init(tmc_alloc_t* alloc);


//! A special "tmc_alloc_t" initializer that has the same effect as
//! calling tmc_cmem_alloc_init() on a tmc_alloc_t.
//!
#define TMC_CMEM_ALLOC_INIT \
{ \
  /* .mmap_prot = */ TMC_ALLOC_DEFAULT_MMAP_PROT, \
  /* .mmap_flags = */ MAP_POPULATE | MAP_SHARED, \
  /* .mbind_policy = */ 0, \
  /* .mbind_nodemask = */ 0, \
  /* .page_size = */ 0, \
  /* .mmap_mbind_func = */ tmc_cmem_mmap_mbind \
}


//! A version of "malloc()" using cmem.
//!
//! @param size The size (in bytes).
//! @return A pointer into shared memory, or NULL on failure.
//!
extern void*
tmc_cmem_malloc(size_t size)
  __attribute__((__malloc__));

//! A version of "calloc()" using cmem.
//!
//! @param num The number of entities.
//! @param size The size of each entity (in bytes).
//! @return A pointer into shared memory, or NULL on failure.
//!
extern void*
tmc_cmem_calloc(size_t num, size_t size)
  __attribute__((__malloc__));

//! A version of "realloc()" using cmem.
//!
//! @param ptr The old pointer.
//! @param size The new size (in bytes).
//! @return A pointer into shared memory, or NULL on failure.
//!
extern void*
tmc_cmem_realloc(void *ptr, size_t size)
  __attribute__((__malloc__,__warn_unused_result__));

//! A version of "memalign()" using cmem.
//!
//! @param boundary The alignment (in bytes).
//! @param size The size (in bytes).
//! @return A pointer into shared memory, or NULL on failure.
//!
extern void*
tmc_cmem_memalign(size_t boundary, size_t size)
  __attribute__((__malloc__));


//! A version of "free()" using cmem.
//!
//! @param ptr The pointer to memory to be freed.
//!
extern void
tmc_cmem_free(void* ptr);


#ifndef __DOXYGEN__

// Function pointer structure for tmc_alloc_t.funcs.
void* tmc_cmem_mmap_mbind(const tmc_alloc_t* alloc, void* start, size_t length);

// Deprecated compatibility function from old cmem implementation.
extern int tmc_cmem_detach(void);

#endif // __DOXYGEN__

__END_DECLS

#endif // __TMC_CMEM_H__

//! @}
