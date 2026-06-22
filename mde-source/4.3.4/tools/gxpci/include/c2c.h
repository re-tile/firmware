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

/**
 * We allocate a huge page, shared by both the send and the receive queue
 * in a duplex chip-to-chip channel, to contain the flow-control registers
 * and the DMA descriptors, as shown below.
 *
 *      Range                Description
 *      ----------           -----------
 *      0 - 256KB              Exported to the PCI space for remote access,
 *                             as part of the BAR0 range.
 *        0     - 64KB         Send queue state structure.
 *        64KB  - 128KB        Receive queue state structure.
 *        128KB - 256KB        Receive buffer descriptor ring maintained
 *                             by the send queue, filled by receive queue.
 *      (rest of the page is not exported to the PCI space)
 *      1 - 2MB              Send DMA command array.
 *      2 - 3MB              Send CSR Push DMA descriptor ring.
 *      3 - 4MB              Send data Push DMA descriptor ring.
 *      4 - 5MB              Receive buffer descriptor ring
 *      5 - 6MB              Receive CSR Push DMA descriptor ring.
 *
 * We use the BAR2 range to map the receive buffers in a huge page that
 * is allocated by user applications to the PCI space so that the send 
 * queue can push data to the receive queue.
 */
#define GXPCI_C2C_BACK_MEM_SIZE tmc_alloc_get_huge_pagesize()

#define C2C_DATA_PUSH_RING_GEN_BIT GXPCI_C2C_DATA_DMA_RING_ORD
#define C2C_DATA_PUSH_RING_MASK (GXPCI_C2C_DATA_DMA_RING_LEN - 1)

#define C2C_CSR_PUSH_RING_GEN_BIT GXPCI_C2C_CSR_DMA_RING_ORD
#define C2C_CSR_PUSH_RING_MASK (GXPCI_C2C_CSR_DMA_RING_LEN - 1)

/** Offset in the backing memory of the part that is mapped to PCI space. */
#define GXPCI_C2C_MAP_MEMORY_OFFSET 0

/** The send queue state structure offset in the backing memory. */
#define GXPCI_C2C_SEND_QUEUE_STATE_OFFSET GXPCI_C2C_MAP_MEMORY_OFFSET

/** Size of the backing memory that holds the send queue state structure. */
#define GXPCI_C2C_SEND_QUEUE_STATE_SIZE (1 << 16)

/** The receive queue state structure offset in the backing memory. */
#define GXPCI_C2C_RECV_QUEUE_STATE_OFFSET \
        (GXPCI_C2C_SEND_QUEUE_STATE_OFFSET + GXPCI_C2C_SEND_QUEUE_STATE_SIZE)

/** Size of the backing memory that holds the receive queue state structure. */
#define GXPCI_C2C_RECV_QUEUE_STATE_SIZE (1 << 16)

/**
 * The receive buffer descriptor array offset in the backing memory
 * of the sender's address space. This is where the receiver process
 * transfers the descriptors of the receiver buffers, before they are
 * used to generate Push DMAs. Since this space on the sender needs to
 * be accessed by the receiver, it is exposed to the PCI space.
 */
#define GXPCI_C2C_RECV_BUF_DESC_OFFSET \
        (GXPCI_C2C_RECV_QUEUE_STATE_OFFSET + GXPCI_C2C_RECV_QUEUE_STATE_SIZE)

/** Size of the backing memory that holds the recv buffer descriptor array. */
#define GXPCI_C2C_RECV_BUF_DESC_SIZE (1 << 17)

/**
 * The size of the scatter queue mapping region, i.e. the size of huge
 * page memory that is mapped to the PCI space.
 */
#define GXPCI_C2C_SQ_REGION_SIZE \
        (GXPCI_C2C_SEND_QUEUE_STATE_SIZE + GXPCI_C2C_RECV_QUEUE_STATE_SIZE + \
         GXPCI_C2C_RECV_BUF_DESC_SIZE)

/** Send queue DMA command array offset in the backing memory. */
#define GXPCI_C2C_SEND_DMA_CMDS_OFFSET 0x100000

/** Send queue data Push DMA descriptor ring offset in the backing memory. */
#define GXPCI_C2C_SEND_DATA_DMA_RING_OFFSET 0x300000

/**
 * Receive queue buffer descriptor ring offset in the backing memory
 * of the receiver's address space. This is where the receiver process
 * posts the descriptors of the receiver buffers, before they are
 * transferred to the sender's address space via Push DMAs.
 */
#define GXPCI_C2C_RECV_BUF_DESC_RING_OFFSET 0x500000

/**
 * Receive queue CSR Push DMA descriptor ring offset in the backing memory.
 * This DMA ring is used to write receive buffer descriptors to the sender.
 */
#define GXPCI_C2C_RECV_CSR_DMA_RING_OFFSET 0x600000

/**
 * The base address of the BAR0 segment that is used to map the C2C registers
 * and DMA descriptors. Right now, it starts at 5MB and uses the upper 3MB
 * of BAR0 range. For a C2C channel that communicates with a remote port with
 * link index N, its part of the BAR0 segment starts at:
 *   GXPCI_C2C_BAR0_OFFSET + (N * GXPCI_C2C_SQ_REGION_SIZE).
 * With the total usable BAR0 range being 3MB and each duplex channel
 * using 256KB, 12 Gx ports are supported.
 */
#define GXPCI_C2C_BAR0_OFFSET 0x500000

/** The C2C max receive buffer size. This is determined by the push DMA
    descriptor's xsize field width. For Gx36, 16KB is the limit. */
#define GXPCI_C2C_MAX_RECV_BUF_SIZE  (1 << TRIO_DMA_DESC_WORD0__XSIZE_WIDTH)

