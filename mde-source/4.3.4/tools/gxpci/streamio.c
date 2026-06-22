/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
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

#define _XOPEN_SOURCE 500 // Needed to get pread/pwrite from unistd.h

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <gxio/common.h>
#include <gxio/trio.h>

#include <tmc/alloc.h>
#include <tmc/cpus.h>
#include <tmc/interrupt.h>
#include <tmc/ipi.h>
#include <tmc/task.h>

#include <gxpci/gxpci.h>
#include <gxpci/streamio.h>


static __thread streamio_context_t *g_streamio_context;


static void
mmi_func_dispatch(void* event)
{
  gxio_trio_context_t *trio_context = g_streamio_context->trio_context;

  //
  // Read out memory map region interrupt status from TRIO_INT_VEC4_W1TC.
  //
  int isr_status = gxio_trio_read_isr_status(trio_context, 4);

  //
  // Clear the status bits.
  //
  gxio_trio_write_isr_status(trio_context, 4, isr_status);
 
  //
  // The mmi_funcs has the interrupted memory map region index as the input 
  // parameter.
  // 
  for (int i = 0; i < TRIO_NUM_MAP_MEM_REGIONS; i++)
  {
    if (isr_status & 0x1)
    {
      int arg = i;
      g_streamio_context->resource.mmi_funcs[i](&arg);
    }

    isr_status >>= 1;
  }  
}

static void
push_dma_comp_func_dispatch(void* event)
{
  gxio_trio_context_t *trio_context = g_streamio_context->trio_context;

  //
  // Read out push dma interrupt status from TRIO_INT_VEC1_W1TC.
  //
  int isr_status = gxio_trio_read_isr_status(trio_context, 1);

  //
  // Clear the status bits.
  //
  gxio_trio_write_isr_status(trio_context, 1, isr_status);

  //
  // The push_dma_comp_funcs has the interrupted push dma ring index as the
  // input parameter.
  //
  for (int i = 0; i < TRIO_NUM_PUSH_DMA_RINGS; i++)
  {
    if (isr_status & 0x1)
    {
      int arg = i;
      g_streamio_context->resource.push_dma_comp_funcs[i](&arg);
    }

    isr_status >>= 1;
  }
}

static void
pull_dma_comp_func_dispatch(void* event)
{
  gxio_trio_context_t *trio_context = g_streamio_context->trio_context;

  //
  // Read out pull dma interrupt status from TRIO_INT_VEC2_W1TC.
  //
  int isr_status = gxio_trio_read_isr_status(trio_context, 2);

  //
  // Clear the status bits.
  //
  gxio_trio_write_isr_status(trio_context, 2, isr_status);

  //
  // The pull_dma_comp_funcs has the interrupted pull dma ring index as the
  // input parameter.
  //
  for (int i = 0; i < TRIO_NUM_PULL_DMA_RINGS; i++)
  {
    if (isr_status & 0x1)
    {
      int arg = i;
      g_streamio_context->resource.pull_dma_comp_funcs[i](&arg);
    }

    isr_status >>= 1;
  }
}


