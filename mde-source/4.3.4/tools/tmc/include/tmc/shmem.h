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
//! Inter-process dynamic shared memory support.

//! @addtogroup tmc_shmem
//! @{
//!
//! Inter-process dynamic shared memory support.
//!
//! This API provides a convenient method for multiple processes to share
//! memory using a persistent filesystem-based arena that is automatically
//! mapped at the same, fixed address in all processes sharing the arena.
//! The application chooses an address for the arena to be located at
//! and a maximum size to which it can grow, and the system manages
//! coordinating access to memory mapped from the file among the processes.
//! Since the address is fixed, absolute pointer values, etc., may be
//! safely stored into the arena.
//!
//! As is always true when you use shared memory,
//! you should employ appropriate memory fencing to ensure that any
//! modifications are actually fully visible before they are used by
//! any other process.
//!
//! Typically an initial process will call tmc_shmem_create() to create
//! the file using a fixed, known file name.  Other processes then call
//! tmc_shmem_open() to gain access to the arena.  The creator should
//! first initialize a @c tmc_alloc_t object to indicate any special
//! attributes of the desired memory, such as huge page size, variant
//! cache attributes, etc. If huge pages are requested, the tmc_shmem
//! code will automatically open an additional file in the appropriate
//! hugetlb file system.  The files are opened such that they are automatically
//! closed if the process calls exec() to start a new executable.
//!
//! The APIs create a file with permission 0600 (owner-only read/write),
//! but the application may invoke fchmod() or fchown() on the underlying
//! file descriptors if desired to reset the ownership and permissions.
//!
//! If the application wishes to create a temporary file name to hold
//! the arena, it can use tmc_shmem_create_temp() and pass in a template
//! filename, just as is done for mkstemp().  In this case it is typically
//! then necessary to communicate the chosen filename out-of-band to the other
//! processes that wish to share the arena.
//!
//! To grow the arena, any process that has the tmc_shmem arena open can
//! call tmc_shmem_grow(); this is implemented by extending the underlying
//! file and returning a pointer to the newly-allocated chunk of memory
//! at the end of the file.  Similarly, tmc_shmem_shrink() will truncate
//! the underlying file to a shorter length, invalidating any pointers
//! into the truncated portion of the file.
//!
//! If MAP_POPULATE is set in the tmc_alloc_t mmap_flags, the code arranges
//! to fault in new pages in a way that ensures that the kernel allocates
//! physical memory for the new pages prior to returning from
//! tmc_shmem_grow().  If sufficient memory is not available (or if
//! the maximum address space as specified in tmc_shmem_create has been
//! exhausted) the routine will fail and set errno to ENOMEM.
//!
//! To handle allocating and freeing shared memory more dynamically, the
//! tmc_shmem_alloc() routine returns a tmc_alloc_t pointer that can
//! be used to allocate individual pages from the end of the file.
//! The general case of unmapping individual pages is not supported
//! (although the special case of unmapping the page(s) at the end of
//! the currently-allocated file mapping will call tmc_shmem_shrink()
//! to return those pages to the operating system).  The assumption is that
//! the resulting tmc_alloc_t is passed to the @c tmc_mspace code and used
//! to create a @c tmc_mspace arena that supports dynamically allocating
//! and freeing memory, for example:
//!
//! @code
//! tmc_mspace msp =
//!   tmc_mspace_create_special(0, TMC_MSPACE_LOCKED,
//!                             tmc_shmem_alloc(shmem));
//! @endcode
//!
//! The resulting arena can be used for tmc_mspace_malloc(),
//! tmc_mspace_free(), etc; freed memory will be available for re-allocation
//! through the usual mspace mechanisms.
//!
//! On Tile systems, homecaching support is available to set the home
//! cache of pages of memory.  This is supported via the
//! tmc_shmem_set_page_home() API, which takes a pointer, a range of memory,
//! and a mmap "flags" value to use to set the home.  The shmem arena
//! itself must not have been opened with any explicit homecache flags,
//! or the tmc_shmem_set_page_home() API will fail with errno of EINVAL.
//! Similarly, the mmap "flags" must only specify MAP_CACHE_HOME_xxx
//! values or the call will fail with errno of EINVAL.
//!
//! When a process is done using an arena, it can call tmc_shmem_close(),
//! or it can simply allow the operating system to clean up its use of
//! the arena at process exit time.
//!
//! When access to an arena is no longer needed, it can be unlinked by
//! calling tmc_shmem_unlink().  Note that a call to tmc_shmem_unlink() is
//! safe while the arena is in use, and the actual (now-unnamed) file and
//! associated memory will remain reserved until the last process calls
//! tmc_shmem_close() or exits.  In fact, this pattern can be useful
//! if a fixed set of processes wishes to share some memory, but then
//! forbid any other processes from gaining access to the file; in that
//! case after all the tmc_shmem_open() calls are complete, one process
//! can call tmc_shmem_unlink() to prohibit any other access to the arena.
//!

