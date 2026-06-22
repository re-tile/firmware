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
 * trio driver.
 */
#ifndef _DRIVERS_TRIO_TRIO_H_
#define _DRIVERS_TRIO_TRIO_H_

#include <types.h>

#include <arch/trio.h>
#include <arch/trio_constants.h>
#include <arch/trio_pcie_intfc.h>
#include <arch/trio_pcie_intfc_def.h>

#include <hv/iorpc.h>
#include <hv/drv_mshim_intf.h>

#include "lock.h"

/** Use I/O MMU to map the whole physical memory to the PCI bus space
    for RC ports. */
#define USE_IOMMU_FOR_RC

/** Allow up to 32 applications. */
#define TRIO_NUM_SVC_DOM 32

/** The MMU page size order. */
#define TRIO_MMU_PG_SIZE_ORDER 30


/** Per-service-domain trio state. */
typedef struct
{
  /** Bitmask of ASIDs owned by this service domain. */
  unsigned long asid_mask;
  
  /** Bitmask of MapMem regions owned by this service domain. */
  unsigned long map_mem_mask;

  /** Bitmask of scatter queue regions owned by this service domain. */
  unsigned long sq_mask;
  
  /** Bitmask of scatter queue regions that have interrupt bindings. */
  unsigned long sq_intr_mask;
  
  /** Bitmask of PIO regions owned by this service domain. */
  unsigned long pio_mask;

  /** Bitmask of push DMA engines owned by this service domain. */
  unsigned long push_dma_mask;
  
  /** Bitmask of pull DMA engines owned by this service domain. */
  unsigned long pull_dma_mask;
}
trio_resources_t;

/** Global trio state. */
typedef struct
{
  /** Most hold this lock to modify shared data. */
  spinlock_t lock;
  
  /** Shim location. */
  pos_t shim_pos;

  /** Set if this is a Gx72 device. */
  int is_gx72;

  /** Shim instance. */
  int instance;

  /** Shim virtual instance; this is the instance potentially translated
   *  through a SHIM_VIRT_INSTANCE BIB item, and is used to look up
   *  port-related BIB items. */
  int virt_instance;
 
  /** Flag indicating if the TRIO has already been quiesced. */
  int quiesced;

  /** A bit set for each unallocated svc_dom. */
  unsigned long long svc_dom_avail_mask;

  /** Allocated resources. */
  trio_resources_t resources;

  /** Allocated resources for each service domain. */
  trio_resources_t svc_dom_resources[TRIO_NUM_SVC_DOM];

  /** The number of IOTLB entries used by each ASID. */
  unsigned int iotlb_entries_used[TRIO_NUM_ASIDS];

  /** Flag used to ensure that only kernel gets mapping to TRIO config space. */
  unsigned int cfg_mmio_mapped;

  /** The service domain number for the kernel. */
  unsigned int os_svc_dom;

#ifdef USE_IOMMU_FOR_RC
  /** Flag used to indicate if the MMU's page size has been set. */
  unsigned int mmu_page_size_set;
#endif

  /** Flag used to indicate if each memory node has been mapped. */
  unsigned int rc_phys_mem_mapped[TILE_MAX_MSHIMS];

  /** Base VA of the Mem Map region that maps to TILE CPA space. */
  unsigned long cpa_map_va_base;
 
  /** Base bus address of the Mem Map region that maps to TILE CPA space. */
  unsigned long cpa_map_bus_base;
 
}
trio_state_t;

/** Interrupt state struct. */
typedef struct
{
  /** The trio state struct. */
  trio_state_t *ts;

  /** MAC instance that triggers the interrupt. */
  unsigned int mac;
}
trio_intr_t;

#endif /* ! _DRIVERS_TRIO_TRIO_H_ */
