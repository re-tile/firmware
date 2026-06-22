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

#include <arch/spr.h>
#include <arch/spr_def.h>

#include <hv/hypervisor.h>

#include <bme/tlb.h>
#include <bme/tte.h>

#include "misc.h"


/** Dump a TTE.
 *  Example output:
 *  1234:  In 0x00000000/16M  -> 0x000000000 ASID 0x00/G PL0   XS C:RB   X,Y
 *  5678: Vl   0x00000000/16M  -> 0x000000000 ASID 0x71   PL3 RW     -- L=X,Y
 * @param tte TTE to dump (as a structure, not a pointer to one).
 * @param index Index of the TTE within the TLB, used just for labeling
 *        output.
 * @param dump_bits If nonzero, dump the raw TTE bits in addition to the
 *        interpreted fields.
 */
void
_bme_dump_tte(tte_t tte, int index, int dump_bits)
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
  case SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_MMIO:
    snprintf(lbuf, sizeof (lbuf), "  (%3u,%3u)",
             tte.w0.location_x_or_page_mask,
             tte.w0.location_y_or_page_offset);
    break;
  }

  const char* w = tte.w0.w ? "W" : " ";
  const char* pi = tte.w0.pin ? "P" : "-";
  const char* l1 = tte.w0.no_l1d_allocation ? "-" : "1";
  const char* cp = tte.w0.cache_prefetch ? "C" : "-";
  const char* ad = tte.w0.adaptive_allocation ? "A" : "-";




  tprintf("%4x: %3s %011llX/%s > %010llX ASID %02X%s "

          "PL%X %s %s %s %s%s%s%s\n", index, v,
          tte.w1.word & 0xFFFFFFFFFFF, pagesizes[tte.w0.ps], tte.w2.word,
          tte.w0.asid, g, tte.w0.mpl, w,
          memattr[tte.w0.memory_attribute], lbuf, pi, l1, cp, ad);

  if (dump_bits)
    utprintf("   0x%'19lx 0x%'019lx 0x%'019lx\n", tte.w0.word, tte.w1.word,
             tte.w2.word);
}


void
bme_dump_itlb(char* title)
{
  int i;
  int nent;
  tte_t tte;

  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  tprintf("ITLB dump%s: %d entries, %llu wired\n", (title) ? title : "", nent,
          __insn_mfspr(SPR_WIRED_ITLB));

  for (i = 0; i < nent; i++)
  {
    BME_LOAD_TLB(I, i);
    tte.w0.word = __insn_mfspr(SPR_ITLB_CURRENT_ATTR);
    tte.w1.word = __insn_mfspr(SPR_ITLB_CURRENT_VA);
    tte.w2.word = __insn_mfspr(SPR_ITLB_CURRENT_PA);

    _bme_dump_tte(tte, i, 0);
  }
}


void
bme_dump_dtlb(char* title)
{
  int i;
  int nent;
  tte_t tte;

  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  tprintf("DTLB dump%s: %d entries, %llu wired\n", (title) ? title : "", nent,
          __insn_mfspr(SPR_WIRED_DTLB));

  for (i = 0; i < nent; i++)
  {
    BME_LOAD_TLB(D, i);
    tte.w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);
    tte.w1.word = __insn_mfspr(SPR_DTLB_CURRENT_VA);
    tte.w2.word = __insn_mfspr(SPR_DTLB_CURRENT_PA);

    _bme_dump_tte(tte, i, 0);
  }
}


void
bme_clean_itlb(int clean_wired)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_ITLB);
  int firstent = clean_wired ? 0 : __insn_mfspr(SPR_WIRED_ITLB);


  for (i = firstent; i < nent; i++)
  {
    __insn_mtspr(SPR_ITLB_INDEX, i);
    __insn_mtspr(SPR_ITLB_CURRENT_VA, 0);
    __insn_mtspr(SPR_ITLB_CURRENT_PA, 0);
    __insn_mtspr(SPR_ITLB_CURRENT_ATTR, 0);
  }
  if (clean_wired)
    __insn_mtspr(SPR_WIRED_ITLB, 0);
}


