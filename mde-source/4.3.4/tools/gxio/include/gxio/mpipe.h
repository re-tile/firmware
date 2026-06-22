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

#ifndef _GXIO_MPIPE_H_
#define _GXIO_MPIPE_H_

/**
 * @file
 *
 * An API for allocating, configuring, and manipulating mPIPE hardware
 * resources.
 */

#include <gxio/common.h>
#include <gxio/dma_queue.h>

#include <errno.h>
#include <sys/time.h>

#include <arch/mpipe.h>
#include <arch/mpipe_def.h>
#include <arch/mpipe_shm.h>

#include <hv/drv_mpipe_intf.h>
#include <hv/iorpc.h>

__BEGIN_DECLS

/**
 * @addtogroup gxio_mpipe
 * @{
 *
 * The TILE-Gx mPIPE&tm; shim provides Ethernet connectivity, packet
 * classification, and packet load balancing services.  The
 * gxio_mpipe_ API, declared in <gxio/mpipe.h>, allows applications to
 * allocate mPIPE IO channels, configure packet distribution
 * parameters, and send and receive Ethernet packets.  The API is
 * designed to be a minimal wrapper around the mPIPE hardware, making
 * system calls only where necessary to preserve inter-process
 * protection guarantees.
 *
 * The APIs described below allow the programmer to allocate and
 * configure mPIPE resources.  As described below, the mPIPE is a
 * single shared hardware device that provides partitionable resources
 * that are shared between all applications in the system.  The
 * gxio_mpipe_ API allows userspace code to make resource request
 * calls to the hypervisor, which in turns keeps track of the
 * resources in use by all applications, maintains protection
 * guarantees, and resets resources upon application shutdown.
 *
 * We strongly recommend reading the mPIPE section of the IO Device
 * Guide (UG404) before working with this API.  Most functions in the
 * gxio_mpipe_ API are directly analogous to hardware interfaces and
 * the documentation assumes that the reader understands those
 * hardware interfaces.
 *
 * @section mpipe__ingress mPIPE Ingress Hardware Resources
 *
 * The mPIPE ingress hardware provides extensive hardware offload for
 * tasks like packet header parsing, load balancing, and memory
 * management.  This section provides a brief introduction to the
 * hardware components and the gxio_mpipe_ calls used to manage them;
 * see the IO Device Guide for a much more detailed description of the
 * mPIPE's capabilities.
 *
 * When a packet arrives at one of the mPIPE's Ethernet MACs, it is
 * assigned a channel number indicating which MAC received it.  It
 * then proceeds through the following hardware pipeline:
 *
 * @subsection mpipe__classification Classification
 *
 * A set of classification processors run header parsing code on each
 * incoming packet, extracting information including the destination
 * MAC address, VLAN, Ethernet type, and five-tuple hash.  Some of
 * this information is then used to choose which buffer stack will be
 * used to hold the packet, and which bucket will be used by the load
 * balancer to determine which application will receive the packet.
 *
 * The rules by which the buffer stack and bucket are chosen can be
 * configured via the @ref gxio_mpipe_classifier API.  A given app can
 * specify multiple rules, each one specifying a bucket range, and a
 * set of buffer stacks, to be used for packets matching the rule.
 * Each rule can optionally specify a restricted set of channels,
 * VLANs, and/or dMACs, in which it is interested.  By default, a
 * given rule starts out matching all channels associated with the
 * mPIPE context's set of open links; all VLANs; and all dMACs.
 * Subsequent restrictions can then be added.
 *
 * @subsection mpipe__load_balancing Load Balancing
 *
 * The mPIPE load balancer is responsible for choosing the NotifRing
 * to which the packet will be delivered.  This decision is based on
 * the bucket number indicated by the classification program.  In
 * general, the bucket number is based on some number of low bits of
 * the packet's flow hash (applications that aren't interested in flow
 * hashing use a single bucket).  Each load balancer bucket keeps a
 * record of the NotifRing to which packets directed to that bucket
 * are currently being delivered.  Based on the bucket's load
 * balancing mode (@ref gxio_mpipe_bucket_mode_t), the load balancer
 * either forwards the packet to the previously assigned NotifRing or
 * decides to choose a new NotifRing.  If a new NotifRing is required,
 * the load balancer chooses the least loaded ring in the NotifGroup
 * associated with the bucket.
 *
 * The load balancer is a shared resource.  Each application needs to
 * explicitly allocate NotifRings, NotifGroups, and buckets, using
 * gxio_mpipe_alloc_notif_rings(), gxio_mpipe_alloc_notif_groups(),
 * and gxio_mpipe_alloc_buckets().  Then the application needs to
 * configure them using gxio_mpipe_init_notif_ring() and
 * gxio_mpipe_init_notif_group_and_buckets().
 *
 * @subsection mpipe__buffers Buffer Selection and Packet Delivery
 *
 * Once the load balancer has chosen the destination NotifRing, the
 * mPIPE DMA engine pops at least one buffer off of the 'buffer stack'
 * chosen by the classification program and DMAs the packet data into
 * that buffer.  Each buffer stack provides a hardware-accelerated
 * stack of data buffers with the same size.  If the packet data is
 * larger than the buffers provided by the chosen buffer stack, the
 * mPIPE hardware pops off multiple buffers and chains the packet data
 * through a multi-buffer linked list.  Once the packet data is
 * delivered to the buffer(s), the mPIPE hardware writes the
 * ::gxio_mpipe_idesc_t metadata object (calculated by the classifier)
 * into the NotifRing and increments the number of packets delivered
 * to that ring.
 *
 * Applications can push buffers onto a buffer stack by calling
 * gxio_mpipe_push_buffer() or by egressing a packet with the
 * ::gxio_mpipe_edesc_t::hwb bit set, indicating that the egressed
 * buffers should be returned to the stack.
 *
 *
 * Applications can allocate and initialize buffer stacks with the
 * gxio_mpipe_alloc_buffer_stacks() and gxio_mpipe_init_buffer_stack()
 * APIs.
 *
 * The application must also register the memory pages that will hold
 * packets.  This requires calling gxio_mpipe_register_page() for each
 * memory page that will hold packets allocated by the application for
 * a given buffer stack.  Since each buffer stack is limited to 16
 * registered pages, it may be necessary to use huge pages, or even
 * extremely huge pages, to hold all the buffers.
 *
 * @subsection mpipe__iqueue NotifRings
 *
 * Each NotifRing is a region of shared memory, allocated by the
 * application, to which the mPIPE delivers packet descriptors
 * (::gxio_mpipe_idesc_t).  The application can allocate them via
 * gxio_mpipe_alloc_notif_rings().  The application can then either
 * explicitly initialize them with gxio_mpipe_init_notif_ring() and
 * then read from them manually, or can make use of the convenience
 * wrappers provided by @ref gxio_mpipe_wrappers.
 *
 * @section mpipe__egress mPIPE Egress Hardware
 *
 * Applications use eDMA rings to queue packets for egress.  The
 * application can allocate them via gxio_mpipe_alloc_edma_rings().
 * The application can then either explicitly initialize them with
 * gxio_mpipe_init_edma_ring() and then write to them manually, or
 * can make use of the convenience wrappers provided by
 * @ref gxio_mpipe_wrappers.
 *
 * @section mpipe__consideration Multiple mPIPE instances
 *
 * On the silicon of some members of Gx, there are two independent
 * mPIPE instances. Applications may need interface to both mPIPE
 * instances. i.e. egress packet and return buffer to other mPIPE
 * instance or allocate buffer stacks, register memory on both mPIPE
 * instances etc. gxio provides compilation flag: GXIO_MULTI_MPIPES
 * for this purpose. To enable this operation mode (Multi-mPIPE mode),
 * build App with flag: GXIO_MULTI_MPIPES defined.
 * - @ref mpipe/forward/app_gx8072.c : Forwarding packets using
 * multiple mPIPE instances.
 *
 * For each mPIPE instance, the egress packet descriptor's HW buffer
 * return bit 'hwb' only works within its own instance. The GXIO
 * library provides a software function to return buffer to remote
 * mPIPE instance. Application sets the egress packet descriptor's
 * 'hwb' bit to return the buffer after egressing regardless which
 * mPIPE instance the buffer belonging to. The overhead is about 10
 * cycles for each remote buffer return. Under Multi-mPIPE mode,
 * App can disable the automatic remote buffer return by defining
 * compilation flag GXIO_RHWB_NONE.
 *
 * Note: In order to support the Multi-mPIPE mode, the default
 * classifier was modified to encode the mPIPE instance ID in the
 * ingress packet descriptor. The classifier reserved the upper
 * bit(bit 7) of the Byte 7 (CTR0 is defined in its lower 5 bits)
 * in the 64 byte packet descriptor as one bit instance ID. All
 * packet descriptors generated by mPIPE instance 0 have the
 * instance ID set to 0 while the packet descriptors came from
 * mPIPE instance 1 have instance ID as 1. If App had defined the
 * same bit (bit 7 of Byte 7) in the packet descriptor for other
 * usage, gxio library will not work properly under Multi-mPIPE
 * mode.
 *
 * @section gxio__shortcomings Plans for Future API Revisions
 *
 * The API defined here is only an initial version of the mPIPE API.
 * Future plans include:
 *
 * - Higher level wrapper functions to provide common initialization
 * patterns.  This should help users start writing mPIPE programs
 * without having to learn the details of the hardware.
 *
 * - Support for reset and deallocation of resources, including
 * cleanup upon application shutdown.
 *
 * - Support for calling these APIs in the BME.
 *
 * - Support for IO interrupts.
 *
 * - Clearer definitions of thread safety guarantees.
 *
 * @section gxio__mpipe_examples Examples
 *
 * See the following mPIPE example programs for more information about
 * allocating mPIPE resources and using them in real applications:
 *
 * - @ref mpipe/ingress/app.c : Receiving packets.
 *
 * - @ref mpipe/forward/app.c : Forwarding packets.
 *
 * Note that there are several more examples.
 */


/** Flags that can be passed to resource allocation functions. */
enum gxio_mpipe_alloc_flags_e {
  /** Require an allocation to start at a specified resource index. */
  GXIO_MPIPE_ALLOC_FIXED = HV_MPIPE_ALLOC_FIXED,
};

/** Flags that can be passed to memory registration functions. */
enum gxio_mpipe_mem_flags_e {
  /** Do not fill L3 when writing, and invalidate lines upon egress. */
  GXIO_MPIPE_MEM_FLAG_NT_HINT = IORPC_MEM_BUFFER_FLAG_NT_HINT,

  /** L3 cache fills should only populate IO cache ways. */
  GXIO_MPIPE_MEM_FLAG_IO_PIN = IORPC_MEM_BUFFER_FLAG_IO_PIN,
};

/** Flags of mpipe mode. */
enum gxio_mpipe_mode_flags_e {
  /** Do remote buffer return if set. */
  GXIO_MPIPE_RHWB          = 0x01,
  /** Support multiple mPIPE instances if set. */
  GXIO_MPIPE_MULTI_MPIPES  = 0x02,
};

/** An ingress packet descriptor.  When a packet arrives, the mPIPE
 * hardware generates this structure and writes it into a NotifRing.
 */
typedef MPIPE_PDESC_t gxio_mpipe_idesc_t;


/** An egress command descriptor.  Applications write this structure
 * into eDMA rings and the hardware performs the indicated operation
 * (normally involving egressing some bytes).  Note that egressing a
 * single packet may involve multiple egress command descriptors.
 */
typedef MPIPE_EDMA_DESC_t gxio_mpipe_edesc_t;

/** A mPIPE buffer descriptor. Applications could use this one word
 *  descriptor to track a mpipe buffer.
 *  Note: Its 'inst' field is used by SW only.
 */
typedef MPIPE_BSM_REGION_VAL_t gxio_mpipe_bdesc_t;

/** Check if a buffer descriptor is valid.
 */
#define GXIO_MPIPE_BDESC_IS_VALID(_bdesc)                       \
  ((_bdesc.c != MPIPE_EDMA_DESC_WORD1__C_VAL_INVALID) &&        \
   (_bdesc.c !=  MPIPE_EDMA_DESC_WORD1__C_VAL_NOT_RDY))

#ifndef __DOXYGEN__
/*
 * Following 4 weak declared variables: mod_verify_xxx_0/1 are used to
 * verify whether the user used consistent compilation flags:
 * GXIO_MULTI_MPIPES and GXIO_RHWB_NONE and generate error if any
 * inconsistance was detected.
 */

#if !defined(__GXIO_LIBRARY__)
extern char mode_verify_dual_0      __attribute__ ((weak));
extern char mode_verify_dual_1      __attribute__ ((weak));
extern char mode_verify_rhwb_none_0 __attribute__ ((weak));
extern char mode_verify_rhwb_none_1 __attribute__ ((weak));

#if defined(GXIO_MULTI_MPIPES)
/*
 * Implement mode_verify_dual_1.
 */
char mode_verify_dual_1;

#else
/*
 * Implement mode_verify_dual_0.
 */
char mode_verify_dual_0;

#endif /* defined(GXIO_MULTI_MPIPES) */

#if defined(GXIO_RHWB_NONE)
/*
 * Implement mode_verify_rhwb_none_1.
 */
char mode_verify_rhwb_none_1;

#else
/*
 * Implement mode_verify_rhwb_none_0.
 */
char mode_verify_rhwb_none_0;

#endif /*defined(GXIO_RHWB_NONE)*/
#endif /* !defined(__GXIO_LIBRARY__) */

/*
 * Max # of mpipe instances. 2 currently.
 */
#define GXIO_MPIPE_INSTANCE_MAX  HV_MPIPE_INSTANCE_MAX


#define EDESC_W1_INST_OFFSET MPIPE_EDMA_DESC_WORD1__INST_SHIFT
#define EDESC_W1_INST_MASK   (0x1ULL << EDESC_W1_INST_OFFSET)

/*
 * Remote buffer return is done RHWB_BATCH descriptors each time.
 * The starting descriptor has to be modulo of RHWB_BATCH.
 */
#define RHWB_BATCH 32
#define EDESC_W1_STACK_IDX_OFFSET MPIPE_EDMA_DESC_WORD1__STACK_IDX_SHIFT

/*
 * Define a SW remote buffer return bit(RHWB) in the egress descriptor.
 * When user post a descriptor, in the gxio_mpipe_equeue_put_at_aux(.)
 * gxio set or clear this RHWB bit if a remote mPIPE buffer return is
 * required for this descriptor. This bit will be double checked after
 * its completion to perform mmio write buffer return if it was set.
 */

#define EDESC_W1_RHWB_OFFSET                    (57)
#define EDESC_W1_RHWB_MASK   (0x1ULL << EDESC_W1_RHWB_OFFSET)

/*
 * Define a SW instance bit in the ingress packet descriptor locates
 * at bit 63 of its word 0 i.e. upper bit of ctr0. This bit was set
 * by classifier according to the instance variable HV reprogrammed
 * after loading classifier's immage.
 */

#define IDESC_W0_INST_OFFSET                    (63)
#define IDESC_W0_INST_MASK   (0x1ULL << IDESC_W0_INST_OFFSET)
#endif /*__DOXYGEN__*/

