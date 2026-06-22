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
 * Bare Metal Environment support, including the BME loader.
 */

#include <elf.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <arch/abi.h>
#include <arch/chip.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "sys/bme/bme/sys_info.h"

#include <hv/hypervisor.h>

#include "sys/libc/include/util.h"

/** Use the non-standard-I/O version of the compression library */
#define BZ_NO_STDIO
#include <bzlib.h>

#include "board_info.h"
#include "bits.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "filesys.h"
#include "hv.h"
#include "mapping.h"
#include "mshim_acc.h"
#include "misc.h"
#include "page.h"
#include "physacc.h"
#include "tlb.h"
#include "tte.h"


/** PA of page of memory that is shared between the HV and the BME.  Used
 * primarily for shared locks.
 */
PA shared_lock_page_pa;

/** Lotar of page of memory that is shared between the HV and the BME.  Used
 * primarily for shared locks.
 */
Lotar shared_lock_page_lotar;

/** Size, in bytes, of shared data area */
#define BME_SHARED_DATA_SIZE (1 << PG_SHIFT_4M)
/** Elf Pheader */
#define BME_Elf_Phdr Elf64_Phdr
/** Elf Eheader */
#define BME_Elf_Ehdr Elf64_Ehdr

/** Maximum number of free memory segments to be recorded and passed 
 * to the BME application.
 */
#define BME_MAX_FREE_MEM_SEGS 24 /* FIXME: needs to be dynamically decided */

/** Maximum number of extra files to be read into memory. */
#define BME_MAX_EXTRA_FILES 8 /* FIXME: needs to be dynamically decided */

/** Size, in bytes, of the BME scratchpad area that is shared amongst
 * tiles.
 */
#define BME_SCRATCHPAD_LEN_BYTES (1 << PG_SHIFT_4K)

/** Default virtual stack address.
*/
#define BME_DEFAULT_PERTILE_PAGE_VA 0xfc000000

/** Base virtual address to use for shared data.
*/
#define BME_SHARED_DATA_VA_BASE 0xffc00000

/** Maximum number of I/O devices for which we store information.
 */
#define BME_MAX_IO_DEVICES 29  /* FIXME: needs to be dynamically decided */

/** Maximum page size used by the BME. */
// FIXME they can be much bigger on gx
#define BME_MAX_PAGE_SIZE     (1 << PG_SHIFT_16M)

/** List of information for individual mapping elements.  Each element
 * in the list contains mapping information for one page.  The entire list
 * comprises mapping information for a contiguous range of memory (useful
 * for when a program segment or stack/heap area can't be mapped with one
 * page).
 */
struct bme_map_info_list
{
  /** VA of start of page */
  VA page_va;
  /** PA of start of page */
  PA page_pa;
  /** Page size */
  uint32_t page_size;
  /** Pointer to next element in list */
  struct bme_map_info_list* next;
};

/** Information on how to map memory.  The list of page mappings is 
 * mapped using their respective (va, pa, page_size) using the cache 
 * attributes contained here.  Segment type determines whether a list
 * of mappings is mapped in the ITLB or DTLB, and whether it is 
 * read/write or read only.
 */
struct bme_map_list
{
  /** Total size of all the pages in the list, for convenience */
  uint32_t total_memsz;
  /** Type of mapping (SEG_TYPE_xxx) */
  uint8_t seg_type;
  /** Configued cache mode (BME_CACHE_MODE_xxx) */
  uint8_t cache_mode;
  /** Coordinates of tile on which to cache this memory, if 
   * cache_mode == BME_CACHE_MODE_COORDS
   */
  pos_t cache_coords;
  /** List of page mappings */
  struct bme_map_info_list* info_list;
};

/** Structure to relate a map list and an elf header.
 * Needed for sorting.
 */
struct bme_header_and_mapping
{
  /** Segment header. */
  BME_Elf_Phdr phdr;
  /** List of mappings for the header. */
  struct bme_map_list map_list;
};

/** Description of available memory on a memory controller. */
struct bme_mem_ctl
{
  /** One greater than the PA of unallocated byte with the highest
   * addresses.
   */
  PA top;
  /** PA of unallocated byte with the lowest addresses. */
  PA bottom;
};

/** Description of all memory controllers in system. */
struct bme_mem_ctl bme_mem_controller[MAX_MSHIMS];

/** Description of data area that is shared by all tiles. */
struct bme_shared_data
{
  /** Physical address of shared data, so client tiles can map as they like */
  PA shared_data_pa;
  /** TTE for the global information struct */
  tte_t global_info_tte;
  /** Pointer to global information struct, which resides on the shared 
   * data page right after this data structure */
  bme_global_info_t* global_info;
  /** Memory controller information, for passing to client tiles */
  struct bme_mem_ctl bme_mem_controller[MAX_MSHIMS];
};

/** Pointer to global shared data structure. */
static struct bme_shared_data* shared_data;

/** Destination description for an ELF segment. */
struct bme_phys_dest
{
  /** Physical start address for this segment */
  PA pa;
  /** File offset */
  uint32_t offset;
  /** How much to read out of file */
  uint32_t filesz;
  /** Size that segment will be in memory */
  uint32_t memsz;
};

/** List of destinations for an ELF segment.  The BME configuration
 * may call for an ELF segment to have several copies in memory.  Each
 * element in the list represents a single segment copy, and contains
 * information for the physical destination, cache attributes, and
 * mapping lists for that copy.
*/
struct bme_seg_dest_list
{
  /** Destination description for this element. */
  struct bme_phys_dest dest;
  /** Description for this element, for searching for duplicates. */
  struct bme_mem_desc mem_desc;
  /** Mapping information for this element. */
  struct bme_map_list map_list;
  /** Pointer to next element in list.  */
  struct bme_seg_dest_list* next;
};

/** List of segments of memory that have already been allocated and
 * therefore are not available for use.  Elements are added to this list
 * in order of increasing PA.
*/
struct bme_allocated_seg_list
{
  /** Starting physical address of allocated memory. */
  PA start_pa;
  /** Length of memory segment, in bytes. */
  uint32_t memsz;
  /** Pointer to next element in list. */
  struct bme_allocated_seg_list* next;
};

/** List of allocated segments of memory */
static struct bme_allocated_seg_list* allocated_seglist;

/** Allocate 25k on the stack for temporary use */
#define BME_TEMP_MEMORY_SIZE 0x6400


/** Translate a segment type to its name. */
const char* const seg_type_to_name[] =
{
  [SEG_TYPE_UNKNOWN] = "unknown",
  [SEG_TYPE_TEXT] = "text",
  [SEG_TYPE_RODATA] = "rodata",
  [SEG_TYPE_RWDATA] = "rwdata",
  [SEG_TYPE_TEXT_RODATA] = "text_rodata",
};


/** Translate ELF program header flags to a BME segment type.
 * @param flags Program header flags.
 * @return Segment type.
 */
static bme_seg_type_t
phdr_to_seg_type(unsigned long flags)
{
  if ((flags & PF_X) && (flags & PF_R))
    return SEG_TYPE_TEXT_RODATA;
  else if (flags & PF_X)
    return SEG_TYPE_TEXT;
  else if ((flags & PF_R) && (flags & PF_W))
    return SEG_TYPE_RWDATA;
  else if (flags & PF_R)
    return SEG_TYPE_RODATA;
  else
    return SEG_TYPE_UNKNOWN;
}


/** Pointer to temporary (stack) memory, used by bme_alloca() */
static void* bme_temp_memory;

/** Allocate from temporary storage.  Only callable after bme_temp_memory
 * is set up in either bme_load() or start_client_bme(). Will crash if called
 * before or after those functions.
 * @param size Amount of memory, in bytes, to allocate from temporary storage.
 * @return Pointer to allocated memory on success, 0 on failure.
 */
static void*
bme_alloca(size_t size)
{
  void* retval;
  static void* current;
  static int first_time = 1;

  if (bme_temp_memory == NULL)
    panic("%s: trying to use unallocated memory!", __FUNCTION__);

  if (first_time)
  {
    current = bme_temp_memory;
    first_time = 0;
  }

  retval = current;
  current += size;

  if (current - bme_temp_memory > BME_TEMP_MEMORY_SIZE)
    panic("%s: BME loader out of temporary storage", __FUNCTION__);

  return retval;
}


/** Add a (va, pa, size) mapping to a mapping list.  Keeps a running
 * total of the size of memory covered by the list.
 * @param map_list Mapping list to which to add mapping.
 * @param page_pa PA of mapping to be added.
 * @param page_va VA of mapping to be added.
 * @param page_size Page size of mapping to be added.
 */
static void
add_mapping_to_list(struct bme_map_list* map_list, PA page_pa, VA page_va,
                    uint32_t page_size)
{
  struct bme_map_info_list* list = map_list->info_list;
  struct bme_map_info_list* elem = bme_alloca(sizeof(*elem));
  memset(elem, 0, sizeof (*elem));
  elem->next = NULL;

  elem->page_pa = page_pa;
  elem->page_va = page_va;
  elem->page_size = page_size;

  if (list == NULL)
  {
    // Empty list, this is the first member.
    map_list->info_list = elem;
    map_list->total_memsz = page_size;
    return;
  }

  // List not empty, so stick it on the end.
  struct bme_map_info_list* lp, * prev;
  for (prev = list, lp = list; lp; lp = lp->next) 
  {
    prev = lp;
  }
  prev->next = elem;

  map_list->total_memsz += page_size;
}


/** Copy a mapping list in its entirety.  The dst must be an empty list.
 * @param dst Destination mapping list.
 * @param src Source mapping list to be copied.
 */
static void
copy_mapping_list(struct bme_map_list* dst, struct bme_map_list* src)
{
  dst->seg_type = src->seg_type;
  dst->cache_mode = src->cache_mode;
  dst->cache_coords = src->cache_coords;
  for (struct bme_map_info_list* elem = src->info_list;
       elem;
       elem = elem->next)
    add_mapping_to_list(dst, elem->page_pa, elem->page_va, elem->page_size);
}


/** Searches a segment's list of destinations.
 * Returns 1 if the seglist already contains an element that
 * corresponds to the same memory controller, relative location, and cache
 * characteristics.
 * @param seglist List of segment destinations to search.
 * @param mem_desc Memory characteristics to match in search.
 * @param map_list Resulting list of mappings if the segment is found.
 * @return 0 if no match, 1 if a match is found.
 */
static int
search_segment_dest_list(struct bme_seg_dest_list *seglist,
                         struct bme_mem_desc* mem_desc,
                         struct bme_map_list* map_list)
{
  int match = 0;
  for (struct bme_seg_dest_list *lp = seglist; lp; lp = lp->next)
  {
    if ((lp->mem_desc.ctl_pa == mem_desc->ctl_pa) &&
        (lp->mem_desc.cache_coords.word == mem_desc->cache_coords.word) &&
        (lp->mem_desc.mem_ctl_num == mem_desc->mem_ctl_num) &&
        (lp->mem_desc.pa_mode == mem_desc->pa_mode) &&
        (lp->mem_desc.cache_mode == mem_desc->cache_mode))
    {
      match = 1;
      copy_mapping_list(map_list, &lp->map_list);
      break;
    }
  }

  return match;
}


/** Check to make sure that this page does not overlap any memory in our
 * "already allocated" list, and that it is within the bounds of the
 * free BME memory.
 * @param mem_ctl Memory controller to check for availability.
 * @param start_pa PA of memory we hope is available.
 * @param len Length, in bytes, of memory we hope is available.
 * @return 1 if memory is available, 0 if it is not.
 */
