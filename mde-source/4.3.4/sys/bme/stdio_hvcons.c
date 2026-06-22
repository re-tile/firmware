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
 * Output of characters to the hypervisor console.
 */

#include <stdio.h>

#include <arch/idn.h>

#include "bme_state.h"
#include "hv_msg.h"
#include "misc.h"

#include "../hv/bme_msg.h"

/** Write to the hypervisor console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @return Number of characters written.
 */
static int
_bme_hvcons_write(char* s, int len, unsigned int offset, void* private)
{
  struct bme_msg_write_console wr_msg;

  wr_msg.len = len;

  unsigned long ackbuf;

  pos_t chip_console = *(pos_t *) private;

  if (_bme_send_receive_var(chip_console, BME_TAG_WRITE_CONSOLE, &wr_msg,
                            sizeof (wr_msg), s, len, &ackbuf, sizeof (ackbuf),
                            NULL, MSG_FLG_XMITFAIL))
    return (0);
  else
    return (ackbuf);
}


/** Initialize the hypervisor console output file.
 * @param console_tile Tile to whom we should send console output messages.
 */
static void
_bme_hvcons_init(FILE* f)
{
  bme_global_info_t* bgst = bme_map_global_info();

  //
  // Making this static is somewhat kludgy, but we only currently intend to
  // support one instance of stdout.  If we wanted to do more we'd have to
  // dynamically allocate this to cope with the shared data model case
  // properly.
  //
  static pos_t my_console_tile;
  my_console_tile = bgst->console_tile;

  bme_unmap_global_info();

  f->pvt = &my_console_tile;
}


/** Hypervisor console file operations vector. */
static struct _file_ops _bme_hvcons_fops =
{
  .write = _bme_hvcons_write,
  .init = _bme_hvcons_init,
};

/** Buffer for hypervisor console output file. */
static char _bme_hvcons_outbuf[256];

/** Remote console output file. */
FILE bme_stdout_hvcons =
{
  .buf = _bme_hvcons_outbuf,
  .len = sizeof (_bme_hvcons_outbuf),
  .ptr = _bme_hvcons_outbuf,
  .wrem = sizeof (_bme_hvcons_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &_bme_hvcons_fops
};
