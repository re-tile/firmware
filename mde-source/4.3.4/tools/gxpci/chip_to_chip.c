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

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <gxio/common.h>
#include <gxio/trio.h>
#include <tmc/cpus.h>
#include <tmc/task.h>

#include <arch/sim.h>

#include <gxpci/gxpci.h>

#include <chip_to_chip.h>

#define C2C_PUSH_RING_GEN_BIT GXPCI_C2C_PUSH_DMA_RING_ORD
#define C2C_PUSH_RING_MASK (GXPCI_C2C_PUSH_DMA_RING_LEN - 1)

#define SIM0_BAR_ADDR 0xdeadbeef00000000ull
#define SIM1_BAR_ADDR 0xcafebad000000000ull

/** For simulator test use only: Default BAR0 region size. */
#define BAR_SIZE 0x800000

extern int 
get_bar_addr(gxpci_context_t *context, int local,
             tilegxpci_bar_info_t *bar_info);

static inline void
c2c_dma_to_bus(gxio_trio_dma_queue_t *dma_queue, uint64_t bus_addr,
               void *va, size_t size)
{
  gxio_trio_dma_desc_t *desc_p;
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;
  
  slot = dma_queue->dma_queue.credits_and_next_index & C2C_PUSH_RING_MASK;
  desc_p = &dma_queue->dma_descs[slot];

  desc_p->io_address = bus_addr;
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >>
          C2C_PUSH_RING_GEN_BIT) ^ 1) & 1;

  /* SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. */
  xsize = (size << 1) | (size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) =
    ((uintptr_t)va) | (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
}

/**
 * When the C2C app runs on an EP node, it calls this function to mmap
 * the queue status flag that is embedded in the gxpci_host_regs struct
 * which is also mapped by the RC node.
 */
static int
get_queue_status(gxpci_context_t *context)
{
  context->mmio_reg_base = mmap(NULL, getpagesize(), PROT_READ,
                                MAP_SHARED, context->fd, 0);
  if (context->mmio_reg_base == MAP_FAILED)
    return -errno;

  return 0;
}

static inline int is_send_queue_reset(gxpci_context_t *context)
{ 
  if (!sim_is_simulator())
  {
    gxpci_resource_t *resource = &context->resource;
    uint32_t *local_reset_flag = &resource->c2c_send.queue_sts->status;
    if (*local_reset_flag == GXPCI_CHAN_RESET)
      return 1;
  } 
  return 0;
} 

static inline int is_recv_queue_reset(gxpci_context_t *context)
{
  if (!sim_is_simulator())
  {
    gxpci_resource_t *resource = &context->resource;
    uint32_t *local_reset_flag = &resource->c2c_recv.queue_sts->status;
    if (*local_reset_flag == GXPCI_CHAN_RESET)
      return 1;
  } 
  return 0;
}