static int
bme_memory_is_available(struct bme_mem_ctl* mem_ctl, PA start_pa,
                        uint32_t len)
{
  PA end_pa = start_pa + len;
  // Check the top and bottom pointers for overlap
  if (end_pa > mem_ctl->top)
  {
    BME_TRACE("end_pa %#llX is past top %#llX\n", end_pa, mem_ctl->top);
    return 0;
  }
  if (start_pa < mem_ctl->bottom)
  {
    BME_TRACE("start_pa %#llX is less than bottom %#llX\n", 
              start_pa, mem_ctl->bottom);
    return 0;
  }

  // Loop through the allocated list, looking for overlap
  for (struct bme_allocated_seg_list* plist = allocated_seglist; plist;
       plist = plist->next)
  {
      BME_TRACE("Memory overlap check: start_pa = %#llX "
                "end_pa = %#llX list_pa = %#llX list_memsz = %#X\n",
                start_pa, end_pa, plist->start_pa, plist->memsz);

      PA start_list_pa = plist->start_pa;
      PA end_list_pa = start_list_pa + plist->memsz;

      int entirely_before = (start_pa < start_list_pa) &&
                            (end_pa <= start_list_pa);
      int entirely_after = start_pa >= end_list_pa;
      if (! (entirely_before || entirely_after))
      {
        BME_TRACE("Overlap detected: entirely_before = %d "
                  "entirely_after = %d\n", entirely_before, entirely_after);
        return 0;
      }
  }

  return 1;
}

/** Get a page that is less than or equal to the given maximum page size, that
 * attempts to cover the memory described by the given va and memsz with the
 * smallest possible page size.  If this is not possible, get a page that meets
 * the alignment constraints but does not cover all the memory, and return
 * the number of pages it would take to cover the memory.
 * @param max_page_size Maximum page size to consider.
 * @param va VA of start of memory to be covered by page.
 * @param memsz Size, in bytes, of memory to be covered by page.
 * @param page_va VA of start of resulting page.
 * @param ppagesize Size, in bytes, of resulting page.
 * @param npages Number of pages (of pagesize) needed to span the memory.
 */
static void
get_smallest_page_size(uint32_t max_page_size, 
                       VA va,
                       uint32_t memsz, VA* page_va,
                       uint32_t* ppagesize, uint32_t* npages)
{
  static const uint32_t tpagesize[] = {
    (1 << PG_SHIFT_4K),
    (1 << PG_SHIFT_16K),
    (1 << PG_SHIFT_64K),
    (1 << PG_SHIFT_256K),
    (1 << PG_SHIFT_1M),
    (1 << PG_SHIFT_4M),
    (1 << PG_SHIFT_16M)
  };

  int npagesizes = sizeof(tpagesize)/sizeof(tpagesize[0]);

  // For unit testing case where segments will span multiple pages.
  // Don't use pages larger than 16k, so we don't have to make 
  // giant executables that will take forever in the simulator.
  if (debug_flags & DEBUG_BME_SMALLPAGES)
    npagesizes = 2;

  uint32_t pagesize = tpagesize[npagesizes - 1];

  for (int i = 0; i < npagesizes; i++)
  {
    if (tpagesize[i] > max_page_size)
    {
      pagesize = tpagesize[i > 0 ? i - 1 : 0];
      break;
    }

    if (va - ROUND_DN(va, tpagesize[i]) + memsz <= tpagesize[i])
    {
      pagesize = tpagesize[i];
      break;
    }
  }

  // Align the start of the page to the selected page size.
  *page_va = ROUND_DN(va, pagesize);

  // See how many pages we need to contain the memory we need.
  // If we fit on one page before we might not now that we've
  // backed up the pointer.  Or if this is a huge amount
  // of memory we might not have fit on one page.
  *npages = ROUND_UP(((va + memsz) - *page_va), pagesize) / pagesize;
  *ppagesize = pagesize;
}

/** Get a page that attempts to cover the memory described by the given va and
 * memsz with the smallest possible page size.  If this is not possible, get a
 * page that meets the alignment constraints but does not cover all the memory,
 * and return the number of pages it would take to cover the memory.
 * @param va VA of start of memory to be covered by page.
 * @param memsz Size, in bytes, of memory to be covered by page.
 * @param page_va VA of start of resulting page.
 * @param ppagesize Size, in bytes, of resulting page.
 * @param npages Number of pages (of pagesize) needed to span the memory.
 */
static void
get_smallest_aligned_pages(VA va, uint32_t memsz, VA* page_va,
                       uint32_t* ppagesize, uint32_t* npages)
{
  get_smallest_page_size(BME_MAX_PAGE_SIZE, va, memsz, page_va, ppagesize,
                         npages);
}

/** Get a page that is less than or equal to the given maximum page size, that
 * attempts to cover the memory described by the given va and memsz with the
 * smallest possible page size.  If this is not possible, get a page that meets
 * the alignment constraints but does not cover all the memory.
 * @param max_page_size Maximum page size to consider.
 * @param va VA of start of memory to be covered by page.
 * @param memsz Size, in bytes, of memory to be covered by page.
 * @param page_va VA of start of resulting page.
 * @param ppagesize Size, in bytes, of resulting page.
 */
static void
get_smallest_aligned_page_less_than(uint32_t max_page_size, 
                                    VA va,
                                    uint32_t memsz, VA* page_va,
                                    uint32_t* ppagesize)
{
  uint32_t dummy_npages;
  get_smallest_page_size(max_page_size, va, memsz, page_va, ppagesize,
                         &dummy_npages);
}

/** Add a section of memory to the "already allocated" list.
 * @param pa PA of memory to add to allocated list.
 * @param memsz Size, in bytes, of memory to add to allocated list.
 */
static void
add_to_allocated_bme_memory_list(PA pa, uint32_t memsz)
{
  struct bme_allocated_seg_list* elem = bme_alloca(sizeof(*elem));
  memset(elem, 0, sizeof (*elem));
  elem->start_pa = pa;
  elem->memsz = memsz;
  elem->next = NULL;

  if (allocated_seglist == NULL)
  {
    // Empty list, this is the first member.
    allocated_seglist = elem;
    return;
  }

  // List not empty, so insert this element into the list.  We want the
  // list in order of increasing PA.

  // Check if this element's PA is less than the first element's, in which case
  // it is the new head.
  if (elem->start_pa < allocated_seglist->start_pa)
  {
    elem->next = allocated_seglist;
    allocated_seglist = elem;
    
    return;
  }

  // Go through the list, and insert this element just before the first element
  // that contains a start_pa that is greater than this one's.
  struct bme_allocated_seg_list* lp;
  struct bme_allocated_seg_list* prev;
  for (prev = allocated_seglist, lp = allocated_seglist->next;
       lp; lp = lp->next)
  {
    if (elem->start_pa < lp->start_pa)
    {
      prev->next = elem;
      elem->next = lp;
      return;
    }
    prev = lp;
  }

  // None found, stick this on the end.
  prev->next = elem;
}

/** Fix up any remaining references to the nearest memory controller, by
 *  finding the one which is actually closest to a group's tiles.
 * @param mpg Group to fix up.
 */
static void
bme_fixup_nearest(struct bme_mem_placement_group* mpg)
{
  //
  // If there was a group-wide nearest command which specified a specific
  // controller, it'll be used for all of the segments, so we don't need to
  // figure out which controller is closest; otherwise, we do so here.  Note
  // that we don't verify whether any of the controllers specified explicitly
  // are valid; that's done later.
  //
  if (mpg->nearest_ctl == BME_CTL_NUM_NEAREST)
  {
    int lowest_sum = INT_MAX;
    int lowest_sum_mshim = 0;

    for (int i = 0; i < MAX_MSHIMS; i++)
    {
      if (config.clients[my_client].mem_len[i] == 0)
        continue;

      int sum_distance = 0;

      for (int y = client_ulhc.bits.y; y <= client_lrhc.bits.y; y++)
      {
        for (int x = client_ulhc.bits.x; x <= client_lrhc.bits.x; x++)
        {
          pos_t tile = { .bits.x = x, .bits.y = y };

          if (!in_tile_mask(&mpg->tiles, tile))
            continue;

          sum_distance += manhattan(tile, mshims[i]->mdn_ports[1]);
        }
      }

      if (sum_distance < lowest_sum)
      {
        lowest_sum = sum_distance;
        lowest_sum_mshim = i;
      }
    }

    mpg->nearest_ctl = lowest_sum_mshim;
  }

  //
  // Now the group-wide value must be a real controller, so we just assign it
  // to any segment which didn't have an explicit controller specification.
  //
  if (mpg->text.mem_desc.mem_ctl_num == BME_CTL_NUM_NEAREST)
    mpg->text.mem_desc.mem_ctl_num = mpg->nearest_ctl;
  if (mpg->rodata.mem_desc.mem_ctl_num == BME_CTL_NUM_NEAREST)
    mpg->rodata.mem_desc.mem_ctl_num = mpg->nearest_ctl;
  if (mpg->rwdata.mem_desc.mem_ctl_num == BME_CTL_NUM_NEAREST)
    mpg->rwdata.mem_desc.mem_ctl_num = mpg->nearest_ctl;
  if (mpg->pertile.mem_desc.mem_ctl_num == BME_CTL_NUM_NEAREST)
    mpg->pertile.mem_desc.mem_ctl_num = mpg->nearest_ctl;
  for (struct bme_extrafile_desc* extra = mpg->extra; 
       extra; extra = extra->next)
  {
    if (extra->mem_desc.mem_ctl_num == BME_CTL_NUM_NEAREST)
      extra->mem_desc.mem_ctl_num = mpg->nearest_ctl;
  }
}


/** Generic function for assigning physical memory.  Reserve physical memory, 
 * which must encompass total_memsz bytes starting from a physical address that
 * is properly aligned with the given VA (based on the given page size).  Pick
 * the memory controller and the relative placement on that memory controller
 * based on the params provided in mem_desc.  Update the map_list with the
 * cache characteristics, and return the physical address of the first page of
 * the reserved memory and the physical address that corresponds to the given
 * va.
 * @param va Virtual address of the start of the memory which must be reserved.
 * @param page_size Page size to which assigned pa must be mutually aligned
 *        with va.
 * @param total_memsz Total amount of memory which must be reserved.
 * @param explicit_pa Used if mem_desc calls for an explicit physical address.
 * @param mem_desc Description of memory characteristics.
 * @param map_list List of mappings corresponding to this reserved memory.
 * @param assigned_pa Pointer to physical address (aligned with given va)
 *        of reserved memory.
 * @param assigned_page_pa Pointer to physical address of first page of 
 *        reserved memory.               
 * @returns 0 if memory was reserved, -1 if it could not be reserved.
 */
