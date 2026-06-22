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

#include <arch/cycle.h>

#include <gxio/common.h>
#include <gxio/trio.h>

#include <gxpci/gxpci.h>
#include <gxpci/host_nic.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <tmc/cpus.h>
#include <tmc/alloc.h>
#include <tmc/task.h>
#include <tmc/interrupt.h>

/**
 * This file implements the core tile/host data transfer engine, allowing
 * the tile user application and the host machine to communicate via a
 * pair of unidirectional streams.  Each side can post buffers
 * to send or receive from a particular stream.  Data is transferred
 * when a send from one side matches a receive from the other.
 */

#if 1
#define GXPCI_TRACE(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define GXPCI_TRACE(...)
#endif

#define HOST_NIC_PUSH_RING_GEN_BIT GXPCI_HOST_NIC_PUSH_DMA_RING_ORD
#define HOST_NIC_PUSH_RING_MASK (GXPCI_HOST_PUSH_DMA_RING_LEN - 1)
#define HOST_NIC_PULL_RING_GEN_BIT GXPCI_HOST_NIC_PULL_DMA_RING_ORD
#define HOST_NIC_PULL_RING_MASK (GXPCI_HOST_PULL_DMA_RING_LEN - 1)

/** Mask of entry number in the host and Tile buffer queues. */
#define PCIE_CMD_QUEUE_ENTRY_MASK (PCIE_CMD_QUEUE_ENTRIES - 1)

static int
gxpci_nic_queue_ready_or_reset(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  volatile uint32_t *loc_ready = &host_regs->queue_status;

  //
  // In case the queue is reset by the peer, we also quit.
  //
  if (*loc_ready == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  if (*loc_ready != GXPCI_HOST_CHAN_READY)
    return -EAGAIN;

  return 0;
}

static inline void
pcie_dma_from_bus(gxio_trio_dma_queue_t *dma_queue, uint64_t bus_addr,
                  uint64_t va, size_t size)
{
  gxio_trio_dma_desc_t *desc_p;
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;

  slot = dma_queue->dma_queue.credits_and_next_index & HOST_NIC_PULL_RING_MASK;
  desc_p = &dma_queue->dma_descs[slot];

  desc_p->io_address = bus_addr;
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >>
          HOST_NIC_PULL_RING_GEN_BIT) ^ 1) & 1;

  //
  // SMOD is one bit below XSIZE.  We set SMOD if size is 16KB.
  //
  xsize = (size << 1) | (size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) =
    va | (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
}

static inline void
pcie_dma_to_bus(gxio_trio_dma_queue_t *dma_queue, uint64_t bus_addr,
                uint64_t va, size_t size)
{
  gxio_trio_dma_desc_t *desc_p;
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;

  slot = dma_queue->dma_queue.credits_and_next_index & HOST_NIC_PUSH_RING_MASK;
  desc_p = &dma_queue->dma_descs[slot];

  desc_p->io_address = bus_addr;
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >>
          HOST_NIC_PUSH_RING_GEN_BIT) ^ 1) & 1;

  //
  // SMOD is one bit below XSIZE.  We set SMOD if size is 16KB.
  //
  xsize = (size << 1) | (size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) =
    va | (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
}

/**
 * Process any new host commands that have been written to the DMA command
 * array. If the DMA command entry has a tile command ready, start the DMA.
 * Otherwise, the DMA will be started later when a tile command is submitted
 * to this DMA command entry.
 */
static void
gxpci_process_host_h2t_cmds(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  uint32_t dma_cmd_index;
  tile_nic_dma_cmd_t *dma_cmd;

  while (1)
  {
    dma_cmd_index = resource->host.dmas_started & PCIE_CMD_QUEUE_ENTRY_MASK;
    dma_cmd = resource->host.dma_cmd_array + dma_cmd_index;

    __insn_prefetch((void *)dma_cmd + 64);

    if (dma_cmd->host_desc.filled && dma_cmd->tile_desc.filled)
    {
      //
      // Both commands are ready and we issue the DMA.
      //
      gxio_trio_dma_queue_t *dma_queue_data;

      dma_cmd->tile_desc.size =
        MIN(dma_cmd->tile_desc.size, dma_cmd->host_desc.size);

      dma_queue_data = &resource->host.pull_dma_queue;
      pcie_dma_from_bus(dma_queue_data, dma_cmd->host_addr,
                        dma_cmd->tile_addr, (size_t)dma_cmd->tile_desc.size);

      //
      // Copy the per-packet VLAN flag and Tag to the tile descriptor.
      //
      dma_cmd->tile_desc.vlan_packet = dma_cmd->host_desc.vlan_packet;
      dma_cmd->tile_desc.csum_vlan_hash = dma_cmd->host_desc.csum_vlan_hash;

      //
      // Copy the per-packet checksum offload flag to the tile descriptor.
      // The per-packet checksum offset and start is already copied above.
      //
      dma_cmd->tile_desc.csum_h2t = dma_cmd->host_desc.csum;

      dma_cmd->host_desc.filled = 0;
      dma_cmd->tile_desc.filled = 0;
      resource->host.dmas_started++;
    }
    else
      break;
  }
}