int gxpci_open_c2c_send_queue(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_context_t *trio_context = context->trio_context;
  struct gxpci_send_state *queue_state;
  int asid = context->resource.asid;
  unsigned int mac = context->mac;
  unsigned int local_link_index = 0;
  unsigned long long local_bar;
  unsigned long long remote_bar;
  unsigned long long local_bus_addr;
  void *backing_mem;
  char device_name[40];
  int err;

  //
  // Get the file handle for queue status monitoring.
  //
  if (!sim_is_simulator())
  {
    snprintf(device_name, sizeof(device_name),
             "/dev/trio%d-mac%d/c2c/send/%d",
             context->trio_index, context->mac, context->queue_index);
    context->fd = open(device_name, O_RDWR);
    if (context->fd < 0)
    {
      fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
      return -errno;
    }
  }

  //
  // Retrieve the local and remote BAR0 addresses.
  // Use the hard-coded BAR address under the simulator.
  //
  if (sim_is_simulator())
  {
    local_bar = SIM0_BAR_ADDR;
    remote_bar = SIM1_BAR_ADDR;
  }
  else
  {
    tilegxpci_bar_info_t bar_info;
    int ret;

    ret = get_bar_addr(context, 1, &bar_info);
    if (ret < 0)
      return ret;

    local_bar = bar_info.bar_addr;

    //
    // If local_link_index is 0, this is a RC port.
    //
    local_link_index = bar_info.link_index;
    context->local_link_index = local_link_index; 

    if (context->rem_link_index == local_link_index)
    {
      fprintf(stderr, "gxpci_open_c2c_send_queue: bad remote link index\n");
      return GXPCI_EINVAL;
    }

    ret = get_bar_addr(context, 0, &bar_info);
    if (ret < 0)
      return ret;

    remote_bar = bar_info.bar_addr;

    //
    // Map driver's c2c send queue status to user-space.
    //
    if (local_link_index == 0)
    {
      resource->c2c_send.queue_sts = 
        mmap(NULL, sizeof(struct tlr_c2c_status), PROT_READ,
             MAP_SHARED, context->fd, 0);
      if (resource->c2c_send.queue_sts == MAP_FAILED)
      {
        fprintf(stderr, "gxpci_open_c2c_send_queue: %s\n", strerror(errno));
        return -errno;
      }
    }
    else
    {
      ret = get_queue_status(context);
      if (ret < 0)
      {
        fprintf(stderr, "get_queue_status: %s\n", strerror(-ret));
        return ret;
      }

      resource->c2c_send.queue_sts = (struct tlr_c2c_status *)
        (context->mmio_reg_base + offsetof(struct gxpci_host_regs,
         c2c_send_status[context->queue_index]));
    }
  }
 
  //
  // Allocate and bind a huge page which is used for both backing the PCI
  // address window and serving the push DMA operation. We use the beginning
  // of the page to back the PCI space and the end of it for push DMA.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_huge(&alloc);
  backing_mem = tmc_alloc_map(&alloc, GXPCI_C2C_SEND_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_C2C_SEND_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  resource->backing_mem = backing_mem;

  //
  // Allocate and initialize the memory map region that backs the
  // gxpci_send_state struct for this queue.
  //
  int mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  local_bus_addr = local_bar + GXPCI_C2C_SEND_REGS_OFFSET +
                   context->queue_index * GXPCI_C2C_QUEUE_MEM_MAP_SIZE;

  resource->c2c_send.queue_state = backing_mem + GXPCI_C2C_QUEUE_STATE_OFFSET;
  queue_state = (struct gxpci_send_state *)resource->c2c_send.queue_state;
  err = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  (void *)queue_state,
                                  GXPCI_C2C_QUEUE_MEM_MAP_SIZE,
                                  asid, mac, local_bus_addr,
                                  GXIO_TRIO_ORDER_MODE_STRICT);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_memory_map()");

  queue_state->sq_bus_addr = remote_bar + GXPCI_C2C_RECV_DATA_ADDR_OFFSET +
                             context->queue_index * GXPCI_C2C_SQ_REGION_SIZE;
  queue_state->dmas_started = 0;
  queue_state->dmas_completed = 0;
  queue_state->dma_queue_counter = 0;
  queue_state->send_cmds_posted = 0;
  queue_state->send_cmds_consumed = 0;
  queue_state->doorbell.pop = 1;
  queue_state->doorbell.doorbell = 0;

  //
  // Allocate and initialize a push DMA ring.
  //
  int push_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(push_ring, "gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  size_t dma_ring_size = GXPCI_C2C_PUSH_DMA_RING_LEN *
                         sizeof(gxio_trio_dma_desc_t);
  void* dma_ring_mem = backing_mem + GXPCI_C2C_SEND_BACK_MEM_SIZE -
                       dma_ring_size;
  gxio_trio_dma_queue_t* queue = &resource->c2c_send.dma_queue;
  err = gxio_trio_init_push_dma_queue(queue, trio_context, push_ring, mac,
                                      asid, 0, dma_ring_mem,
                                      dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the sender side marks itself
  // ready and waits for the receiver to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &queue_state->ready;

  if (sim_is_simulator())
    *loc_ready = GXPCI_C2C_SEND_PORT_READY;
  else
  {
    err = ioctl(context->fd, TILEPCI_IOC_C2C_SET_SENDER_READY);
    if (err < 0)
    {
      fprintf(stderr, "%s TILEPCI_IOC_C2C_SET_SENDER_READY ioctl failure: %s\n",
              device_name, strerror(errno));
      return -errno;
    }
  }

  while (*loc_ready != GXPCI_C2C_RECV_PORT_READY)
  {
    if (!sim_is_simulator())
      sleep(1);

    if (is_send_queue_reset(context))
      return GXPCI_ERESET;
  }

  //
  // By now the receiver should have written us its sq region size and the
  // packet headroom, from which we calculate the doorbell register
  // bus address and the starting data address.
  //
  queue_state->doorbell_bus_addr = queue_state->sq_bus_addr +
                                   queue_state->sq_region_size - 8;
  queue_state->sq_data_addr = queue_state->sq_bus_addr +
                              queue_state->pkt_headroom;

  //
  // Activate this queue in the C2C queue status array in RC memory.
  //
  if (!sim_is_simulator())
  {
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
  }

  return 0;
}

int
gxpci_open_c2c_recv_queue(gxpci_context_t *context,
                          unsigned int pkt_headroom,
                          unsigned int recv_buf_size)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_context_t *trio_context = context->trio_context;
  struct gxpci_recv_state *queue_state;
  int asid = context->resource.asid;
  unsigned int mac = context->mac;
  unsigned int local_link_index = 0;
  unsigned long long local_bar;
  unsigned long long remote_bar;
  unsigned long long local_bus_addr;
  unsigned long long remote_bus_addr;
  unsigned long long recv_port_bus_addr;
  void *backing_mem;
  char device_name[40];
  int err;

  //
  // Get the file handle for queue status monitoring.
  //
  if (!sim_is_simulator())
  {
    snprintf(device_name, sizeof(device_name),
             "/dev/trio%d-mac%d/c2c/recv/%d",
             context->trio_index, context->mac, context->queue_index);
    context->fd = open(device_name, O_RDWR);
    if (context->fd < 0)
    {
      fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
      return -errno;
    }
  }

  //
  // Retrieve the local and remote BAR0 addresses.
  // Use the hard-coded BAR address under the simulator.
  //
  if (sim_is_simulator())
  {
    local_bar = SIM1_BAR_ADDR;
    remote_bar = SIM0_BAR_ADDR;
  }
  else
  {
    tilegxpci_bar_info_t bar_info;
    int ret;

    ret = get_bar_addr(context, 1, &bar_info);
    if (ret < 0)
      return ret;

    local_bar = bar_info.bar_addr;

    //
    // If local_link_index is 0, this is a RC port.
    //
    local_link_index = bar_info.link_index;
    context->local_link_index = local_link_index; 

    if (context->rem_link_index == local_link_index)
    {
      fprintf(stderr, "gxpci_open_c2c_recv_queue: bad remote link index\n");
      return GXPCI_EINVAL;
    }

    ret = get_bar_addr(context, 0, &bar_info);
    if (ret < 0)
      return ret;

    remote_bar = bar_info.bar_addr;

    //
    // Map driver's c2c receive queue status to user-space.
    //
    if (local_link_index == 0) 
    {
      resource->c2c_recv.queue_sts = 
        mmap(NULL, sizeof(struct tlr_c2c_status), PROT_READ,
             MAP_SHARED, context->fd, 0);
      if (resource->c2c_recv.queue_sts == MAP_FAILED)
      {
        fprintf(stderr, "gxpci_open_c2c_recv_queue: %s\n", strerror(errno));
        return -errno;
      }
    }
    else
    {
      ret = get_queue_status(context);
      if (ret < 0)
      {
        fprintf(stderr, "get_queue_status: %s\n", strerror(-ret));
        return ret;
      }

      resource->c2c_recv.queue_sts = (struct tlr_c2c_status *)
        (context->mmio_reg_base + offsetof(struct gxpci_host_regs,
         c2c_recv_status[context->queue_index]));
    }
  }

  //
  // Make sure that the receive buffer size is a power of 2 and that
  // the packet headroom is less than the receive buffer size.
  //
  if ((recv_buf_size > GXPCI_C2C_MAX_RECV_BUF_SIZE) ||
      (recv_buf_size & (recv_buf_size - 1)) ||
      (pkt_headroom >= recv_buf_size))
    return GXPCI_EINVAL;

  //
  // Allocate and bind a page which is used for backing the PCI
  // address window.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  backing_mem = tmc_alloc_map(&alloc, GXPCI_C2C_RECV_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  err = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_C2C_RECV_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_register_page()");

  resource->backing_mem = backing_mem;

  //
  // Allocate and initialize the memory map region that backs the
  // gxpci_recv_state struct for this queue.
  //
  int mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  local_bus_addr = local_bar + GXPCI_C2C_RECV_REGS_OFFSET +
                   context->queue_index * GXPCI_C2C_QUEUE_MEM_MAP_SIZE;

  recv_port_bus_addr = local_bus_addr + GXPCI_C2C_QUEUE_STATE_OFFSET;

  resource->c2c_recv.queue_state = backing_mem + GXPCI_C2C_QUEUE_STATE_OFFSET;
  queue_state = (struct gxpci_recv_state *)resource->c2c_recv.queue_state;
  err = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  (void *)queue_state,
                                  GXPCI_C2C_QUEUE_MEM_MAP_SIZE,
                                  asid, mac, local_bus_addr,
                                  GXIO_TRIO_ORDER_MODE_STRICT);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_memory_map()");

  queue_state->recv_cmds_posted = 0;
  queue_state->recv_cmds_completed = 0;
  queue_state->recv_cmds_consumed = 0;

  remote_bus_addr = remote_bar + GXPCI_C2C_SEND_REGS_OFFSET +
                    context->queue_index * GXPCI_C2C_QUEUE_MEM_MAP_SIZE +
                    GXPCI_C2C_QUEUE_STATE_OFFSET;

  //
  // Allocate and initialize the PIO region that maps to the
  // gxpci_send_state struct on the sender port.
  //
  int pio_region = gxio_trio_alloc_pio_regions(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(pio_region, "gxio_trio_alloc_pio_regions()");

  err = gxio_trio_init_pio_region(trio_context, pio_region, mac,
                                  remote_bus_addr, 0);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_pio_region()");

  //
  // Get the VA to the MMIO PIO region, which needs to be munmap'ed
  // before closing the trio_context file descriptor.
  //
  void* mmio = gxio_trio_map_pio_region(trio_context, pio_region,
                                        GXPCI_C2C_QUEUE_MEM_MAP_SIZE, 0);
  if (mmio == MAP_FAILED)
  {
    fprintf(stderr, "Failure in gxio_trio_map_pio_region: %s\n",
            strerror(errno));
    return -errno;
  }

  queue_state->send_state = (struct gxpci_send_state *)mmio;
  resource->c2c_recv.pio = pio_region;

  struct gxpci_send_state *send_state = queue_state->send_state;

  //
  // Allocate and initialize a scatter queue.
  //
  int sq_index = gxio_trio_alloc_scatter_queues(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(sq_index, "gxio_trio_alloc_scatter_queues()");

  uint64_t sq_bus_addr = local_bar + GXPCI_C2C_RECV_DATA_ADDR_OFFSET +
                         context->queue_index * GXPCI_C2C_SQ_REGION_SIZE;

  err = gxio_trio_init_scatter_queue(trio_context, sq_index,
                                     GXPCI_C2C_SQ_REGION_SIZE,
                                     asid, mac,
                                     sq_bus_addr,
                                     GXIO_TRIO_ORDER_MODE_STRICT);
  GXIO_VERIFY_ZERO(err, "gxio_trio_init_scatter_queue()");

  resource->c2c_recv.scatter_queue = sq_index;

  //
  // Now that all the resources are allocated, the receiver side
  // waits for the sender to enter the ready state.
  //
  if (sim_is_simulator())
  {
    uint32_t *rem_ready = &send_state->ready;

    while (gxio_trio_read_uint32(rem_ready) != GXPCI_C2C_SEND_PORT_READY)
    {
      if (is_recv_queue_reset(context))
        return GXPCI_ERESET;
    }
  }
  else
  {
    volatile uint32_t sender_ready = 0;

    while (1)
    {
      err = ioctl(context->fd, TILEPCI_IOC_C2C_GET_SENDER_READY, &sender_ready);
      if (err < 0)
      {
        fprintf(stderr,
                "%s TILEPCI_IOC_C2C_GET_SENDER_READY ioctl failure: %s\n",
                device_name, strerror(errno));
        return -errno;
      }

      if (sender_ready == GXPCI_TILE_CHAN_READY)
        break;

      sleep(1);
      if (is_recv_queue_reset(context))
        return GXPCI_ERESET;
    }
  }

  //
  // The sender is ready. Inform the sender of the scatter queue region size.
  //
  gxio_trio_write_uint32(&send_state->sq_region_size, GXPCI_C2C_SQ_REGION_SIZE);

  //
  // Inform the sender of the packet headroom space.
  //
  gxio_trio_write_uint32(&send_state->pkt_headroom, pkt_headroom);

  //
  // Inform the sender of the receive buffer size.
  //
  gxio_trio_write_uint32(&send_state->recv_buf_size, recv_buf_size);

  //
  // Inform the sender of the receive port address.
  //
  gxio_trio_write_uint64(&send_state->recv_state, recv_port_bus_addr);

  //
  // Finally, the receiver sends its ready status to the sender.
  //
  gxio_trio_write_uint32(&send_state->ready, GXPCI_C2C_RECV_PORT_READY);

  //
  // Initialize the SQ completion counter by reading from MMIO register
  // because this counter is reset only when TRIO is reset.
  //
  queue_state->sq_comp_counter =
    gxio_trio_read_scatter_queue_pop_count(trio_context, sq_index);

  return 0;
}

static void gxpci_c2c_process_cmds(gxpci_resource_t *resource)
{
  struct gxpci_send_state *queue_state = resource->c2c_send.queue_state;
  gxio_trio_dma_queue_t *dma_queue = &resource->c2c_send.dma_queue;
  uint64_t sq_data_addr = queue_state->sq_data_addr;
  uint32_t dma_cmd_index;
  dma_cmd_t *dma_cmd;

  for (; queue_state->dmas_started != queue_state->send_cmds_posted;
       queue_state->dmas_started++)
  {
    dma_cmd_index = queue_state->dmas_started & (GXPCI_C2C_MAX_CMDS - 1);
    dma_cmd = queue_state->dma_cmds + dma_cmd_index;

    //
    // If the recv_addr field is NULL, the receive buffer is not available yet.
    //
    if (dma_cmd->recv_addr == NULL)
      break;

    //
    // Use DMA engine to alternately write data buffers and doorbells.
    //
    // To send one data buffer, 3 DMA descriptors are queued: one for the
    // data buffer, one for the receive completion and one for the doorbell
    // write. The send completion processing in gxpci_c2c_send_get_comps()
    // has a dependency on the number of the DMA descriptors used here.
    // If change is made on this regard, gxpci_c2c_send_get_comps() must
    // also be changed.
    //
    // For each buffer, add the receive buffer's offset in the 4KB page to the
    // target PCI address. The packet headroom size is also added if needed.
    //
    uint64_t bus_addr;

    bus_addr = sq_data_addr +
               ((intptr_t)dma_cmd->recv_addr & (HV_TRIO_PAGE_SIZE - 1));

    c2c_dma_to_bus(dma_queue, bus_addr, (void *)dma_cmd->send_addr,
                   dma_cmd->size);

    //
    // Since we use the recv buffer address as the flag for the receive buffer
    // availability, need to reset it once the DMA descriptor is generated.
    //
    dma_cmd->recv_addr = NULL;

    //
    // Write the completion status to the receiver using
    // the DMA engine which ensures that the completion
    // arrives later than the data buffer.
    //
    bus_addr = queue_state->recv_state +
               offsetof(struct gxpci_recv_state, recv_cmds[dma_cmd_index]) +
               offsetof(struct recv_cmd, size);
    c2c_dma_to_bus(dma_queue, bus_addr, (void *)&dma_cmd->size,
                   sizeof(dma_cmd->size));

    //
    // Write the POP bit to have the SQ dequeue the buffer.
    // This must follow the completion write.
    //
    c2c_dma_to_bus(dma_queue, queue_state->doorbell_bus_addr,
                   (void *)&queue_state->doorbell,
                   sizeof(TRIO_MAP_SQ_DOORBELL_FMT_t));
  }

  return;
}

int gxpci_c2c_send_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *queue_state = resource->c2c_send.queue_state;
  uint32_t recv_buf_size = queue_state->recv_buf_size;
  uint32_t pkt_headroom = queue_state->pkt_headroom;

  if (is_send_queue_reset(context))
    return GXPCI_ERESET;

  if (cmd->size == 0)
    return GXPCI_EINVAL;

  if ((queue_state->send_cmds_posted -
       queue_state->send_cmds_consumed) == GXPCI_C2C_MAX_CMDS)
    return GXPCI_ECREDITS;

  // Queue this command.
  uint32_t dma_cmd_index;
  dma_cmd_t *dma_cmd;

  dma_cmd_index = queue_state->send_cmds_posted++ & (GXPCI_C2C_MAX_CMDS - 1);
  dma_cmd = queue_state->dma_cmds + dma_cmd_index;

  dma_cmd->send_addr = cmd->buffer;
  dma_cmd->size = MIN(cmd->size, recv_buf_size - pkt_headroom);

  gxpci_c2c_process_cmds(resource);

  return 0;
}

int gxpci_c2c_recv_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  int sq_index = resource->c2c_recv.scatter_queue;
  struct gxpci_recv_state *queue_state = resource->c2c_recv.queue_state;
  struct gxpci_send_state *send_state = queue_state->send_state;

  if (is_recv_queue_reset(context))
    return GXPCI_ERESET;

  if ((queue_state->recv_cmds_posted -
       queue_state->recv_cmds_consumed) == GXPCI_C2C_MAX_CMDS)
    return GXPCI_ECREDITS;
    
  // Queue this command.
  uint32_t cmd_index;

  cmd_index = queue_state->recv_cmds_posted++ & (GXPCI_C2C_MAX_CMDS - 1);
  queue_state->recv_cmds[cmd_index].recv_addr = cmd->buffer;

  gxio_trio_push_scatter_queue_buffer(trio_context, sq_index, cmd->buffer, 0);

  //
  // Update the sender about this buffer's address.
  //

  //
  // If this is built in 32-bit mode (-m32), we must use 32-bit PIOs.
  //
#ifdef __LP64__
    gxio_trio_write_uint64(&send_state->dma_cmds[cmd_index].recv_addr,
                           (intptr_t)cmd->buffer);
#else
    gxio_trio_write_uint32(&send_state->dma_cmds[cmd_index].recv_addr,
                           (intptr_t)cmd->buffer);
#endif

  return 0;
}

