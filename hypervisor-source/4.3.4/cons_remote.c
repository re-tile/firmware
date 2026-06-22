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
 * Output of characters to the console via message to the console tile.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "console.h"
#include "debug.h"
#include "msg.h"


/** Write to the remote console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer; if this is zero, this is a
 *   hypervisor write, otherwise it is a client write.
 * @return Number of characters written.
 */
static int
remote_cons_write(char* s, int len, unsigned int offset, void* private)
{
  struct hv_msg_write_console wr_msg =
  {
    .len = len,
    .client_no = ((uintptr_t) private != 0) ? my_client : -1,
  };

  // Make sure we aren't being asked to write more than will fit in one message.
  assert(len < W2B(HV_MAXMSGWDS - 1) - sizeof (wr_msg));

  unsigned long ackbuf;

  if (send_receive_var(chip_console, HV_TAG_WRITE_CONSOLE, &wr_msg,
                       sizeof (wr_msg), s, len, &ackbuf, sizeof (ackbuf),
                       NULL, MSG_FLG_XMITFAIL))
    return (0);
  else
    return (ackbuf);
}


/** Read from the remote console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param private Private data pointer; if this is zero, this is a
 *   hypervisor write, otherwise it is a client write.
 * @return Number of characters read.
 */
static int
remote_cons_read(char* s, int len, unsigned int offset, void* private)
{
  struct hv_msg_read_console rd_msg =
  {
    .len = len,
    .client_no = ((uintptr_t) private != 0) ? my_client : -1,
  };

  union
  {
    struct hv_msg_read_console_reply reply;
    unsigned long words[HV_MAXMSGWDS];
  } replybuf;
  size_t replylen;

  if (send_receive(chip_console, HV_TAG_READ_CONSOLE, &rd_msg, sizeof (rd_msg),
                   replybuf.words, sizeof (replybuf.words), &replylen,
                   MSG_FLG_XMITFAIL | MSG_FLG_SHORTREPLY))
    return (0);

  struct hv_msg_read_console_reply* msgp = &replybuf.reply;

  assert(replylen >= sizeof (*msgp));

  if (msgp->len > 0)
  {
    assert(msgp->len <= replylen - sizeof (*msgp));
    memcpy(s, (char*) &msgp[1], msgp->len);
  }

  return (msgp->len);
}


/** Sync the remote console.
 * @param private Private file state.
 * @return Zero if all data was successfully flushed.
 */
static int
remote_cons_sync(void* private)
{
  long ack;
  struct hv_msg_flush_console msg =
  {
    .client_no = ((uintptr_t) private != 0) ? my_client : -1,
    .dummy = 0,
  };

  send_receive(chip_console, HV_TAG_FLUSH_CONSOLE, &msg,
               sizeof (msg), &ack, sizeof (ack), NULL, MSG_FLG_XMITFAIL);

  return ack;
}


/** Remote console file operations vector. */
static struct _file_ops remote_fops =
{
  .write = remote_cons_write,
  .read = remote_cons_read,
  .sync = remote_cons_sync
};

/** Buffer for remote console output file. */
static char remote_outbuf[256];

/** Remote console output file. */
FILE remote_out =
{
  .buf = remote_outbuf,
  .len = sizeof (remote_outbuf),
  .ptr = remote_outbuf,
  .wrem = sizeof (remote_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &remote_fops
};

/** Buffer for remote console input file.  This is intentionally limited
 *  to 1 character so that we provide a consistent, serialized, view
 *  of console input to the supervisor, which may be moving between
 *  different tiles.  If it's bigger than that, the supervisor may ask
 *  for 1 character on tile X, but we can read more than that into the
 *  buffer, if the characters are available.  A subsequent hypervisor
 *  call to read the console on tile Y will go to the console tile again
 *  to get a new character, while the characters which were actually next
 *  in the input stream are stuck in tile X's buffer.
 *
 *  When we provide a supervisor call to read more than one byte at a time,
 *  this can be increased, but we'll still probably need a special stdio
 *  "unbuffered" mode to only read what we can consume immediately.
 */
static char remote_inbuf[1];

/** Remote console input file. */
FILE remote_in =
{
  .buf = remote_inbuf,
  .len = sizeof (remote_inbuf),
  .ptr = remote_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &remote_fops
};

/** Buffer for remote client console output file. */
static char remote_client_outbuf[256];

/** Remote console output file. */
FILE remote_client_out =
{
  .buf = remote_client_outbuf,
  .len = sizeof (remote_client_outbuf),
  .ptr = remote_client_outbuf,
  .wrem = sizeof (remote_client_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &remote_fops,
  .pvt = (void*) 1,
};

/** Buffer for remote client console input file; see comments above for why
 *  this is only 1 character.
 */
static char remote_client_inbuf[1];

/** Remote client console input file. */
FILE remote_client_in =
{
  .buf = remote_client_inbuf,
  .len = sizeof (remote_client_inbuf),
  .ptr = remote_client_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &remote_fops,
  .pvt = (void*) 1,
};
