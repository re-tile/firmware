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
 *
 * Bare Metal Environment virtual address allocator.  This functionality
 * allows users to create pools of contiguous virtual address space.  The
 * pool must be created with a designated page size.  Users are responsible
 * for ensuring that the range of addresses covered by the pool is available
 * for use.  Once the pool is created, ranges within the pool can be 
 * allocated and freed in increments of the pool's page size.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_ADDR_ALLOC_H
#define _SYS_BME_ADDR_ALLOC_H

#include <features.h>
#include <stdint.h>

#include <bme/types.h>

#include <tmc/spin.h>

__BEGIN_DECLS

/** typedef to use for all address types.
 */
typedef uint64_t ADDR;

/** Data structure containing mask which represent the free pages in a
 * virtual address pool.
 */
typedef struct bme_addr_free_mask_t
{
  uint32_t* mask;               /**< Pointer to mask of free pages. */
  uint32_t num_mask_words;      /**< Number of words in mask. */
  uint32_t num_pages;           /**< Number of pages represented in mask. */
}  bme_addr_free_mask_t;

/** Data structure representing pool of virtual address space. */
typedef struct bme_addr_pool_t
{
  ADDR start_addr;                  /**< Start ADDR of space covered by this pool. */
  uint32_t page_size;           /**< Size of pages in this pool. */
  bme_addr_free_mask_t free_mask; /**< Mask representing free pages. */
  tmc_spin_queued_mutex_t lock;            /**< Lock for protection. */
} bme_addr_pool_t;

/** Create a pool of virtual address space from which virtual addresses
 * can be allocated.  Users are responsible for ensuring that the address
 * range covered by the pool are available for use.
 * @param start_addr ADDR at which to start the ADDR pool.  Actual start_addr of
 * the created pool will be a page aligned value that is equal to or 
 * greater than the value of this parameter.
 * @param page_size Size of pages to be allocated from this pool.  Must
 * be a supported page size.
 * @param num_pages Number of pages this range will cover.
 * @param addr_pool Pointer to the resulting pool that is created.
 * @returns 0 if successful, non-zero if pool could not be created.
 */
int bme_create_addr_pool(ADDR start_addr, int page_size, int num_pages, 
                       bme_addr_pool_t* addr_pool);
/** 
 * @param addr_pool ADDR pool from which to allocate contiguous range of ADDR space.
 * @param num_pages Number of pages to allocate.
 * @param addr Pointer to the resulting start addr of the allocated range.
 * @returns 0 if successful, non-zero if range could not be allocated.
 */
int bme_alloc_addr_range(bme_addr_pool_t* addr_pool, int num_pages, ADDR* addr);

/** Free num_pages of contiguous allocated ADDR range, starting at the page
 * on which addr resides.
 * @param addr_pool Pool which contains this ADDR range.
 * @param num_pages Number of contiguous pages to return to free pool.
 * @param addr start ADDR of first page to return to free pool.
 */
void bme_free_addr_range(bme_addr_pool_t* addr_pool, int num_pages, ADDR addr);

__END_DECLS

#endif /* _SYS_BME_ADDR_ALLOC_H */

/** @} */
