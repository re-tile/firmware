/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

/**
 * @file
 * Implementation of trio gxio calls.
 */


#include "gxio/trio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include "trio_rpc_call.h"



int
gxio_trio_destroy(gxio_trio_context_t* context)
{

  int i;

  // Unmap all the mmap regions of the trio context.
  for (i = 0; i < TRIO_NUM_MAP_SQ_REGIONS; i++)
    munmap(context->scatter_queue_vas[i], HV_TRIO_SQ_SIZE);

  for (i = 0; i < TRIO_NUM_PUSH_DMA_RINGS; i++)
    munmap(context->push_dma_vas[i], HV_TRIO_DMA_REGION_SIZE);

  for (i = 0; i < TRIO_NUM_PULL_DMA_RINGS; i++)
    munmap(context->pull_dma_vas[i], HV_TRIO_DMA_REGION_SIZE);

  munmap(context->mmio_base_mac, 1 << TRIO_CFG_REGION_ADDR__PROT_SHIFT);

  /* Close the TRIO file handler. */
  if (context->fd >= 0)
    close(context->fd);


  return 0;
}

int
gxio_trio_init(gxio_trio_context_t* context, unsigned int trio_index)
{
  char file[32];
  int fd;


  snprintf(file, sizeof(file), "/dev/iorpc/trio%d", trio_index);
  fd = open(file, O_RDWR);
  if (fd < 0)
  {
    context->fd = -1;
    return -errno;
  }

  memset(context, 0, sizeof(*context));
  context->fd = fd;

  /* Create MMIO mapping to TRIO's config space, i.e. centralized registers. */
  context->mmio_base_mac =
    mmap(NULL, 1 << TRIO_CFG_REGION_ADDR__PROT_SHIFT,
         PROT_READ | PROT_WRITE, MAP_SHARED,
         context->fd, HV_TRIO_CONFIG_OFFSET);
  if (context->mmio_base_mac == MAP_FAILED)
    return -errno;


  return 0;
}

int
gxio_trio_free_memory_map(gxio_trio_context_t* context, unsigned int map)
{
  return gxio_trio_free_memory_map_aux(context, map);
}

int
gxio_trio_init_memory_map(gxio_trio_context_t* context, unsigned int map,
                          void* target_mem, size_t target_size,
                          unsigned int asid, unsigned int mac,
                          uint64_t bus_address,
                          gxio_trio_order_mode_t order_mode)
{
  unsigned long vpn;

  /* Memory maps must be 4kB aligned. */
  if ((unsigned long)target_mem & ((1 << HV_TRIO_PAGE_SHIFT) - 1))
    return GXIO_ERR_ALIGNMENT;

  vpn = (unsigned long)target_mem >> HV_TRIO_PAGE_SHIFT;
  return gxio_trio_init_memory_map_aux(context, map, vpn, target_size,
                                       asid, mac, bus_address, order_mode);
}

int
gxio_trio_free_scatter_queue(gxio_trio_context_t* context, unsigned int queue)
{
  int result = gxio_trio_free_scatter_queue_aux(context, queue);
  if (result < 0)
    return result;

  /* Delete MMIO mappings. */
  if (context->scatter_queue_vas[queue])
  {
    result = munmap(context->scatter_queue_vas[queue], HV_TRIO_SQ_SIZE);

    context->scatter_queue_vas[queue] = NULL;
  }

  return 0;
}

int
gxio_trio_unregister_page(gxio_trio_context_t* context, unsigned int asid,
                          void* page)
{
  /*
   * Initialize a dummy but legal page_size for gxio_trio_unregister_page_aux()
   * below, though it is not used to unregister a page at all.
   */
  size_t page_size = getpagesize();

  return gxio_trio_unregister_page_aux(context, page, page_size, 0,
                                       asid,
                                       (unsigned long)page >>
                                       HV_TRIO_PAGE_SHIFT);
}

int
gxio_trio_register_page(gxio_trio_context_t* context, unsigned int asid,
                        void* page, size_t page_size, unsigned int page_flags)
{
  return gxio_trio_register_page_aux(context, page, page_size, page_flags,
                                     asid,
                                     (unsigned long)page >>
                                     HV_TRIO_PAGE_SHIFT);
}