#ifndef __TMC_SHMEM_H__
#define __TMC_SHMEM_H__

#include <stddef.h>
#include <tmc/alloc.h>
#include <tmc/sync.h>

__BEGIN_DECLS

struct tmc_shmem;

//! Handle for TMC shmem arena.
typedef struct tmc_shmem tmc_shmem_t;

//! Create a new arena of shared memory.
//!
//! This call creates a new file at the given PATH.  For default small-page
//! allocations that file is the backing store for the memory arena.  For
//! huge-page allocation, a separate file is used in the appropriate
//! hugetlbfs filesystem.  The file is created with default owner and group
//! and permissions 0600 (owner-only).  There must be no file with the
//! given name already, or the call fails with errno set to EEXIST.
//!
//! If the user passes ALLOC as NULL, a default tmc_alloc_t will be used.
//!
//! If the user specifies ADDR as zero, the system will choose an address,
//! and all subsequent users of the arena will automatically try to load it
//! at that address.  Note that it is generally better to pick an address
//! explicitly to avoid conflicts in other processes or in future runs of
//! the same program, by ensuring the arena is well away from all other
//! other mappings (for example shared objects, thread stacks, etc).
//! Doing so makes it more likely that all processes that wish to load
//! the arena at its required address can in fact do so.
//!
//! If the user specifies MAXSIZE as zero, the system will provide a
//! default 1 GB value for the maximum size.
//!
//! @param path Path to use for the arena.
//! @param alloc Pointer to tmc_alloc_t for allocation info, or NULL.
//! @param addr Base address to use for arena, or NULL.
//! @param maxsize Maximum size that the arena is expected to ever have, or 0.
//! @return Handle for new arena, or if NULL, then errno set.
//!
tmc_shmem_t* tmc_shmem_create(const char* path, const tmc_alloc_t* alloc,
                              void* addr, size_t maxsize);


//! Create a new arena of shared memory with a semi-random name.
//!
//! Application code should invoke tmc_shmem_create_temp() with PATH
//! being a "template" pointer to a character buffer ending with "XXXXXX";
//! see mkstemp(3).  On return, if the function succeeds, PATH will hold
//! the chosen filename with "XXXXXX" replaced by six random characters.
//!
//! @param path mkstemp()-style pathname template ending in "XXXXXX".
//! @param alloc Pointer to tmc_alloc_t for allocation info, or NULL.
//! @param addr Base address to use for arena, or NULL.
//! @param maxsize Maximum size that the arena is expected to ever have, or 0.
//! @return Handle for new arena, or if NULL, then errno set.
//!
tmc_shmem_t* tmc_shmem_create_temp(char* path, const tmc_alloc_t* alloc,
                                   void* addr, size_t maxsize);

//! Open an existing tmc_shmem arena and map it into memory.
//!
//! @param path Path of file holding tmc_shmem arena.
//! @return Handle to arena on success, or NULL and sets errno.
//!
tmc_shmem_t* tmc_shmem_open(const char* path);

//! Close an existing tmc_shmem arena.
//!
//! This will unmap the arena but leave the file(s) available for a
//! subsequent tmc_shmem_open().  The object pointed to by the tmc_shmem_t
//! pointer is no longer valid after a call to tmc_shmem_close().
//!
//! @param shmem Pointer to tmc_shmem arena.
//!
void tmc_shmem_close(tmc_shmem_t* shmem);

//! Unlink an existing tmc_shmem arena from the filesystem.
//!
//! Note that this can be called while the arena is mapped by one or more
//! processes (including the current process), and the mapping(s) will
//! continue to work, but no new processes will be able to map the file
//! into memory.  When the last process exits or explicitly closes the arena,
//! the memory will be reclaimed by the system.
//!
//! @param path Path of file holding tmc_shmem arena.
//! @return Zero on success, or -1 and sets errno.
//!
int tmc_shmem_unlink(const char* path);

