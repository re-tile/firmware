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
//! Routines for allocating pages with specified home cache, memory
//! controller, and length.
//!

//! @addtogroup tmc_alloc
//! @{
//!
//! Routines for allocating pages with specified home cache, memory
//! controller, and length.
//!
//! The <tmc/alloc.h> API allows users to specify memory performance
//! parameters and easily allocate pages using those parameters.
//! The core of the API is the ::tmc_alloc_t structure, which is
//! configured to specify how memory should be cached, what size pages
//! to use, and so forth.
//!
//! The simplest use of the API is to initialize the ::tmc_alloc_t
//! to the default values and then allocate bytes.  For example:
//!
//! @code
//! // Allocate at least 12345 bytes of memory with the default memory
//! // type.  Allocation sizes are rounded up to a page-size boundary.
//! tmc_alloc_t alloc = TMC_ALLOC_INIT;
//! void* mem = tmc_alloc_map(&alloc, 12345);
//! if (mem == NULL)
//!   die("Failed to allocate memory.\n");
//! @endcode
//!
//! Note that the allocation size passed to tmc_alloc_map() is measured
//! in bytes and the allocator rounds up to the nearest page size
//! increment.  By default, the system allocates normal size
//! (64kB), process-private memory pages with "hash-for-home" cache homing.
//! The ucache_hash boot option and LD_CACHE_HASH environment variable can
//! be used to change the default cache homing; see 
//! <em>Programming the TILE-Gx Processor</em> (UG505) for more information.
//!
//! A ::tmc_alloc_t structure can also be passed to the tmc_mspace heap
//! allocator in order to allow custom memory allocation via a
//! familiar, malloc()-like interface.  For more information, see @ref
//! tmc_mspace.
//!
//! @section tmc_alloc_t Changing Memory Attributes
//!
//! As described above, a ::tmc_alloc_t initialized to ::TMC_ALLOC_INIT
//! results in memory with a set of default properties.  A variety of
//! 'setter' methods allow the user to modify these default policies to
//! tune for particular performance considerations.  For example, the
//! tmc_alloc_set_home() method controls the CPU or CPUs on which pages
//! are homed.  For example:
//!
//! @code
//! // Allocate memory homed on this task's CPU, and dynamically rehome
//! // the pages if this task moves to another CPU.
//! tmc_alloc_t alloc = TMC_ALLOC_INIT;
//! tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_TASK);
//! p1 = tmc_alloc_map(&alloc, size);
//!
//! // Allocate 'hash-for-home' memory.
//! tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_HASH);
//! p2 = tmc_alloc_map(&alloc, size);
//! @endcode
//!
//! The TMC_ALLOC_HOME_xxx values are intended as arguments to
//! tmc_alloc_set_home(), and are returned by tmc_alloc_get_home().
//! Note that they are different from the mmap() MAP_CACHE_HOME_xxx
//! arguments and are not interchangeable with them, despite the
//! similarity of the names.
//!
//! Similarly, the tmc_alloc_set_huge() method configures the allocator to
//! allocate default-size huge memory pages; these are typically 16MB, but
//! may be overridden with the "default_hugepagesz=" kernel boot option.
//!
//! Multiple setter methods can be invoked on the same allocator object:
//!
//! @code
//! // Allocate default-sized huge page, 'hash-for-home' memory.
//! tmc_alloc_t alloc = TMC_ALLOC_INIT;
//! tmc_alloc_set_huge(&alloc);
//! tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_HASH);
//! p3 = tmc_alloc_map(&alloc, size);
//! @endcode
//!
//! Other huge memory page sizes may be available depending on boot arguments
//! and the particular machine type; you can ask for the set of available
//! page sizes with tmc_alloc_get_pagesizes(), and request a specific
//! page size with tmc_alloc_set_pagesize_exact().  If you just need
//! a chunk of memory with contiguous physical addresses, or want to use
//! the largest possible pages for a given allocation size, you can use
//! tmc_alloc_set_pagesize() to request the smallest page size that is
//! at least as large as the passed argument.
//!
//! Several other memory parameters can be specified via the
//! tmc_alloc_t, including memory controllers and L1 / L2 cacheability.
//!
//! @section tmc_alloc_share Shared Memory
//!
//! Applications that run multiple tasks often require 'shared' memory
//! regions that are accessible to all tasks in the application.  In
//! threaded applications, all memory is shared by default.  In
//! process-based applications, memory allocations must be specially
//! configured in order to be shared.  By default, memory allocations
//! are private to whatever process allocates the memory.
//!
//! The Tilera MDE provides two mechanisms for sharing memory between
//! processes.  The first method makes use of the standard Linux @c
//! MAP_SHARED flag.  This type of memory can be allocated by calling
//! tmc_alloc_set_shared().  @c MAP_SHARED memory is shared between the
//! allocating process and any child processes created after the
//! allocation.  As a result, it is useful for applications that can
//! allocate all of their memory at startup, but is not appropriate for
//! applications that need to allocate more shared memory after
//! creating many parallel processes.
//!
//! The second shared memory mechanism is the Tilera-provided "shared
//! memory" API.  This allocation system allows dynamic allocation of
//! shared memory even after the application has gone parallel.  For
//! more information, see @ref tmc_shmem.
//!

