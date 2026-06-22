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

#include <util.h>
#include <stdio.h>
#include <string.h>

#include <bme/dtlb.h>
#include <bme/interrupts.h>
#include <bme/bme_malloc.h>
#include <bme/sys_info.h>
#include <bme/tlb.h>
#include <bme/tte.h>


/** Debug tracing. */
#if 0
#define DTLB_TRACE(fmt, ...) \
    tprintf("dtlb: " fmt, ## __VA_ARGS__);
#else
#define DTLB_TRACE(...)
#endif

/** Number of bytes of VA space per "slot" */
#define SIZE_PER_SLOT 0x1000000 /* 16MB */
/** Number of slots required at above size to cover 32 bits of address space */
#define NUM_SLOTS 256
/** Macro to get the slot number that corresponds to a VA */
#define VA_TO_SLOT(addr) ((addr) / SIZE_PER_SLOT)
/** Macro to get the index into the tte table for a slot, given a VA */
#define VA_TO_IDX(va, page_size) \
  (((va) & 0x00ffffff) >> __insn_ctz(page_size))

/** Information on a VA range.  Each 16MB segment of the VA space has a
 * miss handler and a tte_table associated with it.  All pages in a segment
 * must be the same size.
 */
struct dtlb_seg_info {
  /** The table of entires for this segment */
  tte_t* tte_table;
  /** The handler used for any misses in this VA range  */
  bme_dtlb_miss_handler_t* miss_handler;
  /** The size of the pages in the segment */
  int page_size;
};
/** Pointer to all DTLB segment information for VA space. */
static struct dtlb_seg_info* seg_info;

static void
bme_dtlb_master_miss_handler(int interrupt, struct bme_saved_regs_full *sr)
{
  VA bad_addr = __insn_mfspr(SPR_DTLB_BAD_ADDR);
  int seg = VA_TO_SLOT(bad_addr);

  if (seg_info[seg].miss_handler)
  {
    if (seg_info[seg].miss_handler(bad_addr) == 0)
      return;
  }
  else
  {
    DTLB_TRACE("No miss handler installed for seg %d (bad_addr = %#lX)\n",
            seg, bad_addr);
  }

  panic("DTLB miss handler could not find mapping for VA %#lX\n", bad_addr);
}

int
bme_install_dtlb_handler(VA va, bme_dtlb_miss_handler_t* miss_handler)
{
  int seg = VA_TO_SLOT(va);

  DTLB_TRACE("Installing miss handler for va = %#lX, seg = %d\n", va, seg);

  if (seg > NUM_SLOTS)
    return -1;

  seg_info[seg].miss_handler = miss_handler;
  return 0;
}

int
bme_dtlb_uninstall_miss_handler(VA va)
{
  int seg = VA_TO_SLOT(va);

  if (seg > NUM_SLOTS)
    return -1;

  seg_info[seg].miss_handler = 0;
  return 0;
}


int
bme_dtlb_default_range_miss_handler(VA va)
{
  tte_t* ptte;
  int seg = VA_TO_SLOT(va);

  DTLB_TRACE("DTLB miss: va = %#lX\n", va);

  if (seg_info[seg].tte_table == 0)
  {
    DTLB_TRACE("seg %d has no tte table\n", seg);
    return -1;
  }

  uint32_t index = VA_TO_IDX(va, seg_info[seg].page_size);

  // Using index, get tte from table
  ptte = &seg_info[seg].tte_table[index];

  // check for valid
  if (!ptte->w2.v)
  {
    DTLB_TRACE("seg %d tte at index %d not valid (page size = %#lX)\n",
            seg, index, seg_info[seg].page_size);
    return -1;
  }

  // Install in a random slot.
  bme_install_dtte(ptte, BME_TTE_INDEX_RANDOM);

  return 0;
}

static int 
setup_tte(tte_t* ptte, VA va, PA pa, int page_size, bme_memory_attr_t* attr)
{
  // Make sure that va not already mapped.
  if (ptte->w2.v)
  {
    DTLB_TRACE("tte already mapped\n");
    return -1;
  }

  int cacheable = 0;
  int loc_override = 0;
  pos_t pos = { .word = __insn_mfspr(SPR_TILE_COORD) }; /* my_pos */

  switch (attr->cache_mode)
  {
  case BME_CACHE_MODE_LOCAL:
    cacheable = 1;
    loc_override = 1;
    break;
  case BME_CACHE_MODE_HASH:
    cacheable = 1;
    loc_override = 0;
    break;
  case BME_CACHE_MODE_NONE:
    cacheable = 0;
    loc_override = 0;
    break;
  case BME_CACHE_MODE_COORDS:
    cacheable = 1;
    loc_override = 1;
    pos.word = attr->cache_coords.word;
    break;
  default:
    return -1;
  }

  ptte->w0.ps = TTE_SHIFT_TO_PS(__insn_ctz(page_size));
  ptte->w0.g = 1;
  ptte->w0.asid = 0;
  ptte->w0.vpn = VPFN(va);
  ptte->w1.pfn_high = PFN_HI(pa);
  ptte->w2.v = 1;
  ptte->w2.w = attr->flags & BME_MEMORY_MAP_FLAGS_WRITEABLE;
  ptte->w2.c = cacheable;
  ptte->w2.lo = loc_override;
  ptte->w2.be = 1;
  ptte->w2.re = 1;
  ptte->w2.mpl = 2;
  ptte->w2.pfn_low = PFN_LO(pa);
  ptte->w3.lotar_x = pos.bits.x;
  ptte->w3.lotar_y = pos.bits.y;

  return 0;
}

// FIXME: maybe wired needs to be a flag instead of a separate arg
int
bme_memory_map(VA va, PA pa, int page_size, int num_pages, 
               bme_memory_attr_t* attr, int wired)
{
  for (int i = 0; i < num_pages; i++)
  {
    struct dtlb_seg_info* seg = &seg_info[VA_TO_SLOT(va)];

    if (seg->tte_table == 0)
    {
      int memsz = sizeof(*seg->tte_table) * (SIZE_PER_SLOT / page_size);
      seg->tte_table = bme_malloc(memsz);
      if (!seg->tte_table)
      {
        panic("Could not allocate space for page table");
      }
      memset(seg->tte_table, 0, memsz);
      seg->page_size = page_size;

      if (seg->miss_handler == 0)
      {
        DTLB_TRACE("Installing miss handler for va = %#lX\n", va);
        bme_install_dtlb_handler(va, bme_dtlb_default_range_miss_handler);
      }
    }
    else if (page_size != seg->page_size)
    {
      // FIXME: make this a nice error code instead of a print
      DTLB_TRACE("trying to add wrong-sized tte to table\n");
      return -1;
    }

    uint32_t index = VA_TO_IDX(va, page_size);
    DTLB_TRACE("va = %#lX page_size = %#lX shift = %d index = %d\n",
               va, page_size, __insn_ctz(page_size), index);

    tte_t* ptte = &seg->tte_table[index];

    // Page-align the addresses.
    // FIXME: return error if these addresses don't work together.
    // FOR NOW just verify that addresses are already aligned.  Dunno if 
    // we should try to map based on funny addresses.

    int err = setup_tte(ptte, va, pa, page_size, attr);
    if (err < 0)
    {
      DTLB_TRACE("Could not set up tte\n");
      return err;
    }

    if (wired)
    {
      err = bme_install_dtte(ptte, BME_TTE_INDEX_WIRED);
      if (err < 0)
      {
        DTLB_TRACE("Could not install tte\n");
        return err;
      }
    }

    va += page_size;
    pa += (uint64_t)page_size;
  }

  return 0;
}

/** Search the DTLB for an entry which matches the given VA, and remove
 *  the entry.
 * @param va Virtual address to match.
 */
static void
search_dtlb_and_remove(tte_t* tte)
{
  int crit_sec_save = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  int nent = __insn_mfspr(SPR_NUMBER_DTLB);

  for (int i = 0; i < nent; i++)
  {
    SPR_DTLB_CURRENT_0_t w0;

    BME_LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_0);

    if (w0.word == tte->w0.word)
    {
      DTLB_TRACE("Removing DTLB entry %d from table (w0 = %#lX)\n",
                 i, w0.word);

       // We don't technically need to clear word 0, but this'll keep future
       // flushes from re-flushing this line even when it's invalid.
       __insn_mtspr(SPR_DTLB_CURRENT_0, 0);
       __insn_mtspr(SPR_DTLB_CURRENT_2, 0);
    }
  }

  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, crit_sec_save);
}

