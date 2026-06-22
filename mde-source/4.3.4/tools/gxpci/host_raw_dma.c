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
#include <gxio/trio.h>
#include <tmc/cpus.h>
#include <tmc/alloc.h>
#include <tmc/task.h>

#include <gxpci/gxpci.h>
#include <asm/tilegxpci.h>

/**
 * This file implements the API to issue raw DMA commands that transfer
 * data to and from a remote PCI device, using the target PCI bus addresses.
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
 * check on each command that is added. Enabling this degrades performance so
 * it should only be used for development purposes.
 */
#if 0
#define ENABLE_CMD_SIZE_QSTS_CHECKS
#endif

#define RD_PUSH_RING_GEN_BIT GXPCI_RAW_DMA_PUSH_DMA_RING_ORD
#define RD_PUSH_RING_MASK (GXPCI_RAW_DMA_PUSH_DMA_RING_LEN - 1)
#define RD_PUSH_RING_CRED_MASK ((GXPCI_RAW_DMA_PUSH_DMA_RING_LEN << 1) - 1)
#define RD_PULL_RING_GEN_BIT GXPCI_RAW_DMA_PULL_DMA_RING_ORD
#define RD_PULL_RING_MASK (GXPCI_RAW_DMA_PULL_DMA_RING_LEN - 1)
#define RD_PULL_RING_CRED_MASK ((GXPCI_RAW_DMA_PULL_DMA_RING_LEN << 1) - 1)


/**
 * Update the number of credits based on how many push DMAs have completed,
 * and send interrupts to host if enabled.
 */
uint32_t gxpci_rd_t2h_update_counters(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue_data = &resource->raw_dma.dma_queue_data;
  gxpci_raw_dma_state_t *rd_state = resource->raw_dma.rd_state;
  struct gxpci_host_rd_regs_drv *host_regs = resource->raw_dma.drv_regs;
  unsigned int old_completions = resource->raw_dma.dma_cpl_cnt;
  uint16_t hw_cnt;
  int credits;

  //
  // Check the Push DMA complete count (MMIO read).
  //
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue_data);

  //
  // Update the DMA complete counter.  Bump upper 16 bits if hw_cnt has
  // wrapped.
  //
  if (hw_cnt <
     (resource->raw_dma.dma_cpl_cnt & TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK))
  {
    resource->raw_dma.dma_cpl_cnt =
      (resource->raw_dma.dma_cpl_cnt & ~TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK) +
      (TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK + 1) + hw_cnt;
  }
  else
  {
    resource->raw_dma.dma_cpl_cnt =
      (resource->raw_dma.dma_cpl_cnt & ~TRIO_PUSH_DMA_REGION_VAL__COUNT_RMASK) +
      hw_cnt;
  }

  //
  // Credit is based on how much we've sent to the local ring and how much 
  // has been completely processed, including returning completions to the user.
  // The local ring pointer is just the low bits of the credits_and_next_index 
  // member of the dma queue.
  //
  credits = GXPCI_RAW_DMA_PUSH_DMA_RING_LEN - 
    (((dma_queue_data->dma_queue.credits_and_next_index &
      RD_PUSH_RING_CRED_MASK) - 
      (context->completed & RD_PUSH_RING_CRED_MASK)) & RD_PUSH_RING_CRED_MASK);

  context->credits = credits;

  if (rd_state->rd_q_intr_vec_base < 0)
    return credits;

  if (old_completions != resource->raw_dma.dma_cpl_cnt)
  {
    //
    // If this is the first completion(s), set the flag as a reminder that
    // we need to interrupt the host.
    //
    if (resource->raw_dma.interrupt_pending == 0)
      resource->raw_dma.interrupt_pending = 1;
  }

  //
  // Interrupt the host if it is needed and it is enabled.
  //
  if (resource->raw_dma.interrupt_pending && host_regs->interrupt_enable)
  {
    gxio_trio_trigger_host_interrupt(context->trio_context, context->mac, 1,
                                     *resource->raw_dma.msix_addr,
                                     *resource->raw_dma.msix_data);
    resource->raw_dma.interrupt_pending = 0;
  }

  return credits;
}

/**
 * Update the number of credits based on how many pull DMAs have completed.
 */