static int
assign_physical_memory(VA va, uint32_t page_size, uint32_t total_memsz,
                         PA explicit_pa, struct bme_mem_desc* mem_desc,
                         struct bme_map_list* map_list,
                         PA* assigned_pa, PA* assigned_page_pa)
{
  PA pa;
  PA page_pa;

  // Figure out memory controller number from config.
  int mem_ctl_num = mem_desc->mem_ctl_num;
  struct bme_mem_ctl* mem_ctl = &bme_mem_controller[mem_ctl_num];
  uint8_t pa_mode = mem_desc->pa_mode;

  if (mem_desc->pa_mode == BME_CTL_PLACE_BOTTOM)
  {
    page_pa = ROUND_UP(mem_ctl->bottom, page_size);
    // Calculate the pa that is aligned with va, that is greater than page_pa
    pa = page_pa + (va % page_size);
  }
  else if (mem_desc->pa_mode == BME_CTL_PLACE_TOP)
  {
    page_pa = mem_ctl->top - total_memsz;
    page_pa = ROUND_DN(page_pa, page_size);
    // Calculate the pa that is aligned with va, that is greater than page_pa
    pa = page_pa + (va % page_size);
  }
  else if (mem_desc->pa_mode == BME_CTL_PLACE_EXE)
  {
    pa = explicit_pa | (mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT));
    page_pa = ROUND_DN(pa, page_size);
    BME_TRACE("EXE explicit_pa=%#llX pa=%#llX page_pa=%#llX "
            "(mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT))=%#llX\n",
            explicit_pa, pa, page_pa,
            (mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT)));
  }
  else if (mem_desc->pa_mode == BME_CTL_PLACE_CONFIG)
  {
    pa = mem_desc->ctl_pa | (mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT));
    page_pa = ROUND_DN(pa, page_size);
    BME_TRACE("CONFIG explicit_pa=%#llX pa=%#llX page_pa=%#llX "
              "(mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT))=%#llX\n",
              explicit_pa, pa, page_pa,
              (mem_ctl_num * (1ULL << MSH_MAX_SIZE_SHIFT)));
  }
  else
  {
    panic("Unknown PA mode %d.", mem_desc->pa_mode);
  }

  if ((pa_mode == BME_CTL_PLACE_EXE) || (pa_mode == BME_CTL_PLACE_CONFIG))
  {
    // check that va and pa are aligned with page size
    if ((va % page_size) != (pa % page_size))
    {
      panic("can't align explicit PA %#llX", pa);
    }
  }

  // Check for overlap against the global "allocated memory" list,
  // and make sure that the memory fits in free BME memory.
  // If the memory we want to allocate for this segment
  // overlaps previously allocated memory, print an error and exit.
  // We do not plan to ever handle this case.
  // PROBLEM if multiple explicit pas want to end up on same page, 
  // this isn't really an error, but we aren't handling that case
  // right now. 

  if (!bme_memory_is_available(mem_ctl, page_pa, total_memsz))
  {
    tprintf("bme: attempted to use unavailable memory: "
            "page_pa = %#llX, pa = %#llX, memsz = %#X, "
            "page_size = %#X", page_pa, pa, total_memsz, page_size);
    return -1;
  }

  *assigned_pa = pa;
  *assigned_page_pa = page_pa;

  // Memory is okay to allocate, so advance the appropriate 
  // mem_ctl pointer. If pa_mode is explicit or exe, add this
  // memory to "allocated memory" list. 

  if (mem_desc->pa_mode == BME_CTL_PLACE_BOTTOM)
    mem_ctl->bottom = page_pa + total_memsz;
  else if (mem_desc->pa_mode == BME_CTL_PLACE_TOP)
    mem_ctl->top = page_pa;
  else 
    add_to_allocated_bme_memory_list(page_pa, total_memsz);

  // Create mapping based on the physical memory that was just allocated.
  map_list->cache_mode = mem_desc->cache_mode;
  map_list->cache_coords = mem_desc->cache_coords;

  return 0;
}

/** Assign physical memory to for pertile (stack/heap).
 * @param va Virtual address of the start of the memory to be reserved.
 * @param explicit_pa Used if mem_desc calls for an explicit physical address.
 * @param mem_desc Description of memory characteristics.
 * @param map_list List of mappings corresponding to this reserved memory.
 * @param assigned_pa Pointer to physical address (aligned with given va)
 *        of reserved memory.
 * @returns 0 if memory was reserved, -1 if it could not be reserved.
 */
static int
assign_pertile_physical_memory(VA va, PA explicit_pa, uint32_t memsz,
                       struct bme_mem_desc* mem_desc,
                       struct bme_map_list* map_list,
                       PA* assigned_pa)
{
  // Using the memory controller pointers and the va & length of the 
  // segment, get the first appropriate PA (considering page size and
  // alignment). If pa_mode is explicit or exe, use that PA.

  // Figure out page size for this segment, assuming we can use a PA
  // aligned the same as the VA

  VA page_va;
  PA page_pa;
  uint32_t npages;
  uint32_t page_size;

  get_smallest_aligned_pages(va, memsz, &page_va, &page_size, &npages);
  uint32_t total_memsz = page_size * npages;

  int err = assign_physical_memory(va, page_size, total_memsz, explicit_pa,
                                   mem_desc, map_list, assigned_pa, &page_pa);

  while (total_memsz > 0)
  {
    add_mapping_to_list(map_list, page_pa, page_va, page_size);
    total_memsz -= page_size;
    page_pa += page_size;
    page_va += page_size;
  }

  return err;
}


/** Assign physical memory for an ELF segment. 
 * @param phdr Program header of ELF segment.
 * @param mem_desc Description of memory characteristics for this segment,
 *        from config.
 * @param phys_dest Resulting physical destination for this segment.
 * @param map_list Resulting list of mappings for this segment.
 * @return 0 if successful, -1 if memory could not be reserved.
 */
static int
assign_segment_physical_memory(BME_Elf_Phdr* phdr,
                       struct bme_mem_desc* mem_desc,
                       struct bme_phys_dest* phys_dest,
                       struct bme_map_list* map_list)
{

  // Using the memory controller pointers and the va & length of the 
  // segment, get the first appropriate PA (considering page size and
  // alignment). If pa_mode is explicit or exe, use that PA.

  // Figure out page size for this segment, assuming we can use a PA
  // aligned the same as the VA
  phys_dest->offset = phdr->p_offset;
  phys_dest->filesz = phdr->p_filesz;
  phys_dest->memsz = phdr->p_memsz;

  PA page_pa;
  uint32_t page_size = map_list->info_list->page_size;
  uint32_t total_memsz = map_list->total_memsz;

  int err = assign_physical_memory(phdr->p_vaddr, page_size, total_memsz,
                                   phdr->p_paddr, mem_desc, map_list,
                                   &phys_dest->pa, &page_pa);

  map_list->seg_type = phdr_to_seg_type(phdr->p_flags);

  // Now fill the PAs into the mappings.
  for (struct bme_map_info_list* elem = map_list->info_list;
       elem;
       elem = elem->next)
  {
    elem->page_pa = page_pa;
    page_pa += elem->page_size;
  }

  return err;
}

/** Record permanent mappings for the tiles specified by the given mask.
 * Group-wide segment types (text & rodata; also rwdata if using shared
 * data) get recorded on all tiles.  Per-tile copies of segments (rwdata
 * if using private data) get recorded only on the tile which owns that
 * memory.
 * @param tiles Tile mask, indicating which tiles this mapping list is for.
 * @param map_list Mapping list to be recorded for runtime use.
 */
static void
record_permanent_mappings(tile_mask* tiles, struct bme_map_list* map_list)
{
  // Record this mapping for all tiles that want it.

  uint8_t cache_mode = map_list->cache_mode;
  pos_t cache_coords = map_list->cache_coords;

  for (int tile_no = 0; tile_no < HV_TILES; tile_no++)
  {
    if (in_tile_mask(tiles, IDX2POS(tile_no)))
    {
      // Go through list, recording mappings one by one.
      for (struct bme_map_info_list* elem = map_list->info_list;
           elem;
           elem = elem->next)
      {
        int nmappings =
          shared_data->global_info->tile_table[tile_no].num_mem_segs;

        if (nmappings >= BME_MAX_MEM_SEGMENTS)
          panic("out of mapping slots");

        bme_mem_seg_info_t* client_map_info =
          &shared_data->global_info->tile_table[tile_no].mem_seg[nmappings];

        // Fill in all fields except for the tlb index fields.  They will
        // be filled in later, when we make the final list of mappings.
        client_map_info->base_pa = elem->page_pa;
        client_map_info->base_va = elem->page_va;
        client_map_info->size = elem->page_size;
        client_map_info->cache_mode = cache_mode;
        client_map_info->cache_coords = cache_coords;

        client_map_info->seg_type = map_list->seg_type;
        BME_TRACE("Recording mapping for tile %d (slot %d): "
                  "pa = %#llX va = %#lX size = %#X\n",
                  tile_no, nmappings,
                  client_map_info->base_pa, client_map_info->base_va, 
                  client_map_info->size);
        BME_TRACE("tile %d (slot %d): cache_mode = %d cache_coords (%d,%d) "
                  "type = %s\n", tile_no, nmappings,
                  client_map_info->cache_mode,
                  client_map_info->cache_coords.bits.x,
                  client_map_info->cache_coords.bits.y,
                  seg_type_to_name[map_list->seg_type]);

        shared_data->global_info->tile_table[tile_no].num_mem_segs++;
      }
    }
  }
}

/** Add physical destination to list of destinations for this segment.
 * @param seglist 
 * @param phys_dest Physical destination for the segment, to be added to list.
 * @param mem_desc Description of memory characteristics for this copy of the
 *        segment.
 * @param map_list List of mappings for this copy of the segment.
 */
static void
add_dest_to_segment_list(struct bme_seg_dest_list** seglist,
                         struct bme_phys_dest* phys_dest,
                         struct bme_mem_desc* mem_desc,
                         struct bme_map_list* map_list)
{
  struct bme_seg_dest_list* elem = bme_alloca(sizeof(*elem));
  memset(elem, 0, sizeof (*elem));
  memcpy(&elem->dest, phys_dest, sizeof(*phys_dest));
  memcpy(&elem->mem_desc, mem_desc, sizeof(*mem_desc));
  copy_mapping_list(&elem->map_list, map_list);
  elem->next = NULL;

  if (*seglist == NULL)
  {
    // Empty list, this is the first member.
    *seglist = elem;
    return;
  }

  // List not empty, so stick it on the end.
  struct bme_seg_dest_list* lp, * prev;
  for (prev = *seglist, lp = *seglist; lp; lp = lp->next) 
  {
    prev = lp;
  }
  prev->next = elem;
}


/** Routine to be passed to qsort for sorting the phdr array by va.
 * @param elem1 First element to compare.
 * @param elem2 Second element to compare.
 */
static int
phdr_compare_va(const void* elem1, const void* elem2)
{
  const BME_Elf_Phdr* h1 = elem1;
  const BME_Elf_Phdr* h2 = elem2;

  if (h1->p_vaddr == h2->p_vaddr)
    return 0;
  else if (h1->p_vaddr < h2->p_vaddr)
    return -1;
  else
    return 1;
}


/** Routine to be passed to qsort for sorting the segment array
 * by file offset.
 * @param elem1 First element to compare.
 * @param elem2 Second element to compare.
 */
static int
segmap_compare_file_offset(const void* elem1, const void* elem2)
{
  const struct bme_header_and_mapping* h1 =
    *(const struct bme_header_and_mapping**)elem1;
  const struct bme_header_and_mapping* h2 =
    *(const struct bme_header_and_mapping**)elem2;

  if (h1->phdr.p_offset == h2->phdr.p_offset)
    return 0;
  else if (h1->phdr.p_offset < h2->phdr.p_offset)
    return -1;
  else
    return 1;
}

/** Given a config, the program headers from an elf file, and the virtual
 * address mappings for the program segments, generate a list of physical
 * memory destinations for each segment.
 * @param my_config This client's config.
 * @param nsegs Number of segments in the program header/map list.
 * @param headermaplist List of program headers and their mappings.
 * @param seglist Resulting list of segment destinations.
 * @return 0 on success, -1 if memory could not be assigned to the program
 *         segments.
 */
static int
generate_segment_destinations(struct client_config* my_config,
                              int nsegs,
                              struct bme_header_and_mapping** headermaplist,
                              struct bme_seg_dest_list** seglist)
{
  // already have config and phdrs.  Phdrs will have been sorted by 
  // file offset (this may eventually end up as a linked list) 