#ifndef __TMC_ALLOC_H__
#define __TMC_ALLOC_H__

#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

__BEGIN_DECLS

#ifndef __DOXYGEN__

// Provide values here to match <sys/mman.h> if __STRICT_ANSI__ is set.
// We validate that they match when building libtmc.
#if defined(__tile__)
#define __TMC_ALLOC_MAP_POPULATE 0x0040
#define __TMC_ALLOC_MAP_HUGETLB  0x4000
#else  /* assume <asm-generic/mman.h> */
#define __TMC_ALLOC_MAP_POPULATE 0x8000
#define __TMC_ALLOC_MAP_HUGETLB  0x40000
#endif

// Provide values here to match <numaif.h> to avoid a build dependency.
// We validate that they match when building libtmc.
#define __TMC_ALLOC_MPOL_DEFAULT     0
#define __TMC_ALLOC_MPOL_PREFERRED   1
#define __TMC_ALLOC_MPOL_BIND        2
#define __TMC_ALLOC_MPOL_INTERLEAVE  3

// Default value to initialize the "mmap_prot" field of a tmc_alloc_t.
#define TMC_ALLOC_DEFAULT_MMAP_PROT  \
  (PROT_READ | PROT_WRITE)

// Default value to initialize the "mmap_flags" field of a tmc_alloc_t.
#define TMC_ALLOC_DEFAULT_MMAP_FLAGS __TMC_ALLOC_MAP_POPULATE

// Convert the 'convenient' home definition into actual homing flags.
#define TMC_ALLOC_HOME_TO_FLAGS(home)                              \
  (((home) == TMC_ALLOC_HOME_NONE) ?                               \
    (MAP_CACHE_NO_LOCAL | MAP_CACHE_HOME_NONE) :                   \
   ((home) == TMC_ALLOC_HOME_INCOHERENT) ? MAP_CACHE_INCOHERENT :  \
   ((home) == TMC_ALLOC_HOME_DEFAULT) ? 0 :                        \
   _MAP_CACHE_MKHOME(home))

// Return the path to the mount point for default-size huge pages.
#define TMC_ALLOC_DEFAULT_HUGE_MOUNTPOINT "/dev/hugepages"


// mmap() and mbind() memory; support function for tmc_alloc_map().
void* tmc_alloc_mmap_mbind(void* start, size_t length, int mmap_prot,
                           int mmap_flags, int fd, unsigned long long offset,
                           int mbind_policy, unsigned long mbind_nodemask);

struct tmc_alloc_t;

// Type of a function for mmap() and mbind() of memory.
// For a non-zero START, must return mapping at START, or NULL on failure.
// Must assume alloc->mmap_flags without MAP_PRIVATE|MAP_SHARED as private.
typedef void* (*tmc_alloc_mmap_mbind_func_t)(const struct tmc_alloc_t* alloc,
                                             void* start, size_t length);