/** Get the "va" field from an "idesc".
 *
 * This is the address at which the ingress hardware copied the first
 * byte of the packet.
 *
 * If the classifier detected a custom header, then this will point to
 * the custom header, and gxio_mpipe_idesc_get_l2_start() will point
 * to the actual L2 header.
 *
 * Note that this value may be misleading if "idesc->be" is set.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned char*
gxio_mpipe_idesc_get_va(gxio_mpipe_idesc_t* idesc)
{
  return (unsigned char*)(long)idesc->va;
}


/** Get the "xfer_size" from an "idesc".
 *
 * This is the actual number of packet bytes transferred into memory
 * by the hardware.
 *
 * Note that this value may be misleading if "idesc->be" is set.
 *
 * @param idesc An ingress packet descriptor.
 *
 * ISSUE: Is this the best name for this?
 * FIXME: Add more docs about chaining, clipping, etc.
 */
static __USUALLY_INLINE unsigned int
gxio_mpipe_idesc_get_xfer_size(gxio_mpipe_idesc_t* idesc)
{
  return idesc->l2_size;
}



/** Get the "flow_hash" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * The default classifier uses a five-tuple hash (src/dest IP addr,
 * src/dest port, protocol) for IPv4/IPv6 TCP/UDP packets, a two-tuple
 * hash (src/dest IP addr) for other IPv4/IPv6 packets, and otherwise
 * a two-tuple hash (dest/src mac addr).
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint32_t
gxio_mpipe_idesc_get_flow_hash(gxio_mpipe_idesc_t* idesc)
{
  return idesc->custom0;
}


/** Get the "vlan" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * The default classifier uses a vlan of 0xFFFF unless it processed
 * a known vlan-encapsulating ethertype, in which case this will be
 * one of the encapsulated vlans.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint16_t
gxio_mpipe_idesc_get_vlan(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 0) & 0xFFFF;
}


/** Get the "ethertype" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * This is normally the final ethertype detected by the classifier.
 *
 * The default classifier only understands a few basic ethertypes.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint16_t
gxio_mpipe_idesc_get_ethertype(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 16) & 0xFFFF;
}


/** Get the "l2_offset" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * This is the number of bytes between the "va" and the L2 header.
 *
 * The L2 header consists of a destination mac address, a source mac
 * address, and an initial ethertype.  Various initial ethertypes
 * allow encoding extra information in the L2 header, often including
 * a vlan, and/or a new ethertype.
 *
 * Note that the "l2_offset" will be non-zero if (and only if) the
 * classifier processed a custom header for the packet.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint8_t
gxio_mpipe_idesc_get_l2_offset(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 32) & 0xFF;
}

/** Get the "l2_start" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_va() plus
 * gxio_mpipe_idesc_get_l2_offset().
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned char*
gxio_mpipe_idesc_get_l2_start(gxio_mpipe_idesc_t* idesc)
{
  unsigned char* va = gxio_mpipe_idesc_get_va(idesc);
  return va + gxio_mpipe_idesc_get_l2_offset(idesc);
}

/** Get the "l2_length" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_xfer_size() minus
 * gxio_mpipe_idesc_get_l2_offset().
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned int
gxio_mpipe_idesc_get_l2_length(gxio_mpipe_idesc_t* idesc)
{
  unsigned int xfer_size = idesc->l2_size;
  return xfer_size - gxio_mpipe_idesc_get_l2_offset(idesc);
}



/** Get the "l3_offset" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * This is the number of bytes between the "va" and the L3 header,
 * or, if the classifier was unable to identify the L3 header, then
 * the first byte after the last known ethertype.
 *
 * The default classifier knows that ethertype 0x0800 indicates an
 * IPv4 L3 header and ethertype 0x0866 indicates an IPv6 L3 header.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint8_t
gxio_mpipe_idesc_get_l3_offset(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 40) & 0xFF;
}

/** Get the "l3_start" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_va() plus
 * gxio_mpipe_idesc_get_l3_offset().
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned char*
gxio_mpipe_idesc_get_l3_start(gxio_mpipe_idesc_t* idesc)
{
  unsigned char* va = gxio_mpipe_idesc_get_va(idesc);
  return va + gxio_mpipe_idesc_get_l3_offset(idesc);
}

/** Get the "l3_length" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_xfer_size() minus
 * gxio_mpipe_idesc_get_l3_offset().
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned int
gxio_mpipe_idesc_get_l3_length(gxio_mpipe_idesc_t* idesc)
{
  unsigned int xfer_size = idesc->l2_size;
  return xfer_size - gxio_mpipe_idesc_get_l3_offset(idesc);
}


/** Get the "l4_offset" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * This is the number of bytes between the "va" and the first byte of
 * the L4 header, or, if the classifier was unable to properly parse
 * the L3 header, then zero.
 *
 * The default classifier sets "l4_offset" (only) for IPv4/IPv6 packets.
 *
 * For IPv4/IPv6 TCP packets, the "l4_start" points at the TCP header.
 *
 * For IPv4/IPv6 UDP packets, the "l4_start" points at the UDP header.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint8_t
gxio_mpipe_idesc_get_l4_offset(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 48) & 0xFF;
}

/** Get the "l4_start" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_va() plus
 * gxio_mpipe_idesc_get_l4_offset().
 *
 * Note that if "l4_offset" is zero, then this function is pointless.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned char*
gxio_mpipe_idesc_get_l4_start(gxio_mpipe_idesc_t* idesc)
{
  unsigned char* va = gxio_mpipe_idesc_get_va(idesc);
  return va + gxio_mpipe_idesc_get_l4_offset(idesc);
}

/** Get the "l4_length" from an "idesc".
 *
 * This is simply gxio_mpipe_idesc_get_xfer_size() minus
 * gxio_mpipe_idesc_get_l4_offset().
 *
 * Note that if "l4_offset" is zero, then this function is pointless.
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE unsigned int
gxio_mpipe_idesc_get_l4_length(gxio_mpipe_idesc_t* idesc)
{
  unsigned int xfer_size = idesc->l2_size;
  return xfer_size - gxio_mpipe_idesc_get_l4_offset(idesc);
}


/** Get the "status" from an "idesc".
 *
 * Extremely customized classifiers might not support this function.
 *
 * The "status" is a set of bit flags.
 *
 * Bit 0x80 indicates that the packet is "bad".
 *
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE uint8_t
gxio_mpipe_idesc_get_status(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->custom1 >> 56) & 0xFF;
}



/** Determine if a packet descriptor has an "error" flag.
 *
 * This function checks for four "error" flags, including "idesc->me"
 * (mac error), "idesc->tr" (truncation), "idesc->ce" (crc error), and
 * "idesc->be" (buffer error).
 *
 * Note that the default classifier will drop "me"/"tr"/"ce" packets,
 * except for jumbo cut-through packets, and "be" packets will not be
 * possible if sufficient buffers are available.
 *
 * See also gxio_mpipe_idesc_is_bad().
 *
 * @param idesc An ingress packet descriptor.
 * @return 1 if packet has an "error", or 0 otherwise.
 */
static __USUALLY_INLINE int
gxio_mpipe_idesc_has_error(gxio_mpipe_idesc_t* idesc)
{
  return (idesc->me || idesc->tr || idesc->ce || idesc->be);
}


/** Determine if a packet descriptor is "bad".
 *
 * This function checks for three "bad" conditions.
 *
 * (1) The packet has an "error" flag, as evidenced by
 * "gxio_mpipe_idesc_has_error(idesc)".
 *
 * (2) The packet has a bad IPv4 header checksum, as evidenced by
 * "(gxio_mpipe_idesc_get_status(idesc) & 0x80) != 0".
 *
 * (3) The packet has a bad TCP/UDP checksum, as evidenced by
 * "idesc->cs && idesc->csum_seed_val != 0xFFFF".
 *
 * See also gxio_mpipe_iqueue_drop_if_bad().
 *
 * @param idesc An ingress packet descriptor.
 * @return 1 if packet is "bad", or 0 otherwise.
 */
static __USUALLY_INLINE int
gxio_mpipe_idesc_is_bad(gxio_mpipe_idesc_t* idesc)
{
  return (gxio_mpipe_idesc_has_error(idesc) ||
          (gxio_mpipe_idesc_get_status(idesc) & 0x80) != 0 ||
          (idesc->cs && (idesc->csum_seed_val != 0xFFFF)));
}


/** Initialize an "edesc" from some fields in an "idesc".
 *
 * Specifically, this function zeros out the edesc, and then sets
 * "edesc->ns" to "gxio_mpipe_idesc_has_error(idesc)", "edesc->bound"
 * to "1", and "edesc->xfer_size" to "idesc->l2_size", and then copies
 * the "va", "stack_idx", "inst", "hwb", "size", and "c" fields from
 * "idesc" to "edesc", in a reasonably efficient manner.
 *
 * @param edesc An egress packet descriptor.
 * @param idesc An ingress packet descriptor.
 */
static __USUALLY_INLINE void
gxio_mpipe_edesc_copy_idesc(gxio_mpipe_edesc_t* edesc,
                            gxio_mpipe_idesc_t* idesc)
{
  edesc->words[0] = 0;
  edesc->words[1] = idesc->words[7];
  edesc->ns = gxio_mpipe_idesc_has_error(idesc);
  edesc->bound = 1;
  edesc->xfer_size = idesc->l2_size;
#ifdef GXIO_MULTI_MPIPES
  edesc->inst = (idesc->words[0] & IDESC_W0_INST_MASK) ? 1 : 0;
#endif
}

/** Extract buffer descriptor from some fields in an "idesc".
 * @param idesc An ingress packet descriptor.
 */

static __USUALLY_INLINE  gxio_mpipe_bdesc_t
gxio_mpipe_idesc_to_bdesc(gxio_mpipe_idesc_t* idesc)
{
  gxio_mpipe_bdesc_t bdesc;

  bdesc.inst = ((idesc->words[0] & IDESC_W0_INST_MASK) ? 1 : 0);

  return bdesc;
}

/** Set buffer info. of an egress descriptor based on a buffer descriptor.
 * @param edesc A egress packet descriptor.
 * @param bdesc A buffer descriptor.
 */

static __USUALLY_INLINE void
gxio_mpipe_edesc_set_bdesc(gxio_mpipe_edesc_t* edesc, gxio_mpipe_bdesc_t bdesc)
{
  edesc->words[1] = bdesc.word;
}


/** Set the "va" field in an "edesc".
 *
 * @param edesc An egress packet descriptor.
 * @param va The address of some packet data.
 */
static __USUALLY_INLINE void
gxio_mpipe_edesc_set_va(gxio_mpipe_edesc_t* edesc, void* va)
{
  edesc->va = (unsigned long)va;
}




/** A context object used to manage mPIPE hardware resources. */
typedef struct {

  /** File descriptor for calling up to Linux (and thus the HV). */
  int fd;

  /** Corresponding mpipe instance #. */
  int instance;

  /** The VA at which configuration registers are mapped. */
  char* mmio_cfg_base;

  /** The VA at which IDMA, EDMA, and buffer manager are mapped. */
  char* mmio_fast_base;

  /** The "initialized" buffer stacks. */
  gxio_mpipe_rules_stacks_t __stacks;

} gxio_mpipe_context_t;


#ifndef __DOXYGEN__
/* This is only used internally, but it's most easily made visible here. */
typedef gxio_mpipe_context_t gxio_mpipe_info_context_t;

extern int
__gxio_mpipe_init(gxio_mpipe_context_t* context, unsigned int mpipe_instance,
                  unsigned int mode_flags);
#endif

/** Initialize an mPIPE context.
 *
 * This function allocates an mPIPE "service domain" and maps the MMIO
 * registers into the caller's VA space.
 *
 * To use gxio defined mPIPE contexts: GXIO_MPIPE_CONTEXT(mpipe_instance),
 * App has to call this function with the argument context = NULL.
 *
 * If GXIO_MULTI_MPIPES is defined, the argument context must be NULL.
 *
 * @param context Context object to be initialized.
 * @param mpipe_instance Instance number of mPIPE shim to be controlled via
 *  context.
 */

static __USUALLY_INLINE int
gxio_mpipe_init(gxio_mpipe_context_t* context, unsigned int mpipe_instance)
{
  int retval;
  unsigned int mode_flags = 0;
#if !defined(__GXIO_LIBRARY__)
  /* To verify consistent #define of GXIO_MULTI_MPIPES and GXIO_RHWB_NONE
   * across different object files. For example, if a C file was compiled with
   * -DGXIO_MULTI_MPIPES and another C file was compiled without it and both
   * were linked into a executable. At run time, this sanity check will
   * fail the gxio_mpipe_init(.) function call if both mode_verify_dual_0 and
   * 1 have its own definition or both mode_rhwb_none_0 and 1 have its own
   * definition.
   */
  if ((&mode_verify_dual_0 && &mode_verify_dual_1)  ||
      (&mode_verify_rhwb_none_0 && &mode_verify_rhwb_none_1))
  {
    errno = ELIBEXEC;
    return -ELIBEXEC;
  }
#endif /* !defined(__KERNEL__) && !defined(__GXIO_LIBRARY__) */

#ifdef  GXIO_MULTI_MPIPES
  mode_flags |= GXIO_MPIPE_MULTI_MPIPES;
#ifndef GXIO_RHWB_NONE
  mode_flags |= GXIO_MPIPE_RHWB;
#endif
#endif

#ifdef GXIO_MULTI_MPIPES

  /* GXIO_MULTI_MPIPES mode requires context = NULL */
  if (context)
    return -EINVAL;

  retval = __gxio_mpipe_init(NULL, mpipe_instance, mode_flags);
  if (retval == 0)
  {
    retval = __gxio_mpipe_init(NULL, mpipe_instance ? 0 : 1, 0);
    if (retval == -ENODEV)
    {
      retval = 0;
    }
  }
#else
  retval = __gxio_mpipe_init(context, mpipe_instance, mode_flags);
#endif
  return retval;
}


#ifndef __DOXYGEN__

extern gxio_mpipe_context_t* gxio_mpipe_context[GXIO_MPIPE_INSTANCE_MAX];

#endif  /* !__DOXYGEN__ */

/** gxio mPIPE contexts.
 *
 * User can use GXIO_MPIPE_CONTEXT(mpipe_instance) as the mPIPE
 * contexts after calling API:
 *      gxio_mpipe_init(gxio_mpipe_context_t* context,
 *                      unsigned int mpipe_instance);
 * where argument context must be NULL.
 *
 * App has to use GXIO_MPIPE_CONTEXT(n), where n is 0 or 1, as its mPIPE
 * context if it is in MULTI_MPIPES mode i.e. GXIO_MULTI_MPIPES is defined.
 * Under this mode, GXIO_MPIPE_CONTEXT(n) = NULL indicates that mPIPE
 * instance n does not exist.
 */

#define GXIO_MPIPE_CONTEXT(_n) gxio_mpipe_context[_n]