  int seg_type;
  // Loop over program header table
  for (int seg_idx = 0; seg_idx < nsegs; seg_idx++)
  {
    BME_Elf_Phdr* phdr = &headermaplist[seg_idx]->phdr;
    int one_copy_per_group;

    // If it's not readable, we don't load it.
    if ((phdr->p_flags & PF_R) == 0)
      continue;

    // Read a segment header.  Get its type, va, pa and len.
    seg_type = phdr_to_seg_type(phdr->p_flags);

    // If seg_type is text, text_rodata, or rodata, or if type is rwdata
    // and we're going to use "shared" mode (private_app_data == 0), going
    // to want one copy per group. 
    if ((seg_type == SEG_TYPE_RWDATA) &&
        (my_config->flags & CLIENT_BME_PRIVATE))
      one_copy_per_group = 0;
    else
      one_copy_per_group = 1;

    BME_TRACE("Segment %d is of type %s, one_copy_per_group = %d\n", seg_idx,
              seg_type_to_name[seg_type], one_copy_per_group);

    // For that type, go through groups in config, find out who wants
    // to put on (mem_ctl_num, top|bottom|pa).  For each distinct tuple,
    // a separate copy will be placed in memory, so search current 
    // destination list for this segment.
    for (struct bme_mem_placement_group* mpg = my_config->bme_groups;
         mpg; mpg = mpg->next)
    {
      struct bme_mem_desc* mem_desc;
      if (seg_type == SEG_TYPE_TEXT || seg_type == SEG_TYPE_TEXT_RODATA)
        mem_desc = &mpg->text.mem_desc;
      else if (seg_type == SEG_TYPE_RWDATA)
        mem_desc = &mpg->rwdata.mem_desc;
      else
        mem_desc = &mpg->rodata.mem_desc;

      /* NOTE: by searching this list for duplicates the way we're 
       * doing here, if multiple groups list that they want, say,
       * text on mem_ctl 0 at the top, all groups will share one
       * copy.  Is this what we want?  The way the config data
       * structure is now, we don't have a way to distinguish a
       * case where multiple groups want multiple copies using
       * the same criteria.  It is extremely unlikely that anyone
       * would even want to do this, but folks should know the 
       * expected behavior.
       */
      if (one_copy_per_group)
      {
        struct bme_map_list map_list = { 0 };

        // If this is a RWDATA segment and we are in shared mode,
        // we actually only want one copy for the whole client.
        // Check to see if we already have one, and if we do, just map it.
        if (seg_type == SEG_TYPE_RWDATA)
        {
          if (seglist[seg_idx])
          {
            copy_mapping_list(&map_list, &seglist[seg_idx]->map_list);
            record_permanent_mappings(&mpg->tiles, &map_list);
            continue;
          }
        }

        // If a group shares a copy of a segment, make sure we don't put
        // down needless duplicates.  Just map the segment that is already
        // there.
        if (search_segment_dest_list(seglist[seg_idx], mem_desc, &map_list))
        {
          record_permanent_mappings(&mpg->tiles, &map_list);
          continue;
        }
      }

      // Assign physical memory to each copy of this segment.  If we
      // are placing one copy for each tile in this group, assign
      // page-aligned physical memory for each copy we are placing.
      // FUTURE: in order to optimize TLB entries and/or memory space,
      // we could store up the entire list of memory to be assigned
      // in the system.  Then, loop through that list, and for each
      // segment type, figure out which segments have virtual
      // addresses that can be mapped on the same page.  Those can
      // then be optimized for space and TLB entries.  Segments 
      // within a segment type may have to be rearranged to do this.

      for (int tile_no = 0; tile_no < HV_TILES; tile_no++)
      {
        if (in_tile_mask(&mpg->tiles, IDX2POS(tile_no)))
        {
          struct bme_map_list* map_list = &headermaplist[seg_idx]->map_list;
          struct bme_phys_dest phys_dest = { 0 };
          int err = assign_segment_physical_memory(phdr, mem_desc, &phys_dest,
                                                   map_list);
          if (err)
          {
            tprintf("bme: could not assign physical memory to program "
                    "segment\n");
            return -1;
          }

          // Add this pa and len to list of places to put this segment 
          // number.  Also record the mapping info, because a duplicate
          // destination will need it (it won't assign its own physical
          // memory).
          map_list->seg_type = seg_type;
          add_dest_to_segment_list(&seglist[seg_idx], &phys_dest,
                                   mem_desc, map_list);

          if (one_copy_per_group)
          {
            record_permanent_mappings(&mpg->tiles, map_list);
            break;
          }
          else
          {
            tile_mask tiles;
            clear_tile_mask(&tiles);
            add_tile_mask(&tiles, IDX2POS(tile_no));
            record_permanent_mappings(&tiles, map_list);
          }
        }
      }
    }
  }
  // FUTURE: when we switch to sorting the phdr array by type
  // before generating the segment destination list, we will have
  // to sort the segment destination list by file offset before
  // starting the actual loading.
  // After we have completed list of segment destinations, sort
  // the segment list again, by file_offset.  This is because we can't
  // seek backwards when actually loading from the .elf.

  return 0;
}


// FIXME: these are also in loader.c
/** ELF magic number. */
#define ELF_MAGIC (ELFMAG0 | (ELFMAG1 << 8) | (ELFMAG2 << 16) | (ELFMAG3 << 24))


#ifdef BME_DECOMPRESS
// FIXME: these are supposed to be static in loader.c
void init_decomp(void);
extern int do_read(int inode, char* buf, int length, int offset);
#else
/** Bzip2 magic number. */
#define BZ2_MAGIC ('B' | ('Z' << 8) | ('h' << 16))
/** Length of Bzip2 magic number. */
#define BZ2_MAGIC_LEN 3
#endif


/** Install and wire a DTLB entry that is cached locally.
 * @param page_va VA of page to wire.  Must be aligned to page_size.
 * @param page_pa PA of page to wire.  Must be aligned to page_size.
 * @param page_size Size of page to wire.
 */
static void
install_wired_dtlb(VA page_va, PA page_pa, uint32_t page_size)
{
  int shift = __insn_ctz(page_size);
  tte_w0_t attr =
  {{
    .v = 1,
    .w = 1,
    .mpl = HV_PL,
    .ps = TTE_SHIFT_TO_PS(shift),
    .g = 1,
    .asid = 0,
    .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
    .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
    .no_l1d_allocation = 0,
    .adaptive_allocation = 0,
    .pin = 0,
    .cache_prefetch = 0,
    .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
    .location_x_or_page_mask =  HV_LOTAR_X(my_lotar),
    .dtlbv = 1,
    .itlbv = 0,
  }};

  install_wired_tte(attr, page_va, page_pa);
}

/** Install a wired DTLB entry in a particular slot.  Just write over anything
 * that happens to be there already.  This has the effect of wiring all entries
 * up to that slot.
 * @param page_va VA of page to wire.  Must be aligned to page_size.
 * @param page_pa PA of page to wire.  Must be aligned to page_size.
 * @param page_size Size of page to wire.
 * @param slot Slot in which to wire the entry.
 */
static void
install_wired_dtlb_slot(VA page_va, PA page_pa, uint32_t page_size, int slot)
{
  int shift = __insn_ctz(page_size);
  tte_t tte= {
    {{
        .v = 1,
        .w = 1,
        .mpl = HV_PL,
        .ps = TTE_SHIFT_TO_PS(shift),
        .g = 1,
        .asid = 0,
        .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .no_l1d_allocation = 0,
        .adaptive_allocation = 0,
        .pin = 0,
        .cache_prefetch = 0,
        .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
        .location_x_or_page_mask =  HV_LOTAR_X(my_lotar),
        .dtlbv = 1,             /* FIXME make sure this is other places too */
        .itlbv = 0,
      }},
    { .word = page_va },
    { .word = page_pa },
  };

  __insn_mtspr(SPR_WIRED_DTLB, slot + 1);
  WRITE_TLB_AT(D, slot, tte);
}

/** Install wired DTLB entries to cover the address range given in the
 * parameters.  Addresses do not need to be page-aligned.
 * @param va VA of start of range of memory to wire into DTLB.
 * @param pa PA of start of range of memory to wire into DTLB.
 * @param memsz Size, in bytes, of range of memory to wire into DTLB.
 * @param pagesz Page size, in bytes, to use for DTLB entries.
 * @return The starting slot number at which these entries were wired (for
 *         unwiring later).
 */
static int
install_wired_dtlb_range(VA va, PA pa, uint32_t memsz, uint32_t pagesz)
{
  int nwired = __insn_mfspr(SPR_WIRED_DTLB);

  VA page_va = ROUND_DN(va, pagesz);
  PA page_pa = ROUND_DN(pa, pagesz);
  int64_t memsz_ctr = memsz + (va - page_va);
  int count = 0;

  while (memsz_ctr > 0)
  {
    install_wired_dtlb(page_va, page_pa, pagesz);
    page_va += pagesz;
    page_pa += pagesz;
    memsz_ctr -= pagesz;
    count++;
  }

  if (count + nwired >= CHIP_DTLB_ENTRIES())
  {
    dump_dtlb(0);
    panic("too many DTLB entries");
  }

  return nwired;
}

/** Install wired DTLB entries in the given mapping list.
 * @param map_list List of mappings to wire into the DTLB.
 * @return The starting slot number at which these entries were wired (for
 *         unwiring later).
 */
static int
install_wired_dtlb_mappings(struct bme_map_list* map_list)
{
  int nwired = __insn_mfspr(SPR_WIRED_DTLB);

  int count = 0;

  for (struct bme_map_info_list* elem = map_list->info_list;
       elem;
       elem = elem->next)
  {
    install_wired_dtlb(elem->page_va, elem->page_pa, elem->page_size);
    count++;
  }

  if (count + nwired >= CHIP_DTLB_ENTRIES())
  {
    dump_dtlb(0);
    panic("too many DTLB entries");
  }

  return nwired;
}

/** Helper function to figure out free memory segments, and put this
 * info into the global data structure.
 * @param shared_data Shared data area, including global info and mapping
 *        information.
 */
static void
calculate_free_mem_segs(struct bme_shared_data* shared_data)
{
  bme_free_mem_info_t* free_mem = shared_data->global_info->free_mem;
  int seg_idx = 0;

  // Find free segments on each memory controller.  We don't even try to 
  // combine segments between controllers.
  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    struct bme_mem_ctl* mem_ctl = &bme_mem_controller[i];

    if (mem_ctl->top <= mem_ctl->bottom)
    {
      BME_TRACE("No memory available on controller %d\n", i);
      continue;
    }

    PA start_pa = mem_ctl->bottom;

    // Find the first element, if any, in the allocated segment list 
    // that is between us and the top of the controller.  List of allocated
    // segments has been sorted in order of increasing PA.
    for (struct bme_allocated_seg_list* lp = allocated_seglist; lp;
         lp = lp->next)
    {
      if (lp->start_pa >= mem_ctl->top)
      {
        // We're done with this controller.  Take all the memory between here
        // and the top.
        free_mem[seg_idx].base_pa = start_pa;
        free_mem[seg_idx].len = mem_ctl->top - start_pa;
        BME_TRACE("To TOP: Free segment %d on controller %d: start_pa = %#llX "
                  "memsz = %#llX\n", seg_idx, i, free_mem[seg_idx].base_pa,
                  free_mem[seg_idx].len);
        seg_idx++;
        if (seg_idx >= BME_MAX_FREE_MEM_SEGS)
        {
          tprintf("bme: max allowable free memory segs reached, not recording "
                  "more\n");
          break;
        }
        BME_TRACE("Done with controller %d\n", i);
        continue;
      }

      if (start_pa < lp->start_pa)
      {
        // There is an allocated segment between us and the top of the 
        // controller.  Calculate this free segment's size, and the
        // next free segment's start address.
        free_mem[seg_idx].base_pa = start_pa;
        free_mem[seg_idx].len = lp->start_pa - start_pa;
        BME_TRACE("To NEXT: Free segment %d on controller %d: "
                  "start_pa = %#llX memsz = %#llX\n", seg_idx, i,
                  free_mem[seg_idx].base_pa, free_mem[seg_idx].len);
        seg_idx++;
        if (seg_idx >= BME_MAX_FREE_MEM_SEGS)
        {
          tprintf("bme: max allowable free memory segs reached, not recording "
                  "more\n");
          break;
        }
        start_pa = lp->start_pa + lp->memsz;
      }
    }

    // Been through the whole list.  See if there is any memory left between
    // us and the top of the controller.
    if (start_pa < mem_ctl->top)
    {
        free_mem[seg_idx].base_pa = start_pa;
        free_mem[seg_idx].len = mem_ctl->top - start_pa;
        BME_TRACE("End of list: Free segment %d on controller %d: "
                  "start_pa = %#llX memsz = %#llX\n", seg_idx, i,
                  free_mem[seg_idx].base_pa, free_mem[seg_idx].len);
        seg_idx++;
        if (seg_idx >= BME_MAX_FREE_MEM_SEGS)
        {
          tprintf("bme: max allowable free memory segs reached, not recording "
                  "more\n");
          break;
        }
        BME_TRACE("Done with controller %d\n", i);
    }
  }
  shared_data->global_info->num_free_mem_segs = seg_idx;
}


