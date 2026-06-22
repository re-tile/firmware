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

#include "tmc/mem.h"

#include <unistd.h>
#include <arch/chip.h>
#include <arch/icache.h>
#include <sys/syscall.h>
#include <asm/cachectl.h>


// Return a pointer to the first cache line which the buffer is on.
static inline char*
l2_aligned_start(const void* buffer)
{
  return (char*) ((long)buffer & -CHIP_L2_LINE_SIZE());
}


// Return a pointer to the start of the cache line following the buffer.
static inline char*
l2_aligned_end(const void* buffer, size_t size)
{
  unsigned long finish = (long)buffer + size;
  return (char*) ((finish + CHIP_L2_LINE_SIZE() - 1) & -CHIP_L2_LINE_SIZE());
}


void
tmc_mem_flush_no_fence(const void* buffer, size_t size)
{
  char *next = l2_aligned_start(buffer);
  char *finish = l2_aligned_end(buffer, size);
  while (next < finish)
  {
    __insn_flush(next);
    next += CHIP_FLUSH_STRIDE();
  }
}


void
tmc_mem_finv_no_fence(const void* buffer, size_t size)
{
  char *next = l2_aligned_start(buffer);
  char *finish = l2_aligned_end(buffer, size);
  while (next < finish)
  {
    __insn_finv(next);
    next += CHIP_FINV_STRIDE();
  }
}


#ifndef __tilegx__
void
tmc_mem_inv_no_fence(void* buffer, size_t size)
{
  char *next = l2_aligned_start(buffer);
  char *finish = l2_aligned_end(buffer, size);
  while (next < finish)
  {
    __insn_inv(next);
    next += CHIP_INV_STRIDE();
  }
}
#endif


void
tmc_mem_write_hint(void* buffer, size_t size)
{
  char *next = l2_aligned_start(buffer);
  char *finish = l2_aligned_end(buffer, size);
  while (next < finish)
  {
    __insn_wh64(next);
    next += 64;
  }
}


void
tmc_mem_prefetch(const void* buffer, size_t size)
{
  char *next = l2_aligned_start(buffer);
  char *finish = l2_aligned_end(buffer, size);
  while (next < finish)
  {
#ifdef __tilegx__
    __insn_prefetch_l2_fault(next);
#else
    // The flush instruction generates a TLB fault, and the prefetch brings
    // the line into the L2.  We could also issue "__insn_prefetch_l1()" in
    // a loop and avoid the flush, but that would pollute the L1 cache.
    __insn_flush(next);
    __insn_prefetch(next);
#endif
    next += CHIP_L2_LINE_SIZE();
  }
}


void
tmc_mem_invalidate_icache(const void* addr, size_t size)
{
  invalidate_icache(addr, size, getpagesize());
}


void
tmc_mem_flush_l2(void)
{
  (void) syscall(SYS_cacheflush, 0, 0, DCACHE);
}
