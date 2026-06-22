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
 * Manage the "extra" hypervisor physical memory, and its virtual memory
 * overall.  Also, routines to implement client syscalls related to virtual
 * memory.
 */

#include "sys/libc/include/util.h"

#include <string.h>
#include <arch/spr.h>

#include <hv/hypervisor.h>

#include "debug.h"
#include "devices.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "lock.h"
#include "mapping.h"
#include "physacc.h"
#include "page.h"
#include "tlb.h"


//
// The interfaces we provide in this file support a virtual memory
// allocator, a physical memory allocator, a shared allocator (which
// dynamically allocates both VA and PA space from the first two
// allocators), and a local allocator (which does neither).
//

/** Lowest VA currently allocated by get_virt().  This decreases
 *  as we allocate more virtual space. */
static VA virt_alloc_start;

/** Nonzero if the shared allocator has been initialized on this
 *  tile. */
int shared_alloc_initialized = 0;

/** Start of the remaining shared allocatable space.  This increases
 *  as we allocate more shared space.  If this is >= virt_alloc_start
 *  on a non-dedicated tile, we are out of virtual memory. */
static char* shared_alloc_start _SHARED;

/** Length of the remaining local allocatable space.  This is just what's
 *  currently mapped; we might be able to get more space than this by
 *  allocating additional physical pages. */
static size_t shared_alloc_len _SHARED;

/** Lock protecting shared_alloc_start and shared_alloc_len. */
static spinlock_t shared_alloc_lock _SHARED;

/** Physical address of our dedicated L2-flush memory region. */
PA l2_flush_pa;

/** The last byte of free physical memory.  This decreases as we
 *  allocate more physical space. This instance is used only until
 *  the shared allocator is initialized; after that, we use
 *  hv_alloc_pa_shared. */
static PA hv_alloc_pa;

/** The last byte of free physical memory.  This decreases as we
 *  allocate more physical space.  This instance is used after
 *  the shared allocator is initialized; before that, we use 
 *  hv_alloc_pa. */
static PA hv_alloc_pa_shared _SHARED;

/** Lock protecting hv_alloc_pa_shared.  Only used when
 *  shared_alloc_initialized is nonzero. */
static spinlock_t phys_alloc_lock _SHARED;

/** Client virtual address of first byte of client-shared region. */
VA client_shared_client_va_base;

/** Client virtual address of last byte of client-shared region. */
VA client_shared_client_va_last;

/** Start of the remaining local allocatable space.  This increases
 *  as we allocate more local space. */
static char* local_alloc_start;

/** Length of the remaining local allocatable space. */
static size_t local_alloc_len;

/** Description of our client-shared pages. */
client_shared_map_t client_shared_map[HV_NUM_CLIENT_SHARED_PAGES];

/** Table of pages used for the shared memory region.  Invalid entries are
 *  set to ~0.  This structure will be allocated out of the shared space,
 *  and will thus be mapped to a VA, but for consistency, it is always
 *  accessed via phys_{rd,wr}64. */
PA shared_mapping_table;

/** Attribute (AER_1 or AAR, depending on the chip) used to access shared
 *  space, including the shared mapping table. */
unsigned long shared_attr;


/** Initialize the local space allocator.
 */
void
init_local_alloc()
{
  local_alloc_start = (char*) ROUND_UP_WD((intptr_t) _ebss);
  local_alloc_len = ROUND_DN_WD((1 << HV_DATA_PAGE_SHIFT) -
                                (_ebss - _sstack));

  //
  // Initialize the client-shared space allocator.  We use the space at the
  // very end of our HV region for the client-shared pages.
  //
  client_shared_client_va_last = HV_VA_LIMIT - 1;
  client_shared_client_va_base =
    client_shared_client_va_last + 1 -
    (HV_NUM_CLIENT_SHARED_PAGES << HV_CLIENT_SHARED_PAGE_SHIFT);
}


/** Initialize the VA and PA allocators.
 */
