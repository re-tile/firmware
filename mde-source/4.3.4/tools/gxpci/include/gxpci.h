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
 * The gxpci library allows applications to post zero copy
 * data transfer commands to the Gx PCIe ports.
 */ 
    
#ifndef __GXPCI_H__
#define __GXPCI_H__

#include <features.h>
#include <stdint.h>
#include <unistd.h>

#include <asm/tilegxpci.h>

#include <gxio/trio.h>

#include <linux/pci_regs.h>

#include <sys/types.h>
#include <sys/param.h>		// Get MIN() and MAX().

#include <tmc/alloc.h>


__BEGIN_DECLS

#ifndef __DOXYGEN__

#define GXIO_VERIFY_ZERO(VAL, WHAT)                            \
  do {                                                         \
    long long __val = (VAL);                                   \
    if (__val != 0)                                            \
    {                                                          \
      fprintf(stderr, "Failure in '%s': %lld", (WHAT), __val); \
      return errno;                                            \
    }                                                          \
  } while (0)

#define GXIO_VERIFY_NON_NEGATIVE(VAL, WHAT)                    \
  do {                                                         \
    long long __val = (VAL);                                   \
    if (__val < 0)                                             \
    {                                                          \
      fprintf(stderr, "Failure in '%s': %lld", (WHAT), __val); \
      return errno;                                            \
    }                                                          \
  } while (0)

#endif

/**
 * @defgroup gxpci gxpci PCIe and StreamIO Data Transfer API
 * @{
 *
 * The Gx Chip-to-Chip (C2C), host NIC and host Packet Queue API
 * allows applications to post PCIe zero-copy commands to an unidirectional
 * data transfer queue which is defined by the queue type gxpci_queue_type_t,
 * the queue index and link index of the two peer PCIe ports. The API
 * eliminates the Linux system calls from the data transfer process and 
 * is able to achieve low overhead and high throughput.
 *
 * The Gx C2C and H2T/T2H API libgxpci is implemented on top of the libgxio API
 * for allocating, configuring, and manipulating Gx TRIO hardware resources.
 *
 * @section Initialization
 * 
 * First, bind each process that will communicate from a Gx processor
 * to a unique core, so that each process cannot migrate to another
 * core during the course of its execution. 
 * 
 * Second, each process invokes the libgxio gxio_trio_init() to allocate
 * a TRIO context or "service domain". The initialized TRIO context will
 * be passed to all libgxpci API functions. gxio_trio_init() is the only
 * libgxio function that needs to called explicitly by the application.
 *
 * Next, invoke gxpci_init() to allocate a gxpci context which includes
 * a reference to the aforementioned TRIO context. The gxpci context is
 * also associated with the local PCIe port whose MAC index is passed
 * to gxpci_init(). Note that one gxpci context is associated with one
 * process or thread that should monopolize a single core. It is suggested
 * that multiple worker threads could feed into a service thread that
 * owns the gxpci context.
 * 
 * Next, invoke gxpci_open_queue() to create a data transfer queue, passing
 * in queue type, queue index within that type and the remote PCIe port link
 * index.  The ::GXPCI_NIC_H2T and ::GXPCI_NIC_T2H types specify the H2T/T2H
 * queues while ::GXPCI_C2C_SEND and ::GXPCI_C2C_RECV types create C2C queues.
 * 
 * @section Memory Management
 *
 * The gxpci API requires that applications use special "registered" memory
 * to store their packet data.  Memory is registered via the 
 * gxpci_iomem_register() function, which takes the virtual address of a 
 * range of huge page memory and sets up the VA/PA mapping needed by the TRIO
 * engine.
 *
 * @section Data Transfer 
 *
 * At this point, the process can use gxpci_post_cmd() to post commands and
 * gxpci_get_comps() to get completions for those commands. Since there are
 * a limited number of commands outstanding on any communication queue,
 * trying to have more commands outstanding than the limit will cause
 * the gxpci_post_cmd() to return error type GXPCI_ECREDITS which is 
 * a hint to the application to retry the command.
 * 
 * @section Queue Reset and Resource Deallocation
 *
 * Once a queue is reset, all SW and HW resources associated with this
 * queue are released. The queue must start from scratch to run again.
 * If the reset is initiated by the tile application, it should simply
 * call gxpci_destroy() function. If the reset is initiated by the remote
 * end, the reset condition will be detected by checking for return value
 * GXPCI_ERESET from functions gxpci_open_queue(), gxpci_post_cmd() and
 * gxpci_get_comps(). Upon detecting the queue reset event, the tile
 * application should call gxpci_destroy() before exiting or restarting.
 * 
 * @section Examples
 * 
 * See $TILERA_ROOT/examples/trio/gxpci_c2c/c2c_send.c for an example of
 * chip-to-chip zero-copy messaging using the gxpci API.
 * 
 */

/**
 * Max number of VFs that share a single ASID.
 */
#define VFS_SHARE_AN_ASID			4

/**
 * Max size for each Push or Pull DMA descriptor ring buffer. This is
 * set to 1MB because hardware supports max RING_ORDs of 16 (65536 entries),
 * with each entry being 16-byte gxio_trio_dma_desc_t. This is used by
 * the VFs in sharing a huge page to store their DMA descriptor rings.
 */
#define MAX_DMA_DESC_RING_BUF			(1 << 20)

/**
 * Below is the Raw DMA queue definitions.
 */

/** The Raw DMA queue backing memory size. */
#define GXPCI_RAW_DMA_BACK_MEM_SIZE tmc_alloc_get_huge_pagesize()

/** The Raw DMA queue's host MMIO registers' offset
 *  in the backing memory.
 */
#define GXPCI_HOST_RD_REGS_OFFSET		0x0

/**
 * The Raw DMA queue's data DMA command ring buffer's offset
 * in the backing memory.
 */
#define GXPCI_HOST_RD_DMA_CMDS_BUF_OFFSET	0x200000

/**
 * Different types of zero-copy queues. Each queue provides
 * unidirectional data transfer path between two Tile Processors
 * or a Tile Processor and the host machine. The unidirectional
 * nature allows for fine-grained hardware resource allocation.
 *
 */
