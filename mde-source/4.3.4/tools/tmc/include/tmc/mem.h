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
//! Support for moving blocks of memory in and out of a Tile's cache.
//!

//! @addtogroup tmc_mem
//! @{
//!
//! Support for moving blocks of memory in and out of a core's cache.
//!
//! @ifnot __tilegx__
//! The Tile Processor supports both coherent and incoherent memory
//! models.  Coherent shared memory provides the shared memory model
//! familiar to most programmers working in pthreads environments -
//! loads and stores behave as if all cores are accessing one global
//! memory scratchpad.  The incoherent memory model allows each core
//! to keep its own copy of a memory location, so that writes to that
//! address might never be visible to other cores.
//! @endif
//!
//! @section tmc_mem_coherent Working With Coherent Memory
//!
//! Most parallel algorithms are written to work with coherent shared
//! memory.  When writing such algorithms, remember
//! that the Tile Processor implements a relaxed memory model.  In
//! order to guarantee that a store operation to a coherent memory
//! address is visible to other tiles, the core that issued the store
//! instruction must perform a "memory fence".  The coherent memory
//! fence operation, provided by tmc_mem_fence(), blocks the processor
//! from issuing any other instructions until all previous stores are
//! visible to all other cores.
//!
//! The memory fence operation is particularly important when
//! implementing shared memory synchronization algorithms.  Suppose
//! core A wants to write a data structure to coherent shared memory
//! and then set a flag telling core B that the data is ready to be
//! consumed.  A memory fence is required between the data structure
//! store instructions and the flag store instruction; otherwise the
//! relaxed memory model might allow core B to see the "data is ready"
//! flag while stores to the data structure are still in flight.
//!
//! In general, we recommend that application developers avoid this
//! kind of low-level shared memory algorithm development.  The MDE
//! provides the standard pthreads synchronization mechanisms as well
//! as some TMC extensions.  These provided primitives should be
//! adequate for many applications.
//!
//!
//! @ifnot __tilegx__
//! @section tmc_mem_incoherent Working with Incoherent Memory
//!
//! The Tile Processor also allows applications to allocate incoherent
//! memory.  Incoherent memory allows each core to keep its own,
//! locally-cached version of a memory address without automatically
//! synchronizing that copy with any other core.  Thus, a store by
//! core A to an incoherent address cannot be guaranteed to be visible
//! to core B unless core A flushes the new value out to DRAM and core
//! B then reloads its copy from DRAM.  Working with this memory model
//! presents more of a challenge than using coherent memory.
//!
//! Incoherent memory accesses are most frequently used when
//! interacting with I/O devices.  On TILE64, the I/O shims can only
//! read memory values from DRAM, so applications must flush I/O data
//! to memory before posting it to egress.  Similarly, an application
//! must invalidate any locally cached copies of a memory address
//! before receiving an ingress packet.  See the NetIO API Reference
//! (UG212) for more information on working with I/O devices and
//! incoherent memory.  The TILE@e Pro I/O shims support direct-to-cache
//! memory accesses, so applications developed for TILE@e Pro are not
//! required to deal with incoherent memory at all.
//!
//! TMC provides several helper functions used for flushing or invalidating
//! locally cached copies of incoherent memory.  The tmc_mem_flush()
//! function flushes all cachelines in the specified memory buffer
//! back to DRAM, making any store instructions to that data visible
//! to all other cores and I/O devices.  The tmc_mem_finv() function
//! flushes the specified cachelines back to DRAM if they are dirty,
//! then invalidates the cache lines locally.
//!
//! Both of these routines (tmc_mem_flush() and tmc_mem_finv())
//! finish with implicit calls to tmc_mem_fence_incoherent().
//! This function guarantees that all previous flush
//! or invalidate operations have completed.  Thus, the
//! flush operation causes a data packet to be sent from the local
//! cache to main memory, and tmc_mem_fence_incoherent() guarantees
//! that that data packet has actually arrived at the memory
//! controller.  Just as coherent memory algorithms must use
//! tmc_mem_fence() to guarantee that data is visible to other cores
//! before telling those cores to read the data values, incoherent
//! memory algorithms must use tmc_mem_fence_incoherent() to guarantee
//! that flushed cachelines are visible to other cores or I/O devices.
//! Note that tmc_mem_fence_incoherent() does not work correctly if
//! the memory in question is "homed" anywhere else on the chip via
//! a remote L3 cache, either to some other tile or using TILE@e Pro's
//! hash-for-home functionality; the memory must be homed on the local
//! core only, i.e. incoherent.  
//!
//! TMC also provides tmc_mem_flush_no_fence() and
//! tmc_mem_finv_no_fence() operations for use by applications that
//! need to, for example, flush several buffers and then perform a
//! single tmc_mem_fence_incoherent() afterwards.
//! @endif
//!
//!
//! @section tmc_mem_other Other Functionality
//!
//! The tmc_mem_prefetch() function allows applications to prefetch
//! data (or code) into a core's L2 cache in order to avoid cache
//! misses or page faults later. 
//!


