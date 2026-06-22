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
 * Physical memory allocator implementation.
 * @file
 */

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>  // NULL

#include <arch/chip.h>

#include "pa_allocator.h"




















// Real TILE build
#include <tnslock.h>
#define DELTA offsetof(PA_Allocator, base)
#define lock(p) \
  tnslock_lock((char*)(p)+DELTA, sizeof(*(p))-DELTA, (p)->lock)
#define unlock(p) \
  tnslock_unlock((char*)(p)+DELTA, sizeof(*(p))-DELTA, (p)->lock)



/** The large page index on the freelist.
 * It is immediately after the "colored" entries for the small pages.
 */
#define LARGE_PAGE_FREELIST_INDEX PA_COLORS

/** Number of small pages in a large page. */
#define LARGE_PAGE_PAGES (HV_PAGE_SIZE_LARGE / HV_PAGE_SIZE_SMALL)

#if HV_L2_SIZE > HV_PAGE_SIZE_SMALL

/** The L2 page table index on the freelist.
 */
#define L2_PAGE_TABLE_FREELIST_INDEX (PA_COLORS+1)

/** Number of pages for L2 page table.
 */
#define L2_PAGE_TABLE_PAGES (HV_L2_SIZE / HV_PAGE_SIZE_SMALL)

#endif // L2 page table support

/** Value to use for empty freelists.
 * Note that this precludes us from using the last page of memory if we
 * ever allow 44 bits of physical address space.  Not a big worry.
 */
#define EMPTY_FREELIST -1U

/** PTE describing how to read/write the pages */
static HV_PTE pa_access_pte =
  {
    HV_PTE_NO_ALLOC_L1 |
    HV_PTE_READABLE | HV_PTE_WRITABLE |
    (HV_PTE_MODE_UNCACHED << HV_PTE_INDEX_MODE)
  };

PA_Allocator*
pa_init(PA_Allocator* p, PA_Addr base, uint64_t size)
{
  // Disable locking on the heap until we are ready.
  p->lock = NULL;

  // Round up the base by discarding anything at the beginning
  // not aligned to a small page size.
  int delta = base & (HV_PAGE_SIZE_SMALL-1);
  if (delta)
  {
    delta = HV_PAGE_SIZE_SMALL - delta;
    base += delta;
    size -= delta;
  }

  // Save base and size, converted to page units.
  p->base = HV_CPA_TO_PFN(base);
  p->size = HV_CPA_TO_PFN(size);

  // Start with no dedicated large-page area.
  p->large_heap_base = p->large_heap_size = 0;

  // Clear all the freelists.
  for (size_t i = 0; i < sizeof(p->freelist)/sizeof(p->freelist[0]); ++i)
    p->freelist[i] = EMPTY_FREELIST;


  // Make sure it is all out to memory.
  __insn_flush(p);
  __insn_flush(&p->base);
  __insn_mf();


  return p;
}

void
pa_set_lock(PA_Allocator* p, int* lock)
{
  p->lock = lock;


  // Flush the new lock address out to memory.
  __insn_flush(p);
  __insn_mf();

}

/** Return the page color for a virtual address.
 * See discussion for PA_COLORS in pa_allocator.h.
 */
static unsigned int
page_color(uintptr_t va)
{
  return HV_CPA_TO_PFN(va) & (PA_COLORS - 1);
}

/** Put a given physical page at the head of the given freelist.
 * Since our API is 64-bit anyway, we use CPAs to chain the free pages
 * rather than PFNs.
 */
static void
put_freelist_page(PA_Allocator* p, unsigned int index, PA_Addr pa)
{
  hv_physaddr_write64(pa, pa_access_pte, HV_PFN_TO_CPA(p->freelist[index]));
  p->freelist[index] = HV_CPA_TO_PFN(pa);
}

/** Get a page from the head of the given freelist. */
static PA_Addr
get_freelist_page(PA_Allocator* p, unsigned int index)
{
  unsigned int free_pfn = p->freelist[index];
  if (free_pfn == EMPTY_FREELIST)
    return PA_FAILED;
  PA_Addr retval = HV_PFN_TO_CPA(free_pfn);
  p->freelist[index] =
    HV_CPA_TO_PFN(hv_physaddr_read64(retval, pa_access_pte));
  return retval;
}