typedef enum gxpci_queue_type_e {

  /** Host to TILE: this is the egress queue of the host NIC interface. */
  GXPCI_NIC_H2T,

  /** TILE to host: this is the ingress queue of the host NIC interface. */
  GXPCI_NIC_T2H,

  /** Packet Queue H2T: this is the endpoint reading data from the host. */
  GXPCI_PQ_H2T,

  /** Packet Queue T2H: this is the endpoint writing data to the host. */
  GXPCI_PQ_T2H,

  /** Packet Queue Duplex: data transfer between the host and the endpoint. */
  GXPCI_PQ_DUPLEX,

  /** TILE to TILE: this is the sender TILE PCIe port. */
  GXPCI_C2C_SEND,

  /** TILE to TILE: this is the receiver TILE PCIe port. */
  GXPCI_C2C_RECV,

  /** TILE to TILE: bi-directional transfer queues. */
  GXPCI_C2C_DUPLEX,

  /** Raw DMA: this is the sender. */
  GXPCI_RAW_DMA_SEND,

  /** Raw DMA: this is the receiver. */
  GXPCI_RAW_DMA_RECV,

  /** Packet Queue H2T for SR-IOV VF. */
  GXPCI_PQ_H2T_VF,

  /** Packet Queue T2H for SR-IOV VF. */
  GXPCI_PQ_T2H_VF,

  /** Duplex PQ for VF: data transfer between a guest and the endpoint. */
  GXPCI_PQ_DUPLEX_VF,

} gxpci_queue_type_t;

/** A structure used to post zero-copy commands. */
typedef struct
{
  void* buffer;                   /**< Buffer virtual address. */

  /** Size of the buffer, maximum 16KB for Gx36. Any values larger than 16KB
      will be treated as 16KB. */
  uint32_t size:GXPCI_MAX_CMD_SIZE_BITS;

  uint32_t reserved:(32 - GXPCI_MAX_CMD_SIZE_BITS);	/**< Must be zero. */

}
gxpci_cmd_t;

/** A structure used to post DMA commands. */
typedef struct
{
  void* buffer;                   /**< Buffer virtual address. */

  /**
   * Size of the buffer, maximum 16KB for Gx36. Any values larger than 16KB
   * will be treated as 16KB.
   */
  uint32_t size:GXPCI_MAX_CMD_SIZE_BITS;

  uint32_t reserved:(32 - GXPCI_MAX_CMD_SIZE_BITS);     /**< Must be zero. */

  /**
   * Offset in bytes of the remote target buffer's PCI address from
   * the base PCI address of the host's Raw DMA buffer.
   */
  uint32_t remote_buf_offset;
}
gxpci_dma_cmd_t;

/** The structure returned when a command completes. */
typedef struct
{
  void* buffer;                   /**< Buffer virtual address. */

  /** Size of the actual data transfer. */
  uint32_t size:GXPCI_MAX_CMD_SIZE_BITS;

  uint32_t reserved:(32 - GXPCI_MAX_CMD_SIZE_BITS); /**< Must be zero. */
}
gxpci_comp_t;

/** The structure returned when a host NIC command completes. */
typedef struct
{
  /** Buffer virtual address. */
  void* buffer;

  /** Packet descriptor and data transfer status. */
  tile_nic_tile_desc_t desc;
}
gxpci_nic_comp_t;

/**
 * A gxpci_nic_state_t describes the NIC-wide state, as opposed to
 * queue-specific resource structure gxpci_resource_t. This should
 * contain state information that is used once at app init time.
 */
typedef struct gxpci_nic_state_s
{
  /** TRIO index. */
  unsigned int trio_index;

  /** Application space ID. */
  unsigned int asid;

  /** MAC number. */
  unsigned int mac;

  /** VF index if this NIC belongs to a VF. */
  int vf_index;

  /** NIC interface index. */
  int nic_index;

  /** File handle for NIC channel reset control. */
  int reset_fd;

  /** File handle for barmem access. */
  int barmem_fd;

  /** Number of host virtual NIC interfaces. */
  unsigned int num_nic_ports;

  /** Number of TX queues per virtual NIC interface. */
  unsigned int num_nic_tx_queues;

  /** Number of RX queues per virtual NIC interface. */
  unsigned int num_nic_rx_queues;

  unsigned long bar0_size;    /**< Local BAR0 size. */
  unsigned long bar0_addr;    /**< Local BAR0 base address. */
  unsigned long bar2_addr;    /**< Local BAR2 base address. */

  /** The MSI-X interrupt vector base number of the host NICs. */
  int host_nic_intr_vec_base;

  /** MSI-X table size in bytes. */
  unsigned int msix_table_size;

  /** Pointer to MSI-X table. */
  void *msix_table_base;

  /** Backing memory for NIC, shared by all the tx/rx queues. */
  void *backing_mem;

  /** Backing memory for NIC registers, shared by all the tx/rx queues. */
  struct gxpci_host_nic_regs *nic_regs;

  /** Backing memory for NIC DMA rings, shared by all the tx/rx queues. */
  struct gxpci_host_nic_desc *nic_desc;

  /** The mmap'ed address to the VF BAR0 space. */
  void *vf_barmem;

  /** The size of the vf_barmem. */
  unsigned long vf_barmem_size;

  /** Backing memory for VF NIC registers, shared by all the tx/rx queues. */
  struct gxpci_host_nic_regs_vf *nic_regs_vf;

  /** Backing memory for VF NIC DMA rings, shared by all the tx/rx queues. */
  struct gxpci_host_nic_desc_vf *nic_desc_vf;

  /** DMA ring info for all the H2T queues. */
  int h2t_dma_queue[GXPCI_HOST_NIC_SIMPLEX_QUEUES_VF_MAX];

  /** DMA ring info for all the T2H queues. */
  int t2h_dma_queue[GXPCI_HOST_NIC_SIMPLEX_QUEUES_VF_MAX];

} gxpci_nic_state_t;

/**
 * We need to embed a chip-to-chip packet header in the beginning of
 * each packet for the following reasons:
 *   -- to transport the size of packet data payload to the receiver,
 *      in the absence of dedicated metadata packets. The sender application
 *      should set the data payload size in this header. The PCIe packet
 *      payload size is the data payload size plus the c2c_pkt_header_t size.
 *   -- to include a packet availability flag informing the receiver of the
 *      the packet arrival. This flag should also be set by the sender
 *      application.
 */