/** Destroy an mPIPE context.
 *
 * This function frees the mPIPE "service domain" and unmaps the MMIO
 * registers from the caller's VA space.
 *
 * If a user process exits without calling this routine, the kernel
 * will destroy the mPIPE context as part of process teardown.
 *
 * @param context Context object to be destroyed.
 */
extern int
gxio_mpipe_destroy(gxio_mpipe_context_t* context);



/******************************************************************
 *                         Buffer Stacks                          *
 ******************************************************************/


/** Allocate a set of buffer stacks.
 *
 * The return value is NOT interesting if count is zero.
 *
 * If GXIO_MULTI_MPIPES is defined, this function will allocate buffer stacks
 * from all mPIPE instances.
 *
 * @param context An initialized mPIPE context.
 * @param count Number of stacks required.
 * @param first Index of first stack if ::GXIO_MPIPE_ALLOC_FIXED flag is set,
 *   otherwise ignored.
 * @param flags Flag bits from ::gxio_mpipe_alloc_flags_e.
 * @return Index of first allocated buffer stack, or
 * ::GXIO_MPIPE_ERR_NO_BUFFER_STACK if allocation failed.
 */
extern int
gxio_mpipe_alloc_buffer_stacks(gxio_mpipe_context_t* context,
                               unsigned int count, unsigned int first,
                               unsigned int flags);


/** Enum codes for buffer sizes supported by mPIPE. */
typedef enum {
  /** 128 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_128 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_128,
  /** 256 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_256 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_256,
  /** 512 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_512 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_512,
  /** 1024 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_1024 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_1024,
  /** 1664 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_1664 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_1664,
  /** 4096 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_4096 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_4096,
  /** 10368 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_10368 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_10368,
  /** 16384 byte packet data buffer. */
  GXIO_MPIPE_BUFFER_SIZE_16384 = MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_16384
} gxio_mpipe_buffer_size_enum_t;


/** Convert a buffer size in bytes into a buffer size enum. */
extern gxio_mpipe_buffer_size_enum_t
gxio_mpipe_buffer_size_to_buffer_size_enum(size_t size);

/** Convert a buffer size enum into a buffer size in bytes. */
extern size_t
gxio_mpipe_buffer_size_enum_to_buffer_size(gxio_mpipe_buffer_size_enum_t
                                           buffer_size_enum);


/** Calculate the number of bytes required to store a given number of
 * buffers in the memory registered with a buffer stack via
 * gxio_mpipe_init_buffer_stack().
 */
extern size_t
gxio_mpipe_calc_buffer_stack_bytes(unsigned long buffers);


/** Initialize a buffer stack.  This function binds a region of memory
 * to be used by the hardware for storing buffer addresses pushed via
 * gxio_mpipe_push_buffer() or as the result of sending a buffer out
 * the egress with the 'push to stack when done' bit set.  Once this
 * function returns, the memory region's contents may be arbitrarily
 * modified by the hardware at any time and software should not access
 * the memory region again.
 * 
 * Buffer stack memory must be physically contiguous and aligned
 * to a 64KB boundary. (Note that this is separate from the actual
 * buffers stored in the stack, which have their own alignment
 * requirements. See the description of
 * gxio_mpipe_push_buffer() for more information.)
 *
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 * @param buffer_size_enum The size of each buffer in the buffer stack,
 * as an enum.
 * @param mem The address of the buffer stack. This memory must
 * be physically contiguous and aligned to a 64KB boundary.
 * The lower 16 bits of the address are ignored.
 * @param mem_size The size of the buffer stack, in bytes.
 * @param mem_flags ::gxio_mpipe_mem_flags_e memory flags.
 * @return Zero on success, ::GXIO_MPIPE_ERR_INVAL_BUFFER_SIZE if
 * buffer_size_enum is invalid, ::GXIO_MPIPE_ERR_BAD_BUFFER_STACK if
 * stack has not been allocated.
 */
extern int
gxio_mpipe_init_buffer_stack(gxio_mpipe_context_t* context,
                             unsigned int stack,
                             gxio_mpipe_buffer_size_enum_t buffer_size_enum,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags);


/** Register a page in the context's IOTLB.
 *
 * All packet buffers must reside in memory registered via this
 * function.
 *
 * Other memory structures, such as NotifRings, eDMA rings, and buffer
 * stack memory, may also reside in memory registered via this
 * function, but this is not necessary.
 *
 * If GXIO_MULTI_MPIPES is defined, this function will register the page
 * to the buffer stacks from all mPIPE instances.
 *
 * Note that the underlying driver internally marks the registered 
 * page shared, as if it is allocated with mmap(2) flag MAP_SHARED,
 * which implies that the memory is shared between the allocating
 * process and any child processes created after the allocation.
 *
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 * @param page Starting VA of a contiguous memory page.
 * @param page_size Size of the page in bytes.
 * @param page_flags ::gxio_mpipe_mem_flags_e memory flags.
 * @return Zero on success, EINVAL if page does not map a contiguous
 *   page, ::GXIO_ERR_IOTLB_ENTRY if no more IOTLB entries are
 *   available.
 */
extern int
gxio_mpipe_register_page(gxio_mpipe_context_t* context,
                         unsigned int stack,
                         void* page, size_t page_size,
                         unsigned int page_flags);

/** Push a buffer using buffer descriptor.
 *
 * The given buffer descriptor has to be a valid descriptor and its
 * 'inst' bit set to the corresponding mPIPE instance.
 *
 * @param context An initialized mPIPE context. If context is null,
 * use GXIO_MPIPE_CONTEXT.
 * @param bdesc A buffer descriptor.
 */
static __USUALLY_INLINE void
gxio_mpipe_push_buffer_bdesc(gxio_mpipe_context_t* context,
                             gxio_mpipe_bdesc_t bdesc)
{
  MPIPE_BSM_REGION_ADDR_t offset = {{ 0 }};

  /* Use gxio's default mpipe context if the context is NULL. */
  if (!context)
    context = GXIO_MPIPE_CONTEXT(bdesc.inst);

  /*
   * The mmio_fast_base region starts at the IDMA region, so subtract
   * off that initial offset.
   */
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_BSM - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  offset.stack = bdesc.stack_idx;

  __gxio_mmio_write(context->mmio_fast_base + offset.word, bdesc.word);
}


/** Pop a buffer off of a previously initialized buffer stack
 *  and return the buffer descriptor.
 *
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 * @return The buffer descriptor.
 */

static __USUALLY_INLINE gxio_mpipe_bdesc_t
gxio_mpipe_pop_buffer_bdesc(gxio_mpipe_context_t* context, unsigned int stack)
{
  MPIPE_BSM_REGION_ADDR_t offset = {{ 0 }};
  /*
   * The mmio_fast_base region starts at the IDMA region, so subtract
   * off that initial offset.
   */
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_BSM - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  offset.stack = stack;

  while (1)
  {
    /*
     * Case 1: val.c == ..._UNCHAINED, va is non-zero.
     * Case 2: val.c == ..._INVALID, va is zero.
     * Case 3: val.c == ..._NOT_RDY, va is zero.
     */
    MPIPE_BSM_REGION_VAL_t val;
    val.word = __gxio_mmio_read(context->mmio_fast_base + offset.word);

    /*
     * Handle case 1 and 2 by returning the buffer (or NULL).
     * Handle case 3 by waiting for the prefetch buffer to refill.
     */
    if (val.c != MPIPE_EDMA_DESC_WORD1__C_VAL_NOT_RDY)
    {
      /*
       * Set the instance bit in the buffer descriptor.
       * mPIPE HW won't set it.
       */
      val.inst = context->instance;

      return val;
    }
  }
}

/** Push a buffer onto a previously initialized buffer stack.
 *
 * The size of the buffer being pushed must match the size that was
 * registered with gxio_mpipe_init_buffer_stack().
 *
 * Each buffer's memory must be aligned to a 128-byte boundary.
 *
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 * @param buffer The address of the buffer. This memory must be
 * 128-byte aligned; the low 7 bits of the address are ignored.
 */
static __USUALLY_INLINE void
gxio_mpipe_push_buffer(gxio_mpipe_context_t* context,
                       unsigned int stack, void* buffer)
{
  MPIPE_BSM_REGION_ADDR_t offset = {{ 0 }};
  MPIPE_BSM_REGION_VAL_t val = {{ 0 }};

  /*
   * The mmio_fast_base region starts at the IDMA region, so subtract
   * off that initial offset.
   */
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_BSM - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  offset.stack = stack;

#if __SIZEOF_POINTER__ == 4
  val.va = ((unsigned long)buffer) >> MPIPE_BSM_REGION_VAL__VA_SHIFT;
#else
  val.va = ((long)buffer) >> MPIPE_BSM_REGION_VAL__VA_SHIFT;
#endif

  __gxio_mmio_write(context->mmio_fast_base + offset.word, val.word);
}

/** Pop a buffer off of a previously initialized buffer stack.
 *
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 * @return The buffer or NULL if the stack is empty.
 */
static __USUALLY_INLINE void*
gxio_mpipe_pop_buffer(gxio_mpipe_context_t* context, unsigned int stack)
{
  gxio_mpipe_bdesc_t val = gxio_mpipe_pop_buffer_bdesc(context, stack);

  return (void*)((unsigned long)val.va << MPIPE_BSM_REGION_VAL__VA_SHIFT);
}

/******************************************************************
 *                          NotifRings                            *
 ******************************************************************/


/** Allocate a set of NotifRings.
 *
 * The return value is NOT interesting if count is zero.
 *
 * Note that NotifRings are allocated in chunks, so allocating one at
 * a time is much less efficient than allocating several at once.
 *
 * @param context An initialized mPIPE context.
 * @param count Number of NotifRings required.
 * @param first Index of first NotifRing if ::GXIO_MPIPE_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits from ::gxio_mpipe_alloc_flags_e.
 * @return Index of first allocated buffer NotifRing, or
 * ::GXIO_MPIPE_ERR_NO_NOTIF_RING if allocation failed.
 */
extern int
gxio_mpipe_alloc_notif_rings(gxio_mpipe_context_t* context,
                             unsigned int count, unsigned int first,
                             unsigned int flags);


/** Initialize a NotifRing, using the given memory and size.
 *
 * A NotifRing's memory must be 4KB aligned.
 *
 * @param context An initialized mPIPE context.
 * @param ring The NotifRing index.
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_mpipe_idesc_t structures. This memory must be
 * 4KB aligned; the low 12 bits of the address are ignored.
 * @param mem_size Number of bytes in the ring.  Must be 128, 512,
 * 2048, or 65536 * sizeof(gxio_mpipe_idesc_t).
 * @param mem_flags ::gxio_mpipe_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_NOTIF_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_mpipe_init_notif_ring(gxio_mpipe_context_t* context,
                           unsigned int ring,
                           void* mem, size_t mem_size,
                           unsigned int mem_flags);


/******************************************************************
 *                        Notif Groups                            *
 ******************************************************************/


/** Allocate a set of NotifGroups.
 *
 * The return value is NOT interesting if count is zero.
 *
 * @param context An initialized mPIPE context.
 * @param count Number of NotifGroups required.
 * @param first Index of first NotifGroup if ::GXIO_MPIPE_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits from ::gxio_mpipe_alloc_flags_e.
 * @return Index of first allocated buffer NotifGroup, or
 * ::GXIO_MPIPE_ERR_NO_NOTIF_GROUP if allocation failed.
 */
extern int
gxio_mpipe_alloc_notif_groups(gxio_mpipe_context_t* context,
                              unsigned int count, unsigned int first,
                              unsigned int flags);

/** Add a NotifRing to a NotifGroup.  This only sets a bit in the
 * application's 'group' object; the hardware NotifGroup can be
 * initialized by passing 'group' to gxio_mpipe_init_notif_group() or
 * gxio_mpipe_init_notif_group_and_buckets().
 */
static __USUALLY_INLINE void
gxio_mpipe_notif_group_add_ring(gxio_mpipe_notif_group_bits_t* bits, int ring)
{
  bits->ring_mask[ring / 64] |= (1ull << (ring  % 64));
}

/** Set a particular NotifGroup bitmask.  Since the load balancer
 * makes decisions based on both bucket and NotifGroup state, most
 * applications should use gxio_mpipe_init_notif_group_and_buckets()
 * rather than using this function to configure just a NotifGroup.
 */
extern int
gxio_mpipe_init_notif_group(gxio_mpipe_context_t* context, unsigned int group,
                            gxio_mpipe_notif_group_bits_t bits);



/******************************************************************
 *                         Load Balancer                          *
 ******************************************************************/

/** Allocate a set of load balancer buckets.
 *
 * The return value is NOT interesting if count is zero.
 *
 * Note that buckets are allocated in chunks, so allocating one at
 * a time is much less efficient than allocating several at once.
 *
 * Note that the buckets are actually divided into two sub-ranges, of
 * different sizes, and different chunk sizes, and the range you get
 * by default is determined by the size of the request.  Allocations
 * cannot span the two sub-ranges.
 *
 * @param context An initialized mPIPE context.
 * @param count Number of buckets required.
 * @param first Index of first bucket if ::GXIO_MPIPE_ALLOC_FIXED flag is set,
 *   otherwise ignored.
 * @param flags Flag bits from ::gxio_mpipe_alloc_flags_e.
 * @return Index of first allocated buffer bucket, or
 * ::GXIO_MPIPE_ERR_NO_BUCKET if allocation failed.
 */
extern int
gxio_mpipe_alloc_buckets(gxio_mpipe_context_t* context,
                         unsigned int count, unsigned int first,
                         unsigned int flags);

/** The legal modes for gxio_mpipe_bucket_info_t and
 * gxio_mpipe_init_notif_group_and_buckets().
 *
 * All modes except ::GXIO_MPIPE_BUCKET_ROUND_ROBIN expect that the user
 * will allocate a power-of-two number of buckets and initialize them
 * to the same mode.  The classifier program then uses the appropriate
 * number of low bits from the incoming packet's flow hash to choose a
 * load balancer bucket.  Based on that bucket's load balancing mode,
 * reference count, and currently active NotifRing, the load balancer
 * chooses the NotifRing to which the packet will be delivered.
 */
typedef enum {
  /** All packets for a bucket go to the same NotifRing unless the
   * NotifRing gets full, in which case packets will be dropped.  If
   * the bucket reference count ever reaches zero, a new NotifRing may
   * be chosen.
   */
  GXIO_MPIPE_BUCKET_DYNAMIC_FLOW_AFFINITY =
  MPIPE_LBL_INIT_DAT_BSTS_TBL__MODE_VAL_DFA,

  /** All packets for a bucket always go to the same NotifRing.
   */
  GXIO_MPIPE_BUCKET_STATIC_FLOW_AFFINITY =
  MPIPE_LBL_INIT_DAT_BSTS_TBL__MODE_VAL_FIXED,

  /** All packets for a bucket go to the least full NotifRing in the
   * group, providing load balancing round robin behavior.
   */
  GXIO_MPIPE_BUCKET_ROUND_ROBIN =
  MPIPE_LBL_INIT_DAT_BSTS_TBL__MODE_VAL_ALWAYS_PICK,

  /** All packets for a bucket go to the same NotifRing unless the
   * NotifRing gets full, at which point the bucket starts using the
   * least full NotifRing in the group.  If all NotifRings in the
   * group are full, packets will be dropped.
   */
  GXIO_MPIPE_BUCKET_STICKY_FLOW_LOCALITY =
  MPIPE_LBL_INIT_DAT_BSTS_TBL__MODE_VAL_STICKY,

  /** All packets for a bucket go to the same NotifRing unless the
   * NotifRing gets full, or a random timer fires, at which point the
   * bucket starts using the least full NotifRing in the group.  If
   * all NotifRings in the group are full, packets will be dropped.
   * WARNING: This mode is BROKEN on chips with fewer than 64 tiles.
   */
  GXIO_MPIPE_BUCKET_PREFER_FLOW_LOCALITY =
  MPIPE_LBL_INIT_DAT_BSTS_TBL__MODE_VAL_STICKY_RAND,

} gxio_mpipe_bucket_mode_t;


