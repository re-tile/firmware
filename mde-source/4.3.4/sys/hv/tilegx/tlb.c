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
 * Routines dealing with the TLBs.
 */

#include <stdio.h>
#include <string.h>

#include <arch/spr.h>

#include "debug.h"
#include "hv.h"
#include "misc.h"
#include "tlb.h"
#include "tte.h"


/** Search the DTLB for an entry which matches the given VA, and set the
 *  writable bit on that entry.
 * @param va Virtual address to match.
 * @param asid ASID to match.
 * @return 1 if the TTE was found and updated, else 0.
 */
int
search_dtlb_and_set_writable(VA va, Asid asid)
{
  unsigned long match_bits = dtlb_probe(va);

  while (match_bits)
  {
    tte_w0_t w0;

    int i = __insn_ctz(match_bits);
    LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);

    //
    // The probe instruction has verified that the entry is valid, and that
    // it matches the given VA.  However, it doesn't pay attention to the
    // ASID field, so we need to verify that.
    //
    if (w0.g || w0.asid == asid)
    {
      w0.w = 1;
      __insn_mtspr(SPR_DTLB_CURRENT_ATTR, w0.word);
      return (1);
    }

    match_bits &= match_bits - 1;
  }
  return (0);
}


/** Dump a TTE.
 * @param tte TTE to dump.
 * @param index Index of the TTE in the TLB or TSB it's from.
 * @param dump_bits If true, dump the raw TTE bits.
 */
void
dump_tte(tte_t tte, int index, int dump_bits)
{
// Example output:
//  V   0123456789abcdef/16M  > 0123456789 ASID 00G PL0 RW  MMIO   (A,0) ----
//    I 0123456789abcdef/64K  > 0123456789 ASID 00  PL1  WX UnCa   (0,0) ----
//  V   0123456789abcdef/256M > 0123456789 ASID 00G PL2 RW  CCoh L=(2,7) --C-
//    I 0123456789abcdef/1G   > 0123456789 ASID 00  PL3 RW  CNco H &3 +1 P1-A

  static const char* const pagesizes[] = { "4K  ", "16K ", "64K ", "256K",
                                           "1M  ", "4M  ", "16M ", "64M ",
                                           "256M", "1G  ", "4G  ", "16G ",
                                           "64G ", "?13?", "?14?", "?15?" };

  static const char* const memattr[] =   { "CCoh", "CNco", "UnCa", "MMIO" };




  const char* v = tte.w0.v ? "V  " : "  I";

  const char* g = tte.w0.g ? "G" : " ";

  char lbuf[16];
  switch (tte.w0.memory_attribute)
  {
  case SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT:
  case SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT:
    if (tte.w0.cache_home_mapping ==
        SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE)
      snprintf(lbuf, sizeof (lbuf), "L=(%X,%X)",
               tte.w0.location_x_or_page_mask,
               tte.w0.location_y_or_page_offset);
    else if (tte.w0.cache_home_mapping ==
             SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_HASH)
      snprintf(lbuf, sizeof (lbuf), "H &%X +%X",
               tte.w0.location_x_or_page_mask,
               tte.w0.location_y_or_page_offset);
    else
      snprintf(lbuf, sizeof (lbuf), "?%X ?%X ?%X",
               tte.w0.cache_home_mapping,
               tte.w0.location_x_or_page_mask,
               tte.w0.location_y_or_page_offset);

    break;

  case SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE:
    strcpy(lbuf, "       ");
    break;

  case SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_MMIO:
    snprintf(lbuf, sizeof (lbuf), "(%2u,%2u)",
             tte.w0.location_x_or_page_mask,
             tte.w0.location_y_or_page_offset);
    break;
  }

  const char* r = tte.w0.dtlbv ? "R" : " ";
  const char* w = tte.w0.w ? "W" : " ";
  const char* x = tte.w0.itlbv ? "X" : " ";
  const char* pi = tte.w0.pin ? "P" : "-";
  const char* l1 = tte.w0.no_l1d_allocation ? "-" : "1";
  const char* cp = tte.w0.cache_prefetch ? "C" : "-";
  const char* ad = tte.w0.adaptive_allocation ? "A" : "-";




  tprintf("%4x: %3s %011llX/%s > %010llX ASID %02X%s "

          "PL%X %s%s%s %s %s %s%s%s%s\n", index, v,
          tte.w1.word & 0xFFFFFFFFFFF, pagesizes[tte.w0.ps], tte.w2.word,
          tte.w0.asid, g, tte.w0.mpl, r, w, x,
          memattr[tte.w0.memory_attribute], lbuf, pi, l1, cp, ad);

  if (dump_bits)
    utprintf("   0x%'19lx 0x%'019lx 0x%'019lx\n", tte.w0.word, tte.w1.word,
             tte.w2.word);
}

