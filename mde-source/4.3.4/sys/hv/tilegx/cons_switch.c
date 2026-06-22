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
 * Output of characters to the UART console, or the tile-monitor FIFO
 * console, depending on which is currently enabled.
 */

#include <stdio.h>

#include <arch/uart.h>

#include "cfg.h"
#include "console.h"
#include "cons_rshim.h"
#include "types.h"
#include "uart.h"


/** Write to the switchable console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer; unused for this file.
 * @return Number of characters written.
 */
static int
switch_cons_write(char* s, int len, unsigned int offset, void* private)
{
  int use_tmfifo = cfg_rd(__insn_mfspr(SPR_RSHIM_COORD),
                          UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
                   CONS_RSHIM_H2T_USE_TMF_CON_MASK;

  FILE* f = (use_tmfifo) ? (private) ? &tmfifo_out_onlcr : &tmfifo_out
                         : (private) ? &uart_out_onlcr : &uart_out;

  return f->ops->write(s, len, offset, f->pvt);
}


/** Read from the switchable console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param private Private data pointer; unused for this file.
 * @return Number of characters read.
 */
static int
switch_cons_read(char* s, int len, unsigned int offset, void* private)
{
  int use_tmfifo = cfg_rd(__insn_mfspr(SPR_RSHIM_COORD),
                          UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
                   CONS_RSHIM_H2T_USE_TMF_CON_MASK;

  FILE* f = (use_tmfifo) ? &tmfifo_in : &uart_in;

  return f->ops->read(s, len, offset, f->pvt);
}


/** Flush the switchable console.
 * @param private Private data pointer; unused for this file.
 * @return Zero if all data was successfully flushed.
 */
static int
switch_cons_sync(void* private)
{
  int use_tmfifo = cfg_rd(__insn_mfspr(SPR_RSHIM_COORD),
                          UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
                   CONS_RSHIM_H2T_USE_TMF_CON_MASK;

  FILE* f = (use_tmfifo) ? &tmfifo_in : &uart_in;

  return f->ops->sync(f->pvt);
}


/** Switchable console file operations vector. */
static struct _file_ops switch_fops =
{
  .write = switch_cons_write,
  .read = switch_cons_read,
  .sync = switch_cons_sync,
};

/** Buffer for switchable console output file. */
static char switch_outbuf[256];

/** Switchable console output file. */
FILE switch_out =
{
  .buf = switch_outbuf,
  .len = sizeof (switch_outbuf),
  .ptr = switch_outbuf,
  .wrem = sizeof (switch_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &switch_fops
};

/** Buffer for switchable console output file. */
static char switch_outbuf_onlcr[256];

/** Switchable console output file. */
FILE switch_out_onlcr =
{
  .buf = switch_outbuf,
  .len = sizeof (switch_outbuf_onlcr),
  .ptr = switch_outbuf,
  .wrem = sizeof (switch_outbuf_onlcr),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &switch_fops,
  .pvt = (void *) 1,
};

/** Buffer for switchable console input file.  See the TM FIFO or UART
 *  console drivers for an explanation of why it's just one character.
 */
static char switch_inbuf[1];

/** Switchable console input file. */
FILE switch_in =
{
  .buf = switch_inbuf,
  .len = sizeof (switch_inbuf),
  .ptr = switch_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &switch_fops
};