/** Copy a set of bucket initialization values into the mPIPE
 * hardware.  Since the load balancer makes decisions based on both
 * bucket and NotifGroup state, most applications should use
 * gxio_mpipe_init_notif_group_and_buckets() rather than using this
 * function to configure a single bucket.
 *
 * @param context An initialized mPIPE context.
 * @param bucket Bucket index to be initialized.
 * @param bucket_info Initial reference count, NotifRing index, and mode.
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_BUCKET on failure.
 */
extern int
gxio_mpipe_init_bucket(gxio_mpipe_context_t* context, unsigned int bucket,
                       gxio_mpipe_bucket_info_t bucket_info);


/** Initializes a group and range of buckets and range of rings such
 * that the load balancer runs a particular load balancing function.
 *
 * First, the group is initialized with the given rings.
 *
 * Second, each bucket is initialized with the mode and group, and a
 * ring chosen round-robin from the given rings.
 *
 * Normally, the classifier picks a bucket, and then the load balancer
 * picks a ring, based on the bucket's mode, group, and current ring,
 * possibly updating the bucket's ring.
 *
 * @param context An initialized mPIPE context.
 * @param group The group.
 * @param ring The first ring.
 * @param num_rings The number of rings.
 * @param bucket The first bucket.
 * @param num_buckets The number of buckets.
 * @param mode The load balancing mode.
 *
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_BUCKET,
 * ::GXIO_MPIPE_ERR_BAD_NOTIF_GROUP, or
 * ::GXIO_MPIPE_ERR_BAD_NOTIF_RING on failure.
 */
extern int
gxio_mpipe_init_notif_group_and_buckets(gxio_mpipe_context_t* context,
                                        unsigned int group,
                                        unsigned int ring,
                                        unsigned int num_rings,
                                        unsigned int bucket,
                                        unsigned int num_buckets,
                                        gxio_mpipe_bucket_mode_t mode);


/** Return credits to a NotifRing and/or bucket.
 *
 * @param context An initialized mPIPE context.
 * @param ring The NotifRing index, or -1.
 * @param bucket The bucket, or -1.
 * @param count The number of credits to return.
 */
static __USUALLY_INLINE void
gxio_mpipe_credit(gxio_mpipe_context_t* context,
                  int ring, int bucket, unsigned int count)
{
  /* NOTE: Fancy struct initialization would break "C89" header test. */

  MPIPE_IDMA_RELEASE_REGION_ADDR_t offset = {{ 0 }};
  MPIPE_IDMA_RELEASE_REGION_VAL_t val = {{ 0 }};

  /*
   * The mmio_fast_base region starts at the IDMA region, so subtract
   * off that initial offset.
   */
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_IDMA - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  offset.ring = ring;
  offset.bucket = bucket;
  offset.ring_enable = (ring >= 0);
  offset.bucket_enable = (bucket >= 0);
  val.count = count;

  __gxio_mmio_write(context->mmio_fast_base + offset.word, val.word);
}



/******************************************************************
 *                         Egress Rings                           *
 ******************************************************************/


/** Allocate a set of eDMA rings.
 *
 * The return value is NOT interesting if count is zero.
 *
 * @param context An initialized mPIPE context.
 * @param count Number of eDMA rings required.
 * @param first Index of first eDMA ring if ::GXIO_MPIPE_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits from ::gxio_mpipe_alloc_flags_e.
 * @return Index of first allocated buffer eDMA ring, or
 * ::GXIO_MPIPE_ERR_NO_EDMA_RING if allocation failed.
 */
extern int
gxio_mpipe_alloc_edma_rings(gxio_mpipe_context_t* context,
                            unsigned int count, unsigned int first,
                            unsigned int flags);


/** Initialize an eDMA ring, using the given memory and size.
 *
 * An eDMA ring's memory must be 1KB aligned.
 *
 * @param context An initialized mPIPE context.
 * @param ering The eDMA ring index.
 * @param channel The channel to use.  This must be one of the channels
 * associated with the context's set of open links.
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_mpipe_edesc_t structures. This memory must be
 * 1KB aligned; the low 10 bits of the address are ignored.
 * @param mem_size Number of bytes in the ring.  Must be 512, 2048,
 * 8192 or 65536, times 16 (i.e. sizeof(gxio_mpipe_edesc_t)).
 * @param mem_flags ::gxio_mpipe_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_EDMA_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_mpipe_init_edma_ring(gxio_mpipe_context_t* context,
                          unsigned int ering, unsigned int channel,
                          void* mem, size_t mem_size, unsigned int mem_flags);


/** Set the "max_blks", "min_snf_blks", and "db" fields of
 * ::MPIPE_EDMA_RG_INIT_DAT_THRESH_t for a given edma ring.
 *
 * The global pool of dynamic blocks will be automatically adjusted.
 *
 * This function should not be called after any egress has been done
 * on the edma ring.
 *
 * Most applications should just use gxio_mpipe_equeue_set_snf_size().
 *
 * @param context An initialized mPIPE context.
 * @param ering The eDMA ring index.
 * @param max_blks The number of blocks to dedicate to the ring
 * (normally min_snf_blks + 1).  Must be greater than min_snf_blocks.
 * @param min_snf_blks The number of blocks which must be stored
 * prior to starting to send the packet (normally 12).
 * @param db Whether to allow use of dynamic blocks by the ring
 * (normally 1).
 *
 * @return 0 on success, negative on error.
 */
extern int
gxio_mpipe_config_edma_ring_blks(gxio_mpipe_context_t* context,
                                 unsigned int ering,
                                 unsigned int max_blks,
                                 unsigned int min_snf_blks,
                                 unsigned int db);

/** @} */


/******************************************************************
 *                      Classifier Program                        *
 ******************************************************************/

/**
 * @addtogroup gxio_mpipe_classifier
 * @{
 *
 * Functions for loading or configuring the mPIPE classifier program.
 *
 * The mPIPE classification processors all run a special "classifier"
 * program which, for each incoming packet, parses the packet headers,
 * encodes some packet metadata in the "idesc", and either drops the
 * packet, or picks a notif ring to handle the packet, and a buffer
 * stack to contain the packet, usually based on the channel, VLAN,
 * dMAC, flow hash, and packet size, under the guidance of the "rules"
 * API described below.
 *
 * @section gxio_mpipe_classifier_default Default Classifier
 *
 * The MDE provides a simple "default" classifier program.  It is
 * shipped as source in "$TILERA_ROOT/src/sys/mpipe/classifier.c",
 * which serves as its official documentation.  It is shipped as a
 * binary program in "$TILERA_ROOT/tile/boot/classifier", which is
 * automatically included in bootroms created by "tile-monitor", and
 * is automatically loaded by the hypervisor at boot time.
 *
 * The L2 analysis handles LLC packets, SNAP packets, and "VLAN
 * wrappers" (keeping the outer VLAN).
 *
 * The L3 analysis handles IPv4 and IPv6, dropping packets with bad
 * IPv4 header checksums, requesting computation of a TCP/UDP checksum
 * if appropriate, and hashing the dest and src IP addresses, plus the
 * ports for TCP/UDP packets, into the flow hash.  No special analysis
 * is done for "fragmented" packets or "tunneling" protocols.  Thus,
 * the first fragment of a fragmented TCP/UDP packet is hashed using
 * src/dest IP address and ports and all subsequent fragments are only
 * hashed according to src/dest IP address.
 *
 * The L3 analysis handles other packets too, hashing the dMAC
 * smac into a flow hash.
 *
 * The channel, VLAN, and dMAC used to pick a "rule" (see the
 * "rules" APIs below), which in turn is used to pick a buffer stack
 * (based on the packet size) and a bucket (based on the flow hash).
 *
 * To receive traffic matching a particular (channel/VLAN/dMAC
 * pattern, an application should allocate its own buffer stacks and
 * load balancer buckets, and map traffic to those stacks and buckets,
 * as decribed by the "rules" API below.
 *
 * Various packet metadata is encoded in the idesc.  The flow hash is
 * four bytes at 0x0C.  The VLAN is two bytes at 0x10.  The ethtype is
 * two bytes at 0x12.  The l3 start is one byte at 0x14.  The l4 start
 * is one byte at 0x15 for IPv4 and IPv6 packets, and otherwise zero.
 * The protocol is one byte at 0x16 for IPv4 and IPv6 packets, and
 * otherwise zero.
 *
 * @section gxio_mpipe_classifier_custom Custom Classifiers.
 *
 * A custom classifier may be created using "tile-mpipe-cc" with a
 * customized version of the default classifier sources.
 *
 * The custom classifier may be included in bootroms using the
 * "--classifier" option to "tile-monitor", or loaded dynamically
 * using gxio_mpipe_classifier_load_from_file().
 *
 * Be aware that "extreme" customizations may break the assumptions of
 * the "rules" APIs described below, but simple customizations, such
 * as adding new packet metadata, should be fine.
 */


/** Load a classifier binary from a file. */
extern int
gxio_mpipe_classifier_load_from_file(gxio_mpipe_context_t* context,
                                     const char* path);

/** Load a classifier binary from some bytes. */
extern int
gxio_mpipe_classifier_load_from_bytes(gxio_mpipe_context_t* context,
                                      const void* blob, size_t blob_size);

/** Customize a customized classifier. */
extern int
gxio_mpipe_classifier_customize(gxio_mpipe_context_t* context,
                                const char* symbol,
                                const void* data, size_t len);


/** A set of classifier rules, plus a context. */
typedef struct {

  /** The context. */
  gxio_mpipe_context_t* context;

  /** The actual rules. */
  gxio_mpipe_rules_list_t list;

} gxio_mpipe_rules_t;


/** Initialize a classifier program rules list.
 *
 * This function can be called on a previously initialized rules list
 * to discard any previously added rules.
 *
 * @param rules Rules list to initialize.
 * @param context An initialized mPIPE context.
 */
extern void
gxio_mpipe_rules_init(gxio_mpipe_rules_t* rules,
                      gxio_mpipe_context_t* context);

/** Begin a new rule on the indicated rules list.
 *
 * Note that an empty rule matches all packets, but an empty rule list
 * matches no packets.
 *
 * @param rules Rules list to which new rule is appended.
 * @param bucket First load balancer bucket to which packets will be
 * delivered.
 * @param num_buckets Number of buckets (must be a power of two) across
 * which packets will be distributed based on the "flow hash".
 * @param stacks Either NULL, to assign each packet to the smallest
 * initialized buffer stack which does not induce chaining (and to
 * drop packets which exceed the largest initialized buffer stack
 * buffer size), or an array, with each entry indicating which buffer
 * stack should be used for packets up to that size (with 255
 * indicating that those packets should be dropped).
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_begin(gxio_mpipe_rules_t* rules,
                       unsigned int bucket, unsigned int num_buckets,
                       gxio_mpipe_rules_stacks_t* stacks);


/** Set the priority of the current rule.
 *
 * The default priority is zero.  A negative priority has "higher"
 * priority, and a positive priority has "lower" priority.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param priority The priority.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_set_priority(gxio_mpipe_rules_t* rules,
                              int priority);


/** Set the headroom of the current rule.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param headroom The headroom.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_set_headroom(gxio_mpipe_rules_t* rules,
                              uint8_t headroom);



/** Set the tailroom of the current rule.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param tailroom The tailroom.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_set_tailroom(gxio_mpipe_rules_t* rules,
                              uint8_t tailroom);

/** Set the capacity of the current rule.
 *
 * If the packet size, plus the headroom and tailroom, exceed the
 * capacity, then the packet will be dropped.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param capacity The capacity.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_set_capacity(gxio_mpipe_rules_t* rules,
                              uint16_t capacity);


/** Indicate that packets from a particular channel can be delivered
 * to the buckets and buffer stacks associated with the current rule.
 *
 * Channels added must be associated with links opened by the mPIPE context
 * used in gxio_mpipe_rules_init().  A rule with no channels is equivalent
 * to a rule naming all such associated channels.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param channel The channel to add.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_add_channel(gxio_mpipe_rules_t* rules,
                             unsigned int channel);


/** Indicate that packets targetting a particular destination MAC
 * address can be delivered to the current rule's buckets and buffer
 * stacks.
 *
 * A rule with NO dMACs is equivalent to a rule with ALL dMACs.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param dmac The destination MAC to add.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_add_dmac(gxio_mpipe_rules_t* rules,
                          gxio_mpipe_rules_dmac_t dmac);

/** Indicate that packets with a particular VLAN can be delivered to
 * the current rule's buckets and buffer stacks.
 *
 * A rule with NO VLANs is equivalent to a rule with ALL VLANs.
 *
 * @param rules Rules list whose current rule will be modified.
 * @param vlan The VLAN to add.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_add_vlan(gxio_mpipe_rules_t* rules,
                          gxio_mpipe_rules_vlan_t vlan);


/** Commit rules.
 *
 * The rules are sent to the hypervisor, where they are combined with
 * the rules from other apps, and used to program the hardware classifier.
 *
 * Note that if this function returns an error, then the rules will NOT
 * have been committed, even if the error is due to interactions with
 * rules from another app.
 *
 * @param rules Rules list to commit.
 * @return 0 on success, or a negative error code on failure.
 */
extern int
gxio_mpipe_rules_commit(gxio_mpipe_rules_t* rules);

/** @} */


/******************************************************************
 *                     Ingress Queue Wrapper                      *
 ******************************************************************/