static void
gxpci_process_host_t2h_cmds(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  uint32_t dma_cmd_index, cmds_to_process;
  tile_nic_dma_cmd_t *dma_cmd;

  //
  // Get the number of host commands to process.
  //
  cmds_to_process = host_regs->cmds_posted_count - resource->host.dmas_started;
  while (cmds_to_process--)
  {
    dma_cmd_index = resource->host.dmas_started & PCIE_CMD_QUEUE_ENTRY_MASK;
    dma_cmd = resource->host.dma_cmd_array + dma_cmd_index;

    __insn_prefetch((void *)dma_cmd + 64);

    //
    // Check valid Tile side commands.
    //
    if (dma_cmd->tile_desc.filled)
    {
      //
      // Both commands are ready and we issue the DMA.
      //
      gxio_trio_dma_queue_t *dma_queue_data;

      dma_cmd->tile_desc.size =
        MIN(dma_cmd->tile_desc.size, resource->host.host_rx_buf_len);

      dma_queue_data = &resource->host.push_dma_queue;
      pcie_dma_to_bus(dma_queue_data, dma_cmd->host_addr,
                      dma_cmd->tile_addr, (size_t)dma_cmd->tile_desc.size);

      dma_cmd->tile_desc.filled = 0;
      resource->host.dmas_started++;
    }
    else
      break;
  }
}

static void
gxpci_host_process_h2t_comps(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs;
  gxio_trio_dma_queue_t *dma_queue_data;
  unsigned int dma_cmds_completed;
  uint16_t new_completions;
  volatile uint16_t hw_cnt;

  host_regs = resource->host.regs;

  dma_queue_data = &resource->host.pull_dma_queue;

  //
  // Check if any data DMAs have been completely processed and if so,
  // generate host interrupt if needed.
  //
  dma_cmds_completed = resource->host.dmas_completed;
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue_data);
  new_completions = (uint16_t)(hw_cnt -
    (dma_cmds_completed & TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK));

  if (new_completions)
  {
    resource->host.dmas_completed += new_completions;

    //
    // Update the host cmds complete counter.
    //
    host_regs->cmds_consumed_count = resource->host.dmas_completed;

    //
    // If this is the first completion(s), set the flag as a reminder that
    // we need to interrupt the host and start the interrupt timer.
    //
    if (resource->host.interrupt_pending == 0)
    {
      resource->host.interrupt_pending = 1;
      resource->host.intr_timer_start = get_cycle_count();
    }
  }

  //
  // Interrupt the host if it is needed and it is enabled, AND if either of the
  // following is true:
  // 1. Timer expires since one packet has been processed after the last
  // interrupt.
  // 2. Enough packets have been received or transmitted since last interrupt,
  // even if the timer has not gone off yet.
  //
  if (resource->host.interrupt_pending && host_regs->interrupt_enable &&
      (get_cycle_count() - resource->host.intr_timer_start >
       GXPCI_HOST_NIC_INTR_TIME_INTERVAL ||
       resource->host.dmas_completed - resource->host.dmas_upon_intr >
       GXPCI_HOST_NIC_INTR_DMA_INTERVAL))
  {
    gxpci_nic_state_t *nic_state = resource->host.nic_state;

    //
    // VFs don't support MSI-X yet.
    //
    if (nic_state->vf_index < 0)
      gxio_trio_trigger_host_interrupt(context->trio_context, context->mac, 1,
                                       *resource->host.msix_addr,
                                       *resource->host.msix_data);
    else
      gxio_trio_trigger_host_interrupt_vf(context->trio_context, context->mac,
                                          1, 0, 0, nic_state->vf_index);

    resource->host.interrupt_pending = 0;
    resource->host.dmas_upon_intr = resource->host.dmas_completed;
  }

  //
  // Process any host commands.
  //
  gxpci_process_host_h2t_cmds(context);
}

static void
gxpci_host_process_t2h_comps(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs;
  gxio_trio_dma_queue_t *dma_queue_data;
  unsigned int dma_cmds_completed;
  uint16_t new_completions;
  volatile uint16_t hw_cnt;

  host_regs = resource->host.regs;

  dma_queue_data = &resource->host.push_dma_queue;

  //
  // Check if any data DMAs have been completely processed and if so,
  // generate host interrupt if needed.
  //
  dma_cmds_completed = resource->host.dmas_completed;
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue_data);
  new_completions = (uint16_t)(hw_cnt -
    (dma_cmds_completed & TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK));

  if (new_completions)
  {
    resource->host.dmas_completed += new_completions;

    //
    // Update the host cmds complete counter.
    //
    host_regs->cmds_consumed_count = resource->host.dmas_completed;

    //
    // If this is the first completion(s), set the flag as a reminder that
    // we need to interrupt the host and start the interrupt timer.
    //
    if(resource->host.interrupt_pending == 0)
    {
      resource->host.interrupt_pending = 1;
      resource->host.intr_timer_start = get_cycle_count();
    }
  }

  //
  // Interrupt the host if it is needed and it is enabled, AND if either of the
  // following is true:
  // 1. Timer expires since one packet has been processed after the last
  // interrupt.
  // 2. Enough packets have been received or transmitted since last interrupt,
  // even if the timer has not gone off yet.
  //
  if (resource->host.interrupt_pending && host_regs->interrupt_enable &&
      (get_cycle_count() - resource->host.intr_timer_start >
       GXPCI_HOST_NIC_INTR_TIME_INTERVAL ||
       resource->host.dmas_completed - resource->host.dmas_upon_intr >
       GXPCI_HOST_NIC_INTR_DMA_INTERVAL)) 
  {
    gxpci_nic_state_t *nic_state = resource->host.nic_state;

    //
    // VFs don't support MSI-X yet.
    //
    if (nic_state->vf_index < 0)
      gxio_trio_trigger_host_interrupt(context->trio_context, context->mac, 1,
                                       *resource->host.msix_addr,
                                       *resource->host.msix_data);
    else
      gxio_trio_trigger_host_interrupt_vf(context->trio_context, context->mac, 
                                          1, 0, 0, nic_state->vf_index);

    resource->host.interrupt_pending = 0;
    resource->host.dmas_upon_intr = resource->host.dmas_completed;
  }

  //
  // Process any host commands.
  //
  gxpci_process_host_t2h_cmds(context);
}

