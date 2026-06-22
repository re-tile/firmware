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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <gxio/common.h>
#include <tmc/cpus.h>
#include <tmc/alloc.h>
#include <tmc/task.h>

#include <gxpci/gxpci.h>

#include <gxpci/host_packet_queue.h>

/**
 * This file implements a simplified version of the generic tile/host data
 * transfer engine, supporting the packet queue interfaces on the host.
 */

#if 0
#define DEBUG_T2H
#define DEBUG_H2T
#define GXPCI_TRACE(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define GXPCI_TRACE(...)
#endif

/** 
 * This define enables checking of the command size as well as a queue-status
 * check on each command that is added.  Additionally, it enables a credit check
 * on each command.  Enabling this degrades performance so it should only be
 * used for development purposes.
 */
#if 0
#define ENABLE_CMD_SIZE_QSTS_CRED_CHECKS
#endif

#define PQ_PUSH_RING_GEN_BIT GXPCI_HOST_PQ_PUSH_DMA_RING_ORD 
#define PQ_PUSH_RING_MASK (GXPCI_HOST_PQ_PUSH_DMA_RING_LEN - 1)
#define PQ_PULL_RING_GEN_BIT GXPCI_HOST_PQ_PULL_DMA_RING_ORD 
#define PQ_PULL_RING_MASK (GXPCI_HOST_PQ_PULL_DMA_RING_LEN - 1)


/**
 * Update the producer_index based on how many push DMAs have completed
 * and return the number of credits available.
 */
uint32_t gxpci_pq_t2h_update_counters(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state;
  struct gxpci_host_pq_regs_app *app_regs;
  gxio_trio_dma_queue_t *dma_queue_data;
  uint32_t producer_index;
  uint16_t hw_cnt;
  int credits;

  app_regs = resource->host_pq.app_regs;
  queue_state = resource->host_pq.queue_state;
  dma_queue_data = &resource->host_pq.dma_queue_data;

  /** Check the Push DMA complete count (MMIO read). */
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue_data);

  /*
   * Credit is based on how much we've sent to the local ring and how much 
   * has been completely processed, including returning completions to the user.
   * The local ring pointer is just the low bits of the credits_and_next_index 
   * member of the dma queue.
   */
  credits = queue_state->cred_total -
    (((dma_queue_data->dma_queue.credits_and_next_index &
       queue_state->cred_mask) -
      (context->completed & queue_state->cred_mask)) &
     queue_state->cred_mask);

  /*
   * Update the write_ptr (read by host). Bump upper 16 bits if hw_cnt has
   * wrapped.
   */
  if (hw_cnt <
      (app_regs->producer_index & TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK))
  {
    producer_index =
      (app_regs->producer_index & ~TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK) +
      (TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK + 1) + hw_cnt;
  }
  else 
  {
    producer_index =
      (app_regs->producer_index & ~TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK) +
      hw_cnt;
  }

  context->credits = credits;

  app_regs->producer_index = producer_index;

  return credits;
}

/**
 * Update the "completed" count based on MMIO read of pull DMA completions.
 * This value is 16 bits so comparisons with producer/consumer must be masked.
 */
uint32_t gxpci_pq_h2t_update_counters(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue_data;
  dma_queue_data = &resource->host_pq.dma_queue_data;

  /** Check the Pull DMA complete count (MMIO read). */
  context->completed = gxio_trio_read_dma_queue_complete_count(dma_queue_data);

  return context->completed;
}

/**
 * Initialize the queue state.
 */