// Type of a function that can remap memory (see "man mremap").
typedef void* (*tmc_alloc_mremap_func_t)(const struct tmc_alloc_t* alloc,
                                         void* old_address, size_t old_length,
                                         size_t new_length, int flags,
                                         void* new_address);

// Type of a function that can unmap memory (see "man munmap").
typedef int (*tmc_alloc_munmap_func_t)(const struct tmc_alloc_t* alloc,
                                       void* start, size_t length);

// Register an alloc object that requires non-default munmap/mremap.
int tmc_alloc_register_address_range(struct tmc_alloc_t*,
                                     void* start_ptr, size_t length,
                                     tmc_alloc_munmap_func_t unmap,
                                     tmc_alloc_mremap_func_t remap);

#endif // __DOXYGEN__

//! Describe an allocation.  The fields in this structure should only
//! be modified via the various tmc_alloc_set/tmc_alloc_get methods.
typedef struct tmc_alloc_t {

  //! "prot" value passed to mmap().
  //! This field should hold a value made of a bitwise OR of PROT_READ,
  //! PROT_WRITE, and PROT_EXEC.
  int mmap_prot;

  //! "flags" value passed to mmap().
  //!
  //! The Tilera Linux mmap() extensions are documented in its man page;
  //! see "tile-man mmap" for more information or see <sys/mman.h>
  //! for reference.
  int mmap_flags;

  //! "policy" value passed to mbind().
  //! If equal to zero (MPOL_DEFAULT), mbind() is not called.
  //! The Tilera Linux mbind() has no Tilera-specific extensions; for
  //! convenience, the man page is available via "tile-man mbind".
  int mbind_policy;

  //! "nodemask" value whose address is passed to mbind().
  unsigned long mbind_nodemask;

  //! If non-zero, the hugetlbfs filesystem page size to use.
  //! If zero, regular MAP_ANONYMOUS mappings are used
  //! (with the MAP_HUGETLB bit allowing default-size huge pages instead
  //! of regular small pages, as usual).
  size_t pagesize;

  //! Internal: hook function to use instead of tmc_mspace_mmap_mbind().
  tmc_alloc_mmap_mbind_func_t mmap_mbind_func;

} tmc_alloc_t;


//! Default initializer for a tmc_alloc_t object.
#define TMC_ALLOC_INIT \
{ \
  /* .mmap_prot = */ TMC_ALLOC_DEFAULT_MMAP_PROT, \
  /* .mmap_flags = */ TMC_ALLOC_DEFAULT_MMAP_FLAGS, \
  0, 0, 0, 0 \
}


#ifdef __tile__

//! Allocate memory whose home cache is on a single CPU chosen by the OS.
#define TMC_ALLOC_HOME_SINGLE _MAP_CACHE_HOME_SINGLE

//! Allocate memory whose home cache is the CPU the task was running
//! on at the time of the tmc_alloc_map() call.
#define TMC_ALLOC_HOME_HERE _MAP_CACHE_HOME_HERE

//! Allocate memory whose home cache migrates to the CPU on which this
//! task runs.  Note that this attribute should not be used with memory
//! that is shared between processes, e.g. memory allocated after
//! calling tmc_alloc_set_shared and forking multiple processes.
//! This is because the kernel only automatically migrates page homes
//! if the page is not shared between processes.
#define TMC_ALLOC_HOME_TASK _MAP_CACHE_HOME_TASK

//! Allocate memory whose home cache is distributed around
//! via hash-for-home.
#define TMC_ALLOC_HOME_HASH _MAP_CACHE_HOME_HASH

//! Allocate memory that is completely uncached.
//! Note that on TILE-Gx, you may not issue atomic instructions to
//! uncached memory (a SIGBUS will result if you do).
#define TMC_ALLOC_HOME_NONE _MAP_CACHE_HOME_NONE

//! Allocate memory that is incoherent between CPUs, and requires
//! user-managed explicit flush and invalidate instructions to use.
#define TMC_ALLOC_HOME_INCOHERENT (_MAP_CACHE_HOME_MASK + 1)

//! Allocate memory whose home caching is determined by $LD_CACHE_HASH.
#define TMC_ALLOC_HOME_DEFAULT (_MAP_CACHE_HOME_MASK + 2)

