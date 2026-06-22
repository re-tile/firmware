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

#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arch/cycle.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <gxio/common.h>
#include <gxio/trio.h>
#include <tmc/cpus.h>
#include <tmc/task.h>

#include <gxpci/gxpci.h>

#include <c2c.h>

extern int 
get_bar_addr(gxpci_context_t *context, int local,
             tilegxpci_bar_info_t *bar_info);

extern int
gxpci_alloc_mapping_region(gxio_trio_context_t *trio_context, void *target_mem,
                           size_t target_size, unsigned int asid,
                           unsigned int mac, uint64_t pci_address);

static inline void
c2c_csr_dma_to_bus(gxio_trio_dma_queue_t *dma_queue, uint64_t bus_addr,
                   void *va, size_t size)
{
  gxio_trio_dma_desc_t *desc_p;
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;
  
  slot = dma_queue->dma_queue.credits_and_next_index & C2C_CSR_PUSH_RING_MASK;
  desc_p = &dma_queue->dma_descs[slot];

  desc_p->io_address = bus_addr;
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >>
          C2C_CSR_PUSH_RING_GEN_BIT) ^ 1) & 1;

  /* SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. */
  xsize = (size << 1) | (size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) =
    ((uintptr_t)va) | (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
}

static inline void
c2c_data_dma_to_bus(gxio_trio_dma_queue_t *dma_queue, uint64_t bus_addr,
                    void *va, size_t size)
{
  gxio_trio_dma_desc_t *desc_p;
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;

  slot = dma_queue->dma_queue.credits_and_next_index & C2C_DATA_PUSH_RING_MASK;
  desc_p = &dma_queue->dma_descs[slot];

  desc_p->io_address = bus_addr;
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >>
          C2C_DATA_PUSH_RING_GEN_BIT) ^ 1) & 1;

  /* SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. */
  xsize = (size << 1) | (size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) =
    ((uintptr_t)va) | (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
}

static inline int
is_send_queue_reset(gxpci_context_t *context)
{ 
  gxpci_resource_t *resource = &context->resource;
  uint32_t *local_reset_flag = &resource->send_c2c.queue_sts->status;
  if (*local_reset_flag == GXPCI_CHAN_RESET)
    return 1;

  return 0;
} 

static inline int
is_recv_queue_reset(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  uint32_t *local_reset_flag = &resource->recv_c2c.queue_sts->status;
  if (*local_reset_flag == GXPCI_CHAN_RESET)
    return 1;

  return 0;
}