static void
gxpci_packet_queue_state_init(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state;
  int bufs_per_segment;
  int segment_index;
  int buf_index;
  int i;

  queue_state = resource->host_pq.queue_state;

  queue_state->num_segments = resource->host_pq.drv_regs->num_segments;
  assert(queue_state->num_segments > 0);

  queue_state->segment_size =
    resource->host_pq.drv_regs->segment_size;

  for (i = 0; i < queue_state->num_segments; i++)
    queue_state->segment_bus_addr[i] =
      resource->host_pq.drv_regs->segment_bus_addr[i];

  queue_state->buf_size = resource->host_pq.drv_regs->buf_size;
  queue_state->buf_size_order = __insn_ctz(queue_state->buf_size);

  /*
   * This is the largest buf_size we can support before the descriptors 
   * jump to powers-of 2 xsize.
   */
  assert(queue_state->buf_size <= (1 << TRIO_DMA_DESC_WORD0__XSIZE_WIDTH));

  /* The number of packet buffers inside a single PA-contiguous segment. */
  bufs_per_segment = queue_state->segment_size / queue_state->buf_size;

  if ((context->type == GXPCI_PQ_T2H) || (context->type == GXPCI_PQ_T2H_VF))
  {
#ifdef DEBUG_T2H
    GXPCI_TRACE("Initializing PUSH DMA ring\n");
#endif
    /*
     * Init the push DMA ring directly with buffer addresses.
     * But leave them invalid.  TileVA, xsize, and Gen filled in
     * later when we send a buffer.
     */
    for (i = 0; i < GXPCI_HOST_PQ_PUSH_DMA_RING_LEN; i++) 
    {
      gxio_trio_dma_desc_t desc = {{
          .va = (intptr_t)0,
          .bsz = 0,
          .c = 0,
          .notif = 0,
          .smod = 0,
          .xsize = 0,
          .gen = 0,
        }};

      segment_index = (i / bufs_per_segment) % queue_state->num_segments;
      buf_index = i % bufs_per_segment;
      desc.io_address = queue_state->segment_bus_addr[segment_index] +
			(buf_index << queue_state->buf_size_order);

      gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
      gxio_trio_dma_desc_t *desc_p = &dma_queue->dma_descs[i];
      desc_p->words[1] = desc.words[1];
      desc_p->words[0] = desc.words[0];
    }
    
    context->credits =
      GXPCI_HOST_PQ_SEGMENT_ENTRIES * queue_state->num_segments;

    /*
     * Push DMA ring must be at least as large as host ring for the
     * T2H flow control model to work.
     */
    assert(GXPCI_HOST_PQ_PUSH_DMA_RING_LEN >=
           GXPCI_HOST_PQ_SEGMENT_ENTRIES * queue_state->num_segments);
  }
  else 
  {
#ifdef DEBUG_H2T
    GXPCI_TRACE("Initializing PULL DMA ring\n");
#endif
    /*
     * Init the pull DMA ring directly with buffer addresses.
     * But leave them invalid.  TileVA and Gen filled in later
     * when we post buffers.
     */
    for (i = 0; i < GXPCI_HOST_PQ_PULL_DMA_RING_LEN; i++) 
    {
      gxio_trio_dma_desc_t desc = {{
          .va = (intptr_t)0,
          .bsz = 0,
          .c = 0,
          .notif = 0,
          .smod = (queue_state->buf_size > 0x3fff),
          .xsize = (queue_state->buf_size & 0x3fff),
          .gen = 0,
        }};

      segment_index = (i / bufs_per_segment) % queue_state->num_segments;
      buf_index = i % bufs_per_segment;
      desc.io_address = queue_state->segment_bus_addr[segment_index] +
			(buf_index << queue_state->buf_size_order);

      gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
      gxio_trio_dma_desc_t *desc_p = &dma_queue->dma_descs[i];
      desc_p->words[1] = desc.words[1];
      desc_p->words[0] = desc.words[0];
    }
    
    context->credits = 0;

    /*
     * Pull DMA ring must be at least as large as host ring for the
     * H2T flow control model to work.
     */
    assert(GXPCI_HOST_PQ_PULL_DMA_RING_LEN >=
           GXPCI_HOST_PQ_SEGMENT_ENTRIES * queue_state->num_segments);
  }

  queue_state->cred_mask =
    (GXPCI_HOST_PQ_SEGMENT_ENTRIES * queue_state->num_segments << 1) - 1;
  queue_state->cred_total =
    GXPCI_HOST_PQ_SEGMENT_ENTRIES * queue_state->num_segments;

  return;
}

extern int 
get_bar_addr(gxpci_context_t *context, int local,
             tilegxpci_bar_info_t *bar_info);
extern int 
get_vf_bar_addr(gxpci_context_t *context, tilegxpci_bar_info_t *bar_info);

extern int
gxpci_alloc_mapping_region(gxio_trio_context_t *trio_context, void *target_mem,
                           size_t target_size, unsigned int asid,
                           unsigned int mac, uint64_t pci_address);

/**
 * Get the packet queue interface info.
 */
int
gxpci_pq_get_info(gxpci_context_t *context)
{
  tilegxpci_get_pq_queue_msg_t pq_queue_msg;
  char device_name[40];
  int request;
  int ret;
  int fd;

  if ((context->type == GXPCI_PQ_H2T_VF) || (context->type == GXPCI_PQ_T2H_VF))
  {
    request = TILEPCI_IOC_VF_GET_PQ_QUEUE_MSG;
    snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
             context->trio_index, context->mac);
  }
  else
  {
    request = TILEPCI_IOC_GET_PQ_QUEUE_MSG;
    snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem",
             context->trio_index, context->mac);
  }

  fd = open(device_name, O_RDWR);
  if (fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  pq_queue_msg.trio_index = context->trio_index;
  pq_queue_msg.mac_index = context->mac;

  ret = ioctl(fd, request, &pq_queue_msg);
  if (ret < 0)
  {
    fprintf(stderr, "%s GET_PQ_QUEUE_MSG ioctl failure: %s\n", device_name,
            strerror(errno));
    ret = -errno;
  }
  context->resource.host_pq.num_pq_h2t_queues = pq_queue_msg.num_h2t_queues;
  context->resource.host_pq.num_pq_t2h_queues = pq_queue_msg.num_t2h_queues;

  close(fd);

  return ret;
}

int
gxpci_open_pq_h2t_queue(gxpci_context_t *context)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  int asid = context->resource.asid;
  unsigned int mac = context->mac;
  tilegxpci_bar_info_t bar_info;
  unsigned long long local_bar;
  unsigned long long local_pci_addr;
  void *backing_mem;
  int dma_ring;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  int err;

#ifdef DEBUG_H2T
  GXPCI_TRACE("Openning H2T Queue\n");