//! Increase the allocated size for the shmem arena.  New zero-filled
//! pages will now be available starting from the old end of the arena.
//!
//! The requested LENGTH will be rounded up to the system page size and
//! a pointer to the start of the newly-allocated memory will be returned.
//!
//! @param shmem Pointer to tmc_shmem arena.
//! @param bytes_to_grow Number of bytes by which to grow the arena.
//! @return Pointer to allocated memory on success, or NULL and sets errno.
//!
void* tmc_shmem_grow(tmc_shmem_t* shmem, size_t bytes_to_grow);

//! Decrease the allocated size for the shmem arena.  Pointers pointing
//! at or above the new end of the arena are now invalid and derefencing
//! them will cause a SIGBUS.  The requested length (BYTES_TO_SHRINK) will
//! be rounded up to the system page size; the OFFSET must be a multiple
//! of page size.  Both OFFSET and BYTES_TO_SHRINK are required as a way
//! to guard against race conditions where tmc_shmem_grow() is called by
//! another process or thread at the same time as the call to
//! tmc_shmem_shrink(); if (OFFSET + BYTES_TO_SHRINK) is not equal to the
//! current size of the arena, the call fails and returns with errno set
//! to EINVAL.
//!
//! @param shmem Pointer to tmc_shmem arena.
//! @param offset Desired size of shmem arena after shrinking.
//! @param bytes_to_shrink Size in bytes of memory to be shrunk away at end.
//! @return Zero on success, or -1 and sets errno.
//!
int tmc_shmem_shrink(tmc_shmem_t* shmem, off_t offset, size_t bytes_to_shrink);

#ifdef __tile__
//! Change the home cache for page(s) from a tmc_shmem arena.
//! The HOME argument is the same as is used for tmc_alloc_set_home().
//! The requested LENGTH will be rounded up to the system page size.
//!
//! The memory will have its home set in accordance with the passed flags.
//! Note that this API is only effective if the initial mapping is made
//! without any MAP_CACHE_HOME_xxx bits specified, since otherwise that
//! initial homecache specification will override any attempts to modify
//! individual page mappings here.  (Even if you try to set the same
//! homecache, it will still fail, for simplicity.)
//!
//! Using this API, different parts of the same arena can end up with
//! different homes, at page granularity.
//!
//! @param shmem Pointer to tmc_shmem arena.
//! @param address Pointer to start of region whose home is to be changed.
//! @param length Size in bytes of allocation requested.
//! @param home A TMC_ALLOC_HOME_xxx value to specify the home cache.
//! @return Zero on success, or -1 and sets errno.
//!
int tmc_shmem_set_page_home(tmc_shmem_t* shmem, void* address, size_t length,
                            int home);
#endif // __tile__

//! Return the file descriptor of the primary file.
//! For a tmc_shmem arena of default-size pages, this is the only file;
//! for a huge-page file, this is the control file for the arena.
int tmc_shmem_fd(tmc_shmem_t* shmem);

//! Return the file descriptor of the secondary hugetlbfs file.
//! For a tmc_shmem arena of hugepages, this is where the application data is.
//! For an arena of default-size pages, this API will return -1.
int tmc_shmem_hugefd(tmc_shmem_t* shmem);

//! Return the current allocated size of the shmem arena.
size_t tmc_shmem_current_size(tmc_shmem_t* shmem);

//! Return the maximum possible size of the shmem arena.
size_t tmc_shmem_maximum_size(tmc_shmem_t* shmem);

//! Return the page size of the shmem arena.
size_t tmc_shmem_page_size(tmc_shmem_t* shmem);

//! Return the base address of the shmem arena.
void *tmc_shmem_address(tmc_shmem_t* shmem);

//! Return the tmc_alloc_t associated with this shmem arena.
//! It can then be passed to tmc_mspace routines, etc. as desired.
//! The "mmap_mbind" function of the resulting alloc will grow the shmem
//! arena by the requested amount; the "munmap" function will shrink
//! the arena if page(s) at the end of the arena are unmapped, otherwise
//! returning an error; and the "mremap" function simply returns an error.
//! (Note that the returned pointer points into the tmc_shmem_t object.)
tmc_alloc_t* tmc_shmem_alloc(tmc_shmem_t* shmem);

//! Set a pointer to a data item in the arena.
//! This could be a pointer to a data structure that the application
//! can use to find the rest of the data in the arena.
void tmc_shmem_set_data(tmc_shmem_t* shmem, void* data);

//! Retrieve a pointer to the specified data item in the arena.
//! See tmc_shmem_set_data() for how to set the data pointer.
void* tmc_shmem_get_data(tmc_shmem_t* shmem);

__END_DECLS

#endif // __TMC_SHMEM_H__

//! @}