typedef struct c2c_pkt_header
{
  uint32_t ready;	/**< Flag indicating the packet availability. */
  uint32_t size;	/**< Packet payload size, excluding this header. */
}
c2c_pkt_header_t;

/**
 * This describes the Raw DMA channel-wide state, as opposed to
 * queue-specific resource structure gxpci_resource_t. This should
 * contain state information that is used once at app init time.
 * A Raw DMA channel may include multiple T2H and H2T queues.
 */
typedef struct gxpci_raw_dma_state_s
{
  /** TRIO index. */
  unsigned int trio_index;

  /** Application space ID. */
  unsigned int asid;

  /** MAC number. */
  unsigned int mac;

  /** File handle for barmem access. */
  int barmem_fd;

  /** Number of T2H queues. */
  unsigned int num_rd_t2h_queues;

  /** Number of H2T queues. */
  unsigned int num_rd_h2t_queues;

  unsigned long bar0_size;    /**< Local BAR0 size. */
  unsigned long bar0_addr;    /**< Local BAR0 base address. */

  /** The MSI-X interrupt vector base number of the Raw DMA queues. */
  int rd_q_intr_vec_base;

  /** MSI-X table size in bytes. */
  unsigned int msix_table_size;

  /** Pointer to MSI-X table. */
  void *msix_table_base;

  /** Backing memory for MMIO registers, shared by all t2h/h2t queues. */
  void *mmio_mem;

  /** Memory for Pull DMA descriptor rings, shared by all h2t queues. */
  void *h2t_dma_mem;

  /** Memory for Push DMA descriptor rings, shared by all t2h queues. */
  void *t2h_dma_mem;

} gxpci_raw_dma_state_t;

/**
 * This describes the Packet Queue unidirectional queue state.
 */
struct gxpci_packet_queue_state
{
  /** Bus addresses of the PA-contiguous segments in host memory. */
  uint64_t segment_bus_addr[HOST_PQ_SEGMENT_MAX_NUM];

  /** Number of the PA-contiguous segments. */
  uint32_t num_segments;

  /** Size of a single PA-contiguous segment in bytes. */
  uint32_t segment_size;

  /** Size of a single packet buffer in bytes. */
  uint32_t buf_size;

  /** Packet buffer size order. */
  uint32_t buf_size_order;

  /** Mask of the ring credits. */
  uint32_t cred_mask __attribute__ ((__aligned__(64)));

  /** Total credits of the ring. */
  uint32_t cred_total;

  /** File handle for mapping the BAR0 memory. */
  int barmem_fd;

  /** Size of the BAR0 memory mapping. */
  int barmem_size;

  /** Mapped address for the BAR0 memory. */
  void *bar_mem;
};

/**
 * A gxpci_resource_t describes the resources that are allocated to
 * a ZC client. Each client has one gxpci_resource_t. Note that
 * the types of resources allocated for a client depends on the
 * queue type.
 *
 * A GXPCI_NIC_H2T queue requires the following resources:
 *   push-DMA, pull-DMA, Memory Map
 * A GXPCI_NIC_T2H queue requires the following resources:
 *   push-DMA, Memory Map, pull-DMA
 * A GXPCI_PQ_H2T queue requires the following resources:
 *   pull-DMA, Memory Map, PIO
 * A GXPCI_PQ_T2H queue requires the following resources:
 *   push-DMA, Memory Map, PIO
 * A GXPCI_C2C_SEND queue requires the following resources:
 *   push-DMA, Memory Map
 * A GXPCI_C2C_RECV queue requires the following resources:
 *   Scatter_Queue, Memory Map, PIO
 */
typedef struct gxpci_resource_s
{
  union {
    struct {
      gxio_trio_dma_queue_t pull_dma_queue; /**< Pull DMA queue body. */
      gxio_trio_dma_queue_t push_dma_queue; /**< Push DMA queue body. */

      struct gxpci_host_nic_queue_regs *regs;   /**< Host MMIO registers. */
      gxpci_nic_state_t *nic_state;    /**< Pointer to host NIC state. */

      tile_nic_dma_cmd_t *dma_cmd_array;     /**< DMA commands list. */
      uint32_t dmas_started;        /**< Number of started DMAs. */
      uint32_t dmas_completed;      /**< Number of completed DMAs. */

      /** Number of posted tile commands. */
      uint32_t tile_cmds_posted;
      /** Number of consumed tile commands. */
      uint32_t tile_cmds_consumed;

      unsigned long *msix_addr;        /**< Pointer to MSI-X message address. */
      unsigned int *msix_data;         /**< Pointer to MSI-X message data. */

      /** Interrupt timer start cycle value, set when the first DMA(s) are
          completed after a previous interrupt. */
      uint64_t intr_timer_start;

      /** Snapshots of number of completed DMAs, taken at interrupt generation
          time, used to throttle the interrupt rate. */
      uint32_t dmas_upon_intr;

      /** The interrupt pending register, internal to tile. */
      unsigned int interrupt_pending;

      /** The receive buffer length of the host. */
      unsigned int host_rx_buf_len;
    } host;

    struct {
      gxio_trio_dma_queue_t dma_queue_data; /**< Data DMA queue body. */

      struct gxpci_host_pq_regs_drv *drv_regs;  /**< Host MMIO drv registers. */
      struct gxpci_host_pq_regs_app *app_regs;  /**< Host MMIO app registers. */

      /**< Pointer to host queue state. */
      struct gxpci_packet_queue_state *queue_state;

      /** Number of H2T queues per packet queue interface. */
      unsigned int num_pq_h2t_queues;

      /** Number of T2H queues per packet queue interface. */
      unsigned int num_pq_t2h_queues;
    } host_pq;

    struct {
      gxio_trio_dma_queue_t dma_queue_data; /**< Data DMA queue body. */
      struct gxpci_host_rd_regs_drv *drv_regs;  /**< Host MMIO drv registers. */
      struct gxpci_host_rd_regs_app *app_regs;  /**< Host MMIO app registers. */
      gxpci_raw_dma_state_t *rd_state; /**< Pointer to Raw DMA channel state. */
      unsigned int rd_buf_size;        /**< Host Raw DMA buffer size. */
      unsigned int dma_cpl_cnt;        /**< HW DMA complete count. */

#ifdef RAW_DMA_USE_RESERVED_MEMORY
      /** PCI bus address of the single reserved host Raw DMA buffer. */
      uint64_t rd_buf_addr;
#else
      /** PCI bus addresses of the PA-contiguous segments. */
      uint64_t rd_segment_addr[HOST_RD_SEGMENT_MAX_NUM];
#endif

      unsigned long *msix_addr;        /**< Pointer to MSI-X message address. */
      unsigned int *msix_data;         /**< Pointer to MSI-X message data. */

      /** The interrupt pending register, internal to tile. */
      unsigned int interrupt_pending;
    } raw_dma;

    struct {
      gxio_trio_dma_queue_t dma_queue; /**< The push DMA queue body. */
      struct tlr_c2c_status *queue_sts; /**< Send queue status. */
      void *queue_state;               /**< Pointer to sender queue state. */
    } c2c_send;

    struct {
      unsigned int pio;                /**< PIO index. */
      unsigned int scatter_queue;      /**< Scatter Queue index. */
      struct tlr_c2c_status *queue_sts; /**< Receive queue status. */
      void *queue_state;               /**< Pointer to receiver queue state. */
    } c2c_recv;

    struct {
      gxio_trio_dma_queue_t dma_queue; /**< The push DMA queue body. */
      void *mmap_reg_base;             /**< Shared memory for queue status. */
      struct tlr_c2c_status *queue_sts; /**< Send queue status. */
      void *queue_state;                /**< Pointer to sender queue state. */
      unsigned long local_bar0_addr;    /**< Local BAR0 base address. */
      unsigned long remote_bar0_addr;   /**< Remote BAR0 base address. */
      unsigned long remote_bar2_addr;   /**< Remote BAR2 base address. */
    } send_c2c;

    struct {
      gxio_trio_dma_queue_t csr_queue; /**< The push DMA queue body. */
      void *mmap_reg_base;             /**< Shared memory for queue status. */
      struct tlr_c2c_status *queue_sts; /**< Receive queue status. */
      void *queue_state;                /**< Pointer to receiver queue state. */
      unsigned long local_bar0_addr;    /**< Local BAR0 base address. */
      unsigned long remote_bar0_addr;   /**< Remote BAR0 base address. */
      unsigned long local_bar2_addr;    /**< Local BAR2 base address. */
    } recv_c2c;
  };

  unsigned int asid;                   /**< Application space ID. */

  void *backing_mem;                   /**< Backing memory. */

} gxpci_resource_t;

