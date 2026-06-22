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

/**
 * @file
 *
 * An API for allocating, configuring, and manipulating TRIO hardware
 * resources
 */

/**
 * @addtogroup gxio_trio
 * @{
 *
 * The TILE-Gx TRIO shim provides connections to external devices via
 * PCIe or other transaction IO standards.  The gxio_trio_ API,
 * declared in <gxio/trio.h>, allows applications to allocate and
 * configure TRIO IO resources like DMA command rings, memory map
 * windows, and device interrupts.  The following sections introduce
 * the various components of the API.  We strongly recommend reading
 * the TRIO section of the IO Device Guide (UG404) before working with
 * this API.
 *
 * @section trio__ingress TRIO Ingress Hardware Resources
 *
 * The TRIO ingress hardware is responsible for examining incoming
 * PCIe or StreamIO packets and choosing a processing mechanism based
 * on the packets' bus address.  The gxio_trio_ API can be used to
 * configure different handlers for different ranges of bus address
 * space.  The user can configure "mapped memory" and "scatter queue"
 * regions to match incoming packets within 4kB-aligned ranges of bus
 * addresses.  Each range specifies a different set of mapping
 * parameters to be applied when handling the ingress packet.  The
 * following sections describe how to work with MapMem and scatter
 * queue regions.
 *
 * @subsection trio__mapmem TRIO MapMem Regions
 *
 * TRIO mapped memory (or MapMem) regions allow the user to map
 * incoming read and write requests directly to the application's
 * memory space.  MapMem regions are allocated via
 * gxio_trio_alloc_memory_maps().  Given an integer MapMem number,
 * applications can use gxio_trio_init_memory_map() to specify the
 * range of bus addresses that will match the region and the range of
 * virtual addresses to which those packets will be applied.
 *
 * As with many other gxio APIs, the programmer must be sure to
 * register memory pages that will be used with MapMem regions.  Pages
 * can be registered with TRIO by allocating an ASID (address space
 * identifier) and then using gxio_trio_register_page() to register up to
 * 16 pages with the hardware.  The initialization functions for
 * resources that require registered memory (MapMem, scatter queues,
 * push DMA, and pull DMA) then take an 'asid' parameter in order to
 * configure which set of registered pages is used by each resource.
 *
 * @subsection trio__scatter_queue TRIO Scatter Queues
 *
 * The TRIO shim's scatter queue regions allow users to dynamically
 * map buffers from a large address space into a small range of bus
 * addresses.  This is particularly helpful for PCIe endpoint devices,
 * where the host generally limits the size of BARs to tens of
 * megabytes.
 *
 * Each scatter queue consists of a memory map region, a queue of
 * tile-side buffer VAs to be mapped to that region, and a bus-mapped
 * "doorbell" register that the remote endpoint can write to trigger a
 * dequeue of the current buffer VA, thus swapping in a new buffer.
 * The VAs pushed onto a scatter queue must be 4kB aligned, so
 * applications may need to use higher-level protocols to inform
 * remote entities that they should apply some additional, sub-4kB
 * offset when reading or writing the scatter queue region.  For more
 * information, see the IO Device Guide (UG404).
 *
 * @section trio__egress TRIO Egress Hardware Resources
 *
 * The TRIO shim supports two mechanisms for egress packet generation:
 * programmed IO (PIO) and push/pull DMA.  PIO allows applications to
 * create MMIO mappings for PCIe or StreamIO address space, such that
 * the application can generate word-sized read or write transactions
 * by issuing load or store instructions.  Push and pull DMA are tuned
 * for larger transactions; they use specialized hardware engines to
 * transfer large blocks of data at line rate.
 *
 * @subsection trio__pio TRIO Programmed IO
 *
 * Programmed IO allows applications to create MMIO mappings for PCIe
 * or StreamIO address space.  The hardware PIO regions support access
 * to PCIe configuration, IO, and memory space, but the gxio_trio API
 * only supports memory space accesses.  PIO regions are allocated
 * with gxio_trio_alloc_pio_regions() and initialized via
 * gxio_trio_init_pio_region().  Once a region is bound to a range of
 * bus address via the initialization function, the application can
 * use gxio_trio_map_pio_region() to create MMIO mappings from its VA
 * space onto the range of bus addresses supported by the PIO region.
 *
 * @subsection trio_dma TRIO Push and Pull DMA
 *
 * The TRIO push and pull DMA engines allow users to copy blocks of
 * data between application memory and the bus.  Push DMA generates
 * write packets that copy from application memory to the bus and pull
 * DMA generates read packets that copy from the bus into application
 * memory.  The DMA engines are managed via an API that is very
 * similar to the mPIPE eDMA interface.  For a detailed explanation of
 * the eDMA queue API, see @ref gxio_mpipe_wrappers.
 *
 * Push and pull DMA queues are allocated via
 * gxio_trio_alloc_push_dma_ring() / gxio_trio_alloc_pull_dma_ring().
 * Once allocated, users generally use a ::gxio_trio_dma_queue_t
 * object to manage the queue, providing easy wrappers for reserving
 * command slots in the DMA command ring, filling those slots, and
 * waiting for commands to complete.  DMA queues can be initialized
 * via gxio_trio_init_push_dma_queue() or
 * gxio_trio_init_pull_dma_queue().
 *
 * See @ref trio/push_dma/app.c for an example of how to use push DMA.
 *
 * @section trio_shortcomings Plans for Future API Revisions
 *
 * The simulation framework is incomplete.  Future features include:
 *
 * - Support for reset and deallocation of resources.
 *
 * - Support for pull DMA.
 *
 * - Support for interrupt regions and user-space interrupt delivery.
 *
 * - Support for getting BAR mappings and reserving regions of BAR
 *   address space.
 */
