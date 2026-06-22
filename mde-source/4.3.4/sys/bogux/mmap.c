/**
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
 *
 * Implements the do_mmap(), etc., system call handlers.
 * @file
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>
#include <assert.h>
#include <string.h>

#include <tnslock.h>
#include <hv/hypervisor.h>

#include <arch/chip.h>
#include <arch/interrupts.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "bogux.h"
#include "mman.h"
#include "devices.h"
#include "errno.h"
#include "pa_allocator.h"
#include "mem_layout.h"
#include "syscall.h"
#include "files.h"
#include "rand.h"

// Reference some objects declared in start.S
extern char zero_pages_data[];
extern HV_PTE l2_page_table[HV_L2_ENTRIES];
extern HV_PTE supervisor_page_table[HV_L1_ENTRIES];

// Reference some symbols declared in the linker script
extern char _slocks[], _elocks[];
extern char _stilestate[], _etilestate[];

// Forward-declare some functions.
static void install_pte(VirtualAddress, HV_PTE, bool, bool*);
typedef enum BzeroType { BZERO_NONE, BZERO_FLUSH, BZERO_FINV,
                         BZERO_FINV_ONLY } BzeroType;
static void pa_bzero_internal(HV_PhysAddr, size_t, BzeroType, HV_PhysAddr);
static void pa_bzero(HV_PhysAddr pa, size_t bytes, BzeroType);
static void pa_memcpy(HV_PhysAddr dest, VirtualAddress src, size_t bytes);
static int do_munmap_internal(VirtualAddress, uint32_t s, bool* common_locked);
static void for_each_pte(HV_PTE (*func)(HV_PTE, VirtualAddress, void*),
                         VirtualAddress, int64_t, void*);
#define LOTAR_HASH -1U


/** Max number of pools (since we expect at most one pool per controller). */
#define MAX_POOLS MAX_CONTROLLERS

/** Allocator per pool.
 * Note that we can't have these in a structure with other per-pool
 * stuff because we are user-managing this data across multiple tiles.
 */
static PA_Allocator pool_allocators[MAX_POOLS] _L2ALIGNED;

/** Hypervisor info per pool (start, size, and controller). */
static HV_PhysAddrRange pool_ranges[MAX_POOLS];

/** Locks per pool */
static int pool_locks[MAX_POOLS] _LOCKS;

/** Number of actual pools we are using. */
static unsigned int num_pools;

/** One more than the index of the largest controller seen in the pools. */
static unsigned int num_controllers;

/** CPAs for the zero page range on each controller */
static HV_PhysAddr zero_pages[MAX_CONTROLLERS];

/** Bitmap of valid controllers. */
static uint32_t controllers_present;

/** Physical address of the .locks section */
static HV_PhysAddr locks_pa;

/** ASID to be used on each tile */
static HV_ASID asid;

/** Width of the tile geometry in this supervisor */
int width;

/** Height of the tile geometry in this supervisor */
int height;

/** Available tiles */
int avail_tiles[(MAX_TILES+31)/32];

/** Number of available tiles (width x height, less reserved tiles) */
int num_avail_tiles;

/** Lotar of initial boot tile */
static unsigned int boot_lotar;

/** Settable control to allow doing PA-is-VA allocation */
static bool pa_is_va_loader = false;


////// Tile-specific data
////// Should all be initialized to avoid linker confusion.

/** MEM_SVMISC L2 page table for this tile.
 * This must be first in the tile-specific data area.
 */
static HV_PTE ts_svmisc_page_table[HV_L2_ENTRIES] _TILESTATE_1;

/** L1 page table for this tile.
 * This must be second in the tile-specific data area.
 */
static HV_PTE ts_l1_page_table[HV_L1_ENTRIES] _TILESTATE_2;

/** L0 page table for this tile.
 */
static HV_PTE __attribute__((aligned(HV_PAGE_TABLE_ALIGN)))
  ts_l0_page_table[HV_L0_ENTRIES] _TILESTATE;

/** Physical address of the tile-specific data page. */
static HV_PhysAddr ts_tilestate_pa _TILESTATE;

/** Coordinates of this tile. */
HV_Coord ts_coord _TILESTATE;

/** Nearest memory controller to this tile */
static int ts_local_controller _TILESTATE;

/** Default controller to use for memory allocation */
uint32_t ts_controller _TILESTATE;

/** Lowest address we've seen for the stack yet */
static VirtualAddress ts_stack_bottom _TILESTATE;

/** Highest address we've seen loaded, i.e. start of brk */
static VirtualAddress ts_brk_start _TILESTATE;

/** Current brk value */
static VirtualAddress ts_brk _TILESTATE;

/** Any priority pages in this page table? */
static bool ts_contains_priority_page _TILESTATE;

/** Are we oloc'ing non-common pages somewhere else? */
bool ts_default_oloc_enabled _TILESTATE;
/** Are we randomly determining what those other locations are? */
static bool ts_default_oloc_random _TILESTATE;
/** X coordinate we're oloc'ing to */
static int ts_default_oloc_x _TILESTATE;
/** Y coordinate we're oloc'ing to */
static int ts_default_oloc_y _TILESTATE;
/** Random number seed for random oloc'ing */
static uint32_t ts_random_oloc_seed _TILESTATE;

/////////////////////////////////////////////////////////////////////////////
// Code from Hypervisor.cc in tile-sim
/////////////////////////////////////////////////////////////////////////////

static int is_common_locked _LOCKS;

/** Values for CommonState.refcount */
enum {
  CS_UNUSED = 0,  /**< L1 PTE has not been allocated anywhere */
  CS_COMMON = -1  /**< L1 PTE has been allocated with MAP_COMMON */
};

/** Provide information on common vs. private state for L1 PTEs. */
typedef struct
{
  HV_PTE pte;     /**< What PTE should be used for common entries? */
  int refcount;   /**< CS_UNUSED, CS_COMMON, or a refcount if private */
} CommonState;

/** Track CommonState of each L1 entry */
static CommonState common_state[HV_L1_ENTRIES] _L2ALIGNED;

static void
lock_common()
{
  tnslock_rawlock(&is_common_locked);
}

static void
lock_common_if_necessary(bool* is_locked)
{
  if (!*is_locked)
  {
    *is_locked = true;
    lock_common();
  }
}

static void
unlock_common()
{
  // Flush the cache_state.  We could try to track (min,max) and only
  // flush that range, but that's extra book-keeping to avoid a loop
  // that should be only 64 calls to finv with the POR hardware.
  char* const end = (char*) &common_state[HV_L1_ENTRIES];
  for (char* p = (char*) common_state; p < end; p += CHIP_L1D_LINE_SIZE())
    __insn_finv(p);
  __insn_mf();
  is_common_locked = 0;
}

static void
unlock_common_if_necessary(bool* is_locked)
{
  if (*is_locked)
  {
    *is_locked = false;
    unlock_common();
  }
}

static void
assert_locked()
{
  // Don't do this for now, since we'd have to hit uncached memory
  // assert(is_common_locked);
}

static HV_PTE
get_common_l1_pte(VirtualAddress va)
{
  assert_locked();
  if (HV_L0_INDEX(va))
    return hv_pte(0);
  CommonState* cs = &common_state[HV_L1_INDEX(va)];
  HV_PTE pte;
  if (cs->refcount == CS_COMMON)
    pte = cs->pte;
  else
    pte = hv_pte(0);
  return pte;
}

static void
drop_common_l1_pte(VirtualAddress va)
{
  assert_locked();
  if (HV_L0_INDEX(va))
    return;
  CommonState* cs = &common_state[HV_L1_INDEX(va)];
  assert(cs->refcount == CS_COMMON);
  cs->refcount = CS_UNUSED;
  cs->pte = hv_pte(0);        // be tidy
}

static void
set_common_l1_pte(VirtualAddress va, HV_PTE pte, bool common)
{
  assert_locked();
  CommonState* cs = &common_state[HV_L1_INDEX(va)];

  if (common)
  {
    assert(!HV_L0_INDEX(va));
    assert(cs->refcount == CS_UNUSED);
    cs->refcount = CS_COMMON;
    cs->pte = pte;
  }
  else
  {
    if (HV_L0_INDEX(va))
      return;
    assert(cs->refcount != CS_COMMON);
    ++cs->refcount;
  }
}

static void
update_common_l1_pte(VirtualAddress va, HV_PTE pte)
{
  assert_locked();
  assert(!HV_L0_INDEX(va));
  CommonState* cs = &common_state[HV_L1_INDEX(va)];
  assert(cs->refcount == CS_COMMON);
  cs->pte = pte;
}

/////////////////////////////////////////////////////////////////////////////
// Code from PageTable.cc in tile-sim
/////////////////////////////////////////////////////////////////////////////

#if 0
/** Helper for dump_pte. */
#define DUMP_FIELD(name_str, val_str) \
  printf("  %-16s %s\n", name_str ":", val_str)