int
gxpci_alloc_c2c_resource(gxpci_context_t *send_context,
                         gxpci_context_t *recv_context,
                         unsigned int rem_link_index)
{
  gxio_trio_context_t *trio_context;
  unsigned long long local_pci_addr;
  gxpci_resource_t *send_resource;
  gxpci_resource_t *recv_resource;
  unsigned int local_link_index;
  unsigned long long remote_bar;
  tilegxpci_bar_info_t bar_info;
  void *backing_mem;
  int local;
  int asid;
  int err;

  trio_context = send_context->trio_context;
  asid = send_context->resource.asid;

  send_resource = &send_context->resource;
  recv_resource = &recv_context->resource;
  send_context->rem_link_index = rem_link_index;
  recv_context->rem_link_index = rem_link_index;

  //
  // Retrieve the local and remote BAR0 addresses.
  //
  bar_info.bar_index = 0;
  local = 1;
  err = get_bar_addr(send_context, local, &bar_info);
  if (err < 0)
    return err;

  send_resource->send_c2c.local_bar0_addr = bar_info.bar_addr;
  recv_resource->recv_c2c.local_bar0_addr = bar_info.bar_addr;

  local_link_index = bar_info.link_index;

  //
  // C2C must be between two different ports.
  //
  if (rem_link_index == local_link_index)
  {
    fprintf(stderr, "gxpci_alloc_c2c_resource: bad remote link index\n");
    return GXPCI_EINVAL;
  }

  send_context->local_link_index = local_link_index; 
  recv_context->local_link_index = local_link_index; 

  local = 0;
  err = get_bar_addr(send_context, local, &bar_info);
  if (err < 0)
    return err;

  remote_bar = bar_info.bar_addr;
 
  send_resource->send_c2c.remote_bar0_addr = remote_bar;
  recv_resource->recv_c2c.remote_bar0_addr = remote_bar;

  //
  // Retrieve the local and remote BAR2 addresses.
  //
  bar_info.bar_index = 2;
  local = 1;
  err = get_bar_addr(recv_context, local, &bar_info);
  if (err < 0)
    return err;

  recv_resource->recv_c2c.local_bar2_addr = bar_info.bar_addr;

  local = 0;
  err = get_bar_addr(send_context, local, &bar_info);
  if (err < 0)
    return err;

  send_resource->send_c2c.remote_bar2_addr = bar_info.bar_addr;

  //
  // Allocate and bind a huge page which is used for both backing the PCI
  // address window and serving the push DMA operation. We use the beginning
  // of the page to back the PCI space and the end of it for push DMA rings.
  // This huge page is used by both the send and the receive queues.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  backing_mem = tmc_alloc_map(&alloc, GXPCI_C2C_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_C2C_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  send_resource->backing_mem = backing_mem;
  recv_resource->backing_mem = backing_mem;

  local_pci_addr = send_resource->send_c2c.local_bar0_addr +
                   GXPCI_C2C_BAR0_OFFSET +
                   rem_link_index * GXPCI_C2C_SQ_REGION_SIZE;

  //
  // Allocate and initialize a memory mapping region.
  //
  err = gxpci_alloc_mapping_region(trio_context,
                                   backing_mem + GXPCI_C2C_MAP_MEMORY_OFFSET,
                                   GXPCI_C2C_SQ_REGION_SIZE, asid,
                                   send_context->mac, local_pci_addr);
  if (err < 0)
    return err;

  return 0;
}

int
gxpci_c2c_open_send_queue(gxpci_context_t *context, unsigned int queue_index)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *send_state;
  gxio_trio_dma_queue_t *dma_queue;
  unsigned int mac = context->mac;
  char device_name[40];
  size_t dma_ring_size;
  void *dma_ring_mem;
  void *backing_mem;
  int push_ring;
  int err;

  context->queue_index = queue_index;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/c2c/send/%d", context->trio_index,
           context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  resource->send_c2c.mmap_reg_base =
    mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, context->fd, 0);
  if (resource->send_c2c.mmap_reg_base == MAP_FAILED)
  {
    fprintf(stderr, "gxpci_c2c_open_send_queue: mmap failure: %s\n",
            strerror(errno));
    return -errno;
  }

  //
  // Map driver's c2c send queue status to user-space.
  // If this is the RC port, this maps the kernel page that contains
  // the struct tlr_c2c_status.
  //
  if (context->local_link_index == 0)
  {
    resource->send_c2c.queue_sts = (struct tlr_c2c_status *)
      resource->send_c2c.mmap_reg_base;
  }
  else
  {
    resource->send_c2c.queue_sts = (struct tlr_c2c_status *)
      (resource->send_c2c.mmap_reg_base + offsetof(struct gxpci_host_regs,
       c2c_send_status[context->queue_index]));
  }
 
  backing_mem = resource->backing_mem;

  resource->send_c2c.queue_state =
    backing_mem + GXPCI_C2C_SEND_QUEUE_STATE_OFFSET;
  send_state = (struct gxpci_send_state *)resource->send_c2c.queue_state;

  send_state->dma_cmds = (dma_cmd_t *)
    (backing_mem + GXPCI_C2C_SEND_DMA_CMDS_OFFSET);
  send_state->recv_cmds = (recv_cmd_t *)
    (backing_mem + GXPCI_C2C_RECV_BUF_DESC_OFFSET);
  send_state->recv_cmds_consumed = 0;
  send_state->send_cmds_completed = 0;
  send_state->send_cmds_posted = 0;
  send_state->send_cmds_consumed = 0;

  send_state->recv_state =
    resource->send_c2c.remote_bar0_addr + GXPCI_C2C_BAR0_OFFSET +
    context->local_link_index * GXPCI_C2C_SQ_REGION_SIZE +
    GXPCI_C2C_RECV_QUEUE_STATE_OFFSET;

  send_state->recv_page_base = resource->send_c2c.remote_bar2_addr +
    context->local_link_index * GXPCI_C2C_RECV_DATA_REGION_SIZE;

  //
  // Allocate and initialize the data push DMA ring.
  //
  push_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(push_ring, "gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_C2C_DATA_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem + GXPCI_C2C_SEND_DATA_DMA_RING_OFFSET;
  dma_queue = &resource->send_c2c.dma_queue;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, push_ring,
                                      mac, resource->asid, 0, dma_ring_mem,
                                      dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the sender side marks itself
  // ready and waits for the receiver to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &send_state->ready;

  err = ioctl(context->fd, TILEPCI_IOC_C2C_SET_SENDER_READY);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_C2C_SET_SENDER_READY ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  while (*loc_ready != GXPCI_C2C_RECV_PORT_READY)
  {
    sleep(1);

    if (is_send_queue_reset(context))
      return GXPCI_ERESET;
  }

  //
  // We should have received the pkt_headroom value from the receiver.
  // Add it to the receive page's PCI base address here so that no
  // extra addition operation is needed for each packet.
  //
  send_state->recv_page_base += send_state->pkt_headroom;

  //
  // Activate this queue in the C2C queue status array in RC memory.
  //
  tilegxpci_c2c_activate_queue_t activate_queue;

  activate_queue.sender_link_index = context->local_link_index;
  activate_queue.receiver_link_index = context->rem_link_index;

  err = ioctl(context->fd, TILEPCI_IOC_C2C_ACTIVATE_QUEUE, &activate_queue);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_C2C_ACTIVATE_QUEUE ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  return 0;
}