uint32_t gxpci_rd_h2t_update_counters(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue_data;
  uint16_t hw_cnt;
  int credits;

  dma_queue_data = &resource->raw_dma.dma_queue_data;

  //
  // Check the Pull DMA complete count (MMIO read).
  //
  hw_cnt = gxio_trio_read_dma_queue_complete_count(dma_queue_data);

  //
  // Update the DMA complete counter.  Bump upper 16 bits if hw_cnt has
  // wrapped.
  //
  if (hw_cnt <
     (resource->raw_dma.dma_cpl_cnt & TRIO_PULL_DMA_REGION_VAL__COUNT_RMASK))
  {
    resource->raw_dma.dma_cpl_cnt =
      (resource->raw_dma.dma_cpl_cnt & ~TRIO_PULL_DMA_REGION_VAL__COUNT_RMASK) +
      (TRIO_PULL_DMA_REGION_VAL__COUNT_RMASK + 1) + hw_cnt;
  }
  else
  {
    resource->raw_dma.dma_cpl_cnt =
      (resource->raw_dma.dma_cpl_cnt & ~TRIO_PULL_DMA_REGION_VAL__COUNT_RMASK) +
      hw_cnt;
  }

  //
  // Credit is based on how much we've sent to the local ring and how much
  // has been completely processed, including returning completions to the user.
  // The local ring pointer is just the low bits of the credits_and_next_index
  // member of the dma queue.
  //
  credits = GXPCI_RAW_DMA_PULL_DMA_RING_LEN -
    (((dma_queue_data->dma_queue.credits_and_next_index &
      RD_PULL_RING_CRED_MASK) -
      (context->completed & RD_PULL_RING_CRED_MASK)) & RD_PULL_RING_CRED_MASK);

  context->credits = credits;

  return credits;
}

extern int 
get_bar_addr(gxpci_context_t *context, int local,
             tilegxpci_bar_info_t *bar_info);

static void *
gxpci_alloc_hugepage(gxio_trio_context_t *trio_context, int asid)
{
  void *backing_mem;
  size_t size;
  int ret;

  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_shared(&alloc);
  tmc_alloc_set_huge(&alloc);
  size = tmc_alloc_get_huge_pagesize();
  backing_mem = tmc_alloc_map(&alloc, size);
  if (backing_mem == NULL)
    return NULL;

  ret = gxio_trio_register_page(trio_context, asid, backing_mem, size, 0);
  if (ret)
    return NULL;

  return backing_mem;
}