int 
streamio_init(gxio_trio_context_t *trio_context, streamio_context_t *context,
              int asid, unsigned int trio_index, unsigned int mac)
{
  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return STREAMIO_EBINDCPU;
  
  //
  // Error checkings. 
  //
  if (asid >= TRIO_NUM_ASIDS || asid < GXIO_ASID_NULL)
    return STREAMIO_EINVAL;

  if (trio_index >= TILEGX_NUM_TRIO || 
      mac >= TILEGX_TRIO_PCIES)
    return STREAMIO_EINVAL;

  //
  // Allocate an ASID if it isn't pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    asid = gxio_trio_alloc_asids(trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }
  else
  {
    asid = gxio_trio_alloc_asids(trio_context, 1, asid, HV_TRIO_ALLOC_FIXED);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }

  //
  // Fill in the context structure.
  //
  memset(context, 0, sizeof(*context));
  context->trio_context = trio_context;
  context->trio_index = trio_index;
  context->mac = mac;
  context->resource.asid = asid;

  //
  // Init push and pull DMA ring memory.
  // Note that the total memory size for both push and pull DMA rings can not
  // exceed a huge page.
  //
  size_t push_dma_size_per_ring = STREAMIO_PUSH_DMA_RING_LEN * 
    sizeof(gxio_trio_dma_desc_t);

  size_t pull_dma_size_per_ring = STREAMIO_PULL_DMA_RING_LEN *
    sizeof(gxio_trio_dma_desc_t);

  size_t total_dma_mem = push_dma_size_per_ring * TRIO_NUM_PUSH_DMA_RINGS + 
    pull_dma_size_per_ring * TRIO_NUM_PULL_DMA_RINGS;

  //
  // The smallest memory size is 512KB for both push and pull DMA rings, so
  // a huge page is initialized here.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  void* dma_mem = tmc_alloc_map(&alloc, total_dma_mem);
  if (dma_mem == NULL)
    return STREAMIO_ENOMEM;

  //
  // Register the allocated page to IOTLB, a huge page size is used.
  //
  int result = gxio_trio_register_page(trio_context, (unsigned int)asid, 
                                       dma_mem, STREAMIO_HPAGE_SIZE, 0);
  GXIO_VERIFY_ZERO(result, "gxio_trio_register_page()");

  for (int i = 0; i < TRIO_NUM_MAP_MEM_REGIONS; i++)
    context->resource.map_mem_regions[i] = STREAMIO_RESOURCE_EMPTY;

  for (int i = 0; i < TRIO_NUM_PUSH_DMA_RINGS; i++)
  {
    context->resource.push_dma_rings[i] = STREAMIO_RESOURCE_EMPTY;
    context->resource.push_dma_mems[i] = dma_mem + push_dma_size_per_ring * i;
  }

  for (int i = 0; i < TRIO_NUM_PULL_DMA_RINGS; i++)
  {
    context->resource.pull_dma_rings[i] = STREAMIO_RESOURCE_EMPTY;
    context->resource.pull_dma_mems[i] = 
      dma_mem + push_dma_size_per_ring * TRIO_NUM_PUSH_DMA_RINGS + 
      pull_dma_size_per_ring * i;
  }
  
  // 
  // Initialize this process for IPI use (on this core only).
  //
  result = tmc_ipi_init(NULL);
  GXIO_VERIFY_NON_NEGATIVE(result, "tmc_ipi_init()");

  // Now that we're bound to a core, activate the IPI functionality.
  result = tmc_ipi_activate();
  GXIO_VERIFY_ZERO(result, "tmc_ipi_activate()");

  //
  // Install memory map interrupt callback function dispatcher.
  //
  result = tmc_ipi_event_install(0, IPI_EVENT_MMI, mmi_func_dispatch, NULL);
  GXIO_VERIFY_ZERO(result, "tmc_ipi_event_install()");

  //
  // Install push dma completion interrupt callback function dispatcher.
  //
  result = tmc_ipi_event_install(0, IPI_EVENT_PUSH_DMA_COMP, 
                                 push_dma_comp_func_dispatch, NULL);
  GXIO_VERIFY_ZERO(result, "tmc_ipi_event_install()");

  //
  // Install pull dma completion interrupt callback function dispatcher.
  //
  result = tmc_ipi_event_install(0, IPI_EVENT_PULL_DMA_COMP, 
                                 pull_dma_comp_func_dispatch, NULL);
  GXIO_VERIFY_ZERO(result, "tmc_ipi_event_install()");  

  //
  // Make streamio_context global visible.
  //
  g_streamio_context = context;

  return 0;
}