static PA_Addr
get_heap_page(PA_Allocator* p, unsigned int color)
{
  PA_Addr retval;
  while (1)
  {
    if (p->size == 0)
    {
      if (p->large_heap_size == 0)
      {
        // The heap is truly empty, but try to scavenge a free large page.
        retval = get_freelist_page(p, LARGE_PAGE_FREELIST_INDEX);
        if (retval == PA_FAILED)
          break;        // no heap space left
        p->base = HV_CPA_TO_PFN(retval);
        p->size = LARGE_PAGE_PAGES;
      }
      else
      {
        // The heap is not really empty; reclaim the large page heap.
        p->base = p->large_heap_base;
        p->size = p->large_heap_size;
        p->large_heap_base = p->large_heap_size = 0;
      }
    }
    retval = HV_PFN_TO_CPA(p->base);
    ++p->base;
    --p->size;
    unsigned int new_color = page_color((uintptr_t) retval);
    if (new_color == color)
      break;
    put_freelist_page(p, new_color, retval);
  }
  return retval;
}

PA_Addr
pa_alloc_page(PA_Allocator* p, const void* va)
{
  lock(p);
  unsigned int color = page_color((uintptr_t) va);
  PA_Addr retval = get_freelist_page(p, color);
  if (retval == PA_FAILED)
    retval = get_heap_page(p, color);
  unlock(p);
  return retval;
}

PA_Addr
pa_alloc_large_page(PA_Allocator* p)
{
  lock(p);
  PA_Addr retval = get_freelist_page(p, LARGE_PAGE_FREELIST_INDEX);
  if (retval == PA_FAILED)
  {
    if (p->large_heap_size == 0)
    {
      // We don't have a properly-aligned area in the heap yet.
      // Make one, and tell the rest of the allocator that the heap
      // now ends just below that properly-aligned area.
      // If we don't have room, we just leave large_heap_size == 0.
      uint32_t large_heap_base =
        (p->base + LARGE_PAGE_PAGES - 1) & -LARGE_PAGE_PAGES;
      if ((large_heap_base - p->base) + LARGE_PAGE_PAGES <= p->size)
      {
        p->large_heap_base = large_heap_base;
        p->large_heap_size = p->size - (large_heap_base - p->base);
        p->size = large_heap_base - p->base;
      }
    }

    // If we have enough space left in our large-page heap, return a
    // new page from that part of the heap.
    if (p->large_heap_size < LARGE_PAGE_PAGES)
      retval = PA_FAILED;
    else
    {
      retval = HV_PFN_TO_CPA(p->large_heap_base);
      p->large_heap_base += LARGE_PAGE_PAGES;
      p->large_heap_size -= LARGE_PAGE_PAGES;
    }
  }
  unlock(p);
  return retval;
}

PA_Addr
pa_alloc_pages(PA_Allocator* p, const void* va, unsigned int bytes)
{
  unsigned int npages =
    (bytes + HV_PAGE_SIZE_SMALL - 1) >> HV_LOG2_PAGE_SIZE_SMALL;
  if (npages == 1)
    return pa_alloc_page(p, va);
  lock(p);
  PA_Addr retval;
#if HV_L2_SIZE > HV_PAGE_SIZE_SMALL
  if (npages == L2_PAGE_TABLE_PAGES)
  {
    retval = get_freelist_page(p, L2_PAGE_TABLE_FREELIST_INDEX);
    if (retval != PA_FAILED)
    {
      unlock(p);
      return retval;
    }
  }
#endif
  unsigned int color = page_color((uintptr_t) va);
  retval = get_heap_page(p, color);
  if (p->size < npages-1)
  {
    --p->base;
    ++p->size;
    retval = PA_FAILED;
  }
  else
  {
    p->base += npages-1;
    p->size -= npages-1;
  }
  unlock(p);
  return retval;
}

void
pa_free_page(PA_Allocator* p, PA_Addr pa)
{
  lock(p);
  put_freelist_page(p, page_color((uintptr_t) pa), pa);
  unlock(p);
}

void
pa_free_large_page(PA_Allocator* p, PA_Addr pa)
{
  lock(p);
  put_freelist_page(p, LARGE_PAGE_FREELIST_INDEX, pa);
  unlock(p);
}

void
pa_free_pages(PA_Allocator* p, PA_Addr pa, unsigned int bytes)
{
  unsigned int npages =
    (bytes + HV_PAGE_SIZE_SMALL - 1) >> HV_LOG2_PAGE_SIZE_SMALL;
  lock(p);

#if HV_L2_SIZE > HV_PAGE_SIZE_SMALL
  if (npages == L2_PAGE_TABLE_PAGES)
  {
    put_freelist_page(p, L2_PAGE_TABLE_FREELIST_INDEX, pa);
    unlock(p);
    return;
  }
#endif

  for (unsigned int i = 0; i < npages; ++i)
  {
    put_freelist_page(p, page_color((uintptr_t) pa), pa);
    pa += HV_PAGE_SIZE_SMALL;
  }
  unlock(p);
}
