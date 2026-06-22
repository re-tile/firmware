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
 * Support for address allocation.  This code is used for both physical
 * and virtual address allocation, since in the abstract, the difference
 * between the two is just 32-bit vs 64-bit.
 */

#include <string.h>
#include <addr_alloc.h>
#include <bits.h>
#include <stdio.h>
#include <util.h>

#include <bme/bme_malloc.h>
#include <bme/tte.h>
#include <bme/types.h>

#include <tmc/spin.h>

/** Debug tracing. */
#if 0
#define ADDR_ALLOC_TRACE(fmt, ...) \
    tprintf("dtlb: " fmt, ## __VA_ARGS__);
#else
#define ADDR_ALLOC_TRACE(...)
#endif

/** Macro to determine minimum of two numbers */
#define	MIN(a,b) (((a)<(b))?(a):(b))

/** Number of pages represented in a word of the free mask */
#define BME_FREEMASK_PAGES_PER_WORD 32

// The pages are represented in the mask as follows:
// The lowest-numbered address is the LSB of the 0th index.
// Increasing indexes into the mask array are used for increasing
// addresses.
static int
find_consecutive_free_pages(bme_addr_free_mask_t* free_mask, int num_pages,
                            uint32_t* start_index)
{
  int start_found_consec = 0;
  int found_consec = 0;

  if (!free_mask)
  {
    ADDR_ALLOC_TRACE("null free mask\n");
    return -1;
  }

  uint32_t* mask = free_mask->mask;
  for (int i = 0; i < free_mask->num_pages; i++)
  {
    // This is straightforward, but not efficient.  It will likely
    // be replaced with a linked list of free segments.
    int mask_index = i / BME_FREEMASK_PAGES_PER_WORD;
    int page_index_word = i % BME_FREEMASK_PAGES_PER_WORD;

    ADDR_ALLOC_TRACE("mask_index = %#lX page_index_word = %#lX "
                     "found_consec = %d start_found_consec = %d "
                     "mask[%d] = %#lX\n",
                     mask_index, page_index_word, found_consec,
                     start_found_consec, mask_index, mask[mask_index]);

    if ((mask[mask_index] & (1 << page_index_word)) == 0)
    {
      found_consec++;
      if (found_consec >= num_pages)
        break;
    }
    else
    {
      found_consec = 0;
      start_found_consec = i + 1;
    }
  }

  if (found_consec < num_pages)
    return -1;

  *start_index = start_found_consec;

  return 0;
}

static void
mask_consecutive_pages(bme_addr_free_mask_t* free_mask, int num_pages,
                       uint32_t start_page_index)
{
  uint32_t* mask = free_mask->mask;
  int end_page_index = start_page_index + num_pages;

  end_page_index = MIN(end_page_index, free_mask->num_pages - 1);

  // This is straightforward, but not efficient.  It will likely
  // be replaced with a linked list of free segments.
  for (int i = start_page_index; i < end_page_index; i++)
  {
    int mask_index = i / BME_FREEMASK_PAGES_PER_WORD;
    mask[mask_index] |= (1 << (i % BME_FREEMASK_PAGES_PER_WORD));
  }
}

static void
free_consecutive_pages(bme_addr_free_mask_t* free_mask, int num_pages,
                       uint32_t start_page_index)
{
  uint32_t* mask = free_mask->mask;
  int end_page_index = start_page_index + num_pages;

  end_page_index = MIN(end_page_index, free_mask->num_pages - 1);

  // This is straightforward, but not efficient.  It will likely
  // be replaced with a linked list of free segments.
  for (int i = start_page_index; i < end_page_index; i++)
  {
    int mask_index = i / BME_FREEMASK_PAGES_PER_WORD;
    mask[mask_index] &= ~(1 << (i % BME_FREEMASK_PAGES_PER_WORD));
  }
}

static int
page_size_supported(int page_size)
{
  if ((page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_4K))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_16K))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_64K))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_256K))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_1M))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_4M))) ||
      (page_size == (1 << TTE_PS_TO_SHIFT(TTE_PS_16M))))
    return 1;
  return 0;
}