/* Raw DMA channel-wide initialization function. */
int
gxpci_raw_dma_init(gxio_trio_context_t *trio_context,
                   gxpci_raw_dma_state_t *rd_state,
                   unsigned int trio_index,
                   unsigned int mac,
                   int asid)
{
  tilegxpci_get_rd_queue_cfg_t queue_cfg;
  unsigned long long local_pci_addr;
  tilegxpci_msix_info_t msix_info;
  tilegxpci_bar_info_t bar_info;
  char device_name[40];
  void *backing_mem;
  int ret;

  if (trio_index >= TILEGX_NUM_TRIO || mac >= TILEGX_TRIO_PCIES)
    return GXPCI_EINVAL;

  rd_state->trio_index = trio_index;
  rd_state->mac = mac;

  //
  // Allocate an ASID if it isn't pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    asid = gxio_trio_alloc_asids(trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }
  rd_state->asid = asid;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem",
           trio_index, mac);

  rd_state->barmem_fd = open(device_name, O_RDWR);
  if (rd_state->barmem_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  queue_cfg.trio_index = trio_index;
  queue_cfg.mac_index = mac;

  ret = ioctl(rd_state->barmem_fd, TILEPCI_IOC_GET_RD_QUEUE_CFG, &queue_cfg);
  if (ret < 0)
  {
    fprintf(stderr, "%s GET_RD_QUEUE_CFG ioctl failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  rd_state->num_rd_t2h_queues = queue_cfg.num_t2h_queues;
  rd_state->num_rd_h2t_queues = queue_cfg.num_h2t_queues;

  //
  // Retrieve the MSI-X table info.
  //
  ret = ioctl(rd_state->barmem_fd, TILEPCI_IOC_GET_MSIX, &msix_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_MSIX failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }
  
  rd_state->msix_table_size = msix_info.msix_vectors * PCI_MSIX_ENTRY_SIZE;
  rd_state->msix_table_base = mmap(NULL, rd_state->msix_table_size, PROT_READ,
                                   MAP_SHARED, rd_state->barmem_fd, 0);
  if (rd_state->msix_table_base == MAP_FAILED)
  {
    rd_state->msix_table_base = NULL;
    return -errno;
  }

  //
  // If the endpoint driver does not enable Raw DMA interrupt support by
  // applying the kernel module parameter 'enable_rd_interrupt=1', the return
  // value of msix_info.msix_rd_q_intr_vec_base will be -1.
  //
  rd_state->rd_q_intr_vec_base = msix_info.msix_rd_q_intr_vec_base;
  if (rd_state->rd_q_intr_vec_base < 0)
    printf("%s: Raw DMA interrupt support can be enabled by applying "
           "'enable_rd_interrupt=1' to gxpci_endp driver if needed.\n",
           device_name);

  //
  // Retrieve the local BAR0 address and size.
  //
  bar_info.link_index = TILEGXPCI_LOCAL_LINK_INDEX;
  bar_info.bar_index = 0;
  ret = ioctl(rd_state->barmem_fd, TILEPCI_IOC_GET_BAR, &bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_BAR BAR0 failure: %s\n", device_name,
            strerror(errno));
    return -errno;
  }

  rd_state->bar0_addr = bar_info.bar_addr;
  rd_state->bar0_size = bar_info.bar_size;

  //
  // Depend on the Push and Pull DMA ring sizes, one or more huge pages
  // are allocated to back up host MMIO registers and store the DMA
  // descriptors which will use separate huge pages if the ring is > 8K.
  // A 8K-deep ring takes 128KB, 2MB for 16 queues.
  //
  backing_mem = gxpci_alloc_hugepage(trio_context, asid);
  if (backing_mem == NULL)
    return -errno;

  //
  // The MMIO registers take the first 1MB of the huge page.
  // We assume that both Pull and Push DMA rings are 8K or shorter,
  // starting at page offset 1MB and 8MB respectively.
  //
  rd_state->mmio_mem = backing_mem;
  rd_state->h2t_dma_mem = backing_mem + (1 << 20);
  rd_state->t2h_dma_mem = backing_mem + (1 << 23);

  //
  // If either ring is deeper than 8K, alloc a dedicated page.
  //
  if (GXPCI_RAW_DMA_PULL_DMA_RING_ORD > 13)
  {
    backing_mem = gxpci_alloc_hugepage(trio_context, asid);
    if (backing_mem == NULL)
      return -errno;

    rd_state->h2t_dma_mem = backing_mem;
  }

  if (GXPCI_RAW_DMA_PUSH_DMA_RING_ORD > 13)
  {
    backing_mem = gxpci_alloc_hugepage(trio_context, asid);
    if (backing_mem == NULL)
      return -errno;

    rd_state->t2h_dma_mem = backing_mem;
  }

  //
  // Set up Memory Map window in BAR0 to map the Raw DMA control registers
  // for all the t2h/h2t queues.
  //
  int mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

  //
  // This is the PCI address that is mapped to the gxpci_host_rd_regs_drv 
  // structs.
  //
  local_pci_addr = rd_state->bar0_addr + GXPCI_HOST_RD_H2T_REGS_OFFSET;

  ret = gxio_trio_init_memory_map(trio_context, mem_map_index,
                                  rd_state->mmio_mem,
                                  2 * GXPCI_RAW_DMA_MAX_QUEUE_NUM *
                                  (GXPCI_HOST_RD_REGS_MAP_SIZE +
                                  GXPCI_HOST_RD_REG_APP_MAP_SIZE),
                                  asid, mac, local_pci_addr,
                                  GXIO_TRIO_ORDER_MODE_STRICT);
  GXIO_VERIFY_ZERO(ret, "gxio_trio_init_memory_map()");

  return 0;
}

static void
gxpci_rd_queue_common_init(gxio_trio_context_t *trio_context,
                           gxpci_raw_dma_state_t *rd_state,
                           gxpci_context_t *context,
                           unsigned int queue_index)
{
  gxpci_resource_t *resource;
  int msix_table_index;

  //
  // Fill in the context structure.
  //
  memset(context, 0, sizeof(*context));
  context->trio_context = trio_context;
  context->trio_index = rd_state->trio_index;
  context->mac = rd_state->mac;
  context->queue_index = queue_index;
  context->completed = 0;

  resource = &context->resource;

  resource->raw_dma.rd_state = rd_state;

  resource->raw_dma.dma_cpl_cnt = 0;

  if (rd_state->rd_q_intr_vec_base < 0)
    return;

  msix_table_index = rd_state->rd_q_intr_vec_base + queue_index;

  resource->raw_dma.msix_addr = (unsigned long *)(rd_state->msix_table_base +
     PCI_MSIX_ENTRY_SIZE * msix_table_index + PCI_MSIX_ENTRY_LOWER_ADDR);

  resource->raw_dma.msix_data = (unsigned int *)(rd_state->msix_table_base +
     PCI_MSIX_ENTRY_SIZE * msix_table_index + PCI_MSIX_ENTRY_DATA);
}

int
gxpci_open_raw_dma_recv_queue(gxio_trio_context_t *trio_context,
                              gxpci_raw_dma_state_t *rd_state,
                              gxpci_context_t *context,
                              unsigned int queue_index)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  size_t dma_ring_size;
  void *dma_ring_mem; 
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= rd_state->num_rd_h2t_queues)
    return GXPCI_EINVAL;

  gxpci_rd_queue_common_init(trio_context, rd_state, context, queue_index);

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/raw_dma/recv/%d",
           context->trio_index, context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  resource->raw_dma.drv_regs =(struct gxpci_host_rd_regs_drv *)
                              (rd_state->mmio_mem + queue_index *
                              (GXPCI_HOST_RD_REGS_MAP_SIZE +
                              GXPCI_HOST_RD_REG_APP_MAP_SIZE));
  resource->raw_dma.app_regs =(struct gxpci_host_rd_regs_app *)
                              (rd_state->mmio_mem + queue_index *
                              (GXPCI_HOST_RD_REGS_MAP_SIZE +
                              GXPCI_HOST_RD_REG_APP_MAP_SIZE) +
                              GXPCI_HOST_RD_REGS_MAP_SIZE);

  //
  // Allocate and initialize a pull DMA ring for the data.
  //
  dma_ring = gxio_trio_alloc_pull_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_pull_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_RAW_DMA_PULL_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = rd_state->h2t_dma_mem + queue_index * dma_ring_size;
  memset(dma_ring_mem, 0, dma_ring_size);
  dma_queue = &resource->raw_dma.dma_queue_data;
  err = gxio_trio_init_pull_dma_queue(dma_queue, trio_context, dma_ring,
                                      rd_state->mac, rd_state->asid, 0,
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_pull_dma_queue()");

  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->raw_dma.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    //
    // In case the queue is reset by the peer, we also quit.
    //
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host application is ready and should have initialized the registers
  // in struct gxpci_host_rd_regs_drv that records the the PCI addresses and
  // size info of the host Raw DMA buffer.
  //
#ifdef RAW_DMA_USE_RESERVED_MEMORY
  resource->raw_dma.rd_buf_addr = resource->raw_dma.drv_regs->rd_buf_bus_addr;
#else
  for (int i = 0; i < HOST_RD_SEGMENT_MAX_NUM; i++)
    resource->raw_dma.rd_segment_addr[i] =
      resource->raw_dma.drv_regs->segment_bus_addr[i];
#endif

  resource->raw_dma.rd_buf_size = resource->raw_dma.drv_regs->rd_buf_size;
  assert(resource->raw_dma.rd_buf_size > 0);

  context->credits = GXPCI_RAW_DMA_PULL_DMA_RING_LEN;

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_RAW_DMA);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_RAW_DMA ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  return 0;
}

