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
 * Routines related to client messaging.
 */

#include <stdio.h>
#include <string.h>


#include <hv/hypervisor.h>
#include "sys/libc/include/util.h"

#include "client_msg.h"
#include "client_msg_shared.h"
#include "client_obj.h"
#include "debug.h"
#include "downcall.h"
#include "fault.h"
#include "hv.h"
#include "idn.h"
#include "mapping.h"
#include "msg.h"
#include "types.h"

// State for client messaging subsystem.

struct client_msg_area* cmap;     ///< The client's state
VA client_cmap;                   ///< Client's version of cmap
int client_msg_intr_armed = 1;    ///< True if we interrupt on next message

/** Handle the hv_register_message_state() syscall.
 * @param msgstate Message state object.
 */
int
syscall_register_message_state(HV_MsgState* msgstate)
{
  SYSCALL_TRACE("register_message_state(state=%p)\n", msgstate);

  if (!cmap)
  {
    cmap = client_shared_alloc(sizeof (*cmap), 0, 0, 1, &client_cmap);
    if (!cmap)
      panic("can't alloc memory for supervisor messaging");
  }

  cmap->head = cmap->tail = 0;

  //
  // Initialize client's state object.
  //
  ON_FAULT_RETURN_EFAULT(msgstate, sizeof (*msgstate));

  msgstate->opaque[0] = client_cmap;
  msgstate->opaque[1] = CLIENT_MSG_MASK;

  FAULT_END();

  return (0);
}


int
deliver_local_message(uint32_t source, void* buf, int buflen)
{
  if (!cmap)
    return (1);

  int new_tail = (cmap->tail + sizeof (cmap->msgs[0])) & CLIENT_MSG_MASK;

  if (new_tail == cmap->head)
    return (1);

  // Note that the client can write the tail pointer, so we mask it to force
  // it to be in-range and properly aligned.  It's a byte offset, so we need
  // to divide it down before using it as an array index.
  int old_tail = cmap->tail & CLIENT_MSG_MASK;
  struct client_msg* msgp = &cmap->msgs[old_tail / sizeof (cmap->msgs[0])];
  cmap->tail = new_tail;

  memcpy(msgp->msg, buf, buflen);
  msgp->len = buflen;
  msgp->source = source;

  if (client_msg_intr_armed)
  {
    client_msg_intr_armed = 0;
    downcall_message();
  }

  return (0);
}


int
send_sv_message(pos_t tile, uint32_t source, void* buf, int buflen)
{
  if (tile.word == my_pos.word)
    return (deliver_local_message(source, buf, buflen));

  struct hv_msg_sv sv_msg =
  {
    .len = buflen,
    .source = source,
  };
  unsigned long ackbuf;

  send_receive_var(tile, HV_TAG_MSG_SV, &sv_msg, sizeof (sv_msg),
                   buf, buflen, &ackbuf, sizeof (ackbuf), NULL,
                   MSG_FLG_SHORTREPLY);

  return (ackbuf);
}


/** Handle the hv_send_message() syscall.
 * @param recips List of recipients.
 * @param nrecip Number of recipients.
 * @param buf Address of message data.
 * @param buflen Length of message data.
 * @return Number of messages sent, or a hypervisor error code.
 */
int
syscall_send_message(HV_Recipient *recips, int nrecip, char* buf, int buflen)
{
  SYSCALL_TRACE("send_message(recips=%p, nrecip=%d, buf=%p, len=%d)\n",
                recips, nrecip, buf, buflen);

  // FIXME: we ought to do this in parallel, instead of blocking on the
  // ACKs before sending messages to subsequent tiles.

  if (buflen <= 0 || buflen > HV_MAX_MESSAGE_SIZE || nrecip > HV_TILES - 1)
    return (HV_EINVAL);

  int nfatal = 0;
  int nsent = 0;

  char msgbuf[HV_MAX_MESSAGE_SIZE];

  ON_FAULT_RETURN_EFAULT(buf, buflen);
  FAULT_ADD_ADDR(recips, nrecip * sizeof (recips[0]));
  memcpy(msgbuf, buf, buflen);

  struct hv_msg_sv sv_msg =
  {
    .len = buflen,
    .source = HV_MSG_TILE,
  };

  for (int i = 0; i < nrecip; i++)
  {
    if (recips[i].state != HV_TO_BE_SENT)
      continue;

    Lotar dest_lotar = HV_XY_TO_LOTAR(recips[i].x, recips[i].y);
    Lotar real_lotar;

    if (c2r_lotar(dest_lotar, &real_lotar))
    {
      recips[i].state = HV_BAD_RECIP;
      nfatal++;
      continue;
    }

    // Send message

    unsigned long ackbuf;

    if (real_lotar == my_lotar)
    {
      ackbuf = deliver_local_message(HV_MSG_TILE, msgbuf, buflen);
    }
    else
    {
      pos_t dest_pos = { .bits.x = HV_LOTAR_X(real_lotar),
                         .bits.y = HV_LOTAR_Y(real_lotar) };

      send_receive_var(dest_pos, HV_TAG_MSG_SV, &sv_msg, sizeof (sv_msg),
                       msgbuf, buflen, &ackbuf, sizeof (ackbuf), NULL, 0);
    }

    if (ackbuf == 0)
    {
      recips[i].state = HV_SENT;
      nsent++;
    }
  }

  FAULT_END();

  if (nfatal)
    return (HV_ERECIP);
  else
    return (nsent);
}


/** Handle part of the hv_receive_message() syscall. */
void
syscall_receive_message()
{
  SYSCALL_TRACE("receive_message()\n");

  //
  // All this routine does is tell the hypervisor that the client has drained
  // his buffer, and wants an interrupt when the next message shows up.  It
  // used to actually retrieve the next message, and in the magic hypervisor
  // case, it still does; hence the somewhat nonintuitive name.
  //
  if (cmap->head == cmap->tail)
    client_msg_intr_armed = 1;
  else
  {
    //
    // A message showed up after the client found the buffer empty, so deliver
    // it.
    //
    client_msg_intr_armed = 0;
    downcall_message();
  }
}