int
gxpci_c2c_open_recv_queue(gxpci_context_t *context, unsigned int pkt_headroom,
                          unsigned int recv_page_size, void *recv_page_addr,
                          unsigned int queue_index)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *recv_state;
  unsigned int mac = context->mac;
  unsigned long local_bus_addr;
  unsigned long recv_page_mask;
  char device_name[40];
  void *backing_mem;
  int err;

  context->queue_index = queue_index;

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/c2c/recv/%d", context->trio_index,
           context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  resource->recv_c2c.mmap_reg_base =
    mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, context->fd, 0);
  if (resource->recv_c2c.mmap_reg_base == MAP_FAILED)
  {
    fprintf(stderr, "gxpci_c2c_open_recv_queue: mmap failure: %s\n",
            strerror(errno));
    return -errno;
  }

  //
  // Map driver's c2c receive queue status to user-space.
  // If this is the RC port, this maps the kernel page that contains
  // the struct tlr_c2c_status.
  //
  if (context->local_link_index == 0)
  {
    resource->recv_c2c.queue_sts = (struct tlr_c2c_status *)
      resource->recv_c2c.mmap_reg_base;
  }
  else
  {
    resource->recv_c2c.queue_sts = (struct tlr_c2c_status *)
      (resource->recv_c2c.mmap_reg_base + offsetof(struct gxpci_host_regs,
       c2c_recv_status[context->queue_index]));
  }

  //
  // Make sure that the receive page size is a power of 2 and it is
  // page-aligned.
  //
  recv_page_mask = recv_page_size - 1;
  if ((recv_page_addr == NULL) ||
      (recv_page_size & (recv_page_size - 1)) ||
      (((intptr_t)recv_page_addr) & recv_page_mask))
    return GXPCI_EINVAL;

  backing_mem = resource->backing_mem;

  resource->recv_c2c.queue_state =
    backing_mem + GXPCI_C2C_RECV_QUEUE_STATE_OFFSET;
  recv_state = (struct gxpci_recv_state *)resource->recv_c2c.queue_state;
  recv_state->recv_cmds_posted = 0;
  recv_state->recv_cmds_sent = 0;
  recv_state->recv_cmds_consumed = 0;

  recv_state->recv_cmds = (recv_cmd_t *)
    (backing_mem + GXPCI_C2C_RECV_BUF_DESC_RING_OFFSET);

  recv_state->send_state =
    resource->recv_c2c.remote_bar0_addr + GXPCI_C2C_BAR0_OFFSET +
    context->local_link_index * GXPCI_C2C_SQ_REGION_SIZE +
    GXPCI_C2C_SEND_QUEUE_STATE_OFFSET;

  recv_state->recv_cmds_at_sender =
    resource->recv_c2c.remote_bar0_addr + GXPCI_C2C_BAR0_OFFSET +
    context->local_link_index * GXPCI_C2C_SQ_REGION_SIZE +
    GXPCI_C2C_RECV_BUF_DESC_OFFSET;
 
  //
  // Allocate and initialize the memory map region that maps the huge page
  // allocated by the application as receive buffer pool to the PCI space.
  //
  int mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  //
  // With the default RX_BAR2_ADDR_MASK value -0ULL, full PCI address is
  // written to MEM_MAP_BASE.
  //
  local_bus_addr = resource->recv_c2c.local_bar2_addr +
                   context->rem_link_index * GXPCI_C2C_RECV_DATA_REGION_SIZE;

  err = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  (void *)recv_page_addr,
                                  GXPCI_C2C_RECV_DATA_REGION_SIZE,
                                  resource->asid, mac, local_bus_addr,
                                  GXIO_TRIO_ORDER_MODE_UNORDERED);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_memory_map()");

  //
  // Allocate and initialize the CSR push DMA ring.
  //
  int push_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(push_ring, "gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  size_t dma_ring_size = GXPCI_C2C_CSR_DMA_RING_LEN *
                         sizeof(gxio_trio_dma_desc_t);
  void *dma_ring_mem = backing_mem + GXPCI_C2C_RECV_CSR_DMA_RING_OFFSET;
  gxio_trio_dma_queue_t *dma_queue = &resource->recv_c2c.csr_queue;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, push_ring,
                                      mac, resource->asid, 0, dma_ring_mem,
                                      dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the receiver side
  // waits for the sender to enter the ready state.
  //
  volatile uint32_t sender_ready = 0;

  while (1)
  {
    err = ioctl(context->fd, TILEPCI_IOC_C2C_GET_SENDER_READY, &sender_ready);
    if (err < 0)
    {
      fprintf(stderr, "%s TILEPCI_IOC_C2C_GET_SENDER_READY failure: %s\n",
              device_name, strerror(errno));
      return -errno;
    }

    if (sender_ready == GXPCI_TILE_CHAN_READY)
      break;

    sleep(1);
    if (is_recv_queue_reset(context))
      return GXPCI_ERESET;
  }

  //
  // The sender is ready.
  //

  recv_state->recv_page_mask = recv_page_mask;
  recv_state->pkt_headroom = pkt_headroom;
  recv_state->ready = GXPCI_C2C_RECV_PORT_READY;

  // Make data visible to push DMA command
  __insn_mf();

  //
  // Inform the sender of the receive page mask and
  // the packet headroom space.
  //
  c2c_csr_dma_to_bus(dma_queue,
                     recv_state->send_state +
                     offsetof(struct gxpci_send_state, recv_page_mask),
                     &recv_state->recv_page_mask,
                     sizeof(recv_state->recv_page_mask));
  c2c_csr_dma_to_bus(dma_queue,
                     recv_state->send_state +
                     offsetof(struct gxpci_send_state, pkt_headroom),
                     &recv_state->pkt_headroom,
                     sizeof(recv_state->pkt_headroom));

  //
  // Finally, the receiver sends its ready status to the sender.
  //
  c2c_csr_dma_to_bus(dma_queue,
                     recv_state->send_state +
                     offsetof(struct gxpci_send_state, ready),
                     &recv_state->ready, sizeof(recv_state->ready));

  return 0;
}