#endif

//! Allocate the specified amount of memory as anonymous pages at the
//! specified address.
//!
//! This routine is similar to the tmc_alloc_map() routine, but it
//! specifies an address to try to map memory; if that address is in
//! use, NULL is returned with an EINVAL errno.  If "start" is NULL,
//! this API is the same as tmc_alloc_map().
//!
//! @param alloc Allocator object to use.
//! @param start Starting memory address, or NULL if no preference.
//! @param length Size of allocation to use (rounded up to pagesize).
//! @return Pointer to allocated memory, or NULL on error.
//!
void* tmc_alloc_map_at(const tmc_alloc_t* alloc, void* start, size_t length)
  __attribute__((__malloc__));

//! Allocate the specified amount of memory as anonymous pages.
//!
//! Memory returned is always zero; length is rounded up to the next
//! page size boundary.  The tmc_alloc_t pointer can specify how to
//! allocate memory, or can be NULL, in which case memory is allocated
//! via plain @c mmap().  On failure, this API returns NULL and sets errno
//! to ENOMEM if out of memory or EINVAL if any fields in the
//! tmc_alloc_t were illegal.
//!
//! @param alloc Allocator object to use.
//! @param length Size of allocation to use (rounded up to pagesize).
//! @return Pointer to allocated memory, or NULL on error.
//!
static __inline void*
tmc_alloc_map(const tmc_alloc_t* alloc, size_t length)
{
  return tmc_alloc_map_at(alloc, 0, length);
}

//! Unmap memory allocated by tmc_alloc_map().
//!
//! If the address is not aligned to the page size, -1 is returned with EINVAL.
//! If the address range spans multiple page sizes, EINVAL is also returned.
//! See "man munmap" for more information on the arguments.
//!
//! @param start Starting memory address.
//! @param length Size of memory to unmap (rounded up to pagesize).
//! @return Zero on success, or -1 on failure (and sets errno).
//!
int tmc_alloc_unmap(void* start, size_t length);

//! Change the length and/or address of a memory mapping from tmc_alloc_map()
//! with the given tmc_alloc_t.
//!
//! If MREMAP_FIXED is set in flags, then a fifth argument must be passed
//! as the new address to use for the modified mapping.
//! If addresses are not aligned to the page size, -1 is returned with EINVAL.
//! If the address range spans multiple page sizes, EINVAL is also returned.
//! See "man mremap" for more information on the arguments.
//!
//! @param addr Starting memory address of existing mapping.
//! @param old_len Size of original mapping (rounded up to pagesize).
//! @param new_len Requested new size (rounded up to pagesize).
//! @param flags Flags (see "man mremap").
//! @return The new address on success, or MAP_FAILED on error (sets errno).
//!
void* tmc_alloc_remap(void* addr, size_t old_len, size_t new_len,
                      int flags, ...)
  __attribute__((__malloc__,__warn_unused_result__));


//! Initialize a tmc_alloc_t dynamically to TMC_ALLOC_INIT.
//!
//! @param alloc Uninitialized memory to be initialized.
//! @return Returns the @c alloc pointer.
//!
tmc_alloc_t* tmc_alloc_init(tmc_alloc_t* alloc);


//! Set the MAP_HUGETLB bit in the "mmap_flags" word.
//! This will cause default-size huge pages to be allocated.
//!
//! See the tmc_alloc_set_pagesize() and tmc_alloc_set_pagesize_exact()
//! methods for other techniques for choosing a non-default page size.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_huge(tmc_alloc_t* alloc)
{
  alloc->mmap_flags |= __TMC_ALLOC_MAP_HUGETLB;
  alloc->pagesize = 0;
  return alloc;
}

//! Clear the MAP_HUGETLB bit in the "mmap_flags" word.
//! This will cause normal-size pages to be allocated.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_clear_huge(tmc_alloc_t* alloc)
{
  alloc->mmap_flags &= ~__TMC_ALLOC_MAP_HUGETLB;
  alloc->pagesize = 0;
  return alloc;
}

#ifdef __tile__