/**
 * @addtogroup gxio_mpipe_wrappers
 * @{
 *
 * Convenience functions for receiving packets from a NotifRing and
 * sending packets via an eDMA ring.
 *
 * The mpipe ingress and egress hardware uses shared memory packet
 * descriptors to describe packets that have arrived on ingress or
 * are destined for egress.  These descriptors are stored in shared
 * memory ring buffers and written or read by hardware as necessary.
 * The gxio library provides wrapper functions that manage the head and
 * tail pointers for these rings, allowing the user to easily read or
 * write packet descriptors.
 *
 * The initialization interface for ingress and egress rings is quite
 * similar.  For example, to create an ingress queue, the user passes
 * a ::gxio_mpipe_iqueue_t state object, a ring number from
 * gxio_mpipe_alloc_notif_rings(), and the address of memory to hold a
 * ring buffer to the gxio_mpipe_iqueue_init() function.  The function
 * returns success when the state object has been initialized and the
 * hardware configured to deliver packets to the specified ring
 * buffer.  Similarly, gxio_mpipe_equeue_init() takes a
 * ::gxio_mpipe_equeue_t state object, a ring number from
 * gxio_mpipe_alloc_edma_rings(), and a shared memory buffer.
 *
 * @section gxio_mpipe_iqueue Working with Ingress Queues
 *
 * Once initialized, the gxio_mpipe_iqueue_t API provides two flows
 * for getting the ::gxio_mpipe_idesc_t packet descriptor associated
 * with incoming packets.  The simplest is to call
 * gxio_mpipe_iqueue_get() or gxio_mpipe_iqueue_try_get().  These
 * functions copy the oldest packet descriptor out of the NotifRing and
 * into a descriptor provided by the caller.  They also immediately
 * inform the hardware that a descriptor has been processed.
 *
 * For applications with stringent performance requirements, higher
 * efficiency can be achieved by avoiding the packet descriptor copy
 * and processing multiple descriptors at once.  The
 * gxio_mpipe_iqueue_peek() and gxio_mpipe_iqueue_try_peek() functions
 * allow such optimizations.  These functions provide a pointer to the
 * next valid ingress descriptor in the NotifRing's shared memory ring
 * buffer, and a count of how many contiguous descriptors are ready to
 * be processed.  The application can then process any number of those
 * descriptors in place, calling gxio_mpipe_iqueue_consume() to inform
 * the hardware after each one has been processed.
 *
 * @section gxio_mpipe_equeue Working with Egress Queues
 *
 * Similarly, the egress queue API provides a high-performance
 * interface plus a simple wrapper for use in posting
 * ::gxio_mpipe_edesc_t egress packet descriptors.  The simple
 * version, gxio_mpipe_equeue_put(), allows the programmer to wait for
 * an eDMA ring slot to become available and write a single descriptor
 * into the ring.
 *
 * Alternatively, you can reserve slots in the eDMA ring using
 * gxio_mpipe_equeue_reserve() or gxio_mpipe_equeue_try_reserve(), and
 * then fill in each slot using gxio_mpipe_equeue_put_at().  This
 * capability can be used to amortize the cost of reserving slots
 * across several packets.  It also allows gather operations to be
 * performed on a shared equeue, by ensuring that the edescs for all
 * the fragments are all contiguous in the eDMA ring.
 *
 * The gxio_mpipe_equeue_reserve() and gxio_mpipe_equeue_try_reserve()
 * functions return a 63-bit "completion slot", which is actually a
 * sequence number, the low bits of which indicate the ring buffer
 * index and the high bits the number of times the application has
 * gone around the egress ring buffer.  The extra bits allow an
 * application to check for egress completion by calling
 * gxio_mpipe_equeue_is_complete() to see whether a particular 'slot'
 * number has finished.  Given the maximum packet rates of the Gx
 * processor, the 63-bit slot number will never wrap.
 *
 * In practice, most applications use the ::gxio_mpipe_edesc_t::hwb
 * bit to indicate that the buffers containing egress packet data
 * should be pushed onto a buffer stack when egress is complete.  Such
 * applications generally do not need to know when an egress operation
 * completes (since there is no need to free a buffer post-egress),
 * and thus can use the optimized gxio_mpipe_equeue_reserve_fast() or
 * gxio_mpipe_equeue_try_reserve_fast() functions, which return a 24
 * bit "slot", instead of a 63-bit "completion slot".
 *
 * Once a slot has been "reserved", it MUST be filled.  If the
 * application reserves a slot and then decides that it does not
 * actually need it, it can set the ::gxio_mpipe_edesc_t::ns (no send)
 * bit on the descriptor passed to gxio_mpipe_equeue_put_at() to
 * indicate that no data should be sent.  This technique can also be
 * used to drop an incoming packet, instead of forwarding it, since
 * any buffer will still be pushed onto the buffer stack when the
 * egress descriptor is processed.
 *
 * Note that any underlying memory referenced by ::gxio_mpipe_edesc_t
 * must be flushed (using arch_atomic_write_barrier(), __insn_mf(), or
 * __sync_synchronize()) before calling gxio_mpipe_equeue_put_at().
 * Batching can be used to amortize the cost of the flush.
 */

/** Enum for iqueue entry counts supported by mPIPE. */
enum {
  /** 128 entries iqueue. */
  GXIO_MPIPE_IQUEUE_ENTRY_128 = 128,
  /** 512 entries iqueue. */
  GXIO_MPIPE_IQUEUE_ENTRY_512 = 512,
  /** 2048 entries iqueue. */
  GXIO_MPIPE_IQUEUE_ENTRY_2K  = 2048,
  /** 65536 entries iqueue. */
  GXIO_MPIPE_IQUEUE_ENTRY_64K = (64 * 1024)
};


/** A convenient interface to a NotifRing, for use by a single thread.
 */
typedef struct {

  /** The context. */
  gxio_mpipe_context_t* context;

  /** The actual NotifRing. */
  gxio_mpipe_idesc_t* idescs;

  /** The number of entries. */
  unsigned long num_entries;

  /** The number of entries minus one. */
  unsigned long mask_num_entries;

  /** The log2() of the number of entries. */
  unsigned long log2_num_entries;

  /** The next entry. */
  unsigned int head;

  /** The NotifRing id. */
  unsigned int ring;

#ifdef __BIG_ENDIAN__
  /** The number of byteswapped entries. */
  unsigned int swapped;
#endif

} gxio_mpipe_iqueue_t;


/** Initialize an iqueue wrapper. 
 *
 * This function uses gxio_mpipe_init_notif_ring() to initialize the
 * underlying NotifRing using the provided arguments.
 *
 * A NotifRing's memory must be 4KB aligned.
 *
 * @param iqueue The ingress queue to be initialized.
 * @param context An initialized mPIPE context.
 * @param ring The NotifRing index.
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_mpipe_idesc_t structures. This memory must be
 * 4KB aligned; the low 12 bits of the address are ignored.
 * @param mem_size Number of bytes in the ring.  Must be 128, 512,
 * 2048, or 65536 * sizeof(gxio_mpipe_idesc_t).
 * @param mem_flags ::gxio_mpipe_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_NOTIF_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_mpipe_iqueue_init(gxio_mpipe_iqueue_t* iqueue,
                       gxio_mpipe_context_t* context,
                       unsigned int ring,
                       void* mem, size_t mem_size, unsigned int mem_flags);


/** Advance over some old entries in an iqueue.
 *
 * Please see the documentation for gxio_mpipe_iqueue_consume().
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param count The number of entries to advance over.
 */
static __USUALLY_INLINE void
gxio_mpipe_iqueue_advance(gxio_mpipe_iqueue_t* iqueue, int count)
{
  /* Advance with proper wrap. */
  int head = iqueue->head + count;
  iqueue->head =
    (head & iqueue->mask_num_entries) + (head >> iqueue->log2_num_entries);

#ifdef __BIG_ENDIAN__
  /* HACK: Track swapped entries. */
  iqueue->swapped -= count;
#endif
}


/** Release the ring and bucket for an old entry in an iqueue.
 *
 * Releasing the ring allows more packets to be delivered to the ring.
 *
 * Releasing the bucket allows flows using the bucket to be moved to a
 * new ring when using GXIO_MPIPE_BUCKET_DYNAMIC_FLOW_AFFINITY.
 *
 * This function is shorthand for "gxio_mpipe_credit(iqueue->context,
 * iqueue->ring, idesc->bucket_id, 1)", and it may be more convenient
 * to make that underlying call, using those values, instead of
 * tracking the entire "idesc".
 *
 * If packet processing is deferred, optimal performance requires that
 * the releasing be deferred as well.
 *
 * Please see the documentation for gxio_mpipe_iqueue_consume().
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc The descriptor which was processed.
 */
static __USUALLY_INLINE void
gxio_mpipe_iqueue_release(gxio_mpipe_iqueue_t* iqueue,
                          gxio_mpipe_idesc_t* idesc)
{
  gxio_mpipe_credit(iqueue->context, iqueue->ring, idesc->bucket_id, 1);
}


/** Consume a packet from an "iqueue".
 *
 * After processing packets peeked at via gxio_mpipe_iqueue_peek()
 * or gxio_mpipe_iqueue_try_peek(), you must call this function, or
 * gxio_mpipe_iqueue_advance() plus gxio_mpipe_iqueue_release(), to
 * advance over those entries, and release their rings and buckets.
 *
 * You may call this function as each packet is processed, or you can
 * wait until several packets have been processed.
 *
 * Note that if you are using a single bucket, and you are handling
 * batches of N packets, then you can replace several calls to this
 * function with calls to "gxio_mpipe_iqueue_advance(iqueue, N)" and
 * "gxio_mpipe_credit(iqueue->context, iqueue->ring, bucket, N)".
 *
 * Note that if your classifier sets "idesc->nr", then you should
 * explicitly call "gxio_mpipe_iqueue_advance(iqueue, idesc)" plus
 * "gxio_mpipe_credit(iqueue->context, iqueue->ring, -1, 1)", to
 * avoid incorrectly crediting the (unused) bucket.
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc The descriptor which was processed.
 */
static __USUALLY_INLINE void
gxio_mpipe_iqueue_consume(gxio_mpipe_iqueue_t* iqueue,
                          gxio_mpipe_idesc_t* idesc)
{
  gxio_mpipe_iqueue_advance(iqueue, 1);
  gxio_mpipe_iqueue_release(iqueue, idesc);
}


/** Peek at the next packet(s) in an "iqueue", without waiting.
 *
 * If no packets are available, fills idesc_ref with NULL, and then
 * returns ::GXIO_MPIPE_ERR_IQUEUE_EMPTY.  Otherwise, fills idesc_ref
 * with the address of the next valid packet descriptor, and returns
 * the maximum number of valid descriptors which can be processed.
 * You may process fewer descriptors if desired.
 *
 * Call gxio_mpipe_iqueue_consume() on each packet once it has been
 * processed (or dropped), to allow more packets to be delivered.
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc_ref A pointer to a packet descriptor pointer.
 * @return The (positive) number of packets which can be processed,
 * or ::GXIO_MPIPE_ERR_IQUEUE_EMPTY if no packets are available.
 */
static __USUALLY_INLINE int
gxio_mpipe_iqueue_try_peek(gxio_mpipe_iqueue_t* iqueue,
                           gxio_mpipe_idesc_t** idesc_ref)
{
  gxio_mpipe_idesc_t* next;

  uint64_t head = iqueue->head;
  uint64_t tail = __gxio_mmio_read(iqueue->idescs);

  /* Available entries. */
  uint64_t avail =
    (tail >= head) ? (tail - head) : (iqueue->num_entries - head);

  if (avail == 0)
  {
    *idesc_ref = NULL;
    return GXIO_MPIPE_ERR_IQUEUE_EMPTY;
  }

  next = &iqueue->idescs[head];

  /* Prefetch the first idesc. */
  __insn_prefetch(next);

#ifdef __BIG_ENDIAN__
  /* HACK: Swap new entries directly in memory. */
  {
    int i, j;
    for (i = iqueue->swapped; i < avail; i++)
    {
      for (j = 0; j < 8; j++)
        next[i].words[j] = __builtin_bswap64(next[i].words[j]);
    }
    iqueue->swapped = avail;
  }
#endif

  *idesc_ref = next;

  return avail;
}


/** Peek at the next packet(s) in an "iqueue", waiting if necessary.
 *
 * Waits for a packet to be available, and then fills idesc_ref with
 * the address of the next valid packet descriptor, and returns
 * the maximum number of valid descriptors which can be processed.
 * You may process fewer descriptors if desired.
 *
 * Call gxio_mpipe_iqueue_consume() on each packet once it has been
 * processed (or dropped), to allow more packets to be delivered.
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc_ref A pointer to a packet descriptor pointer.
 * @return The (positive) number of packets which can be processed.
 */
extern int
gxio_mpipe_iqueue_peek(gxio_mpipe_iqueue_t* iqueue,
                       gxio_mpipe_idesc_t** idesc_ref);


/** Get the next packet in an "iqueue", if any.
 *
 * If no packets are available, returns ::GXIO_MPIPE_ERR_IQUEUE_EMPTY.
 * Otherwise, copies the next valid packet descriptor into idesc,
 * consumes that packet descriptor, and returns zero.
 *
 * This is a convenience wrapper around gxio_mpipe_iqueue_try_peek(),
 * memcpy(), and gxio_mpipe_iqueue_consume().
 *
 * @param iqueue An igress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc A description of the packet to be filled in.
 * @return 0 on success, or ::GXIO_MPIPE_ERR_IQUEUE_EMPTY if no packets
 * are available.
 */
extern int
gxio_mpipe_iqueue_try_get(gxio_mpipe_iqueue_t* iqueue,
                          gxio_mpipe_idesc_t* idesc);


/** Get the next packet in an "iqueue", waiting if necessary.
 *
 * Waits for a packet to be available, and then copies the next valid
 * packet descriptor into idesc, and consumes that packet descriptor.
 *
 * This is a convenience wrapper around gxio_mpipe_iqueue_peek(),
 * memcpy(), and gxio_mpipe_iqueue_consume().
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc A description of the packet to be filled in.
 * @return 0 on success.
 */
extern int
gxio_mpipe_iqueue_get(gxio_mpipe_iqueue_t* iqueue,
                      gxio_mpipe_idesc_t* idesc);



/** Drop a packet by pushing its buffer (if appropriate).
 *
 * NOTE: The caller must still call gxio_mpipe_iqueue_consume() if idesc
 * came from gxio_mpipe_iqueue_try_peek() or gxio_mpipe_iqueue_peek().
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc A packet descriptor.
 */
static __USUALLY_INLINE void
gxio_mpipe_iqueue_drop(gxio_mpipe_iqueue_t* iqueue,
                       gxio_mpipe_idesc_t* idesc)
{
  if (idesc->be)
  {
    /* FIXME: Handle "chaining" properly. */
  }
  else
  {
    unsigned char* va = gxio_mpipe_idesc_get_va(idesc);
    gxio_mpipe_push_buffer(iqueue->context, idesc->stack_idx, va);
  }
}



/** Drop a packet if it is "bad".
 *
 * If gxio_mpipe_idesc_is_bad(idesc), then call gxio_mpipe_iqueue_drop()
 * and return 1, else, return 0.
 *
 * NOTE: The caller must still call gxio_mpipe_iqueue_consume() if idesc
 * came from gxio_mpipe_iqueue_try_peek() or gxio_mpipe_iqueue_peek().
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @param idesc A packet descriptor.
 * @return 1 if packet was dropped, else 0.
 */
static __USUALLY_INLINE int
gxio_mpipe_iqueue_drop_if_bad(gxio_mpipe_iqueue_t* iqueue,
                              gxio_mpipe_idesc_t* idesc)
{
  if (gxio_mpipe_idesc_is_bad(idesc))
  {
    gxio_mpipe_iqueue_drop(iqueue, idesc);
    return 1;
  }

  return 0;
}