/**
 * A gxpci_context_t describes a unidirectional ZC queue.
 * Each ZC client instance has one gxpci_context_t.
 */
typedef struct gxpci_context_s
{
  gxio_trio_context_t *trio_context;   /**< TRIO context. */

  /** VA of the mapped gxpci_host_regs struct, needed by C2C only. */
  void *mmio_reg_base;

  /** Resource that is allocated to this client. */
  gxpci_resource_t resource;

  int fd;                              /**< File handle for driver access. */

  unsigned int trio_index;             /**< TRIO index. */

  unsigned int mac;                    /**< MAC number. */

  unsigned int rem_link_index;         /**< Remote PCIe port index. */

  unsigned int local_link_index;       /**< Local PCIe port index. */

  gxpci_queue_type_t type;             /**< Communication queue type. */

  /** Queue index. Within a PCIe domain, all the queues of the same C2C queue 
   *  type are indexed. It is the user's responsibility to assign an unique
   *  queue index to each pair of applications on the two ends of the
   *  PCIe link. The queues of the H2T and T2H queue types are indexed within
   *  a single PCIe link. */
  unsigned int queue_index;

  /** The number of commands completed. The initial value is 0. */
  unsigned int completed;

  /** The number of commands credits. The initial value is 0. */
  unsigned int credits;

} gxpci_context_t;
 
/**
 * In some applications, such as the host NIC interface, the queue_index
 * field of gxpci_context_t consists of two indices: the interface index
 * at the low half-word and the queue index at the high half-word.
 * One interface has one or more channels. Each channel is either
 * a xmit/recv queue pair or a unidirectional queue.
 */
#define GXPCI_QUEUE_INDEX_INTERFACE_MASK	0xffff
 
/** The shift of the queue index part. */
#define GXPCI_QUEUE_INDEX_QUEUE_SHIFT		16

/**
 * Error codes that may be returned by the PCI zero-copy command posting APIs.
 * Such functions return 0 on success and a negative value if an error occurs.
 *
 * In cases where a gxpci function failed due to a error reported by
 * system libraries, the error code will be the negation of the system
 * errno at the time of failure.  The @ref gxpci_strerror() function
 * will deliver error strings for both gxpci and system error codes.
 */
enum gxpci_err_e {
  /** Largest gxpci error number. */
  GXPCI_ERR_MAX = -1301,

  /** Invalid parameter. */
  GXPCI_EINVAL = -1301,

  /** Process must be bound to a single cpu to invoke gxpci APIs. */
  GXPCI_EBINDCPU = -1302,

  /** Insufficient command credits to post command. */
  GXPCI_ECREDITS = -1303,

  /** The queue is being reset. */
  GXPCI_ERESET = -1304,

  /** Smallest gxpci error number. */
  GXPCI_ERR_MIN = -1304
};

/**
 * @brief Create a communication endpoint which is used to post
 *   data transfer commands, wait for transfer completions.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[out] context Pointer to a context object to be initialized.
 * Once initialized, this object is supplied to other gxpci
 * routines.
 * @param[in] trio_index The TRIO index of the local PCIe port,
 * Gx36 must pass 0.
 * @param[in] mac The mac number of the local PCIe port,
 * e.g. 0, 1 or 2.
 *
 * @return 0 on success, GXPCI_EINVAL if either trio_index or mac
 * is invalid.
 */
int
gxpci_init(gxio_trio_context_t *trio_context,
           gxpci_context_t *context,
           unsigned int trio_index,
           unsigned int mac);