#ifndef __DOXYGEN__
// This macro causes users that accidentally pass a constant
// MAP_CACHE_HOME_xxx flag to tmc_alloc_set_home() to get a link error
// rather than a cryptic runtime error.
//
tmc_alloc_t* tmc_alloc_set_home__does_not_take_MAP_CACHE_xxx_arguments(void)
  __attribute__((warning("use ALLOC_HOME_xxx arguments")));
#define tmc_alloc_set_home(alloc, home) \
  ((__builtin_constant_p(home) && ((home) & _MAP_CACHE_HOME) != 0) ? \
    tmc_alloc_set_home__does_not_take_MAP_CACHE_xxx_arguments() : \
    __tmc_alloc_set_home((alloc), (home)))
static __inline tmc_alloc_t*
__tmc_alloc_set_home(tmc_alloc_t* alloc, int home)
#else
//! Set the mmap_flags value to represent the desired cache homing.
//!
//! This function also clears the MAP_CACHE_NO_LOCAL flags, or sets them
//! for TMC_ALLOC_HOME_NONE.
//!
//! If this function is not used, the cache homing is set based
//! on the kernel's defaults.
//!
//! Note that since this function is designed to take CPU numbers
//! and TMC_ALLOC_HOME_xxx flags, it can not take the similar mmap() flags
//! such as MAP_CACHE_HOME_HASH; the mmap() flags have different
//! numerical values.  The header uses a macro wrapper to attempt to
//! catch the use of the wrong kind of values and generate a link error.
//!
//! @param alloc Allocator object to be updated.
//! @param home CPU number or one of the special TMC_ALLOC_HOME_xxx flags.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_home(tmc_alloc_t* alloc, int home)
#endif
{
  int mask = MAP_CACHE_NO_LOCAL | MAP_CACHE_INCOHERENT |
    _MAP_CACHE_MKHOME(_MAP_CACHE_HOME_MASK);
  int newbits = TMC_ALLOC_HOME_TO_FLAGS(home);
  alloc->mmap_flags = (alloc->mmap_flags & ~mask) | newbits;
  return alloc;
}

//! Set the mmap_flags value to represent the desired level of caching.
//!
//! The "caching" value should be MAP_CACHE_NO_L1, MAP_CACHE_NO_L2
//! or MAP_CACHE_NO_LOCAL to disable all local caching.
//! Note that "tmc_alloc_set_home(TMC_ALLOC_HOME_NONE)" sets the
//! MAP_CACHE_NO_LOCAL flag for you.
//!
//! @param alloc Allocator object to be updated.
//! @param caching mmap() flags combination of MAP_CACHE_NO_xxx bits.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_caching(tmc_alloc_t* alloc, int caching)
{
  alloc->mmap_flags = (alloc->mmap_flags & ~MAP_CACHE_NO_LOCAL) | caching;
  return alloc;
}

#ifndef __tilegx__
//! Set the mmap_flags value to indicate priority (also known as "pinned").
//! This setting makes the resulting mapping take priority in the cache.
//! Currently not implemented for TILE-Gx.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_priority(tmc_alloc_t* alloc)
{
  alloc->mmap_flags |= MAP_CACHE_PRIORITY;
  return alloc;
}

//! Set the mmap_flags value not to indicate priority (also known as "pinned").
//! This setting makes the resulting mapping not take priority in the cache,
//! and is the default for all normal mappings.
//! Currently not implemented for TILE-Gx.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_clear_priority(tmc_alloc_t* alloc)
{
  alloc->mmap_flags &= ~MAP_CACHE_PRIORITY;
  return alloc;
}
#endif /* !__tilegx__ */

#endif /* __tile__ */


//! Set allocated memory to be shared.
//!
//! Setting the MAP_SHARED flag on anonymous memory generally makes
//! that memory shared between parent and child after a call to @c fork().
//! This is an easy and efficient way to set up shared memory in
//! a process hierarchy.  However, note that after forking, additional
//! memory that is allocated is @e not shared with the parent; it is only
//! shared with any subsequent children of the allocating process.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_shared(tmc_alloc_t* alloc)
{
  alloc->mmap_flags &= ~MAP_PRIVATE;
  alloc->mmap_flags |= MAP_SHARED;
  return alloc;
}