/** Return the iqueue fullness.
 *
 * The hardware tracks the fullness of each NotifRing in a 3-bit quantized
 * value readable via user-space MMIO.  0 is empty, 7 is full.  The values
 * in between are controlled by the MPIPE_LBL_QUANT_THRESH registers.  They
 * will typically be configured to be exponential such that each incremental
 * 3-bit fullness value represents an exponentially larger number of packets
 * in the queue.
 *
 * @param iqueue An ingress queue initialized via gxio_mpipe_iqueue_init().
 * @return the 3 bit quantized fullness of the iqueue.
 */
static __USUALLY_INLINE int
gxio_mpipe_iqueue_get_fullness(gxio_mpipe_iqueue_t* iqueue)
{
  void* mmio_addr =
    (iqueue->context->mmio_cfg_base + MPIPE_LBL_NR_STATE__FIRST_WORD +
     ((iqueue->ring >> 4) << 3));

  uint64_t quant_reg = __gxio_mmio_read(mmio_addr);

  return (quant_reg >> (4 * (iqueue->ring & 0xf))) & 0x7;
}



/******************************************************************
 *                      Egress Queue Wrapper                      *
 ******************************************************************/

/** Enum code for equeue entries supported by mPIPE. */
enum {
  /** 512 entries equeue. */
  GXIO_MPIPE_EQUEUE_ENTRY_512 = 512,
  /** 2048 entries equeue. */
  GXIO_MPIPE_EQUEUE_ENTRY_2K  = 2048,
  /** 8192 entries equeue. */
  GXIO_MPIPE_EQUEUE_ENTRY_8K  = 8192,
  /** 65536 entries equeue. */
  GXIO_MPIPE_EQUEUE_ENTRY_64K = 0x10000
};


/** A convenient, thread-safe interface to an eDMA ring. */
typedef struct {

  /** State object for tracking head and tail pointers. */
  __gxio_dma_queue_t dma_queue;

  /** The ring entries. */
  gxio_mpipe_edesc_t* edescs;

  /** The number of entries minus one. */
  unsigned long mask_num_entries;

  /** The log2() of the number of entries. */
  unsigned long log2_num_entries;

  /** The context. */
  gxio_mpipe_context_t* context;

  /** The other mpipe context. Valid only with dual mpipe.
   Will be NULL with single mpipe instance. */
  gxio_mpipe_context_t* other_context;

  /** The ering. */
  unsigned int ering;

  /** The channel. */
  unsigned int channel;

  /** The remote buffer return count. */
  uint64_t rhwb_complete_count __attribute__ ((aligned(64)));

  /** The remote buffer return counts, each one is for 1/8
      segment of queue. */
  uint32_t rhwb_complete_bin[8];

  /** Instance bit map.
      instance << MPIPE_EDMA_DESC_WORD1__INST_SHIFT
      Used to quick check if SW need
      perform remote buffer return. */
  uint_reg_t inst_map  __attribute__ ((aligned(64)));

} gxio_mpipe_equeue_t;


/** Initialize an "equeue".
 *
 * This function uses gxio_mpipe_init_edma_ring() to initialize the
 * underlying edma_ring using the provided arguments.
 *
 * An eDMA ring's memory must be 1KB aligned.
 *
 * @param equeue The egress queue to be initialized.
 * @param context An initialized mPIPE context.
 * @param ering The eDMA ring index.
 * @param channel The channel to use.  This must be one of the channels
 * associated with the context's set of open links.
 * @param mem A physically contiguous region of memory to be filled
 * with a ring of ::gxio_mpipe_edesc_t structures. This memory must be
 * 1KB aligned; the low 10 bits of the address are ignored.
 * @param mem_size Number of bytes in the ring.  Must be 512, 2048,
 * 8192 or 65536, times 16 (i.e. sizeof(gxio_mpipe_edesc_t)).
 * @param mem_flags ::gxio_mpipe_mem_flags_e memory flags.
 *
 * @return 0 on success, ::GXIO_MPIPE_ERR_BAD_EDMA_RING or
 * ::GXIO_ERR_INVAL_MEMORY_SIZE on failure.
 */
extern int
gxio_mpipe_equeue_init(gxio_mpipe_equeue_t* equeue,
                       gxio_mpipe_context_t* context,
                       unsigned int ering,
                       unsigned int channel,
                       void* mem, unsigned int mem_size,
                       unsigned int mem_flags);

/** Reserve completion slots for edescs.
 *
 * Use gxio_mpipe_equeue_put_at() to actually populate the slots.
 *
 * This function is slower than gxio_mpipe_equeue_reserve_fast(), but
 * returns a full 64 bit completion slot, which can be used with
 * gxio_mpipe_equeue_is_complete().
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param num Number of slots to reserve (must be non-zero).
 * @return The first reserved completion slot, or a negative error code.
 */
static __USUALLY_INLINE int64_t
gxio_mpipe_equeue_reserve(gxio_mpipe_equeue_t* equeue, unsigned int num)
{
  return __gxio_dma_queue_reserve_aux(&equeue->dma_queue, num, true);
}

/** Reserve completion slots for edescs, if possible.
 *
 * Use gxio_mpipe_equeue_put_at() to actually populate the slots.
 *
 * This function is slower than gxio_mpipe_equeue_try_reserve_fast(),
 * but returns a full 64 bit completion slot, which can be used with
 * gxio_mpipe_equeue_is_complete().
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param num Number of slots to reserve (must be non-zero).
 * @return The first reserved completion slot, or a negative error code.
 */
static __USUALLY_INLINE int64_t
gxio_mpipe_equeue_try_reserve(gxio_mpipe_equeue_t* equeue, unsigned int num)
{
  return __gxio_dma_queue_reserve_aux(&equeue->dma_queue, num, false);
}


/** Reserve slots for edescs.
 *
 * Use gxio_mpipe_equeue_put_at() to actually populate the slots.
 *
 * This function is faster than gxio_mpipe_equeue_reserve(), but
 * returns a 24 bit slot (instead of a 64 bit completion slot), which
 * thus cannot be used with gxio_mpipe_equeue_is_complete().
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param num Number of slots to reserve (should be non-zero).
 * @return The first reserved slot, or a negative error code.
 */
static __USUALLY_INLINE int64_t
gxio_mpipe_equeue_reserve_fast(gxio_mpipe_equeue_t* equeue, unsigned int num)
{
  return __gxio_dma_queue_reserve(&equeue->dma_queue, num, true, false);
}

/** Reserve slots for edescs, if possible.
 *
 * Use gxio_mpipe_equeue_put_at() to actually populate the slots.
 *
 * This function is faster than gxio_mpipe_equeue_try_reserve(), but
 * returns a 24 bit slot (instead of a 64 bit completion slot), which
 * thus cannot be used with gxio_mpipe_equeue_is_complete().
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param num Number of slots to reserve (should be non-zero).
 * @return The first reserved slot, or a negative error code.
 */
static __USUALLY_INLINE int64_t
gxio_mpipe_equeue_try_reserve_fast(gxio_mpipe_equeue_t* equeue,
                                   unsigned int num)
{
  return __gxio_dma_queue_reserve(&equeue->dma_queue, num, false, false);
}


#ifndef __DOXYGEN__

/*
 * HACK: This helper function tricks gcc 4.6 into avoiding saving
 * a copy of "edesc->words[0]" on the stack for no obvious reason.
 */

static __USUALLY_INLINE void
gxio_mpipe_equeue_put_at_aux(gxio_mpipe_equeue_t* equeue,
                             uint_reg_t ew[2], unsigned long slot)
{
  unsigned long edma_slot = slot & equeue->mask_num_entries;
  gxio_mpipe_edesc_t* edesc_p = &equeue->edescs[edma_slot];

#if defined(GXIO_MULTI_MPIPES) && !defined(GXIO_RHWB_NONE)
  uint_reg_t mask = ew[1] >> 2;

#ifdef GX36_EMUL_DUAL_MPIPES
  /* Put the mPIPE INST. bit in the ew[1]. */
  ew[1] |= EDESC_W1_INST_MASK;
#endif
  /*
   * Compare the egress's INST bit with the equeue's instance id
   * (use inst_map field), if they are same, do nothing. If they
   * are different and HWB bit set, then clear the HWB bit and set
   * the RHWB bit. After the egress is done, SW will do the buffer
   * return if the RHWB bit was set.
   *
   * To generate optimized binary, take advantage of facts:
   * INST bit is at offset 56. HWB bit is at offset 58.
   * and newly defined RHWB bit is at offset 57.
   */
#if (EDESC_W1_INST_OFFSET != 56 || MPIPE_PDESC_WORD7__HWB_SHIFT != 58)
#error "Code assume HWB is at bit 58, INST bit is at 56!"
#endif

  mask &= ((ew[1] ^ equeue->inst_map) & EDESC_W1_INST_MASK);

  mask = (mask << 2) + (mask << 1);

  ew[1] ^= mask;

#endif  /*defined(GXIO_MULTI_MPIPES) && !defined(GXIO_RHWB_NONE)*/
  ew[0] |= !((slot >> equeue->log2_num_entries) & 1);

  /*
   * NOTE: We use "__gxio_mpipe_write()", plus the fact that the eDMA
   * queue alignment restrictions ensure that these two words are on
   * the same cacheline, to force proper ordering between the stores.
   */
  __gxio_mmio_write64(&edesc_p->words[1], ew[1]);
  __gxio_mmio_write64(&edesc_p->words[0], ew[0]);
}

#endif


/** Post an edesc to a given slot in an equeue.
 *
 * This function copies the supplied edesc into entry "slot mod N" in
 * the underlying ring, setting the "gen" bit to the appropriate value
 * based on "(slot mod N*2)", where "N" is the size of the ring.  Note
 * that the higher bits of slot are unused, and thus, this function
 * can handle "slots" as well as "completion slots".
 *
 * Normally this function is used to fill in slots reserved by
 * gxio_mpipe_equeue_try_reserve(), gxio_mpipe_equeue_reserve(),
 * gxio_mpipe_equeue_try_reserve_fast(), or
 * gxio_mpipe_equeue_reserve_fast(),
 *
 * This function can also be used without "reserving" slots, if the
 * application KNOWS that the ring can never overflow, for example, by
 * pushing fewer buffers into the buffer stacks than there are total
 * slots in the equeue, but this is NOT recommended.
 *
 * If GXIO_MULTI_MPIPES is defined, this function will automatically
 * enable the remote buffer return feature if there are multiple mpipe
 * instances available. It can be disabled if GXIO_RHWB_NONE is
 * defined.
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param edesc The egress descriptor to be posted.
 * @param slot An egress slot (only the low bits are actually used).
 */
static __USUALLY_INLINE void
gxio_mpipe_equeue_put_at(gxio_mpipe_equeue_t* equeue,
                         gxio_mpipe_edesc_t edesc, unsigned long slot)
{
  gxio_mpipe_equeue_put_at_aux(equeue, edesc.words, slot);
}


/** Post an edesc to the next slot in an equeue.
 *
 * This is a convenience wrapper around
 * gxio_mpipe_equeue_reserve_fast() and gxio_mpipe_equeue_put_at().
 *
 * If GXIO_MULTI_MPIPES is defined, this function will automatically
 * enable the remote buffer return feature if there are multiple mpipe
 * instances available. It can be disabled once GXIO_NO_RHWB is defined.
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param edesc The egress descriptor to be posted.
 * @return 0. This api always returns zero and caller has no need to check
 *  the return value.
 */
static __USUALLY_INLINE int
gxio_mpipe_equeue_put(gxio_mpipe_equeue_t* equeue,
                      gxio_mpipe_edesc_t edesc)
{
  int64_t slot = gxio_mpipe_equeue_reserve_fast(equeue, 1);

  gxio_mpipe_equeue_put_at(equeue, edesc, slot);

  return 0;
}


/** Ask the mPIPE hardware to egress outstanding packets immediately.
 *
 * This call is not necessary, but may slightly reduce overall latency.
 *
 * Technically, you should flush all gxio_mpipe_equeue_put_at() writes
 * to memory before calling this function, to ensure the descriptors
 * are visible in memory before the mPIPE hardware actually looks for
 * them.  But this should be very rare, and the only side effect would
 * be increased latency, so it is up to the caller to decide whether
 * or not to flush memory.
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 */
static __USUALLY_INLINE void
gxio_mpipe_equeue_flush(gxio_mpipe_equeue_t* equeue)
{
  /* Use "ring_idx = 0" and "count = 0" to "wake up" the eDMA ring. */
  MPIPE_EDMA_POST_REGION_VAL_t val = {{ 0 }};
  /* Flush the write buffers. */
  __insn_flushwb();
  __gxio_mmio_write(equeue->dma_queue.post_region_addr, val.word);
}


/** Determine if a given edesc has been completed.
 *
 * Note that this function requires a "completion slot", and thus may
 * NOT be used with a "slot" from gxio_mpipe_equeue_reserve_fast() or
 * gxio_mpipe_equeue_try_reserve_fast().
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param completion_slot The completion slot used by the edesc.
 * @param update If true, and the desc does not appear to have completed
 * yet, then update any software cache of the hardware completion counter,
 * and check again.  This should normally be true.
 * @return True iff the given edesc has been completed.
 */
static __USUALLY_INLINE int
gxio_mpipe_equeue_is_complete(gxio_mpipe_equeue_t* equeue,
                              int64_t completion_slot,
                              int update)
{
  return __gxio_dma_queue_is_complete(&equeue->dma_queue,
                                      completion_slot, update);
}


/** Set the snf (store and forward) size for an equeue.
 *
 * The snf size for an equeue defaults to 1536, and encodes the size
 * of the largest packet for which egress is guaranteed to avoid
 * transmission underruns and/or corrupt checksums under heavy load.
 *
 * The snf size affects a global resource pool which cannot support,
 * for example, all 24 equeues each requesting an snf size of 8K.
 *
 * To ensure that jumbo packets can be egressed properly, the snf size
 * should be set to the size of the largest possible packet, which
 * will usually be limited by the size of the app's largest buffer.
 *
 * This is a convenience wrapper around
 * gxio_mpipe_config_edma_ring_blks().
 *
 * This function should not be called after any egress has been done
 * on the equeue.
 *
 * @param equeue An egress queue initialized via gxio_mpipe_equeue_init().
 * @param size The snf size, in bytes.
 * @return Zero on success, negative error otherwise.
 */
static __USUALLY_INLINE int
gxio_mpipe_equeue_set_snf_size(gxio_mpipe_equeue_t* equeue, size_t size)
{
  int blks = (size + 127) / 128;
  return gxio_mpipe_config_edma_ring_blks(equeue->context, equeue->ering,
                                          blks + 1, blks, 1);
}

/** Enum codes for edma priority supported by mPIPE. */
typedef enum {
  /** Lowest priority level. */
  GXIO_MPIPE_EDMA_PRIORITY_LOW =
  MPIPE_EDMA_RG_INIT_DAT_MAP__PRIORITY_LVL_VAL_LOW,
  /** Medium priority level. */
  GXIO_MPIPE_EDMA_PRIORITY_MED =
  MPIPE_EDMA_RG_INIT_DAT_MAP__PRIORITY_LVL_VAL_MED,
  /** High priority level. */
  GXIO_MPIPE_EDMA_PRIORITY_HIGH =
  MPIPE_EDMA_RG_INIT_DAT_MAP__PRIORITY_LVL_VAL_HIGH
} gxio_mpipe_equeue_priority_enum_t;