#ifndef _GXIO_TRIO_H_
#define _GXIO_TRIO_H_

#include <stdbool.h>

#include <gxio/common.h>
#include <gxio/dma_queue.h>

#include <arch/trio_constants.h>
#include <arch/trio.h>
#include <arch/trio_pcie_intfc.h>
#include <arch/trio_pcie_rc.h>
#include <arch/trio_pcie_ep.h>
#include <arch/trio_shm.h>
#include <hv/drv_trio_intf.h>
#include <hv/iorpc.h>

__BEGIN_DECLS

/** A context object used to manage TRIO hardware resources. */
typedef struct {

  /** File descriptor for calling up to Linux (and thus the HV). */
  int fd;

  /** The VA at which the MAC MMIO registers are mapped. */
  char* mmio_base_mac;


  /** File offsets for PIO regions, to compensate for hardware's
      4GB-aligned-region requirement. */
  uint32_t pio_offsets[TRIO_NUM_TPIO_REGIONS];

  /** VAs to which the push DMA post regions have been mapped. */
  void* push_dma_vas[TRIO_NUM_PUSH_DMA_RINGS];

  /** VAs to which the pull DMA post regions have been mapped. */
  void* pull_dma_vas[TRIO_NUM_PULL_DMA_RINGS];

  /** VAs to which the scatter queues have been mapped. */
  void* scatter_queue_vas[TRIO_NUM_MAP_SQ_REGIONS];


}
gxio_trio_context_t;

/** Command descriptor for push or pull DMA. */
typedef TRIO_DMA_DESC_t gxio_trio_dma_desc_t;

/** A convenient, thread-safe interface to an eDMA ring. */
typedef struct {

  /** State object for tracking head and tail pointers. */
  __gxio_dma_queue_t dma_queue;

  /** The ring entries. */
  gxio_trio_dma_desc_t* dma_descs;

  /** The number of entries minus one. */
  unsigned long mask_num_entries;

  /** The log2() of the number of entries. */
  unsigned int log2_num_entries;

} gxio_trio_dma_queue_t;

/** Destroy a TRIO context.
 *
 * This function de-allocates a TRIO "service domain" and unmaps the MMIO
 * registers from the the caller's VA space.
 *
 * @param context An initialized context object to be destroyed, returned
 * by gxio_trio_init().
 */
extern int
gxio_trio_destroy(gxio_trio_context_t* context);

/** Initialize a TRIO context.
 *
 * This function allocates a TRIO "service domain" and maps the MMIO
 * registers into the the caller's VA space.
 *
 * @param context Context object to be initialized.
 * @param trio_index Which TRIO shim; Gx36 must pass 0.
 */
extern int
gxio_trio_init(gxio_trio_context_t* context, unsigned int trio_index);

/** This indicates that an ASID hasn't been allocated. */
#define GXIO_ASID_NULL -1

/** Ordering modes for map memory regions and scatter queue regions. */
typedef enum gxio_trio_order_mode_e {
  /** Writes are not ordered.  Reads always wait for previous writes. */
  GXIO_TRIO_ORDER_MODE_UNORDERED =
  TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_UNORDERED,
  /** Both writes and reads wait for previous transactions to complete. */
  GXIO_TRIO_ORDER_MODE_STRICT =
  TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT,
  /** Writes are ordered unless the incoming packet has the
      relaxed-ordering attributes set. */
  GXIO_TRIO_ORDER_MODE_OBEY_PACKET =
  TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_REL_ORD
} gxio_trio_order_mode_t;

/** Free a memory mapping region.
 *
 * @param context An initialized TRIO context.
 * @param map An initialized Memory map region.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP if illegal
 *   memory map region.
 */
extern int
gxio_trio_free_memory_map(gxio_trio_context_t* context, unsigned int map);

/** Initialize a memory mapping region.
 *
 * @param context An initialized TRIO context.
 * @param map A Memory map region allocated by gxio_trio_alloc_memory_maps().
 * @param target_mem VA of backing memory, should be registered via
 *   gxio_trio_register_page() and aligned to 4kB.
 * @param target_size Length of the memory mapping, must be a multiple
 * of 4kB.
 * @param asid ASID to be used for Tile-side address translation.
 * @param mac MAC number.
 * @param bus_address Bus address at which the mapping starts.
 * @param order_mode Memory ordering mode for this mapping.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP,
 *   GXIO_TRIO_ERR_BAD_ASID, or ::GXIO_TRIO_ERR_BAD_BUS_RANGE.
 */
extern int
gxio_trio_init_memory_map(gxio_trio_context_t* context, unsigned int map,
                          void* target_mem, size_t target_size,
                          unsigned int asid,
                          unsigned int mac,
                          uint64_t bus_address,
                          gxio_trio_order_mode_t order_mode);

/** Flags that can be passed to resource allocation functions. */
enum gxio_trio_alloc_flags_e {
  GXIO_TRIO_ALLOC_FIXED = HV_TRIO_ALLOC_FIXED,
};

/** Flags that can be passed to memory registration functions. */
enum gxio_trio_mem_flags_e {
  /** Do not fill L3 when writing, and invalidate lines upon egress. */
  GXIO_TRIO_MEM_FLAG_NT_HINT = IORPC_MEM_BUFFER_FLAG_NT_HINT,