/** Fill in the I/O device information in the global data structure.
 * @param shared_data Shared data area, including global info and mapping
 *        information.
 */
static void
populate_global_io_table(struct bme_shared_data* shared_data)
{
  bme_global_info_t* global_info = shared_data->global_info;
  bme_io_dev_info_t* io_dev_info = { 0 };
  struct device* devp;

  for (devp = devices, io_dev_info = global_info->io_table;
       devp->name && (global_info->num_io_devices < BME_MAX_IO_DEVICES);
       devp++, io_dev_info++, global_info->num_io_devices++)
  {
    io_dev_info->shim_type = devp->shim_type;
    io_dev_info->instance = devp->instance;
    io_dev_info->flags = devp->flags;

    io_dev_info->num_stiles = devp->info.num_stiles;
    for (int i = 0; i < io_dev_info->num_stiles; i++)
      io_dev_info->stiles[i].word = devp->info.stiles[i].word;

    io_dev_info->num_dtiles = devp->info.num_dtiles;
    for (int i = 0; i < io_dev_info->num_dtiles; i++)
      io_dev_info->dtiles[i].word = devp->info.dtiles[i].word;

    io_dev_info->num_idn_ports = devp->info.num_idn_ports;
    for (int i = 0; i < io_dev_info->num_idn_ports; i++)
      io_dev_info->idn_ports[i].word = devp->info.idn_ports[i].word;

    io_dev_info->num_mdn_ports = devp->info.num_mdn_ports;
    for (int i = 0; i < io_dev_info->num_mdn_ports; i++)
      io_dev_info->mdn_ports[i].word = devp->info.mdn_ports[i].word;

    io_dev_info->channel = devp->info.channel;
    io_dev_info->intchan = devp->info.intchan;
    io_dev_info->num_intchan = devp->info.num_intchan;
    strncpy(io_dev_info->name, devp->name, BME_MAX_NAME_LEN);
    io_dev_info->name[BME_MAX_NAME_LEN - 1] = '\0';

    io_dev_info->owned_by_bme = devp->drv == NULL;
  }
}


/** Construct a bme_global_info_t based on number of tiles, max number of free
 * memory segments, the number of io devices, and size of the BME scratchpad.
 * This will be constructed early, and record_permanent_mappings() will simply
 * write to this struct.  Another function will extract those mappings and
 * put them in tte form, for handing to jump2_vaplsp_mappings().
 * @param shared_data Shared data area, including global info and mapping
 *        information.
 */
static void
construct_shared_data_area(struct bme_shared_data* shared_data)
{
  bme_global_info_t* global_info;

  // Get some memory under us, then skip past the shared data structure.
  intptr_t next = (intptr_t)shared_data;
  next += sizeof (*shared_data);

  // The global info structure itself.
  next = ROUND_UP(next, 8);
  global_info = (bme_global_info_t*) next;
  shared_data->global_info = global_info;
  next += sizeof (*global_info);

  // Fill in pointers to and lengths of the variable-sized regions we'll
  // be using, also part of shared_data.
  
  // Tile table.
  next = ROUND_UP(next, 8);
  global_info->tile_table = (bme_tile_info_t*) next;
  global_info->num_tiles = HV_TILES; /* FIXME: how to tell 64 vs 36? */
  next += global_info->num_tiles * sizeof (*global_info->tile_table);

  // Free memory table.
  next = ROUND_UP(next, 8);
  global_info->free_mem = (bme_free_mem_info_t*) next;
  next += BME_MAX_FREE_MEM_SEGS * sizeof (*global_info->free_mem);

  // Extra file table.
  next = ROUND_UP(next, 8);
  global_info->extra_file = (bme_extra_file_info_t*) next;
  next += BME_MAX_EXTRA_FILES * sizeof (*global_info->extra_file);

  // I/O device table.
  next = ROUND_UP(next, 8);
  global_info->io_table = (bme_io_dev_info_t*) next;
  next += BME_MAX_IO_DEVICES * sizeof (*global_info->io_table);

  // Scratchpad area.
  next = ROUND_UP(next, CHIP_L2_LINE_SIZE());
  global_info->scratchpad = (void*) next;
  global_info->scratchpad_len = BME_SCRATCHPAD_LEN_BYTES;
  next += global_info->scratchpad_len;

  // Command line.
  global_info->bme_app_args = (char*) next;

  // Before actually writing command line, check to make sure we're not writing
  // off the end of the page.  Might as well advance pointer here too.
  next += config.clients[my_client].arg.len + 1;
  if (next > (intptr_t)global_info + BME_SHARED_DATA_SIZE)
    panic("shared BME data too large for reserved area: %ld vs %d",
          (intptr_t)next - (intptr_t)global_info,
          BME_SHARED_DATA_SIZE);

  // Write command line.
  if (config.clients[my_client].arg.len > 0)
    fs_pread(config.clients[my_client].arg.ino,
             global_info->bme_app_args,
             config.clients[my_client].arg.len,
             config.clients[my_client].arg.off);

  global_info->bme_app_args[config.clients[my_client].arg.len] = '\0';

  // BIB.
  next = ROUND_UP(next, 8);
  global_info->bib_len = bi_block_length();
  global_info->bib_buf = (uint32_t*)next;
  bi_block_copy(global_info->bib_buf, global_info->bib_len);
  next += global_info->bib_len;

  // CPU speed.
  global_info->cpu_speed = cpu_speed;

  // JTAG lock physical address and Lotar.
  global_info->shared_lock_pa = shared_lock_page_pa;
  global_info->shared_lock_lotar = shared_lock_page_lotar;

  // L2 cache flush region.
  next = ROUND_UP(next, HV_FLUSH_PAGE_SIZE);
  // FIXME: add a #define for this magic VA above
  global_info->flush_va = 0xFFE00000;
  global_info->flush_pa = ROUND_DN(next - (intptr_t) shared_data +
                                   shared_data->shared_data_pa,
                                   HV_FLUSH_PAGE_SIZE);
  global_info->flush_offset = next - (intptr_t) shared_data +
                              shared_data->shared_data_pa -
                              global_info->flush_pa;
  global_info->flush_ps = TTE_SHIFT_TO_PS(HV_FLUSH_PAGE_SHIFT);
  next += HV_FLUSH_PAGE_SIZE;

  // We've allocated all of the variable-length stuff; verify that we stayed
  // in bounds.
  if (next > (intptr_t)global_info + BME_SHARED_DATA_SIZE)
    panic("shared BME data too large for reserved area: %ld vs %d",
          (intptr_t)next - (intptr_t)global_info,
          BME_SHARED_DATA_SIZE);

  // Miscellaneous extras.
  global_info->console_tile = chip_console;

  //
  // Set up data needed by the fence_incoherent operation.
  //
    memcpy(global_info->fence_incoherent_pas, hv_fence_incoherent_pas,
           sizeof (global_info->fence_incoherent_pas));

  // Set tile owners.
  for (int i = 0; i < global_info->num_tiles; i++)
  {
    global_info->tile_table[i].index = i;
    global_info->tile_table[i].pos.word = IDX2POS(i).word;
    global_info->tile_table[i].exec_type = EXEC_TYPE_HV;
    global_info->tile_table[i].client_num = -1;

    for (int j = 0; j < config.nclients; j++)
      if (in_tile_mask(&config.clients[j].tiles, IDX2POS(i)))
      {
        global_info->tile_table[i].client_num = j;
        if (config.clients[j].flags & CLIENT_BME)
          global_info->tile_table[i].exec_type = EXEC_TYPE_BME;
        else
          global_info->tile_table[i].exec_type = EXEC_TYPE_LINUX;
      }
  }

  // Fill in a bunch of HV info.
  populate_global_io_table(shared_data);

  // Add the BME version.
  global_info->hv_interface_version =
    BME_VERSION_DEF(BME_CURRENT_VERSION_MAJOR, BME_CURRENT_VERSION_MINOR);
}

/** This function takes a list of bme_mem_seg_info_t structs that were 
 * recorded when we were setting up permanent mappings, and makes
 * two lists-- one of dtlb ttes, and one of itlb ttes-- suitable for use
 * with jump2_vaplsp_mappings().
 * @param mem_seg List of mappings to turn into TLB entries.
 * @param num_mem_segs Number of mappings in mem_seg list.
 * @param itte Array of resulting ITLB entries.
 * @param n_itte Number of resulting ITLB entries.
 * @param dtte  Array of resulting DTLB entries.
 * @param n_dtte Number of resulting DTLB entries.
 */
static void
mem_seg_info_to_tte_tables(bme_mem_seg_info_t* mem_seg, int num_mem_segs,
                           tte_t* itte, int* n_itte,
                           tte_t* dtte, int* n_dtte)
{
  *n_itte = 0;
  *n_dtte = 0;

  for (int i = 0; i < num_mem_segs; i++)
  {
    Lotar lotar = 0;
    int cacheable = 0;
    int loc_override = 0;
    bme_mem_seg_info_t* ms = &mem_seg[i];

    switch (ms->cache_mode)
    {
    case BME_CACHE_MODE_LOCAL:
      cacheable = 1;
      loc_override = 1;
      lotar = my_lotar;
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
      loc_override = 1;
      lotar = POS2LOTAR(ms->cache_coords);
      cacheable = 1;
      break;
    default:
      panic("unknown cache mode %#X", ms->cache_mode);
    }
     
    int shift = __insn_ctz(ms->size);

    tte_t tte =
      {
        {{
            .v = 1,
            .w = (ms->seg_type == SEG_TYPE_RWDATA),
            .mpl = HV_PL,
            .ps = TTE_SHIFT_TO_PS(shift),
            .g = 1,
            .asid = 0,
            .memory_attribute = cacheable ?
            SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT :
            SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE,
            .cache_home_mapping = loc_override ?  // FIXME: verify
            SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE :
            SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_HASH,
            .no_l1d_allocation = 0,
            .adaptive_allocation = 0,
            .pin = 0,
            .cache_prefetch = 0,
            .location_y_or_page_offset = HV_LOTAR_Y(lotar),
            .location_x_or_page_mask =  HV_LOTAR_X(lotar),
            .dtlbv = 1,
            .itlbv = 0,
          }},
        { .word = ms->base_va },
        { .word = ms->base_pa },
  };

    switch (ms->seg_type)
    {
    case SEG_TYPE_TEXT:
      if (*n_itte >= CHIP_ITLB_ENTRIES())
        panic("BME program needs too many ITLB entries");
      ms->itlb_index = *n_itte;
      itte[(*n_itte)++] = tte;
      ms->dtlb_index = -1;
      break;

    case SEG_TYPE_TEXT_RODATA:
      if (*n_itte >= CHIP_ITLB_ENTRIES())
        panic("BME program needs too many ITLB entries");
      if (*n_dtte >= CHIP_DTLB_ENTRIES())
        panic("BME program needs too many DTLB entries");
      ms->itlb_index = *n_itte;
      itte[(*n_itte)++] = tte;
      ms->dtlb_index = *n_dtte;
      dtte[(*n_dtte)++] = tte;
      break;
    
    case SEG_TYPE_RODATA:
    case SEG_TYPE_RWDATA:
      if (*n_dtte >= CHIP_DTLB_ENTRIES())
        panic("BME program needs too many DTLB entries");
      ms->itlb_index = -1;
      ms->dtlb_index = *n_dtte;
      dtte[(*n_dtte)++] = tte;
      break;
    
    default:
      panic("unknown seg type %#x in mem_seg_info_to_tte_tables", ms->seg_type);
    }
  }
}

