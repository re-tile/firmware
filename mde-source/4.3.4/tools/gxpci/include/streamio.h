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
 * The streamio library allows applications to implement 
 * data transfers on StreamIO link between a tile-gx chip 
 * and a FPGA-like device.
 */ 

#ifndef __STREAMIO_H__
#define __STREAMIO_H__

#include <features.h>
#include <stdint.h>
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


/** IPI event definitions. */
#define IPI_EVENT_MMI           	  (31)
#define IPI_EVENT_PUSH_DMA_COMP 	  (30)
#define IPI_EVENT_PULL_DMA_COMP 	  (29)

/** STREAMIO specific macros. */
#define STREAMIO_PUSH_DMA_RING_LEN        (512) 
#define STREAMIO_PULL_DMA_RING_LEN        (512) 

/** Memory map region interrupt registers' offset in byte. */
#define MEM_MAP_REGISTER_SIZE             (64)

/** FIXME: We limit the max DMA transfer size to 16KB. */
#define MAX_DMA_SIZE                      (0x4000) 
#define STREAMIO_RESOURCE_EMPTY 	  (0xFFFF)
#define STREAMIO_HPAGE_SIZE               tmc_alloc_get_huge_pagesize()
#define STREAMIO_PAGE_SIZE                getpagesize()

/**
 * Error codes that may be returned by the StreamIO APIs.
 * Such functions return 0 on success and a negative value if an error occurs.
 */
enum streamio_err_e {
  /** Largest streamio error number. */
  STREAMIO_ERR_MAX  = -1311,

  /** Invalid parameter. */
  STREAMIO_EINVAL   = -1311,

  /** Process must be bound to a single cpu to invoke streamio APIs. */
  STREAMIO_EBINDCPU = -1312,

  /** Overwrite an initialized resource. */
  STREAMIO_EOVWR    = -1313,

  /** No memory to allocate. */
  STREAMIO_ENOMEM   = -1314,
 
  /** No DMA credits available. */
  STREAMIO_ECREDITS = -1315, 
  
  /** Smallest streamio error number. */
  STREAMIO_ERR_MIN  = -1315
};

typedef struct streamio_resource_s
{
  int asid;  /** Application space ID. */

  /** Memory map regtion array which keeps the allocated map_mem indices. */
  unsigned int map_mem_regions[TRIO_NUM_MAP_MEM_REGIONS];

  /**
   * Memory map tile-side normal address array, 64-byte memory map interrupt 
   * registers are not included to avoid incorrect interrupt triggers. 
   */
  void* map_mems[TRIO_NUM_MAP_MEM_REGIONS];

  /** Memory map interrupt callback handler array. */
  tmc_ipi_func_t mmi_funcs[TRIO_NUM_MAP_MEM_REGIONS];

  /** Push dma ring array which keeps the allocated push dma ring indices. */
  unsigned int push_dma_rings[TRIO_NUM_PUSH_DMA_RINGS];

  /** Push dma queue structure array. */
  gxio_trio_dma_queue_t push_dma_queues[TRIO_NUM_PUSH_DMA_RINGS];

  /** Push dma ring memory array. */
  void* push_dma_mems[TRIO_NUM_PUSH_DMA_RINGS];

  /** Push dma completion interrupt callback handler array. */
  tmc_ipi_func_t push_dma_comp_funcs[TRIO_NUM_PUSH_DMA_RINGS];

  /** Pull dma ring array which keeps the allocated pull dma ring indices. */
  unsigned int pull_dma_rings[TRIO_NUM_PULL_DMA_RINGS];

  /** Pull dma queue structure array. */
  gxio_trio_dma_queue_t pull_dma_queues[TRIO_NUM_PULL_DMA_RINGS];
  
  /** Pull dma ring memory array. */
  void* pull_dma_mems[TRIO_NUM_PULL_DMA_RINGS];
  
  /** Pull dma completion interrupt callback handler array. */
  tmc_ipi_func_t pull_dma_comp_funcs[TRIO_NUM_PULL_DMA_RINGS];

} streamio_resource_t;

/**
 * A streamio_context_t.
 */
typedef struct streamio_context_s
{
  gxio_trio_context_t *trio_context; /** TRIO context. */

  /** Resource that is allocated to this client. */
  streamio_resource_t resource;

  unsigned int trio_index;           /** TRIO index. */

  unsigned int mac;                  /** MAC number. */

} streamio_context_t;