/**
 * @brief Open a selected queue between the local PCIe port
 * and a remote PCIe port.
 *
 * @param[in] context Initialized context object.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 * @param[in] type The zero-copy queue type.
 * @param[in] rem_link_index The global PCIe link index of the target PCIe port
 *   of the chip-to-chip queue, when the queue is opened by a Gx PCIe
 *   physical function. This argument specifies the SR-IOV virtual function
 *   number if the queue is opened by a Gx PCIe VF.
 * @param[in] queue_index The index of the queue.
 * @param[in] pkt_headroom The receive packet headroom size in chip-to-chip
 *   queues. It must not be larger than the value in the recv_buf_size argument.
 *   For queue types other than GXPCI_C2C_RECV or GXPCI_NIC_H2T, it is ignored.
 * @param[in] recv_buf_size The receive buffer size in chip-to-chip queues.
 *   It must not be larger than GXPCI_C2C_MAX_RECV_BUF_SIZE. For queue types
 *   other than GXPCI_C2C_RECV, this is ignored.
 *
 * @return 0 on success, -EBUSY if the queue resource is not
 * available, GXPCI_ERESET if the queue is reset by the remote end,
 * GXPCI_EBINDCPU if process is not bound to a single cpu,
 * or an error value of type gxio_err_e.
 */
int
gxpci_open_queue(gxpci_context_t *context,
                 int asid,
                 gxpci_queue_type_t type,
                 unsigned int rem_link_index,
                 unsigned int queue_index,
                 unsigned int pkt_headroom,
                 unsigned int recv_buf_size);

/**
 * @brief Open a selected duplex queue between the local PCIe port
 * and a remote PCIe port.
 *
 * @param[in] h2t_context Initialized context object of H2T direction
 *   for queue types GXPCI_PQ_DUPLEX and GXPCI_PQ_DUPLEX_VF.
 *   For queue type GXPCI_C2C_DUPLEX, this is the context object for
 *   the receive queue.
 * @param[in] t2h_context Initialized context object of T2H direction
 *   for queue type GXPCI_PQ_DUPLEX and GXPCI_PQ_DUPLEX_VF.
 *   For queue type GXPCI_C2C_DUPLEX, this is the context object for
 *   the send queue.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 * @param[in] type The duplex queue type.
 * @param[in] rem_link_index The global PCIe link index of the target PCIe port
 *   for queue type GXPCI_C2C_DUPLEX. For queue type GXPCI_PQ_DUPLEX_VF,
 *   this is the SR-IOV virtual function instance number.
 * @param[in] queue_index The index of the duplex queue for queue types
 *   GXPCI_PQ_DUPLEX and GXPCI_PQ_DUPLEX_VF, ignored for other queue types.
 *
 * @return 0 on success, -EBUSY if the queue resource is not
 * available, GXPCI_ERESET if the queue is reset by the remote end,
 * or an error value of type gxio_err_e.
 */
int
gxpci_open_duplex_queue(gxpci_context_t *h2t_context,
                        gxpci_context_t *t2h_context,
                        int asid,
                        gxpci_queue_type_t type,
                        unsigned int rem_link_index,
                        unsigned int queue_index);

/**
 * @brief Post a command.
 *
 * @param[in] context Initialized context object.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 if the command is posted, or GXPCI_ECREDITS if this
 * queue does not have enough command credits to post the command
 * or in the case of send queues if the send completion array is
 * full, or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_post_cmd(gxpci_context_t *context, const gxpci_cmd_t* cmd);

/**
 * @brief Post a command to the tile-to-host packet queue.
 * The performance requirement for packet queues dictates that this
 * function should be accessable from the application directly.
 *
 * @param[in] context Initialized context object.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 if the command is posted, or GXPCI_ECREDITS if this
 * queue does not have enough command credits to post the command,
 * or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_pq_t2h_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd);

/**
 * @brief Post a command to the host-to-tile packet queue.
 * The performance requirement for packet queues dictates that this
 * function should be accessable from the application directly.
 *
 * @param[in] context Initialized context object.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 if the command is posted, or GXPCI_ECREDITS if this
 * queue does not have enough command credits to post the command,
 * or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_pq_h2t_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd);

/**
 * @brief Post a command to the Raw DMA transmit queue.
 *
 * @param[in] context Initialized context object.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 if the command is posted,
 * or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_raw_dma_send_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd);

/**
 * @brief Post a command to the Raw DMA receive queue.
 *
 * @param[in] context Initialized context object.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 if the command is posted,
 * or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_raw_dma_recv_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd);

/**
 * @brief Retrieve command completions.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] comps Pointer to completion packet array.
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_get_comps(gxpci_context_t *context,
                gxpci_comp_t* comps,
                int min,
                int max);

/**
 * @brief Retrieve the number of command credits, i.e. free command
 * descriptors.
 * 
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success,
 * or GXPCI_EINVAL if queue type is invalid.
 */ 
int
gxpci_get_cmd_credits(gxpci_context_t *context);

/**
 * @brief Register huge-page-size memory with the PCIe system
 *   for data transfer. This memory is used for packet buffers.
 *
 * @param[in] context Initialized context object.
 * @param[in] va Data buffer address.
 * @param[in] size Data buffer size.
 *
 * @return 0 on success, an error value returned by gxio_trio_register_page().
 */
int
gxpci_iomem_register(gxpci_context_t *context,
                     void *va,
                     size_t size);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources. These resources include both SW resources and TRIO HW
 * resources. These resources will be released implicitly when the
 * application exits without calling gxpci_destroy() or is terminated.
 *
 * @param[in] context Initialized context object.
 * @return 0 on success, non-zero on error.
 */
int
gxpci_destroy(gxpci_context_t *context);

/**
 * @brief Release the duplex communication endpoint and free the
 * allocated resources. These resources include both SW resources and
 * TRIO HW resources. These resources will be released implicitly when
 * the application exits without calling gxpci_destroy_duplex() or is
 * terminated.
 *
 * @param[in] h2t_context Initialized context object of H2T direction.
 * @param[in] t2h_context Initialized context object of T2H direction.
 * @param[in] type The zero-copy duplex queue type.
 * @return 0 on success, non-zero on error.
 */
int
gxpci_destroy_duplex(gxpci_context_t *h2t_context,
                     gxpci_context_t *t2h_context,
                     gxpci_queue_type_t type);

/**
 * Translate a gxpci error code into a string.
 *
 * @param[in] gxpci_errno An error number returned from a gxpci API call.
 * @return A string describing the requested gxpci error number.  If
 *   the error number corresponds to a gxio error, system error or is invalid,
 *   the gxio_strerror() result for the error number is returned.
 */
const char*
gxpci_strerror(int gxpci_errno);

