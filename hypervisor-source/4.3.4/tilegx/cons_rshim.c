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
 * Output of characters to the console via the rshim scratch buffer.
 */

#include <stdio.h>
#include <string.h>

#include <arch/rsh.h>
#include <arch/uart.h>

#include "bits.h"
#include "cfg.h"
#include "cons_rshim.h"
#include "hw_config.h"
#include "types.h"
#include "uart.h"


/** Mask for output read/write buffer pointers. */
static int out_buf_mask;

/** Mask for input read/write buffer pointers. */
static int in_buf_mask;

/** Offset of the input buffer within the scratch buffer, in words. */
static int in_buf_base;

/** Cached copy of the output write buffer pointer. */
static uint_reg_t out_writeptr;

/** Cached copy of the input read buffer pointer.  Note that this is not
 *  right-justified in the word; it's just the UART scratchpad ANDed with
 *  the input mask. */
static uint_reg_t in_readptr;


/** Initialize the rshim console, if it should be enabled.
 * @param use_tmf_con If non-NULL, the value this points to is set to 
 *  1 if we should be using the tile-monitor FIFO console once the
 *  rshim console has been closed.
 * @return Nonzero if the rshim console is enabled, else zero.
 */
int
rshim_console_init(int* use_tmf_con)
{
  uint_reg_t rshimaddr = __insn_mfspr(SPR_RSHIM_COORD);

  uint32_t sp = cfg_rd(rshimaddr, UART_CHANNEL_PORT1, UART_SCRATCHPAD);

  if (use_tmf_con)
    *use_tmf_con = ((sp & CONS_RSHIM_H2T_USE_TMF_CON_MASK) != 0);

  //
  // Note that we don't support console input without console output,
  // so if output is disabled we can just return now.
  //
  if (!(sp & CONS_RSHIM_H2T_OUT_ENABLE_MASK))
    return 0;

  //
  // Extract the log2 of the number of words to use, add 3 to convert
  // to the log2 of the number of bytes to use, then turn into a mask.
  //
  out_buf_mask = RMASK(3 + ((sp & CONS_RSHIM_H2T_OUT_BUFSIZ_MASK) >>
                            CONS_RSHIM_H2T_OUT_BUFSIZ_SHIFT));

  //
  // We call this routine both from the booter and from the hypervisor, so
  // we can't just start the write pointer at zero.
  //
  out_writeptr = cfg_rd(rshimaddr, UART_CHANNEL, UART_SCRATCHPAD) &
                 out_buf_mask;

  //
  // Now let's check for input.  Note that we use the buffer mask being
  // zero as a flag to say "we aren't doing input".
  //
  if (sp & CONS_RSHIM_H2T_IN_ENABLE_MASK)
  {
    //
    // Extract the log2 of the number of words to use, add 3 to convert
    // to the log2 of the number of bytes to use, then turn into a mask.
    //
    in_buf_mask = RMASK(3 + ((sp & CONS_RSHIM_H2T_IN_BUFSIZ_MASK) >>
                             CONS_RSHIM_H2T_IN_BUFSIZ_SHIFT)) <<
                  CONS_RSHIM_T2H_IN_RPTR_SHIFT;

    //
    // We call this routine both from the booter and from the hypervisor, so
    // we can't just start the read pointer at zero.
    //
    in_readptr = cfg_rd(rshimaddr, UART_CHANNEL, UART_SCRATCHPAD) &
                 in_buf_mask;

    in_buf_base = (out_buf_mask >> 3) + 1;
  }

  return 1;
}


/** Shut down the rshim console.
 * @param wait If nonzero, wait for an acknowledgement from the reader.
 */
void
rshim_console_fini(int wait)
{
  uint_reg_t rshimaddr = __insn_mfspr(SPR_RSHIM_COORD);

  //
  // Tell the reader we're done...
  //
  cfg_wr(rshimaddr, UART_CHANNEL, UART_SCRATCHPAD,
         out_writeptr | in_readptr | CONS_RSHIM_T2H_DONE_MASK);

  //
  // ...and wait for an acknowledgement if requested.
  //
  if (wait)
    while ((cfg_rd(rshimaddr, UART_CHANNEL_PORT1,
                   UART_SCRATCHPAD) & CONS_RSHIM_H2T_DONE_MASK) == 0)
      ;
}


/** Write a character to the rshim console.
 * @param c Character to write.
 */