/** NIC-wide initialization function. */
int
gxpci_nic_init(gxio_trio_context_t *trio_context,
               gxpci_nic_state_t *nic_state,
               unsigned int trio_index,
               unsigned int mac,
               int nic_index,
               int asid)
{
  tilegxpci_get_nic_queue_cfg_t queue_cfg;
  unsigned long long local_pci_addr;
  tilegxpci_msix_info_t msix_info;
  tilegxpci_bar_info_t bar_info;
  char device_name[40];
  void *backing_mem;
  int ret;

  if (trio_index >= TILEGX_NUM_TRIO || mac >= TILEGX_TRIO_PCIES)
    return GXPCI_EINVAL;

  nic_state->trio_index = trio_index;
  nic_state->mac = mac;

  //
  // Allocate an ASID if it isn't pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    asid = gxio_trio_alloc_asids(trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }
  nic_state->asid = asid;

  //
  // Get the file handle for NIC channel reset control.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_nic/%d", trio_index, mac, nic_index);
  nic_state->reset_fd = open(device_name, O_RDWR);
  if (nic_state->reset_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  //
  // Retrieve misc host NIC configuration info.
  //
  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem",
           trio_index, mac);

  nic_state->barmem_fd = open(device_name, O_RDWR);
  if (nic_state->barmem_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  queue_cfg.trio_index = trio_index;
  queue_cfg.mac_index = mac;

  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_GET_NIC_QUEUE_CFG, &queue_cfg);
  if (ret < 0)
  {
    fprintf(stderr, "%s GET_NIC_QUEUE_CFG ioctl failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  nic_state->num_nic_ports = queue_cfg.num_ports;
  nic_state->num_nic_tx_queues = queue_cfg.num_tx_queues;
  nic_state->num_nic_rx_queues = queue_cfg.num_rx_queues;

  if (nic_index >= nic_state->num_nic_ports)
    return GXPCI_EINVAL;

  nic_state->nic_index = nic_index;

  //
  // This indicates that this NIC belongs to the PF.
  //
  nic_state->vf_index = -1;

  //
  // Get the MSI-X table info.
  //
  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_GET_MSIX, &msix_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s GET_MSIX ioctl failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  nic_state->msix_table_size = msix_info.msix_vectors * PCI_MSIX_ENTRY_SIZE;
  nic_state->msix_table_base = mmap(NULL, nic_state->msix_table_size, PROT_READ,
                                    MAP_SHARED, nic_state->barmem_fd, 0);
  if (nic_state->msix_table_base == MAP_FAILED)
  {
    nic_state->msix_table_base = NULL;
    return -errno;
  }

  nic_state->host_nic_intr_vec_base = msix_info.msix_host_nic_intr_vec_base;

  //
  // Retrieve the local BAR0 address and size.
  //
  bar_info.link_index = TILEGXPCI_LOCAL_LINK_INDEX;
  bar_info.bar_index = 0;
  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_GET_BAR, &bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_BAR BAR0 failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  nic_state->bar0_addr = bar_info.bar_addr;

  //
  // Retrieve the local BAR2 address.
  //
  bar_info.link_index = TILEGXPCI_LOCAL_LINK_INDEX;
  bar_info.bar_index = 2;
  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_GET_BAR, &bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_BAR BAR2 failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  nic_state->bar2_addr = bar_info.bar_addr;

  //
  // Allocate a huge page which is used for backing the PCI
  // host MMIO registers and storing the DMA descriptors.
  //
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_shared(&alloc);
  tmc_alloc_set_huge(&alloc);
  backing_mem = tmc_alloc_map(&alloc, GXPCI_HOST_QUEUE_BACK_MEM_SIZE);
  if (backing_mem == NULL)
    return -errno;

  ret = gxio_trio_register_page(trio_context, asid, backing_mem,
                                GXPCI_HOST_QUEUE_BACK_MEM_SIZE, 0);
  GXIO_VERIFY_ZERO(ret, "gxio_trio_register_page()");

  nic_state->backing_mem = backing_mem;

  //
  // Set up two Memory Map windows that are shared by all the tx/rx queues.
  // One window is in the BAR0 space, mapping the host NIC queue registers.
  // The other window is in the BAR2 space, mapping the host NIC queue
  // DMA descriptor array.
  //
  int mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  //
  // This is the PCI address that is mapped to the gxpci_host_nic_queue_regs
  // struct which starts at backing_mem.
  //
  local_pci_addr = nic_state->bar0_addr + GXPCI_HOST_NIC_REGS_BAR_OFFSET +
                   nic_index * GXPCI_HOST_NIC_REGS_SIZE;

  ret = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  backing_mem +
                                  GXPCI_HOST_QUEUE_MMIO_REGS_OFFSET,
                                  GXPCI_HOST_NIC_REGS_SIZE,
                                  asid, mac, local_pci_addr,
                                  GXIO_TRIO_ORDER_MODE_UNORDERED);
  GXIO_VERIFY_ZERO(ret, "gxio_trio_init_memory_map()");

  //
  // Now the BAR2 mapping. Note that the Mem Map region requires full
  // PCI address for BAR2 mapping.
  // The host NIC DMA descriptor rings, i.e. struct gxpci_host_nic_desc,
  // are located in the first 16MB of the BAR2 space.
  //
  mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  local_pci_addr = nic_state->bar2_addr +
                   nic_index * GXPCI_HOST_NIC_DMA_RING_SIZE;

  ret = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  backing_mem +
                                  GXPCI_HOST_DMA_DESC_RING_OFFSET,
                                  GXPCI_HOST_NIC_DMA_RING_SIZE,
                                  asid, mac, local_pci_addr,
                                  GXIO_TRIO_ORDER_MODE_UNORDERED);
  GXIO_VERIFY_ZERO(ret, "gxio_trio_init_memory_map()");

  return 0;
}