/**
 * The following prototypes are for the new Chip-to-Chip API implementation
 *  that supports 64 C2C queues.
 */

/**
 * @brief Open a chip-to-chip sender queue.
 *
 * @param[in] context Initialized gxpci_context_t struct for the
 *   sender queue.
 * @param[in] send_queue_index The index of the sender queue.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_c2c_open_send_queue(gxpci_context_t *context,
                          unsigned int send_queue_index);

/**
 * @brief Open a chip-to-chip receiver queue.
 *
 * @param[in] context Initialized gxpci_context_t struct for the
 *   receiver queue.
 * @param[in] pkt_headroom The receive packet headroom size in a chip-to-chip
 *   receiver queue, indicating the offset from the beginning of the receive
 *   buffer where data will be written to.
 * @param[in] recv_page_size The size of the single page that is allocated
 *   by the receiver application to serve as the receive buffer pool. Usually,
 *   this is size of the huge page, i.e. 16MB.
 * @param[in] recv_page_addr The address of the single page that is allocated
 *   by the receiver application to serve as the receive buffer pool.
 * @param[in] recv_queue_index The index of the receiver queue.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_c2c_open_recv_queue(gxpci_context_t *context,
                          unsigned int pkt_headroom,
                          unsigned int recv_page_size,
                          void *recv_page_addr,
                          unsigned int recv_queue_index);

/**
 * @brief Post some commands from a command array to a chip-to-chip
 * sender queue.
 *
 * @param[in] context Initialized gxpci_context_t struct for the
 *   sender queue.
 * @param[in] cmds Pointer to a command array.
 * @param[in] cmd_count Number of commands to be posted.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_c2c_send_cmds(gxpci_context_t *context,
                    const gxpci_cmd_t* cmds,
                    uint32_t cmd_count);

/**
 * @brief Post some commands from a command array to a chip-to-chip
 * receiver queue.
 *
 * @param[in] context Initialized gxpci_context_t struct for the
 *   receiver queue.
 * @param[in] cmds Pointer to a command array.
 * @param[in] cmd_count Number of commands to be posted.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_c2c_recv_cmds(gxpci_context_t *context,
                    const gxpci_cmd_t* cmds,
                    uint32_t cmd_count);

/**
 * @brief Retrieve command completions from a chip-to-chip sender queue.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] cpls Pointer to completion packet array.
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_c2c_get_send_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max);

/**
 * @brief Retrieve command completions from a chip-to-chip receiver queue.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] cpls Pointer to completion packet array.
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_c2c_get_recv_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                         int min, int max);

/**
 * @brief Retrieve the number of command credits, i.e. new command
 * descriptors that can be posted, of a chip-to-chip sender queue..
 *
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success.
 */
uint32_t
gxpci_c2c_send_get_credits(gxpci_context_t *context);

/**
 * @brief Retrieve the number of command credits, i.e. new command
 * descriptors that can be posted, of a chip-to-chip receiver queue..
 *
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success.
 */
uint32_t
gxpci_c2c_recv_get_credits(gxpci_context_t *context);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for a chip-to-chip sender queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_c2c_send_destroy(gxpci_context_t *context);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for a chip-to-chip receiver queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_c2c_recv_destroy(gxpci_context_t *context);


/**
 * The following prototypes are for the host NIC API implementation.
 */

/**
 * @brief Open a host NIC interface.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] trio_index The TRIO index of the local PCIe port, 0 or 1.
 * @param[in] mac The mac number of the local PCIe port, 0, 1 or 2.
 * @param[in] nic_index The index of this host NIC interface.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_init(gxio_trio_context_t *trio_context, gxpci_nic_state_t *nic_state,
               unsigned int trio_index, unsigned int mac,
               int nic_index, int asid);

/**
 * @brief Initialize a host NIC T2H queue.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the T2H queue within this NIC.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_t2h_queue_init(gxio_trio_context_t *trio_context,
                         gxpci_nic_state_t *nic_state,
                         gxpci_context_t *context,
                         unsigned int queue_index);

/**
 * @brief Complete a host NIC T2H queue initialization.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 *
 * @return 0 on success.
 */
int
gxpci_nic_t2h_queue_complete_init(gxpci_context_t *context);

/**
 * @brief Check if a host NIC T2H queue initialization can be 
 *   completed. If not, this function needs to be called again.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 *
 * @return 0 on success, -EAGAIN if incompletion,
 *   GXPCI_ERESET if queue is reset.
 */
int
gxpci_nic_t2h_queue_complete_init_nb(gxpci_context_t *context);

/**
 * @brief Initialize a host NIC H2T queue.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the H2T queue within this NIC.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_h2t_queue_init(gxio_trio_context_t *trio_context,
                         gxpci_nic_state_t *nic_state,
                         gxpci_context_t *context,
                         unsigned int queue_index);

/**
 * @brief Complete a host NIC H2T queue initialization.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 *
 * @return 0 on success.
 */
int
gxpci_nic_h2t_queue_complete_init(gxpci_context_t *context);

/**
 * @brief Check if a host NIC H2T queue initialization can be 
 *   completed. If not, this function needs to be called again.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 *
 * @return 0 on success, -EAGAIN if incompletion,
 *   GXPCI_ERESET if queue is reset.
 */
int
gxpci_nic_h2t_queue_complete_init_nb(gxpci_context_t *context);

/**
 * @brief Open a host NIC interface on a Virtual Function.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] trio_index The TRIO index of the local PCIe port, 0 or 1.
 * @param[in] mac The mac number of the local PCIe port, 0, 1 or 2.
 * @param[in] vf_index The index of associated VF.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 * @param[in] backing_mem The registered buffer that is used to
 *   store the VF NIC's registers and DMA descriptors.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_init_vf(gxio_trio_context_t *trio_context,
                  gxpci_nic_state_t *nic_state, unsigned int trio_index,
                  unsigned int mac, int vf_index, int asid, void *backing_mem);

/**
 * @brief Complete a host NIC initialization on a Virtual Function.
 *
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_nic_complete_init_vf(gxpci_nic_state_t *nic_state);

/**
 * @brief Check if a host NIC initialization can be completed on a Virtual
 * Function. If not, this function needs to be called again.
 *
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success, -EAGAIN if incompletion.
 */
int
gxpci_nic_complete_init_vf_nb(gxpci_nic_state_t *nic_state);