int
gxpci_open_raw_dma_send_queue(gxio_trio_context_t *trio_context,
                              gxpci_raw_dma_state_t *rd_state,
                              gxpci_context_t *context,
                              unsigned int queue_index)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t *dma_queue;
  char device_name[40];
  size_t dma_ring_size;
  void *dma_ring_mem;
  int dma_ring;
  int err;

  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  if (queue_index >= rd_state->num_rd_t2h_queues)
    return GXPCI_EINVAL;

  gxpci_rd_queue_common_init(trio_context, rd_state, context, queue_index);

  //
  // Get the file handle for queue status monitoring.
  //
  snprintf(device_name, sizeof(device_name),
           "/dev/trio%d-mac%d/raw_dma/send/%d",
           context->trio_index, context->mac, context->queue_index);
  context->fd = open(device_name, O_RDWR);
  if (context->fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  resource->raw_dma.drv_regs =(struct gxpci_host_rd_regs_drv *)
                              (rd_state->mmio_mem +
                              (GXPCI_RAW_DMA_MAX_QUEUE_NUM + queue_index) *
                              (GXPCI_HOST_RD_REGS_MAP_SIZE +
                              GXPCI_HOST_RD_REG_APP_MAP_SIZE));
  resource->raw_dma.app_regs =(struct gxpci_host_rd_regs_app *)
                              (rd_state->mmio_mem +
                              (GXPCI_RAW_DMA_MAX_QUEUE_NUM + queue_index) *
                              (GXPCI_HOST_RD_REGS_MAP_SIZE +
                              GXPCI_HOST_RD_REG_APP_MAP_SIZE) +
                              GXPCI_HOST_RD_REGS_MAP_SIZE);

  //
  // Allocate and initialize a push DMA ring for tranmitting data to the host.
  //
  dma_ring = gxio_trio_alloc_push_dma_ring(trio_context, 1, 0, 0);
  GXIO_VERIFY_NON_NEGATIVE(dma_ring, "data gxio_trio_alloc_push_dma_ring()");

  //
  // Bind the DMA ring to our MAC, and use registered memory to store
  // the command ring.
  //
  dma_ring_size = GXPCI_RAW_DMA_PUSH_DMA_RING_LEN *
                  sizeof(gxio_trio_dma_desc_t);
  dma_ring_mem = rd_state->t2h_dma_mem + queue_index * dma_ring_size;
  memset(dma_ring_mem, 0, dma_ring_size);
  dma_queue = &resource->raw_dma.dma_queue_data;
  err = gxio_trio_init_push_dma_queue(dma_queue, trio_context, dma_ring,
                                      rd_state->mac, rd_state->asid, 0,
                                      dma_ring_mem, dma_ring_size, 0);
  GXIO_VERIFY_ZERO(err, "data gxio_trio_init_push_dma_queue()");

  //
  // Now that all the resources are allocated, the TILE side marks itself
  // ready and waits for the host to enter the ready state before
  // starting any data transfer.
  //
  volatile uint32_t *loc_ready = &resource->raw_dma.drv_regs->queue_status;
  *loc_ready = GXPCI_TILE_CHAN_READY;

  while (*loc_ready != GXPCI_HOST_CHAN_READY)
  {
    // In case the queue is reset by the peer, we also quit.
    if (*loc_ready == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
  }

  //
  // The host is ready and should have initialized the registers in
  // struct gxpci_host_rd_regs_drv that records the the PCI addresses and
  // size info of the host ring buffer.
  //

#ifdef RAW_DMA_USE_RESERVED_MEMORY
  resource->raw_dma.rd_buf_addr = resource->raw_dma.drv_regs->rd_buf_bus_addr;
#else
  for (int i = 0; i < HOST_RD_SEGMENT_MAX_NUM; i++)
    resource->raw_dma.rd_segment_addr[i] =
      resource->raw_dma.drv_regs->segment_bus_addr[i];
#endif

  resource->raw_dma.rd_buf_size = resource->raw_dma.drv_regs->rd_buf_size;
  context->credits = GXPCI_RAW_DMA_PUSH_DMA_RING_LEN;

  //
  // Activate the queue.
  //
  err = ioctl(context->fd, TILEPCI_IOC_ACTIVATE_RAW_DMA);
  if (err < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_ACTIVATE_RAW_DMA ioctl failure: %s\n",
            device_name, strerror(errno));
    return -errno;
  }

  return 0;
}