/** Capture the main processor instruction TLB.
 * @param state State structure to hold the TLB contents.
 */
void
save_itlb(struct itlb_state* state)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_ITLB);
  if (nent > CHIP_ITLB_ENTRIES())
    panic("impossibly large ITLB: %d", nent);

  for (i = 0; i < nent; i++)
    state->tte[i] = READ_TLB_AT(I, i);
}

/** Dump the saved main processor instruction TLB.
 * @param state State structure holding the TLB contents.
 * @param title Optional title to print in dump header (can be NULL).
 */
void
dump_saved_itlb(const struct itlb_state* state, char* title)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  tprintf("ITLB dump%s: %d entries, %lld wired\n", (title) ? title : "", nent,
          __insn_mfspr(SPR_WIRED_ITLB));

  for (i = 0; i < nent; i++)
    dump_tte(state->tte[i], i, 0);
}

/** Capture the data TLB.
 * @param state State structure to hold the TLB contents.
 */
void
save_dtlb(struct dtlb_state* state)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_DTLB);
  if (nent > CHIP_DTLB_ENTRIES())
    panic("impossibly large DTLB: %d", nent);

  for (i = 0; i < nent; i++)
    state->tte[i] = READ_TLB_AT(D, i);
}

/** Dump the saved data TLB.
 * @param state State structure holding the TLB contents.
 * @param title Optional title to print in dump header (can be NULL).
 */
void
dump_saved_dtlb(const struct dtlb_state* state, char* title)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  tprintf("DTLB dump%s: %d entries, %lld wired\n", (title) ? title : "", nent,
          __insn_mfspr(SPR_WIRED_DTLB));

  for (i = 0; i < nent; i++)
    dump_tte(state->tte[i], i, 0);
}

/** Zero out the main processor instruction TLB.
 * @param clean_wired Iff nonzero, zero out the wired TLB entries.
 */
void
clean_itlb(int clean_wired)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_ITLB);
  int firstent = clean_wired ? 0 : __insn_mfspr(SPR_WIRED_ITLB);

  for (i = firstent; i < nent; i++)
    WRITE_TLB_AT(I, i, TTE_ZERO);
  if (clean_wired)
    __insn_mtspr(SPR_WIRED_ITLB, 0);
}

/** Zero out the data TLB.
 * @param clean_wired Iff nonzero, zero out the wired TLB entries.
 */
void
clean_dtlb(int clean_wired)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_DTLB);
  int firstent = clean_wired ? 0 : __insn_mfspr(SPR_WIRED_DTLB);

  for (i = firstent; i < nent; i++)
    WRITE_TLB_AT(D, i, TTE_ZERO);
  if (clean_wired)
    __insn_mtspr(SPR_WIRED_DTLB, 0);
}

/** Install a new wired data TLB entry.
 * @param attr The attributes of the TLB entry to be installed.
 * @param va The VA of the TLB entry to be installed.
 * @param pa The PA of the TLB entry to be installed.
 * @return 0 on success, non-zero if no more wired entries are available.
 */