void
init_va_pa_alloc()
{
  //
  // Set up the virtual allocator.  Note that on a dedicated tile,
  // we can use all of the VA space normally given to the client.
  //
  virt_alloc_start = (is_dedicated) ? HV_VA_BASE : HV_ALLOC_VA;

  //
  // Set up the physical allocator.  If we're striping on T64/Pro, all of
  // our memory goes into it; otherwise, just memory on mshim 0.
  //
  PA total_mem = mshim_sizes[0];


  //
  // We steal pages for cache flushing first, before we round down to the
  // shared page size, since it reduces waste from rounding.
  //
  // TODO: we could eventually reserve l2-flushing pages on each mshim, and
  // distribute their usage among the tiles, although this would incur
  // more rounding loss.
  //
  l2_flush_pa = ROUND_DN(total_mem -
                         HV_CODE_DATA_MEM_SIZE(chip_ulhc, chip_lrhc) -
                         2 * CHIP_L2_CACHE_SIZE(),
                         HV_FLUSH_PAGE_SIZE);

  hv_alloc_pa = ROUND_DN(l2_flush_pa, HV_SHARED_PAGE_SIZE);

  if (hv_alloc_pa > total_mem)
    panic("not enough memory for per-tile data areas and flush pages");

  hv_alloc_pa += mshim_bases[0];
}


/** Set up shared_attr to home shared memory on the given tile.
 * @param shared_home Home tile for shared memory.
 */
static void
set_shared_attr(pos_t shared_home)
{

  SPR_DTLB_TSB_FILL_CURRENT_ATTR_t attr =
  {{
    .v = 1,
    .g = 1,
    .ps = TTE_SHIFT_TO_PS(page_shift_small),
    .w = 1,
    .mpl = HV_PL,
    .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT,
    .cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE,
    .location_x_or_page_mask = shared_home.bits.x,
    .location_y_or_page_offset = shared_home.bits.y,
    .dtlbv = 1,
  }};
  shared_attr = attr.word;

}


/** Initialize the shared space allocator.
 * @param sts Slave tile state.
 */
void
init_shared_alloc(struct slave_tile_state* sts)
{
  //
  // Set up the attributes we'll use for shared pages.  Right now we just
  // home them on the chip console tile, which we know is not dedicated and
  // which is already incurring some hypervisor overhead.  If the chip
  // console tile isn't the master tile, the master tile uses itself
  // initially and rehomes after; see rehome_shared(), below.  We might
  // eventually also want to support using hash-for-home here, but note
  // that we can't set that up initially, since we can't home on a tile
  // which is still in the booter; we'd have to do an even more complex
  // rehoming procedure, including a global TLB shootdown and cache flush,
  // after all tiles had been started.
  //
  set_shared_attr(is_master ? chip_master : chip_console);

  if (is_master)
  {
    //
    // Go get the appropriate number of shared pages and record their
    // addresses so the TLB miss handler can map them in.
    //
    size_t static_shared_space = ROUND_UP(_eshared - _sshared, sizeof (PA));
    size_t total_shared_space = static_shared_space +
                                HV_NUM_SHARED_PAGES * sizeof (PA);
    size_t total_shared_pages =
      (total_shared_space + HV_SHARED_PAGE_SIZE - 1) / HV_SHARED_PAGE_SIZE;

    hv_alloc_pa -= total_shared_pages * HV_SHARED_PAGE_SIZE;
#ifdef TTE_VA_PA_ALIGNMENT
    // The target VA is aligned to 4GB, so align the PA as well.
    hv_alloc_pa = ROUND_DN(hv_alloc_pa, TTE_VA_PA_ALIGN_SIZE);
#endif
    shared_mapping_table = hv_alloc_pa + static_shared_space;

    for (int i = 0; i < total_shared_pages; i++)
    {
      phys_wr64(shared_mapping_table + i * sizeof (PA),
                hv_alloc_pa + i * HV_SHARED_PAGE_SIZE,
                shared_attr);
    }

    //
    // Invalidate the rest of the page list.
    //
    for (int i = total_shared_pages; i < HV_NUM_SHARED_PAGES; i++)
      phys_wr64(shared_mapping_table + i * sizeof (PA), ~(PA) 0, shared_attr);

    //
    // Copy from the prototype shared area to the actual shared area.  The
    // prototype stuff ends up in the memory image right after our text and
    // regular initialized data, so with a little address magic we can use
    // a normal data VA to read it; this relies on the special support we
    // have for mapping our text as data for debugging.
    //
    char* proto_shared = _stext + (_sshared_phys - _stext_phys);
    memcpy(_sshared, proto_shared, _eshared - _sshared);

    shared_alloc_start = _sshared + total_shared_space;
    shared_alloc_len = 
      total_shared_pages * HV_SHARED_PAGE_SIZE - total_shared_space;

    sts->shared_mapping_table = shared_mapping_table;

    hv_alloc_pa_shared = hv_alloc_pa;
  }
  else
  {
    shared_mapping_table = sts->shared_mapping_table;
  }

  shared_alloc_initialized = 1;
}