  /** L3 cache fills should only populate IO cache ways. */
  GXIO_TRIO_MEM_FLAG_IO_PIN = IORPC_MEM_BUFFER_FLAG_IO_PIN,
};

/** Flag indicating a request generator uses a special traffic
    class. */
#define GXIO_TRIO_FLAG_TRAFFIC_CLASS(N) HV_TRIO_FLAG_TC(N)

/** Flag indicating a request generator uses a virtual function
    number. */
#define GXIO_TRIO_FLAG_VFUNC(N) HV_TRIO_FLAG_VFUNC(N)


/******************************************************************
 *                       Memory Registration                      *
 ******************************************************************/

/** De-allocate an allocated Application Space Identifiers (ASIDs).
 *
 * @param context An initialized TRIO context.
 * @param asid Index of ASID to be de-allocated.
 * @return 0 on success, or ::GXIO_TRIO_ERR_BAD_ASID if de-allocation
 *   failed.
 */
extern int
gxio_trio_dealloc_asid(gxio_trio_context_t* context, unsigned int asid);

/** Allocate Application Space Identifiers (ASIDs).  Each ASID can
 * register up to 16 page translations.  ASIDs are used by memory map
 * regions, scatter queues, and DMA queues to translate application
 * VAs into memory system PAs.
 *
 * @param context An initialized TRIO context.
 * @param count Number of ASIDs required.
 * @param first Index of first ASID if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first ASID, or ::GXIO_TRIO_ERR_NO_ASID if allocation
 *   failed.
 */
extern int
gxio_trio_alloc_asids(gxio_trio_context_t* context, unsigned int count,
                      unsigned int first, unsigned int flags);

/** Unregister a page with an ASID.
 *
 * @param context An initialized TRIO context.
 * @param asid An ASID allocated via gxio_trio_alloc_asids().
 * @param page Starting VA of a contiguous memory page.
 * @return Zero on success, EINVAL if page does not map a contiguous
 *   page, ::GXIO_TRIO_ERR_BAD_ASID if illegal ASID or ::GXIO_ERR_IOTLB_ENTRY
 *   if no registered IOTLB entry is found.
 */
extern int
gxio_trio_unregister_page(gxio_trio_context_t* context, unsigned int asid,
                          void* page);

/** Register a page with an ASID.  Each ASID can map up to 16 pages.
 * Tile-side memory addresses generated by memory map regions, scatter
 * queues, push DMA rings, and pull DMA rings must reference a VA that
 * has been registered with this function.
 *
 * Note that the underlying driver internally marks the registered 
 * page shared, as if it is allocated with mmap(2) flag MAP_SHARED,
 * which implies that the memory is shared between the allocating
 * process and any child processes created after the allocation.
 *
 * @param context An initialized TRIO context.
 * @param asid An ASID allocated via gxio_trio_alloc_asids().
 * @param page Starting VA of a contiguous memory page.
 * @param page_size Size of the page.
 * @param page_flags ::gxio_trio_mem_flags_e memory flags.
 * @return Zero on success, EINVAL if page does not map a contiguous
 *   page, ::GXIO_ERR_IOTLB_ENTRY if no more IOTLB entries are
 *   available.
 */
extern int
gxio_trio_register_page(gxio_trio_context_t* context, unsigned int asid,
                        void* page, size_t page_size, unsigned int page_flags);

/** Free a push DMA ring.
 *
 * @param context An initialized TRIO context.
 * @param ring An initialized push DMA ring.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_PUSH_DMA_RING if illegal
 *   push DMA ring.
 */
extern int
gxio_trio_free_push_dma_ring(gxio_trio_context_t* context, unsigned int ring);

/** Initialize a gxio_trio_dma_queue_t for use with push DMA.
 *
 * Takes the queue plus the same args as gxio_trio_init_push_dma_ring().
 */
extern int
gxio_trio_init_push_dma_queue(gxio_trio_dma_queue_t* queue,
                              gxio_trio_context_t* context,
                              unsigned int ring, unsigned int mac,
                              unsigned int asid, unsigned int req_flags,
                              void* mem, unsigned int mem_size,
                              unsigned int mem_flags);

/** Free a pull DMA ring.
 *
 * @param context An initialized TRIO context.
 * @param ring An initialized pull DMA ring.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_PULL_DMA_RING if illegal
 *   pull DMA ring.
 */
extern int
gxio_trio_free_pull_dma_ring(gxio_trio_context_t* context, unsigned int ring);

/** Initialize a gxio_trio_dma_queue_t for use with pull DMA.
 *
 * Takes the queue plus the same args as gxio_trio_init_pull_dma_ring().
 */
extern int
gxio_trio_init_pull_dma_queue(gxio_trio_dma_queue_t* queue,
                              gxio_trio_context_t* context,
                              unsigned int ring, unsigned int mac,
                              unsigned int asid, unsigned int req_flags,
                              void* mem, unsigned int mem_size,
                              unsigned int mem_flags);

/** Free a scatter queue.
 *
 * @param context An initialized TRIO context.
 * @param queue An initialized scatter queue.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_SCATTER_QUEUE if illegal
 *   scatter queue region.
 */
extern int
gxio_trio_free_scatter_queue(gxio_trio_context_t* context, unsigned int queue);

/** Reserve slots for DMA commands.
 *
 * Use gxio_trio_dma_queue_put_at() to actually populate the slots.
 *
 * @param queue An dma queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @param num Number of slots to reserve.
 * @return The first reserved slot, or a negative error code.
 */