int
gxpci_nic_init_vf(gxio_trio_context_t *trio_context,
                  gxpci_nic_state_t *nic_state,
                  unsigned int trio_index,
                  unsigned int mac,
                  int vf_index,
                  int asid,
                  void *backing_mem)
{
  tilegxpci_get_nic_queue_cfg_t queue_cfg;
  tilegxpci_bar_info_t bar_info;
  unsigned long bar0_offset;
  unsigned long bar0_size;
  char device_name[40];
  void *bar_mem;
  int ret, i;

  if (trio_index >= TILEGX_NUM_TRIO || mac >= TILEGX_TRIO_PCIES)
    return GXPCI_EINVAL;

  //
  // The ASID must be pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    fprintf(stderr, "Invalid ASID, must be pre-allocated.\n");
    return GXPCI_EINVAL;
  }

  nic_state->trio_index = trio_index;
  nic_state->mac = mac;
  nic_state->asid = asid;
  nic_state->backing_mem = backing_mem;

  //
  // Get the file handle for NIC channel reset control.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/host_nic_vf/%d", trio_index, mac, vf_index);
  nic_state->reset_fd = open(device_name, O_RDWR);
  if (nic_state->reset_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  //
  // Retrieve misc host NIC configuration info.
  //
  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
           trio_index, mac);
  nic_state->barmem_fd = open(device_name, O_RDWR);
  if (nic_state->barmem_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  queue_cfg.trio_index = trio_index;
  queue_cfg.mac_index = mac;

  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_VF_GET_NIC_QUEUE_CFG,
              &queue_cfg);
  if (ret < 0)
  {
    fprintf(stderr, "%s VF_GET_NIC_QUEUE_CFG ioctl failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  nic_state->num_nic_ports = queue_cfg.num_ports;
  nic_state->num_nic_tx_queues = queue_cfg.num_tx_queues;
  nic_state->num_nic_rx_queues = queue_cfg.num_rx_queues;

  nic_state->vf_index = vf_index;
  nic_state->nic_index = 0;

  for (i = 0; i < GXPCI_HOST_NIC_SIMPLEX_QUEUES_VF_MAX; i++)
  {
    nic_state->h2t_dma_queue[i] = -1;
    nic_state->t2h_dma_queue[i] = -1;
  }

  //
  // Retrieve the local VF BAR0 address and size.
  //
  bar_info.link_index = vf_index;
  bar_info.bar_index = 0;
  ret = ioctl(nic_state->barmem_fd, TILEPCI_IOC_GET_VF_BAR, &bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_VF_BAR failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  bar0_size = bar_info.bar_size;

  nic_state->vf_barmem_size = GXPCI_VF_HOST_NIC_MAP_SIZE;

  //
  // This is the BAR0 window that is mapped to the host NIC queue registers
  // and the DMA descriptors in BAR0 that are shared by all the tx/rx queues.
  //
  bar0_offset = vf_index * bar0_size +
                nic_state->nic_index * nic_state->vf_barmem_size;

  bar_mem = mmap(NULL,
                 nic_state->vf_barmem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 nic_state->barmem_fd,
                 bar0_offset);
  if (bar_mem == MAP_FAILED)
    return -errno;

  nic_state->vf_barmem = bar_mem;
  nic_state->nic_regs_vf = (struct gxpci_host_nic_regs_vf *)
                           (bar_mem + GXPCI_VF_HOST_NIC_REG_OFFSET);
  nic_state->nic_desc_vf = (struct gxpci_host_nic_desc_vf *)
                           (bar_mem + GXPCI_VF_HOST_NIC_DESC_OFFSET);

  return 0;
}

static int
gxpci_nic_queue_fixup_vf(gxpci_nic_state_t *nic_state)
{
  //
  // Fixup the VF NIC queue configuration if needed.
  //
  if (nic_state->num_nic_tx_queues !=
      nic_state->nic_regs_vf->num_t2h_queues ||
      nic_state->num_nic_rx_queues !=
      nic_state->nic_regs_vf->num_h2t_queues)
  {
    GXPCI_TRACE("Fixup NIC queue number from %d to %d for VF%d.\n",
                nic_state->num_nic_tx_queues,
                nic_state->nic_regs_vf->num_t2h_queues,
                nic_state->vf_index);

    nic_state->num_nic_tx_queues = nic_state->nic_regs_vf->num_t2h_queues;
    nic_state->num_nic_rx_queues = nic_state->nic_regs_vf->num_h2t_queues;
  }

  return 0;
}

/**
 * NIC-wide complete initialization function, on a VF.
 * Return 0 on success.
 */
int
gxpci_nic_complete_init_vf(gxpci_nic_state_t *nic_state)
{
  volatile uint32_t *num_t2h_queues = &nic_state->nic_regs_vf->num_t2h_queues;
  volatile uint32_t *num_h2t_queues = &nic_state->nic_regs_vf->num_h2t_queues;

  //
  // Wait until VF fills valid queue numbers.
  //
  while (1)
  {
    if (*num_t2h_queues && *num_h2t_queues)
      break;
  }

  return gxpci_nic_queue_fixup_vf(nic_state);
}

/**
 * Non-blocking version of gxpci_nic_complete_init_vf.
 * Return 0 if completed, -EAGAIN otherwise.
 */
int
gxpci_nic_complete_init_vf_nb(gxpci_nic_state_t *nic_state)
{
  volatile uint32_t *num_t2h_queues = &nic_state->nic_regs_vf->num_t2h_queues;
  volatile uint32_t *num_h2t_queues = &nic_state->nic_regs_vf->num_h2t_queues;

  if (*num_t2h_queues == 0 && *num_h2t_queues == 0)
    return -EAGAIN;
      
  return gxpci_nic_queue_fixup_vf(nic_state);
}

static void
gxpci_nic_queue_common_init(gxio_trio_context_t *trio_context,
                            gxpci_nic_state_t *nic_state,
                            gxpci_context_t *context,
                            unsigned int queue_index)
{
  gxpci_resource_t *resource;
  int host_nic_queue_vectors;
  int msix_table_index;

  //
  // Fill in the context structure.
  //
  memset(context, 0, sizeof(*context));
  context->trio_context = trio_context;
  context->trio_index = nic_state->trio_index;
  context->mac = nic_state->mac;
  context->queue_index = queue_index;

  resource = &context->resource;

  resource->host.nic_state = nic_state;

  resource->host.dmas_started = 0;
  resource->host.dmas_completed = 0;
  resource->host.dmas_upon_intr = 0;
  resource->host.tile_cmds_posted = 0;
  resource->host.tile_cmds_consumed = 0;

  //
  // VFs don't support MSI-X yet.
  //
  if (nic_state->vf_index >= 0)
    return;

  host_nic_queue_vectors = MAX(nic_state->num_nic_tx_queues,
                               nic_state->num_nic_rx_queues);
  msix_table_index = nic_state->host_nic_intr_vec_base +
                     (nic_state->nic_index * host_nic_queue_vectors) +
                      queue_index;

  resource->host.msix_addr = (unsigned long *)(nic_state->msix_table_base +
     PCI_MSIX_ENTRY_SIZE * msix_table_index + PCI_MSIX_ENTRY_LOWER_ADDR);

  resource->host.msix_data = (unsigned int *)(nic_state->msix_table_base +
     PCI_MSIX_ENTRY_SIZE * msix_table_index + PCI_MSIX_ENTRY_DATA);
}

static int
gxpci_nic_queue_complete_init(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  gxpci_queue_type_t queue_type = context->type;
  volatile uint32_t *loc_ready = &host_regs->queue_status;

  //
  // The TILE side waits for the host to enter the ready state before
  // starting any data transfer.
  //
  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    //
    // In case the queue is reset by the peer, we also quit.
    //
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Get host receive buffer length for T2H queue.
  //
  if (queue_type == GXPCI_NIC_T2H)
    resource->host.host_rx_buf_len = host_regs->host_rx_buf_len;

  return 0;
}

static int
gxpci_nic_queue_complete_init_vf(gxpci_context_t *context,
                                 gxpci_nic_state_t *nic_state)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  gxpci_queue_type_t queue_type = context->type;
  volatile uint32_t *loc_ready = &host_regs->queue_status;
  struct gxpci_host_nic_regs_vf *nic_regs;
  int err;

  //
  // The TILE side waits for the host to enter the ready state before
  // starting any data transfer.
  //
  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    //
    // In case the queue is reset by the peer, we also quit.
    //
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // Get host receive buffer length for T2H queue.
  //
  if (queue_type == GXPCI_NIC_T2H)
    resource->host.host_rx_buf_len = host_regs->host_rx_buf_len;

  //
  // Activate the queue.
  //
  nic_regs = nic_state->nic_regs_vf;
  err = ioctl(nic_state->reset_fd, TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE,
              nic_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE ioctl failure: %s\n",
            strerror(errno));
    return -errno;
  }

  return 0;
}