/** Helper for dump_pte. */
#define DUMP_BIT(name) \
  DUMP_FIELD(#name, hv_pte_get_##name(pte) ? "true" : "false")

/** Dump out a page table for debugging use. */
static HV_PTE
dump_pte(HV_PTE pte, VirtualAddress va, void* ptr)
{
  printf("VA %#08x PTE 0x%016llx:\n", va, hv_pte_val(pte));

  DUMP_BIT(present);
  DUMP_BIT(page);
  DUMP_BIT(readable);
  DUMP_BIT(writable);
  DUMP_BIT(executable);
  DUMP_BIT(accessed);
  DUMP_BIT(dirty);
  DUMP_BIT(cached_priority);
  DUMP_BIT(global);
  DUMP_BIT(user);

#undef DUMP_FIELD
#undef DUMP_BIT

  static const char* modes[1 << HV_PTE_MODE_BITS] =
  {
    [HV_PTE_MODE_UNCACHED] = "uncached",
    [HV_PTE_MODE_CACHE_NO_L3] = "cached no L3",
    [HV_PTE_MODE_CACHE_TILE_L3] = "cached tile L3",
    [HV_PTE_MODE_CACHE_HASH_L3] = "cached hash L3",
  };

  if (modes[hv_pte_get_mode(pte)])
    printf("  %-16s %s\n", "mode", modes[hv_pte_get_mode(pte)]);
  else
    printf("  %-16s unknown mode %d\n", "mode", hv_pte_get_mode(pte));

  printf("  %-16s 0x%08x (cpa 0x%llx)\n",
         "pfn", hv_pte_get_pfn(pte), HV_PFN_TO_CPA(hv_pte_get_pfn(pte)));

  HV_LOTAR lotar = hv_pte_get_lotar(pte);
  printf("  %-16s 0x%08x (x=%d, y=%d)\n",
         "lotar", lotar, HV_LOTAR_X(lotar), HV_LOTAR_Y(lotar));
  return pte;
}

/** Get the page table entry for a specific VA. */
static void
dump_page_table()
{
  for_each_pte(dump_pte, 0, -1, NULL);
}
#endif

/** Return the size of a page mapped by a PTE */
static unsigned long
pte_page_size(HV_PTE pte)
{
  return hv_pte_get_page(pte) ? HV_PAGE_SIZE_LARGE : HV_PAGE_SIZE_SMALL;
}

/** Does this PTE correspond to a page shared by multiple tiles? */
static inline bool
is_common_pte(HV_PTE pte)
{
  return hv_pte_get_client0(pte);
}

/** Make this PTE correspond to a page shared by multiple tiles? */
static inline HV_PTE
set_common_pte(HV_PTE pte)
{
  return hv_pte_set_client0(pte);
}

/** Does this PTE correspond to a copy-on-write page? */
static inline bool
is_copy_on_write_pte(HV_PTE pte)
{
  assert(hv_pte_get_present(pte));
  return hv_pte_get_client1(pte);
}

/** Make this PTE correspond to a copy-on-write page */
static inline HV_PTE
set_copy_on_write_pte(HV_PTE pte)
{
  return hv_pte_set_client1(pte);
}

/** Make this PTE not correspond to a copy-on-write page */
static inline HV_PTE
clear_copy_on_write_pte(HV_PTE pte, HV_PhysAddr cpa)
{
  pte = hv_pte_clear_client1(pte);
  pte = hv_pte_set_writable(pte);
  pte = hv_pte_set_dirty(pte);
  pte = hv_pte_set_pfn(pte, HV_CPA_TO_PFN(cpa));
  return pte;
}

/** Is this a valid PTE?
 * We will likely extend our definition of "valid" to include PTEs
 * that are marked not present but encode information in their bits.
 * For now we don't have any such.
 */
static inline bool
is_valid_pte(HV_PTE pte)
{
  return hv_pte_get_present(pte);
}

/** Read a PTE from a CPA */
static inline HV_PTE
read_pte(HV_PhysAddr pa, HV_PTE access_pte)
{
  return hv_pte(hv_physaddr_read64(pa, access_pte));
}

/** Write a PTE to a CPA */
static inline void
write_pte(HV_PhysAddr pa, HV_PTE access_pte, HV_PTE pte)
{
  hv_physaddr_write64(pa, access_pte, hv_pte_val(pte));
}

/** PTE describing how to read/write L1 page tables.
 * Since they are per-tile, we can always read them through our cache.
 * At some point we may wish to support something like threads, in
 * which case we will need to promote all use of local_access_pte
 * to use a LOTAR to some specific tile instead.
 */
static HV_PTE local_access_pte = {
  HV_PTE_READABLE |
  HV_PTE_WRITABLE |
  HV_PTE_PRESENT |
  (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE)
};


/** Read a PTE from an index in this tile's L0 page table. */
static inline HV_PTE
read_l0_pte(unsigned int index)
{
  assert(index < HV_L0_ENTRIES);
  return ts_l0_page_table[index];
}

/** Write a PTE to an index in this tile's L0 page table. */
static inline void
write_l0_pte(unsigned int index, HV_PTE pte)
{
  assert(index < HV_L0_ENTRIES);
  ts_l0_page_table[index] = pte;

  // FIXME The simulator doesn't currently correctly check for modifications
  // made to page tables by cached writes, so have to flush them all
  // explicitly for now.  We fence here as well just so the callers don't
  // have to be aware of this limitation.
  __insn_flush(&ts_l0_page_table[index]);
  __insn_mf();
}

/** Write a PTE to an index in this tile's MEM_SVMISC L2 page table. */
static inline void
write_svmisc_pte(unsigned int index, HV_PTE pte)
{
  assert(index < HV_L2_ENTRIES);
  ts_svmisc_page_table[index] = pte;

  // FIXME The simulator doesn't currently correctly check for modifications
  // made to page tables by cached writes, so have to flush them all
  // explicitly for now.  We fence here as well just so the callers don't
  // have to be aware of this limitation.
  __insn_flush(&ts_svmisc_page_table[index]);
  __insn_mf();
}

/** Is the given tile available for Bogux? */
bool
is_avail_tile(unsigned int tilenum)
{
  return (avail_tiles[tilenum/32] >> (tilenum % 32)) & 1;
}

/** Globally initialize the memory subsystem. */
void
init_physmem(HV_Topology topology)
{
  // Each tile gets the same ASID, since they're basically a per-tile resource.
  // We may want to revisit this decision.
  asid = hv_inquire_asid(0).start;

  // Get the information for each physical area of memory and
  // use it to initialize pools[], including the contained PA_Allocator's.
  // Also initialize num_pools and num_controllers.

  boot_lotar = HV_XY_TO_LOTAR(topology.coord.x, topology.coord.y);
  width = topology.width;
  height = topology.height;
  int rc = hv_inquire_tiles(HV_INQ_TILES_AVAIL, (HV_VirtAddr) avail_tiles,
                            sizeof(avail_tiles));
  if (rc != 0)
    panic("hv_inquire_tiles failed: %d", rc);
  for (int t = topology.width * topology.height - 1; t >= 0; --t)
    if (is_avail_tile(t))
      ++num_avail_tiles;

  num_controllers = 0;
  int i;
  for (i = 0; ; ++i)
  {
    // Get a CPA range from the controller.  "size == 0" means
    // that we've already gotten all the range data.
    HV_PhysAddrRange range = hv_inquire_physical(i);
    if (range.size == 0)
    {
      num_pools = i;
      break;
    }

    // Make sure we don't get out of sync with our expectations.
    assert(i < MAX_POOLS);

    // Track the largest controller value.
    controllers_present |= (1 << range.controller);
    if (range.controller >= num_controllers)
    {
      num_controllers = range.controller + 1;
    }

    // Reserve the first two large pages of CPA, since that's
    // where the supervisor was loaded.
    int64_t delta = (2 * HV_PAGE_SIZE_LARGE) - range.start;
    if (delta > 0)
    {
      range.start += delta;
      range.size -= delta;
    }

    // Initialize the allocator.
    pa_init(&pool_allocators[i], range.start, range.size);

    // Get zero-page memory from each controller
    if (zero_pages[range.controller] == 0)
    {
      // We special-case zero_pages_data just because this way when we are
      // running on the magic hypervisor we are fast, since the 32KB of
      // zero pages on the first controller are loaded "for free".  In a
      // real implementation we'd probably dynamically alloc them all.
      // Alternately, we could just forget about all this stuff, and not
      // map zero pages in at all; in that case we'd alloc a new page on
      // the first read, rather than the first write, and we wouldn't need
      // to have eight zero pages per memory controller allocated at all.
      if (delta > 0)
        zero_pages[range.controller] =
          va_to_cpa(zero_pages_data);
      else
      {
        zero_pages[range.controller] =
          pa_alloc_pages(&pool_allocators[i], 0, PA_COLORS*HV_PAGE_SIZE_SMALL);
        if (zero_pages[range.controller] == PA_FAILED)
          panic("No memory for zero pages on controller %d",
                range.controller);
      }
    }

    pool_ranges[i] = range;
  }

  // Allocate memory for locks
  locks_pa =
    pa_alloc_pages(&pool_allocators[0], (void*) MEM_LOCKS_VA, MEM_LOCKS_SIZE);
  if (locks_pa == PA_FAILED)
    panic("No memory for locks on controller zero");

  // Make all modified read-only state visible on other tiles.
  __insn_flush(&asid);
  __insn_flush(&width);
  __insn_flush(&height);
  __insn_flush(&num_avail_tiles);
  for (i = 0; i < sizeof(avail_tiles)/sizeof(avail_tiles[0]); ++i)
    __insn_flush(&avail_tiles[i]);
  __insn_flush(&num_controllers);
  __insn_flush(&controllers_present);
  __insn_flush(&num_pools);
  __insn_flush(&locks_pa);
  for (i = 0; i < num_pools; ++i)
    __insn_flush(&pool_ranges[i]);
}


// FIXME: Get rid of "hv_bzero_page".

static void
bzero_page(VirtualAddress va, size_t nbytes)
{
  memset((void*) va, 0, nbytes);
}

void
alloc_tile_mem(HV_Coord coord, struct AllocMemory* memory, HV_Coord my_coord)
{
  assert(num_pools != 0);

  unsigned int nearest = 0;
  unsigned int best_distance = (1<<31);
  if (num_pools > 1)
  {
    for (int i = 0; i < num_pools; ++i)
    {
      HV_MemoryControllerInfo info =
        hv_inquire_memory_controller(coord, pool_ranges[i].controller);
      unsigned int distance =
        abs(info.coord.x - coord.x) + abs(info.coord.y - coord.y);
      if (distance < best_distance)
      {
        best_distance = distance;
        nearest = i;
      }
    }
  }

  // Record this so we can put it in tile-specific memory later.
  memory->local_controller = nearest;

  // Allocate initial memory for each tile from its nearest allocator.
  PA_Allocator* alloc = &pool_allocators[nearest];
  memory->tile_state =
    pa_alloc_pages(alloc, (void*) MEM_TILESTATE_VA, MEM_TILESTATE_SIZE);
  memory->stack =
    pa_alloc_pages(alloc, (void*) MEM_STACK_VA, MEM_STACK_SIZE);
  if (memory->tile_state == PA_FAILED || memory->stack == PA_FAILED)
    panic("No memory for tile-specific data on controller %d", nearest);

  // Bzero the tile state, because it holds page tables, relying on
  // the fact that we are still running the preliminary (firstpages.c)
  // memory mapping that has a static L2 table mapped in that lets us
  // get at MEM_PAMAP.
  BzeroType type = BZERO_FLUSH;
  if (coord.x == my_coord.x && coord.y == my_coord.y)
    type = BZERO_NONE;   // no need to flush our own zeroing
  pa_bzero_internal(memory->tile_state, MEM_TILESTATE_SIZE, type,
                    ((HV_PhysAddr)(uintptr_t)l2_page_table -
                     MEM_DATA_PA_ADJUST));
}

void
init_tile(HV_Coord coord, const struct AllocMemory* memory)
{
  assert(((uintptr_t)&ts_l1_page_table & (HV_PAGE_TABLE_ALIGN-1)) == 0);
  unsigned int l1_offset = (uintptr_t)&ts_l1_page_table - MEM_TILESTATE_VA;
  assert(l1_offset < MEM_TILESTATE_SIZE);
  HV_PhysAddr l1_page_table = memory->tile_state + l1_offset;
  assert(((uintptr_t)&ts_l0_page_table & (HV_PAGE_TABLE_ALIGN-1)) == 0);
  unsigned int l0_offset = (uintptr_t)&ts_l0_page_table - MEM_TILESTATE_VA;
  assert(l0_offset < MEM_TILESTATE_SIZE);
  HV_PhysAddr l0_page_table = memory->tile_state + l0_offset;
  HV_PhysAddr page_table = l0_page_table;

  // Fill in the L0 page table.

  // Create one L0 entry which points to our L1 table.
  HV_PTE l0_pte = hv_pte(0);
  l0_pte = hv_pte_set_readable(l0_pte);
  l0_pte = hv_pte_set_writable(l0_pte);
  l0_pte = hv_pte_set_dirty(l0_pte);
  l0_pte = hv_pte_set_global(l0_pte);
  l0_pte = hv_pte_set_present(l0_pte);
  l0_pte = hv_pte_set_accessed(l0_pte);
  l0_pte = hv_pte_set_mode(l0_pte, HV_PTE_MODE_CACHE_NO_L3);

  assert(HV_L0_INDEX(MEM_CODE_VA) == HV_L0_INDEX(MEM_DATA_VA));

  l0_pte = hv_pte_set_ptfn(l0_pte, HV_CPA_TO_PTFN(l1_page_table));
  write_pte(l0_page_table + HV_L0_INDEX(MEM_CODE_VA) * HV_PTE_SIZE,
            local_access_pte, l0_pte);

  // Copy the large pages from the bootup page table.
  write_pte(l1_page_table + HV_L1_INDEX(MEM_CODE_VA) * HV_PTE_SIZE,
            local_access_pte,
            supervisor_page_table[HV_L1_INDEX(MEM_CODE_VA)]);
  write_pte(l1_page_table + HV_L1_INDEX(MEM_DATA_VA) * HV_PTE_SIZE,
            local_access_pte,
            supervisor_page_table[HV_L1_INDEX(MEM_DATA_VA)]);

  // Install the page table
  hv_install_context(page_table, local_access_pte, asid, HV_CTX_DIRECTIO);

  // Discard the temporary mappings we used earlier when initializing.
  // No point doing it before now since a TSB miss would just bring it back.
  hv_flush_pages(0, HV_PAGE_SIZE_LARGE, 2 * HV_PAGE_SIZE_LARGE);

  // Map in a second-level pagetable at the bottom of supervisor space
  // that we can hang random pages off of.
  // All supervisor global page table entries use the local_access_pte, since
  // they will not be shared across tiles (unlike MAP_COMMON L2 page tables).
  // See the comments for the local_access_pte for more info.
  // We just add the PFN for the L2 pointer to the local access PTE.
  // Note that we can't call get_new_page() here since that will
  // access ts_local_controller before we have mapped the _TILESTATE stuff.
  // Also note that the l2 page table is still garbage here.
  unsigned int svmisc_offset = (uintptr_t)&ts_svmisc_page_table -
                               MEM_TILESTATE_VA;
  assert(svmisc_offset < MEM_TILESTATE_SIZE);
  HV_PhysAddr svmisc = memory->tile_state + svmisc_offset;
  assert(((uintptr_t)&ts_svmisc_page_table & (HV_L2_SIZE-1)) == 0);
  HV_PTE pte = hv_pte_set_pfn(local_access_pte, HV_CPA_TO_PFN(svmisc));
  pte = hv_pte_set_present(pte);
  write_pte(l1_page_table + HV_L1_INDEX(MEM_SVMISC_VA) * HV_PTE_SIZE,
            local_access_pte, pte);

  // All the remaining PTEs we are setting up are global small data pages.
  // We will just vary the PFN from now on.
  pte = hv_pte(0);
  pte = hv_pte_set_readable(pte);
  pte = hv_pte_set_writable(pte);
  pte = hv_pte_set_dirty(pte);
  pte = hv_pte_set_global(pte);
  pte = hv_pte_set_present(pte);
  pte = hv_pte_set_accessed(pte);
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);

  // Map the per-tile state
  int tilestate_size = _etilestate - _stilestate;
  assert(tilestate_size <= MEM_TILESTATE_SIZE);
  assert(HV_L1_INDEX(MEM_TILESTATE_VA) == HV_L1_INDEX(MEM_SVMISC_VA));
  for (int i = 0; i < tilestate_size; i += HV_PAGE_SIZE_SMALL)
  {
    write_pte(svmisc + HV_L2_INDEX(MEM_TILESTATE_VA + i) * HV_PTE_SIZE,
              local_access_pte,
              hv_pte_set_pfn(pte, HV_CPA_TO_PFN(memory->tile_state + i)));
  }

  // Map the per-tile kernel stack.  We keep this separate
  // from the state as such so we get an automatic stack overflow fault.
  assert(HV_L1_INDEX(MEM_STACK_VA) == HV_L1_INDEX(MEM_SVMISC_VA));
  assert(memory->stack != 0);
  for (int i = 0; i < MEM_STACK_SIZE; i += HV_PAGE_SIZE_SMALL)
  {
    write_pte(svmisc + HV_L2_INDEX(MEM_STACK_VA + i) * HV_PTE_SIZE,
              local_access_pte,
              hv_pte_set_pfn(pte, HV_CPA_TO_PFN(memory->stack + i)));
  }

  // We zero the stack just to keep things more deterministic; it's
  // not strictly necessary.
  bzero_page(MEM_STACK_VA, MEM_STACK_SIZE);

  // Map the locks area.  We cache locks on the boot tile since it's easy;
  // if we cared about performance we could pick a more centrally located tile.
  int locks_size = _elocks - _slocks;
  assert(locks_size <= MEM_LOCKS_SIZE);
  assert(HV_L1_INDEX(MEM_LOCKS_VA) == HV_L1_INDEX(MEM_SVMISC_VA));
  HV_PTE locks_pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_TILE_L3);
  locks_pte = hv_pte_set_lotar(locks_pte, boot_lotar);
  for (int i = 0; i < locks_size; i += HV_PAGE_SIZE_SMALL)
  {
    write_pte(svmisc + HV_L2_INDEX(MEM_LOCKS_VA + i) * HV_PTE_SIZE,
              local_access_pte,
              hv_pte_set_pfn(locks_pte, HV_CPA_TO_PFN(locks_pa + i)));
  }

  // Now that we have tile state mapped, write values into it.
  ts_coord = coord;
  ts_local_controller = memory->local_controller;
  ts_controller = ts_local_controller;
  ts_tilestate_pa = memory->tile_state;
  ts_contains_priority_page = false;

  // Initialize the FILE for stdout as well
  init_per_tile_stdout();
}