/** Rehome shared memory.  We want to home shared memory on the console
 *  tile, but if that's not the master tile, the master has to home locally
 *  until the console tile is started.  In that case, this routine is
 *  called right before that happens; it flushes any cached shared data
 *  to memory, changes the shared memory attribute to point to the new
 *  tile, and flushes the TLB/TSB to get rid of any old mappings.
 */
void
rehome_shared()
{
  inv_whole_l2();
  set_shared_attr(chip_console);
  clean_dtlb(0);
  init_tsb();
}


/** Allocate virtual memory.
 * @param size Number of bytes to allocate.
 * @param alignment Alignment; the allocated address will be an integral
 *  multiple of this value, and the allocated region's length will also be an
 *  integral multiple of this value.
 * @return Address of the start of the allocated block, or 0 if the
 *         requested amount is unavailable.
 */
VA
get_virt(size_t size, size_t alignment)
{
  size = ROUND_UP(size, alignment);
  VA new_virt_alloc_start = ROUND_DN(virt_alloc_start, alignment);
  new_virt_alloc_start -= size;

  if (shared_alloc_initialized && !is_dedicated &&
      shared_alloc_start > (char*) new_virt_alloc_start)
    return (0);
  else
    virt_alloc_start = new_virt_alloc_start;

  return (virt_alloc_start);
}


/** Free virtual memory.  Note: the current allocator is only able to free
 *  the last chunk of memory allocated.
 * @param va Address of memory to free.
 * @param size Number of bytes to free.
 */
void
free_virt(VA va, size_t size)
{
  if (virt_alloc_start == va)
    virt_alloc_start += size;
}


/** Return amount of available physical memory.
 * @param alignment Alignment; the amount of physical memory returned is
 *  such that a get_phys call for that amount, specifying this alignment,
 *  would succeed.
 * @return Amount of available physical memory.
 */
PA
avail_phys(size_t alignment)
{
  //
  // Note that we do not bother to get phys_alloc_lock here, since
  // we're just reading a value which we only store to atomically.
  //
  if (shared_alloc_initialized)
    return (ROUND_DN(hv_alloc_pa_shared - mshim_bases[0], alignment));
  else
    return (ROUND_DN(hv_alloc_pa - mshim_bases[0], alignment));
}


/** Allocate physical memory.
 * @param size Number of bytes to allocate.
 * @param alignment Alignment; the allocated address will be an integral
 *        multiple of this value.
 * @return Address of the start of the allocated block, or ~0 if the
 *         requested amount is unavailable.
 */
PA
get_phys(PA size, size_t alignment)
{
  PA retval = ~(PA) 0;

  if (shared_alloc_initialized)
  {
    spin_lock(&phys_alloc_lock);

    if (size <= avail_phys(alignment))
    {
      hv_alloc_pa_shared -= size;
      hv_alloc_pa_shared = retval = ROUND_DN(hv_alloc_pa_shared, alignment);
    }

    spin_unlock(&phys_alloc_lock);
  }
  else
  {
    if (!is_master)
      panic("call to get_phys() on non-master tile");

    if (size <= avail_phys(alignment))
    {
      hv_alloc_pa -= size;
      hv_alloc_pa = retval = ROUND_DN(hv_alloc_pa, alignment);
    }
  }

  return (retval);
}


