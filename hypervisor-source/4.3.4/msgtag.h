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
 * Hypervisor messaging definitions.
 */

#ifndef _SYS_HV_MSGTAG_H
#define _SYS_HV_MSGTAG_H

#define HV_MAXMSGWDS    128     /**< Maximum HV message length in words */

/**
 * Hypervisor message tag.
 */
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    uint32_t   len: 7;  /**< Length in words of following message; does not
                             count the automatic tile ID included in requests */
    uint32_t reply: 1;  /**< If 1, this is a reply; otherwise, a request */
    uint32_t  chan:13;  /**< Channel number */
    uint32_t  type:11;  /**< Message type; must be nonzero */
#else   // __BIG_ENDIAN__
    uint32_t  type:11;  /**< Message type; must be nonzero */
    uint32_t  chan:13;  /**< Channel number */
    uint32_t reply: 1;  /**< If 1, this is a reply; otherwise, a request */
    uint32_t   len: 7;  /**< Length in words of following message; does not
                             count the automatic tile ID included in requests */
#endif
  }
  bits;                 /**< Bitfield for set/get */
  uint32_t word;        /**< Packed tag for send/receive */
}
hv_tag;

//
// The hypervisor uses a message priority scheme to prevent message
// deadlock; that is, a set of tiles all of which are awaiting a response
// from another tile in the set before they continue to execute.  (This
// is distinct from the prevention of IODN network deadlock described
// in idn.h.)  The scheme works as follows:
//
// 1. Every intertile request has a priority associated with it.
//
// 2. At any given time, the hypervisor has a "current priority".
//    The current priority is the priority of the intertile request it
//    is currently processing, or the lowest possible priority if it is
//    not currently processing a request.
//
// 3. The hypervisor is only allowed to make intertile requests whose
//    priority level is greater than the current priority level.
//
// 4. The hypervisor is required to process incoming intertile requests
//    which are of a priority greater than its current priority, without
//    waiting for responses to any outstanding requests it might have.
//    In other words, whenever the hypervisor is awaiting a response
//    from another tile, it must allow itself to be interrupted by a
//    higher-priority request.  Note that the priority of the interrupted
//    hypervisor's outstanding request is immaterial.
//
// These rules make it impossible to create a cycle of tiles within the
// graph defined by the "tile X is waiting on a response from tile Y"
// relation.  To demonstrate this, assume that such a cycle does exist.
// There must exist within that cycle a tile whose current priority
// is greater than or equal to the current priority of all the other
// tiles in the cycle; call this T0, and call the current priority of
// a tile P(tile).  Thus, for all tiles Tx within the cycle, P(T0) >=
// P(Tx).  Since it is a part of a cycle, T0 has an outstanding request
// to another tile; call that tile T1, call the request R0, and call the
// priority of the request P(R).  By rule 3, P(R) > P(T0).  Since P(T0) >=
// P(T1), then P(R) > P(T1).  By rule 4, T1 must thus process R without
// waiting for whatever outstanding requests it might have to complete.
// However, then by rule 2, P(T1) >= P(R).  This is a contradiction; thus,
// our assumption that a cycle could exist is incorrect.
//

//
// Messages handled by the hypervisor.  We encode the priority within the
// tag for each message type.  As a special case, driver delayed interrupt
// messages are treated as having priority HV_MSG_PRI_DRVINTR.
//

#define HV_MSG_PRI_CONSOLE          5  /**< Console message */

#define HV_MSG_PRI_SVMSG            4  /**< Supervisor message */

#define HV_MSG_PRI_DRVMSG           3  /**< Driver message */
#define HV_MSG_PRI_DRVINTR          3  /**< Driver interrupt */

#define HV_MSG_PRI_DRVREQ_HI        2  /**< Driver request, high priority */

#define HV_MSG_PRI_DRVREQ           1  /**< Driver request */
#define HV_MSG_PRI_FLUSH            1  /**< Flush remote */
#define HV_MSG_PRI_INIT             1  /**< Initialization (start client etc.)*/

#define HV_MSG_PRI_IDLE             0  /**< Nothing happening */

#define HV_MSG_MAXPRI HV_MSG_PRI_CONSOLE   /**< Highest priority. */
#define HV_MSG_MINPRI HV_MSG_PRI_INIT      /**< Lowest non-idle priority. */

/** Construct tag */
#define HV_MKTAG(num, pri)          (((pri) << 8) | (num))
/** Extract message number from tag */
#define HV_MSGNUM(tag)              ((tag) & 0xFF)
/** Extract priority from tag */
#define HV_PRI(tag)                 ((tag) >> 8)

#endif /* _SYS_HV_MSGTAG_H */