/** Set the priority level to an eMDA ring.
    Rings with a higher priority level will be chosen over rings with a lower
    priority level. Within each priority level, the selection is round-robin.

 * @param equeue An egress queue to set priority level.
 * @param priority A priority level.
 * @return 0 on success, negative on failure.
 */
extern int
gxio_mpipe_equeue_set_priority(
  gxio_mpipe_equeue_t *equeue,
  gxio_mpipe_equeue_priority_enum_t priority);

/** Retrieve the priority level of an eDMA ring.
 * @param equeue An egress queue to get priority level.
 * @return priority level, negative on failure.
 */
extern int
gxio_mpipe_equeue_get_priority(gxio_mpipe_equeue_t *equeue);

/** Retrieve the number of buffers currently in a buffer stack.
 *  Note: This will not include up to 64 buffers in hardware stack. So the
 *  uncertainty is up to 64. If user wants to figure out exactly how many
 *  buffers in a stack, one debug purpose approach would be stopping the
 *  app's operation then pop out and count buffers until it was empty.
 * @param context An initialized mPIPE context.
 * @param stack The buffer stack index.
 */
extern int
gxio_mpipe_get_buffer_count(gxio_mpipe_context_t* context,
                            unsigned int stack);

/** @} */

/******************************************************************
 *                        Link Management                         *
 ******************************************************************/

/**
 * @addtogroup gxio_mpipe_link
 * @{
 *
 * Functions for manipulating and sensing the state and configuration
 * of physical network links.
 *
 * @section gxio_mpipe_link_perm Link Permissions
 *
 * Opening a link (with gxio_mpipe_link_open()) requests a set of link
 * permissions, which control what may be done with the link, and potentially
 * what permissions may be granted to other processes.
 *
 * Data permission allows the process to receive packets from the link by
 * specifying the link's channel number in mPIPE packet distribution rules,
 * and to send packets to the link by using the link's channel number as
 * the target for an eDMA ring.
 *
 * Stats permission allows the process to retrieve link attributes (such as
 * the speeds it is capable of running at, or whether it is currently up), and
 * to read and write certain statistics-related registers in the link's MAC.
 *
 * Control permission allows the process to retrieve and modify link attributes
 * (so that it may, for example, bring the link up and take it down), and
 * read and write many registers in the link's MAC and PHY.
 *
 * Any permission may be requested as shared, which allows other processes
 * to also request shared permission, or exclusive, which prevents other
 * processes from requesting it.  In keeping with GXIO's typical usage in
 * an embedded environment, the defaults for all permissions are shared.
 *
 * Permissions are granted on a first-come, first-served basis, so if two
 * applications request an exclusive permission on the same link, the one
 * to run first will win.  Note, however, that some system components, like
 * the kernel Ethernet driver, may get an opportunity to open links before
 * any applications run.
 *
 * @section gxio_mpipe_link_names Link Names
 *
 * Link names are of the form gbe<em>number</em> (for Gigabit Ethernet),
 * xgbe<em>number</em> (for 10 Gigabit Ethernet), loop<em>number</em> (for
 * internal mPIPE loopback), or ilk<em>number</em>/<em>channel</em>
 * (for Interlaken links); for instance, gbe0, xgbe1, loop3, and
 * ilk0/12 are all possible link names.  The correspondence between
 * the link name and an mPIPE instance number or mPIPE channel number is
 * system-dependent; all links will not exist on all systems, and the set
 * of numbers used for a particular link type may not start at zero and may
 * not be contiguous.  Use gxio_mpipe_link_enumerate() to retrieve the set of
 * links which exist on a system, and always use gxio_mpipe_link_instance()
 * to determine which mPIPE controls a particular link.
 *
 * Note that in some cases, links may share hardware, such as PHYs, or
 * internal mPIPE buffers; in these cases, only one of the links may be
 * opened at a time.  This is especially common with xgbe and gbe ports,
 * since each xgbe port uses 4 SERDES lanes, each of which may also be
 * configured as one gbe port.
 *
 * @section gxio_mpipe_link_states Link States
 *
 * The mPIPE link management model revolves around three different states,
 * which are maintained for each link:
 *
 * 1. The <em>current</em> link state: is the link up now, and if so, at
 *    what speed?
 *
 * 2. The <em>desired</em> link state: what do we want the link state to be?
 *    The system is always working to make this state the current state;
 *    thus, if the desired state is up, and the link is down, we'll be
 *    constantly trying to bring it up, automatically.
 *
 * 3. The <em>possible</em> link state: what speeds are valid for this
 *    particular link?  Or, in other words, what are the capabilities of
 *    the link hardware?
 *
 * These link states are not, strictly speaking, related to application
 * state; they may be manipulated at any time, whether or not the link
 * is currently being used for data transfer.  However, for convenience,
 * gxio_mpipe_link_open() and gxio_mpipe_link_close() (or application exit)
 * can affect the link state.  These implicit link management operations
 * may be modified or disabled by the use of link open flags.
 *
 * From an application, you can use gxio_mpipe_link_get_attr()
 * and gxio_mpipe_link_set_attr() to manipulate the link states.
 * gxio_mpipe_link_get_attr() with ::GXIO_MPIPE_LINK_POSSIBLE_STATE
 * gets you the possible link state.  gxio_mpipe_link_get_attr() with
 * ::GXIO_MPIPE_LINK_CURRENT_STATE gets you the current link state.
 * Finally, gxio_mpipe_link_set_attr() and gxio_mpipe_link_get_attr()
 * with ::GXIO_MPIPE_LINK_DESIRED_STATE allow you to modify or retrieve
 * the desired link state.
 *
 * If you want to manage a link from a part of your application which isn't
 * involved in packet processing, you can use the ::GXIO_MPIPE_LINK_NO_DATA
 * flag on a gxio_mpipe_link_open() call.  This opens the link, but does
 * not request data permission, so it does not conflict with any exclusive
 * data permissions which may be held by other processes.  You can then can
 * use gxio_mpipe_link_get_attr() and gxio_mpipe_link_set_attr() on this link
 * object to bring up or take down the link.
 *
 * Some links support link state bits which support various loopback
 * modes. ::GXIO_MPIPE_LINK_LOOP_MAC tests datapaths within the Tile
 * Processor itself; ::GXIO_MPIPE_LINK_LOOP_PHY tests the datapath between
 * the Tile Processor and the external physical layer interface chip; and
 * ::GXIO_MPIPE_LINK_LOOP_EXT tests the entire network datapath with the
 * aid of an external loopback connector.  In addition to enabling hardware
 * testing, such configuration can be useful for software testing, as well.
 *
 * When LOOP_MAC or LOOP_PHY is enabled, packets transmitted on a channel
 * will be received by that channel, instead of being emitted on the
 * physical link, and packets received on the physical link will be ignored.
 * Other than that, all standard GXIO operations work as you might expect.
 * Note that loopback operation requires that the link be brought up using
 * one or more of the GXIO_MPIPE_LINK_SPEED_xxx link state bits.
 *
 * Those familiar with previous versions of the MDE on TILEPro hardware
 * will notice significant similarities between the NetIO link management
 * model and the mPIPE link management model.  However, the NetIO model
 * was developed in stages, and some of its features -- for instance,
 * the default setting of certain flags -- were shaped by the need to be
 * compatible with previous versions of NetIO.  Since the features provided
 * by the mPIPE hardware and the mPIPE GXIO library are significantly
 * different than those provided by NetIO, in some cases, we have made
 * different choices in the mPIPE link management API.  Thus, please read
 * this documentation carefully before assuming that mPIPE link management
 * operations are exactly equivalent to their NetIO counterparts.
 */

/** An object used to manage mPIPE link state and resources. */
typedef struct {
  /** The overall mPIPE context. */
  gxio_mpipe_context_t* context;

  /** The channel number used by this link. */
  uint8_t channel;

  /** The MAC index used by this link. */
  uint8_t mac;
} gxio_mpipe_link_t;


/** Translate a link name to the instance number of the mPIPE shim which is
 *  connected to that link.  This call does not verify whether the link is
 *  currently available, and does not reserve any link resources;
 *  gxio_mpipe_link_open() must be called to perform those functions.
 *
 *  Typically applications will call this function to translate a link name
 *  to an mPIPE instance number; call gxio_mpipe_init(), passing it that
 *  instance number, to initialize the mPIPE shim; and then call
 *  gxio_mpipe_link_open(), passing it the same link name plus the mPIPE
 *  context, to configure the link.
 *
 * @param link_name Name of the link; see @ref gxio_mpipe_link_names.
 * @return The mPIPE instance number which is associated with the named
 *  link, or a negative error code (::GXIO_ERR_NO_DEVICE) if the link does
 *  not exist.
 */
extern int
gxio_mpipe_link_instance(const char* link_name);


/** Retrieve one of this system's legal link names.
 *
 * @param index Link name index.  If a system supports N legal link names,
 *  then indices between 0 and N - 1, inclusive, each correspond to one of
 *  those names.  Thus, to retrieve all of a system's legal link names,
 *  call this function in a loop, starting with an index of zero, and
 *  incrementing it once per iteration until a negative value is returned.
 * @param link_name Pointer to the buffer which will receive the retrieved
 *  link name.  The buffer should contain space for at least
 *  ::GXIO_MPIPE_LINK_NAME_LEN bytes; the returned name, including the
 *  terminating null byte, will be no longer than that.
 * @return Zero if a link name was successfully retrieved, or a negative
 *  error code is there is no valid link with that index.
 */
extern int
gxio_mpipe_link_enumerate(int index, char* link_name);


/** Retrieve one of this system's legal link names, and its MAC address.
 *
 * @param index Link name index.  If a system supports N legal link names,
 *  then indices between 0 and N - 1, inclusive, each correspond to one of
 *  those names.  Thus, to retrieve all of a system's legal link names,
 *  call this function in a loop, starting with an index of zero, and
 *  incrementing it once per iteration until a negative value is returned.
 * @param link_name Pointer to the buffer which will receive the retrieved
 *  link name.  The buffer should contain space for at least
 *  ::GXIO_MPIPE_LINK_NAME_LEN bytes; the returned name, including the
 *  terminating null byte, will be no longer than that.
 * @param mac_addr Pointer to the buffer which will receive the retrieved
 *  MAC address.  The buffer should contain space for at least 6 bytes.
 * @return Zero if a link name was successfully retrieved, or a negative
 *  error code is there is no valid link with that index.
 */
extern int
gxio_mpipe_link_enumerate_mac(int index, char* link_name, uint8_t* mac_addr);


/** Open an mPIPE link.
 *
 *  A link must be opened before it may be used to send or receive packets,
 *  and before its state may be examined or changed.  Depending upon the
 *  link's intended use, one or more link permissions may be requested via
 *  the flags parameter; see @ref gxio_mpipe_link_perm.  In addition, flags
 *  may request that the link's state be modified at open time.  See @ref
 *  gxio_mpipe_link_states and @ref gxio_mpipe_link_open_flags for more detail.
 *
 * @param link A link state object, which will be initialized if this
 *  function completes successfully.
 * @param context An initialized mPIPE context.
 * @param link_name Name of the link.
 * @param flags Zero or more @ref gxio_mpipe_link_open_flags, ORed together.
 * @return 0 if the link was successfully opened, or a negative error code.
 *
 */
extern int
gxio_mpipe_link_open(gxio_mpipe_link_t* link, gxio_mpipe_context_t* context,
                     const char* link_name, unsigned int flags);

/** Close an mPIPE link.
 *
 *  Closing a link makes it available for use by other processes.  Once
 *  a link has been closed, packets may no longer be sent on or received
 *  from the link, and its state may not be examined or changed.
 *
 * @param link A link state object, which will no longer be initialized
 *  if this function completes successfully.
 * @return 0 if the link was successfully closed, or a negative error code.
 *
 */
extern int
gxio_mpipe_link_close(gxio_mpipe_link_t* link);


/** Return a link's channel number.
 *
 * @param link A properly initialized link state object.
 * @return The channel number for the link.
 */
static __USUALLY_INLINE int
gxio_mpipe_link_channel(gxio_mpipe_link_t* link)
{
  return link->channel;
}


/** Set a link attribute.
 *
 * @param link A properly initialized link state object.
 * @param attr An attribute from the set of @ref gxio_mpipe_link_attrs.
 * @param val New value of the attribute.
 * @return 0 if the attribute was successfully set, or a negative error
 *  code.
 */
extern int
gxio_mpipe_link_set_attr(gxio_mpipe_link_t* link, uint32_t attr, int64_t val);


/** Read an MDIO register in a link's PHY.  Accessing MDIO registers
 *  requires detailed knowledge of PHY operation and may interfere with the
 *  operation of the system's link managment code; it is not recommended for
 *  most applications.
 *
 * @param link A properly initialized link state object.
 * @param dev Address of a device within the PHY.  If this value is less
 *  than zero, the PHY is treated as an IEEE 802.3 clause 22 PHY, which
 *  does not use a device address; otherwise, it is treated as a clause
 *  45 PHY, which does.
 * @param addr Address of a register within the PHY.  For a clause 22 PHY,
 *  this is a 5-bit value; for a clause 45 PHY, a 16-bit value.
 * @return The 16-bit value of the PHY register if the read was successful,
 *  or a negative error code.
 */
extern int
gxio_mpipe_link_mdio_rd(gxio_mpipe_link_t* link, int dev, int addr);


/** Write an MDIO register in a link's PHY.  Accessing MDIO registers
 *  requires detailed knowledge of PHY operation and may interfere with the
 *  operation of the system's link managment code; it is not recommended for
 *  most applications.
 *
 * @param link A properly initialized link state object.
 * @param dev Address of a device within the PHY.  If this value is less
 *  than zero, the PHY is treated as an IEEE 802.3 clause 22 PHY, which
 *  does not use a device address; otherwise, it is treated as a clause
 *  45 PHY, which does.
 * @param addr Address of a register within the PHY.  For a clause 22 PHY,
 *  this is a 5-bit value; for a clause 45 PHY, a 16-bit value.
 * @param val 16-bit value to be written to the register.
 * @return Zero if the write was successful, or a negative error code.
 */
extern int
gxio_mpipe_link_mdio_wr(gxio_mpipe_link_t* link, int dev, int addr,
                        uint16_t val);


/** Read an MDIO register.  Accessing MDIO registers requires detailed
 * knowledge of PHY operation and may interfere with the operation of the
 * system's link managment code; it is not recommended for most applications.
 * This interface is meant to be used only when there is a need to access PHY
 * devices that are not associated with the link in question.  Normal usage
 * should continue to use gxio_mpipe_link_mdio_rd().
 *
 * @param link A properly initialized link state object.
 * @param phy PHY address.  Allowed values here include the link's
 *  predefined PHY, as well as any other unused PHY on the same bus.
 * @param dev Address of a device within the PHY.  If this value is less
 *  than zero, the PHY is treated as an IEEE 802.3 clause 22 PHY, which
 *  does not use a device address; otherwise, it is treated as a clause
 *  45 PHY, which does.
 * @param addr Address of a register within the PHY.  For a clause 22 PHY,
 *  this is a 5-bit value; for a clause 45 PHY, a 16-bit value.
 * @return The 16-bit value of the PHY register if the read was successful,
 *  or a negative error code.
 */