void
init_final()
{
  // Walk over all the locks and set them to zero.
  // We could do this with memset on the page, but as long as we don't
  // have a ton of locks, this is probably faster, even though
  // the writes are uncached.
  for (int* p = (int*) _slocks; p < (int*) _elocks; ++p)
    *p = 0;

  // Walk over the zero pages and make sure they are all zero.
  HV_PhysAddr zero_pages_data_pa = va_to_cpa(zero_pages_data);
  for (int i = 0; i < num_controllers; ++i)
  {
    if (zero_pages[i] == zero_pages_data_pa)
      continue;
    pa_bzero(zero_pages[i], PA_COLORS * HV_PAGE_SIZE_SMALL, BZERO_FLUSH);
  }

  // Point the PA_Allocator objects at their locks.
  for (unsigned int i = 0; i < num_pools; ++i)
    pa_set_lock(&pool_allocators[i], &pool_locks[i]);

  // Make sure common_state isn't in the cache.
  unlock_common();
}

HV_PhysAddr
get_new_page(unsigned long page_size,
             VirtualAddress va,
             int controller)
{
  HV_PhysAddr cpa = PA_FAILED;

  if (pa_is_va_loader)
  {
    cpa = va & 0xFFFFFFFF;
    controller = 0;
  }
  else
  {
    for (unsigned int i = 0; i < num_pools; ++i)
    {
      if (pool_ranges[i].controller == controller)
      {
        if (page_size == HV_PAGE_SIZE_LARGE)
          cpa = pa_alloc_large_page(&pool_allocators[i]);
        else
          cpa = pa_alloc_pages(&pool_allocators[i], (void*) va, page_size);
        if (cpa != PA_FAILED)
          break;
      }
    }

    if (cpa == PA_FAILED)
      panic("Out of memory in get_new_page for %ld bytes on controller %d",
            page_size, controller);
  }

  return cpa;
}


void
free_page(unsigned long page_size, HV_PhysAddr cpa)
{
  for (unsigned int i = 0; i < num_pools; ++i)
  {
    HV_PhysAddrRange* range = &pool_ranges[i];
    if (cpa >= range->start && cpa < (range->start + range->size))
    {
      if (page_size == HV_PAGE_SIZE_LARGE)
        pa_free_large_page(&pool_allocators[i], cpa);
      else
        pa_free_pages(&pool_allocators[i], cpa, page_size);
    }
  }
}


/** Iterate over page table for a given range and invoke a callback. */
static void
for_each_pte(HV_PTE (*func)(HV_PTE pte,
                            VirtualAddress va,
                            void* ptr),
             VirtualAddress start,
             int64_t size,
             void* ptr)
{
  // Page-align the address.
  unsigned int align_delta = start & (HV_PAGE_SIZE_SMALL - 1);
  start -= align_delta;
  size += align_delta;

  VirtualAddress va = start;
  VirtualAddress end_address = start + size - 1;
  if (end_address < va)
    end_address = ~(VirtualAddress) 0;
  VirtualAddress last_va = va;

  while (va >= last_va && va <= end_address)
  {
    // Remember VA so we don't wrap around the end of the address space
    last_va = va;

    // Fetch the top-level page table entry.
    unsigned int l0_index = HV_L0_INDEX(va);
    HV_PTE l0_pte = read_l0_pte(l0_index);

    // See if it's valid.
    if (!is_valid_pte(l0_pte))
    {
      // Invalid, so skip ahead to the start of the next jumbo page.
      va = (va + HV_L1_SPAN) & -HV_L1_SPAN;
      continue;
    }

    // Fetch the L1 page table entry.
    HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));

    // Sanity check to be friendly to users of tsim; the real hv
    // may not be so kind!
    assert((l1_pt_pa & (HV_L1_SIZE - 1)) == 0 &&
           (l1_pt_pa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);

    // Convert the cpa to an actual address we can use.
    HV_PhysAddr pte_l1_pa = l1_pt_pa + (HV_L1_INDEX(va) * HV_PTE_SIZE);

    // Read the L1 pte.
    HV_PTE pte = read_pte(pte_l1_pa, l0_pte);

    // See if it's valid.
    if (!is_valid_pte(pte))
    {
      // Invalid, so skip ahead to the start of the next large page.
      va = (va + HV_PAGE_SIZE_LARGE) & -HV_PAGE_SIZE_LARGE;
      continue;
    }

    if (hv_pte_get_page(pte))
    {
      // Call user callback.
      va &= -HV_PAGE_SIZE_LARGE;
      HV_PTE new_pte = func(pte, va, ptr);

      if (hv_pte_val(pte) != hv_pte_val(new_pte))
      {
        write_pte(pte_l1_pa, l0_pte, new_pte);
        hv_flush_pages(va, HV_PAGE_SIZE_LARGE, 1);
      }

      // Skip ahead to the start of the next large page.
      va += HV_PAGE_SIZE_LARGE;
      continue;
    }

    // The top-level entry is valid and not a large page.
    HV_PhysAddr cpa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(pte));

    // Sanity check to be friendly to users of tsim; the real hv
    // may not be so kind!
    assert((cpa & (HV_L2_SIZE - 1)) == 0 &&
           (cpa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);

    // Convert the cpa to an actual address we can use.
    HV_PhysAddr pte_l2_pa = cpa + (HV_L2_INDEX(va) * HV_PTE_SIZE);

    // Read the second-level HV_PTE.
    HV_PTE l2_pte = read_pte(pte_l2_pa, pte);

    if (is_valid_pte(l2_pte))
    {
      // Simplify things by clearing the large page flag here.
      // This way the caller can trust the large page flag in the PTE.
      l2_pte = hv_pte_clear_page(l2_pte);

      // Call user callback.
      HV_PTE new_pte = func(l2_pte, va, ptr);

      if (hv_pte_val(l2_pte) != hv_pte_val(new_pte))
      {
        write_pte(pte_l2_pa, pte, new_pte);
        hv_flush_pages(va, HV_PAGE_SIZE_SMALL, 1);
      }
    }

    // Skip ahead to the start of the next small page.
    va += HV_PAGE_SIZE_SMALL;
  }
}


