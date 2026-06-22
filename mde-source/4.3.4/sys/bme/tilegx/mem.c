// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

#include <stdint.h>
#include <stdio.h>
#include <util.h>

#include <arch/chip.h>
#include <arch/sim.h>

#include <bme/tlb.h>
#include <bme/tte.h>
#include <bme/types.h>

#include <tmc/mem.h>

#include "bits.h"
#include "bme_state.h"
#include "misc.h"
#include "param.h"

extern void
_bme_dump_tte(tte_t tte, int index, int dump_bits);

void
tmc_mem_flush_no_fence(const void* buffer, size_t size)
{
  char *next = (char*) ROUND_DN((uintptr_t)buffer, CHIP_L2_LINE_SIZE());
  char *finish = (char*) ROUND_UP((uintptr_t)buffer + size,
                                  CHIP_L2_LINE_SIZE());
  while (next < finish)
  {
    __insn_flush(next);
    next += CHIP_FLUSH_STRIDE();
  }
}


void
tmc_mem_finv_no_fence(const void* buffer, size_t size)
{
  char *next = (char*) ROUND_DN((uintptr_t)buffer, CHIP_L2_LINE_SIZE());
  char *finish = (char*) ROUND_UP((uintptr_t)buffer + size,
                                  CHIP_L2_LINE_SIZE());
  while (next < finish)
  {
    __insn_finv(next);
    next += CHIP_FINV_STRIDE();
  }
}


void
tmc_mem_inv_no_fence(void* buffer, size_t size)
{
  char *next = (char*) ROUND_DN((uintptr_t)buffer, CHIP_L2_LINE_SIZE());
  char *finish = (char*) ROUND_UP((uintptr_t)buffer + size,
                                  CHIP_L2_LINE_SIZE());
  while (next < finish)
  {
    __insn_inv(next);
    next += CHIP_INV_STRIDE();
  }
}


void
tmc_mem_prefetch(const void* buffer, size_t size)
{
  char *next = (char*) ROUND_DN((uintptr_t)buffer, CHIP_L2_LINE_SIZE());
  char *finish = (char*) ROUND_UP((uintptr_t)buffer + size,
                                  CHIP_L2_LINE_SIZE());
  while (next < finish)
  {
    // The flush instruction generates a TLB fault, and the prefetch brings
    // the line into the L2.  We could also issue "__insn_prefetch_l1()" in
    // a loop and avoid the flush, but that would pollute the L1 cache.
    __insn_flush(next);
    __insn_prefetch(next);
    next += CHIP_L2_LINE_SIZE();
  }
}

#define TSB_D 0
#define TSB_I 1

/** Validate that a TTE is legal.
 * If we are running with 4KB or 16KB client pages, we suspend
 * hardware validation so that we can tolerate VA-to-PA 16KB-aligned
 * mappings instead of the architecturally specified 64KB-aligned
 * mappings.  See the HV_CTX_PG_* flags for do_install_context() in
 * this file, and set_error_enable() in hw_config.c.
 *
 * @param t pointer to TTE we are validating.
 * @param tlb_type type of TLB entry (TSB_I or TSB_D).
 */
static void
validate_tte(tte_t *t, int tlb_type)
{
#if 0
  // If this is true, the hardware is validating anyway, so don't bother.
  if (page_size_small >= 65536)
    return;
#endif

  // The VA value must be legal (properly sign extended).
  // Note we use "intptr_t" instead of "VA" here so we get sign extension.
  intptr_t va = t->w1.word;
  int sign_bits = CHIP_WORD_SIZE() - CHIP_VA_WIDTH();
  if (((va << sign_bits) >> sign_bits) != va)
    panic("illegal TTE (VA not properly sign-extended: %#lx)", va);

  // Handle uncacheable or MMIO specially.
  switch (t->w0.memory_attribute)
  {
  case SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE:
  case SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_MMIO:
    if (tlb_type == TSB_I)
    {
      // Illegal type for ITLB.
      _bme_dump_tte(*t, -1, 1);
      panic("illegal TTE (ITLB with bad memory attribute)");
    }
    else
    {
      // Type requires no VA/PA alignment checking for DTLB.
      return;
    }
  }

  // For 4KB pages the VA and PA must be aligned to 16KB.
  if (t->w0.ps == TTE_PS_4K) {
    PA pa = t->w2.word;
    long mask = ((1 << (TTE_PG_SHIFT_16K - TTE_PG_SHIFT_4K)) - 1) << TTE_PG_SHIFT_4K;
    if ((va & mask) != (pa & mask))
      panic("illegal TTE (VA %#lx misaligned to PA %#llx)", va, pa);
  }
}