/** NIC queue-specific initialization function. */
int
gxpci_nic_t2h_queue_init(gxio_trio_context_t *trio_context,
                         gxpci_nic_state_t *nic_state,
                         gxpci_context_t *context,
                         unsigned int queue_index)
{
  void *backing_mem = nic_state->backing_mem;
  struct gxpci_host_nic_regs *nic_regs;
  struct gxpci_host_nic_desc *nic_desc;
  gxio_trio_dma_queue_t *dma_queue;
  gxpci_resource_t *resource;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= nic_state->num_nic_rx_queues)
    return GXPCI_EINVAL;

  gxpci_nic_queue_common_init(trio_context, nic_state, context, queue_index);

  context->type = GXPCI_NIC_T2H;

  nic_regs = (struct gxpci_host_nic_regs *)
    (backing_mem + GXPCI_HOST_QUEUE_MMIO_REGS_OFFSET);
  nic_desc = (struct gxpci_host_nic_desc *)
    (backing_mem + GXPCI_HOST_DMA_DESC_RING_OFFSET);

  resource = &context->resource;
  resource->host.regs = &nic_regs->t2h_regs[queue_index];
  resource->host.dma_cmd_array = nic_desc->t2h_desc[queue_index].dma_cmds;

  //
  // Allocate and initialize a push DMA ring for tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PUSH_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem + GXPCI_HOST_DATA_PUSH_DMA_CMDS_BUF_OFFSET +
                 queue_index * dma_ring_size;
  dma_queue = &resource->host.push_dma_queue;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring,
                                      nic_state->mac, nic_state->asid,
                                      0, dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready.
  //
  volatile uint32_t *loc_ready = &resource->host.regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  return 0;
}