static int64_t __USUALLY_INLINE
gxio_trio_dma_queue_reserve(gxio_trio_dma_queue_t* queue, unsigned int num)
{
  return __gxio_dma_queue_reserve_aux(&queue->dma_queue, num, 1);
}

/** Post a DMA command to a DMA queue at a given slot.
 *
 * This routine is often used to insert DMA commands into the queue in
 * a pre-determined order.  The implementation does not check to make
 * sure DMA command credits are available.  It is assumed that the
 * user knows the ring can never overflow, generally because the
 * application uses fewer buffers than there are slots in the DMA
 * queue.
 *
 * @param queue An dma queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @param dma_desc DMA command to be posted.
 * @param slot An egress slot (only the low bits are actually used).
 */
static void __USUALLY_INLINE
gxio_trio_dma_queue_put_at(gxio_trio_dma_queue_t* queue,
                           gxio_trio_dma_desc_t dma_desc, uint64_t slot)
{
  uint64_t dma_slot = slot & queue->mask_num_entries;
  gxio_trio_dma_desc_t *desc_p = &queue->dma_descs[dma_slot];

  /*
   * ISSUE: Could set DMA ring to be on generation 1 at start, which
   * would avoid the negation here, perhaps allowing __insn_bfins().
   */
  dma_desc.gen = !((slot >> queue->log2_num_entries) & 1);

#ifndef __LP64__
  /*
   * The VA may be sign-extended if the user didn't use the right
   * pointer type casting and assignment.  Zero-extend the VA to
   * match the IOTLB entries.
   */
  dma_desc.va = (unsigned int) dma_desc.va;  /* Clear high bits */
#endif

  /*
   * We use arch_atomic_access_once(), plus the fact that the DMA
   * queue alignment restrictions ensure that these two words are on
   * the same cacheline, to force proper ordering between the stores.
   */
  arch_atomic_access_once(desc_p->words[1]) = dma_desc.words[1];
  arch_atomic_access_once(desc_p->words[0]) = dma_desc.words[0];
}

/** Ask the TRIO hardware to start DMA immediately.
 *
 * This call is not necessary, but may slightly reduce overall latency.
 *
 * @param queue A DMA queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 */
static void __USUALLY_INLINE
gxio_trio_dma_queue_flush(gxio_trio_dma_queue_t* queue)
{
  /* Use "ring_idx = 0" and "count = 0" to "wake up" the DMA ring. */
  TRIO_PULL_DMA_REGION_VAL_t val = {{ 0 }};
  __insn_flushwb(); /* Flush the write buffers. */ 
  __gxio_mmio_write(queue->dma_queue.post_region_addr, val.word);
}

/** Find out how many DMA queue descriptors have been completely processed.
 *
 * @param queue A DMA queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @result A 16-bit running count of the number of descriptors that
 * have been completely processed.
 */
static uint16_t __USUALLY_INLINE
gxio_trio_read_dma_queue_complete_count(gxio_trio_dma_queue_t* queue)
{
  TRIO_PUSH_DMA_REGION_VAL_t read_val;
  read_val.word = __gxio_mmio_read(queue->dma_queue.post_region_addr);

  return read_val.count;
}

/** Read out the interrupt status from TRIO_INT_VEC0 to 4.
 *
 * @param context An initialized TRIO context.
 * @param vec_num Interrupt vector number.
 * @return Interrupt status on success, else ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_read_isr_status(gxio_trio_context_t* context, unsigned int vec_num);

/** Write the interrupt status to TRIO_INT_VEC0 to 4 to clear particular
 *  interrupts.
 *
 * @param context An initialized TRIO context.
 * @param vec_num Interrupt vector number.
 * @param bits_to_clear Interrupt bits to be cleared within the paticular
 * interrupt vector.
 * @return Zero on success, else ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_write_isr_status(gxio_trio_context_t* context, unsigned int vec_num,
                           uint32_t bits_to_clear);

/******************************************************************
 *                      Host Interrupt management                 *
 ******************************************************************/

/** Trigger the PCIe RC host MSI/MSI-X interrupt.
 *
 * @param context An initialized TRIO context.
 * @param mac MAC number.
 * @param can_spin Flag indicating if this can spin.
 * @param msix_addr MSI-X message address.
 * @param msix_data MSI-X message data.
 * @return Zero on success, else ::GXIO_ERR_BUSY.
 */
static int __USUALLY_INLINE
gxio_trio_trigger_host_interrupt(gxio_trio_context_t* context,
                                 unsigned int mac,
                                 unsigned int can_spin,
                                 unsigned long msix_addr,
                                 unsigned int msix_data)
{
  TRIO_PCIE_INTFC_EP_INT_GEN_t intr_setup;
  TRIO_SEMAPHORE0_t sem;

  /* Use the Semaphore register for synchronization purpose. */
  sem.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT)));
  if (sem.val && !can_spin)
    return GXIO_ERR_BUSY;

  while (sem.val)
    sem.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT)));

  __insn_mf();

  /*
   * Generate either MSI or MSI-X, based on the msi_ena field
   * in register TRIO_PCIE_INTFC_EP_INT_GEN.
   */
  intr_setup.word = __gxio_mmio_read(context->mmio_base_mac +
    ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)));

  while (intr_setup.send_msi)
    intr_setup.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)));

  if (intr_setup.msi_ena == 0)
  {
    /* Dispatch MSI-X. Set the MSIX_DATA and MSIX_ADDR registers. */
    TRIO_PCIE_INTFC_EP_MSIX_DATA_t data;
    TRIO_PCIE_INTFC_EP_MSIX_ADDR_t addr;

    data.word = 0;
    data.val = msix_data;
    addr.word = 0;
    addr.val = msix_addr;
    __gxio_mmio_write(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_MSIX_DATA << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), data.word);

    __gxio_mmio_write(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_MSIX_ADDR << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), addr.word);
  }

  intr_setup.msi_vf_ena = 0;
  intr_setup.send_msi = 1;
  __gxio_mmio_write(context->mmio_base_mac +
    ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), intr_setup.word);

  __insn_mf();

  sem.val = 0;
  __gxio_mmio_write(context->mmio_base_mac +
    ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT)), sem.word);

  return 0;
}