int
gxio_trio_read_isr_status(gxio_trio_context_t* context, unsigned int vec_num)
{
  if (vec_num >= 5)
    return GXIO_ERR_INVAL;

  return gxio_trio_read_isr_status_aux(context, vec_num);
}

int
gxio_trio_write_isr_status(gxio_trio_context_t* context, unsigned int vec_num,
                           uint32_t bits_to_clear)
{
  if (vec_num >= 5)
    return GXIO_ERR_INVAL;

  return gxio_trio_write_isr_status_aux(context, vec_num, bits_to_clear);
}

int
gxio_trio_write_mmi_bits(gxio_trio_context_t* context, unsigned int map,
                         unsigned int bits, unsigned int mode)
{
  return gxio_trio_write_mmi_bits_aux(context, map, bits, mode);
}


int
gxio_trio_mask_mmi(gxio_trio_context_t* context, unsigned int map,
                   unsigned int mask)
{
  return gxio_trio_mask_mmi_aux(context, map, mask);
}

int
gxio_trio_unmask_mmi(gxio_trio_context_t* context, unsigned int map,
                     unsigned int mask)
{
  return gxio_trio_unmask_mmi_aux(context, map, mask);
}

int
gxio_trio_read_mmi_bits(gxio_trio_context_t* context, unsigned int map)
{
  return gxio_trio_read_mmi_bits_aux(context, map);
}

int
gxio_trio_init_scatter_queue(gxio_trio_context_t* context,
                             unsigned int queue, uint64_t size,
                             unsigned int asid,
                             unsigned int mac,
                             uint64_t bus_address,
                             gxio_trio_order_mode_t order_mode)
{
  /* Bind the scatter queue. */
  int result = gxio_trio_init_scatter_queue_aux(context, queue, size,
                                                asid, mac, bus_address,
                                                order_mode);
  if (result < 0)
    return result;

  /* Create MMIO mappings. */
  if (context->scatter_queue_vas[queue] == NULL)
  {
    context->scatter_queue_vas[queue] = mmap(NULL, HV_TRIO_SQ_SIZE,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       context->fd, HV_TRIO_SQ_OFFSET(queue));
    if (context->scatter_queue_vas[queue] == MAP_FAILED)
    {
      context->scatter_queue_vas[queue] = NULL;
      return -errno;
    }
  }

  return 0;
}

int
gxio_trio_free_pio_region(gxio_trio_context_t* context,
                          unsigned int pio_region)
{
  return gxio_trio_free_pio_region_aux(context, pio_region);
}

int
gxio_trio_init_pio_region(gxio_trio_context_t* context,
                          unsigned int pio_region,
                          unsigned int mac,
                          uint64_t bus_address,
                          unsigned int flags)
{
  uint32_t hi = bus_address >> 32;
  uint32_t lo = bus_address & ((1ULL << 32) - 1);

  /*
   * We're going to add the low part of the offset into the mmap
   * offset (see gxio_trio_map_pio_region()), so make sure the offset
   * is page aligned.
   */
  if (bus_address & (getpagesize() - 1))
    return GXIO_ERR_ALIGNMENT;

  int result = gxio_trio_init_pio_region_aux(context, pio_region, mac,
                                             hi, flags);

  if (result == 0)
    context->pio_offsets[pio_region] = lo;

  return result;
}

int
gxio_trio_unmap_pio_region(void* pio_mmio_addr, unsigned int length)
{
  return munmap(pio_mmio_addr, length);
}

void*
gxio_trio_map_pio_region(gxio_trio_context_t* context,
                         unsigned int pio_region, unsigned int length,
                         unsigned int offset)
{
  /*
   * Add back in the portion of the initial bus_address that couldn't
   * be programmed into hardware via gxio_trio_init_pio_region_aux().
   */
  offset += context->pio_offsets[pio_region];

  return mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, context->fd,
              HV_TRIO_PIO_OFFSET(pio_region) + offset);
}


int
gxio_trio_free_push_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring)
{
  int result = gxio_trio_free_push_dma_ring_aux(context, ring);
  if (result < 0)
    return result;

  /* Un-map the push DMA register region from VA space. */
  if (context->push_dma_vas[ring])
  {
    result = munmap(context->push_dma_vas[ring], HV_TRIO_DMA_REGION_SIZE);

    context->push_dma_vas[ring] = NULL;
  }

  return 0;
}