/** Create a pool of virtual address space from which virtual addresses
 * can be allocated.  Users are responsible for ensuring that the address
 * range covered by the pool are available for use.
 * @param start_addr address at which to start the address pool.  Actual
 * start_addr of the created pool will be a page aligned value that is equal
 * to or greater than the value of this parameter.
 * @param page_size Size of pages to be allocated from this pool.  Must
 * be a supported page size.
 * @param num_pages Number of pages this range will cover.
 * @param addr_pool Pointer to the resulting pool that is created.
 * @returns 0 if successful, non-zero if pool could not be created.
 */
int
bme_create_addr_pool(ADDR start_addr, int page_size, int num_pages, 
                   bme_addr_pool_t* addr_pool)
{
  // Verify page size is a supported one
  if (!page_size_supported(page_size))
  {
    ADDR_ALLOC_TRACE("page size %#lX not supported\n", page_size);
    return -1;
  }

  // Force start_addr alignment
  start_addr = ROUND_UP(start_addr, (uint64_t)page_size);

  // Range check num_pages
  uint32_t range = page_size * num_pages;

  ADDR end_addr = start_addr + range;
  if (end_addr && (end_addr < start_addr))
  {
    ADDR_ALLOC_TRACE("range check failed: page_size=%#lX, "
                     "num_pages=%d(%#lX), start_addr = %#llX, "
                     "range=%#lX end_addr=%#llX\n", 
                     page_size, num_pages, num_pages, start_addr, range,
                     start_addr + range);
    return -1;
  }

  // Make data structure 
  addr_pool->start_addr = start_addr;
  addr_pool->page_size = page_size;
  addr_pool->free_mask.num_pages = num_pages;
  addr_pool->free_mask.num_mask_words = 
    (num_pages + BME_FREEMASK_PAGES_PER_WORD - 1) / BME_FREEMASK_PAGES_PER_WORD;
  addr_pool->free_mask.mask =
    (uint32_t*)bme_malloc(sizeof(*addr_pool->free_mask.mask) *
                          addr_pool->free_mask.num_mask_words);
  if (addr_pool->free_mask.mask == 0)
    return -1;
  memset(addr_pool->free_mask.mask, 0, sizeof(*addr_pool->free_mask.mask) *
                          addr_pool->free_mask.num_mask_words);

  tmc_spin_queued_mutex_init(&addr_pool->lock);

  return 0;
}

/** Find an unallocated range of address space, mark the rangs as allocated,
 * and return the starting address of the range.
 * @param addr_pool address pool from which to allocate contiguous range of
 * address space.
 * @param num_pages Number of pages to allocate.
 * @param addr Pointer to the resulting start addr of the allocated range.
 * @returns 0 if successful, non-zero if range could not be allocated.
 */
int
bme_alloc_addr_range(bme_addr_pool_t* addr_pool, int num_pages, ADDR* addr)
{
  uint32_t start_index = 0;

  if (!addr_pool || !addr)
    return -1;

  tmc_spin_queued_mutex_lock(&addr_pool->lock);

  int err = find_consecutive_free_pages(&addr_pool->free_mask, num_pages,
                                        &start_index);

  if (err >= 0)
  {
    // Calculate the address that corresponds to found index
    *addr = addr_pool->start_addr + (start_index * addr_pool->page_size);
    // Mark the memory as allocated
    mask_consecutive_pages(&addr_pool->free_mask, num_pages, start_index);
  }

  tmc_spin_queued_mutex_unlock(&addr_pool->lock);

  return err;
}

/** Free num_pages of contiguous allocated address range, starting at the page
 * on which addr resides.
 * @param addr_pool Pool which contains this address range.
 * @param num_pages Number of contiguous pages to return to free pool.
 * @param addr start address of first page to return to free pool.
 */
void
bme_free_addr_range(bme_addr_pool_t* addr_pool, int num_pages, ADDR addr)
{
  if (!addr_pool)
    return;

  tmc_spin_queued_mutex_lock(&addr_pool->lock);

  addr = ROUND_DN(addr, addr_pool->page_size);
  uint32_t start_index = (addr - addr_pool->start_addr) / addr_pool->page_size;
  ADDR_ALLOC_TRACE("start_index = %d\n", start_index);

  free_consecutive_pages(&addr_pool->free_mask, num_pages, start_index);

  tmc_spin_queued_mutex_unlock(&addr_pool->lock);
}