int
gxpci_c2c_send_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue = &resource->c2c_send.dma_queue;
  struct gxpci_send_state *queue_state = resource->c2c_send.queue_state;
  unsigned int hw_counter = queue_state->dma_queue_counter;
  unsigned int ret_completions;
  volatile uint16_t hw_cnt;
  unsigned int comp_index;
  dma_cmd_t *dma_cmd;
  int i;

try_again:

  if (is_send_queue_reset(context))
    return GXPCI_ERESET;

  gxpci_c2c_process_cmds(resource);

  //
  // Note that the following is based on the fact that 3 Push DMA descriptors
  // are queued in order to send one data buffer. Need to change the result
  // type to uint16_t to avoid overflow issue when doing the compare.
  //
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue);
  while ((uint16_t)(hw_cnt -
         (hw_counter & TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK)) > 2)
  {
    hw_counter += 3;
    queue_state->dmas_completed++;
  }

  ret_completions = queue_state->dmas_completed -
                    queue_state->send_cmds_consumed;

  if (ret_completions >= max)
    ret_completions = max;
  else if (ret_completions < min)
    goto try_again;

  for (i = 0; i < ret_completions; i++)
  {
    comp_index = queue_state->send_cmds_consumed++ & (GXPCI_C2C_MAX_CMDS - 1);
    dma_cmd = queue_state->dma_cmds + comp_index;

    cpls[i].buffer = dma_cmd->send_addr;
    cpls[i].size = dma_cmd->size;
  }

  queue_state->dma_queue_counter = hw_counter;

  return ret_completions;
}
 