#endif

  //
  // Retrieve the packet queue interface info.
  //
  err = gxpci_pq_get_info(context);
  if (err < 0)
    return -err;

  if (context->queue_index >= context->resource.host_pq.num_pq_h2t_queues)
    return GXPCI_EINVAL;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq/h2t/%d",
           context->trio_index, context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  // 
  // Retrieve the local BAR0 addresses.
  //
  err = get_bar_addr(context, 1, &bar_info);
  if (err < 0)
    return err;

  local_bar = bar_info.bar_addr;

  //
  // Allocate and bind a huge page which is used for both backing the PCI
  // host MMIO registers and serving the DMA operations. We use the
  // top of the page to back the PCI space and the end of it for DMA.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_TASK);
  backing_mem = tmc_alloc_map(&alloc, GXPCI_HOST_PQ_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_HOST_PQ_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  resource->backing_mem = backing_mem;

  resource->host_pq.queue_state = (struct gxpci_packet_queue_state *)
                                  (resource->backing_mem +
                                  GXPCI_HOST_PQ_STATE_STRUCT_OFFSET);

  //
  // This is the PCI address that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct. Note that the queue_index is the index
  // to one of the GXPCI_HOST_PQ_H2T_COUNT queues.
  //
  local_pci_addr = local_bar + GXPCI_HOST_PQ_H2T_REGS_OFFSET +
                   context->queue_index * (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                   GXPCI_HOST_PQ_REGS_APP_MAP_SIZE) * 2;

  resource->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
                        (backing_mem + GXPCI_HOST_PQ_REGS_DRV_OFFSET);
  resource->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
                        (backing_mem + GXPCI_HOST_PQ_REGS_APP_OFFSET);

  //
  // Allocate and initialize a memory mapping region.
  //
  err = gxpci_alloc_mapping_region(trio_context,
                                   (void *)resource->host_pq.drv_regs,
                                   GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                                   GXPCI_HOST_PQ_REGS_APP_MAP_SIZE,
                                   asid, mac, local_pci_addr);
  if (err < 0)
    return err;

  resource->host_pq.app_regs->consumer_index = 0;

  //
  // Allocate and initialize a pull DMA ring for the data.
  //
  dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PULL_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem + GXPCI_HOST_PQ_PULL_DMA_CMDS_BUF_OFFSET;
  dma_queue = &resource->host_pq.dma_queue_data;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring, mac,
                                      asid, 0, dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host application is ready and should have initialized the registers
  // in struct gxpci_host_pq_regs_drv that records the the PCI addresses and
  // size info of the host ring buffer.
  //

  //
  // Initialize some states and allocate some resource for the queue.
  //
  gxpci_packet_queue_state_init(context);

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  context->completed = 0;

  return 0;
}

int
gxpci_open_pq_t2h_queue(gxpci_context_t *context)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  int asid = context->resource.asid;
  unsigned int mac = context->mac;
  tilegxpci_bar_info_t bar_info;
  unsigned long long local_bar;
  unsigned long long local_pci_addr;
  void *backing_mem;
  int dma_ring;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  int err;

#ifdef DEBUG_T2H
  GXPCI_TRACE("Openning T2H Queue\n");
#endif

  //
  // Retrieve the packet queue interface info.
  //
  err = gxpci_pq_get_info(context);
  if (err < 0)
    return -err;

  if (context->queue_index >= context->resource.host_pq.num_pq_t2h_queues)
    return GXPCI_EINVAL;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq/t2h/%d",
           context->trio_index, context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  // 
  // Retrieve the local BAR0 addresses.
  //
  err = get_bar_addr(context, 1, &bar_info);
  if (err < 0)
    return err;

  local_bar = bar_info.bar_addr;

  //
  // Allocate and bind a huge page which is used for both backing the PCI
  // host MMIO registers and serving the DMA operations. We use the
  // top of the page to back the PCI space and the end of it for DMA.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  tmc_alloc_set_home(&alloc, TMC_ALLOC_HOME_TASK);
  backing_mem = tmc_alloc_map(&alloc, GXPCI_HOST_PQ_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_HOST_PQ_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  resource->backing_mem = backing_mem;

  resource->host_pq.queue_state = (struct gxpci_packet_queue_state *)
                                  (resource->backing_mem +
                                  GXPCI_HOST_PQ_STATE_STRUCT_OFFSET);

  //
  // This is the PCI address that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct. Note that the queue_index is the index
  // to one of the GXPCI_HOST_PQ_T2H_COUNT queues.
  //
  local_pci_addr = local_bar + GXPCI_HOST_PQ_H2T_REGS_OFFSET +
                   context->queue_index * (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                   GXPCI_HOST_PQ_REGS_APP_MAP_SIZE) * 2 +
                   (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                    GXPCI_HOST_PQ_REGS_APP_MAP_SIZE);

  resource->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
                        (backing_mem + GXPCI_HOST_PQ_REGS_DRV_OFFSET);
  resource->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
                        (backing_mem + GXPCI_HOST_PQ_REGS_APP_OFFSET);

  //
  // Allocate and initialize a memory mapping region.
  //
  err = gxpci_alloc_mapping_region(trio_context,
                                   (void *)resource->host_pq.drv_regs,
                                   GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                                   GXPCI_HOST_PQ_REGS_APP_MAP_SIZE,
                                   asid, mac, local_pci_addr);
  if (err < 0)
    return err;

  resource->host_pq.app_regs->producer_index = 0;

  //
  // Allocate and initialize a push DMA ring for tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PUSH_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
#if 0
  // example optimization - use a locally homed page for the ring.
  tmc_alloc_t dma_alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_home(&dma_alloc, TMC_ALLOC_HOME_TASK);
  dma_ring_mem = tmc_alloc_map(&dma_alloc, 65536);
  if (dma_ring_mem == NULL)
    return -errno;
#else
  dma_ring_mem = backing_mem + GXPCI_HOST_PQ_PUSH_DMA_CMDS_BUF_OFFSET;
#endif

  dma_queue = &resource->host_pq.dma_queue_data;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring, mac,
                                      asid, 0, dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host is ready and should have initialized the registers in
  // struct gxpci_host_pq_regs_drv that records the the PCI addresses and
  // size info of the host ring buffer.
  //

  //
  // Initialize some states and allocate some resource for the queue.
  //
  gxpci_packet_queue_state_init(context);

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  context->completed = 0;

  return 0;
}