// FIXME: Maybe this should also be page size / number of pages!
int
bme_memory_unmap(VA va, int num_bytes)
{
  struct dtlb_seg_info* seg = &seg_info[VA_TO_SLOT(va)];

  // This memory was never mapped.
  if (seg->tte_table == 0)
    return -1;

  // Get page size for this segment, and get index from address.
  int page_size = seg->page_size;
  uint32_t start_index = VA_TO_IDX(va, page_size);

  // Calculate number of pages, so that all pages will be removed.
  int num_pages = num_bytes / page_size;

  for (int i = start_index; i < (start_index + num_pages); i++)
  {
    // Invalidate the entry.
    tte_t* ptte = &seg->tte_table[i];
    if (!ptte->w2.v)
    {
      DTLB_TRACE("%s: seg %d tte at index %d not valid (page size = %#lX)\n",
                 __FUNCTION__, seg, i, seg->page_size);
      return -1;
    }
    ptte->w2.v = 0;

    // Search the dtlb, remove from DTLB (if wired and not last
    // wired entry, just remove and leave hole).
    search_dtlb_and_remove(ptte);
  }

  return 0;
}

int
bme_default_dtlb_handler_init()
{
  seg_info = bme_malloc(sizeof(*seg_info) * NUM_SLOTS);

  if (!seg_info)
    return -1;

  memset (seg_info, 0, sizeof(*seg_info) * NUM_SLOTS);

  // Install the master miss handler.
  bme_install_interrupt_handler(INT_DTLB_MISS,
                                bme_dtlb_master_miss_handler);

  return 0;
}