/** Allocate memory from the per-tile data page.
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment of the allocated space.  If this is zero,
 *        the returned value is sufficiently well-aligned for any data type.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
void*
local_alloc(size_t size, size_t alignment)
{
  void* retval;

  ALLOC_TRACE("local_alloc(%zd, %zd)\n", size, alignment);

  if (alignment <= 0)
    alignment = 8;

  size_t alignment_loss = ROUND_UP((intptr_t) local_alloc_start, alignment) -
                          (intptr_t) local_alloc_start;

  ALLOC_TRACE("start %p, avail %zd, loss %zd\n", local_alloc_start,
              local_alloc_len, alignment_loss);

  if (size > local_alloc_len - alignment_loss)
    return (NULL);

  //
  // If this allocation is the size of a client-shared page, or it's going to
  // take all of our remaining space, and we can stick it at the end of the
  // per-tile page, let's do so.
  //
  if ((size == HV_CLIENT_SHARED_PAGE_SIZE ||
       size + alignment_loss == local_alloc_len) &&
      (((uintptr_t) local_alloc_start + local_alloc_len) &
       (alignment - 1)) == 0)
  {
    retval = local_alloc_start + local_alloc_len - size;
    local_alloc_len -= size;

    ALLOC_TRACE("end case, rv %p, start unchanged, new len %zd\n", retval,
                local_alloc_len);

    return (retval);
  }

  retval = local_alloc_start + alignment_loss;

  local_alloc_start += size + alignment_loss;
  local_alloc_len -= size + alignment_loss;

  ALLOC_TRACE("normal case, rv %p, new start %p, new len %zd\n", retval,
              local_alloc_start, local_alloc_len);

  return (retval);
}


/** Allocate memory from the globally shared data area.  This routine does
 *  not respect the lock for the shared allocator and should not be called
 *  directly.
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment of the allocated space.  If this is zero,
 *        the returned value is sufficiently well-aligned for any data type.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
static void*
_shared_alloc_unlocked(size_t size, size_t alignment)
{
  void* retval;

  ALLOC_TRACE("shared_alloc(%zd, %zd)\n", size, alignment);

  if (alignment <= 0)
    alignment = 8;

  size_t alignment_loss = ROUND_UP((intptr_t) shared_alloc_start, alignment) -
                          (intptr_t) shared_alloc_start;

  ALLOC_TRACE("start %p, avail %zd, loss %zd\n", shared_alloc_start,
              shared_alloc_len, alignment_loss);

  if (size > shared_alloc_len - alignment_loss)
  {
    //
    // We don't have enough space mapped for this.  Before we give up,
    // we'll try to get some more physical pages and map them in.
    //
    size_t needed = size - (shared_alloc_len - alignment_loss);
#ifdef TTE_VA_PA_ALIGNMENT
    // Request enough pages so that we can assign them to VAs that are
    // aligned to the PAs we have allocated.  We could try and take
    // less memory if the next available PA happened to be aligned
    // with the next VA, but we don't bother for now.
    needed = ROUND_UP(needed, TTE_VA_PA_ALIGN_SIZE);
#else
    needed = ROUND_UP(needed, HV_SHARED_PAGE_SIZE);
#endif

    //
    // Before we allocate memory, make sure we would have enough
    // VA space, and enough slots in the mapping table, to use it.
    // Note that the VA space check can not be completely reliable,
    // since the VA allocator is per-tile.
    //
    if (shared_alloc_start + alignment_loss + needed >
        ((is_dedicated) ? (char*) HV_ALLOC_VA : (char*) virt_alloc_start))
      return (NULL);

    int next_slot = ((shared_alloc_start - _sshared) + shared_alloc_len) /
                    HV_SHARED_PAGE_SIZE;
    int pages_needed = needed / HV_SHARED_PAGE_SIZE;
    if (next_slot + pages_needed > HV_NUM_SHARED_PAGES)
      return (NULL);

    //
    // Okay, let's see if we can get the memory.
    //
    PA new_space = get_phys(needed, HV_SHARED_PAGE_SIZE);
    if (new_space == ~(PA) 0)
      return (NULL);

#ifdef TTE_VA_PA_ALIGNMENT
    //
    // As we walk through the PAs, normally we just assign them to the
    // VAs starting with the lowest address we just allocated.  But
    // if they're not aligned properly for that, we add a bias to the
    // VA slot we're using for the next PA so that it ends up aligned.
    //
    int align_bias = ((new_space >> HV_SHARED_PAGE_SHIFT) - next_slot) &
      ((1 << (TTE_VA_PA_ALIGN_SHIFT - HV_SHARED_PAGE_SHIFT)) - 1);
#endif

    //
    // Now map it in.
    //
    for (int i = 0; i < pages_needed; i++)
    {
#ifdef TTE_VA_PA_ALIGNMENT
      int slot = next_slot + ((i + align_bias) % pages_needed);
#else
      int slot = next_slot + i;
#endif
      phys_wr64(shared_mapping_table + slot * sizeof (PA),
                new_space, shared_attr);
      new_space += HV_SHARED_PAGE_SIZE;
    }

    shared_alloc_len += needed;
  }

  retval = shared_alloc_start + alignment_loss;

  shared_alloc_start += size + alignment_loss;
  shared_alloc_len -= size + alignment_loss;

  ALLOC_TRACE("rv %p, new start %p, new len %zd\n", retval,
              shared_alloc_start, shared_alloc_len);

  return (retval);
}


/** Allocate memory from the globally shared data area.
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment of the allocated space.  If this is zero,
 *        the returned value is sufficiently well-aligned for any data type.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
void*
shared_alloc(size_t size, size_t alignment)
{
  spin_lock(&shared_alloc_lock);

  void* retval = _shared_alloc_unlocked(size, alignment);

  spin_unlock(&shared_alloc_lock);

  return (retval);
}


/** Allocate memory from the set of pages shared with the client.
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @param readonly If nonzero, the memory will not be writable by the
 *        client.
 * @param superonly If nonzero, the memory will not be readable or writable
 *        by code running at PL0.
 * @param client_va The client virtual address at which the memory is mapped
 *        will be written to this location.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
void*
client_shared_alloc(size_t size, size_t alignment, int readonly, int superonly,
                    VA* client_va)
{
  ALLOC_TRACE("client_shared_alloc(%zd, %zd)\n", size, alignment);

  //
  // We don't currently have any way to map something bigger than the page size.
  //
  if (size > HV_CLIENT_SHARED_PAGE_SIZE ||
      alignment > HV_CLIENT_SHARED_PAGE_SIZE)
    return (NULL);

  //
  // Default alignment.
  //
  if (alignment <= 0)
    alignment = 8;

  //
  // Ensure flags are either 1 or 0.
  //
  readonly = (readonly != 0);
  superonly = (superonly != 0);

  //
  // Hunt through the current mappings to see if there's an appropriate one
  // with enough space.
  //
  for (int i = 0; i < HV_NUM_CLIENT_SHARED_PAGES; i++)
  {
    client_shared_map_t* smp = &client_shared_map[i];
    size_t alignment_loss;

    if (!smp->valid)
    {
      //
      // If this entry isn't valid, none of the previous ones were suitable,
      // so let's just try to fill this one in.
      //
      char* newbuf =
        local_alloc(HV_CLIENT_SHARED_PAGE_SIZE, HV_CLIENT_SHARED_PAGE_SIZE);
      if (!newbuf)
        return (NULL);

      smp->hv_addr = newbuf;
      smp->valid = 1;
      smp->readonly = readonly;
      smp->superonly = superonly;
      smp->pa = vtop((VA) newbuf);
      smp->client_va =
        client_shared_client_va_base + (i << HV_CLIENT_SHARED_PAGE_SHIFT);
      smp->alloc_start = 0;
      smp->alloc_len = HV_CLIENT_SHARED_PAGE_SIZE;
      alignment_loss = 0;
    }
    else
    {
      //
      // If attributes don't match, or the request won't fit, try the next one.
      //
      if (smp->readonly != readonly || smp->superonly != superonly)
        continue;

      alignment_loss = ROUND_UP(smp->alloc_start, alignment) - smp->alloc_start;

      if (size > smp->alloc_len - alignment_loss)
        continue;
    }

    void* retval = smp->hv_addr + smp->alloc_start + alignment_loss;
    *client_va = smp->client_va + (smp->pa &
                                   RMASK(HV_CLIENT_SHARED_PAGE_SHIFT)) +
                 smp->alloc_start + alignment_loss;

    smp->alloc_start += size + alignment_loss;
    smp->alloc_len -= size + alignment_loss;

    ALLOC_TRACE("smp->start %#x, smp->avail %d, loss %zd\n", smp->alloc_start,
                smp->alloc_len, alignment_loss);

    return (retval);
  }

  return (NULL);
}


/** Validate a range of client virtual addresses.
 * @param va Starting virtual address.
 * @param len Length in bytes.
 * @return 0 if [va, va + len) are all valid client virtual addreses, else 1.
 */