/**
 * Check for and process any new receive commands that have been received from
 * the receiver. For each command, deposit it in the next entry of the DMA
 * command array. If the DMA command entry has a send command ready, start
 * the DMA; otherwise, the DMA will be started later when a send command
 * is submitted to this DMA command entry.
 */
static void
gxpci_parse_recv_cmds(gxpci_resource_t *resource)
{
  struct gxpci_send_state *send_state = resource->send_c2c.queue_state;
  gxio_trio_dma_queue_t *dma_queue = &resource->send_c2c.dma_queue;
  recv_cmd_t *recv_cmd;
  uint32_t cmd_index;
  dma_cmd_t *dma_cmd;
  uint32_t consumed;

  consumed = send_state->recv_cmds_consumed;

  while (1)
  {
    //
    // Make sure we don't overwrite the completed dma_cmd entries that
    // haven't been consumed by gxpci_c2c_get_send_comps().
    //
    if (send_state->send_cmds_completed != send_state->send_cmds_consumed &&
        consumed - send_state->send_cmds_consumed == GXPCI_C2C_MAX_CMDS)
      break;

    cmd_index = consumed & (GXPCI_C2C_MAX_CMDS - 1);
    recv_cmd = send_state->recv_cmds + cmd_index;
    if (recv_cmd->addr == NULL)
      break;

    dma_cmd = send_state->dma_cmds + cmd_index;

    dma_cmd->recv_offset = (intptr_t)recv_cmd->addr &
                           send_state->recv_page_mask;

    //
    // If the dma_cmd is already set with the send buffer info,
    // start the DMA WRITE.
    //
    dma_cmd->recv_filled = 1;
    if (dma_cmd->send_filled)
    {
      dma_cmd->size = MIN(dma_cmd->size, recv_cmd->size);

      c2c_data_dma_to_bus(dma_queue,
                          send_state->recv_page_base + dma_cmd->recv_offset,
                          dma_cmd->send_addr, dma_cmd->size);
      dma_cmd->send_filled = 0;
      dma_cmd->recv_filled = 0;
    }
    else
    {
      dma_cmd->size = recv_cmd->size;
    }
    recv_cmd->addr = NULL;
    consumed++;
  }

  send_state->recv_cmds_consumed = consumed;
}

