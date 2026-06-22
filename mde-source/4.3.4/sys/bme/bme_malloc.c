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
 * Bare Metal Environment malloc/free functions.
 */

#include <bme/sys_info.h>

#include <bme/bme_malloc.h>

#include <tmc/mspace.h>

tmc_mspace bme_heap_mspace[BME_MAX_TILES];

/** Allocate memory from the heap.
 * @param bytes Number of bytes to allocate.
 * @returns Pointer to allocated memory, 0 if none.
 */
void*
bme_malloc(size_t bytes)
{
  return tmc_mspace_malloc(bme_heap_mspace[bme_tile_ordinal()], bytes);
}


/** Return memory refererenced by mem to the heap.
 * @param mem Pointer to memory to free.
 */
void
bme_free(void* mem)
{
  tmc_mspace_free(mem);
}