void
bme_clean_dtlb(int clean_wired)
{
  int i;
  int nent;

  nent = __insn_mfspr(SPR_NUMBER_DTLB);
  int firstent = clean_wired ? 0 : __insn_mfspr(SPR_WIRED_DTLB);

  for (i = firstent; i < nent; i++)
  {
    __insn_mtspr(SPR_DTLB_INDEX, i);
    __insn_mtspr(SPR_DTLB_CURRENT_VA, 0);
    __insn_mtspr(SPR_DTLB_CURRENT_PA, 0);
    __insn_mtspr(SPR_DTLB_CURRENT_ATTR, 0);
  }
  if (clean_wired)
    __insn_mtspr(SPR_WIRED_DTLB, 0);
}


int
bme_install_dtte(tte_t* ttep, int index)
{  
  int crit_sec_save = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
  
  int nent = __insn_mfspr(SPR_NUMBER_DTLB);
  int nwired = __insn_mfspr(SPR_WIRED_DTLB);

  if (index == BME_TTE_INDEX_WIRED && nwired < nent)
  {
    index = nwired;
    __insn_mtspr(SPR_WIRED_DTLB, nwired + 1);
  }
  else if (index == BME_TTE_INDEX_RANDOM && nwired < nent)
    index = __insn_mfspr(SPR_REPLACEMENT_DTLB);

  //
  // Note that if the user specified one of the special values, but the
  // precondition was not satisfied (e.g., BME_TTE_INDEX_WIRED but there
  // were no more wired entries) then index will still be negative and
  // we'll return an error here.
  //
  if (index < 0 || index >= nent)
  {
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);
    return (-1);
  }

  __insn_mtspr(SPR_DTLB_INDEX, index);
  __insn_mtspr(SPR_DTLB_CURRENT_VA, ttep->w1.word);
  __insn_mtspr(SPR_DTLB_CURRENT_PA, ttep->w2.word);
  __insn_mtspr(SPR_DTLB_CURRENT_ATTR, ttep->w0.word);
  
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);

  return (index);
}


int
bme_remove_dtte(int index)
{  
  int crit_sec_save = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
  
  int nent = __insn_mfspr(SPR_NUMBER_DTLB);
  int nwired = __insn_mfspr(SPR_WIRED_DTLB);

  if (index < 0 || index >= nent)
  {
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);
    return (-1);
  }

  int wired_err = 0;

  if (index == nwired - 1)
    __insn_mtspr(SPR_WIRED_DTLB, nwired - 1);
  else if (index < nwired - 1)
    wired_err = 1;
  
  __insn_mtspr(SPR_DTLB_INDEX, index);

  __insn_mtspr(SPR_DTLB_CURRENT_VA, 0);
  __insn_mtspr(SPR_DTLB_CURRENT_PA, 0);
  __insn_mtspr(SPR_DTLB_CURRENT_ATTR, 0);
  
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);

  return (wired_err);
}


int
bme_install_itte(tte_t* ttep, int index)
{  
  int crit_sec_save = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
  
  int nent = __insn_mfspr(SPR_NUMBER_ITLB);
  int nwired = __insn_mfspr(SPR_WIRED_ITLB);

  if (index == BME_TTE_INDEX_WIRED && nwired < nent)
  {
    index = nwired;
    __insn_mtspr(SPR_WIRED_ITLB, nwired + 1);
  }
  else if (index == BME_TTE_INDEX_RANDOM && nwired < nent)
    index = __insn_mfspr(SPR_REPLACEMENT_ITLB);

  //
  // Note that if the user specified one of the special values, but the
  // precondition was not satisfied (e.g., BME_TTE_INDEX_WIRED but there
  // were no more wired entries) then index will still be negative and
  // we'll return an error here.
  //
  if (index < 0 || index >= nent)
  {
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);
    return (-1);
  }

  __insn_mtspr(SPR_ITLB_INDEX, index);
  __insn_mtspr(SPR_ITLB_CURRENT_VA, ttep->w1.word);
  __insn_mtspr(SPR_ITLB_CURRENT_PA, ttep->w2.word);
  __insn_mtspr(SPR_ITLB_CURRENT_ATTR, ttep->w0.word);
  
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);

  return (index);
}


