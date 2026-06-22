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
 * Bare Metal Environment physical address allocator.  This functionality
 * allows users to create pools of contiguous physical address space.  The
 * pool must be created with a designated page size.  Users are responsible
 * for ensuring that the range of addresses covered by the pool is available
 * for use.  Once the pool is created, ranges within the pool can be 
 * allocated and freed in increments of the pool's page size.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_PA_ALLOC_H
#define _SYS_BME_PA_ALLOC_H

#include <features.h>
#include <stdint.h>

#include "types.h"

__BEGIN_DECLS

/** Handle on allocated pool. */
typedef uint32_t bme_pa_pool_t;

/** Create a pool of physical address space from which physical addresses
 * can be allocated.  Users are responsible for ensuring that the address
 * range covered by the pool are available for use.
 * @param start_pa PA at which to start the PA pool.  Actual start_pa of
 * the created pool will be a page aligned value that is equal to or 
 * greater than the value of this parameter.
 * @param page_size Size of pages to be allocated from this pool.  Must
 * be a supported page size.
 * @param num_pages Number of pages this range will cover.
 * @param pa_pool Pointer to the resulting pool that is created.
 * @returns 0 if successful, non-zero if pool could not be created.
 */
int bme_create_pa_pool(PA start_pa, int page_size, int num_pages, 
                       bme_pa_pool_t* pa_pool);

/** Frees the storage occupied by a physical address pool.
 */
void bme_free_pa_pool(bme_pa_pool_t* pa_pool);

/** 
 * @param pa_pool PA pool from which to allocate contiguous range of PA space.
 * @param num_pages Number of pages to allocate.
 * @param pa Pointer to the resulting start pa of the allocated range.
 * @returns 0 if successful, non-zero if range could not be allocated.
 */
int bme_alloc_pa_range(bme_pa_pool_t* pa_pool, int num_pages, PA* pa);

/** Free num_pages of contiguous allocated PA range, starting at the page
 * on which pa resides.
 * @param pa_pool Pool which contains this PA range.
 * @param num_pages Number of contiguous pages to return to free pool.
 * @param pa start PA of first page to return to free pool.
 */
void bme_free_pa_range(bme_pa_pool_t* pa_pool, int num_pages, PA pa);

__END_DECLS

#endif /* _SYS_BME_PA_ALLOC_H */

/** @} */