int
c_va_invalid(VA va, unsigned long len)
{
  VA end_va = va + len - 1;

  // If len is so large that va wraps around, it must be bogus, because we
  // know that address ~0 is not owned by the client.
  if (end_va < va)
    return (1);

  // The client owns anything below where the hypervisor starts.
  // On Gx only, the client also owns the last 2 GB of the address space.

  if (end_va < HV_VA_BASE
      || (va >= 0xFFFFFFFF80000000UL && end_va <= 0xFFFFFFFFFFFFFFFFUL)
      )
    return (0);
  else
    return (1);
}


/** Return one of the client's virtual memory ranges.
 * @param idx Range number requested.
 * @return Virtual range structure.
 */
HV_VirtAddrRange
syscall_inquire_virtual(int idx)
{
  HV_VirtAddrRange retval;

  SYSCALL_TRACE("inquire_virtual(idx=%d)\n", idx);

  switch (idx)
  {
    //
    // We give the supervisor 255/256ths of the address space.  The
    // hypervisor owns 0xFFFF_FFFC_0000_0000 - 0xFFFF_FFFF_8000_0000.  This
    // doesn't change whether we're at PL2 or PL3; it could, but there's
    // really no requirement that it do so.  The last 2 GB of the address
    // space are also given to the client, to support programs compiled
    // in ILP32 mode.
    //
    case 0:
      retval.start =                  0UL;
      retval.size =       0x20000000000UL;
      break;
    case 1:
      retval.start = 0xFFFFFE0000000000UL;
      retval.size =       0x1FC00000000UL;
      break;
    case 2:
      retval.start = 0xFFFFFFFF80000000UL;
      retval.size =          0x80000000UL;
      break;
    default:
      retval.start =                  0UL;
      retval.size =                   0UL;
      break;
  }

  return (retval);
}