int
install_wired_tte(tte_w0_t attr, VA va, PA pa)
{
  uint_reg_t crit_sec_save = UPDATE_TLB_START();

  uint_reg_t nwired = __insn_mfspr(SPR_WIRED_DTLB);

  // Always keep around at least one unwired entry.
  if (nwired + 1 >= CHIP_DTLB_ENTRIES())
  {
    UPDATE_TLB_FINISH(crit_sec_save);
    return (1);
  }

  __insn_mtspr(SPR_WIRED_DTLB, nwired + 1);
  __insn_mtspr(SPR_DTLB_INDEX, nwired);
  tte_t tte = { .w0 = attr, .w1 = { .word = va }, .w2 = { .word = pa }};
  WRITE_TLB(D, tte);

  UPDATE_TLB_FINISH(crit_sec_save);

  return (0);
}

/** Install a new wired data TLB entry.
 * @param va Virtual address for the entry.
 * @param pa Physical address for the entry.
 * @param ps Page size for the entry (TLB page size code, not number of bytes).
 * @param mode One of the HV_PTE_MODE_ values.
 * @param lotar Tile on which to cache the page, if required by mode.
 * @return 0 on success, non-zero if no more wired entries are
 * available or mode couldn't be applied.
 */
int
install_wired_mapping(VA va, PA pa, int ps, int mode, Lotar lotar)
{
  // Start with a HV_PTE_MODE_CACHE_TILE_L3 mapping.
  tte_w0_t attr =
  {{
    .ps = ps,
    .g = 1,
    .asid = 0,
    .v = 1,
    .w = 1,
    .mpl = HV_PL,
    .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
    .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
    .location_x_or_page_mask = HV_LOTAR_X(lotar),
    .location_y_or_page_offset = HV_LOTAR_Y(lotar)
  }};

  switch(mode)
  {
  case HV_PTE_MODE_UNCACHED:
    attr.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE;
    break;

  case HV_PTE_MODE_CACHE_NO_L3:
    attr.location_x_or_page_mask = HV_LOTAR_X(my_lotar);
    attr.location_y_or_page_offset = HV_LOTAR_Y(my_lotar);
    break;

  case HV_PTE_MODE_CACHE_TILE_L3:
    break;

  case HV_PTE_MODE_CACHE_HASH_L3:
    attr.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_HASH;
    attr.location_x_or_page_mask = 0xF;
    attr.location_y_or_page_offset = 0;
    break;

  default:
    return (1);

  }
  return install_wired_tte(attr, va, pa);
}

/** Remove the last wired data TLB entry.
 * @return 0 on success, non-zero if only the hypervisor entry remained.
 */
int
remove_wired_tte()
{
  uint_reg_t crit_sec_save = UPDATE_TLB_START();

  uint_reg_t nwired = __insn_mfspr(SPR_WIRED_DTLB);

  // Always keep around the first wired entry (which belongs to the
  // hypervisor itself)
  if (nwired <= 1)
  {
    UPDATE_TLB_FINISH(crit_sec_save);
    return (1);
  }

  __insn_mtspr(SPR_WIRED_DTLB, nwired - 1);
  __insn_mtspr(SPR_DTLB_INDEX, nwired - 1);
  __insn_mtspr(SPR_DTLB_CURRENT_ATTR, 0);

  UPDATE_TLB_FINISH(crit_sec_save);

  return (0);
}

/** Remove the last wired data TLB entry, if the provided VA matches that
 *  entry.
 * @return 0 on success, non-zero if only the hypervisor entry remained or
 *  if the VA did not match.
 */
int
remove_wired_tte_va(VA va)
{
  uint_reg_t crit_sec_save = UPDATE_TLB_START();

  uint_reg_t nwired = __insn_mfspr(SPR_WIRED_DTLB);

  // Always keep around the first wired entry (which belongs to the
  // hypervisor itself)
  if (nwired <= 1)
  {
    UPDATE_TLB_FINISH(crit_sec_save);
    return (1);
  }

  LOAD_TLB(D, nwired - 1);
  VA entry_va = __insn_mfspr(SPR_DTLB_CURRENT_VA);

  if (entry_va != va)
  {
    UPDATE_TLB_FINISH(crit_sec_save);
    return (1);
  }
  else
  {
    __insn_mtspr(SPR_WIRED_DTLB, nwired - 1);
    __insn_mtspr(SPR_DTLB_CURRENT_ATTR, 0);
    UPDATE_TLB_FINISH(crit_sec_save);
    return (0);
  }
}