int
streamio_init_mem_map(streamio_context_t *context, unsigned int mem_map_index, 
                      void* tile_mem, size_t map_size, 
                      uint64_t bus_address, tmc_ipi_func_t mmi_func)
{
  streamio_resource_t *resource = &context->resource;
  gxio_trio_context_t *trio_context = context->trio_context;
  int asid = context->resource.asid;
  int result;
  unsigned int tile_mem_offset = 0;
  unsigned int mac = context->mac;

  //
  // Error checkings.
  //
  if (mem_map_index >= TRIO_NUM_MAP_MEM_REGIONS)
    return STREAMIO_EINVAL;

  if (resource->map_mem_regions[mem_map_index] != STREAMIO_RESOURCE_EMPTY)
    return STREAMIO_EOVWR; 

  if (tile_mem != NULL && (intptr_t)tile_mem & (HV_TRIO_PAGE_SIZE - 1))
    return STREAMIO_EINVAL;

  if (map_size != STREAMIO_HPAGE_SIZE && map_size != STREAMIO_PAGE_SIZE)
    return STREAMIO_EINVAL;

  if (bus_address & (HV_TRIO_PAGE_SIZE - 1))
    return STREAMIO_EINVAL;

  // 
  // Allocate a memory map region according to mem_map_index.
  //
  mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, mem_map_index, 
                                              HV_TRIO_ALLOC_FIXED);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  // 
  // Allocate a page according to target_mem and target_size if necessary.
  //
  if (tile_mem == NULL)
  {
    tmc_alloc_t alloc = TMC_ALLOC_INIT;
   
    if (map_size == STREAMIO_HPAGE_SIZE)
      tmc_alloc_set_huge(&alloc);
     
    tile_mem = tmc_alloc_map(&alloc, map_size);
    if (tile_mem == NULL)
      return STREAMIO_ENOMEM;
  }

  //
  // Register the allocated page to IOTLB.
  //
  result = gxio_trio_register_page(trio_context, (unsigned int)asid, 
                                   tile_mem, map_size, 0);
  GXIO_VERIFY_ZERO(result, "gxio_trio_register_page()");

  //
  // Init the memory map region.
  //
  result = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                     tile_mem, map_size, asid, mac, bus_address,
                                     GXIO_TRIO_ORDER_MODE_STRICT);
  GXIO_VERIFY_ZERO(result, "gxio_trio_init_memory_map()");

  //
  // Init the memory map interrupt according to mmi_func.
  //
  if (mmi_func != NULL)
  {
    //
    // Enable memory map interrupt.
    //
    result = gxio_trio_enable_mmi(trio_context, 
                                  tmc_cpus_get_my_cpu(),
                                  IPI_EVENT_MMI,
                                  mem_map_index,
                                  TRIO_MAP_MEM_SETUP__INT_MODE_VAL_EDGE);
    GXIO_VERIFY_ZERO(result, "gxio_trio_enable_mmi()");

    //
    // Reserve 64-byte for memory map interrupt registers.
    //
    tile_mem_offset = MEM_MAP_REGISTER_SIZE;

    resource->mmi_funcs[mem_map_index] = mmi_func;
  }

  //
  // Record all the allocated resources.
  //
  resource->map_mem_regions[mem_map_index] = mem_map_index;
  resource->map_mems[mem_map_index] = 
    (void*)((intptr_t)tile_mem + tile_mem_offset);

  return 0;
}

int
streamio_init_push_dma(streamio_context_t *context, unsigned int dma_ring_index,
                       tmc_ipi_func_t push_dma_comp_func)
{
  streamio_resource_t *resource = &context->resource;
  gxio_trio_context_t *trio_context = context->trio_context;
  int asid = context->resource.asid;
  int result;
  unsigned int mac = context->mac;

  //
  // Error checkings.
  //
  if (dma_ring_index >= TRIO_NUM_PUSH_DMA_RINGS)
    return STREAMIO_EINVAL;

  if (resource->push_dma_rings[dma_ring_index] != STREAMIO_RESOURCE_EMPTY)
    return STREAMIO_EOVWR;

  //
  // Allocate and initialize a push DMA ring.
  //
  dma_ring_index = gxio_trio_alloc_push_dma_ring(trio_context, 1,
                                                 dma_ring_index,
                                                 HV_TRIO_ALLOC_FIXED);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring_index, "gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  void* push_ring_mem = resource->push_dma_mems[dma_ring_index];
  size_t push_ring_size = 
    STREAMIO_PUSH_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  gxio_trio_dma_queue_t* push_dma_queue =
    &resource->push_dma_queues[dma_ring_index];
  result = gxio_trio_init_push_dma_queue(push_dma_queue, trio_context,
                                         dma_ring_index, mac, asid, 0,
                                         push_ring_mem, push_ring_size, 0);
  GXIO_VERIFY_ZERO(result, "gxio_trio_init_push_dma_queue()");

  //
  // Init the push dma completion interrupt according to push_dma_comp_func.
  //
  if (push_dma_comp_func != NULL)
  {
    //
    // Enable push dma completion interrupt.
    //
    result = gxio_trio_enable_push_dma_isr(trio_context, 
                                           tmc_cpus_get_my_cpu(),
                                           IPI_EVENT_PUSH_DMA_COMP,
                                           dma_ring_index);
    GXIO_VERIFY_ZERO(result, "gxio_trio_enable_push_dma_isr()");

    resource->push_dma_comp_funcs[dma_ring_index] = push_dma_comp_func;
  }

  //
  // Record all the allocated resources.
  //
  resource->push_dma_rings[dma_ring_index] = dma_ring_index;

  return 0;
}