//! Set allocated memory to be private.
//!
//! Private memory is the default for tmc_alloc_map(); such memory is
//! not shared between a parent and child after a call to @c fork(), but
//! instead is copy-on-write when either one tries to write to it.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_clear_shared(tmc_alloc_t* alloc)
{
  alloc->mmap_flags &= ~MAP_SHARED;
  return alloc;
}


//! Return a mask of the memory controller nodes available for use.
//!
//! When striping memory across controllers, node 0 is the only node
//! available under Linux, representing all of the striped memory.
//! As a result, the "nodes" functionality is not useful when striping.
//!
//! @return Bitmask of NUMA memory nodes available for use
//! or 0 on failure (and sets errno).
//!
unsigned long tmc_alloc_get_available_nodes(void);

//! Return the number of memory controller nodes available for use.
//!
//! This API reports one more than the highest valid memory node number
//! that can be used for tmc_alloc_set_node_preferred() and the like.
//! When memory striping is enabled (as it is by default, if possible)
//! this API will always return "1".
//!
//! @return Number of NUMA memory nodes available for use
//! or 0 on failure (and sets errno).
//!
static __inline unsigned long
tmc_alloc_num_available_nodes(void)
{
  return (8 * sizeof(long)) - __builtin_clzl(tmc_alloc_get_available_nodes());
}

//! Set the required memory nodes from which to acquire memory.
//!
//! When this option is set, memory will only be allocated from
//! the memory node(s) specified.  If memory is not available on
//! those node(s), the allocation request will fail.  By default,
//! memory will still be allocated from a cpu's local node, if that
//! node is specified in the "nodes" mask.
//!
//! Note that this routine does not perform error-checking, so it is
//! recommended to check the "nodes" mask against the result of
//! tmc_alloc_get_available_nodes() before using it.  In particular note
//! that you can't change the controller usage if running on a system
//! with striped memory.
//!
//! @param alloc Allocator object to be updated.
//! @param nodes Bitmask of NUMA memory node numbers to require.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_nodes(tmc_alloc_t* alloc, unsigned long nodes)
{
  alloc->mbind_policy = __TMC_ALLOC_MPOL_BIND;
  alloc->mbind_nodemask = nodes;
  return alloc;
}

//! Set the preferred memory node from which to acquire memory.
//!
//! Memory allocated in this way will be from the specified memory
//! node if possible, but if no memory remains on that node, memory
//! will be provided from other memory controllers in the system.
//!
//! Note that this routine does not perform error-checking, so it is
//! recommended to check the "node" value against the result of
//! tmc_alloc_get_available_nodes() before using it.  In particular note
//! that you can't change the controller usage if running on a system
//! with striped memory.
//!
//! @param alloc Allocator object to be updated.
//! @param node NUMA memory node number to prefer.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_node_preferred(tmc_alloc_t* alloc, int node)
{
  alloc->mbind_policy = __TMC_ALLOC_MPOL_PREFERRED;
  alloc->mbind_nodemask = (1UL << node);
  return alloc;
}

//! Set up a mask of memory nodes across which to interleave memory.
//!
//! Memory allocated in this way will be "striped" across the
//! specified set of memory controllers with page granularity,
//! with consecutive pages being allocated from consecutive
//! controllers in the requested "nodes" set.
//!
//! The "nodes" value can contain "out-of-range" bit indexes,
//! which are skipped by the kernel.  Thus, you can pass "~0UL"
//! as the "nodes" value to interleave across all memory controllers,
//! regardless of how many are attached.
//!
//! Since this routine does not perform error-checking, if you want to
//! interleave on specific nodes it is recommended to check the "nodes"
//! mask against the result of tmc_alloc_get_available_nodes() before using it.
//! In particular note that you can't change the controller usage if
//! running on a system with striped memory.
//!
//! @param alloc Allocator object to be updated.
//! @param nodes Bitmask of NUMA memory nodes to interleave across.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_nodes_interleaved(tmc_alloc_t* alloc, unsigned long nodes)
{
  alloc->mbind_policy = __TMC_ALLOC_MPOL_INTERLEAVE;
  alloc->mbind_nodemask = nodes;
  return alloc;
}