void
rshim_putchar(char c)
{
  uint_reg_t rshimaddr = __insn_mfspr(SPR_RSHIM_COORD);

  if (c == '\n')
    rshim_putchar('\r');

  uint_reg_t next_out_writeptr = (out_writeptr + 1) & out_buf_mask;

  //
  // Wait until the buffer is not full.  Note that we just spin forever if
  // it is; we could time out, and proceed, discarding output.  However,
  // the theory is that it will be easier to diagnose problems with the
  // rshim console feature if we don't.
  //
  while (1)
  {
    uint_reg_t readptr =
      cfg_rd(rshimaddr, UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
      out_buf_mask;

    if (next_out_writeptr != readptr)
      break;
  }

  //
  // Acquire the semaphore.
  //
  while (cfg_rd(rshimaddr, 0, RSH_SEMAPHORE0) != 0)
    ;

  //
  // Write the new byte into the scratch buffer.  The buffer is only
  // word-addressable, and we write one byte at a time, so we have to do a
  // read-modify-write to put the byte in the right place.  We could cache
  // the last written value to avoid an MMIO read and a write in most
  // cases, but that's somewhat tricky, and this is not at all
  // performance-critical.
  //
  cfg_wr(rshimaddr, 0, RSH_SCRATCH_BUF_CTL, out_writeptr >> 3);
  uint_reg_t word = cfg_rd(rshimaddr, 0, RSH_SCRATCH_BUF_DAT);
  int shift = (out_writeptr & 7) << 3;
  word = (word & ~((uint_reg_t) 0xFF << shift)) | ((uint_reg_t) c << shift);
  cfg_wr(rshimaddr, 0, RSH_SCRATCH_BUF_CTL, out_writeptr >> 3);
  cfg_wr(rshimaddr, 0, RSH_SCRATCH_BUF_DAT, word);

  //
  // Update the write pointer.
  //
  out_writeptr = next_out_writeptr;
  cfg_wr(rshimaddr, UART_CHANNEL, UART_SCRATCHPAD,
         out_writeptr | in_readptr);

  //
  // Release the semaphore.
  //
  cfg_wr(rshimaddr, 0, RSH_SEMAPHORE0, 0);
}


/** Flush the rshim output stream (i.e., wait for any output characters to
 *  drain). */
void
rshim_flush_output()
{
  uint_reg_t rshimaddr = __insn_mfspr(SPR_RSHIM_COORD);

  //
  // Wait until the buffer is empty.
  //
  while ((cfg_rd(rshimaddr, UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
          out_buf_mask) != out_writeptr)
    ;
}


/** Get a character from the rshim console, if one is available; if not, wait
 *  up to the specified timeout for one to show up.
 * @param msec Time to wait in milliseconds.
 * @return Character received, or -1 if none are available after the timeout
 *   period expires.
 */
int
rshim_getchar_timeout(int msec)
{
  uint_reg_t rshimaddr = __insn_mfspr(SPR_RSHIM_COORD);

  //
  // If we aren't doing input, we never have any characters, so there's
  // no point in waiting at all.
  //
  if (!in_buf_mask)
    return -1;

  uint_reg_t clk = __insn_mfspr(SPR_CYCLE) + msec * (REFCLK / 1000);

  do
  {
    //
    // See if the buffer has any characters.
    //
    uint_reg_t writeptr =
      cfg_rd(rshimaddr, UART_CHANNEL_PORT1, UART_SCRATCHPAD) &
      in_buf_mask;

    if (in_readptr != writeptr)
    {
      //
      // Acquire the semaphore.
      //
      while (cfg_rd(rshimaddr, 0, RSH_SEMAPHORE0) != 0)
        ;

      //
      // Read the new byte from the scratch buffer.
      //
      cfg_wr(rshimaddr, 0, RSH_SCRATCH_BUF_CTL,
             in_buf_base + (in_readptr >> (3 + CONS_RSHIM_T2H_IN_RPTR_SHIFT)));
      uint_reg_t word = cfg_rd(rshimaddr, 0, RSH_SCRATCH_BUF_DAT);

      int shift = ((in_readptr >> CONS_RSHIM_T2H_IN_RPTR_SHIFT) & 7) << 3;
      int retval = (word >> shift) & 0xFF;

      //
      // Update the read pointer.
      //
      in_readptr = (in_readptr + (1 << CONS_RSHIM_T2H_IN_RPTR_SHIFT)) &
        in_buf_mask;
      cfg_wr(rshimaddr, UART_CHANNEL, UART_SCRATCHPAD,
             out_writeptr | in_readptr);

      //
      // Release the semaphore.
      //
      cfg_wr(rshimaddr, 0, RSH_SEMAPHORE0, 0);

      return retval;
    }
  }
  while (__insn_mfspr(SPR_CYCLE) <= clk);

  return -1;
}


#ifndef HV_L1BOOT

/** Write to the rshim console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer; unused for this file.
 * @return Number of characters written.
 */
static int
rshim_cons_write(char* s, int len, unsigned int offset, void* private)
{
  int orig_len = len;

  while (len--)
    rshim_putchar(*s++);

  return orig_len;
}


/** Read from the rshim console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param private Private data pointer; unused for this file.
 * @return Number of characters read.  Always 0, since we don't support
 *  reading the rshim console.
 */
static int
rshim_cons_read(char* s, int len, unsigned int offset, void* private)
{
  return 0;
}


/** rshim console file operations vector. */
static struct _file_ops rshim_fops =
{
  .write = rshim_cons_write,
  .read = rshim_cons_read,
};

/** Buffer for rshim console output file. */
static char rshim_outbuf[256];

/** rshim console output file. */
FILE rshim_out_onlcr =
{
  .buf = rshim_outbuf,
  .len = sizeof (rshim_outbuf),
  .ptr = rshim_outbuf,
  .wrem = sizeof (rshim_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &rshim_fops
};

/** Buffer for rshim console input file.  We don't support reading
 *  the rshim console, so it's just one character.
 */
static char rshim_inbuf[1];

/** rshim console input file. */
FILE rshim_in =
{
  .buf = rshim_inbuf,
  .len = sizeof (rshim_inbuf),
  .ptr = rshim_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &rshim_fops
};

#endif