#ifndef __TMC_MEM_H__
#define __TMC_MEM_H__

#include <stddef.h>
#include <features.h>

#include <arch/cycle.h>
#include <arch/chip.h>
#include <arch/spr.h>
#include <arch/inline.h>

__BEGIN_DECLS


//! Fence to guarantee visibility of stores to cache coherent memory.
//!
//! Newer gccs support this as the __sync_synchronize() primitive as well.
//!
static __USUALLY_INLINE void
tmc_mem_fence(void)
{
  __sync_synchronize();
}


//! Flush a region of memory, but do not fence.
//!
//! This function flushes the specified region of memory, writing it
//! back to memory without removing it from the cache.  The actual
//! range of memory flushed is padded to a cache line boundary on both
//! ends.  This routine does not perform any memory fence operation,
//! so some victim cachelines might not have reached main memory
//! when this function returns.
//!
//! @param addr Start of region to be flushed.
//! @param size Number of bytes to be flushed.
//!
extern void
tmc_mem_flush_no_fence(const void* addr, size_t size);


//! Flush and invalidate a region of memory, but do not fence.
//!
//! This function flushes and invalidates the specified region of
//! memory, writing it back to memory and removing it from the cache.
//! The actual range of affected memory is padded to a cache line
//! boundary on both ends.  This routine does not perform any
//! memory fence operation, so some victim cachelines might not have
//! reached main memory when this function returns.
//!
//! @param addr Start of region to be flushed and invalidated.
//! @param size Number of bytes to be flushed and invalidated.
//!
extern void
tmc_mem_finv_no_fence(const void* addr, size_t size);


#ifndef __tilegx__
//! Fence to guarantee visibility of stores to incoherent memory.
//!
//! Note that this routine does not work for memory that has an L3
//! cache home elsewhere on the chip, i.e. either homed remotely on
//! another core, or homed using hash-for-home.  The memory must be
//! homed on the calling core, as is the case, for example,
//! with incoherent memory.
//!
static __inline void
tmc_mem_fence_incoherent(void)
{
  extern int __tmc_mem_sys_fence_incoherent(void);

  __insn_mf();
  
#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
  {
#if CHIP_HAS_TILE_WRITE_PENDING()
    const unsigned long WRITE_TIMEOUT_CYCLES = 400;
    uint_reg_t start = get_cycle_count_low();
    do
    {
      if (__insn_mfspr(SPR_TILE_WRITE_PENDING) == 0)
        return;
    }
    while ((get_cycle_count_low() - start) < WRITE_TIMEOUT_CYCLES);
#endif // CHIP_HAS_TILE_WRITE_PENDING()
    (void) __tmc_mem_sys_fence_incoherent();
  }
#endif // CHIP_HAS_MF_WAITS_FOR_VICTIMS()
}


//! Flush a region of memory.
//!
//! This function flushes the specified region of memory, writing it
//! back to memory without removing it from the cache.  The actual
//! range of memory flushed is padded to a cache line boundary on both
//! ends.  tmc_mem_fence_incoherent() is called internally before
//! returning.
//!
//! tmc_mem_flush() does not provide sufficient memory fence guarantee
//! if the memory in question is "homed" anywhere else on the chip via
//! a remote L3 cache, either to some other tile or using TILE@e Pro's
//! hash-for-home functionality; the memory must be homed on the local
//! core only, as is the case, for example, with incoherent memory.
//!
//! @param addr Start of region to be flushed.
//! @param size Number of bytes to be flushed.
//!
static __inline void
tmc_mem_flush(const void* addr, size_t size)
{
  tmc_mem_flush_no_fence(addr, size);
  tmc_mem_fence_incoherent();
}


//! Flush and invalidate a region of memory.
//!
//! This function flushes and invalidates the specified region of
//! memory, writing it back to memory and removing it from the cache.
//! The actual range of affected memory is padded to a cache line
//! boundary on both ends.  tmc_mem_fence_incoherent() is called
//! internally before returning.
//!
//! tmc_mem_finv() does not provide sufficient memory fence guarantee
//! if the memory in question is "homed" anywhere else on the chip via
//! a remote L3 cache, either to some other tile or using TILE@e Pro's
//! hash-for-home functionality; the memory must be homed on the local
//! core only, as is the case, for example, with incoherent memory.
//!
//! @param addr Start of region to be flushed and invalidated.
//! @param size Number of bytes to be flushed and invalidated.
//!
static __inline void
tmc_mem_finv(const void* addr, size_t size)
{
  tmc_mem_finv_no_fence(addr, size);
  tmc_mem_fence_incoherent();
}


