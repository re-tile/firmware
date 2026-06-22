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
 * Support for sending messages to the hypervisor.
 */

#ifndef _SYS_BME_HV_MSG_H
#define _SYS_BME_HV_MSG_H

#include <stdint.h>

#include <arch/idn.h>

#include <bme/types.h>

/** Send a request, and return the reply.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number
 *        of words.
 * @param replybuf Buffer for the reply; must be word-aligned.  Can be NULL in
 *        which case the reply is not saved, although its length will still be
 *        checked (see below).
 * @param replybuflen Length of the reply buffer, in bytes; must be an even
 *        number of words.  If replybuflen < 0, then the length of the reply
 *        must be less than or equal to abs(replybuflen).  If replybuflen > 0,
 *        then the length of the reply must exactly equal replybuflen.
 * @param replylenp The length of the received reply, in bytes, is written
 *        here.  Can be NULL in which case the length is not returned,
 *        although it is still checked (see above).
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
#define _bme_send_receive(dest, msgtype, msg, msglen, replybuf, replybuflen, \
                          replylenp, flags) \
        _bme_send_receive_var(dest, msgtype, msg, msglen, NULL, 0, replybuf, \
                              replybuflen, replylenp, flags)

/** Send a request with a variable-length component, and return the reply.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number
 *        of words.
 * @param buf Variable-length data; need not be word-aligned.
 * @param buflen Length of variable-length data.
 * @param replybuf Buffer for the reply; must be word-aligned.  Can be NULL in
 *        which case the reply is not saved, although its length will still be
 *        checked (see below).
 * @param replybuflen Length of the reply buffer, in bytes; must be an even
 *        number of words.  If replybuflen < 0, then the length of the reply
 *        must be less than or equal to abs(replybuflen).  If replybuflen > 0,
 *        then the length of the reply must exactly equal replybuflen.
 * @param replylenp The length of the received reply, in bytes, is written
 *        here.  Can be NULL in which case the length is not returned,
 *        although it is still checked (see above).
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
int _bme_send_receive_var(pos_t dest,
                          uint32_t msgtype, void* msg, int msglen,
                          void* buf, int buflen,
                          void* replybuf, int replybuflen, int* replylenp,
                          uint32_t flags);

#define MSG_FLG_XMITFAIL  0x1  /**< Return error code on transmit failure (due
                                    to a busy IDN), don't panic */

#endif /* _SYS_BME_HV_MSG_H */
