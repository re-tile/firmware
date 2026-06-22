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
 * I/O dynamic network routines.
 */

#ifndef _SYS_HV_IDN_H
#define _SYS_HV_IDN_H

#include <arch/spr.h>

/*
 * IDN usage in the booter and the hypervisor:
 *
 * The three available IDN demultiplexing endpoints are used for different
 * purposes by the booter and hypervisor.  One is used synchronously
 * (i.e., the reader blocks on a read to the appropriate register); the
 * others are used asynchronously (i.e., the reader gets an interrupt
 * when data is available, and then reads it).
 *
 * The booter uses idn0 to receive all of its messages.  This includes
 * replies from I/O shims, messages between the master boot tile and the
 * slave boot tiles, and messages from the master hypervisor tile to the
 * slave boot tiles.  All of these messages are received synchronously.
 * The BOOT_IDN_TAG_0 tag is used for these messages, whose first word
 * denotes their type.
 *
 * The hypervisor uses idn0 for messages which it wants to receive
 * synchronously.  We do this while waiting for acknowledgements from I/O
 * shims (for instance, config read or write responses) and in a limited
 * number of cases, when waiting for acknowledgements from other tiles
 * (for instance, replies to the hypervisor messages which coordinate the
 * initial multi-tile startup).  The HV_IDN_TAG_0 tag is used for these
 * messages, whose first word denotes their type.
 *
 * idn1 is used for asynchronous interrupt messages from high-speed
 * I/O shims.  The HV_IDN_TAG_1 tag is be used for these messages.
 * The first (and perhaps only) word of these messages is in the standard
 * I/O shim format, and the channel number in its low 7 bits denotes the
 * message type.
 *
 * The hypervisor uses the catch-all queue for all post-startup
 * inter-hypervisor messages, including those sent on behalf of supervisors
 * via the hv_send_message() API, as well as for most acknowledgments of
 * those messages.  These messages are received asynchronously.  The tag
 * used for these messages denotes their type.
 *
 * To prevent network deadlock, we obey the following rules when using the
 * IDN:
 *
 * - Messages are never sent to idn0 unless they're acknowledgements which
 *   are expected by the receiver.  The receiver will not do anything
 *   which depends on the IDN between the time it completes sending the
 *   original request and the time it goes to read its acknowledgment
 *   from idn0.  Thus, all messages to idn0 will eventually be sinked.
 *   (The messages used during startup are a degenerate case of this,
 *   as we know the receiver has nothing to do other than drain idn0.)
 *
 * - The other endpoints (the catch-all, and idn1) are interrupt-driven,
 *   and we never do anything which could make the execution or completion
 *   of the interrupt routine dependent upon the IDN.  Specifically,
 *   we never read or write the IDN when other endpoints' interrupts
 *   are disabled; we never write the IDN from an IDN interrupt routine;
 *   and we never block on reading the IDN from an IDN interrupt routine.
 *   We use message-level flow control and static data allocation to ensure
 *   that the IDN interrupt routines can always successfully dequeue any
 *   message from the IDN.  These rules ensure that all messages to idn1
 *   or the catch-all will eventually be sinked.
 *
 * (Note that the tags used while we're in the booter are different from
 * those used while we're in the hypervisor.  This is an attempt to keep
 * one from accidentally receiving data meant for the other; this may
 * not turn out to be very useful.)
 */

/** IDN tag value for the booter, demux 0. */
#define BOOT_IDN_TAG_0 0xFFFFFF00
/** IDN tag value for the booter, demux 1. */
#define BOOT_IDN_TAG_1 0xFFFFFF01

/** IDN tag values for the hypervisor.  TAG_1 must match the device driver
 *  tag for instant interrupts, and the device driver tag for delayed
 *  interrupts must not match any of these tags; this is verified in idn.c.
 */

#define HV_IDN_TAG_0   0x78  /**< Tag used for IDN 0 */
#define HV_IDN_TAG_1   0x79  /**< Tag used for IDN 1 */
#define HV_IDN_TAG_ACK 0x7A  /**< Tag used for 1-word ACKs which go to the
                                  catch-all */

#ifndef __ASSEMBLER__


void init_idn(void);
void enable_idn1_intr(void);
void disable_idn1_intr(void);
void init_idn_dedicated(void);
void init_idn_ca(void);

/** Nonzero if it's unsafe for an interrupt routine to use idn0. */
extern int idn0_busy;

/** Notify interrupt routines that idn0 is in use. */
#define IDN0_SET_BUSY() idn0_busy = 1;

/** Notify interrupt routines that idn0 is free. */
#define IDN0_CLEAR_BUSY() idn0_busy = 0;

/** Is idn0 busy? */
#define IDN0_IS_BUSY() (idn0_busy || __insn_mfspr(SPR_IDN_PENDING))

















#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_IDN_H */