/** Get the page table entry for a specific VA.
 * We don't use for_each because we want to return the PTE even
 * if it isn't present, so we can hide stuff in the bits for page faults.
 * Also, this is a frequently called function that it pays to optimize.
 */
static HV_PTE
get_page_table_entry(VirtualAddress va)
{
  // Check L0 page table
  HV_PTE l0_pte = read_l0_pte(HV_L0_INDEX(va));
  if (!is_valid_pte(l0_pte))
    return l0_pte;

  // Check L1 page table
  HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));
  assert((l1_pt_pa & (HV_L1_SIZE - 1)) == 0 &&
         (l1_pt_pa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
  HV_PhysAddr l1_pte_pa = l1_pt_pa + (HV_L1_INDEX(va) * HV_PTE_SIZE);
  HV_PTE pte = read_pte(l1_pte_pa, l0_pte);

  if (!is_valid_pte(pte) || hv_pte_get_page(pte))
    return pte;

  // Check L2 page table
  HV_PhysAddr l2_cpa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(pte));
  assert((l2_cpa & (HV_L2_SIZE - 1)) == 0 &&
         (l2_cpa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
  HV_PhysAddr pte_l2_pa = l2_cpa + (HV_L2_INDEX(va) * HV_PTE_SIZE);
  return read_pte(pte_l2_pa, pte);
}


/** Handler for a page fault interrupt.
 * @param int_name Interrupt name.
 * @param int_number Interrupt number.
 * @param va Faulting address.
 */
int
page_fault(char* int_name, int int_number, VirtualAddress va)
{
  // Release critical section
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);

  // Don't even try to check ts_page_table if we get a non-user fault
  if (va >= MEM_USER_TOP)
    panic("got %s interrupt for access to non-user address %#lX from PC %#lX",
          int_name, va, (long) __insn_mfspr(EX_CONTEXT_BX_0));

  // Preset the L1 PTE to a value which will cause us to fall through to the
  // stack expansion check if the L0 PTE is not valid.
  HV_PTE l1_pte = hv_pte(0);
  HV_PhysAddr l1_index = 0;

  // Check the L0 page table.
  HV_PTE l0_pte = read_l0_pte(HV_L0_INDEX(va));
  HV_PhysAddr l1_pte_pa = 0;
  if (is_valid_pte(l0_pte))
  {
    // Check the L1 page table.
    HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));
    assert((l1_pt_pa & (HV_L1_SIZE - 1)) == 0 &&
           (l1_pt_pa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
    l1_index = HV_L1_INDEX(va);
    l1_pte_pa = l1_pt_pa + (l1_index * HV_PTE_SIZE);
    l1_pte = read_pte(l1_pte_pa, l0_pte);
  }

  if (!hv_pte_get_present(l1_pte))
  {
    // The L1 PTE is not present in our page.  If there's a suitable
    // common state entry, copy it into our page table and return
    // so that the user program gets a chance to try again.
    lock_common();
    HV_PTE common_pte = get_common_l1_pte(va);
    unlock_common();
    if (hv_pte_get_present(common_pte))
    {
      assert(l1_pte_pa != 0);
      write_pte(l1_pte_pa, l0_pte, common_pte);
      // ISSUE: This code block is duplicated below.  Make a sub-function?
      // Return whether we got here through a downcall.
      return INT_HAND_NO_DOWNCALL;
    }
  }

  // Check to see if we are close below the last-seen stack address.
  if (va < ts_stack_bottom && va + MEM_USERSTACK_SIZE > ts_stack_bottom)
  {
    va &= -HV_PAGE_SIZE_SMALL;
    VirtualAddress new_bottom =
      do_mmap(va, ts_stack_bottom - va, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS,
              0, 0,          // no fd or offset
              ts_controller,
              MMAP_ZERO);    // force allocating the page
    if (va != new_bottom)
      panic("unable to allocate additional stack at VA %#lx", va);
    ts_stack_bottom = va;
    // Return whether we got here through a downcall.
    return INT_HAND_NO_DOWNCALL;
  }

  panic("got %s interrupt for access to user address %#lX from PC %#lX",
        int_name, va, (long) __insn_mfspr(EX_CONTEXT_BX_0));

  return 0;
}


/** Convert a copy-on-write page to a real page.
 * @param pte Page table entry.
 * @param va Virtual address.
 */
void
realize_page(HV_PTE pte, VirtualAddress va)
{
  assert(!is_common_pte(pte));  // we always allocate these eagerly
  HV_PhysAddr pa = HV_PFN_TO_CPA(hv_pte_get_pfn(pte));
  HV_PhysAddr zero_base = pa & -((HV_PhysAddr)(PA_COLORS * HV_PAGE_SIZE_SMALL));
  int controller;
  for (controller = 0; ; ++controller)
  {
    // If the PA doesn't reference a zero page, we're in trouble.
    // Eventually we may implement these for real, and then we'll just
    // want to allocate a new page, map it in using the MEM_PAMAP
    // window, but do a pa_memcpy() instead.  Even then we'll probably
    // want to specialize the zero_pages case to be memset() for speed.
    if (controller >= num_controllers)
      panic("copy-on-write page %#lX is not a zero page", va);
    if (zero_base == zero_pages[controller])
      break;
  }

  // Acquire a real page of zero bits
  unsigned long page_size = pte_page_size(pte);
  HV_PhysAddr cpa = get_new_page(page_size, va, controller);

  // Reset the PTE
  pte = clear_copy_on_write_pte(pte, cpa);

  bool common_locked = false;
  install_pte(va, pte, true, &common_locked);
  assert(!common_locked);  // we always allocate common pages eagerly

  hv_flush_page(va, page_size);

  // Now that it's mapped, zero it out for real.
  bzero_page(va & -page_size, page_size);
}

/** Handler for a write access interrupt.
 * @param int_name Interrupt name.
 * @param int_number Interrupt number.
 * @param va Faulting address.
 */
int
access_fault(char* int_name, int int_number, VirtualAddress va)
{
  // Release critical section
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);

  // See if the PTE is copy-on-write.  If not, it's an unrecoverable fault.
  HV_PTE pte = get_page_table_entry(va);
  if (!is_copy_on_write_pte(pte))
    panic("write access error to address %#lX from PC %#lX",
          va, (long) __insn_mfspr(EX_CONTEXT_BX_0));

  // Convert the page to a real page.
  realize_page(pte, va);

  return INT_HAND_NO_DOWNCALL;
}


HV_PhysAddr
va_to_cpa(const void* va_ptr)
{
  // Shortcut if we are in the kernel data section
  VirtualAddress va = (VirtualAddress) va_ptr;
  if (va >= MEM_DATA_VA && va < (MEM_DATA_VA + MEM_DATA_SIZE))
    return va - MEM_DATA_PA_ADJUST;

  HV_PTE pte = get_page_table_entry(va);
  assert(hv_pte_get_present(pte));
  return (HV_PFN_TO_CPA(hv_pte_get_pfn(pte))
          + ((VirtualAddress)va & (pte_page_size(pte) - 1)));
}


HV_PhysAddr
va_to_cpa_and_pte(const void* va_ptr, HV_PTE* ptep)
{
  VirtualAddress va = (VirtualAddress) va_ptr;
  HV_PTE pte = get_page_table_entry(va);
  assert(hv_pte_get_present(pte));
  *ptep = pte;
  return (HV_PFN_TO_CPA(hv_pte_get_pfn(pte))
          + ((VirtualAddress)va & (pte_page_size(pte) - 1)));
}


/** See if a given PTE matches a given protection */
static bool
validate_pte(HV_PTE pte, VirtualAddress va, int prot)
{
  if (!is_valid_pte(pte))
    return false;
  if ((prot & PROT_READ) && !hv_pte_get_readable(pte))
    return false;
  if ((prot & PROT_EXEC) && !hv_pte_get_executable(pte))
    return false;
  if (prot & PROT_WRITE)
  {
    if (is_copy_on_write_pte(pte))
      realize_page(pte, va);
    else if (!hv_pte_get_writable(pte))
      return false;
  }
  return true;
}


int
is_valid_user_buf(const void* user_va, size_t size, int prot)
{
  VirtualAddress va = (VirtualAddress) user_va;
  VirtualAddress end = va + size;

  while (va < end)
  {
    HV_PTE pte = get_page_table_entry(va);
    size_t pgsize;

    //
    // If the page is a user page, and it's valid, we're OK.
    //
    if (hv_pte_get_user(pte) && validate_pte(pte, va, prot))
    {
      pgsize = pte_page_size(pte);
    }
    //
    // If it's not yet valid, but it's a stack page, go create it,
    // and we're still OK.
    //
    else if (va < ts_stack_bottom && va + MEM_USERSTACK_SIZE > ts_stack_bottom)
    {
      va &= -HV_PAGE_SIZE_SMALL;
      VirtualAddress new_bottom =
        do_mmap(va, ts_stack_bottom - va, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                0, 0,          // no fd or offset
                ts_controller,
                MMAP_ZERO);    // force allocating the page
      if (va != new_bottom)
        panic("unable to allocate additional stack at VA %#lX", va);
      ts_stack_bottom = va;
      pgsize = HV_PAGE_SIZE_SMALL;
    }
    //
    // Otherwise, it's not OK.
    //
    else
      return 0;

    va = (va & -pgsize) + pgsize;
  }


  return 1;
}


int
is_valid_user_string(const void* user_va, int prot)
{
  VirtualAddress va = (VirtualAddress) user_va;

  while (1)
  {
    HV_PTE pte = get_page_table_entry(va);
    if (!validate_pte(pte, va, prot))
      return 0;
    size_t pgsize = pte_page_size(pte);
    int bytes_to_next_page = pgsize - (va & (pgsize - 1));
    int len = strnlen((const char*) va, bytes_to_next_page);
    if (len < bytes_to_next_page)
      return 1;
    va = (va & -pgsize) + pgsize;
  }
}


