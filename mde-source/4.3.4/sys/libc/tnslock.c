/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

/**
 * @file
 * Simple tns-based lock.
 */

// From "sys/libc/include/".
#include <tnslock.h>

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/spr.h>


void
tnslock_lock(void* object, size_t size, int* lock)
{
  // Check that the object being guarded is properly aligned.
  // If it may cross L2 cache lines, we may have some other object
  // trying to do locking on intersecting cache lines, which will
  // cause it all to fall apart.
  assert((size & (CHIP_L2_LINE_SIZE()-1)) == 0);
  assert(((uintptr_t) object & (CHIP_L2_LINE_SIZE()-1)) == 0);
  
  // Strictly speaking, it is unnecessary to finv() before we access
  // this object, since we finv on the way out when we unlock.
  // However, if a stray read-pointer were to reference the
  // line(s) holding the object, we would have it live in our cache
  // (and thus incoherent) on entry.  Better to do the low-overhead
  // finv in all the cases where we expect it to be already invalid in
  // the cache anyway.
  for (size_t i = 0; i < size; i += CHIP_FINV_STRIDE())
    __insn_finv((char*)object+i);

  // No locking being done yet, so nothing to do.
  if (lock == NULL)
    return;

  tnslock_rawlock(lock);
}


#ifndef __DOXYGEN__
// Initial cycle delay for exponential backoff.  Our stall mechanism
// is an instruction that takes 5 or 6 cycles, and looping around it takes
// one more, so this constant corresponds to 12 or 14 cycles, but
// note that the crc32 step can up to almost double the cycle count.
#define BACKOFF_START 2
#endif

// Idle the core for 6 or 7 times iterations cycles.
static __inline void
relax(int iterations)
{
#pragma unroll 0
  for (/*above*/; iterations > 0; iterations--)
    __insn_mfspr(SPR_PASS);

}

// Perform bounded exponential backoff.  The 'backoff' parameter is
// the number of times we've tried and failed to get the resource.
static __inline void
exp_backoff(unsigned int backoff)
{
  // Backoff up to 2048 cycles.
  int loops =
    (backoff > 6) ? (BACKOFF_START << 6) : (BACKOFF_START << backoff);

  // Prevent cpus from falling in lock step.
  register unsigned long sp asm("sp");
  loops += __insn_crc32_32(sp, get_cycle_count_low()) & (loops - 1);
  relax(loops);
}


void 
tnslock_rawlock(int* lock)
{
  int backoff = 0;

  while (__insn_exch4(lock, 1) != 0)
    exp_backoff(backoff++);
}
  
void
tnslock_unlock(void* object, size_t size, int* lock)
{
  for (size_t i = 0; i < size; i += CHIP_FINV_STRIDE())
    __insn_finv((char*)object+i);
  __insn_mf();

  if (lock)
    *(volatile int*)lock = 0;
}