/** Map the shared BME global data area.  This is set up by the boot tile
 * but all tiles must map it in order to use it.
 */
static void
map_shared_bme_data()
{
  int first_mem_ctl;
  // Find the first memory controller that has some memory assigned.
  for (first_mem_ctl = 0; first_mem_ctl < MAX_MSHIMS; first_mem_ctl++)
  {
    if (config.clients[my_client].mem_len[first_mem_ctl] > 0)
      break;
  }
  if (first_mem_ctl == MAX_MSHIMS)
    panic("no memory for BME client");
  // FIXME: don't panic when BME can't load

  // Set up the memory controller info.
  for (int i = first_mem_ctl; i < MAX_MSHIMS; i++)
  {
    bme_mem_controller[i].bottom = config.clients[my_client].mem_base[i];
    bme_mem_controller[i].top =
      bme_mem_controller[i].bottom + config.clients[my_client].mem_len[i];
  }

  BME_TRACE("Loading the BME.  Memory controllers:\n");
  for (int i = 0; i < MAX_MSHIMS; i++)
    BME_TRACE("top[%d] = %#llX, bottom[%d] = %#llX\n", 
              i, bme_mem_controller[i].top,
              i, bme_mem_controller[i].bottom);

  // Carve out a chunk of memory for bringup communication with client tiles
  // (mapping info for each tile, for example).  Put this at the top of the
  // first memory controller.

  uint32_t shared_page_size = BME_SHARED_DATA_SIZE;
  VA shared_page_va = BME_SHARED_DATA_VA_BASE;
  PA shared_page_pa = ROUND_DN(bme_mem_controller[first_mem_ctl].top - 
                               shared_page_size,
                               shared_page_size);
  bme_mem_controller[first_mem_ctl].top = shared_page_pa;

   int cacheable = 1;

  int shift = __insn_ctz(shared_page_size);
  tte_w0_t shared_attr =
  {{
    .v = 1,
    .w = 1,
    .mpl = HV_PL,
    .ps = TTE_SHIFT_TO_PS(shift),
    .g = 1,
    .asid = 0,
    .memory_attribute = cacheable ?
    SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT :
    SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE,
    .cache_home_mapping = 
    SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
    .no_l1d_allocation = 0,
    .adaptive_allocation = 0,
    .pin = 0,
    .cache_prefetch = 0,
    .location_y_or_page_offset = config.clients[my_client].start_tile.bits.y,
    .location_x_or_page_mask = config.clients[my_client].start_tile.bits.x,
    .dtlbv = 1,
    .itlbv = 0,
  }};

  install_wired_tte(shared_attr, shared_page_va, shared_page_pa);

  shared_data = (struct bme_shared_data*)shared_page_va;

  if (config.clients[my_client].start_tile.word == my_pos.word)
    memset(shared_data, 0, BME_SHARED_DATA_SIZE);

  shared_data->shared_data_pa = shared_page_pa;
  shared_data->global_info_tte = (tte_t)
  {
    .w0 = shared_attr,
    .w1.word = shared_page_va,
    .w2.word = shared_page_pa
  };
}


/** Maximum supported page size */
#define BME_MAX_PAGE_SIZE     (1 << PG_SHIFT_16M)
/** Maximum possible virtual address */
#define BME_MAX_VA (~(VA)0)

/** Get one aligned page, bounded by the given low and high virtual
 * addresses.  Given the VA and size of some memory, find a page that is
 * within the bounds of the low and high va.  If a single page cannot meet
 * the requirements, return the largest page and page size that encompasses
 * the start of the memory.
 * @param low_va lower bound of VA range for this page.
 * @param high_va high bound of VA range for this page.
 * @param va VA of the memory we are mapping.
 * @param memsz Size, in bytes, of the memory range we are mapping.
 * @param ppage_va Pointer to VA of the resulting page.
 * @param ppage_size Pointer to the resulting page size.
 */
static void
get_bounded_page(VA low_va, VA high_va, VA va, uint32_t memsz, VA* ppage_va,
                 uint32_t* ppage_size)
{
  VA page_va;
  uint32_t page_size;
  uint32_t max_page_size = BME_MAX_PAGE_SIZE;

  do {
    get_smallest_aligned_page_less_than(max_page_size, va, memsz,
                                        &page_va, &page_size);
    BME_TRACE("%s: low_va = %#lX, high_va = %#lX, va = %#lX, memsz = %#X, "
              "page_va = %#lX, max_page_size = %#X, page_size = %#X\n",
              __FUNCTION__, low_va, high_va, va, memsz, page_va,
              max_page_size, page_size);

    max_page_size = page_size >> 2;

  } while ((page_va < low_va) || (page_va + page_size) > high_va);

  *ppage_va = page_va;
  *ppage_size = page_size;
}


/** Create a list of mappings, bounded by the given low and high virtual
 * addresses, for a portion of memory.
 * @param low_va lower bound of VA range for this mapping.
 * @param high_va high bound of VA range for this mapping.
 * @param va VA of the memory range we are mapping.
 * @param memsz Size, in bytes, of the memory range we are mapping.
 * @param mapping_list Resulting list of mappings.
 */
static void
create_bounded_mappings(VA low_va, VA high_va, VA va, uint32_t memsz,
                        struct bme_map_list* mapping_list)
{
  VA page_va;
  uint32_t page_size;
  int64_t rem_memsz = memsz;

  while (rem_memsz > 0)
  {
    // Get the smallest page within the boundaries that will cover all of our
    // memory, or if that is not possible, get the largest page that can be 
    // aligned that covers the first part of the remaining memory.
    get_bounded_page(low_va, high_va, va, (uint32_t)rem_memsz, &page_va,
                     &page_size);

    BME_TRACE("%s: low_va = %#lX, high_va = %#lX, va = %#lX, memsz = %#llX, "
              "page_va = %#lX, page_size = %#X\n", __FUNCTION__,
              low_va, high_va, va, rem_memsz, page_va, page_size);

    // Give dummy value for PA.  This will get assigned later, when we
    // assign actual memory to this mapping.
    add_mapping_to_list(mapping_list, -1, page_va, page_size);

    rem_memsz -= (page_va + page_size) - va;
    va = page_va + page_size;
    low_va = va;
  }
}

/** Verify and remove GNU_STACK program headers from the array.
 * Other unknown headers will cause us to generate a warning.
 */
static int
verify_gnu_stack(BME_Elf_Phdr* phdr, int nsegs)
{
  int i, j = 0;
  for (i = 0; i < nsegs; i++)
  {
    BME_Elf_Phdr* p = &phdr[i];
    if (p->p_type == PT_LOAD)
    {
      phdr[j++] = *p;
    }
    else if (p->p_type == PT_GNU_STACK)
    {
      if (p->p_flags & PF_X)
        panic("can't load client; BME does not support executable stacks");
    }
    else
    {
      tprintf("bme: warning: unknown program header type (%#x)",
              (int)p->p_type);
    }
  }
  return j;
}

/** Generate virtual address mappings for all the program headers in the
 * elf file that fit the alignment constraints, while minimizing the number
 * of pages per segment.
 * @param phdr Array of ELF program headers.
 * @param nsegs Number of program headers in phdr array.
 * @param global_mappings Pointer to returned list of mappings for the 
 *        program segments.
 */
static void 
generate_segment_mappings(BME_Elf_Phdr* phdr, int nsegs, 
                          struct bme_header_and_mapping** global_mappings)
{
  VA low_va = 0;
  VA high_va;

  int i;
  for (i = 0; i < nsegs; i++)
  {
    BME_Elf_Phdr* next_hdr = &phdr[i + 1];
    high_va = i < (nsegs - 1) ?
      ROUND_DN(next_hdr->p_vaddr, next_hdr->p_align) : BME_MAX_VA;
    
    BME_TRACE("creating mappings for segment %d\n", i);

    global_mappings[i] = bme_alloca(sizeof(*global_mappings[i]));
    memset(global_mappings[i], 0, sizeof(*global_mappings[i]));
    memcpy(&global_mappings[i]->phdr, &phdr[i], sizeof(*phdr));
    create_bounded_mappings(low_va, high_va, phdr[i].p_vaddr, phdr[i].p_memsz,
                            &global_mappings[i]->map_list);
    low_va = high_va;
  }
}


/** Helper macro to calculate minumum of two numbers. */
#define MIN(A,B) (((A) < (B)) ? (A) : (B))

/** Load an ELF executable into physical memory.
 * @param inode Inode of the file to load from the hypervisor filesystem.
 * @return Client physical address of the executable entry point.
 */