/** Install a PTE into the page table for a given VA */
static void
install_pte(VirtualAddress vpn, HV_PTE pte, bool modify, bool* common_locked)
{
  // Fetch the top-level page table entry.
  HV_PhysAddr l0_index = HV_L0_INDEX(vpn);
  HV_PTE l0_pte = read_l0_pte(l0_index);
  HV_PhysAddr l1_pte_pa;
  HV_PTE l1_pte;

  if (!hv_pte_get_present(l0_pte))
  {
    if (modify)
    {
      panic("Missing L0 page table entry when modifying PTE at va 0x%lx",
            (unsigned long)vpn);
    }

    assert(!is_common_pte(l0_pte));
    assert(!hv_pte_get_page(l0_pte));

    // We are adding a new L1 or L2 page where there is no L0 entry
    // yet. We need to allocate an L1 page table.
    HV_PhysAddr l1_pt_pa = get_new_page(HV_L1_SIZE, 0, ts_controller);

    // We don't plan to map the page table into VA space, so just zero
    // it out with our sliding MEM_PAMAP window.
    // We don't have to flush it since it's a private page.
    pa_bzero(l1_pt_pa, HV_L1_SIZE, BZERO_NONE);

    l0_pte = hv_pte_set_present(hv_pte(0));
    l0_pte = hv_pte_set_ptfn(l0_pte, HV_CPA_TO_PTFN(l1_pt_pa));
    l0_pte = hv_pte_set_mode(l0_pte, HV_PTE_MODE_CACHE_NO_L3);

    // Add new PTE to the L0 page table
    write_l0_pte(l0_index, l0_pte);

    // There's no L1 page table entry.
    l1_pte_pa = l1_pt_pa + HV_L1_INDEX(vpn) * HV_PTE_SIZE;
    l1_pte = hv_pte(0);
  }
  else
  {
    // Check the L1 page table.
    HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));
    assert((l1_pt_pa & (HV_L1_SIZE - 1)) == 0 &&
           (l1_pt_pa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
    l1_pte_pa = l1_pt_pa + HV_L1_INDEX(vpn) * HV_PTE_SIZE;
    l1_pte = read_pte(l1_pte_pa, l0_pte);
  }

  if (!hv_pte_get_present(l1_pte))
  {
    if (modify)
    {
      panic("Missing L1 page table entry when modifying PTE at va 0x%lx",
            (unsigned long)vpn);
    }

    if (hv_pte_get_page(pte))
    {
      l1_pte = pte;
    }
    else
    {
      // We are adding a new small page where there is no top-level
      // entry yet. We need to allocate a second-level page table.
      HV_PhysAddr pte_l2_cpa = get_new_page(HV_L2_SIZE, 0, ts_controller);

      // We don't plan to map the page table into VA space, so just zero
      // it out with our sliding MEM_PAMAP window.
      // We don't have to flush it since it's either a private page,
      // or if it's common, we will be OLOC'ing everyone to this cache.
      pa_bzero(pte_l2_cpa, HV_L2_SIZE, BZERO_NONE);

      l1_pte = hv_pte_set_present(hv_pte(0));
      l1_pte = hv_pte_set_ptfn(l1_pte, HV_CPA_TO_PTFN(pte_l2_cpa));
      if (is_common_pte(pte))
      {
        l1_pte = set_common_pte(l1_pte);
        l1_pte = hv_pte_set_mode(l1_pte, HV_PTE_MODE_CACHE_TILE_L3);
        l1_pte =
          hv_pte_set_lotar(l1_pte, HV_XY_TO_LOTAR(ts_coord.x, ts_coord.y));
      }
      else
        l1_pte = hv_pte_set_mode(l1_pte, HV_PTE_MODE_CACHE_NO_L3);
    }

    // Add new PTE (large page or L2 table) to the L1 page table
    write_pte(l1_pte_pa, l0_pte, l1_pte);

    // Add a new entry to the common_state
    lock_common_if_necessary(common_locked);
    set_common_l1_pte(vpn, l1_pte, is_common_pte(l1_pte));

    if (hv_pte_get_page(pte))
      return;

    // Fall through to the normal logic for updating an L2 page table
  }
  else
  {
    // If we are trying to modify an existing L1 large page PTE, it must be
    // with another large page PTE.
    if (hv_pte_get_page(pte))
    {
      if (modify)
      {
        assert(hv_pte_get_page(l1_pte));   // already checked
        write_pte(l1_pte_pa, l0_pte, pte);
        return;
      }
      panic("Attempting to overwrite %s in the L1 page table "
            "with a new large page for va 0x%lx",
            hv_pte_get_page(l1_pte) ? "large page" : "L2 entry",
            (unsigned long)vpn);
    }

    // If there is already a large page entry here, and we're trying
    // to put in a small page, we blew it somehow.
    assert(!hv_pte_get_page(l1_pte));
  }

  // Get the address of the level 2 page table.
  // Level 2 tables are required to be self-size aligned so
  // we discard their low bits to force this to be true.
  // Knowing they are aligned makes validating resulting references
  // easier.
  const unsigned int ptfn = hv_pte_get_ptfn(l1_pte);
  HV_PhysAddr cpa = HV_PTFN_TO_CPA(ptfn);

#if HV_LOG2_L2_SIZE > HV_LOG2_PAGE_TABLE_ALIGN
  // Sanity check to be friendly to users of tsim; the real hv
  // may not be so kind!
  assert((ptfn & ((1 << (HV_LOG2_L2_SIZE - HV_LOG2_PAGE_TABLE_ALIGN)) - 1)) ==
         0);
#endif

  // Convert the cpa to an actual address we can use.
  unsigned int l2_byte_offset =
    (((vpn >> HV_LOG2_PAGE_SIZE_SMALL) << HV_LOG2_PTE_SIZE)
     & (HV_L2_SIZE - 1));

  HV_PhysAddr pte_l2_pa = cpa + l2_byte_offset;

  // Read the second-level HV_PTE.
  HV_PTE old_l2_pte = read_pte(pte_l2_pa, l1_pte);

  if (modify && !hv_pte_get_present(old_l2_pte))
  {
    // Second-level entry is missing.
    panic("Missing L2 page table entry when modifying PTE at va 0x%lx",
           (unsigned long)vpn);
  }

  if (!modify && hv_pte_get_present(old_l2_pte))
  {
    // Second-level entry is unexpectedly present.
    panic("Illegal overwrite of L2 page table entry at va 0x%lx",
          (unsigned long)vpn);
  }

  // Second-level entry is not present, so overwrite it.
  write_pte(pte_l2_pa, l1_pte, pte);
}


static HV_PTE
create_pte(HV_PhysAddr cpa,
           bool readable,
           bool writable,
           bool executable,
           bool cacheable,
           bool priority,
           bool global,
           bool common,
           bool location_override,
           int home_x,
           int home_y,
           uint32_t mpl,
           unsigned long page_size)
{
  assert(page_size == HV_PAGE_SIZE_SMALL || page_size == HV_PAGE_SIZE_LARGE);

  //
  // Create the new page table entry from the incoming flags.
  //

  HV_PTE pte = hv_pte(0);

  pte = hv_pte_set_present(pte);
  pte = hv_pte_set_accessed(pte);
  pte = hv_pte_set_pfn(pte, HV_CPA_TO_PFN(cpa));
  if (readable)
    pte = hv_pte_set_readable(pte);
  if (writable)
    pte = hv_pte_set_writable(hv_pte_set_dirty(pte));
  if (executable)
    pte = hv_pte_set_executable(pte);
  if (global)
    pte = hv_pte_set_global(pte);
  if (page_size == HV_PAGE_SIZE_LARGE)
    pte = hv_pte_set_page(pte);
  if (cacheable && priority)
    pte = hv_pte_set_cached_priority(pte);
  if (common)
    pte = set_common_pte(pte);

  if (location_override)
  {
    if (home_x == LOTAR_HASH && home_y == LOTAR_HASH)
      pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_HASH_L3);
    else
    {
      assert (home_x < width && home_y < height);
      pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(home_x, home_y));
      pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_TILE_L3);
    }
  }
  else
  {
    if (cacheable)
      pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);
    else
      pte = hv_pte_set_mode(pte, HV_PTE_MODE_UNCACHED);
  }

  if (mpl == 0)
    pte = hv_pte_set_user(pte);
  else
    assert(mpl == 1);

  return pte;
}


/** Check to see if the common_state allows us to map here.
 * If we have some holes in our L1 in a given range and they are available
 * from the common state, fill them in while we're at it.
 */
static bool
check_common_state(VirtualAddress* start, unsigned long size,
                   bool common, bool* common_locked)
{
  if (common && (HV_L0_INDEX(*start) || HV_L0_INDEX(*start + size - 1)))
    return false;

  const int l0_base = HV_L0_INDEX(*start);
  const int l0_end = HV_L0_INDEX(*start + size - 1);
  bool ok = true;

  for (int l0_i = l0_base; l0_i <= l0_end; ++l0_i)
  {
    HV_PTE l0_pte = read_l0_pte(l0_base);
    if (!hv_pte_get_present(l0_pte))
      return true;
    HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));

    const int base = (l0_i == l0_base) ? HV_L1_INDEX(*start) : 0;
    const int end = (l0_i == l0_end) ? HV_L1_INDEX(*start + size - 1) :
                                       HV_L1_ENTRIES - 1;
    for (int i = base; i <= end; ++i)
    {
      HV_PTE pte = read_pte(l1_pt_pa + i * HV_PTE_SIZE, l0_pte);
      if (!hv_pte_get_present(pte) && !l0_i)
      {
        lock_common_if_necessary(common_locked);
        pte = get_common_l1_pte(i << HV_LOG2_PAGE_SIZE_LARGE);
        if (hv_pte_get_present(pte))
          write_pte(l1_pt_pa + i * HV_PTE_SIZE, l0_pte, pte);
      }
      if (hv_pte_get_present(pte) && common != is_common_pte(pte))
      {
        // If we're scanning, note that we should start one large page
        // after the last mismatch.
        *start = (l0_i * HV_L1_ENTRIES + i + 1) << HV_LOG2_PAGE_SIZE_LARGE;
        ok = false;
      }
    }
  }
  return ok;
}