int
gxpci_raw_dma_recv_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->raw_dma.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;
#ifdef ENABLE_CMD_SIZE_QSTS_CHECKS
  if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  if ((cmd->size == 0) ||
      (cmd->size > (1 << TRIO_DMA_DESC_WORD0__XSIZE_WIDTH)) ||
      (cmd->size + cmd->remote_buf_offset > resource->raw_dma.rd_buf_size))
    return GXPCI_EINVAL;
#endif

#ifndef RAW_DMA_USE_RESERVED_MEMORY
   //
   // The DMA access shouldn't cross the segment boundary.
   //
  unsigned int segment_offset =
    cmd->remote_buf_offset & (resource->raw_dma.drv_regs->segment_size - 1);
  if (segment_offset + cmd->size > resource->raw_dma.drv_regs->segment_size)
    return GXPCI_EINVAL;
#endif

  //
  // If no credits, return GXPCI_ECREDITS so that the caller
  // can update the credit counters by getting completions.
  //
  if (!context->credits)
      return GXPCI_ECREDITS;

  slot = dma_queue->dma_queue.credits_and_next_index & RD_PULL_RING_MASK; 
  desc_p = &dma_queue->dma_descs[slot];

#ifdef RAW_DMA_USE_RESERVED_MEMORY
  desc_p->io_address = cmd->remote_buf_offset + resource->raw_dma.rd_buf_addr;
