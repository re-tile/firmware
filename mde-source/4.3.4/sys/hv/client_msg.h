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
 * Client messaging definitions.
 */

#ifndef _SYS_HV_CLIENT_MSG_H
#define _SYS_HV_CLIENT_MSG_H

#include <hv/hypervisor.h>

#include "types.h"

int syscall_register_message_state(HV_MsgState* msgstate);

/** Deliver a message locally.
 *
 * @param source Source of the message (HV_MSG_xxx).
 * @param buf Message.
 * @param buflen Length of the message.
 * @return Nonzero if the message could not be delivered, otherwise zero.
 */
int deliver_local_message(uint32_t source, void* buf, int buflen);

/** Send a message to a client supervisor.
 *
 * @param tile Destination tile (could be the local tile).
 * @param source Source of the message (HV_MSG_xxx).
 * @param buf Message.
 * @param buflen Length of the message.
 * @return Nonzero if the message could not be delivered, otherwise zero.
 */
int send_sv_message(pos_t tile, uint32_t source, void* buf, int buflen);

int syscall_send_message(HV_Recipient *recips, int nrecip, char* buf,
                         int buflen);

/* Note that with the new client-shared message implementation, where most
 * of the hv_receive_message work is done by the glue code, this syscall's
 * arguments no longer match those of the hypervisor service.
 */
void syscall_receive_message(void);

#endif /* _SYS_HV_CLIENT_MSG_H */