int
gxpci_open_pq_duplex_queue(gxpci_context_t *h2t_context,
                           gxpci_context_t *t2h_context)
{
  gxio_trio_context_t *trio_context = h2t_context->trio_context;
  gxpci_resource_t *h2t_res = &h2t_context->resource;
  gxpci_resource_t *t2h_res = &t2h_context->resource;
  tilegxpci_bar_info_t bar_info;
  unsigned int mac = h2t_context->mac;
  int asid = h2t_res->asid;
  char device_name[40];

#if defined(DEBUG_H2T) && defined(DEBUG_T2H)
  GXPCI_TRACE("Openning Duplex Packet Queue\n");
#endif

  //
  // Retrieve the packet queue interface info, for both H2T and T2H directions.
  //
  int err = gxpci_pq_get_info(h2t_context);
  if (err < 0)
    return err;

  err = gxpci_pq_get_info(t2h_context);
  if (err < 0)
    return err;

  //
  // Get the file handle for queue status monitoring, for both H2T and
  // T2H directions.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq/h2t/%d",
           h2t_context->trio_index, h2t_context->mac, h2t_context->queue_index);
  h2t_context->fd = open(device_name, O_RDWR);
  if (h2t_context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  } 

  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq/t2h/%d",
           t2h_context->trio_index, t2h_context->mac, t2h_context->queue_index);
  t2h_context->fd = open(device_name, O_RDWR);
  if (t2h_context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  //
  // Retrieve the local BAR0 address.
  //
  err = get_bar_addr(h2t_context, 1, &bar_info);
  if (err < 0)
    return err;

  unsigned long long local_bar = bar_info.bar_addr;

  //
  // Allocate and bind a huge page which is used for both backing the PCI
  // host MMIO registers and serving the DMA operations. We use the
  // top of the page to back the PCI space and the end of it for DMA.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  void *backing_mem = tmc_alloc_map(&alloc, GXPCI_HOST_PQ_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_HOST_PQ_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  //
  // This is the PCI address that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct.
  //
  unsigned long long local_pci_addr =
    local_bar + GXPCI_HOST_PQ_H2T_REGS_OFFSET +
    h2t_context->queue_index * (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
    GXPCI_HOST_PQ_REGS_APP_MAP_SIZE) * 2;

  //
  // Allocate and initialize a memory mapping region.
  //
  err = gxpci_alloc_mapping_region(trio_context, backing_mem,
                                   (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE +
                                   GXPCI_HOST_PQ_REGS_APP_MAP_SIZE) * 2,
                                   asid, mac, local_pci_addr);
  if (err < 0)
    return err;

  //
  // Initialize H2T packet queue registers.
  //
  h2t_res->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
    (backing_mem + GXPCI_HOST_PQ_REGS_DRV_OFFSET);
  h2t_res->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
    (backing_mem + GXPCI_HOST_PQ_REGS_APP_OFFSET);
  h2t_res->host_pq.app_regs->consumer_index = 0;

  //
  // Initialize T2H packet queue registers.
  //
  t2h_res->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
    (backing_mem + GXPCI_HOST_PQ_REGS_DRV_OFFSET +
    (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE + GXPCI_HOST_PQ_REGS_APP_MAP_SIZE));
  t2h_res->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
    (backing_mem + GXPCI_HOST_PQ_REGS_APP_OFFSET +
    (GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE + GXPCI_HOST_PQ_REGS_APP_MAP_SIZE));
  t2h_res->host_pq.app_regs->producer_index = 0;

  //
  // H2T packet queue: Allocate and initialize a pull DMA ring for the data.
  //
  int dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  size_t dma_ring_size = GXPCI_HOST_PQ_PULL_DMA_RING_LEN *
                         sizeof(gxio_trio_dma_desc_t);
  void *dma_ring_mem = backing_mem + GXPCI_HOST_PQ_PULL_DMA_CMDS_BUF_OFFSET;
  gxio_trio_dma_queue_t *dma_queue = &h2t_res->host_pq.dma_queue_data;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring,
                                      mac, asid, 0, dma_ring_mem,
                                      dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  //
  // T2H packet queue: Allocate and initialize a push DMA ring for
  // tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PUSH_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem + GXPCI_HOST_PQ_PUSH_DMA_CMDS_BUF_OFFSET;

  dma_queue = &t2h_res->host_pq.dma_queue_data;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring,
                                      mac, asid, 0, dma_ring_mem,
                                      dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  backing_mem += GXPCI_HOST_PQ_BACK_MEM_SIZE / 2;

  h2t_res->backing_mem = backing_mem;
  t2h_res->backing_mem = backing_mem + GXPCI_HOST_PQ_PUSH_DMA_CMDS_BUF_OFFSET;

  h2t_res->host_pq.queue_state = (struct gxpci_packet_queue_state *)
                             (h2t_res->backing_mem +
                             GXPCI_HOST_PQ_STATE_STRUCT_OFFSET);
  t2h_res->host_pq.queue_state = (struct gxpci_packet_queue_state *)
                             (t2h_res->backing_mem +
                             GXPCI_HOST_PQ_STATE_STRUCT_OFFSET);

  //
  // Handshake for H2T packet queue:
  //
  volatile uint32_t *loc_ready = &h2t_res->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Handshake for T2H packet queue:
  //
  loc_ready = &t2h_res->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Initialize some states and allocate some resource for H2T and T2H packet
  // queues separately.
  //
  gxpci_packet_queue_state_init(h2t_context);
  gxpci_packet_queue_state_init(t2h_context);

  //
  // Activate the H2T packet queue.
  //
  err = ioctl(h2t_context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  h2t_context->completed = 0;

  //
  // Activate the T2H packet queue.
  //
  err = ioctl(t2h_context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  t2h_context->completed = 0;

  return 0;
}

int
gxpci_open_pq_duplex_queue_vf(gxpci_context_t *h2t_context,
                              gxpci_context_t *t2h_context,
                              int asid,
                              struct gxpci_packet_queue_state *h2t_queue_state,
                              struct gxpci_packet_queue_state *t2h_queue_state,
                              unsigned int vf,
                              unsigned int queue_index,
                              void *h2t_backing_mem,
                              void *t2h_backing_mem)
{
  gxio_trio_context_t *trio_context = h2t_context->trio_context;
  gxpci_resource_t *h2t_res = &h2t_context->resource;
  gxpci_resource_t *t2h_res = &t2h_context->resource;
  unsigned int trio_index = h2t_context->trio_index;
  unsigned int mac = h2t_context->mac;
  tilegxpci_bar_info_t bar_info;
  unsigned long bar0_offset;
  unsigned long bar0_size;
  char device_name[40];
  void *bar_mem; 
  int barmem_fd;
  int err;

  h2t_context->type = GXPCI_PQ_H2T_VF;
  t2h_context->type = GXPCI_PQ_T2H_VF;
  h2t_context->resource.asid = asid;
  t2h_context->resource.asid = asid;
  h2t_context->queue_index = queue_index;
  t2h_context->queue_index = queue_index;
  h2t_res->backing_mem = h2t_backing_mem;
  t2h_res->backing_mem = t2h_backing_mem;
  h2t_res->host_pq.queue_state = h2t_queue_state;
  t2h_res->host_pq.queue_state = t2h_queue_state;

  //
  // Retrieve the packet queue interface info, for both H2T and T2H directions.
  //
  err = gxpci_pq_get_info(h2t_context);
  if (err < 0)
    return err;

  err = gxpci_pq_get_info(t2h_context);
  if (err < 0)
    return err;

  if (h2t_context->queue_index >=
      h2t_context->resource.host_pq.num_pq_h2t_queues ||
      t2h_context->queue_index >=
      t2h_context->resource.host_pq.num_pq_t2h_queues)
    return GXPCI_EINVAL;

  //
  // Get the file handle for queue status monitoring, for both H2T and
  // T2H directions.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq_vf/h2t/vf_%d/%d",
           trio_index, mac, vf, h2t_context->queue_index);
  h2t_context->fd = open(device_name, O_RDWR);
  if (h2t_context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  } 

  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq_vf/t2h/vf_%d/%d",
           trio_index, mac, vf, t2h_context->queue_index);
  t2h_context->fd = open(device_name, O_RDWR);
  if (t2h_context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  // 
  // Retrieve the VF's local BAR0 addresses.
  //
  bar_info.link_index = vf;
  bar_info.bar_index = 0;
  err = get_vf_bar_addr(h2t_context, &bar_info);
  if (err < 0)
    return err;

  bar0_size = bar_info.bar_size;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
           trio_index, mac);

  barmem_fd = open(device_name, O_RDWR);
  if (barmem_fd < 0)
  {
    fprintf(stderr, "VF %d: can't open %s: %s\n",
            vf, device_name, strerror(errno));
    return -errno;
  }

  //
  // This is the BAR0 window that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct for the H2T queue.
  //
  bar0_offset = vf * bar0_size +
                GXPCI_VF_HOST_PQ_H2T_REGS_OFFSET +
                h2t_context->queue_index * (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE) * 2;

  h2t_queue_state->barmem_size = GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                                 GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE;
  bar_mem = mmap(NULL,
                 h2t_queue_state->barmem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 barmem_fd,
                 bar0_offset);
  if (bar_mem == MAP_FAILED)
    return -errno;

  //
  // Initialize H2T packet queue registers.
  //
  h2t_res->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
    (bar_mem + GXPCI_VF_HOST_PQ_REGS_DRV_OFFSET);
  h2t_res->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
    (bar_mem + GXPCI_VF_HOST_PQ_REGS_APP_OFFSET);
  h2t_res->host_pq.app_regs->consumer_index = 0;

  h2t_queue_state->barmem_fd = barmem_fd;
  h2t_queue_state->bar_mem = bar_mem;

  //
  // This is the BAR0 window that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct for the T2H queue.
  //
  bar0_offset = vf * bar0_size +
                GXPCI_VF_HOST_PQ_H2T_REGS_OFFSET +
                t2h_context->queue_index * (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE) * 2 +
                (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE);

  t2h_queue_state->barmem_size = GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                                 GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE;
  bar_mem = mmap(NULL,
                 t2h_queue_state->barmem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 barmem_fd,
                 bar0_offset);
  if (bar_mem == MAP_FAILED)
    return -errno;

  //
  // Initialize T2H packet queue registers.
  //
  t2h_res->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
    (bar_mem + GXPCI_VF_HOST_PQ_REGS_DRV_OFFSET);
  t2h_res->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
    (bar_mem + GXPCI_VF_HOST_PQ_REGS_APP_OFFSET);
  t2h_res->host_pq.app_regs->producer_index = 0;

  t2h_queue_state->barmem_fd = barmem_fd;
  t2h_queue_state->bar_mem = bar_mem;

  //
  // H2T packet queue: Allocate and initialize a pull DMA ring for the data.
  //
  int dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  size_t dma_ring_size = GXPCI_HOST_PQ_PULL_DMA_RING_LEN *
                         sizeof(gxio_trio_dma_desc_t);
  void *dma_ring_mem = h2t_backing_mem;
  gxio_trio_dma_queue_t *dma_queue = &h2t_res->host_pq.dma_queue_data;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring,
                                      mac, asid, HV_TRIO_FLAG_VFUNC(vf),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  //
  // T2H packet queue: Allocate and initialize a push DMA ring for
  // tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PUSH_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = t2h_backing_mem;
  dma_queue = &t2h_res->host_pq.dma_queue_data;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring,
                                      mac, asid, HV_TRIO_FLAG_VFUNC(vf),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  //
  // Handshake for H2T packet queue:
  //
  volatile uint32_t *loc_ready = &h2t_res->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Handshake for T2H packet queue:
  //
  loc_ready = &t2h_res->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Initialize some states and allocate some resource for H2T and T2H packet
  // queues separately.
  //
  gxpci_packet_queue_state_init(h2t_context);
  gxpci_packet_queue_state_init(t2h_context);

  //
  // Activate the H2T packet queue.
  //
  err = ioctl(h2t_context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE,
              h2t_res->host_pq.drv_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  h2t_context->completed = 0;

  //
  // Activate the T2H packet queue.
  //
  err = ioctl(t2h_context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE,
              t2h_res->host_pq.drv_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  t2h_context->completed = 0;

  return 0;
}

int
gxpci_open_pq_h2t_queue_vf(gxpci_context_t *context,
                           int asid,
                           struct gxpci_packet_queue_state *queue_state,
                           unsigned int vf,
                           unsigned int queue_index,
                           void *backing_mem)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  unsigned int mac = context->mac;
  tilegxpci_bar_info_t bar_info;
  unsigned long bar0_offset;
  unsigned long bar0_size;
  int dma_ring;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  void *bar_mem; 
  int barmem_fd;
  int err;

  context->type = GXPCI_PQ_H2T_VF;
  context->resource.asid = asid;
  context->queue_index = queue_index;
  resource->backing_mem = backing_mem;
  resource->host_pq.queue_state = queue_state;

  //
  // Retrieve the packet queue interface info.
  //
  err = gxpci_pq_get_info(context);
  if (err < 0)
    return -err;

  if (context->queue_index >= context->resource.host_pq.num_pq_h2t_queues)
    return GXPCI_EINVAL;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq_vf/h2t/vf_%d/%d", context->trio_index,
           context->mac, vf, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  // 
  // Retrieve the VF's local BAR0 addresses.
  //
  bar_info.link_index = vf;
  bar_info.bar_index = 0;
  err = get_vf_bar_addr(context, &bar_info);
  if (err < 0)
    return err;

  bar0_size = bar_info.bar_size;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
           context->trio_index, context->mac);

  barmem_fd = open(device_name, O_RDWR);
  if (barmem_fd < 0)
  {
    fprintf(stderr, "VF %d: can't open %s: %s\n",
            vf, device_name, strerror(errno));
    return -errno;
  }

  //
  // This is the BAR0 window that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct. Note that the queue_index is the index
  // to one of the GXPCI_HOST_PQ_H2T_COUNT queues.
  //
  bar0_offset = vf * bar0_size +
                GXPCI_VF_HOST_PQ_H2T_REGS_OFFSET +
                context->queue_index * (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE) * 2;

  queue_state->barmem_size = GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                             GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE;
  bar_mem = mmap(NULL,
                 queue_state->barmem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 barmem_fd,
                 bar0_offset);
  if (bar_mem == MAP_FAILED)
    return -errno;

  resource->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
                        (bar_mem + GXPCI_VF_HOST_PQ_REGS_DRV_OFFSET);
  resource->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
                        (bar_mem + GXPCI_VF_HOST_PQ_REGS_APP_OFFSET);

  queue_state->barmem_fd = barmem_fd;
  queue_state->bar_mem = bar_mem;

  //
  // Allocate and initialize a pull DMA ring for the data.
  //
  dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PULL_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem;
  dma_queue = &resource->host_pq.dma_queue_data;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring, mac,
                                      asid, HV_TRIO_FLAG_VFUNC(vf),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host application is ready and should have initialized the registers
  // in struct gxpci_host_pq_regs_drv that records the the PCI addresses and
  // size info of the host ring buffer.
  //

  //
  // Initialize some states and allocate some resource for the queue.
  //
  gxpci_packet_queue_state_init(context);

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE,
              resource->host_pq.drv_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  context->completed = 0;

  return 0;
}