/** Trigger the PCIe RC host MSI/MSI-X interrupt for a virtual function.
 *
 * @param context An initialized TRIO context.
 * @param mac MAC number.
 * @param can_spin Flag indicating if this can spin.
 * @param msix_addr MSI-X message address.
 * @param msix_data MSI-X message data.
 * @param vf Virtual function instance number.
 * @return Zero on success, else ::GXIO_ERR_BUSY.
 */
static int __USUALLY_INLINE
gxio_trio_trigger_host_interrupt_vf(gxio_trio_context_t* context,
                                    unsigned int mac,
                                    unsigned int can_spin,
                                    unsigned long msix_addr,
                                    unsigned int msix_data,
                                    unsigned int vf)
{
  TRIO_PCIE_INTFC_EP_INT_GEN_t intr_setup;
  TRIO_SEMAPHORE0_t sem;

  /* Use the Semaphore register for synchronization purpose. */
  sem.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT)));
  if (sem.val && !can_spin)
    return GXIO_ERR_BUSY;

  while (sem.val)
    sem.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT)));

  __insn_mf();

  /*
   * Generate either MSI or MSI-X, based on the msi_ena field
   * in register TRIO_PCIE_INTFC_EP_INT_GEN.
   */
  intr_setup.word = __gxio_mmio_read(context->mmio_base_mac +
    ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)));

  while (intr_setup.send_msi)
    intr_setup.word = __gxio_mmio_read(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)));

  /*
   * Note that the msi_ena field reflects the state of the MSI enable
   * in the PF. This implies that, in order to use the MSI for the VF,
   * the PF must be configured for MSI instead of MSI-X.
   */
  if (intr_setup.msi_ena == 0)
  {
    /* Dispatch MSI-X. Set the MSIX_DATA and MSIX_ADDR registers. */
    TRIO_PCIE_INTFC_EP_MSIX_DATA_t data;
    TRIO_PCIE_INTFC_EP_MSIX_ADDR_t addr;

    data.word = 0;
    data.val = msix_data;
    addr.word = 0;
    addr.val = msix_addr;
    __gxio_mmio_write(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_MSIX_DATA << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), data.word);

    __gxio_mmio_write(context->mmio_base_mac +
      ((TRIO_PCIE_INTFC_EP_MSIX_ADDR << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
      TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), addr.word);
  }

  intr_setup.msi_vf_ena = 1;
  intr_setup.msi_vf_sel = vf;
  intr_setup.send_msi = 1;
  __gxio_mmio_write(context->mmio_base_mac +
    ((TRIO_PCIE_INTFC_EP_INT_GEN << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT)), intr_setup.word);

  __insn_mf();

  sem.val = 0;
  __gxio_mmio_write(context->mmio_base_mac +
    ((TRIO_SEMAPHORE0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT)), sem.word);

  return 0;
}

/** Actions that can occur when memory map interrupt bits are
 * written via Tile-side MMIO.
 */
typedef enum {
  GXIO_TRIO_MMI_BITS_WRITE = 0,
  GXIO_TRIO_MMI_BITS_W1TC = 1,
  GXIO_TRIO_MMI_BITS_W1TS = 2
} gxio_trio_mmi_write_effect_t;

/** Write the bus-to-tile interrupt state bits associated with a
 * memory map region.
 *
 * @param context An initialized TRIO context.
 * @param map A Memory map region allocated by gxio_trio_alloc_memory_maps().
 * @param bits Bits to be written.
 * @param mode Select different memory map interrupt registers as the write
 * target, which can generate different behavious.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP or
 * ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_write_mmi_bits(gxio_trio_context_t* context, unsigned int map,
                         unsigned int bits, gxio_trio_mmi_write_effect_t mode);

/******************************************************************
 *                       Memory Map Regions                       *
 ******************************************************************/

/** Allocate a memory map region for mapping incoming bus transactions
 * to Tile memory loads and stores.
 *
 * @param context An initialized TRIO context.
 * @param count Number of memory map regions required.
 * @param first Index of first map region if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first memory map region, or
 * ::GXIO_TRIO_ERR_NO_MEMORY_MAP if allocation failed.
 */
extern int
gxio_trio_alloc_memory_maps(gxio_trio_context_t* context,
                            unsigned int count, unsigned int first,
                            unsigned int flags);

/** Interrupt trigger modes for bus-to-tile interrupts. */
typedef enum {
  /** Interrupt fires continuously if any interrupt bits are 1. */
  GXIO_TRIO_MMI_MODE_LEVEL = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_LEVEL,
  /** Interrupt fires on a rising or falling edge. */
  GXIO_TRIO_MMI_MODE_EDGE = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_EDGE,
  /** Interrupt fires on a rising edge. */
  GXIO_TRIO_MMI_MODE_ASSERT = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_ASSERT,
  /** Interrupt fires on a falling edge. */
  GXIO_TRIO_MMI_MODE_DEASSERT = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_DEASSERT
} gxio_trio_mmi_mode_t;