int
gxpci_c2c_send_cmds(gxpci_context_t *context, const gxpci_cmd_t *cmds,
                    uint32_t cmd_count)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *send_state;
  gxio_trio_dma_queue_t *dma_queue;
  uint32_t cmd_index;
  dma_cmd_t *dma_cmd;
  uint32_t i;

  if (is_send_queue_reset(context))
    return GXPCI_ERESET;

  send_state = resource->send_c2c.queue_state;
  dma_queue = &resource->send_c2c.dma_queue;

  //
  // No need to check for credits here since gxpci_c2c_recv_get_credits()
  // should have been called by the application before reaching here.
  //
  for (i = 0; i < cmd_count; i++)
  {
    cmd_index = send_state->send_cmds_posted++ & (GXPCI_C2C_MAX_CMDS - 1);
    dma_cmd = send_state->dma_cmds + cmd_index;

    dma_cmd->send_addr = cmds[i].buffer;

    //
    // If the dma_cmd is already set with the receiver buffer info,
    // start the DMA WRITE.
    //
    dma_cmd->send_filled = 1;
    if (dma_cmd->recv_filled)
    {
      dma_cmd->size = MIN(cmds[i].size, dma_cmd->size);

      c2c_data_dma_to_bus(dma_queue,
                          send_state->recv_page_base + dma_cmd->recv_offset,
                          dma_cmd->send_addr, dma_cmd->size);
      dma_cmd->send_filled = 0;
      dma_cmd->recv_filled = 0;
    }
    else
    {
      dma_cmd->size = cmds[i].size;
    }
  }

  //
  // Process any unparsed receive commands.
  //
  gxpci_parse_recv_cmds(resource);

  return 0;
}