/**
 * The order of number of entries in the C2C data Push DMA descriptor ring.
 * The hardware supports RING_ORDs of 9 (512 entries), 11 (2048 entries),
 * 13 (8192 entries), and 16 (65536 entries).
 */
#define GXPCI_C2C_DATA_DMA_RING_ORD 11

/**
 * The order of number of entries in the C2C CSR Push DMA descriptor ring.
 * The hardware supports RING_ORDs of 9 (512 entries), 11 (2048 entries),
 * 13 (8192 entries), and 16 (65536 entries).
 */
#define GXPCI_C2C_CSR_DMA_RING_ORD 11

/** The number of entries in the data Push DMA descriptor ring. */
#define GXPCI_C2C_DATA_DMA_RING_LEN (1 << GXPCI_C2C_DATA_DMA_RING_ORD)

/** The number of entries in the CSR Push DMA descriptor ring. */
#define GXPCI_C2C_CSR_DMA_RING_LEN (1 << GXPCI_C2C_CSR_DMA_RING_ORD)

/** The C2C receiver port ready flag. */
#define GXPCI_C2C_RECV_PORT_READY 2

/** The maximum number of commands that can be outstanding on a
    particular chip-to-chip command queue. */
#define GXPCI_C2C_MAX_CMDS 1024

/**
 * The size of the BAR2 segment that is used to map the receive buffers
 * for each receive queue. For a C2C channel that communicates with a 
 * remote port with link index N, its part of the BAR2 segment starts at
 * offset: (N * GXPCI_C2C_RECV_DATA_REGION_SIZE).
 * With the default BAR2 range being 128MB, using a single huge page
 * per receive queue supports 8 Gx ports, including the Gx port that
 * functions as the RC. Max 7 Gx ports are supported if the RC is non-Gx.
 */
#define GXPCI_C2C_RECV_DATA_REGION_SIZE tmc_alloc_get_huge_pagesize()

/**
 * Receiver buffer descriptor structure.
 * The receiver sends one or more receiver buffer descriptors to
 * the sender at a time, to help the sender build the DMA request.
 * The receiver buffer address cannot be used directly because it
 * is the receiver buffer's virtual address. To generate the receive
 * buffer's PCI address, the sender calculates the receiver buffer's
 * offset in the receive huge page and adds it to the BAR2 address
 * that is mapped to the receive huge page. This design can be
 * extended to support multiple receive huge pages by adding the
 * huge page's index to the receive buffer descriptor, in the future.
 */
typedef struct recv_cmd
{
  void *addr;           /**< Receiver buffer address. */
  uint64_t size;        /**< Size of buffer. */
}
recv_cmd_t;

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
  uint32_t recv_offset;      /**< Receiver buffer address. */
  uint16_t size;	/**< Size of transfer. */
  uint8_t send_filled;	/**< Set if this dma_cmd is filled by sender. */
  uint8_t recv_filled;	/**< Set if this dma_cmd is filled by receiver. */
}
dma_cmd_t;

/** State of the sender, containing information of the local send port and
    the remote receive port. */
struct gxpci_send_state
{
  /** Indicates whether the sender port is ready.  This is set by the sender
      and read by the receiver. */
  volatile uint32_t ready;

  /** This is the receive packet headroom size, set by the receiver.
      The sender adds this value to the target base PCI address. */ 
  volatile uint32_t pkt_headroom;

  /**
   * Page mask that is used by the sender to obtain the receive buffers'
   * in-page offset, in generating the buffers' PCI address.
   */
  volatile uint64_t recv_page_mask;

  /**
   * Receive buffer page's base PCI address.
   */
  volatile uint64_t recv_page_base;

  /** The receiver's gxpci_recv_state struct's PCI address. */
  uint64_t recv_state;

  /** DMA commands list. */
  dma_cmd_t *dma_cmds;

  /** Receive commands list. */
  recv_cmd_t *recv_cmds;

  /** Number of parsed send commands. */
  uint32_t send_cmds_posted;
  /** Number of consumed send commands. */
  uint32_t send_cmds_consumed;

  /** Number of completed DMAs. */
  uint32_t send_cmds_completed;

  /** Number of parsed receive commands. */
  uint32_t recv_cmds_consumed;
};

/** State of the receiver. */
struct gxpci_recv_state
{
  /** Indicates whether the receiver port is ready. */
  uint32_t ready;

  /**
   * This is the receive packet headroom size, set by the receiver.
   * The sender adds this value to the target base PCI address.
   */ 
  uint32_t pkt_headroom;

  /**
   * Page mask that is used by the sender to obtain the receive buffers'
   * in-page offset, in generating the buffers' PCI address.
   */
  uint64_t recv_page_mask;

  /** The sender's gxpci_send_state struct's PCI address. */
  uint64_t send_state;

  /** PCI address of receive commands list at sender. */
  uint64_t recv_cmds_at_sender;

  /** Receive commands list. */
  recv_cmd_t *recv_cmds;

  /** Number of posted recv commands. */
  uint32_t recv_cmds_posted;

  /**
   * Number of consumed recv commands, whose slots can be reused.
   */
  uint32_t recv_cmds_consumed;

  /**
   * Number of posted recv commands whose DMAs to the sender
   * have been completed.
   */
  uint32_t recv_cmds_sent;

  /**
   * Flag indicating that the timer is ticking, to determine when is safe 
   * to retrieve the last arriving packet.
   */
  uint32_t ticking;

  /**
   * This holds the starting cycle count, taken when a single packet becomes
   * available and the above timer isn't started yet, i.e. ticking == 0.
   */
  uint64_t timer_start;
};

__END_DECLS

#endif // __GXPCI_C2C_H__