int
gxpci_open_pq_t2h_queue_vf(gxpci_context_t *context,
                           int asid,
                           struct gxpci_packet_queue_state *queue_state,
                           unsigned int vf,
                           unsigned int queue_index,
                           void *backing_mem)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  unsigned int mac = context->mac;
  tilegxpci_bar_info_t bar_info;
  unsigned long bar0_offset;
  unsigned long bar0_size;
  int dma_ring;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  void *bar_mem; 
  int barmem_fd;
  int err;

  context->type = GXPCI_PQ_T2H_VF;
  context->resource.asid = asid;
  context->queue_index = queue_index;
  resource->backing_mem = backing_mem;
  resource->host_pq.queue_state = queue_state;

  //
  // Retrieve the packet queue interface info.
  //
  err = gxpci_pq_get_info(context);
  if (err < 0)
    return -err;

  if (context->queue_index >= context->resource.host_pq.num_pq_t2h_queues)
    return GXPCI_EINVAL;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_pq_vf/t2h/vf_%d/%d", context->trio_index,
           context->mac, vf, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  // 
  // Retrieve the VF's local BAR0 addresses.
  //
  bar_info.link_index = vf;
  bar_info.bar_index = 0;
  err = get_vf_bar_addr(context, &bar_info);
  if (err < 0)
    return err;

  bar0_size = bar_info.bar_size;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
           context->trio_index, context->mac);

  barmem_fd = open(device_name, O_RDWR);
  if (barmem_fd < 0)
  {
    fprintf(stderr, "VF %d: can't open %s: %s\n",
            vf, device_name, strerror(errno));
    return -errno;
  }

  //
  // This is the BAR0 window that is mapped to the gxpci_host_pq_regs_drv and
  // the gxpci_host_pq_regs_app struct. Note that the queue_index is the index
  // to one of the GXPCI_HOST_PQ_T2H_COUNT queues.
  //
  bar0_offset = vf * bar0_size +
                GXPCI_VF_HOST_PQ_H2T_REGS_OFFSET +
                context->queue_index * (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE) * 2 +
                (GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE);

  queue_state->barmem_size = GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE +
                             GXPCI_VF_HOST_PQ_REGS_APP_MAP_SIZE;
  bar_mem = mmap(NULL,
                 queue_state->barmem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 barmem_fd,
                 bar0_offset);
  if (bar_mem == MAP_FAILED)
    return -errno;

  resource->host_pq.drv_regs = (struct gxpci_host_pq_regs_drv *)
                        (bar_mem + GXPCI_VF_HOST_PQ_REGS_DRV_OFFSET);
  resource->host_pq.app_regs = (struct gxpci_host_pq_regs_app *)
                        (bar_mem + GXPCI_VF_HOST_PQ_REGS_APP_OFFSET);

  queue_state->barmem_fd = barmem_fd;
  queue_state->bar_mem = bar_mem;

  //
  // Allocate and initialize a push DMA ring for tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PQ_PUSH_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem;

  dma_queue = &resource->host_pq.dma_queue_data;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring, mac,
                                      asid, HV_TRIO_FLAG_VFUNC(vf),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->host_pq.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host is ready and should have initialized the registers in
  // struct gxpci_host_pq_regs_drv that records the the PCI addresses and
  // size info of the host ring buffer.
  //

  //
  // Initialize some states and allocate some resource for the queue.
  //
  gxpci_packet_queue_state_init(context);

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_PACKET_QUEUE,
              resource->host_pq.drv_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_PACKET_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  context->completed = 0;

  return 0;
}