/** NIC queue-specific complete initialization function. */
int
gxpci_nic_t2h_queue_complete_init(gxpci_context_t *context)
{
  return gxpci_nic_queue_complete_init(context);
}

/**
 * Non-blocking version of gxpci_nic_t2h_queue_complete_init.
 * Return 0 if completed, -EAGAIN if incompletion, GXPCI_ERESET if reset.
 */
int
gxpci_nic_t2h_queue_complete_init_nb(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  int err;

  err = gxpci_nic_queue_ready_or_reset(context);
  if (err < 0)
    return err;

  //
  // Get host receive buffer length for T2H queue.
  //
  resource->host.host_rx_buf_len = host_regs->host_rx_buf_len;

  return 0;
}

int
gxpci_nic_h2t_queue_init(gxio_trio_context_t *trio_context,
                         gxpci_nic_state_t *nic_state,
                         gxpci_context_t *context,
                         unsigned int queue_index)
{
  void *backing_mem = nic_state->backing_mem;
  struct gxpci_host_nic_regs *nic_regs;
  struct gxpci_host_nic_desc *nic_desc;
  gxio_trio_dma_queue_t *dma_queue;
  gxpci_resource_t *resource;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= nic_state->num_nic_tx_queues)
    return GXPCI_EINVAL;

  gxpci_nic_queue_common_init(trio_context, nic_state, context, queue_index);

  context->type = GXPCI_NIC_H2T;

  nic_regs = (struct gxpci_host_nic_regs *)
    (backing_mem + GXPCI_HOST_QUEUE_MMIO_REGS_OFFSET);
  nic_desc = (struct gxpci_host_nic_desc *)
    (backing_mem + GXPCI_HOST_DMA_DESC_RING_OFFSET);

  resource = &context->resource;
  resource->host.regs = &nic_regs->h2t_regs[queue_index];
  resource->host.dma_cmd_array = nic_desc->h2t_desc[queue_index].dma_cmds;

  //
  // Allocate and initialize a pull DMA ring for fetching data from the host.
  //
  dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PULL_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem + GXPCI_HOST_DATA_PULL_DMA_CMDS_BUF_OFFSET +
                 queue_index * dma_ring_size;
  dma_queue = &resource->host.pull_dma_queue;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring,
                                      nic_state->mac, nic_state->asid,
                                      0, dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready.
  //
  volatile uint32_t *loc_ready = &resource->host.regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;
 
  return 0;
}

/** NIC queue-specific complete initialization function. */
int
gxpci_nic_h2t_queue_complete_init(gxpci_context_t *context)
{
  return gxpci_nic_queue_complete_init(context);
}

/**
 * Non-blocking version of gxpci_nic_h2t_queue_complete_init.
 * Return 0 if completed, -EAGAIN if incompletion, GXPCI_ERESET if reset.
 */
int
gxpci_nic_h2t_queue_complete_init_nb(gxpci_context_t *context)
{
  int err;

  err = gxpci_nic_queue_ready_or_reset(context);
  if (err < 0)
    return err;

  return 0;
}

/** NIC queue-specific initialization function for VFs. */
int
gxpci_nic_t2h_queue_init_vf(gxio_trio_context_t *trio_context,
                            gxpci_nic_state_t *nic_state,
                            gxpci_context_t *context,
                            unsigned int queue_index)
{
  void *backing_mem = nic_state->backing_mem;
  gxio_trio_dma_queue_t *dma_queue;
  gxpci_resource_t *resource;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= nic_state->num_nic_rx_queues)
    return GXPCI_EINVAL;

  gxpci_nic_queue_common_init(trio_context, nic_state, context, queue_index);

  context->type = GXPCI_NIC_T2H;

  resource = &context->resource;
  resource->host.regs = &nic_state->nic_regs_vf->t2h_regs[queue_index];
  resource->host.dma_cmd_array =
    nic_state->nic_desc_vf->t2h_desc[queue_index].dma_cmds;

  //
  // Allocate and initialize a push DMA ring for tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PUSH_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem +
                 queue_index * sizeof(gxio_trio_dma_desc_t) *
                 (GXPCI_HOST_PUSH_DMA_RING_LEN + GXPCI_HOST_PULL_DMA_RING_LEN);
  dma_queue = &resource->host.push_dma_queue;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring,
                                      nic_state->mac, nic_state->asid,
                                      HV_TRIO_FLAG_VFUNC(nic_state->vf_index),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  nic_state->t2h_dma_queue[queue_index] = dma_ring;

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready.
  //
  volatile uint32_t *loc_ready = &resource->host.regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  return 0;
}