int 
streamio_init_pull_dma(streamio_context_t *context, unsigned int dma_ring_index,
                       tmc_ipi_func_t pull_dma_comp_func)
{
  streamio_resource_t *resource = &context->resource;
  gxio_trio_context_t *trio_context = context->trio_context;
  int asid = context->resource.asid;
  int result;
  unsigned int mac = context->mac;

  //
  // Error checkings.
  //
  if (dma_ring_index >= TRIO_NUM_PULL_DMA_RINGS)
    return STREAMIO_EINVAL;
  
  if (resource->pull_dma_rings[dma_ring_index] != STREAMIO_RESOURCE_EMPTY)
    return STREAMIO_EOVWR; 

  //
  // Allocate and initialize a pull DMA ring.
  //
  dma_ring_index = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 
                                                 dma_ring_index,
                                                 HV_TRIO_ALLOC_FIXED);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring_index, "gxio_trio_alloc_pull_dma_ring()");

  // 
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  void* pull_ring_mem = resource->pull_dma_mems[dma_ring_index];
  size_t pull_ring_size = 
    STREAMIO_PULL_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  gxio_trio_dma_queue_t* pull_dma_queue = 
    &resource->pull_dma_queues[dma_ring_index];
  result = gxio_trio_init_pull_dma_queue(pull_dma_queue, trio_context, 
                                         dma_ring_index, mac, asid, 0, 
                                         pull_ring_mem, pull_ring_size, 0);
  GXIO_VERIFY_ZERO(result, "gxio_trio_init_pull_dma_queue()");

  //
  // Init the pull dma completion interrupt according to pull_dma_comp_func.
  //
  if (pull_dma_comp_func != NULL)
  {
    //
    // Enable pull dma completion interrupt.
    //
    result = gxio_trio_enable_pull_dma_isr(trio_context, 
                                           tmc_cpus_get_my_cpu(),
                                           IPI_EVENT_PULL_DMA_COMP,
                                           dma_ring_index);
    GXIO_VERIFY_ZERO(result, "gxio_trio_enable_pull_dma_isr()");

    resource->pull_dma_comp_funcs[dma_ring_index] = pull_dma_comp_func;
  }

  //
  // Record all the allocated resources.
  //
  resource->pull_dma_rings[dma_ring_index] = dma_ring_index;

  return 0;
}