//! Set up to use the default strategy for choosing memory nodes.
//!
//! This strategy is the one that the tmc_alloc_t object is set
//! to by the initialization methods.  The kernel
//! interprets this strategy to prefer the default memory controller
//! for the current CPU, which is usually (though not always) the
//! closest memory controller to that CPU.  Note that the default
//! strategy is the same for all page sizes.
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_node_default(tmc_alloc_t* alloc)
{
  alloc->mbind_policy = __TMC_ALLOC_MPOL_DEFAULT;
  alloc->mbind_nodemask = 0;
  return alloc;
}

//! Use the local memory controller for the allocating CPU for memory.
//!
//! This policy is normally the default, but if set_mempolicy(2) has been
//! used to set a different default, this method can be called to
//! ensure that memory is chosen from the allocating CPU's default
//! memory controller (usually the closest controller to the CPU).
//!
//! @param alloc Allocator object to be updated.
//! @return Returns the @c alloc pointer.
//!
static __inline tmc_alloc_t*
tmc_alloc_set_node_local(tmc_alloc_t* alloc)
{
  alloc->mbind_policy = __TMC_ALLOC_MPOL_PREFERRED;
  alloc->mbind_nodemask = 0;   // kernel uses local controller
  return alloc;
}


#ifdef __tile__

//! Get the current cache homing override.
//!
//! This function is not meaningful if _MAP_CACHE_HOME is not set in
//! the mmap_flags.
//!
//! @param alloc Allocator object to be queried.
//! @return Current cache homing (CPU number or TMC_ALLOC_HOME_xxx
//! value, above).
//!
static __inline int
tmc_alloc_get_home(const tmc_alloc_t* alloc)
{
  return !(alloc->mmap_flags & _MAP_CACHE_HOME) ? TMC_ALLOC_HOME_DEFAULT :
    (alloc->mmap_flags & _MAP_CACHE_INCOHERENT) ? TMC_ALLOC_HOME_INCOHERENT :
    (alloc->mmap_flags >> _MAP_CACHE_HOME_SHIFT) & _MAP_CACHE_HOME_MASK;
}

//! Return the caching bits that are set in mmap_flags (MAP_CACHE_NO_L1
//! and/or MAP_CACHE_NO_L2).
//!
//! @param alloc Allocator object to be queried.
//! @return Current caching bitmask (MAP_CACHE_NO_xxx flags).
//!
static __inline int
tmc_alloc_get_caching(const tmc_alloc_t* alloc)
{
  return alloc->mmap_flags & MAP_CACHE_NO_LOCAL;
}

#ifndef __tilegx__
//! Get whether or not priority caching is requested from this tmc_alloc_t.
//! Currently not implemented for TILE-Gx.
//!
//! @param alloc Allocator object to be queried.
//! @return True if priority caching in requested.
//!
static __inline int
tmc_alloc_get_priority(const tmc_alloc_t* alloc)
{
  return (alloc->mmap_flags & MAP_CACHE_PRIORITY) != 0;
}
#endif /* !__tilegx__ */

#endif /* __tile__ */

//! Get whether or not the tmc_alloc_t is for shared memory.
//!
//! See @ref tmc_alloc_set_shared and @ref tmc_alloc_clear_shared
//! for more information.
//!
//! @param alloc Allocator object to be queried.
//! @return True if the tmc_alloc_t is for shared memory.
//!
static __inline int
tmc_alloc_get_shared(const tmc_alloc_t* alloc)
{
  return (alloc->mmap_flags & MAP_SHARED) != 0;
}

//! Return whether the MAP_HUGETLB bit is set in the flags.
//!
//! This bit is set by tmc_alloc_set_huge(), and whenever
//! tmc_alloc_set_pagesize() chooses a huge page size,
//! even if not the default huge page size.
//!
//! See the tmc_alloc_get_pagesize() method for a more generic query.
//!
//! @param alloc Allocator object to be queried.
//! @return True if MAP_HUGETLB is set in the flags bit.
//!
static __inline int
tmc_alloc_get_huge(const tmc_alloc_t* alloc)
{
  return (alloc->mmap_flags & __TMC_ALLOC_MAP_HUGETLB) != 0;
}