/** NIC queue-specific complete initialization function. */
int
gxpci_nic_t2h_queue_complete_init_vf(gxpci_context_t *context,
                                     gxpci_nic_state_t *nic_state)
{
  return gxpci_nic_queue_complete_init_vf(context, nic_state);
}

/**
 * Non-blocking version of gxpci_nic_t2h_queue_complete_init_vf.
 * Return 0 if completed, -EAGAIN if incompletion, GXPCI_ERESET if reset.
 */
int
gxpci_nic_t2h_queue_complete_init_vf_nb(gxpci_context_t *context,
                                        gxpci_nic_state_t *nic_state)
{
  gxpci_resource_t *resource = &context->resource;
  struct gxpci_host_nic_queue_regs *host_regs = resource->host.regs;
  struct gxpci_host_nic_regs_vf *nic_regs;
  int err;

  err = gxpci_nic_queue_ready_or_reset(context);
  if (err < 0)
    return err;

  //
  // Get host receive buffer length for T2H queue.
  //
  resource->host.host_rx_buf_len = host_regs->host_rx_buf_len;

  //
  // Activate the queue.
  //
  nic_regs = nic_state->nic_regs_vf;
  err = ioctl(nic_state->reset_fd, TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE,
              nic_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE ioctl failure: %s\n",
            strerror(errno));
    return -errno;
  }

  return 0;
}

int
gxpci_nic_h2t_queue_init_vf(gxio_trio_context_t *trio_context,
                            gxpci_nic_state_t *nic_state,
                            gxpci_context_t *context,
                            unsigned int queue_index)
{
  void *backing_mem = nic_state->backing_mem;
  gxio_trio_dma_queue_t *dma_queue;
  gxpci_resource_t *resource;
  size_t dma_ring_size;
  void *dma_ring_mem; 
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= nic_state->num_nic_tx_queues)
    return GXPCI_EINVAL;

  gxpci_nic_queue_common_init(trio_context, nic_state, context, queue_index);

  context->type = GXPCI_NIC_H2T;

  resource = &context->resource;
  resource->host.regs = &nic_state->nic_regs_vf->h2t_regs[queue_index];
  resource->host.dma_cmd_array =
    nic_state->nic_desc_vf->h2t_desc[queue_index].dma_cmds;

  //
  // Allocate and initialize a pull DMA ring for fetching data from the host.
  //
  dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_HOST_PULL_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = backing_mem +
                 GXPCI_HOST_PUSH_DMA_RING_LEN * sizeof(gxio_trio_dma_desc_t) +
                 queue_index * sizeof(gxio_trio_dma_desc_t) *
                 (GXPCI_HOST_PUSH_DMA_RING_LEN + GXPCI_HOST_PULL_DMA_RING_LEN);
  dma_queue = &resource->host.pull_dma_queue;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring,
                                      nic_state->mac, nic_state->asid,
                                      HV_TRIO_FLAG_VFUNC(nic_state->vf_index),
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  nic_state->h2t_dma_queue[queue_index] = dma_ring;

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready.
  //
  volatile uint32_t *loc_ready = &resource->host.regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;
 
  return 0;
}

/** NIC queue-specific complete initialization function. */
int
gxpci_nic_h2t_queue_complete_init_vf(gxpci_context_t *context,
                                     gxpci_nic_state_t *nic_state)
{
  return gxpci_nic_queue_complete_init_vf(context, nic_state);
}

/**
 * Non-blocking version of gxpci_nic_h2t_queue_complete_init_vf.
 * Return 0 if completed, -EAGAIN if incompletion, GXPCI_ERESET if reset.
 */
int
gxpci_nic_h2t_queue_complete_init_vf_nb(gxpci_context_t *context,
                                        gxpci_nic_state_t *nic_state)
{
  struct gxpci_host_nic_regs_vf *nic_regs;
  int err;

  err = gxpci_nic_queue_ready_or_reset(context);
  if (err < 0)
    return err;

  //
  // Activate the queue.
  //
  nic_regs = nic_state->nic_regs_vf;
  err = ioctl(nic_state->reset_fd, TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE,
              nic_regs->queue_sts_array_bus_addr);
  if (err < 0)
  {
    fprintf(stderr, "TILEPCI_IOC_VF_ACTIVATE_NIC_QUEUE ioctl failure: %s\n",
            strerror(errno));
    return -errno;
  }

  return 0;
}

int
gxpci_netlib_open_ctrl_link(gxpci_netlib_nic_context_t *context)
{
  gxpci_nic_state_t *nic_state = context->nic_state;
  void* backing_mem = nic_state->backing_mem;

  //
  // Reuse the BAR0 Memory Map window for netlib registers.
  //
  context->netlib_regs = backing_mem + GXPCI_HOST_QUEUE_MMIO_REGS_OFFSET +
                         sizeof(struct gxpci_host_nic_regs);

  return 0;
}