int
gxpci_c2c_recv_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *queue_state = resource->c2c_recv.queue_state;
  int sq_index = resource->c2c_recv.scatter_queue;
  unsigned int hw_counter = queue_state->sq_comp_counter;
  unsigned int ret_completions;
  volatile uint8_t pop_cnt;
  unsigned int comp_index;
  recv_cmd_t *recv_cmd;
  int i;

try_again:

  if (is_recv_queue_reset(context))
    return GXPCI_ERESET;

  pop_cnt = gxio_trio_read_scatter_queue_pop_count(trio_context, sq_index);
  while ((hw_counter & TRIO_MAP_SQ_REGION_READ_VAL__COMPLETE_COUNT_RMASK) !=
          pop_cnt)
  {
    hw_counter++;
    queue_state->recv_cmds_completed++;
  }

  ret_completions = queue_state->recv_cmds_completed -
                    queue_state->recv_cmds_consumed;

  if (ret_completions >= max)
    ret_completions = max;
  else if (ret_completions < min)
    goto try_again;

  for (i = 0; i < ret_completions; i++)
  {
    comp_index = queue_state->recv_cmds_consumed++ & (GXPCI_C2C_MAX_CMDS - 1);
    recv_cmd = queue_state->recv_cmds + comp_index;

    cpls[i].buffer = recv_cmd->recv_addr;
    cpls[i].size = recv_cmd->size;
  }

  queue_state->sq_comp_counter = hw_counter;

  return ret_completions;
}
 
