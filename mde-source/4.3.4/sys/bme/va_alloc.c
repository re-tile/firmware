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

#include <addr_alloc.h>
#include <bme/bme_malloc.h>
#include <bme/tte.h>
#include <bme/types.h>
#include <bme/va_alloc.h>

int
bme_create_va_pool(VA start_va, int page_size, int num_pages, 
                   bme_va_pool_t* va_pool)
{
  if ((start_va + page_size * num_pages) < start_va)
    return -1;
  bme_addr_pool_t* addr_pool = bme_malloc(sizeof(*addr_pool));
  if (addr_pool == 0)
    return -1;
  *va_pool = (bme_va_pool_t)addr_pool;
  return bme_create_addr_pool(start_va, page_size, num_pages, addr_pool);

}

void
bme_free_va_pool(bme_va_pool_t* va_pool)
{
  void* addr_pool = (void*)(*va_pool);
  bme_free(addr_pool);
}

int
bme_alloc_va_range(bme_va_pool_t* va_pool, int num_pages, VA* va)
{
  ADDR addr;
  int err = bme_alloc_addr_range((bme_addr_pool_t*)(*va_pool), num_pages,
                                 &addr);
  *va = (uint32_t)addr;
  return err;
}

void
bme_free_va_range(bme_va_pool_t* va_pool, int num_pages, VA va)
{
  bme_free_addr_range((bme_addr_pool_t*)(*va_pool), num_pages, (ADDR)va);
}