/** Convert a hypervisor virtual address to a physical address.
 * @param va Virtual address to convert.
 * @return The corresponding physical address.
 */
PA
vtop(VA va)
{
  // This is kind of a hack; we're assuming that we don't have any
  // instructions at the very end of our virtual address space, since
  // that's where we put the data page.  Of course, if we did have some
  // there, this routine wouldn't have a strictly defined value in that
  // case, so maybe it's not a big deal.

  if (va >= (VA) _sshared && 
      va < (VA) _sshared + (HV_NUM_SHARED_PAGES << HV_SHARED_PAGE_SHIFT))
  {
    // Page could be in the shared memory area.  Get the PA; if it's not
    // actually mapped, panic.
    int pageno = (va - (VA) _sshared) >> HV_SHARED_PAGE_SHIFT;

    PA pa = phys_rd64(shared_mapping_table + pageno * sizeof (PA), shared_attr);

    if (pa == ~(PA) 0)
      panic("can't translate shared VA %#lX to a physical address", va);

    return (pa + (va & (HV_SHARED_PAGE_SIZE - 1)));
  }
  else if (va >= (VA) _sstack)
    return (my_data_pa + (va - (VA) _sstack));
  else if (va >= (VA) _stext)
    return (my_text_pa + (va - (VA) _stext));
  else
    panic("can't translate VA %#lX to a physical address", va);
  /*NOTREACHED*/
  return (-1);
}