/**
 * Check if the receiver can write more receive buffer descriptors
 * to the sender.
 */
static void
gxpci_update_recv_cmds(gxpci_resource_t *resource)
{
  struct gxpci_recv_state *recv_state;
  gxio_trio_dma_queue_t *dma_queue;
  uint32_t cmds_can_send;
  uint32_t cmd_index;

  recv_state = resource->recv_c2c.queue_state;
  dma_queue = &resource->recv_c2c.csr_queue;
  cmds_can_send = recv_state->recv_cmds_posted -
                 recv_state->recv_cmds_sent;

  // See if we need to send more receive commands. We may need to split
  // the transfer into two DMA commands if they happen to wrap
  // around the end of the local and/or remote commands buffers.
  while (cmds_can_send)
  {
    uint32_t batch;

    cmd_index = recv_state->recv_cmds_sent & (GXPCI_C2C_MAX_CMDS - 1);
    batch = GXPCI_C2C_MAX_CMDS - cmd_index;
    batch = MIN(cmds_can_send, batch);
    batch = MIN(batch, GXPCI_MAX_CMD_SIZE / sizeof(recv_cmd_t));

    // Make data visible to push DMA command
    __insn_mf();

    c2c_csr_dma_to_bus(dma_queue,
                       recv_state->recv_cmds_at_sender +
                       cmd_index * sizeof(recv_cmd_t),
                       &recv_state->recv_cmds[cmd_index],
                       batch * sizeof(recv_cmd_t));

    recv_state->recv_cmds_sent += batch;
    cmds_can_send -= batch;
  }
}

int
gxpci_c2c_recv_cmds(gxpci_context_t *context, const gxpci_cmd_t *cmds,
                    uint32_t cmd_count)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *recv_state;
  uint32_t cmd_index;
  uint32_t i;

  if (is_recv_queue_reset(context))
    return GXPCI_ERESET;

  recv_state = resource->recv_c2c.queue_state;

  //
  // No need to check for credits here since gxpci_c2c_recv_get_credits()
  // should have been called by the application before reaching here.
  //
  for (i = 0; i < cmd_count; i++)
  {
    c2c_pkt_header_t *pkt_header = cmds[i].buffer;

    pkt_header->ready = 0;
    pkt_header->size = 0;

    cmd_index = recv_state->recv_cmds_posted++ & (GXPCI_C2C_MAX_CMDS - 1);
    recv_state->recv_cmds[cmd_index].addr = cmds[i].buffer;
    recv_state->recv_cmds[cmd_index].size = cmds[i].size -
                                            recv_state->pkt_headroom;
  }

  gxpci_update_recv_cmds(resource);

  return 0;
}

int
gxpci_c2c_get_send_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *send_state;
  gxio_trio_dma_queue_t *dma_queue;
  uint32_t ret_completions;
  uint16_t new_completions;
  volatile uint16_t hw_cnt;
  unsigned int comp_index;
  dma_cmd_t *dma_cmd;
  int i;

  dma_queue = &resource->send_c2c.dma_queue;
  send_state = resource->send_c2c.queue_state;

try_again:

  if (is_send_queue_reset(context))
    return GXPCI_ERESET;

  gxpci_parse_recv_cmds(resource);

  //
  // Determine if any data DMAs have completed.
  //
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue);
  new_completions = (uint16_t)
    (hw_cnt - (send_state->send_cmds_completed &
    TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK));

  send_state->send_cmds_completed += new_completions;

  ret_completions = send_state->send_cmds_completed -
                    send_state->send_cmds_consumed;

  if (ret_completions >= max)
    ret_completions = max;
  else if (ret_completions < min)
    goto try_again;

  for (i = 0; i < ret_completions; i++)
  {
    comp_index = send_state->send_cmds_consumed++ & (GXPCI_C2C_MAX_CMDS - 1);
    dma_cmd = send_state->dma_cmds + comp_index;

    cpls[i].buffer = dma_cmd->send_addr;
    cpls[i].size = dma_cmd->size;
  }

  return ret_completions;
}
 