PA
bme_load(int inode)
{
  // See if this is a compressed file, and if so set up for decompression.
  uint32_t bz2_magic = 0;

  if (fs_pread(inode, (char*) &bz2_magic, BZ2_MAGIC_LEN, 0) == BZ2_MAGIC_LEN &&
      bz2_magic == BZ2_MAGIC)
#ifdef BME_DECOMPRESS
    init_decomp();
#else
    panic("can't load client; BME does not support compressed executables");
#endif

  // Do header checks.
    BME_Elf_Ehdr elf_header;

#ifdef BME_DECOMPRESS
  if (do_read(inode, (char*) &elf_header, sizeof (elf_header), 0) !=
      sizeof (elf_header) || elf_header.e_ehsize < sizeof (elf_header))
#else
    if (fs_pread(inode, (char*) &elf_header, sizeof (elf_header), 0) !=
        sizeof (elf_header) || elf_header.e_ehsize < sizeof (elf_header))
#endif
      panic("can't load client; ELF header too short");

  // FIXME probably wrong for gx
  uint32_t* elf_magic = (uint32_t*) &elf_header;
  if (*elf_magic != ELF_MAGIC)
    panic("can't load client; bad magic number");

  if (elf_header.e_machine != CHIP_ELF_TYPE() &&
      elf_header.e_machine != CHIP_COMPAT_ELF_TYPE())
    panic("can't load client; ELF file wrong architecture");

  // Bug 13183: remove uses of xdn from hx build.  Enable when
  // fixed.
#if 0 // #ifndef __tilegx_ise0__
  if (elf_header.e_flags & (1 << EF_TILEGX_ISE0))
    panic("can't load client; ISE0 not supported");
#endif

  if (elf_header.e_flags & (1 << EF_TILEGX_ISE1))
    panic("can't load client; ISE1 not supported");


  if (elf_header.e_type != ET_EXEC)
    panic("can't load client; file is not executable");


  if (elf_header.e_phentsize < sizeof(BME_Elf_Phdr))
    panic("can't load client; no program headers");

  int nsegs = elf_header.e_phnum;

  BME_Elf_Phdr phdrs[nsegs];

  //
  // Read in the program header table.  We do this all at once, and then do
  // another pass through them to load the actual segments, rather than
  // reading them one at a time and loading each one as we see it.  This is
  // because if we're dealing with a compressed input file we can't go
  // backwards from a loaded segment to get the next phdr.
  //
#ifdef BME_DECOMPRESS
  if (do_read(inode, (char*) phdrs, sizeof (phdrs), elf_header.e_phoff) !=
      sizeof (phdrs))
#else
    if (fs_pread(inode, (char*) phdrs, sizeof (phdrs), elf_header.e_phoff) !=
        sizeof (phdrs))
#endif
      panic("can't load client; ELF program headers too short");

  // Verify and remove GNU_STACK program headers from the array.
  nsegs = verify_gnu_stack(phdrs, nsegs);

  struct bme_header_and_mapping** global_segmap_table = 
    alloca(sizeof(*global_segmap_table) * nsegs);
  memset(global_segmap_table, 0,
         sizeof(*global_segmap_table) * nsegs);

  struct bme_seg_dest_list** dest_seglist_table = 
    alloca(sizeof(*dest_seglist_table) * nsegs);
  memset(dest_seglist_table, 0,
         sizeof(*dest_seglist_table) * nsegs);

  // Convert any remaining references to the nearest memory controller to
  // a specific, real controller.
  for (struct bme_mem_placement_group* mpg =
         config.clients[my_client].bme_groups; mpg; mpg = mpg->next)
    bme_fixup_nearest(mpg);

  // Sort the list of progam headers by virtual address, in preparation
  // for calculation of mappings.
  qsort(phdrs, nsegs, sizeof (BME_Elf_Phdr), phdr_compare_va);

  // Generate the list of mappings for all of the program segments.
  generate_segment_mappings(phdrs, nsegs, global_segmap_table);

  // Sort header and mapping list by file offset.
  qsort(global_segmap_table, nsegs, sizeof(*global_segmap_table), 
        segmap_compare_file_offset);

  // Generate a list of destinations for each segment.
  generate_segment_destinations(&config.clients[my_client], nsegs,
                                global_segmap_table, dest_seglist_table);

  // Print the entire segment list and destinations.
  for (int i = 0; i < nsegs; i++)
  {    
    BME_TRACE("Segment %d destinations:\n", i);
    for (struct bme_seg_dest_list* lp = dest_seglist_table[i]; lp;
         lp = lp->next)
    {
      BME_TRACE("  pa:%#llX filesz:%#X memsz:%#X\n",
                lp->dest.pa, lp->dest.filesz, lp->dest.memsz);
    }
  }

  // Take list of segment destinations, sorted by file offset, 
  // and load them into memory.
  for (int i = 0; i < nsegs; i++)
  {
    struct bme_seg_dest_list* seglist = dest_seglist_table[i];

    // Load this segment to all destinations.  First copy is done directly 
    // from file to a destination, all the rest are done via memcpy.
    // All addresses have been checked by this point.
    //
    struct bme_phys_dest* dest = &seglist->dest;

    // Wire down the entire range of memory for the first destination of this
    // segment.  Other copies of this segment will be copied out of this
    // memory.
    //
    VA start_va = (uint32_t)dest->pa;
    VA temp_page_va;
    uint32_t page_size, npages;
    get_smallest_aligned_pages(start_va, dest->memsz, &temp_page_va,
                               &page_size, &npages);

    BME_TRACE("Installing wired DTLB range: start_va = %#lX, "
              "dest->pa = %#llX memsz = %#X page_size = %#X\n",
              start_va, dest->pa, dest->memsz, page_size);
    int start_dtlb_slot = install_wired_dtlb_range(start_va, dest->pa,
                                                   dest->memsz, page_size);

    // Read the segment from the .elf.
    //
#ifdef BME_DECOMPRESS
    if (do_read(inode, (char*)start_va, dest->filesz,
                dest->offset) != dest->filesz)
#else
    int fz = fs_pread(inode, (char*)start_va, dest->filesz, dest->offset);

    BME_TRACE("Putting segment %d at va %#lX pa %#llX\n",
              i, start_va, dest->pa);

    if (fz != dest->filesz)
#endif
      panic("can't load BME; unexpected error or end of file: "
            "segno = %d fz = %d dest->filesz = %d", i, fz, dest->filesz);

    // Zero out any remaining segment pages that are not copied in from
    // the file.
    //
    if (dest->filesz < dest->memsz)
      memset((void*) (start_va + dest->filesz), 0, dest->memsz - dest->filesz);

    // Copy subsequent copies, using only one additional DTLB entry.
    //
    for (seglist = seglist->next; seglist; seglist = seglist->next)
    {
      // Loop through any subsequent copies, setting up temporary
      // DTLB mappings and memcpying from the first location.
      //
      struct bme_phys_dest* dest2 = &seglist->dest;
  
      int64_t rem_memsz = dest2->memsz;
      PA seg_pa = dest2->pa;
      VA original = start_va;

      int dtlb_slot = __insn_mfspr(SPR_WIRED_DTLB);

      while (rem_memsz > 0)
      {
        // Set up temporary DTLB entries for first destination
        VA temp_va = (seg_pa & 0xffffff) + 0xdd000000;
        VA temp_page_va;
        uint32_t page_size, npages;
        get_smallest_aligned_pages(temp_va, rem_memsz, &temp_page_va,
                                   &page_size, &npages);
        PA temp_page_pa = ROUND_DN(seg_pa, page_size);
        BME_TRACE("copy seg: installing dtlb entry va=%#lX pa=%#llX "
                  "ps=%#X slot=%d\n",
                  temp_page_va, temp_page_pa, page_size, dtlb_slot);
        install_wired_dtlb_slot(temp_page_va, temp_page_pa, page_size,
                                dtlb_slot);

        BME_TRACE("Putting segment %d at va %#lX pa %#llX\n",
                  i, temp_va, seg_pa);

        uint32_t rem_page_size = (temp_page_va + page_size) - temp_va;
        uint32_t copy_size = MIN(rem_memsz, rem_page_size);

        // Now copy the portion of the segment that goes on this
        // page from first dest in memory to this one
        memcpy((void*)temp_va, (void*)original, copy_size);

        seg_pa += copy_size;
        rem_memsz -= copy_size;
        original += copy_size;

        finv_range(temp_va, copy_size);
      }

      __insn_mtspr(SPR_WIRED_DTLB, dtlb_slot);
    }

    finv_range(start_va, dest->memsz);

    // Then unwire all the temporary entries.
    __insn_mtspr(SPR_WIRED_DTLB, start_dtlb_slot);

    // Clean the DTLB so we don't get conflicts later.
    clean_dtlb(0);
  }

#ifdef BME_DECOMPRESS
  reset_decomp();
#endif

  return elf_header.e_entry;
}


/** Load any extra files that are specified in the config into memory.
 * Do not set up any permanent memory mappings for it, but record where it
 * went in the global data structure so that users can access it however
 * they want.
 * @param my_config This client's config.
 * @return 0 on success; -1 if memory could not be assigned.
 */
static int
bme_load_extra_files(struct client_config* my_config)
{
  for (struct bme_mem_placement_group* mpg = my_config->bme_groups;
       mpg; mpg = mpg->next)
  {
    for (struct bme_extrafile_desc* config_extra = mpg->extra; 
         config_extra; config_extra = config_extra->next)
    {
      int inode = config_extra->bin_ino;
      if (inode < 0)
      {
        tprintf("bme: file %s not found, continuing\n", config_extra->name);
        continue;
      }

      // Get the size of the file.
      int file_size;
      unsigned int flags;
      fs_stat(inode, &file_size, &flags);

      // Get some physical memory for this file.
      struct bme_map_list dummy_map_list = { 0 };
      VA file_va = 0xde000000;
      PA file_pa;

      int err = assign_pertile_physical_memory(file_va, 0, file_size,
                                               &config_extra->mem_desc,
                                               &dummy_map_list, &file_pa);
      if (err)
      {
        tprintf("bme: could not assign memory to extra file\n");
        return -1;
      }

      int dtlb_slot = __insn_mfspr(SPR_WIRED_DTLB);

      // Update the global data structure so that the user can find
      // the file at runtime.
      int file_idx = shared_data->global_info->num_extra_files;
      bme_extra_file_info_t* global_extra = 
        &shared_data->global_info->extra_file[file_idx];
      strncpy(global_extra->name, config_extra->name, BME_MAX_NAME_SIZE);
      global_extra->name[BME_MAX_NAME_SIZE - 1] = '\0';
      global_extra->len = file_size;
      global_extra->base_pa = file_pa;
      shared_data->global_info->num_extra_files++;

      while (file_size > 0)
      {
        // Set up temporary mapping.
        VA temp_page_va;
        uint32_t page_size, npages;
        get_smallest_aligned_pages(file_va, file_size, &temp_page_va,
                                   &page_size, &npages);

        PA temp_page_pa = ROUND_DN(file_pa, page_size);
        install_wired_dtlb_slot(temp_page_va, temp_page_pa, page_size,
                                dtlb_slot);
        
        // Load the file into memory.
        int rem_page_size = (temp_page_va + page_size) - file_va;
        int copy_size = MIN(file_size, rem_page_size);

        int fz = fs_pread(inode, (char *)file_va, copy_size, 0);
        if (fz != copy_size)
        {
          panic("can't load BME extra file; unexpected error or end of file: "
                "expected to read %#X bytes, read %#X", fz, copy_size);
        }
        BME_TRACE("Putting segment of extra file %s at VA %#lX, PA %#llX, "
                  "size = %#X",
                  config_extra->name, file_va, file_pa, copy_size);

        file_size -= copy_size;
        file_pa += copy_size;
        file_va += copy_size;
      }

      // Unwire the dtlb slot we just were using.
      __insn_mtspr(SPR_WIRED_DTLB, dtlb_slot);

      // Clean the DTLB so we don't get conflicts later.
      clean_dtlb(0);
    }
  }

  return 0;
}


/** Load the client program into the client's memory.
 * @return Client physical address of loaded executable's entry point.
 */
CPA
load_bme()
{
  // Initialize temporary BME memory for holding dynamically 
  // allocated lists.  This memory is on the stack, so as soon as
  // we exit this function, it all disappears.
  // Access this memory by calling bme_alloca().
  bme_temp_memory = alloca(BME_TEMP_MEMORY_SIZE);

  map_shared_bme_data();
  construct_shared_data_area(shared_data);

  // Load the client application.
  if (config.clients[my_client].bin_ino < 0)
  {
    tprintf("bme: could not find bme app file\n");
    return 0;
  }
  PA pa = bme_load(config.clients[my_client].bin_ino);

  // Load any extra files.
  int err = bme_load_extra_files(&config.clients[my_client]);
  if (err)
  {
    tprintf("bme: could not load extra files\n");
  }

  // Write values for memory controller pointers into shared memory
  // for use in further calcluations by the client tiles.
  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    shared_data->bme_mem_controller[i].bottom = 
      bme_mem_controller[i].bottom;
    shared_data->bme_mem_controller[i].top = 
      bme_mem_controller[i].top;
  }

  flush_range((VA)shared_data, sizeof (*shared_data));

  bme_temp_memory = NULL;

  return pa;
}