/**
 * @brief Initialize a host NIC T2H queue on a VF.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the T2H queue within this NIC.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_t2h_queue_init_vf(gxio_trio_context_t *trio_context,
                            gxpci_nic_state_t *nic_state,
                            gxpci_context_t *context,
                            unsigned int queue_index);

/**
 * @brief Complete a host NIC T2H queue initialization on a VF.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_nic_t2h_queue_complete_init_vf(gxpci_context_t *context,
                                     gxpci_nic_state_t *nic_state);

/**
 * @brief Check if a host NIC T2H queue initialization on a VF can be 
 *   completed. If not, this function needs to be called again.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success, -EAGAIN if incompletion,
 *   GXPCI_ERESET if queue is reset.
 */
int
gxpci_nic_t2h_queue_complete_init_vf_nb(gxpci_context_t *context,
                                        gxpci_nic_state_t *nic_state);

/**
 * @brief Initialize a host NIC H2T queue on a VF.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the H2T queue within this NIC.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_nic_h2t_queue_init_vf(gxio_trio_context_t *trio_context,
                            gxpci_nic_state_t *nic_state,
                            gxpci_context_t *context,
                            unsigned int queue_index);

/**
 * @brief Complete a host NIC H2T queue initialization on a VF.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_nic_h2t_queue_complete_init_vf(gxpci_context_t *context,
                                     gxpci_nic_state_t *nic_state);

/**
 * @brief Check if a host NIC H2T queue initialization on a VF can be 
 *   completed. If not, this function needs to be called again.
 *
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success, -EAGAIN if incompletion,
 *   GXPCI_ERESET if queue is reset.
 */
int
gxpci_nic_h2t_queue_complete_init_vf_nb(gxpci_context_t *context,
                                        gxpci_nic_state_t *nic_state);

/**
 * @brief Post multiple commands to a host NIC T2H queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to an array of commands.
 * @param[in] count Number of commands to enqueue.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_nic_t2h_cmds(gxpci_context_t *context, const gxpci_cmd_t* cmd,
                   unsigned int count);

/**
 * @brief Post a single command to a host NIC T2H queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
static inline int
gxpci_nic_t2h_cmd(gxpci_context_t *context, const gxpci_cmd_t* cmd)
{
  return gxpci_nic_t2h_cmds(context, cmd, 1);
}

/**
 * @brief Post multiple commands to a host NIC H2T queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to an array of commands.
 * @param[in] count Number of commands to enqueue.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_nic_h2t_cmds(gxpci_context_t *context, const gxpci_cmd_t* cmd,
                   unsigned int count);

/**
 * @brief Post a single command to a host NIC T2H queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
static inline int
gxpci_nic_h2t_cmd(gxpci_context_t *context, const gxpci_cmd_t* cmd)
{
  return gxpci_nic_h2t_cmds(context, cmd, 1);
}

/**
 * @brief Retrieve command completions from a host NIC T2H or H2T queue.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] cpls Pointer to completion packet array.
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_nic_get_comps(gxpci_context_t *context, gxpci_nic_comp_t* cpls,
                    int min, int max);

/**
 * @brief Retrieve the number of command credits, i.e. new command
 * descriptors that can be posted, of a host NIC H2T or T2H queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success.
 */
int
gxpci_nic_get_cmd_credits(gxpci_context_t *context);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for a host NIC interface.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_nic_destroy(gxio_trio_context_t *trio_context,
                  gxpci_nic_state_t *nic_state);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for a host NIC VF interface.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] nic_state Pointer to a pre-allocated gxpci_nic_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_nic_destroy_vf(gxio_trio_context_t *trio_context,
                     gxpci_nic_state_t *nic_state);

/**
 * The following prototypes are for the host Raw DMA API implementation.
 */

/**
 * @brief Open a host Raw DMA channel containing one or more T2H/H2T queues.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] rd_state Pointer to a pre-allocated gxpci_raw_dma_state_t struct.
 * @param[in] trio_index The TRIO index of the local PCIe port, 0 or 1.
 * @param[in] mac The mac number of the local PCIe port, 0, 1 or 2.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_raw_dma_init(gxio_trio_context_t *trio_context,
                   gxpci_raw_dma_state_t *rd_state, unsigned int trio_index,
                   unsigned int mac, int asid);

/**
 * @brief Open a host Raw DMA receive (H2T) queue.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] rd_state Pointer to a pre-allocated gxpci_raw_dma_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the H2T queue within the channel.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_open_raw_dma_recv_queue(gxio_trio_context_t *trio_context,
                              gxpci_raw_dma_state_t *rd_state,
                              gxpci_context_t *context,
                              unsigned int queue_index);

/**
 * @brief Open a host Raw DMA send (T2H) queue.
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] rd_state Pointer to a pre-allocated gxpci_raw_dma_state_t struct.
 * @param[in] context Initialized gxpci_context_t struct for the queue.
 * @param[in] queue_index The index of the T2H queue within the channel.
 *
 * @return 0 on success, non-zero on error.
 */
int
gxpci_open_raw_dma_send_queue(gxio_trio_context_t *trio_context,
                              gxpci_raw_dma_state_t *rd_state,
                              gxpci_context_t *context,
                              unsigned int queue_index);

/**
 * @brief Obtain the host Raw DMA buffer size. Tile side application
 * can use this value to determine the upper bound of the target buffer.
 * This function should be called after the queue is opened.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 *
 * @return host buffer size in bytes.
 */
unsigned int
gxpci_raw_dma_get_host_buf_size(gxpci_context_t *context);

/**
 * @brief Obtain the host Raw DMA counter that is updated by the host side
 * application. Tile side application can use this value for flow-control
 * purpose. The exact FC algorithm is defined by the application.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 *
 * @return a unsigned integer value.
 */
unsigned int
gxpci_raw_dma_get_host_counter(gxpci_context_t *context);

/**
 * @brief Post a command to a Raw DMA send queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_raw_dma_send_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd);

/**
 * @brief Post a command to a Raw DMA receive queue.
 *
 * @param[in] context Initialized gxpci_context_t struct.
 * @param[in] cmd Pointer to a command.
 *
 * @return 0 on success, or GXPCI_ERESET if the queue is reset.
 */
int
gxpci_raw_dma_recv_cmd(gxpci_context_t *context, const gxpci_dma_cmd_t *cmd);