int
gxio_trio_init_push_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring, unsigned int mac,
                             unsigned int asid, unsigned int req_flags,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags)
{
  /* Configure the hardware registers. */
  int result = gxio_trio_init_push_dma_ring_aux(context, mem, mem_size,
                                                mem_flags, ring, mac, asid,
                                                req_flags);
  if (result < 0)
    return result;

  /* Map the push DMA register region into VA space. */
  if (context->push_dma_vas[ring] == NULL)
  {
    context->push_dma_vas[ring] = mmap(NULL, HV_TRIO_DMA_REGION_SIZE,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       context->fd,
                                       HV_TRIO_PUSH_DMA_OFFSET(ring));
    if (context->push_dma_vas[ring] == MAP_FAILED)
    {
      context->push_dma_vas[ring] = NULL;
      return -errno;
    }
  }

  return 0;
}

int
gxio_trio_free_pull_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring)
{
  int result = gxio_trio_free_pull_dma_ring_aux(context, ring);
  if (result < 0)
    return result;

  /* Un-map the pull DMA register region from VA space. */
  if (context->pull_dma_vas[ring])
  {
    result = munmap(context->pull_dma_vas[ring], HV_TRIO_DMA_REGION_SIZE);

    context->pull_dma_vas[ring] = NULL;
  }

  return 0;
}

int
gxio_trio_init_pull_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring, unsigned int mac,
                             unsigned int asid, unsigned int req_flags,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags)
{
  /* Configure the hardware registers. */
  int result = gxio_trio_init_pull_dma_ring_aux(context, mem, mem_size,
                                                mem_flags, ring, mac, asid,
                                                req_flags);
  if (result < 0)
    return result;

  /* Map the pull DMA register region into VA space. */
  if (context->pull_dma_vas[ring] == NULL)
  {
    context->pull_dma_vas[ring] = mmap(NULL, HV_TRIO_DMA_REGION_SIZE,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       context->fd,
                                       HV_TRIO_PULL_DMA_OFFSET(ring));
    if (context->pull_dma_vas[ring] == MAP_FAILED)
    {
      context->pull_dma_vas[ring] = NULL;
      return -errno;
    }
  }

  return 0;
}

int
gxio_trio_init_push_dma_queue(gxio_trio_dma_queue_t* queue,
                              gxio_trio_context_t* context,
                              unsigned int ring, unsigned int mac,
                              unsigned int asid, unsigned int req_flags,
                              void* mem, unsigned int mem_size,
                              unsigned int mem_flags)
{
  /* The init call below will verify that "mem_size" is legal. */
  unsigned int num_entries = mem_size / sizeof(gxio_trio_dma_desc_t);

  int result =
    gxio_trio_init_push_dma_ring(context, ring, mac, asid, req_flags,
                                 mem, mem_size, mem_flags);
  if (result < 0)
    return result;

  memset(queue, 0, sizeof(*queue));

  __gxio_dma_queue_init(&queue->dma_queue, context->push_dma_vas[ring],
                        num_entries, 0);

  queue->dma_descs = mem;
  queue->mask_num_entries = num_entries - 1;
  queue->log2_num_entries = __builtin_ctz(num_entries);

  return 0;
}

int
gxio_trio_init_pull_dma_queue(gxio_trio_dma_queue_t* queue,
                              gxio_trio_context_t* context,
                              unsigned int ring, unsigned int mac,
                              unsigned int asid, unsigned int req_flags,
                              void* mem, unsigned int mem_size,
                              unsigned int mem_flags)
{
  /* The init call below will verify that "mem_size" is legal. */
  unsigned int num_entries = mem_size / sizeof(gxio_trio_dma_desc_t);

  int result =
    gxio_trio_init_pull_dma_ring(context, ring, mac, asid, req_flags,
                                 mem, mem_size, mem_flags);
  if (result < 0)
    return result;

  memset(queue, 0, sizeof(*queue));

  __gxio_dma_queue_init(&queue->dma_queue, context->pull_dma_vas[ring],
                        num_entries, 0);

  queue->dma_descs = mem;
  queue->mask_num_entries = num_entries - 1;
  queue->log2_num_entries = __builtin_ctz(num_entries);

  return 0;
}