/**
 * @brief Create a communication endpoint which is used to post
 *   data transfer commands.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[out] context Pointer to a context object to be initialized.
 *   Once initialized, this object is supplied to other streamio
 *   routines.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 * @param[in] trio_index The TRIO index of the local StreamIO port,
 *   Gx36 must pass 0.
 * @param[in] mac The mac number of the local StreamIO port,
 *   e.g. 0, 1 or 2.
 *
 * @return 0 on success, GXPCI_EBINDCPU if process is not bound to a single
 *   cpu, or an error value of type streamio_err_e.
 */
int 
streamio_init(gxio_trio_context_t *trio_context, streamio_context_t *context,
              int asid, unsigned int trio_index, unsigned int mac);

/**
 * @brief Initialize a memory map region and register a page to IOTLB.
 *
 * @param[in] context Initialized context object.
 * @param[in] mem_map_index The index of the memory map region.
 * @param[in] tile_mem The tile side memory if pre-allocated, else NULL and 
 *   then this routine will help alloc a map for user. Must aligned to 4KB. 
 * @param[in] map_size The memory map region size, should be 64KB or 16MB. 
 * @param[in] bus_addr The StreamIO side bus address, should be aligned to 4KB. 
 * @param[in] mmi_func The memory map interrupt callback function to user, else
 *   NULL which means the interrrupt will not be enabled for this region.
 *
 * @return 0 on success, or an error value of type streamio_err_e.
 */
int
streamio_init_mem_map(streamio_context_t *context, unsigned int mem_map_index, 
                      void* tile_mem, size_t map_size, 
                      uint64_t bus_address, tmc_ipi_func_t mmi_func);

/**
 * @brief Initialize a push DMA ring.
 *
 * @param[in] context Initialized context object.
 * @param[in] dma_ring_index The index of the push DMA ring.
 * @param[in] push_dma_comp_func The push DMA completion interrupt callback 
 *   function to user (notif bit should be set in the descriptor), else NULL 
 *   which means the interrrupt will not be enabled for this ring. 
 *
 * @return 0 on success, or an error value of type streamio_err_e.
 */
int
streamio_init_push_dma(streamio_context_t *context, unsigned int dma_ring_index,
                       tmc_ipi_func_t push_dma_comp_func);

/**
 * @brief Initialize a pull DMA ring.
 *
 * @param[in] context Initialized context object.
 * @param[in] dma_ring_index The index of the pull DMA ring.
 * @param[in] pull_dma_comp_func The pull DMA completion interrupt callback
 *   function to user (notif bit should be set in the descriptor), else NULL
 *   which means the interrrupt will not be enabled for this ring.
 *
 * @return 0 on success, or an error value of type streamio_err_e.
 */
int 
streamio_init_pull_dma(streamio_context_t *context, unsigned int dma_ring_index,
                       tmc_ipi_func_t pull_dma_comp_func);

/**
 * @brief Write data packets using a pre-init push DMA ring.
 *
 * @param[in] context Initialized context object.
 * @param[in] dma_ring_index The index of a pre-init push DMA ring.
 * @param[in] src_buf Tile side source data buffer, should be pre-registered 
 *   to IOTLB by user using gxio_trio_register_page().
 * @param[in] size The number of data bytes to write.
 * @param[in] streamio_bus_addr The target StreamIO bus address.
 * @param[in] is_notif Set to 0 to disable push DMA completion interrupt, else
 *   to enable it.
 *
 * @return 0 on success, or an error value of type streamio_err_e.
 */
int
streamio_dma_write(streamio_context_t *context, unsigned int dma_ring_index,
                   void* src_buf, uint32_t size, uint64_t streamio_bus_addr, 
                   unsigned int is_notif);

/**
 * @brief Read data packets using a pre-init pull DMA ring.
 *
 * @param[in] context Initialized context object.
 * @param[in] dma_ring_index The index of a pre-init pull DMA ring.
 * @param[in] src_buf Tile side source data buffer, should be pre-registered
 *   to IOTLB by user using gxio_trio_register_page().
 * @param[in] size The number of data bytes to read.
 * @param[in] streamio_bus_addr The target StreamIO bus address.
 * @param[in] is_notif Set to 0 to disable pull DMA completion interrupt, else
 *   to enable it.
 *
 * @return 0 on success, or an error value of type streamio_err_e.
 */
int
streamio_dma_read(streamio_context_t *context, unsigned int dma_ring_index,
                  void* dest_buf, uint32_t size, uint64_t streamio_bus_addr,
                  unsigned int is_notif);

#endif /* __STREAMIO_H__ */