extern int
gxio_mpipe_link_mdio_rd_ex(gxio_mpipe_link_t* link, int phy, int dev, int addr);


/** Write an MDIO register.  Accessing MDIO registers requires detailed
 * knowledge of PHY operation and may interfere with the operation of the
 * system's link managment code; it is not recommended for most applications.
 * This interface is meant to be used only when there is a need to access PHY
 * devices that are not associated with the link in question.  Normal usage
 * should continue to use gxio_mpipe_link_mdio_wr().
 *
 * @param link A properly initialized link state object.
 * @param phy PHY address.  Allowed values here include the link's
 *  predefined PHY, as well as any other unused PHY on the same bus.
 * @param dev Address of a device within the PHY.  If this value is less
 *  than zero, the PHY is treated as an IEEE 802.3 clause 22 PHY, which
 *  does not use a device address; otherwise, it is treated as a clause
 *  45 PHY, which does.
 * @param addr Address of a register within the PHY.  For a clause 22 PHY,
 *  this is a 5-bit value; for a clause 45 PHY, a 16-bit value.
 * @param val 16-bit value to be written to the register.
 * @return Zero if the write was successful, or a negative error code.
 */
extern int
gxio_mpipe_link_mdio_wr_ex(gxio_mpipe_link_t* link, int phy, int dev, int addr,
			   uint16_t val);


/** Get a link attribute.
 *
 * @param link A properly initialized link state object.
 * @param attr An attribute from the set of @ref gxio_mpipe_link_attrs.
 * @return The value of the link attribute, or a negative error code.
 */
extern int64_t
gxio_mpipe_link_get_attr(gxio_mpipe_link_t* link, uint32_t attr);


/** Get a long link attribute.
 *
 * @param link A properly initialized link state object.
 * @param attr An attribute from the set of @ref gxio_mpipe_link_lattrs.
 * @param offset Offset within the long attribute at which to start the
 *  transfer.
 * @param buf buffer which will receive the bytes read from the long
 *  attribute.
 * @param length Number of bytes to read from the long attribute.
 * @return The number of bytes that were available to be transferred at the
 *  given offset, or a negative error code.  Note that this number of bytes
 *  may be more, or less, than the number of bytes that were specified in
 *  length; in particular, it is expected that some applications may call
 *  this routine with a length of 0 to determine how large a buffer to
 *  allocate for a subsequent attempt.
 */
extern int64_t
gxio_mpipe_link_get_lattr(gxio_mpipe_link_t* link, uint32_t attr,
                          size_t offset, void* buf, size_t length);



/** Read a register in a link's MAC.
 *
 * @param link A properly initialized link state object.
 * @param addr Address of a register within the MAC.  This is the offset
 *  within the MAC region of the shim's address space, as obtained from
 *  the definitions in a MAC-specific header file like <arch/mpipe_gbe.h>.
 *  Note that some registers defined in those files may not be accessible
 *  using this interface.
 * @return The 32-bit value of the MAC register if the read was successful,
 *  or a negative error code.
 */
extern int64_t
gxio_mpipe_link_mac_rd(gxio_mpipe_link_t* link, int addr);


/** Write an MDIO register in a link's MAC.  Modifying MAC registers
 *  requires detailed knowledge of MAC operation and may interfere with the
 *  operation of the system's link managment code; it is not recommended for
 *  most applications.
 *
 * @param link A properly initialized link state object.
 * @param addr Address of a register within the MAC.  This is the offset
 *  within the MAC region of the shim's address space, as obtained from
 *  the definitions in a MAC-specific header file like <arch/mpipe_gbe.h>.
 *  Note that some registers defined in those files may not be accessible
 *  using this interface.
 * @param val 32-bit value to be written to the register.
 * @return Zero if the write was successful, or a negative error code.
 */
extern int
gxio_mpipe_link_mac_wr(gxio_mpipe_link_t* link, int addr, uint32_t val);


/** Wait for a link to become ready.
 *
 *  This function waits until the specified link is up, at any of its
 *  possible speeds, then returns.  The link must have been opened with
 *  stats permission.  If an optional timeout is specified, the function
 *  will also return if the specified time passes and the link is still
 *  down.  Note that the fact that this function returned is not a guarantee
 *  that the link is still up, since it might have come up and then quickly
 *  gone down.
 *
 * @param link A properly initialized link state object.
 * @param timeout Amount of time to wait for the link to become ready, in
 *  milliseconds.  If this value is zero, the function will not wait; if
 *  If it is less than zero, there will be no timeout (the function will
 *  block until the link comes up).
 * @return 0 if the link became ready, or ::GXIO_ERR_TIMEOUT if the timeout
 *  expired without the link coming up.
 */
extern int
gxio_mpipe_link_wait(gxio_mpipe_link_t* link, int timeout);

/** Get a file descriptor which may be polled to determine link state
 *  changes.
 *
 *  Before the returned file descriptor can be used with poll() or
 *  select(), it must first be armed with gxio_mpipe_link_arm_pollfd().  See
 *  the description of that function for the precise semantics of the arming
 *  operation.  Note that the application must not do any read() or write()
 *  operations on the returned file descriptor.
 *
 *  If an mPIPE context is destroyed, any file descriptor returned from
 *  this function specifying that context is no longer valid.  If the file
 *  descriptor is no longer needed before destruction of the mPIPE context,
 *  it may be explicitly closed.
 *
 *  See also gxio_mpipe_link_wait() and the ::GXIO_MPIPE_LINK_WAIT flag
 *  to gxio_mpipe_link_open().
 *
 * @param context An initialized mPIPE context.
 * @return If the call was successful, a non-negative file descriptor;
 *  otherwise, a negative error code.
 */
extern int
gxio_mpipe_link_get_pollfd(gxio_mpipe_context_t* context);

/** Get an mPIPE sequence number.
 *
 * The mPIPE shim provides 4160 16-bit sequence numbers, one of which can
 * be incremented for each ingress packet under the control of the
 * classifier program.  The default classifier program does not currently
 * use this feature.  Note that when the sequence number feature is enabled,
 * the number is provided with every packet as part of the packet descriptor,
 * so it is not normally necessary to read it using this function.
 *
 * @param context An initialized mPIPE context.
 * @param index Sequence number index.
 * @return A non-negative sequence number, or a negative error code.
 */
extern int
gxio_mpipe_get_sqn(gxio_mpipe_context_t* context, int index);


/** Configure mPIPE configurable packet drop/performance counters.
 *
 * mPipe has 3 global configurable HW counters:
 * MPIPE_LBL_STAT_CTR, MPIPE_IPKT_STAT_CTR and MPIPE_IDMA_STAT_CTR
 * corresponding to command = GXIO_MPIPE_STAT_CONFIG_COMM_LBL(0),
 * GXIO_MPIPE_STAT_CONFIG_COMM_IPKT(1), GXIO_MPIPE_STAT_CONFIG_COMM_IDMA(2).
 * Each of them can be programmed to collect certain HW events. By default,
 * 3 counters are configured with val = 0, which will count all packet drops
 * due to any known reason. Reprogram a counter with a val(!=0) will cause
 * mPIPE to increment the counter due to a specific reason, for example,
 *   command = GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
 *   val = GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_PKTS
 * will reprogram the counter MPIPE_IPKT_STAT_CTR for packet drop due to iPkt
 * buffer not available.
 *
 * Note: These counters globally apply to all links in a mPipe. Normally,
 * all 3 counters are in default configuration (val = 0), HV maintains 3
 * incremental default counters internally, user can retrieve them by calling
 * API: gxio_mpipe_get_stats(gxio_mpipe_context_t* context,
 *                           gxio_mpipe_stats_t* result);
 *
 *  mPIPE statistics structure. These counters include all relevant
 *  events occurring on all links within the mPIPE shim.
 *  typedef struct
 *  {
 *   // Number of ingress packets dropped for any reason.
 *   uint64_t ingress_drops;
 *   // Number of ingress packets dropped because a buffer stack was empty.
 *   uint64_t ingress_drops_no_buf;
 *   // Number of ingress packets dropped or truncated due to lack of space in
 *   // the iPkt buffer.
 *   uint64_t ingress_drops_ipkt;
 *   // Number of ingress packets dropped by the classifier or load balancer
 *   uint64_t ingress_drops_cls_lb;
 *   // Total number of ingress packets.
 *   uint64_t ingress_packets;
 *   //Total number of egress packets.
 *   uint64_t egress_packets;
 *   // Total number of ingress bytes.
 *   uint64_t ingress_bytes;
 *   // Total number of egress bytes.
 *   uint64_t egress_bytes;
 *  }
 *  gxio_mpipe_stats_t;
 *
 * In the struct gxio_mpipe_stats_t, field ingress_drop_cls_lb is associated
 * with HW counter MPIPE_LBL_STAT_CTR, ingress_drops_ipkt is associated with
 * HW counter MPIPE_IPKT_STAT_CTR and ingress_drops_no_buf is with HW counter
 * MPIPE_IDMA_STAT_CTR. Once API gxio_mpipe_config_state is called to
 * reconfigure a HW counter, the corresponding field in gxio_mpipe_stats_t is
 * switched to the new configuration. Subsequent stats retrieval by
 * API: gxio_mpipe_config_stats will return new configured counters until
 * the configuration is disabled. Also, once a HW counter is reconfigured,
 * no new configuration is allowed until the previous is done.
 *
 * @param context an initialized mPIPE context.
 * @param command a defined in _gxio_mpipe_stat_config
 * @param val value associated with command.
 * @return 0 if the configuration was successfully set, or a negative error
 *  code.
 */
extern int
gxio_mpipe_config_stats(gxio_mpipe_context_t* context,
                        uint32_t command, uint64_t val);

/** Get mPIPE statistics.
 *
 * @param context An initialized mPIPE context.
 * @param result Pointer to a structure to which the statistics will be
 *  written.
 * @return Zero if statistics were successfully retrieved, or a negative
 *  error code.
 */
extern int
gxio_mpipe_get_stats(gxio_mpipe_context_t* context,
                     gxio_mpipe_stats_t* result);


/** Get an mPIPE counter.
 *
 * The mPIPE shim provides 32 48-bit counters, up to two of which can be
 * incremented for each ingress packet under the control of the classifier
 * program.  The default classifier program does not currently use these
 * counters.
 *
 * @param context An initialized mPIPE context.
 * @param index Counter index.
 * @param result Pointer to a variable to which the retrieved counter
 *  will be written.
 * @return Zero if the counter was successfully retrieved, or a negative
 *  error code.
 */
extern int
gxio_mpipe_get_counter(gxio_mpipe_context_t* context,
                       unsigned int index, uint64_t* result);



/** Arm a pollable file descriptor.
 *
 *  Calling this function saves a snapshot of the current up or down state
 *  of all links currently opened with stats permission on a particular
 *  mPIPE context.  Once the current state of any of those links is
 *  different from that saved in the snapshot, the file descriptor passed
 *  to this function will appear to be readable, as determined by the poll()
 *  or select() functions.
 *
 *  After that first status change has occurred, the file descriptor will
 *  remain readable until this function is called again; in other words,
 *  there is no more than one transition of readability, from unreadable
 *  to readable, per call to this function.
 *
 *  Applications may request that an asynchronous signal be sent upon link
 *  status change by using fcntl() with the F_SETOWN command on the
 *  pollable file descriptor to set a target process ID or process group,
 *  and then using fcntl() with the F_SETFL command to set the O_ASYNC flag
 *  on that file descriptor.  The requested signal is sent on the first
 *  transition of the file descriptor from unreadable to readable.  No more
 *  than one signal will be sent per call to this function.
 *
 *  Note that link state changes may happen while this function is
 *  executing, and that multiple changes may happen simultaneously.
 *  Applications waiting for a specific link state should check that state
 *  after calling this function, since it may have been attained before the
 *  link state snapshot was taken; in that case there would be no
 *  readability transition on the file descriptor, and no signal.
 *
 *  See also gxio_mpipe_link_wait() and the ::GXIO_MPIPE_LINK_WAIT flag
 *  to gxio_mpipe_link_open().
 *
 * @param context An initialized mPIPE context.
 * @param fd File descriptor obtained from gxio_mpipe_link_get_pollfd().
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_mpipe_link_arm_pollfd(gxio_mpipe_context_t* context, int fd);



/** @} */

/******************************************************************
 *                             Timestamp                          *
 ******************************************************************/

/**
 * @addtogroup gxio_mpipe_timestamp
 * @{
 *
 * Functions for manipulating the mPIPE timestamping clock.
 * These functions can be used to implement the hardware assisted PTP,
 * synchronize the mPIPE timestamping clock with other
 * clock sources, such as the system clock, etc.
 *
 * @section gxio_mpipe_1588 Timestamping and IEEE-1588
 *
 * The IEEE-1588 specification is a standard for precision time synchronization
 * in local area networks. IEEE-1588 defines a protocol named Precision Time
 * Protocol (PTP) for time synchronizing with submicrosecond accuracy.
 *
 * If timestamp insertion is enabled, mPIPE inserts the timestamp captured
 * from the mPIPE timestamping clock when a packet is received from the MAC;
 * this timestamp is more precise than the software timestamp.
 *
 */

/** Get the current timestamp of the mPIPE.
 *
 * @param context An initialized mPIPE context.
 * @param ts A timespec structure to store the current clock.
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_mpipe_get_timestamp(gxio_mpipe_context_t* context,
                         struct timespec *ts);

/** Set the timestamp of mPIPE.
 *
 * @param context An initialized mPIPE context.
 * @param ts A timespec structure to store the requested clock.
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_mpipe_set_timestamp(gxio_mpipe_context_t* context,
                         const struct timespec *ts);

/** Adjust the timestamp of mPIPE.
 *
 * @param context An initialized mPIPE context.
 * @param delta A signed time offset to adjust, in nanoseconds.
 * The absolute value of this parameter must be less than or
 * equal to 1000000000.
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_mpipe_adjust_timestamp(gxio_mpipe_context_t* context,
                            int64_t delta);

/** Adjust the mPIPE timestamp clock frequency.
 *
 * @param context An initialized mPIPE context.
 * @param ppb A 32-bit signed PPB (Parts Per Billion) value to adjust.
 * The value of ppb should be between -65536 and 32768.  Any values less then
 * -65536 will be truncated to -65536; any values greater then 32768 will
 * be truncated to 32768, as the hardware does not allow to change the
 * frequency to be more then 2 times slower/faster.
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_mpipe_adjust_timestamp_freq(gxio_mpipe_context_t* context,
                                 int32_t ppb);

/** @} */

__END_DECLS


#endif /* !_GXIO_MPIPE_H_ */