int 
gxpci_pq_h2t_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;
#ifdef ENABLE_CMD_SIZE_QSTS_CRED_CHECKS
  struct gxpci_host_pq_regs_app *app_regs = resource->host_pq.app_regs;

  if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  if ((cmd->size == 0) || (cmd->size > resource->host_pq.drv_regs->buf_size))
    return GXPCI_EINVAL;
#endif

  slot = dma_queue->dma_queue.credits_and_next_index & PQ_PULL_RING_MASK; 
  gen = ((dma_queue->dma_queue.credits_and_next_index >> 
          PQ_PULL_RING_GEN_BIT) ^ 1) & 1;
  
#ifdef DEBUG_H2T
  struct gxpci_host_pq_regs_app *app_regs = resource->host_pq.app_regs;
  GXPCI_TRACE("post cmd of %d bytes.  PI:%d SLOT:%d\n",
              cmd->size, app_regs->producer_index, slot);
#endif
  
#ifdef ENABLE_CMD_SIZE_QSTS_CRED_CHECKS
  /*
   * Block here when host hasn't given us anything to do. 
   * Alternatively, could return GXPCI_ECREDITS.  The caller
   * will typically check for credits before sending a batch of 
   * commands since this will yield higher performance.
   */
  while (!context->credits)
  {
    if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
    
    context->credits = ((app_regs->producer_index & queue_state->cred_mask) -
                        (slot & queue_state->cred_mask)) &
                       queue_state->cred_mask;
                        
  }
