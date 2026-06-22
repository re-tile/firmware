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

#include <arch/cycle.h>

// Initial cycle delay for exponential backoff.  Our stall mechanism
// is an instruction that takes 5 (tilegx) or 6 (tilepro) cycles, and
// looping around it takes 6 (tilegx) or 8 (tilepro) cycles, so this
// constant corresponds to 16 (tilegx) or 12 (tilepro) cycles, but
// note that the crc32 step can up to almost double the cycle count.
#define BACKOFF_START 2
#ifdef __tilegx__
#define CYCLES_PER_RELAX_LOOP 6
#else
#define CYCLES_PER_RELAX_LOOP 8
#endif

// Idle the core for 8 * iterations cycles.
static __inline void
relax(int iterations)
{
#ifdef __tile__
  for (/*above*/; iterations > 0; iterations--)
    __insn_mfspr(SPR_PASS);
#else
  for (/*above*/; iterations > 0; iterations--)
    asm("nop");
#endif
}

// Perform bounded exponential backoff.  The 'backoff' parameter is
// the number of times we've tried and failed to get the resource.
static __inline void
exp_backoff(unsigned int backoff)
{
  // Backoff up to 2048 cycles.
  int loops =
    (backoff > 6) ? (BACKOFF_START << 6) : (BACKOFF_START << backoff);

#ifdef __tile__
  // Prevent cpus from falling in lock step.
  register const unsigned long sp asm("sp");
  loops += __insn_crc32_32(sp, get_cycle_count_low()) & (loops - 1);
#endif

  relax(loops);
}
