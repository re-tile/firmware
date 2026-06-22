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

#ifndef __GXPCI_HOST_H__
#define __GXPCI_HOST_H__

#include <features.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/types.h>

#include <asm/tilegxpci.h>

#include <gxio/trio.h>

#include <gxpci/gxpci.h>

__BEGIN_DECLS

/**
 * Below is the host NIC queue backing memory assignment.
 */

/** The h2t and t2h queue backing memory size. */
#define GXPCI_HOST_QUEUE_BACK_MEM_SIZE tmc_alloc_get_huge_pagesize()

/** The NIC MMIO registers' offset in the backing memory. */
#define GXPCI_HOST_QUEUE_MMIO_REGS_OFFSET 0x0

/** The NIC DMA descriptor ring offset in the backing memory. */
#define GXPCI_HOST_DMA_DESC_RING_OFFSET 0x100000

/** The h2t queue's data pull DMA command ring buffer's offset
    in the backing memory, for receiving data. */
#define GXPCI_HOST_DATA_PULL_DMA_CMDS_BUF_OFFSET 0x800000

/** The t2h queue's data push DMA command ring buffer's offset
    in the backing memory, for sending data. */
#define GXPCI_HOST_DATA_PUSH_DMA_CMDS_BUF_OFFSET 0xa00000

/**
 * The number of entries in the host NIC queue Push DMA descriptor rings.
 * These must be at least as large as the PCIE_CMD_QUEUE_ENTRIES.
 * The hardware supports RING_ORDs of 9 (512 entries), 11 (2048 entries),
 * 13 (8192 entries), and 16 (65536 entries).
 */ 
#define GXPCI_HOST_NIC_PUSH_DMA_RING_ORD 11

/** The number of entries in the host NIC queue push DMA descriptor ring. */
#define GXPCI_HOST_PUSH_DMA_RING_LEN (1 << GXPCI_HOST_NIC_PUSH_DMA_RING_ORD)
    
/**
 * The number of entries in the host NIC queue Pull DMA descriptor rings.
 * These must be at least as large as the PCIE_CMD_QUEUE_ENTRIES.
 * The hardware supports RING_ORDs of 9 (512 entries), 11 (2048 entries),
 * 13 (8192 entries), and 16 (65536 entries).
 */
#define GXPCI_HOST_NIC_PULL_DMA_RING_ORD 11
    
/** The number of entries in the host NIC queue pull DMA descriptor ring. */
#define GXPCI_HOST_PULL_DMA_RING_LEN (1 << GXPCI_HOST_NIC_PULL_DMA_RING_ORD)
 
/** The maximum TX/RX queue number per host VNIC interface. */
#define GXPCI_MAX_HOST_NIC_QUEUES 32

/**
 * The number of cycles to wait before generating an interrupt, if fewer than
 * GXPCI_HOST_NIC_INTR_DMA_INTERVAL DMAs are completed.
 */
#define GXPCI_HOST_NIC_INTR_TIME_INTERVAL 500000

/**
 * The number of DMA completions to wait before triggering an interrupt, if the
 * interrupt timer has not expired.
 */
#define GXPCI_HOST_NIC_INTR_DMA_INTERVAL 64

/**
 * A gxpci_netlib_nic_context_t describes a unidirectional ZC queue.
 * Each ZC client instance has one gxpci_netlib_nic_context_t.
 */
typedef struct gxpci_netlib_nic_context
{
  gxio_trio_context_t *trio_context; /**< TRIO context. */

  gxpci_nic_state_t *nic_state;      /**< NIC state structure. */

  unsigned int nic_index;            /**< VNIC port index. */

  unsigned int trio_index;           /**< TRIO index. */

  unsigned int mac;                  /**< MAC number. */

  unsigned int asid;                 /**< Application space ID. */

  void *netlib_regs;                 /**< Netlib registers. */

  /** Host queue registers for all the h2t queues on a VNIC port. */
  struct gxpci_host_nic_queue_regs *h2t_regs[GXPCI_MAX_HOST_NIC_QUEUES];

  /** Host queue registers for all the t2h queues on a VNIC port. */
  struct gxpci_host_nic_queue_regs *t2h_regs[GXPCI_MAX_HOST_NIC_QUEUES];

} gxpci_netlib_nic_context_t;

/**
 * @brief Open a control link between the local PCIe port
 * and a remote PCIe port, for Netlib.
 *
 * @param[in] context Initialized context object.
 *
 * @return 0 on success, or an error value of type gxio_err_e.
 */
int
gxpci_netlib_open_ctrl_link(gxpci_netlib_nic_context_t *context);

__END_DECLS

#endif // __GXPCI_HOST_H__