/** Crunch the entire bme config to figure out the pertile memory placement
 * for each tile, for private data mode.  Search the config for bme tiles,
 * in increasing tile number, and get the pertile (stack/heap) info (page
 * aligned).
 * @param my_config This client's config.
 * @param heap_va Pointer to resulting VA of the heap.
 * @param heap_size Pointer to resulting heap size.
 * @param stack_va Pointer to resulting VA of the stack.
 * @param stack_pa Pointer to the PA of the assigned memory for the stack.
 */
static int
client_assign_private_pertile_memory(struct client_config* my_config,
                                     VA* heap_va, uint32_t* heap_size,
                                     VA* stack_va, PA* stack_pa)
{
  int found_mapping = 0;

  for (int tile = 0; tile < HV_TILES; tile++)
  {
    pos_t tile_pos = IDX2POS(tile);

    // Find the config for tile.  No tile can be in two groups, and
    // each tile must be in at least one group.
    for (struct bme_mem_placement_group* mpg = my_config->bme_groups;
         mpg; mpg = mpg->next)
    {
      if (in_tile_mask(&mpg->tiles, tile_pos))
      {
        struct bme_pertile_mem_desc* pertile = &mpg->pertile;
        uint32_t heapsize = ROUND_UP(pertile->heapsize, 8);
        uint32_t pertile_size = ROUND_UP(pertile->stacksize, 8) + heapsize;
        VA pertile_va = pertile->va == -1 ? BME_DEFAULT_PERTILE_PAGE_VA :
          pertile->va;

        struct bme_map_list map_list = { 0 };
        PA pertile_pa;

        // Here is the pertile config for "tile".
        // See which controller and where on the controller it wants to be,
        // and adjust that controller's pointers.
        int err = assign_pertile_physical_memory(pertile_va, 0, pertile_size,
                                                 &pertile->mem_desc, &map_list,
                                                 &pertile_pa);
        if (err)
        {
          tprintf("bme: could not assign private pertile memory\n");
          return -1;
        }

        // Make sure we don't wrap or overlap interrupt vector.
        VA end_va = pertile_va + map_list.total_memsz;
        if ((end_va < pertile_va) || (end_va >= HV_VA_BASE))
          panic("pertile VA space exceeds valid range: VA = %#lX, "
                "size = %#X", pertile_va, map_list.total_memsz);

        map_list.seg_type = SEG_TYPE_RWDATA;

        // Got to our tile.  Keep this mapping.
        if (tile == POS2IDX(my_pos))
        {
          tile_mask tiles;
          init_tile_mask(&tiles, my_pos, my_pos);
          record_permanent_mappings(&tiles, &map_list);
          install_wired_dtlb_mappings(&map_list);

          *heap_va = pertile_va;
          *heap_size = heapsize;
          *stack_va = *heap_va + pertile_size;
          *stack_pa = pertile_pa + pertile_size;
          BME_TRACE("PRIVATE: stack_va = %#lX, stack_pa = %#llX "
                    "(pertile_pa = %#llX pertile_size = %#X)\n", 
                    *stack_va, *stack_pa, pertile_pa, pertile_size);

          found_mapping = 1;
        }
      }
    }
  }

  assert(found_mapping);

  return 0;
}

/** Crunch the entire bme config to figure out the pertile memory placement
 * for each tile, for shared data mode.  Search the config for bme tiles,
 * in increasing tile number, and get the pertile (stack/heap) info (page
 * aligned).
 * @param my_config This client's config.
 * @param heap_va Pointer to resulting VA of the heap.
 * @param heap_size Pointer to resulting heap size.
 * @param stack_va Pointer to resulting VA of the stack.
 * @param stack_pa Pointer to the PA of the assigned memory for the stack.
 */
static int
client_assign_shared_pertile_memory(struct client_config* my_config,
                                    VA* heap_va, uint32_t* heap_size,
                                    VA* stack_va, PA* stack_pa)
{
  // If we are the start tile, add up the amount of memory needed (different
  // tiles can have different requirements).  Record mapping for all tiles in
  // the mask.  Also, all tiles need to map this memory immediately so that 
  // we can set up the stack.

  uint32_t total_len = 0;
  uint32_t my_offset = -1;
  struct bme_pertile_mem_desc* my_pertile = NULL;

  for (int tile = 0; tile < HV_TILES; tile++)
  {
    pos_t tile_pos = IDX2POS(tile);

    // Go group-by-group in order to extract the pertile config.
    for (struct bme_mem_placement_group* mpg = my_config->bme_groups;
         mpg; mpg = mpg->next)
    {
      if (in_tile_mask(&mpg->tiles, tile_pos))
      {
        if (tile == POS2IDX(my_pos))
        {
          my_offset = total_len;
          my_pertile = &mpg->pertile;
        }

        struct bme_pertile_mem_desc* pertile = &mpg->pertile;
        total_len += ROUND_UP(pertile->stacksize, 8) + 
          ROUND_UP(pertile->heapsize, 8);
      }
    }
  }

  BME_TRACE("Assigning shared memory, total_len = %#X\n", total_len);

  if (my_offset == -1)
    panic("trying to set up stack for non-BME tile");

  struct bme_map_list map_list = { 0 };
  PA pertile_pa;

  // Assign the physical memory for the shared pertile data area, and 
  // map it for all tiles in the client.
  // All tiles must use the same configuration for memory parameters
  // if they are in shared mode, so just use our own here.
  VA pertile_va = my_pertile->va == -1 ? BME_DEFAULT_PERTILE_PAGE_VA : 
    my_pertile->va;
  int err = assign_pertile_physical_memory(pertile_va, 0, total_len,
                                           &my_pertile->mem_desc, &map_list,
                                           &pertile_pa);
  if (err)
  {
    tprintf("bme: could not assign shared pertile memory\n");
    return -1;
  }

  // Make sure we don't wrap or overlap interrupt vector.
  VA end_va = pertile_va + map_list.total_memsz;
  if ((end_va < pertile_va) || (end_va >= HV_VA_BASE))
    panic("pertile VA space exceeds valid range: VA = %#lX, "
          "size = %#X", pertile_va, map_list.total_memsz);

  map_list.seg_type = SEG_TYPE_RWDATA;

  // Each tile must record its stack/heap permanent mapping.
  // This is being run on the client tile itself and the mapping
  // encompasses all of the shared data, so just record the mapping
  // for our tile.
  tile_mask tiles;
  init_tile_mask(&tiles, my_pos, my_pos);
  record_permanent_mappings(&tiles, &map_list);

  // Wire down this memory until we jump to the entry point, at which
  // time the mappings recorded above will be loaded.
  install_wired_dtlb_mappings(&map_list);

  uint32_t hsize = ROUND_UP(my_pertile->heapsize, 8);
  uint32_t ssize = ROUND_UP(my_pertile->stacksize, 8);
  *heap_va = pertile_va + my_offset;
  *heap_size = hsize;
  *stack_va = *heap_va + hsize + ssize;
  *stack_pa = pertile_pa + my_offset + hsize + ssize;

  BME_TRACE("SHARED: stack_va = %#lX, stack_pa = %#llX (pertile_pa = %#llX "
            "my_offset = %#X heap_size = %#X, stack_size = %#X)\n", 
            *stack_va, *stack_pa, pertile_pa, my_offset, hsize, ssize);

  return 0;
}

/** Start the BME. This is run on client tiles.
 * @param mshim_pa List of start PA for the memory controllers.
 * @param mshim_len Length in bytes of available memory on the controllers.
 * @param entrypoint The entrypoint to jump to in order to start the client.
 */
void
start_client_bme(const PA mshim_pa[MAX_MSHIMS], const PA mshim_len[MAX_MSHIMS],
                 CPA entrypoint)
{

  // Initialize temporary BME memory for holding dynamically 
  // allocated lists.  This memory is on the stack, so as soon as
  // we exit this function, it all disappears.
  // Access this memory by calling bme_alloca().
  bme_temp_memory = alloca(BME_TEMP_MEMORY_SIZE);

  // Convert any remaining references to the nearest memory controller to
  // a specific, real controller.
  for (struct bme_mem_placement_group* mpg =
         config.clients[my_client].bme_groups; mpg; mpg = mpg->next)
    bme_fixup_nearest(mpg);

  // Map shared data, unless we're the initial tile for this client, in which
  // case it's already mapped.
  // FIXME: verify that start_tile is guaranteed to be running start_client_bme()
  if (config.clients[my_client].start_tile.word != my_pos.word)
    map_shared_bme_data();

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    bme_mem_controller[i].bottom = shared_data->bme_mem_controller[i].bottom;
    bme_mem_controller[i].top = shared_data->bme_mem_controller[i].top;
  }

  // Assign memory and map the pertile data area (stack/heap).
  VA heap_va = 0, stack_va = 0;
  PA stack_pa = 0;
  uint32_t heap_size = 0;
  struct client_config* my_config = &config.clients[my_client];
  int err;
  if (my_config->flags & CLIENT_BME_PRIVATE)
    err = client_assign_private_pertile_memory(my_config, &heap_va, &heap_size,
                                               &stack_va, &stack_pa);
  else
    err = client_assign_shared_pertile_memory(my_config, &heap_va, &heap_size,
                                              &stack_va, &stack_pa);
  if (err)
  {
    tprintf("bme: could not assign pertile memory, exiting BME loader\n");
    return;
  }

  BME_TRACE("Assigned client pertile memory: stack_va = %#lX "
            "heap_va = %#lX\n", stack_va, heap_va);

  // Add info to the global data structure.  By now we know the heap location
  // and size, and can figure out the free memory segments left.
  bme_tile_info_t* tile_info =
    &shared_data->global_info->tile_table[POS2IDX(my_pos)];

  tile_info->heap_size = heap_size;
  tile_info->heap_start_va = heap_va;

  if (config.clients[my_client].start_tile.word == my_pos.word)
    calculate_free_mem_segs(shared_data);

  // Get lists of the ITLB and DTLB mappings from the global data struct,
  // for jump2_vaplsp_mapping().
  tte_t itte[CHIP_ITLB_ENTRIES()];
  tte_t dtte[CHIP_DTLB_ENTRIES()];
  int n_itte, n_dtte;
  mem_seg_info_to_tte_tables(tile_info->mem_seg, tile_info->num_mem_segs,
                             itte, &n_itte, dtte, &n_dtte);

  // Set the minimum protection level for the exceptions that will be
  // handled by the client to the client's PL.

  set_client_mpls();

  // Put local data in stack memory, advance stack pointer
  // past the local data.  Assign pointer to this local data
  // and pass it as an argument to the client.

  bme_local_info_t* loc_data =
    (bme_local_info_t*) stack_va - sizeof(*loc_data);
  loc_data->index = POS2IDX(my_pos);
  loc_data->global_info = shared_data->global_info;
  loc_data->global_info_tte = shared_data->global_info_tte;
  VA bme_sp = (VA)loc_data - C_ABI_SAVE_AREA_SIZE;
  bme_sp &= -8;  // Ensure 8-byte alignment for SP

  // Notify the simulator about the impending "exec".
  fs_sim_notify_exec(config.clients[my_client].bin_ino);

  // Jump to entrypoint, using function that will replace HV mappings.

  BME_TRACE("Jumping to entrypoint %#llX, sp = %#lX, local data at %p\n",
            entrypoint, bme_sp, loc_data);

  // Invalidate the entire cache before jumping to the entrypoint.
  // This will be too big a hammer if/when we support having all tiles
  // as BME tiles.
  inv_whole_l2();

  jump2_vaplsp_mappings((VA)entrypoint, HV_PL, bme_sp, (VA)loc_data,
                        itte, n_itte, dtte, n_dtte);
}