#else
  int segment = cmd->remote_buf_offset /
		resource->raw_dma.drv_regs->segment_size;
  desc_p->io_address =
    resource->raw_dma.rd_segment_addr[segment] + segment_offset;
#endif
  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >> 
          RD_PULL_RING_GEN_BIT) ^ 1) & 1;
 
  //
  // SMOD is one bit below XSIZE.  We set SMOD if size is 16KB.
  //
  xsize = (cmd->size << 1) | (cmd->size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) = 
    ((intptr_t)(cmd->buffer)) |
    (xsize << TRIO_DMA_DESC_WORD0__SMOD_SHIFT) |
    (gen << TRIO_DMA_DESC_WORD0__GEN_SHIFT);

  gxio_trio_dma_queue_flush(dma_queue);

  dma_queue->dma_queue.credits_and_next_index++;
  context->credits--;
  return 0;
}
 
int
gxpci_raw_dma_send_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->raw_dma.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t slot;
  uint64_t gen;
  uint64_t xsize;

#ifdef ENABLE_CMD_SIZE_QSTS_CHECKS
  if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;

  if ((cmd->size == 0) ||
      (cmd->size > (1 << TRIO_DMA_DESC_WORD0__XSIZE_WIDTH)) ||
      (cmd->size + cmd->remote_buf_offset > resource->raw_dma.rd_buf_size))
    return GXPCI_EINVAL;
#endif

#ifndef RAW_DMA_USE_RESERVED_MEMORY
  //
  // The DMA access shouldn't cross the segment boundary.
  //
  unsigned int segment_offset =
    cmd->remote_buf_offset & (resource->raw_dma.drv_regs->segment_size - 1);
  if (segment_offset + cmd->size > resource->raw_dma.drv_regs->segment_size)
    return GXPCI_EINVAL;
#endif

  //
  // If no credits, return GXPCI_ECREDITS so that the caller
  // can update the credit counters by getting completions.
  //
  if (!context->credits)
      return GXPCI_ECREDITS;

  slot = dma_queue->dma_queue.credits_and_next_index & RD_PUSH_RING_MASK; 
  desc_p = &dma_queue->dma_descs[slot];

#ifdef RAW_DMA_USE_RESERVED_MEMORY
  desc_p->io_address = cmd->remote_buf_offset + resource->raw_dma.rd_buf_addr;
#else
  int segment = cmd->remote_buf_offset /
		resource->raw_dma.drv_regs->segment_size;
  desc_p->io_address =
    resource->raw_dma.rd_segment_addr[segment] + segment_offset;