int
gxpci_c2c_get_recv_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *recv_state;
  unsigned int ret_completions = 0;
  unsigned int comp_index;
  unsigned int consumed;
  recv_cmd_t *recv_cmd;
  int i = 0;

  recv_state = resource->recv_c2c.queue_state;
  consumed = recv_state->recv_cmds_consumed;

try_again:

  if (is_recv_queue_reset(context))
    return GXPCI_ERESET;

  gxpci_update_recv_cmds(resource);

  //
  // Receiving the packets reliably can be tricky.
  // Traditionally, the PCIe data transfer implementations follow the
  // transaction order of transmitting packet metadata (flag and status)
  // after sending the data packets. Unfortunately, even though the data
  // and the metadata packets are sent out of the source PCIe MAC
  // in the right order, it is possible that they arrive at
  // the target in a different order. The solution here is to transfer
  // the data payload and the metadata in the same packet, with metadata
  // located in the beginning of the packet. We define the metadata in
  // the form of struct c2c_pkt_header.
  //
  // Even with the combined packet format, it is possible for some data to
  // be unavailable after the c2c_pkt_header has been read by the receiver
  // program, especially when the combined packet size is larger than the
  // PCIe link MPS causing the packet to be transferred in multiple PCIe
  // packets. To work around this issue, we adopt the logic of only
  // consuming N-1 packets after detecting the availability of N packets.
  // To avoid the situation of leaving one lone packet unconsumed forever,
  // a timer is used to implement a guaranteed delay from the metadata
  // arrival to the packet consumption.
  //
  while (1)
  {
    c2c_pkt_header_t *pkt_header;

    comp_index = consumed & (GXPCI_C2C_MAX_CMDS - 1);
    recv_cmd = recv_state->recv_cmds + comp_index;
    pkt_header = (c2c_pkt_header_t *)recv_cmd->addr;
    if (pkt_header && pkt_header->ready)
    {
      ret_completions++;

      if (ret_completions > max)
      {
        ret_completions = max;
        break;
      }

      cpls[i].buffer = recv_cmd->addr;
      cpls[i].size = pkt_header->size;
      consumed++;
      i++;
    }
    else
      break;
  }

  if (ret_completions < min)
    goto try_again;

  if (ret_completions)
  {
    ret_completions--;
    if (!ret_completions)
    {
      if (!recv_state->ticking)
      {
        recv_state->ticking = 1;
        recv_state->timer_start = get_cycle_count();
      }
      else if (get_cycle_count() - recv_state->timer_start > 5000000)
      {
        ret_completions = 1;
        recv_state->ticking = 0;
      }
    }
    else
      recv_state->ticking = 0;

    recv_state->recv_cmds_consumed += ret_completions;
  }

  return ret_completions;
}
 
uint32_t
gxpci_c2c_send_get_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *send_state = resource->send_c2c.queue_state;

  return GXPCI_C2C_MAX_CMDS -
         (send_state->send_cmds_posted - send_state->send_cmds_consumed);
}

uint32_t
gxpci_c2c_recv_get_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *recv_state = resource->recv_c2c.queue_state;

  return GXPCI_C2C_MAX_CMDS -
         (recv_state->recv_cmds_posted - recv_state->recv_cmds_consumed);
}

int
gxpci_c2c_send_destroy(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;

  munmap(resource->send_c2c.mmap_reg_base, getpagesize());

  close(context->fd);

  return 0;
}

int
gxpci_c2c_recv_destroy(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;

  munmap(resource->recv_c2c.mmap_reg_base, getpagesize());

  close(context->fd);

  return 0;
}

int
gxpci_free_c2c_resource(gxio_trio_context_t *trio_context)
{
  return gxio_trio_destroy(trio_context);
}