//! Return the size of default-size huge pages by reading /proc/meminfo.
//! If it can't be determined, the function will print an error
//! message on stderr and abort().
//!
size_t tmc_alloc_get_huge_pagesize(void) __attribute__((__const__));

//! Return the page size corresponding to this tmc_alloc_t.
//!
//! @param alloc Allocator object to be queried.
//! @return Pagesize of pages allocated using this allocator object.
//!
static __inline size_t
tmc_alloc_get_pagesize(const tmc_alloc_t* alloc)
{
  return alloc->pagesize ? alloc->pagesize :
    tmc_alloc_get_huge(alloc) ? tmc_alloc_get_huge_pagesize() : getpagesize();
}

//! Return the set of page sizes available for use by this process.
//! Note that this only specifies pages that are configured as
//! possible in the kernel, and for which the Tilera startup script
//! creates mountpoints at boot time, but does not guarantee that
//! any pages of that size are in fact available.  Also note that if
//! pages are incompatible with the process (e.g. 4GB pages in an -m32
//! process on TILE-Gx) they will not be returned.
//!
//! The return value is a bit-wise "or" of the available page sizes,
//! so for example if 16MB (0x1000000) and 64KB (0x10000)
//! pages are available for use, the return value will be 0x1010000.
//!
//! Note that the availability of huge pages is controlled by various
//! kernel boot arguments (e.g. "hugepagesz" and "hugepages") and by control
//! files located under /sys/kernel/mm/hugepages/.
//!
//! @return Returns the bitwise "or" of all available page sizes.
//!
unsigned long tmc_alloc_get_pagesizes(void);

//! Set the page size for this tmc_alloc_t based on the given size.
//! The size is rounded up to the nearest page size.  If no single
//! page can hold the given number of bytes, the largest page size
//! is selected, and the method returns NULL.
//!
//! This API can be used to acquire memory which is physically
//! contiguous, by requesting the size needed, then checking that
//! the return value is non-NULL.
//!
//! @param alloc Allocator object to be updated.
//! @param size Size to use when choosing the page size.
//! @return Returns the @c alloc pointer, if a single page can hold
//! the specified size, or NULL otherwise.
//!
tmc_alloc_t* tmc_alloc_set_pagesize(tmc_alloc_t* alloc, size_t size);

//! Set the page size for this tmc_alloc_t.
//! If the page size does not exist on the system, NULL is returned.
//!
//! @param alloc Allocator object to be updated.
//! @param pagesize Page size being requested.
//! @return Returns the @c alloc pointer, if the page size is valid,
//! or NULL otherwise.
//!
tmc_alloc_t* tmc_alloc_set_pagesize_exact(tmc_alloc_t* alloc, size_t pagesize);

//! Return how much memory will be allocated using this allocator
//! for the requested "size" argument, given the underyling page size.
//!
//! @param alloc Allocator object to be updated.
//! @param size Requested size of memory.
//! @return Amount of memory that will be allocated.
//!
static __inline size_t
tmc_alloc_size(const tmc_alloc_t* alloc, size_t size)
{
  size_t pagesize = tmc_alloc_get_pagesize(alloc);
  return (size + pagesize - 1) & -pagesize;
}

#ifdef __tile__

//! Check hash-for-home default for this process.
//! This call reports whether tmc_alloc_map() with no tmc_alloc_set_home(), or
//! mmap() with MAP_ANONYMOUS and no MAP_CACHE_HOME_xxx flags,
//! will return hash-for-home memory.
//! @return 1 for hash-for-home, 0 for memory cached on this cpu.
//!
int tmc_alloc_get_hash_default(void);

#endif

#ifndef __DOXYGEN__
void __tmc_get_huge_file_dir(const tmc_alloc_t* alloc, char* buf);
#endif

__END_DECLS

#endif // __TMC_ALLOC_H__

//! @}