/** Enable bus-to-tile interrupts for a particular memory map region.
 * Once enabled, the interrupt is masked until gxio_trio_unmask_mmi()
 * is called.
 *
 * @param context An initialized TRIO context.
 * @param bind_cpu CPU number to which interrupt will be delivered.
 * @param bind_event Sub-interrupt event bit number.
 * @param map A memory map region allocated by gxio_trio_alloc_memory_maps().
 * @param mode Conditions that trigger the interrupt.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP or
 * ::GXIO_ERR_INTERRUPT or ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_enable_mmi(gxio_trio_context_t* context,
                     int bind_cpu,
                     int bind_event,
                     unsigned int map,
                     gxio_trio_mmi_mode_t mode);


/** Prevent a bus-to-tile memory map interrupt from being delivered.
 *
 * @param context An initialized TRIO context.
 * @param map A memory map region allocated by gxio_trio_alloc_memory_maps().
 * @param mask Mask bits to be set to mask the memory map region interrupt.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP or
 * ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_mask_mmi(gxio_trio_context_t* context, unsigned int map,
                   unsigned int mask);

/** Allow a bus-to-tile memory map interrupt to be delivered.
 *
 * @param context An initialized TRIO context.
 * @param map A memory map region allocated by gxio_trio_alloc_memory_maps().
 * @param mask Mask bits to be set to unmask the memory map region interrupt.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP or
 * ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_unmask_mmi(gxio_trio_context_t* context, unsigned int map,
                     unsigned int mask);

/** Read the bus-to-tile interrupt state bits associated with a memory
 *  map region.
 *
 * @param context An initialized TRIO context.
 * @param map A Memory map region allocated by gxio_trio_alloc_memory_maps().
 * @return 16-bit value in case of a memory, otherwise
 * ::GXIO_TRIO_ERR_BAD_MEMORY_MAP.
 */
extern int
gxio_trio_read_mmi_bits(gxio_trio_context_t* context, unsigned int map);

/******************************************************************
 *                         Scatter Queues                         *
 ******************************************************************/

/** Allocate scatter queues.  A scatter queue creates a window of bus
 * address space that maps to memory addresses pulled out of a queue.
 * Bus writes to the last word of the mapping region pops the queue
 * and causes the next Tile-side buffer to be mapped in.  Tile-side
 * software can push new buffers onto the queue via
 * gxio_trio_push_scatter_queue_buffer().
 *
 * @param context An initialized TRIO context.
 * @param count Number of scatter queues required.
 * @param first Index of first scatter queue if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first scatter queue, or
 * ::GXIO_TRIO_ERR_NO_SCATTER_QUEUE if allocation failed.
 */
extern int
gxio_trio_alloc_scatter_queues(gxio_trio_context_t* context,
                               unsigned int count, unsigned int first,
                               unsigned int flags);

/** Initialize a scatter queue.
 *
 * @param context An initialized TRIO context.
 * @param queue A scatter queue returned by gxio_trio_alloc_scatter_queues().
 * @param size Size of the mapping region.
 * @param asid ASID used to translate Tile-side addresses.
 * @param mac MAC number.
 * @param bus_address Bus address at which the mapping starts.
 * @param order_mode Memory ordering mode for this mapping.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BUS_RANGE or
 *   ::GXIO_TRIO_ERR_INVAL.
 */
extern int
gxio_trio_init_scatter_queue(gxio_trio_context_t* context,
                             unsigned int queue, uint64_t size,
                             unsigned int asid,
                             unsigned int mac,
                             uint64_t bus_address,
                             gxio_trio_order_mode_t order_mode);

/** Push a buffer onto a scatter queue.  Software is responsible for
 * guaranteeing that no more than 64 buffers are on the queue at a
 * time.
 *
 * @param context An initialized TRIO context.
 * @param queue An initialized scatter queue.
 * @param buffer A 4kB aligned buffer in registered memory.
 * @param request_interrupt Pass 1 to request an interrupt when dequeued.
 */
static __USUALLY_INLINE void
gxio_trio_push_scatter_queue_buffer(gxio_trio_context_t* context,
                                    unsigned int queue, void* buffer,
                                    int request_interrupt)
{
  TRIO_MAP_SQ_REGION_WRITE_VAL_t write_val;
  write_val.va = (unsigned long) buffer >> HV_TRIO_PAGE_SHIFT;
  write_val.int_ena = request_interrupt;

  __gxio_mmio_write(context->scatter_queue_vas[queue], write_val.word);
}

/** Find out how many scatter queue VAs have been dequeued.
 *
 * @param context An initialized TRIO context.
 * @param queue An initialized scatter queue.
 * @return A 8-bit running count of how many scatter queue VAs
 * (pushed by gxio_trio_push_scatter_queue_buffer()) have been popped.
 */
static __USUALLY_INLINE uint8_t
gxio_trio_read_scatter_queue_pop_count(gxio_trio_context_t* context,
                                       unsigned int queue)
{
  TRIO_MAP_SQ_REGION_READ_VAL_t read_val;
  read_val.word = __gxio_mmio_read(context->scatter_queue_vas[queue]);

  return read_val.complete_count;
}