int
bme_remove_itte(int index)
{  
  int crit_sec_save = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
  
  int nent = __insn_mfspr(SPR_NUMBER_ITLB);
  int nwired = __insn_mfspr(SPR_WIRED_ITLB);

  if (index < 0 || index >= nent)
  {
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);
    return (-1);
  }

  int wired_err = 0;

  if (index == nwired - 1)
    __insn_mtspr(SPR_WIRED_ITLB, nwired - 1);
  else if (index < nwired - 1)
    wired_err = 1;
  
  __insn_mtspr(SPR_ITLB_INDEX, index);

  __insn_mtspr(SPR_ITLB_CURRENT_VA, 0);
  __insn_mtspr(SPR_ITLB_CURRENT_PA, 0);
  __insn_mtspr(SPR_ITLB_CURRENT_ATTR, 0);
  
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);

  return (wired_err);
}


int
bme_pte2tte(VA va, PA pa, HV_PTE pte, int allow_incoherent, tte_t* ttep)
{
  //
  // Set up the stuff in the TTE which doesn't depend upon the caching mode.
  //
  ttep->w0 = (SPR_DTLB_CURRENT_ATTR_t)
    {{
        .v = 1,
        .ps = hv_pte_get_page(pte) ? TTE_PS_16M : TTE_PS_64K,
        .g = 1,
        .asid = 0,
        .w = hv_pte_get_writable(pte),
        .mpl = 0,
      }};

  ttep->w1 = (SPR_DTLB_CURRENT_VA_t)
    {
      .word = va,
    };

  ttep->w2 = (SPR_DTLB_CURRENT_PA_t)
    {
      .word = pa,
    };

  //
  // If we're not mapping writably, allow incoherent mappings.
  //
  if (!hv_pte_get_writable(pte))
    allow_incoherent = 1;

  //
  // Get the LOTAR from the PTE.
  //
  Lotar pte_lotar = hv_pte_get_lotar(pte);
  pos_t pte_pos =
  {
    .bits.x = HV_LOTAR_X(pte_lotar),
    .bits.y = HV_LOTAR_Y(pte_lotar)
  };
  pos_t my_pos = { .word = __insn_mfspr(SPR_TILE_COORD) };

  //
  // Compute the rest of the TTE depending upon how the PTE is cached.
  //
  switch (hv_pte_get_mode(pte))
  {
  case HV_PTE_MODE_UNCACHED:
    if (!allow_incoherent)
      return (-1);

    ttep->w0.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE;
    break;

  case HV_PTE_MODE_CACHE_NO_L3:
    ttep->w0.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    ttep->w0.cache_home_mapping =
      SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE;
    ttep->w0.location_x_or_page_mask = my_pos.bits.x;
    ttep->w0.location_y_or_page_offset = my_pos.bits.y;
    break;

  case HV_PTE_MODE_CACHE_TILE_L3:
    ttep->w0.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT;

    if (hv_pte_get_nc(pte))
    {
      if (!allow_incoherent)
        return (-3);

      ttep->w0.memory_attribute =
        SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    }
        
    ttep->w0.cache_home_mapping =
      SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE;
    ttep->w0.location_x_or_page_mask = pte_pos.bits.x;
    ttep->w0.location_y_or_page_offset = pte_pos.bits.y;
    break;

  case HV_PTE_MODE_CACHE_HASH_L3:
    ttep->w0.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT;

    if (hv_pte_get_nc(pte))
    {
      if (!allow_incoherent)
        return (-4);

      ttep->w0.memory_attribute =
        SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    }

    ttep->w0.cache_home_mapping =
      SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_HASH;
    ttep->w0.location_x_or_page_mask = 0xF;
    ttep->w0.location_y_or_page_offset = 0;
    break;

  case HV_PTE_MODE_MMIO:
    ttep->w0.memory_attribute =
      SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_MMIO;
    ttep->w0.location_x_or_page_mask = pte_pos.bits.x;
    ttep->w0.location_y_or_page_offset = pte_pos.bits.y;
    break;

  default:
    return (-5);
  }

  return (0);
}
