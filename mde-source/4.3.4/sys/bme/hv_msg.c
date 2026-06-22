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

#include <util.h>

#include <arch/idn.h>
#include <arch/spr.h>

#include "bits.h"
#include "hv_msg.h"

#include "../hv/bme_msg.h"
#include "../hv/msgtag.h"

int
_bme_send_receive_var(pos_t dest, uint32_t msgtype, void* msg, int msglen,
                      void* buf, int buflen,
                      void* replybuf, int replybuflen, int* replylenp,
                      uint32_t flags)
{
  //
  // If we're in the middle of sending another message, we can't start a new
  // one.
  //
  if (__insn_mfspr(SPR_IDN_PENDING) != 0)
    panic("can't send msgtype %#x, message already in progress", msgtype);

  //
  // Send the message.
  //
  int msgwds = B2W_UP(msglen);
  int bufwds = B2W_UP(buflen);

#if 0
  if (msgwds + bufwds + 2 > HV_MAXMSGWDS)
    panic("send message too large; msgwds %d bufwds %d", msgwds, bufwds);
#endif

  hv_tag tag =
  {
    .bits.len = msgwds + bufwds,
    .bits.chan = 0,
    .bits.type = msgtype,
  };

  idn_send(dest.word | (2 + msgwds + bufwds));
  idn_send(tag.word);
  idn_send(__insn_mfspr(SPR_TILE_COORD));

  unsigned long* p = msg;

  for (int len = msgwds; len; len--)
    idn_send(*p++);

  // Send trailing buffer; we check for the common case of it being
  // word-aligned to handle it more efficiently.

  if (((uintptr_t) buf & (sizeof (unsigned long) - 1)) == 0)
  {
    unsigned long* ui_bufp = buf;

    // First handle the full words.

    for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
      idn_send(*ui_bufp++);

    // Now do one last word to handle the residue.

    if (buflen)
      idn_send(*ui_bufp & RMASK(buflen * 8));
  }
  else
  {
    unsigned long outword;
    unsigned char* uc_bufp = buf;

    // First handle the full words.

    for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
    {
      outword = *uc_bufp++;
      for (int i = 1; i < sizeof (unsigned long); i++)
        outword |= *uc_bufp++ << (8 * i);
      idn_send(outword);
    }

    // Now do one last word to handle the residue.

    if (buflen)
    {
      int bitsleft = buflen * 8;

      outword = 0;
      for (int i = 0; i < bitsleft; i += 8)
        outword |= *uc_bufp++ << i;

      idn_send(outword);
    }
  }

  //
  // Now get the reply.
  //
  hv_tag rcv_tag = { .word = idn0_receive() };

  if (rcv_tag.bits.type != msgtype)
    panic("message type mismatch: sent %#x, got %#x", msgtype,
          rcv_tag.bits.type);

  int reply_must_fill_buffer;

  if (replybuflen < 0)
  {
    replybuflen = -replybuflen;
    reply_must_fill_buffer = 0;
  }
  else
    reply_must_fill_buffer = 1;

  int rcv_replylen = W2B(rcv_tag.bits.len);

  if (reply_must_fill_buffer && rcv_replylen != replybuflen)
    panic("reply too short: type %#x, got %d bytes, expected %d bytes",
          msgtype, rcv_replylen, replybuflen);

  if (rcv_replylen > replybuflen)
    panic("reply too long: type %#x, got %d bytes, expected no more than "
          "%d bytes", msgtype, rcv_replylen, replybuflen);

  if (replylenp)
    *replylenp = rcv_replylen;

  uint32_t* ui_replybufp = replybuf;

  for (int len = rcv_tag.bits.len; len; len--)
    *ui_replybufp++ = idn0_receive();

  return (0);
}