#endif

  desc_p = &dma_queue->dma_descs[slot];

  /* SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. */
  xsize = (cmd->size << 1) | (cmd->size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) = 
    ((uintptr_t)(cmd->buffer)) |
    (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);
  
  dma_queue->dma_queue.credits_and_next_index++;
  context->credits--;
  return 0;
}

int
gxpci_pq_t2h_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;

#ifdef ENABLE_CMD_SIZE_QSTS_CRED_CHECKS
  if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  if ((cmd->size == 0) || (cmd->size > resource->host_pq.drv_regs->buf_size))
    return GXPCI_EINVAL;

  /* Try to avoid call to update_counters on every cmd
   * since that's an MMIO access.  Caller presumably
   * updated the credit counters by getting completions.
   */
  while (!context->credits)
  {
    if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
    gxpci_pq_t2h_update_counters(context);
  }
#endif

  slot = dma_queue->dma_queue.credits_and_next_index & PQ_PUSH_RING_MASK; 
  gen = ((dma_queue->dma_queue.credits_and_next_index >> 
          PQ_PUSH_RING_GEN_BIT) ^ 1) & 1;
  desc_p = &dma_queue->dma_descs[slot];

  /* SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. 
   * This shift costs ~3% performance vs. just writing xsize alone.
   * So if performance is very tight it's better to hardcode the size
   * and/or not special case 16KB transfers.
   */
  xsize = (cmd->size << 1) | (cmd->size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) = 
    (uintptr_t)(cmd->buffer) |
    (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
  context->credits--;
  return 0;
}

