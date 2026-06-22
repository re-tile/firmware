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

#ifndef __GXPCI_C2C_H__
#define __GXPCI_C2C_H__

#include <features.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/types.h>

#include <asm/tilegxpci.h>

#include <gxio/trio.h>

__BEGIN_DECLS

/** The offset of the chip-to-chip send port registers in BAR0. */
#define GXPCI_C2C_SEND_REGS_OFFSET		0x500000

/** The offset of the chip-to-chip receive port registers in BAR0. */
#define GXPCI_C2C_RECV_REGS_OFFSET		0x600000

/** The offset of the chip-to-chip transfer target data address in BAR0. */
#define GXPCI_C2C_RECV_DATA_ADDR_OFFSET		0x700000

/**
 * The number of entries in the C2C send queue Push DMA descriptor rings.
 * These must be at least as large as three times the maximum number of
 * SQ FIFO entries, i.e. (1 << TRIO_LOG2_NUM_SQ_FIFO_ENTRIES), where the
 * 3 comes from the fact that 3 Push DMA slots are used for each C2C xfer.
 * The hardware supports RING_ORDs of 9 (512 entries), 11 (2048 entries),
 * 13 (8192 entries), and 16 (65536 entries).
 */
#define GXPCI_C2C_PUSH_DMA_RING_ORD 9

/** The number of entries in the push DMA descriptor ring. */
#define GXPCI_C2C_PUSH_DMA_RING_LEN (1 << GXPCI_C2C_PUSH_DMA_RING_ORD)

/** The C2C sender backing memory size. */
#define GXPCI_C2C_SEND_BACK_MEM_SIZE tmc_alloc_get_huge_pagesize()

/** The C2C receiver backing memory size. */
#define GXPCI_C2C_RECV_BACK_MEM_SIZE getpagesize()

/** The C2C sender port ready flag. */
#define GXPCI_C2C_SEND_PORT_READY 1

/** The C2C receiver port ready flag. */
#define GXPCI_C2C_RECV_PORT_READY 2

/** Size of buffer in the backing memory that is mapped to the PCI space. */
#define GXPCI_C2C_QUEUE_MEM_MAP_SIZE (getpagesize())

/** The queue state structure offset in the backing memory. */
#define GXPCI_C2C_QUEUE_STATE_OFFSET		0

/** The maximum number of commands that can be outstanding on a
    particular chip-to-chip command queue. */
#define GXPCI_C2C_MAX_CMDS (1 << TRIO_LOG2_NUM_SQ_FIFO_ENTRIES)

/** The C2C max receive buffer size. This is determined by the push DMA
    descriptor's xsize field width. For Gx36, 16KB is the limit. */
#define GXPCI_C2C_MAX_RECV_BUF_SIZE  (1 << TRIO_DMA_DESC_WORD0__XSIZE_WIDTH)

/** The size of the scatter queue mapping region. This cannot be less than
    the sum of receive packet headroom size and the max recv buffer size. */
#define GXPCI_C2C_SQ_REGION_SIZE (GXPCI_C2C_MAX_RECV_BUF_SIZE << 1)

/**
 * A structure that is used to keep track of pending commands
 * from both sides, match them, and build the DMA list.  We
 * keep an array of these structures large enough to hold every
 * possible issued command. This array is kept on the sender side
 * only because the sender is responsible for generating the DMAs.
 */
typedef struct dma_cmd
{
  void *send_addr;      /**< Sender buffer address. */
  void *recv_addr;      /**< Receiver buffer address. */
  uint64_t size;	/**< Size of transfer. */
}
dma_cmd_t;

/** State of the sender, containing information of the local send port and
    the remote receive port. */
struct gxpci_send_state
{
  /** Indicates whether the receiver port is ready. This is set by the
      receiver and read by the sender. */
  uint32_t ready;

  /** Size of the SQ mapping region. */
  uint32_t sq_region_size;

  /** Base PCI address of the remote SQ region. */
  uint64_t sq_bus_addr;

  /** PCI address to which data is sent. */
  uint64_t sq_data_addr;

  /** Size of the SQ receive buffer size. */
  uint32_t recv_buf_size;

  /** This is the receive packet headroom size, set by the receiver. The sender
      adds this value to the target base PCI address for the first buffer
      of the packet. */
  uint32_t pkt_headroom;

  /** Remote PCI address of the doorbell register. */
  uint64_t doorbell_bus_addr;

  /** The doorbell value. */
  TRIO_MAP_SQ_DOORBELL_FMT_t doorbell;

  /** The receiver's gxpci_recv_state struct's PCI address. */
  uint64_t recv_state;

  /** DMA commands list. */
  dma_cmd_t dma_cmds[GXPCI_C2C_MAX_CMDS] __attribute__((__aligned__(64)));

  /** Number of started DMAs. */
  uint32_t dmas_started __attribute__((__aligned__(64)));
  /** Number of completed DMAs. */
  uint32_t dmas_completed;

  /** Push DMA queue rolling counter. */
  uint32_t dma_queue_counter;

  /** Number of parsed send commands. */
  uint32_t send_cmds_posted;
  /** Number of consumed send commands. */
  uint32_t send_cmds_consumed;
};

/**
 * Receiver buffer descriptor structure.
 */
typedef struct recv_cmd
{
  void *recv_addr;      /**< Sender buffer address. */
  uint64_t size;        /**< Size of transfer, set by sender. */
}
recv_cmd_t;

/** State of the receiver. */
struct gxpci_recv_state
{
  /** Receive commands list. */
  recv_cmd_t recv_cmds[GXPCI_C2C_MAX_CMDS] __attribute__((__aligned__(64)));

  /** Number of posted recv commands. */
  uint32_t recv_cmds_posted __attribute__((__aligned__(64)));
  /** Number of completed recv commands. */
  uint32_t recv_cmds_completed;
  /** Number of consumed recv commands. */
  uint32_t recv_cmds_consumed;

  /** SQ completion rolling counter. */
  uint32_t sq_comp_counter;

  /** Send state address. This is a VA. */
  struct gxpci_send_state *send_state;
};

__END_DECLS

#endif // __GXPCI_C2C_H__