/** Enable bus-to-tile interrupts for a particular scatter queue.
 * NOTE that we only support user-space and level mode interrupt now.
 *
 * @param context An initialized TRIO context.
 * @param bind_cpu CPU number to which interrupt will be delivered.
 * @param bind_event Sub-interrupt event bit number.
 * @param queue An initialized  scatter queue.
 * @return Zero on success, else ::GXIO_ERR_BAD_SCATTER_QUEUE or
 * ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_enable_sqi(gxio_trio_context_t* context,
                     int bind_cpu,
                     int bind_event,
                     unsigned int queue);


/******************************************************************
 *                  Programmed I/O (PIO) Regions                  *
 ******************************************************************/

/** Allocate programmed I/O (PIO) regions.
 *
 * @param context An initialized TRIO context.
 * @param count Number of PIO regions required.
 * @param first Index of first PIO region if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first PIO region, or
 * ::GXIO_TRIO_ERR_NO_PIO if allocation failed.
 */
extern int
gxio_trio_alloc_pio_regions(gxio_trio_context_t* context,
                            unsigned int count, unsigned int first,
                            unsigned int flags);

/** Flag indicating a strictly ordered PIO region. */
#define GXIO_TRIO_PIO_FLAG_ORDERED HV_TRIO_PIO_FLAG_ORDERED

/** Flag indicating a config space PIO region. */
#define GXIO_TRIO_PIO_FLAG_CONFIG_SPACE HV_TRIO_PIO_FLAG_CONFIG_SPACE

/** Flag indicating an IO space PIO region. */
#define GXIO_TRIO_PIO_FLAG_IO_SPACE HV_TRIO_PIO_FLAG_IO_SPACE


/** Free a PIO region.
 *
 * @param context An initialized TRIO context.
 * @param pio_region An initialized PIO region number.
 * @return 0 on success, ::GXIO_TRIO_ERR_BAD_PIO on failure.
 */
extern int
gxio_trio_free_pio_region(gxio_trio_context_t* context,
                          unsigned int pio_region);

/** Initialize a PIO region.
 *
 * @param context An initialized TRIO context.
 * @param pio_region A PIO region number returned by
 *   gxio_trio_alloc_pio_regions().
 * @param mac MAC number.
 * @param bus_address Start of bus address range to be mapped into
 * virtual address space.  Must be page aligned.
 * @param flags Flags including ::GXIO_TRIO_PIO_FLAG_ORDERED,
 * GXIO_TRIO_FLAG_TRAFFIC_CLASS(), or GXIO_TRIO_FLAG_VFUNC().
 * @return 0 on success, ::GXIO_TRIO_ERR_BAD_PIO or ENOMEM on failure.
 */
extern int
gxio_trio_init_pio_region(gxio_trio_context_t* context,
                          unsigned int pio_region,
                          unsigned int mac,
                          uint64_t bus_address,
                          unsigned int flags);

/** Unmap a PIO region from VA space.
 *
 * @param pio_mmio_addr A PIO region's MMIO address returned by
 * gxio_trio_map_pio_region().
 * @param length Size of the mappings.
 *
 * @return 0 on success, or a negative error code.
 */
extern int
gxio_trio_unmap_pio_region(void* pio_mmio_addr, unsigned int length);

/** Map some or all of a PIO region into VA space.
 *
 * @param context An initialized TRIO context.
 * @param pio_region A PIO region number returned by
 * gxio_trio_alloc_pio_regions() and initialized by
 * gxio_trio_init_pio_region().
 * @param length Size of the mappings.
 * @param offset Offset within the PIO region.
 *
 * @return The PIO region's MMIO address, or MAP_FAILED
 * (that is, (void *) -1) if not mapped.
 */
extern void*
gxio_trio_map_pio_region(gxio_trio_context_t* context, unsigned int pio_region,
                         unsigned int length, unsigned int offset);

/** Store eight bytes to a PIO mapping. */
static __USUALLY_INLINE void
gxio_trio_write_uint64(void* addr, uint64_t val)
{
  __gxio_mmio_write64(addr, val);
}

/** Store four bytes to a PIO mapping. */
static __USUALLY_INLINE void
gxio_trio_write_uint32(void* addr, uint32_t val)
{
  __gxio_mmio_write32(addr, val);
}

/** Store two bytes to a PIO mapping. */
static __USUALLY_INLINE void
gxio_trio_write_uint16(void* addr, uint16_t val)
{
  __gxio_mmio_write16(addr, val);
}

/** Store a byte to a PIO mapping. */
static __USUALLY_INLINE void
gxio_trio_write_uint8(void* addr, uint8_t val)
{
  __gxio_mmio_write8(addr, val);
}

/** Load eight bytes from a PIO mapping. */
static __USUALLY_INLINE uint64_t
gxio_trio_read_uint64(void* addr)
{
  return __gxio_mmio_read64(addr);
}

/** Load four bytes from a PIO mapping. */
static __USUALLY_INLINE uint32_t
gxio_trio_read_uint32(void* addr)
{
  return __gxio_mmio_read32(addr);
}

/** Load two bytes from a PIO mapping. */
static __USUALLY_INLINE uint16_t
gxio_trio_read_uint16(void* addr)
{
  return __gxio_mmio_read16(addr);
}

/** Load a byte from a PIO mapping. */
static __USUALLY_INLINE uint8_t
gxio_trio_read_uint8(void* addr)
{
  return __gxio_mmio_read8(addr);
}


/******************************************************************
 *                     Push and Pull DMA Rings                    *
 ******************************************************************/

/** Allocate push DMA rings.
 *
 * @param context An initialized TRIO context.
 * @param count Number of push DMA rings required.
 * @param first Index of first push DMA ring if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first push DMA ring, or
 * ::GXIO_TRIO_ERR_NO_PUSH_DMA_RING if allocation failed.
 */