/** See if a given range is not in use. */
static bool
is_range_available(VirtualAddress* start, unsigned long size,
                   bool common, bool* common_locked)
{
  // Wraparound, not a valid range.
  VirtualAddress last_byte = *start + size - 1;
  if (last_byte < *start)
    return false;

  // Check to see if this address range is OK for common vs. private.
  // If we fail, we set *start to the first possible address.
  if (!check_common_state(start, size, common, common_locked))
    return false;

  VirtualAddress page_after_last_mapped = -1UL;

  // Page-align the address.
  unsigned int align_delta = *start & (HV_PAGE_SIZE_SMALL - 1);
  start -= align_delta;
  size += align_delta;

  // Inline for_each_pte here to improve performance
  VirtualAddress va = *start;
  while (va <= last_byte)
  {
    HV_PTE l0_pte = read_l0_pte(HV_L0_INDEX(va));

    if (!is_valid_pte(l0_pte))
    {
      va = (va + HV_L1_SPAN) & -HV_L1_SPAN;
      continue;
    }

    HV_PhysAddr l1_pt_pa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(l0_pte));
    assert((l1_pt_pa & (HV_L1_SIZE - 1)) == 0 &&
           (l1_pt_pa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
    HV_PhysAddr l1_pte_pa = l1_pt_pa + HV_L1_INDEX(va) * HV_PTE_SIZE;
    HV_PTE pte = read_pte(l1_pte_pa, l0_pte);

    if (!is_valid_pte(pte) || hv_pte_get_page(pte))
    {
      va = (va + HV_PAGE_SIZE_LARGE) & -HV_PAGE_SIZE_LARGE;
      if (hv_pte_get_page(pte))
        page_after_last_mapped = va;
      continue;
    }
    HV_PhysAddr cpa = HV_PTFN_TO_CPA(hv_pte_get_ptfn(pte));
    assert((cpa & (HV_L2_SIZE - 1)) == 0 &&
           (cpa & (HV_PAGE_TABLE_ALIGN - 1)) == 0);
    VirtualAddress last_byte_on_l1 = last_byte;
    VirtualAddress next_l1_page =
      (va & -HV_PAGE_SIZE_LARGE) + HV_PAGE_SIZE_LARGE;
    if (next_l1_page < last_byte_on_l1)
      last_byte_on_l1 = next_l1_page-1;
    while (va <= last_byte_on_l1)
    {
      HV_PhysAddr pte_l2_pa = cpa + (HV_L2_INDEX(va) * HV_PTE_SIZE);
      HV_PTE l2_pte = read_pte(pte_l2_pa, pte);
      if (is_valid_pte(l2_pte))
        page_after_last_mapped = va + HV_PAGE_SIZE_SMALL;
      va += HV_PAGE_SIZE_SMALL;
    }
  }

  if (page_after_last_mapped == -1UL)
  {
    // We found no valid pages in this range, meaning the space
    // is free. Leave *start alone.
    return true;
  }
  else
  {
    // We found one or more pages. Return the page following the last one.
    *start = page_after_last_mapped;
    return false;
  }
}


/** Helper function for is_range_user. */
static HV_PTE
is_range_user_callback(HV_PTE pte, VirtualAddress addr, void* ptr)
{
  if (!hv_pte_get_user(pte))
    *(bool*)ptr = false;
  return pte;
}

/** Is the given range valid for user mappings?
 * Users can map anything below MEM_USER_TOP; they can also map anything
 * in the range reserved for user interrupt vectors.  However, if there's
 * already a global page mapped there, they can't map over it.
 */
static bool
is_range_user(VirtualAddress start, unsigned long size)
{
  if (start + size > MEM_USER_TOP || start > start + size)
    return false;
  bool is_user = true;
  for_each_pte(is_range_user_callback, start, size, &is_user);
  return is_user;
}


/** Helper function for search_for_priority_page. */
static HV_PTE
search_for_priority_page_callback(HV_PTE pte, VirtualAddress va, void* ptr)
{
  *(bool*)ptr |= hv_pte_get_cached_priority(pte);
  return pte;
}

static bool
search_for_priority_page()
{
  bool found_priority = false;
  for_each_pte(search_for_priority_page_callback, 0, 1ULL << 32,
               (void*)&found_priority);
  return found_priority;
}


/** We have flipped from containing a priority bit to not.
 * Update the hypervisor state and flush everything relevant.
 */
static void
flush_for_priority()
{
  hv_set_caching(ts_contains_priority_page ? ~0 : 0);
  hv_flush_asid(asid);
  hv_flush_page(MEM_SVMISC_VA, HV_PAGE_SIZE_LARGE);
  hv_flush_page(MEM_DATA_VA, HV_PAGE_SIZE_LARGE);
  hv_flush_page(MEM_CODE_VA, HV_PAGE_SIZE_LARGE);
}

VirtualAddress
do_mmap(VirtualAddress address_in, uint32_t size,
        int32_t protection, int32_t flags,
        int fd, size_t offset, int32_t controller,
        MmapInitialize mmap_initialize)
{
  int test_drv_mmap = 0;

  if (size == 0)
  {
    // Immediately grant any request of 0 bytes.
    return address_in;
  }
  else if (size > 0x80000000)
  {
    // Impose a 2GB size limit, since such allocations would run into
    // the stack anyway.
    return -ENOMEM;
  }

  // If the requested controller doesn't exist, fail before we go and
  // allocate any address space.
  if (!(controllers_present & (1 << controller)))
    return -ENOMEM;

  VirtualAddress last_byte = address_in + size - 1;
  if (last_byte < address_in)
  {
    // Wraparound, not a valid range.
    return -ENOMEM;
  }

  // Stealthily convert hugetlb file requests to anonymous ones.
  if ((flags & MAP_ANONYMOUS) == 0 &&
      (fd == MMAP_FD_HUGETLB || is_hugetlb_file(fd)))
  {
    // Match the Linux requirement for aligned mappings.
    if ((address_in & (HV_PAGE_SIZE_LARGE-1)) != 0 ||
        (size & (HV_PAGE_SIZE_LARGE-1)) != 0)
      return -EINVAL;

    flags |= MAP_ANONYMOUS | MAP_HUGETLB;
  }

  // Round up allocation size to a multiple of the page size.
  const unsigned long page_size =
    (flags & MAP_HUGETLB) ? HV_PAGE_SIZE_LARGE : HV_PAGE_SIZE_SMALL;
  size = ROUND_UP(size, page_size);

  if ((address_in & (page_size - 1)) != 0)
  {
    // Unaligned requested address.
    return -EINVAL;
  }

  // Fail attempts to use any old deprecated flags.
  if (flags & _MAP_CACHE_OLD)
  {
    warn("pre-1.2.1 mmap caching flags are not supported.\n");
    return -EINVAL;
  }

  // Extract flag bits.
  bool readable = (protection & PROT_READ) != 0;
  bool writable = (protection & PROT_WRITE) != 0;
  bool executable = (protection & PROT_EXEC) != 0;
  bool common = (flags & MAP_SHARED) != 0;
  bool priority = (flags & MAP_CACHE_PRIORITY) != 0;
  bool cacheable = (flags & MAP_CACHE_NO_LOCAL) == 0;
  bool incoherent_ok = (flags & _MAP_CACHE_INCOHERENT) != 0;

  // Set default home cache values.
  uint32_t lotar_x = ts_coord.x;
  uint32_t lotar_y = ts_coord.y;
  bool oloc = writable;

  // Handle any user override of the home cache.
  if ((flags & _MAP_CACHE_HOME))
  {
    uint32_t tilenum = (flags >> _MAP_CACHE_HOME_SHIFT) & _MAP_CACHE_HOME_MASK;
    if (tilenum == _MAP_CACHE_HOME_NONE)
    {
      // We special-case setting the home cache to "none".
      // If the user isn't indicating willingness to tolerate
      // incoherence, and is caching locally on the cpu, we
      // fail a writable mapping.
      if (cacheable && writable && !incoherent_ok)
        return (VirtualAddress) -EINVAL;
      oloc = false;
    }
    else if (tilenum == _MAP_CACHE_HOME_HASH)
    {
      lotar_x = lotar_y = LOTAR_HASH;
      oloc = true;
    }
    else if (tilenum != _MAP_CACHE_HOME_HERE)
    {
      lotar_y = tilenum / width;
      lotar_x = tilenum % width;
      if (lotar_y >= height)
      {
        // Out of range lotar request from the user
        return -EINVAL;
      }
    }
  }

  // If we're forcing oloc for debugging purposes, do that now
  if (!oloc && ts_default_oloc_enabled)
  {
    get_oloc(&lotar_x, &lotar_y);
    if (lotar_x != ts_coord.x || lotar_y != ts_coord.y)
    {
      oloc = true;
      cacheable = false;
    }
  }

  if (!oloc)
    lotar_x = lotar_y = 0;  // be tidy

  // Only one of MAP_SHARED or MAP_PRIVATE.
  if (__builtin_popcount(flags & MAP_TYPE) != 1)
    return -EINVAL;

  if ((flags & MAP_ANONYMOUS) == 0)
  {
    //
    // The only file descriptor that can be specified is that of the hvtest
    // driver.  In this case, we restrict the mapping to one page, and
    // use offset as a raw PTE for that mapping.
    //
    struct fd* sfd = get_fd(fd);
    if (sfd && (sfd->fops->flags & FOPS_FLG_HVTEST) && size == page_size)
      test_drv_mmap = 1;
    else
      return -EINVAL;
  }

  if (common && priority)
  {
    // FIXME This is problematic because we will need to send
    // hypervisor messages to all tiles to alert them when a common
    // priority mapping is set up for the first time, even if those
    // tiles don't plan to use the particular common mapping.  We
    // can't defer lazily until the tiles go to use it, since a
    // second-level priority page table entry can appear in the common
    // table without a supervisor being aware of it.  So the model
    // is that we track in the common_state somewhere how many priority
    // PTEs we have, and when we transition to or from "zero" we
    // message all the nodes so they know whether or not to override
    // their own local bool that says if they have priority pages.
    panic("We don't currently allow common priority mappings.");
  }

  if (priority)
  {
    // If we are going straight to memory, PRIORITY doesn't make much
    // sense, but we just ignore it rather than returning EINVAL for now.
    // We might eventually implement some kind of priority MDN packet.
    if (!cacheable && !oloc)
      priority = false;

    // sanity that the size is less than or equal to that of one cache way
    if (size > CHIP_L2_CACHE_SIZE() / CHIP_L2_ASSOC())
    {
      return -ENOMEM;
    }
  }

  // Always start out locked if we are doing common mappings.
  // Even if we don't explicitly touch the supervisor common_state[],
  // we still may be updating shared L2 page tables.
  bool common_locked = common;
  if (common)
    lock_common();

  VirtualAddress start = 0;

  bool found_some_priority_page = false;
  bool error = false;

  if ((flags & MAP_FIXED) != 0)
  {
    // The user required a specific address.
    start = address_in;

    // See if this range is valid for common or private, as appropriate.
    if (!check_common_state(&start, size, common, &common_locked))
      error = true;

    // See if it contains only user pages, if any.
    else if (!is_range_user(start, size))
      error = true;
  }
  else
  {
    if (address_in != 0)
    {
      // The user requested a specific address. Give it a try.
      start = address_in;

      if (!is_range_available(&start, size, common, &common_locked))
      {
        start = 0;
      }
    }

    if (start == 0)
    {
      if (common)
      {
        // Put common pages at higher addresses so if we allocate
        // some common pages and then "exec" a new process the common
        // pages probably won't interfere with those needed by the loader.
        //
        // TODO: perhaps allocating all pages in backwards order from
        // the high end of memory would be a better way to avoid this
        // problem.
        start = MEM_COMMONSTART_VA;
      }
      else
      {
        // Leave 0-64K unmapped to catch stray memory references.
        start = MEM_USERSTART_VA;
      }

      while (1)
      {
        // Round up to the next possible address. Finding
        // 16MB unaligned is not good enough for a large page.
        start = ROUND_UP(start, page_size);
        if (start + size > ts_stack_bottom)
        {
          // Can't get higher than the bottom of the allocated user stack.
          // The user interrupt area can be mapped, but only by explicit
          // request.
          error = true;
          break;
        }
        if (is_range_available(&start, size, common, &common_locked))
        {
          // Found a clean address range, we succeeded.
          break;
        }
      }
    }
  }

  if (error)
  {
    unlock_common_if_necessary(&common_locked);
    return -ENOMEM;
  }

  // If we're forcing this address, clear it first to be sure.
  if ((flags & MAP_FIXED) != 0)
    do_munmap_internal(start, size, &common_locked);

  // Large pages can't be lazily allocated since we don't have
  // large zero pages to point them at.
  //
  // If we're allocating common memory, don't do it lazily.
  // This avoids races in access_fault(), and is arguably reasonable
  // in that common memory is probably allocated as a communication
  // medium across tiles and will likely all get used, rather
  // than other kinds of private mappings that may be sparsely used.
  if (mmap_initialize == MMAP_LAZY &&
      (page_size == HV_PAGE_SIZE_LARGE || common))
  {
    mmap_initialize = MMAP_ZERO;
  }

  // allocate the range
  for (uint32_t s = 0; s < size; s += page_size)
  {
    const VirtualAddress current = start + s;

    // make sure we're working with "page numbers"
    HV_PhysAddr cpa;
    HV_PTE pte;
    bool lazy = false;
    if (test_drv_mmap)
    {
      pte = hv_pte(offset);
    }
    else
    {
      if (mmap_initialize == MMAP_LAZY)
      {
        // Create a zero-page reference.
        // If mmap_initialize is not lazy, we are planning to write the page
        // immediately, so we should allocate up front.
        int color = (current >> HV_LOG2_PAGE_SIZE_SMALL) & (PA_COLORS-1);
        assert(zero_pages[controller] != 0);
        cpa = zero_pages[controller] + (page_size * color);
        lazy = true;
      }
      else
      {
        cpa = get_new_page(page_size, current, controller);
      }

      pte = create_pte(cpa,        //cpa
                       readable,
                       writable,
                       executable,
                       cacheable,
                       priority,
                       false,     // global
                       common,    // common
                       oloc,      // oloc
                       lotar_x,
                       lotar_y,
                       0,  // mpl
                       page_size);

      if (lazy)
      {
        if (writable)
        {
          // Convert the PTE to copy-on-write
          pte = hv_pte_clear_writable(pte);
          pte = set_copy_on_write_pte(pte);
        }
      }

      // TODO: we may be putting a large page where there is an L2
      // table already, although we know it is completely empty.
      // We need to free the L2 table so this doesn't blow up.

      // If we're installing a common page, we have to zero it out and
      // flush it to memory before we can place it in the page table,
      // since we are probably placing it on a common L2 where another
      // tile's hypervisor could read it as soon as we write it out,
      // before we finish zeroing it in place.
      if (mmap_initialize == MMAP_ZERO && common)
        pa_bzero(cpa, page_size, cacheable ? BZERO_FLUSH : BZERO_FINV);
    }

    install_pte(current, pte, false, &common_locked);

    // Now that the page is mapped, if we wanted it zeroed, let's do it.
    if (mmap_initialize == MMAP_ZERO && !common)
      bzero_page(current, page_size);
  }

  if (priority)
  {
    // See if this tile needs to be updated to know it contains
    // a priority page, and/or have its modified page table entries flushed.
    if (priority &&
        !ts_contains_priority_page)
    {
      ts_contains_priority_page = true;
      flush_for_priority();
    }
    else if (!priority &&
             found_some_priority_page &&
             !search_for_priority_page())
    {
      ts_contains_priority_page = false;
      flush_for_priority();
    }
  }

  unlock_common_if_necessary(&common_locked);

  return start;
}


static void
mem_set_pa(VirtualAddress va, HV_PhysAddr cpa, int size)
{
  bool common_locked = false;
  for (int i = 0; i < size; )
  {
    HV_PTE pte = get_page_table_entry(va);
    assert(is_copy_on_write_pte(pte));
    assert((va & (pte_page_size(pte)-1)) == 0);
    pte = clear_copy_on_write_pte(pte, cpa+i);
    install_pte(va, pte, true, &common_locked);
    i += pte_page_size(pte);
    va += pte_page_size(pte);
  }
  unlock_common_if_necessary(&common_locked);
}


void
map_initial_stack(VirtualAddress sp, HV_PhysAddr base, int size)
{
  do_mmap(sp, size,
          PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
          0, 0,     // no fd or offset
          ts_controller,
          MMAP_LAZY);
  mem_set_pa(sp, base, size);
  ts_stack_bottom = sp;
}


/** Data for munmap and mprotect callbacks */
typedef struct
{
  bool saw_some_common_page;        /**< Did we see any common pages? */
  bool* common_locked;              /**< Pointer to if we have common lock */
  bool unmapped_some_priority_page; /**< munmap: track priority pages */
  int controller;                   /**< mbind: new controller to use */
  uint64_t or_mask;                 /**< mprotect: protections to "or" in */
} CallbackData;

/** Helper callback for simulate_munmap. */
static HV_PTE
simulate_munmap_callback(HV_PTE pte, VirtualAddress va, void* ptr)
{
  CallbackData* data = (CallbackData*) ptr;

  if (!hv_pte_get_user(pte))
    return pte;

  if (is_common_pte(pte))
  {
    data->saw_some_common_page = true;

    // For large PTEs, we need to clear them out of the common_state[]
    // array for correctness, or page faults will just bring them back
    // in with an invalid PA.  Common L2 page tables we can just leave
    // alone, even if they become empty, at least for now.  Ideally once
    // they are empty we should free them as we do here for large PTEs.
    if (hv_pte_get_page(pte))
    {
      lock_common_if_necessary(data->common_locked);
      drop_common_l1_pte(va);
    }
  }

  // Aggregate if we saw any priority pages
  data->unmapped_some_priority_page |= hv_pte_get_cached_priority(pte);

  // TODO: reclaim physical memory here!
  //
  // We are currently not reclaiming memory because the loader allocates
  // multiple tiles from the same PA (without using MAP_COMMON) and we
  // have no reference tracking to tell us when it's safe to free.
  //
  // We also don't have the right mechanisms in place to handle
  // noticing that a page has changed where it is cached since its
  // previous allocation and flushing its cache there prior to allowing
  // it to be reused homed on a new cpu (a la Linux homecache support).

  //HV_PhysAddr cpa = HV_PFN_TO_CPA(hv_pte_get_pfn(pte));
  //free_page(cpa, pte_page_size(pte));

  return hv_pte_clear_present(pte);
}

static int
do_munmap_internal(VirtualAddress address, uint32_t size, bool* common_locked)
{
  // Disallowing a size of 0 seems strange since you can mmap 0
  // or mprotect 0 bytes, but it's what linux does.
  if (size == 0 || address % HV_PAGE_SIZE_SMALL != 0)
    return -EINVAL;

  // Walk through pages in this range and unmap them.
  CallbackData data;
  data.unmapped_some_priority_page = false;
  data.saw_some_common_page = false;
  data.common_locked = common_locked;

  for_each_pte(simulate_munmap_callback, address, size, &data);

  if (ts_contains_priority_page
      && data.unmapped_some_priority_page
      && !search_for_priority_page())
  {
    /* Flush the TLB so other entries can start using the entire cache. */
    ts_contains_priority_page = false;
    flush_for_priority();
  }

  if (data.saw_some_common_page)
  {
    // FIXME: we need to do hypervisor messaging to all the other tiles
    // for MAP_COMMON memory so they can remove them from their local
    // page tables and do an hv_flush_pages().  As it stands other tiles
    // will continue to be able to access pages they have already
    // accessed until they are evicted from their TLBs.
    warn("munmap of common memory is not supported.\n");
  }

  return 0;
}

int
sys_munmap(VirtualAddress address, uint32_t size)
{
  bool common_locked = false;
  int rc = do_munmap_internal(address, size, &common_locked);
  unlock_common_if_necessary(&common_locked);
  return rc;
}


void
munmap_user_pages()
{
  //
  // For now we don't try to free much when we have large VAs.
  // We clear up to the brk, since that's easy, and required for brk() to
  // work right in the new image.  However, we don't reclaim any page
  // tables.
  //
  if (ts_brk)
  {
    for (int i = 0; i <= HV_L0_INDEX(ts_brk-1); ++i)
      write_l0_pte(i, hv_pte(0));
  }
  hv_flush_asid(asid);
  ts_brk = ts_brk_start = 0;
}


void
set_brk_start(VirtualAddress load_top)
{
  if (load_top > ts_brk_start)
    ts_brk = ts_brk_start = load_top;
}

VirtualAddress
sys_brk(VirtualAddress new_brk)
{
  if (new_brk < ts_brk_start)
    return ts_brk;

  VirtualAddress aligned_new_brk =
    (new_brk + HV_PAGE_SIZE_SMALL - 1) & -HV_PAGE_SIZE_SMALL;
  VirtualAddress aligned_old_brk =
    (ts_brk + HV_PAGE_SIZE_SMALL - 1) & -HV_PAGE_SIZE_SMALL;

  if (aligned_new_brk < aligned_old_brk)
  {
    sys_munmap(aligned_new_brk, aligned_old_brk - aligned_new_brk);
  }
  else if (aligned_new_brk > aligned_old_brk)
  {
    VirtualAddress size = aligned_new_brk - aligned_old_brk;
    VirtualAddress rc =
      do_mmap(aligned_old_brk, size, PROT_READ | PROT_WRITE,
              MAP_ANONYMOUS | MAP_PRIVATE, 0, 0, ts_controller, MMAP_ZERO);
    if (rc != aligned_old_brk)
    {
      sys_munmap(rc, size);
      return ts_brk;
    }
  }

  ts_brk = new_brk;
  return ts_brk;
}
                                

/** Helper callback for simulate_mprotect. */
static HV_PTE
simulate_mprotect_callback(HV_PTE pte, VirtualAddress va, void* ptr)
{
  CallbackData* data = (CallbackData*) ptr;

  if (!hv_pte_get_user(pte))
    return pte;

  if (is_common_pte(pte))
  {
    data->saw_some_common_page = true;
    lock_common_if_necessary(data->common_locked);
  }

  const uint64_t and_mask =
    ~(  HV_PTE_READABLE
      | HV_PTE_WRITABLE
      | HV_PTE_EXECUTABLE);

  return hv_pte((hv_pte_val(pte) & and_mask) | data->or_mask);
}

int
sys_mprotect(VirtualAddress address, uint32_t size, int32_t protection)
{
  if (size == 0)
    return 0;
  else if (address % HV_PAGE_SIZE_SMALL != 0)
    return -EINVAL;

  CallbackData data;
  data.saw_some_common_page = false;
  bool common_locked = false;
  data.common_locked = &common_locked;

  // Set these flags.
  data.or_mask =
    (  ((protection & PROT_READ)  ? HV_PTE_READABLE : 0)
     | ((protection & PROT_WRITE) ? HV_PTE_WRITABLE : 0)
     | ((protection & PROT_EXEC) ? HV_PTE_EXECUTABLE : 0));

  for_each_pte(simulate_mprotect_callback, address, size, &data);

  if (data.saw_some_common_page)
  {
    // FIXME: we need to do hypervisor messaging to all the other tiles
    // for MAP_COMMON memory so they can do an hv_flush_pages().
    // As it stands other tiles will continue to be able to access pages
    // with the old protection until they are evicted from their TLBs.
    warn("mprotect of common memory (%#lX,%#x) is not supported.\n",
         address, size);
  }

  unlock_common_if_necessary(&common_locked);

  return 0;
}

static int
read_mempolicy(int policy, unsigned long* nodemask, unsigned long maxnode)
{
  switch (policy) {
  case MPOL_DEFAULT:
    {
      return ts_local_controller;
    }
  case MPOL_BIND:
  case MPOL_PREFERRED:
    {
      if (!is_valid_user_buf(nodemask, maxnode/8, PROT_READ))
        return -EFAULT;
      int controller = __insn_ctz(*nodemask);
      if (controller >= num_controllers)
        return -EINVAL;
      return controller;
    }
  case MPOL_INTERLEAVE:
  default:
    {
      return -EINVAL;
    }
  }
}

int
sys_set_mempolicy(int policy, unsigned long* nodemask, unsigned long maxnode)
{
  int controller = read_mempolicy(policy, nodemask, maxnode);
  if (controller < 0)
    return controller;
  ts_controller = controller;
  return 0;
}

// This is information-losing; we just report everything back as MPOL_BIND.
int
sys_get_mempolicy(int* policy, unsigned long* nodemask, unsigned long maxnode,
                  unsigned long addr, unsigned long flags)
{
  if (flags || addr)
    return -EINVAL;
  if (policy) {
    if (!is_valid_user_buf(policy, sizeof(*policy), PROT_WRITE))
      return -EFAULT;
    *policy = MPOL_BIND;
  }
  if (nodemask) {
    if (!is_valid_user_buf(nodemask, maxnode/8, PROT_WRITE))
      return -EFAULT;
    if (ts_controller >= maxnode)
      return -EINVAL;
    *nodemask = (1 << ts_controller);
  }
  return 0;
}

/** Helper callback for sys_mbind. */
static HV_PTE
sys_mbind_callback(HV_PTE pte, VirtualAddress va, void* ptr)
{
  CallbackData* data = (CallbackData*) ptr;

  if (!hv_pte_get_user(pte))
    return pte;

  HV_PhysAddr pa = HV_PFN_TO_CPA(hv_pte_get_pfn(pte));

  // Figure out which controller this PA is on.
  int controller;
  for (int i = 0; ; ++i) {
    assert(i < num_pools);
    if (pa >= pool_ranges[i].start &&
        pa < (pool_ranges[i].start + pool_ranges[i].size)) {
      controller = pool_ranges[i].controller;
      break;
    }
  }
  if (controller == data->controller)
    return pte;

  if (is_common_pte(pte))
  {
    data->saw_some_common_page = true;
    lock_common_if_necessary(data->common_locked);
  }

  HV_PhysAddr zero_base =
    pa & -((HV_PhysAddr)(PA_COLORS * HV_PAGE_SIZE_SMALL));

  if (zero_base == zero_pages[controller]) {

    // For lazy PTEs, we can just switch them to the new controller.
    int color = (va >> HV_LOG2_PAGE_SIZE_SMALL) & (PA_COLORS-1);
    assert(zero_pages[data->controller] != 0);
    pa = zero_pages[data->controller] + (HV_PAGE_SIZE_SMALL * color);

  } else {

    // Otherwise, acquire a page on the new controller and copy.
    unsigned long page_size = pte_page_size(pte);
    HV_PhysAddr new_pa = get_new_page(page_size, va, data->controller);
    pa_memcpy(new_pa, va, page_size);
    free_page(page_size, pa);
    pa = new_pa;

  }

  // Update the PTE to reference the new page
  pte = hv_pte_set_pfn(pte, HV_CPA_TO_PFN(pa));

  // If it was a large common PTE, update the common table.
  if (is_common_pte(pte) && hv_pte_get_page(pte))
    update_common_l1_pte(va, pte);

  // Return a PTE referencing the new page
  return hv_pte_set_pfn(pte, HV_CPA_TO_PFN(pa));
}

int
sys_mbind(VirtualAddress address, uint32_t size, int policy,
          unsigned long* nodemask, unsigned long maxnode, uint32_t flags)
{
  if (size == 0)
    return 0;
  else if (address % HV_PAGE_SIZE_SMALL != 0)
    return -EINVAL;

  int controller = read_mempolicy(policy, nodemask, maxnode);
  if (controller < 0)
    return controller;

  CallbackData data;
  data.saw_some_common_page = false;
  bool common_locked = false;
  data.common_locked = &common_locked;
  data.controller = controller;

  for_each_pte(sys_mbind_callback, address, size, &data);

  if (data.saw_some_common_page)
  {
    // FIXME: we need to do hypervisor messaging to all the other tiles
    // for MAP_COMMON memory so they can do an hv_flush_pages().
    // As it stands other tiles will continue to be able to access pages
    // on the old controller until they are evicted from their TLBs.

    // However, we comment this out since mbind() is frequently-used
    // but always immediately after mmap(), so in practice this works OK.
    //warn("mbind of common memory (%#x,%#x) is not supported.\n",
    //     address, size);
  }

  unlock_common_if_necessary(&common_locked);

  return 0;
}

/** Zero out as many full pages as are covered by the given range.
 * The type parameter indicates if we want to do nothing (typical for
 * a cached page that we will continue to access cached), flush the
 * page (typical for a page that we will access on another tile),
 * or finv the page (typical for a page that we will be accessing
 * in some way other than normal cached mode).
 */
static void
pa_bzero_internal(HV_PhysAddr pa, size_t bytes, BzeroType type,
                  HV_PhysAddr l2_page_table_pa)
{
  // Set up bits for the page.
  HV_PTE pte = hv_pte(0);
  pte = hv_pte_set_readable(pte);
  pte = hv_pte_set_writable(pte);
  pte = hv_pte_set_dirty(pte);
  pte = hv_pte_set_global(pte);
  pte = hv_pte_set_present(pte);
  pte = hv_pte_set_accessed(pte);
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);

  // And where to write the page PTE to.
  // If this is zero, we just use normal writes instead of HV writes.
  const int pamap_index = HV_L2_INDEX(MEM_PAMAP_VA);
  HV_PhysAddr pamap_pte_cpa = 0;
  if (l2_page_table_pa)
    pamap_pte_cpa = l2_page_table_pa + pamap_index * HV_PTE_SIZE;

  for (int i = 0; i < bytes; i += HV_PAGE_SIZE_SMALL)
  {
    // Roll to the next page
    pte = hv_pte_set_pfn(pte, HV_CPA_TO_PFN(pa+i));

    // Map the page in
    if (pamap_pte_cpa)
      write_pte(pamap_pte_cpa, local_access_pte, pte);
    else
      write_svmisc_pte(pamap_index, pte);

    // Assume it's now good to go; we shouldn't need to inform the hv.
    if (type != BZERO_FINV_ONLY)
      bzero_page(MEM_PAMAP_VA, HV_PAGE_SIZE_SMALL);

    switch (type)
    {
    case BZERO_NONE:
      break;
    case BZERO_FLUSH:
      for (VirtualAddress p = MEM_PAMAP_VA;
           p < MEM_PAMAP_VA + HV_PAGE_SIZE_SMALL;
           p += CHIP_L2_LINE_SIZE())
      {
        __insn_flush((char*)p);
      }
      break;
    case BZERO_FINV:
    case BZERO_FINV_ONLY:
      for (VirtualAddress p = MEM_PAMAP_VA;
           p < MEM_PAMAP_VA + HV_PAGE_SIZE_SMALL;
           p += CHIP_L1D_LINE_SIZE())
      {
        __insn_finv((char*)p);
      }
      break;
    }

    // Unmap the page
    if (pamap_pte_cpa)
      write_pte(pamap_pte_cpa, local_access_pte, hv_pte(0));
    else
      write_svmisc_pte(pamap_index, hv_pte(0));

    // Tell the hypervisor to drop it from the TLB
    hv_flush_page(MEM_PAMAP_VA, HV_PAGE_SIZE_SMALL);
  }

  // We must make sure our writes are committed before returning.
  // This is required for flush/finv semantics but is also needed in
  // some other cases, e.g. BZERO_NONE to OLOC memory, so we just
  // always do it; the cost is low relative to page bzero'ing anyway.
  __insn_mf();
}