#endif

  __insn_flushwb();

  gen = ((dma_queue->dma_queue.credits_and_next_index >> 
          RD_PUSH_RING_GEN_BIT) ^ 1) & 1;

  //
  // SMOD is one bit below XSIZE.  We set SMOD if size is 16KB. 
  // This shift costs ~3% performance vs. just writing xsize alone.
  // So if performance is very tight it's better to hardcode the size
  // and/or not special case 16KB transfers.
  //
  xsize = (cmd->size << 1) | (cmd->size >> TRIO_DMA_DESC_WORD0__XSIZE_WIDTH);

  *((uint64_t *)desc_p) = 
    (intptr_t)(cmd->buffer) |
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
gxpci_raw_dma_recv_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                             int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->raw_dma.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t comp_index = 0;
  int slot;

  do
  {
    //
    // Note that we ONLY call update_counters if
    // we haven't gotten "max" completions.
    // This prevents extra reads of the MMIO register for
    // pull DMA completion status unless we're not making
    // progress.
    //
    while (context->completed != resource->raw_dma.dma_cpl_cnt) 
    {
      slot = context->completed & RD_PULL_RING_MASK;
      desc_p = &dma_queue->dma_descs[slot];

      //
      // Sign-extend the VA.
      //
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
        return comp_index;
    }

    if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
    {
      return GXPCI_ERESET;
    }
    
    //
    // Update the completed index in case DMA engine is still finishing some
    // work.  This gaurantees forward progress in case work is pending.
    //
    gxpci_rd_h2t_update_counters(context);

  } while(comp_index < min);

  return comp_index;
}

int
gxpci_raw_dma_send_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                             int min, int max)
{
  gxpci_resource_t *resource = &context->resource;
  gxio_trio_dma_queue_t* dma_queue = &resource->raw_dma.dma_queue_data;
  gxio_trio_dma_desc_t *desc_p; 
  uint32_t comp_index = 0;
  int slot;

  do
  {
    //
    // Note that we ONLY call update_counters if
    // we haven't gotten "max" completions.
    // This prevents extra reads of the MMIO register for
    // push DMA completion status unless we're not making
    // progress.
    //
    while (context->completed != resource->raw_dma.dma_cpl_cnt) 
    {
      slot = context->completed & RD_PUSH_RING_MASK;
      desc_p = &dma_queue->dma_descs[slot];

      //
      // Prefetch the next 4 cachelines (There are 4 descs per line).
      //
      if ((comp_index & 0xf) == 0)
      {
        __insn_prefetch((void *)desc_p + (1 << 6));
        __insn_prefetch((void *)desc_p + (2 << 6));
        __insn_prefetch((void *)desc_p + (3 << 6));
        __insn_prefetch((void *)desc_p + (4 << 6));
      }

      //
      // Sign-extend the VA.
      //
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
        return comp_index;
    }

    if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
      return GXPCI_ERESET;
    
    gxpci_rd_t2h_update_counters(context);

  } while(comp_index < min);

  return comp_index;
}

uint32_t gxpci_raw_dma_recv_get_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;
  return gxpci_rd_h2t_update_counters(context);
}

uint32_t gxpci_raw_dma_send_get_credits(gxpci_context_t *context)
{
  gxpci_resource_t *resource = &context->resource;
  if (resource->raw_dma.drv_regs->queue_status == GXPCI_CHAN_RESET)
    return GXPCI_ERESET;
  return gxpci_rd_t2h_update_counters(context);
}

unsigned int gxpci_raw_dma_get_host_buf_size(gxpci_context_t *context)
{
  return context->resource.raw_dma.rd_buf_size;
}

unsigned int gxpci_raw_dma_get_host_counter(gxpci_context_t *context)
{
  return context->resource.raw_dma.app_regs->host_counter;
}

int gxpci_raw_dma_recv_destroy(gxpci_context_t *context)
{
  close(context->fd);

  return 0;
}

int gxpci_raw_dma_send_destroy(gxpci_context_t *context)
{
  close(context->fd);

  return 0;
}

int gxpci_raw_dma_destroy(gxio_trio_context_t *trio_context,
                          gxpci_raw_dma_state_t *rd_state)
{
  if (rd_state->msix_table_base)
    munmap(rd_state->msix_table_base, rd_state->msix_table_size);

  close(rd_state->barmem_fd);

  gxio_trio_destroy(trio_context);

  return 0;
}