//! Invalidate a region of memory, but do not fence.
//!
//! This function invalidates the specified region of memory, removing
//! it from the cache.  The actual range of affected memory is padded
//! to a cache line boundary on both ends.
//! 
//! This routine does not perform a tmc_mem_fence(), so the cache lines
//! may not have been invalidated when this function returns.  This is
//! appropriate if the calling cpu will be the only one accessing the memory;
//! otherwise, a tmc_mem_fence() must be issued to ensure the changes
//! are globally visible before continuing.
//!
//! @param addr Start of region to be invalidated.
//! @param size Number of bytes to be invalidated.
//!
extern void
tmc_mem_inv_no_fence(void* addr, size_t size);


//! Invalidate a region of memory.
//!
//! This function invalidates the specified region of memory, removing
//! it from the cache.  The actual range of affected memory is padded
//! to a cache line boundary on both ends.  tmc_mem_fence()
//! is called internally before returning.
//!
//! @param addr Start of region to be invalidated.
//! @param size Number of bytes to be invalidated.
//!
static __inline void
tmc_mem_inv(void* addr, size_t size)
{
  tmc_mem_inv_no_fence(addr, size);
  tmc_mem_fence();
}
#endif /* !__tilegx__ */


//! Write-hint a region of memory.
//!
//! This function notifies the processor that the caller intends to
//! write every byte of the specified range of memory before reading it.
//! The processor will allocate cache line(s), setting the cache contents
//! to some fixed value, without the overhead of reading any cache lines
//! from memory.  The actual range of affected memory is padded to a cache
//! line boundary on both ends.
//!
//! If the memory will be used by other cores, the caller should
//! call tmc_mem_fence() before notifying any other cores; as for any
//! kind of coherent memory update, this will ensure that the write hint
//! has reached the home and is globally visible.
//!
//! @param addr Start of region to be write-hinted.
//! @param size Number of bytes to be write-hinted.
//!
extern void
tmc_mem_write_hint(void* addr, size_t size);


//! Prefetch a region of memory.
//!
//! This function prefetches the specified region of memory, bringing
//! it into the data cache unless the memory is not cacheable.  The
//! actual range of memory prefetched is padded to a cache line
//! boundary on both ends.
//!
//! @if __tilegx__
//! The "prefetch_l2_fault" instruction is used on TILE-Gx to fault
//! the address range into the TLB as well as prefetching it into
//! the L2 cache.  Applications that would prefer to avoid this
//! fault, or that would like to prefetch into the L1 in addition,
//! or that would like to only prefetch into the L3, can issue
//! appropriate __insn_prefetch_XXX() instructions in a loop instead
//! of calling this function.
//! @else
//! The implementation faults the address range into the TLB before
//! prefetching.  Doing so avoids unexpected behavior due to the
//! semantics of the TILE prefetch instruction, which will be dropped
//! if the address is not present in the TLB.  Applications that
//! require the more speculative behavior of the prefetch instruction
//! can issue __insn_prefetch(addr) in a loop instead of calling this
//! function.
//! @endif
//!
//! @param addr Start of region to be prefetched.
//! @param size Number of bytes to be prefetched.
//!
extern void
tmc_mem_prefetch(const void* addr, size_t size);


//! Invalidate a block of memory in the instruction cache.
//!
//! This function must be called before executing self-modifying code.
//!
//! @param addr Start of region to be invalidated.
//! @param size Number of bytes to be invalidated.
//!
extern void
tmc_mem_invalidate_icache(const void* addr, size_t size);


//! Flush the contents of this core's L2 cache back to main memory.
//!
//! This method displaces all lines in the core's L2 cache, regardless
//! of address.  This operation is equivalent to reading in an amount
//! of memory equal in size to the cache, that is known not to be
//! already in the cache.
//!
//! @ifnot __tilegx__
//! If an application needs to flush buffers back to main memory and
//! those buffers are significantly larger than the capacity of the
//! level two cache, this method can be more efficient than passing
//! the buffer address(es) to tmc_mem_flush().
//! @endif
extern void
tmc_mem_flush_l2(void);

__END_DECLS

#endif // __TMC_MEM_H__

//! @}