static void
pa_bzero(HV_PhysAddr pa, size_t bytes, BzeroType type)
{
  pa_bzero_internal(pa, bytes, type, 0);
}

/** Copy a specified number of (page-aligned) bytes from a VA to a PA. */
static void
pa_memcpy(HV_PhysAddr dest, VirtualAddress src, size_t bytes)
{
  // Set up bits for the page.
  HV_PTE pte = hv_pte(0);
  pte = hv_pte_set_readable(pte);
  pte = hv_pte_set_writable(pte);
  pte = hv_pte_set_dirty(pte);
  pte = hv_pte_set_global(pte);
  pte = hv_pte_set_present(pte);
  pte = hv_pte_set_accessed(pte);
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);

  const int pamap_index = HV_L2_INDEX(MEM_PAMAP_VA);
  for (int i = 0; i < bytes; i += HV_PAGE_SIZE_SMALL)
  {
    pte = hv_pte_set_pfn(pte, HV_CPA_TO_PFN(dest+i));
    write_svmisc_pte(pamap_index, pte);
    memcpy((void*)MEM_PAMAP_VA, (void*)src, HV_PAGE_SIZE_SMALL);
    write_svmisc_pte(pamap_index, hv_pte(0));
    hv_flush_page(MEM_PAMAP_VA, HV_PAGE_SIZE_SMALL);
  }

  __insn_mf();
}


