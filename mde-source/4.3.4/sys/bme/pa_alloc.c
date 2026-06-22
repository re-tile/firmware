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
 * Support for virtual address allocation.
 */

#include <bits.h>
#include <stdio.h>
#include <util.h>

#include "addr_alloc.h"

#include <bme/bme_malloc.h>
#include <bme/tte.h>
#include <bme/types.h>
#include <bme/pa_alloc.h>

int
bme_create_pa_pool(PA start_pa, int page_size, int num_pages, 
                   bme_pa_pool_t* pa_pool)
{
  if ((start_pa + ((uint64_t)page_size * (uint64_t)num_pages)) < start_pa)
    return -1;
  bme_addr_pool_t* addr_pool = bme_malloc(sizeof(*addr_pool));
  if (addr_pool == 0)
    return -1;
  *pa_pool = (bme_pa_pool_t)addr_pool;
  return bme_create_addr_pool(start_pa, page_size, num_pages, addr_pool);

}

void
bme_free_pa_pool(bme_pa_pool_t* pa_pool)
{
  void* addr_pool = (void*)(*pa_pool);
  bme_free(addr_pool);
}

int
bme_alloc_pa_range(bme_pa_pool_t* pa_pool, int num_pages, PA* pa)
{
  ADDR addr;
  int err = bme_alloc_addr_range((bme_addr_pool_t*)(*pa_pool), num_pages,
                                 &addr);
  *pa = (uint32_t)addr;
  return err;
}

void
bme_free_pa_range(bme_pa_pool_t* pa_pool, int num_pages, PA pa)
{
  bme_free_addr_range((bme_addr_pool_t*)(*pa_pool), num_pages, (ADDR)pa);
}