/**
 * @brief Retrieve command completions from a Raw DMA receive queue.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] cpls Pointer to completion array.
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_raw_dma_recv_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                             int min, int max);

/**
 * @brief Retrieve command completions from a Raw DMA send queue.
 *   This function will block if there are fewer than the minimal
 *   number of completions which is specified by "min". It returns
 *   immediately if min is zero even if there are no completions.
 *
 * @param[in] context Initialized context object.
 * @param[in] cpls Pointer to completion array. 
 * @param[in] min Wait for at least this many completions.
 * @param[in] max The maximum number of completions to return.
 *
 * @return The number of commands completed and copied into the comps
 * array  or GXPCI_ERESET if the queue is reset by the remote end.
 */
int
gxpci_raw_dma_send_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                             int min, int max);

/**
 * @brief Retrieve the number of command credits, i.e. new command
 * descriptors that can be posted, of a Raw DMA receive queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success.
 */
unsigned int
gxpci_raw_dma_recv_get_credits(gxpci_context_t *context);

/**
 * @brief Retrieve the number of command credits, i.e. new command
 * descriptors that can be posted, of a Raw DMA send queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return The number of command credits in command queue on success.
 */
unsigned int
gxpci_raw_dma_send_get_credits(gxpci_context_t *context);

/**
 * @brief Close a Raw DMA receive queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return 0 on success.
 */
int
gxpci_raw_dma_recv_destroy(gxpci_context_t *context);

/**
 * @brief Close a Raw DMA send queue.
 *
 * @param[in] context Initialized context object.
 *
 * @return 0 on success.
 */
int
gxpci_raw_dma_send_destroy(gxpci_context_t *context);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for a host Raw DMA channel. This function should be called
 * after each queue has returned from gxpci_raw_dma_send_destroy() or
 * gxpci_raw_dma_recv_destroy().
 *
 * @param[in] trio_context Pointer to a pre-allocated gxio_trio_context_t
 *   struct.
 * @param[in] rd_state Pointer to a pre-allocated gxpci_raw_dma_state_t struct.
 *
 * @return 0 on success.
 */
int
gxpci_raw_dma_destroy(gxio_trio_context_t *trio_context,
                      gxpci_raw_dma_state_t *rd_state);

/**
 * @brief Open a Packet Queue H2T queue between an SR-IOV Virtual Function
 * and a remote PCIe port on the host.
 *
 * @param[in] context Initialized context object.
 * @param[in] asid The pre-allocated ASID.
 * @param[in] queue_state The pre-allocated gxpci_packet_queue_state object.
 * @param[in] vf_instance The VF instance number.
 * @param[in] queue_index The index of the queue.
 * @param[in] backing_mem The registered buffer that is used to
 *   store the Pull DMA descriptor rings.
 *
 * @return 0 on success, -EBUSY if the queue resource is not
 * available, GXPCI_ERESET if the queue is reset by the remote end,
 * or an error value of type gxio_err_e.
 */
int
gxpci_open_pq_h2t_queue_vf(gxpci_context_t *context,
                           int asid,
                           struct gxpci_packet_queue_state *queue_state,
                           unsigned int vf_instance,
                           unsigned int queue_index,
                           void *backing_mem);

/**
 * @brief Open a Packet Queue T2H queue between an SR-IOV Virtual Function
 * and a remote PCIe port on the host.
 *
 * @param[in] context Initialized context object.
 * @param[in] asid The pre-allocated ASID.
 * @param[in] queue_state The pre-allocated gxpci_packet_queue_state object.
 * @param[in] vf_instance The VF instance number.
 * @param[in] queue_index The index of the queue.
 * @param[in] backing_mem The registered buffer that is used to
 *   store the Push DMA descriptor rings.
 *
 * @return 0 on success, -EBUSY if the queue resource is not
 * available, GXPCI_ERESET if the queue is reset by the remote end,
 * or an error value of type gxio_err_e.
 */
int
gxpci_open_pq_t2h_queue_vf(gxpci_context_t *context,
                           int asid,
                           struct gxpci_packet_queue_state *queue_state,
                           unsigned int vf_instance,
                           unsigned int queue_index,
                           void *backing_mem);

/**
 * @brief Open a selected duplex queue between an SR-IOV Virtual Function
 * and a remote PCIe port.
 *
 * @param[in] h2t_context Initialized context object of H2T direction
 *   for queue type GXPCI_PQ_DUPLEX_VF.
 * @param[in] t2h_context Initialized context object of T2H direction
 *   for queue type GXPCI_PQ_DUPLEX_VF.
 * @param[in] asid The ASID if pre-allocated, else GXIO_ASID_NULL.
 * @param[in] h2t_queue_state The pre-allocated gxpci_packet_queue_state object
 *   for the H2T queue.
 * @param[in] t2h_queue_state The pre-allocated gxpci_packet_queue_state object
 *   for the T2H queue.
 * @param[in] vf_instance The VF instance number.
 * @param[in] queue_index The index of the duplex queue for queue type
 *   GXPCI_PQ_DUPLEX_VF.
 * @param[in] h2t_backing_mem The registered buffer that is used to
 *   store the Pull DMA descriptor rings for the H2T queue.
 * @param[in] t2h_backing_mem The registered buffer that is used to
 *   store the Push DMA descriptor rings for the T2H queue.
 *
 * @return 0 on success, -EBUSY if the queue resource is not
 * available, GXPCI_ERESET if the queue is reset by the remote end,
 * or an error value of type gxio_err_e.
 */
int
gxpci_open_pq_duplex_queue_vf(gxpci_context_t *h2t_context,
                              gxpci_context_t *t2h_context,
                              int asid,
                              struct gxpci_packet_queue_state *h2t_queue_state,
                              struct gxpci_packet_queue_state *t2h_queue_state,
                              unsigned int vf_instance,
                              unsigned int queue_index,
                              void *h2t_backing_mem,
                              void *t2h_backing_mem);

/**
 * @brief Release the communication endpoint and free the allocated
 * resources for the Packet Queue interfaces.
 *
 * @param[in] context Initialized context object.
 * @return 0 on success, non-zero on error.
 */
int
gxpci_pq_destroy(gxpci_context_t *context);

/** @} */

__END_DECLS

#endif /* __GXPCI_H__ */