int
streamio_dma_write(streamio_context_t *context, unsigned int dma_ring_index,
                   void* src_buf, uint32_t size, uint64_t streamio_bus_addr, 
                   unsigned int is_notif)
{
  streamio_resource_t *resource = &context->resource;
  int slot_needed;
  int64_t slot;
  uint64_t bus_offset = 0;

  //
  // Error checkings.
  //
  if (dma_ring_index >= TRIO_NUM_PUSH_DMA_RINGS)
    return STREAMIO_EINVAL;
  
  if (resource->push_dma_rings[dma_ring_index] == STREAMIO_RESOURCE_EMPTY)
    return STREAMIO_EINVAL;

  //
  // Note that this src_buf should be registered to IOTLB by user.
  //
  if (src_buf == NULL)
    return STREAMIO_EINVAL;

  //
  // Calculate the number of DMA slots we need and try to reserve them.
  //  
  slot_needed = (size + MAX_DMA_SIZE - 1) / MAX_DMA_SIZE;
  if (slot_needed == 0)
    return 0;  

  gxio_trio_dma_queue_t *dma_queue = &resource->push_dma_queues[dma_ring_index];

  slot = gxio_trio_dma_queue_reserve(dma_queue, slot_needed);
  if (slot == GXIO_ERR_DMA_CREDITS)
    return STREAMIO_ECREDITS;
  
  //
  // Send out DMA commands.
  // 
  for (int i = 0; i < slot_needed; i++)
  {
    uint32_t data_bytes = MIN(size, MAX_DMA_SIZE);
     
    //
    // A default descriptor for 16KB data.
    // 
    gxio_trio_dma_desc_t desc = {{
        .va = (intptr_t)src_buf + bus_offset,
        .xsize = 0,
        .io_address = streamio_bus_addr + bus_offset,
        .smod = 1,
      }};
    
    if (data_bytes < MAX_DMA_SIZE)
    { 
      desc.smod = 0;
      desc.xsize = data_bytes;
    }
    
    if (i == slot_needed - 1)
      desc.notif = (is_notif > 0) ? 1: 0;
   
    __insn_mf();

    gxio_trio_dma_queue_put_at(dma_queue, desc, slot++);
    gxio_trio_dma_queue_flush(dma_queue);
    size -= data_bytes;
    bus_offset += data_bytes; 
  }

  return 0;
}

int
streamio_dma_read(streamio_context_t *context, unsigned int dma_ring_index,
                  void* dest_buf, uint32_t size, uint64_t streamio_bus_addr,
                  unsigned int is_notif)
{
  streamio_resource_t *resource = &context->resource;
  int slot_needed;
  int64_t slot;
  uint64_t bus_offset = 0;

  //
  // Error checkings.
  //
  if (dma_ring_index >= TRIO_NUM_PULL_DMA_RINGS)
    return STREAMIO_EINVAL;

  if (resource->pull_dma_rings[dma_ring_index] == STREAMIO_RESOURCE_EMPTY)
    return STREAMIO_EINVAL;

  //
  // Note that this dest_buf should be registered to IOTLB by user.
  //
  if (dest_buf == NULL)
    return STREAMIO_EINVAL;

  //
  // Calculate the number of DMA slots we need and try to reserve them.
  //
  slot_needed = (size + MAX_DMA_SIZE - 1) / MAX_DMA_SIZE;
  if (slot_needed == 0)
    return 0;

  gxio_trio_dma_queue_t *dma_queue = &resource->pull_dma_queues[dma_ring_index];

  slot = gxio_trio_dma_queue_reserve(dma_queue, slot_needed);
  if (slot == GXIO_ERR_DMA_CREDITS)
    return STREAMIO_ECREDITS;

  //
  // Send out DMA commands.
  //
  for (int i = 0; i < slot_needed; i++)
  {
    uint32_t data_bytes = MIN(size, MAX_DMA_SIZE);

    //
    // A default descriptor for 16KB data.
    // 
    gxio_trio_dma_desc_t desc = {{
        .va = (intptr_t)dest_buf + bus_offset,
        .xsize = 0,
        .io_address = streamio_bus_addr + bus_offset,
        .smod = 1,
      }};
    
    if (data_bytes < MAX_DMA_SIZE)
    {
      desc.smod = 0;
      desc.xsize = data_bytes;
    }
    
    if (i == slot_needed - 1)
      desc.notif = (is_notif > 0) ? 1: 0;

    __insn_mf();

    gxio_trio_dma_queue_put_at(dma_queue, desc, slot++);
    gxio_trio_dma_queue_flush(dma_queue);
    size -= data_bytes;
    bus_offset += data_bytes;
  }

  return 0;
}