void*
map_window(HV_PhysAddr pa, size_t bytes)
{
  if (bytes > MEM_PAMAP_SIZE)
    return NULL;
  HV_PTE pte = hv_pte(0);
  pte = hv_pte_set_readable(pte);
  pte = hv_pte_set_writable(pte);
  pte = hv_pte_set_global(pte);
  pte = hv_pte_set_present(pte);
  pte = hv_pte_set_accessed(pte);
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);

  for (int i = 0; i < bytes; i += HV_PAGE_SIZE_SMALL)
  {
    ts_svmisc_page_table[HV_L2_INDEX(MEM_PAMAP_VA+i)] =
      hv_pte_set_pfn(pte, HV_CPA_TO_PFN(pa+i));
  }
  __insn_mf();   // let things settle for simulator
  return (void*) MEM_PAMAP_VA;
}


void
unmap_window(void* va, size_t bytes)
{
  assert(va == (void*) MEM_PAMAP_VA);
  for (int i = 0; i < bytes; i += HV_PAGE_SIZE_SMALL)
  {
    ts_svmisc_page_table[HV_L2_INDEX(MEM_PAMAP_VA+i)] = hv_pte(0);
  }
  __insn_mf();   // let things settle for simulator
  hv_flush_pages(MEM_PAMAP_VA, HV_PAGE_SIZE_SMALL, bytes);
}


void
set_default_oloc(uint32_t x, uint32_t y)
{
  ts_default_oloc_x = x;
  ts_default_oloc_y = y;
  ts_default_oloc_random = false;
  ts_default_oloc_enabled = true;
}


void
set_random_oloc(uint32_t seed)
{
  ts_random_oloc_seed = seed;
  ts_default_oloc_random = true;
  ts_default_oloc_enabled = true;
}


void
get_oloc(uint32_t* x, uint32_t* y)
{
  if (ts_default_oloc_random)
  {
    uint32_t randpos = rand_step(width * height, &ts_random_oloc_seed);
    *x = randpos % width;
    *y = randpos / width;
  }
  else
  {
    *x = ts_default_oloc_x;
    *y = ts_default_oloc_y;
  }
}
