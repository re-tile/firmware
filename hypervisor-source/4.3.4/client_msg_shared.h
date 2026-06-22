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
 * Definition of the client messaging data structures which are shared between
 * the hypervisor and the client.
 */

#ifndef _SYS_HV_CLIENT_MSG_SHARED_H
#define _SYS_HV_CLIENT_MSG_SHARED_H

#include <hv/hypervisor.h>

#include "hv.h"
#include "types.h"

/** Message buffer. */
struct client_msg
{
  int16_t len;                    /**< Length of this message */
  uint16_t source;                /**< Source of this message (HV_MSG_xxx) */
  char msg[HV_MAX_MESSAGE_SIZE];  /**< Message */
};

/** Number of messages queued.  The hypervisor interface guarantees that this
 *  is at least one per remote tile; with our current implementation, it must
 *  be a power of 2.
 */
#define MAX_CLIENT_MSGS   HV_TILES

/** Mask to use when wrapping the head and tail pointers.  The value is
 *  somewhat unusual; when we apply it, in addition to wrapping out-of-range
 *  values back within the range, we want it to force the low bits of the
 *  offset to zeroes, so that the resulting offset is guaranteed to be aligned
 *  properly.  To do this we subtract the size of an entry, rather than just
 *  subtracting 1, when making the mask value.
 */
#define CLIENT_MSG_MASK  ((HV_TILES * sizeof (struct client_msg)) - \
                          sizeof (struct client_msg))

/** Message area. */
struct client_msg_area
{
  int head;                   /**< Offset of first readable message */
  int tail;                   /**< Offset of slot after last readable message */
  struct client_msg
    msgs[MAX_CLIENT_MSGS];    /**< Messages themselves */
};

#endif /* _SYS_HV_CLIENT_MSG_SHARED_H */