extern int
gxio_trio_alloc_push_dma_ring(gxio_trio_context_t* context,
                              unsigned int count, unsigned int first,
                              unsigned int flags);

/** Allocate pull DMA rings.
 *
 * @param context An initialized TRIO context.
 * @param count Number of pull DMA rings required.
 * @param first Index of first pull DMA ring if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first pull DMA ring, or
 * ::GXIO_TRIO_ERR_NO_PULL_DMA_RING if allocation failed.
 */
extern int
gxio_trio_alloc_pull_dma_ring(gxio_trio_context_t* context,
                              unsigned int count, unsigned int first,
                              unsigned int flags);

/** Initialize a push DMA ring.
 *
 * @param context An initialized TRIO context.
 * @param ring Push DMA ring index.
 * @param mac MAC number.
 * @param asid ASID to be used for Tile-side address translation.
 * @param req_flags Flag bits, possibly including
 * ::GXIO_TRIO_FLAG_VFUNC() or ::GXIO_TRIO_FLAG_TRAFFIC_CLASS().
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_trio_dma_desc_t structures.
 * @param mem_size Number of bytes in the ring.  Must be 512, 2048,
 * 8192 or 65536 * sizeof(gxio_trio_dma_desc_t).
 * @param mem_flags ::gxio_trio_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_TRIO_ERR_BAD_PUSH_DMA_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_trio_init_push_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring, unsigned int mac,
                             unsigned int asid, unsigned int req_flags,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags);

/** Initialize a pull DMA ring.
 *
 * @param context An initialized TRIO context.
 * @param ring Pull DMA ring index.
 * @param mac MAC number.
 * @param asid ASID to be used for Tile-side address translation.
 * @param req_flags Flag bits, possibly including
 * ::GXIO_TRIO_FLAG_VFUNC() or ::GXIO_TRIO_FLAG_TRAFFIC_CLASS().
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_trio_dma_desc_t structures.
 * @param mem_size Number of bytes in the ring.  Must be 512, 2048,
 * 8192 or 65536 * sizeof(gxio_trio_dma_desc_t).
 * @param mem_flags ::gxio_trio_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_TRIO_ERR_BAD_PULL_DMA_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_trio_init_pull_dma_ring(gxio_trio_context_t* context,
                             unsigned int ring, unsigned int mac,
                             unsigned int asid, unsigned int req_flags,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags);


/** Enable push dma completion interrupt for a particular push dma ring.
 *
 * @param context An initialized TRIO context.
 * @param bind_cpu CPU number to which interrupt will be delivered.
 * @param bind_event Sub-interrupt event bit number.
 * @param ring A push dma ring allocated by gxio_trio_alloc_push_dma_ring().
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_PUSH_DMA_RING or
 * ::GXIO_ERR_INTERRUPT or ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_enable_push_dma_isr(gxio_trio_context_t* context,
                              int bind_cpu,
                              int bind_event,
                              unsigned int ring);

/** Enable pull dma completion interrupt for a particular pull dma ring.
 *
 * @param context An initialized TRIO context.
 * @param bind_cpu CPU number to which interrupt will be delivered.
 * @param bind_event Sub-interrupt event bit number.
 * @param ring A pull dma ring allocated by gxio_trio_alloc_pull_dma_ring().
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_PULL_DMA_RING or
 * ::GXIO_ERR_INTERRUPT or ::GXIO_ERR_INVAL.
 */
extern int
gxio_trio_enable_pull_dma_isr(gxio_trio_context_t* context,
                              int bind_cpu,
                              int bind_event,
                              unsigned int ring);


/******************************************************************
 *                        DMA Queue Wrapper                       *
 ******************************************************************/

/** Reserve slots for DMA commands, if possible.
 *
 * Use gxio_trio_dma_queue_put_at() to actually populate the slots.
 *
 * @param queue An dma queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @param num Number of slots to reserve.
 * @return The first reserved slot, or a negative error code.
 */
static __USUALLY_INLINE int64_t
gxio_trio_dma_queue_try_reserve(gxio_trio_dma_queue_t* queue,
                                unsigned int num)
{
  return __gxio_dma_queue_reserve_aux(&queue->dma_queue, num, 0);
}

/** Post a single DMA command to an DMA queue.
 *
 * @param queue An dma queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @param dma_desc DMA command to be posted.
 * @return 0 on success.
 */
static __USUALLY_INLINE int
gxio_trio_dma_queue_put(gxio_trio_dma_queue_t* queue,
                        gxio_trio_dma_desc_t dma_desc)
{
  int64_t slot = gxio_trio_dma_queue_reserve(queue, 1);
  if (slot < 0)
    return (int)slot;

  gxio_trio_dma_queue_put_at(queue, dma_desc, slot);

  return 0;
}


/** Determine if a given DMA command has been completed.
 *
 * @param queue An dma queue initialized via
 * gxio_trio_init_push_dma_queue() or gxio_trio_init_pull_dma_queue().
 * @param slot The slot used by the DMA command.
 * @param update If true, and the command does not appear to have completed
 * yet, then update any software cache of the hardware completion counter,
 * and check again.  This should normally be true.
 * @return True iff the given DMA command has been completed.
 */
static __USUALLY_INLINE bool
gxio_trio_dma_queue_is_complete(gxio_trio_dma_queue_t* queue, int64_t slot,
                                bool update)
{
  return __gxio_dma_queue_is_complete(&queue->dma_queue, slot, update);
}

__END_DECLS


#endif /* ! _GXIO_TRIO_H_ */

/** @} */
