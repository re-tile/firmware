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
//! The malloc replacement library using tmc_mspace.

//! @addtogroup tmc_malloc
//! @{
//!
//! Support for mixing and matching TMC "mspace" library allocation
//! routines with the standard malloc and free API routines.
//!
//! The "tmc_malloc" library allows users to mix and match the TMC
//! "mspace" library allocation routines with the standard malloc
//! and free API routines.  The usual libc routines are not compatible
//! with TMC's mspace routines, so users have to be careful which
//! API to use for any given object.  The tmc_malloc library overrides
//! libc's malloc() to make it use the TMC allocator, so objects can be
//! freed and reallocated without concern for their original source.
//!
//! To enable this library, you should normally just link with it
//! on the command line, via "-ltmc_malloc".  You may also preload
//! it into dynamically-linked executables by setting the
//! environment varibles "LD_PRELOAD=libtmc_malloc.so".  In the
//! latter case, it's possible that some glibc allocations may
//! already have occurred, in which case if they are passed to free()
//! or realloc() they will be leaked (safely, but with a warning).
//!
//! The primary functions that are interposed on by this library are
//! malloc(), calloc(), memalign(), posix_memalign(), realloc(), and free().
//! To support static linking with glibc and avoid linker conflicts, the
//! library also implements some less common glibc malloc functions:
//! cfree(), mallinfo(), mallopt(), pvalloc(), valloc(), and the
//! various malloc_xxx() functions (malloc_get_state, malloc_set_state,
//! malloc_info, malloc_stats, malloc_trim, and malloc_usable_size).
//! Not all of these functions may be fully implemented.
//!
//! The library provides a heap consisting of per-thread mspaces whose
//! homecache and page-size behavior is the same as normal glibc (or mmap).
//! The library checks for the HUGETLB_MORECORE environment variable, and
//! if present, will use huge pages for the heap.  (This is the same
//! environment variable used by libhugetlbfs and set by "hugectl --heap".)
//!
//! Note that in earlier (2.x) releases of the Tilera MDE, libc itself
//! provided malloc functionality that worked directly with mspaces.
//! The defaults for that old libc were slightly different than for
//! this library: in particular, this library always defaults to
//! per-thread mspaces unless huge pages are requested, whereas in the
//! old libc, a single global mspace was the default when
//! hash-for-home memory was provided for heap by default.
//! Also, the 2.x library leaked the per-thread mspaces by default,
//! whereas this library reuses the per-thread mspaces by default.

#ifndef __TMC_MALLOC_H__
#define __TMC_MALLOC_H__

#include <malloc.h>
#include <features.h>

__BEGIN_DECLS

//! Force the tmc_malloc library to be used at runtime.
//!
//! Normally application code can choose at link- or load-time whether
//! to use the tmc_malloc library.  By adding a call to this routine
//! (which in fact does nothing) somewhere in your code, you can force
//! a requirement that the code be linked against (or loaded with)
//! the tmc_malloc library.
//!
extern void tmc_malloc_require_library(void);

__END_DECLS

/// @}

#endif // __TMC_MALLOC_H__