/**
 * Populate the cpls array with completion info.  Completions are any 
 * descriptors starting from the consumer_index and going to the completed
 * count.  The completed count is updated based on MMIO read of pull dma ring.
 * As completions are generated, we move the consumer_index thus allowing the
 * host to make progress.
 */
int
gxpci_pq_h2t_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                    int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state = resource->host_pq.queue_state;
  uint32_t comp_index = 0;
  struct gxpci_host_pq_regs_app *app_regs = resource->host_pq.app_regs;
  gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
  int slot;
  gxio_trio_dma_desc_t *desc_p; 

  do
  {
    while ((app_regs->consumer_index & queue_state->cred_mask) !=
           (context->completed & queue_state->cred_mask))
    {
      slot = app_regs->consumer_index & PQ_PULL_RING_MASK; 
      desc_p = &dma_queue->dma_descs[slot];
      // Sign-extend the VA 
      cpls[comp_index].buffer = 
#ifdef __LP64__
        (void *)(((int64_t)(desc_p->va) << 
#else
        (void *)(uintptr_t)(((int64_t)(desc_p->va) << 
#endif
                  (64 - TRIO_DMA_DESC_WORD0__VA_WIDTH)) >> 
                 (64 - TRIO_DMA_DESC_WORD0__VA_WIDTH));
#ifdef ENABLE_CMD_SIZE_QSTS_CRED_CHECKS
      // xsize is constanst, so this really isn't needed
      cpls[comp_index].size = desc_p->xsize;
#endif
      app_regs->consumer_index++;
      comp_index++;
      if (comp_index == max)
        return comp_index;
    }

    if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
    {
      return GXPCI_ERESET;
    }
    
    /* Update the completed index in case DMA engine is still finishing some
     * work.  This gaurantees forward progress in case work is pending.
     */
    gxpci_pq_h2t_update_counters(context);

  } while(comp_index < min);

  return comp_index;
}

int
gxpci_pq_t2h_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                    int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state;
  struct gxpci_host_pq_regs_app *app_regs;
  struct gxpci_host_pq_regs_drv *drv_regs;
  gxio_trio_dma_queue_t *dma_queue;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t comp_index = 0;
  int slot;

  queue_state = resource->host_pq.queue_state;
  app_regs = resource->host_pq.app_regs;
  drv_regs = resource->host_pq.drv_regs;
  dma_queue = &resource->host_pq.dma_queue_data;

  do
  {
    /*
     * Note that we ONLY call update_counters if
     * we haven't gotten "max" completions.
     * This prevents extra reads of the MMIO register for
     * push DMA completion status unless we're not making
     * progress.  App can force producer_index to be
     * updated by doing credit_check.
     */
    while (context->completed != app_regs->consumer_index) 
    {
      slot = context->completed & PQ_PUSH_RING_MASK;
      desc_p = &dma_queue->dma_descs[slot];
      /* Prefetch the next 4 cachelines (There are 4 descs per line). */
      if ((comp_index & 0xf) == 0)
      {
        __insn_prefetch((void *)desc_p + (1 << 6));
        __insn_prefetch((void *)desc_p + (2 << 6));
        __insn_prefetch((void *)desc_p + (3 << 6));
        __insn_prefetch((void *)desc_p + (4 << 6));
      }
      // Sign-extend the VA 
      cpls[comp_index].buffer = 
#ifdef __LP64__
        (void *)(((int64_t)(desc_p->va) << 
#else
        (void *)(uintptr_t)(((int64_t)(desc_p->va) << 
#endif
                  (64 - TRIO_DMA_DESC_WORD0__VA_WIDTH)) >> 
                 (64 - TRIO_DMA_DESC_WORD0__VA_WIDTH));
      cpls[comp_index].size = desc_p->xsize;
      context->completed++;
      context->credits++;
      comp_index++;
      if (comp_index == max)
      {
        return comp_index;
      }
    }

    if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
    
    /* Update the producer index in case host hasn't
     * seen our comps yet.  This gaurantees forward
     * progress.
     */
    gxpci_pq_t2h_update_counters(context);
  } while(comp_index < min);

  return comp_index;
}

uint32_t gxpci_pq_h2t_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state = resource->host_pq.queue_state;
  gxio_trio_dma_queue_t* dma_queue = &resource->host_pq.dma_queue_data;
  struct gxpci_host_pq_regs_app *app_regs = resource->host_pq.app_regs;
  uint32_t slot;

  if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  slot = dma_queue->dma_queue.credits_and_next_index; 

  context->credits = ((app_regs->producer_index & queue_state->cred_mask) -
                     (slot & queue_state->cred_mask)) & queue_state->cred_mask;
 
  return context->credits;
}

int gxpci_pq_t2h_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  if (resource->host_pq.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;
  return gxpci_pq_t2h_update_counters(context);
}

int gxpci_pq_destroy(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_packet_queue_state *queue_state = resource->host_pq.queue_state;

  close(context->fd);

  if (queue_state->bar_mem)
    munmap(queue_state->bar_mem, queue_state->barmem_size);

  close(queue_state->barmem_fd);

  return 0;
}