int
gxpci_nic_h2t_cmds(gxpci_context_t *context, const gxpci_cmd_t *cmd,
                   unsigned int count)
{
  gxpci_resource_t *resource = &context->resource;
  tile_nic_dma_cmd_t *dma_cmd;
  uint32_t dma_cmd_index;

  if (__builtin_expect(resource->host.regs->queue_status == GXPCI_CHAN_RESET ||
                       resource->host.regs->queue_status !=
                       GXPCI_HOST_CHAN_READY, 0))
    return GXPCI_ERESET;

  for (; count; count--, cmd++)
  {
    //
    // Queue this command.
    //
    dma_cmd_index = resource->host.tile_cmds_posted++ & PCIE_CMD_QUEUE_ENTRY_MASK;
    dma_cmd = resource->host.dma_cmd_array + dma_cmd_index;

    dma_cmd->tile_addr = (intptr_t)cmd->buffer;
    dma_cmd->tile_desc.size = cmd->size;
    dma_cmd->tile_desc.filled = 1;
  }

  //
  // Process any host commands.
  //
  gxpci_process_host_h2t_cmds(context);

  return 0;
}

int
gxpci_nic_t2h_cmds(gxpci_context_t *context, const gxpci_cmd_t *cmd,
                   unsigned int count)
{
  gxpci_resource_t *resource = &context->resource;
  tile_nic_dma_cmd_t *dma_cmd;
  uint32_t dma_cmd_index;

  if (__builtin_expect(resource->host.regs->queue_status == GXPCI_CHAN_RESET ||
                       resource->host.regs->queue_status !=
                       GXPCI_HOST_CHAN_READY, 0))
    return GXPCI_ERESET;

  for (; count; count--, cmd++)
  {
    //
    // Queue this command.
    //
    dma_cmd_index = resource->host.tile_cmds_posted++ & PCIE_CMD_QUEUE_ENTRY_MASK;
    dma_cmd = resource->host.dma_cmd_array + dma_cmd_index;

    dma_cmd->tile_addr = (intptr_t)cmd->buffer;
    dma_cmd->tile_desc.size = cmd->size;
    dma_cmd->tile_desc.filled = 1;
  }

  //
  // Process any host commands.
  //
  gxpci_process_host_t2h_cmds(context);

  return 0;
}

int
gxpci_nic_get_comps(gxpci_context_t *context, gxpci_nic_comp_t *cpls,
                    int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  gxpci_queue_type_t queue_type = context->type;
  uint32_t ret_completions;
  uint32_t dma_cmd_index;
  tile_nic_dma_cmd_t *dma_cmd;
  uint32_t i;

try_again:
  if (__builtin_expect(resource->host.regs->queue_status == GXPCI_CHAN_RESET,
                       0))
    return GXPCI_ERESET;

  if (queue_type == GXPCI_NIC_H2T)
    gxpci_host_process_h2t_comps(context);
  else
    gxpci_host_process_t2h_comps(context);

  ret_completions = resource->host.dmas_completed -
                    resource->host.tile_cmds_consumed;

  if (ret_completions >= max)
    ret_completions = max;
  else if (ret_completions < min)
    goto try_again;

  for (i = 0; i < ret_completions; i++)
  {
    dma_cmd_index = resource->host.tile_cmds_consumed++ &
                    PCIE_CMD_QUEUE_ENTRY_MASK;
    dma_cmd = resource->host.dma_cmd_array + dma_cmd_index;

    cpls[i].buffer = (void *)((uintptr_t)dma_cmd->tile_addr);
    cpls[i].desc = dma_cmd->tile_desc;
  }

  return ret_completions;
}

int gxpci_nic_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  uint32_t cmd_slots;

  cmd_slots = PCIE_CMD_QUEUE_ENTRIES -
    (resource->host.tile_cmds_posted - resource->host.tile_cmds_consumed);

  return cmd_slots;
}

int gxpci_nic_destroy_vf(gxio_trio_context_t *trio_context,
                         gxpci_nic_state_t *nic_state)
{
  int i;

  //
  // Free TRIO DMA resource if it has been allocated.
  //
  for (i = 0; i < GXPCI_HOST_NIC_SIMPLEX_QUEUES_VF_MAX; i++)
  {
    if (nic_state->h2t_dma_queue[i] != -1)
      gxio_trio_free_pull_dma_ring(trio_context, nic_state->h2t_dma_queue[i]); 

    if (nic_state->t2h_dma_queue[i] != -1)
      gxio_trio_free_push_dma_ring(trio_context, nic_state->t2h_dma_queue[i]);
  }

  close(nic_state->reset_fd);

  if (nic_state->vf_barmem)
    munmap(nic_state->vf_barmem, nic_state->vf_barmem_size);

  close(nic_state->barmem_fd);

  return 0;
}

int gxpci_nic_destroy(gxio_trio_context_t *trio_context,
                      gxpci_nic_state_t *nic_state)
{
  close(nic_state->reset_fd);

  if (nic_state->msix_table_base)
    munmap(nic_state->msix_table_base, nic_state->msix_table_size);

  if (nic_state->vf_barmem)
    munmap(nic_state->vf_barmem, nic_state->vf_barmem_size);

  close(nic_state->barmem_fd);

  gxio_trio_destroy(trio_context);

  return 0;
}