int gxpci_c2c_send_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_send_state *queue_state = resource->c2c_send.queue_state;

  return GXPCI_C2C_MAX_CMDS -
         (queue_state->send_cmds_posted - queue_state->send_cmds_consumed);
}

int gxpci_c2c_recv_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_recv_state *queue_state = resource->c2c_recv.queue_state;

  return GXPCI_C2C_MAX_CMDS -
         (queue_state->recv_cmds_posted - queue_state->recv_cmds_consumed);
}

int gxpci_c2c_destroy(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  gxpci_queue_type_t type = context->type;

  if (type == GXPCI_C2C_RECV)
  {
    struct gxpci_recv_state *queue_state;

    if (context->local_link_index == 0)
      munmap(resource->c2c_recv.queue_sts, sizeof(struct tlr_c2c_status));
    else
      munmap(context->mmio_reg_base, getpagesize());

    queue_state = (struct gxpci_recv_state *)resource->c2c_recv.queue_state;
    if (queue_state)
      munmap(queue_state->send_state, GXPCI_C2C_QUEUE_MEM_MAP_SIZE);
  }
  else {
    if (context->local_link_index == 0)
      munmap(resource->c2c_send.queue_sts, sizeof(struct tlr_c2c_status));
    else
      munmap(context->mmio_reg_base, getpagesize());
  }

  close(context->fd);

  return 0;
}