void
tmc_mem_flush_l2()
{
  //
  // What we want to do here is to just read in an L2-cache-size's worth of
  // data in order to evict other stuff out of the cache.  However, in
  // order to make sure we flush the whole cache, without relying on the
  // cache LRU bits (which are a performance optimization but aren't
  // necessarily functionally verified), we read in an L2 way's worth of
  // data at a time, forcing each way in turn to evict the data, using the
  // CACHE_PINNED_WAYS SPR.  In order to flush the L1 at the same time
  // without doing a lot of extra work, we're also going to use different
  // offsets within the L2 lines during various phases of the flush.  All
  // of this means that this will have to be re-done when the cache
  // organization is changed.
  //
  // The choice of which data to read is important here.  Obviously we
  // can't use the client's memory, since that might be what the client is
  // trying to flush.  It turns out, though, that we can't just use random
  // hypervisor memory, either.  If we were to pick something that happened
  // to already be in the L1, then when reading it, we'd hit in the L1, and
  // thus not evict whatever was in the L2.  (Similarly, we could hit in
  // one way of the L2 and not evict what we were trying to evict in the
  // other way.)  So, to make sure this doesn't happen, we reserve an area
  // of memory just for this purpose.  We actually alternate between two
  // separate L2-sized regions; that way we know that we won't hit
  // something that was left in the L1 from the last time we called this
  // routine.
  //

  _bme_state_t* bst = _bme_get_state();
  static VA flush_offset = 0;

#if CHIP_L2_LOG_LINE_SIZE() < CHIP_L1D_LOG_LINE_SIZE()
#error "L2 cache line smaller than L1D line; rework this routine"
#endif

#if CHIP_L2_CACHE_SIZE() < CHIP_L1D_CACHE_SIZE()
#error "L2 cache smaller than L1D; rework this routine"
#endif

  flush_offset ^= CHIP_L2_CACHE_SIZE();

  // What address will we load from?
  VA base_flush_va = bst->flush_va + bst->flush_offset;

  // Notify the --grind-coherence mechanism of what we are doing.
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_GRINDER_CLEAR);

  //
  // We need to temporarily stuff a new entry into the TLB to do our
  // flushing.  We pick the first unwired entry.  We also save the old
  // entry and restore it afterwards.  This is required by the hypervisor
  // TLB-usage convention.  Any code that modifies a TLB entry from an
  // asynchronous interrupt context must put it back the way it was when it
  // completes, so that arbitrary interrupts do not step on each other.
  // Thus if some other part of the hypervisor has also picked this same
  // TLB entry to use, either this code interrupts it, changes the TLB
  // entry, and restores it before the other code continues running, or
  // vice versa.  Likewise, just to be clean, we save and restore
  // CACHE_PINNED_WAYS.
  //
  int wired_idx = __insn_mfspr(SPR_WIRED_DTLB);
  BME_LOAD_TLB(D, wired_idx);
  uint64_t old_w0 = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);
  uint64_t old_w1 = __insn_mfspr(SPR_DTLB_CURRENT_VA);
  uint64_t old_w2 = __insn_mfspr(SPR_DTLB_CURRENT_PA);
  
  pos_t my_pos = { .word = __insn_mfspr(SPR_TILE_COORD) };

  tte_t tte = {
    .w0 = 
    {{
      .ps = bst->flush_pa >> 12,
      .g = 1,
      .asid = 0,
      .v = 1,
      .w = 1,
      .pin = 1,
      .mpl = 0,
      .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
      .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
      .location_x_or_page_mask = my_pos.bits.x,
      .location_y_or_page_offset = my_pos.bits.y,
    }},
    .w1 = { .word = base_flush_va },
    .w2 = { .word = bst->flush_pa }
  };

  validate_tte(&tte, TSB_D);
  __insn_mtspr(SPR_DTLB_CURRENT_VA, tte.w1.word);
  __insn_mtspr(SPR_DTLB_CURRENT_PA, tte.w2.word);
  __insn_mtspr(SPR_DTLB_CURRENT_ATTR, tte.w0.word);

  int old_cpw = __insn_mfspr(SPR_CACHE_PINNED_WAYS);
  int old_dstream_pf = __insn_mfspr(SPR_DSTREAM_PF);
  __insn_mtspr(SPR_DSTREAM_PF, 0);
  __insn_mf();
  VA flush_va = base_flush_va;

  for (int way = 0; way < CHIP_L2_ASSOC(); way++)
  {
    // Select which way to clear
    __insn_mf();
    SPR_CACHE_PINNED_WAYS_t new_cpw = {{ .mp_enable = 1 << way }};
    __insn_mtspr(SPR_CACHE_PINNED_WAYS, new_cpw.word);

    //
    // We use wh64 to create a new dirty line in the cache (evicting
    // whatever was there before), then load from that line to evict a line
    // in the L1 (installing a line with garbage data).  We expect to evict
    // all of the lines in the L1 as we make our way through the entire L2,
    // since the L2 is larger than the L1; this relies on the L1's LRU
    // algorithm working.
    //
    for (int i = 0; i < L2_WAY_SIZE/64; ++i, flush_va += 64)
    {
      __insn_wh64((void*) flush_va);
      *(volatile char*) flush_va;
    }
  }

  //
  // We now invalidate all of the lines we wh64'ed above, to flush
  // them out of the L1 and L2.
  //
  flush_va = base_flush_va;
  for (int i = 0; i < CHIP_L2_CACHE_SIZE()/64; ++i, flush_va += 64)
    __insn_inv((void*) flush_va);

#if 0
  // FIXME
  // Wait for the loads to finish and evict all the old data, and then wait
  // for the old data to reach memory.
  mf_incoherent();
#else
  __insn_mf();
#endif

  __insn_mtspr(SPR_CACHE_PINNED_WAYS, old_cpw);
  __insn_mtspr(SPR_DSTREAM_PF, old_dstream_pf);

  //
  // Restore the old TLB entry.
  //
  __insn_mtspr(SPR_DTLB_CURRENT_VA, old_w1);
  __insn_mtspr(SPR_DTLB_CURRENT_PA, old_w2);
  __insn_mtspr(SPR_DTLB_CURRENT_ATTR, old_w0);
}
