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
 * Output of characters to the rshim's UART.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/rsh.h>
#include <arch/sim.h>
#include <arch/uart.h>

#include "sys/libc/include/util.h"

#include "board_info.h"
#include "config.h"
#include "console.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "idn.h"
#include "mapping.h"
#include "param.h"
#include "uart.h"


/** Address of the UART on the IODN. */
static uint32_t uart_dest;

/** Channel number of the UART. */
static unsigned long uart_chan = UART_CHANNEL;

/** Binding select code for the UART interrupt. */
static int uart_bind_dev_sel = RSH_INT_BIND__DEV_SEL_VAL_UART0;

/** UART interrupt flag; if nonzero, we're in interrupt-driven mode. */
static int uart_intr_enabled;

#define UART_BUF_SIZE   2048 /**< Size of input and output buffer for UART */

/** The UART software buffer structure.
 */
typedef struct uart_buf
{
  char* buf;    /**< UART input or output buffer */
  int rptr;     /**< UART input or output buffer read pointer */
  int wptr;     /**< UART input or output buffer write pointer;
                          when the read and write pointers are equal,
                          the buffer is empty */
} uart_buf_t;
static uart_buf_t inbuf;
static uart_buf_t outbuf;

/** Maximum length of the UART console debug string. */
#define CONS_DEBUG_STR_MAX_LEN  16

/** The UART console debug string; note that this is not null-terminated. */
static char debug_string[CONS_DEBUG_STR_MAX_LEN] = "\x1e";

/** Length of the UART console debug string; needed since the debug string
 *  could conceivably contain nulls. */
static int debug_string_len = 1;

/** Shift table for the debug string; see below for what this means. */
static uint8_t debug_string_shift[CONS_DEBUG_STR_MAX_LEN];

/** Nonzero if we're currently in protocol mode */
static int in_protocol_mode = 0;

/** Read a character from the uart input or output buffer. */
static int uart_get_from_buf(uart_buf_t *buf);

/** Write a character the uart input or output buffer. */
static int uart_add_to_buf(uart_buf_t *buf, char c);

/** Get the number of free slots in the buffer. */
static int uart_buf_free_slot(uart_buf_t *buf);

static int is_uart_buf_empty(uart_buf_t *buf);

static void uart_drain_input(void);


/** Initialize the UART console subsystem.
 * @param dest Address of the UART on the IODN.
 * @param first_init Nonzero iff this is the first call to this routine
 *   from any tile; otherwise, we don't do any destructive reset of the
 *   hardware.
 * @param reconfig_uart Nonzero iff we should reconfigure the UART based
 *   on parameters in the BIB, or the standard defaults; otherwise, we
 *   assume the booter has already done this, and we leave the settings
 *   alone.
 * @param uart_port Which UART to use.
 */
void
init_uart_console(uint32_t dest, int first_init, int reconfig_uart,
                  int uart_port)
{
  uart_dest = dest;

  if (first_init)
  {
    if (uart_port == 1)
    {
      uart_chan = UART_CHANNEL_PORT1;
      uart_bind_dev_sel = RSH_INT_BIND__DEV_SEL_VAL_UART1;
    }

    //
    // Reset the shim to get rid of any gunk that might be in the input FIFO.
    //
    cfg_wr(uart_dest, uart_chan, UART_BASELINE_CTL, 0);
    cfg_wr(uart_dest, uart_chan, UART_BASELINE_CTL,
           UART_BASELINE_CTL__ENABLE_MASK);

    if (reconfig_uart)
    {
      //
      // The booter may have changed the UART parameters based on the BIB,
      // and ideally we'd just leave them alone.  However, we might be
      // booting over UART, in which case these values are still set to the
      // hardware defaults, which aren't what we want for console output.
      // So, we do the BIB lookup and UART setup, possibly for the second
      // time.
      //
      uint32_t desc;
      bi_ptr_t bi;
      struct bi_console_cfg* cfg = NULL;

      desc = bi_getparam(BI_TYPE_CONSOLE_CFG, 0, &bi, NULL);
      if (desc != BI_NULL)
        cfg = (struct bi_console_cfg*) bi;

      if (cfg)
      {
        //
        // If we set the baud rate to zero, UART output will hang forever, so
        // don't allow that.
        //
        long baud_rate = max(110, cfg->baud_rate);

        cfg_wr(uart_dest, uart_chan, UART_DIVISOR,
               (refclk_speed / 16) / baud_rate);
        cfg_wr(uart_dest, uart_chan, UART_TYPE,
               (cfg->parity << UART_TYPE__PTYPE_SHIFT) |
               (cfg->data_bits << UART_TYPE__DBITS_SHIFT) |
               (cfg->stop_bits << UART_TYPE__SBITS_SHIFT));
      }
      else
      {
        cfg_wr(uart_dest, uart_chan, UART_DIVISOR,
               (refclk_speed / 16) / UART_SPEED);
        // Set for 8 bits, no parity, 1 stop bit
        cfg_wr(uart_dest, uart_chan, UART_TYPE, 0);
      }
    }
  }
}

/** Set the UART console debug string.
 * @param str Debug string in human-readable format (before escape processing).
 */
void
init_uart_debug_string(char* str)
{
  //
  // If we're called, we're going to override the default, so we might as
  // well zero the length out now.
  //
  debug_string_len = 0;

  //
  // If we weren't given a string, we'll disable the default and not have
  // a debug string.
  //
  if (!str)
    return;

  //
  // Copy the given string to our buffer, resolving the escape sequences.
  // This is fairly generic code, but we don't do this in config.c because
  // we want to handle the case where the string contains nulls, and it
  // seems odd to push the overhead of maintaining a separate length count
  // on all option processing.
  //
  for (char* p = str; *p && debug_string_len < CONS_DEBUG_STR_MAX_LEN; p++)
  {
    if (*p == '\\')
    {
      //
      // This is an escape sequence.
      //
      p++;

      //
      // First handle the single-character escapes.
      //
      if (*p == 'r')
        debug_string[debug_string_len++] = '\r';
      else if (*p == 'n')
        debug_string[debug_string_len++] = '\n';
      else if (*p == 't')
        debug_string[debug_string_len++] = '\t';
      else if (*p == '\\')
        debug_string[debug_string_len++] = '\\';
      else if (*p >= '0' && *p < '9')
      {
        //
        // Octal character specifier.
        //
        int c = 0;   // Character we're building

        //
        // We only want to parse the number of digits that won't overflow
        // an 8-bit character, so if the first digit is less than 4, we
        // allow three digits, otherwise we allow two.
        //
        if (*p < '4')
          c = (c << 3) + *p++ - '0';
        if (*p >= '0' && *p <= '7')
          c = (c << 3) + *p++ - '0';
        if (*p >= '0' && *p <= '7')
          c = (c << 3) + *p++ - '0';

        debug_string[debug_string_len++] = c;
        p--;  // Counteract the increment which the for loop will do.
      }
      else if (*p == 'x' || *p == 'X')
      {
        //
        // Hexadecimal character specifier.
        //
        if (*++p == '\0')
        {
          //
          // If there's nothing after the x or X, just take that as a
          // character.
          //
          debug_string[debug_string_len++] = p[-1];
          break;
        }

        //
        // If the first digit isn't hex, just take the x or X.
        //
        if (!isxdigit(*p))
        {
          debug_string[debug_string_len++] = *--p;
          continue;
        }

        //
        // Convert the first digit.  This takes advantage of the fact that
        // the low 4 bits of 0-9 are the digit's value, while the low 4 bits
        // of both A-F and a-f are 1 to 6.
        //
        int c = (*p & 0xF) + ((*p >= 'A') ? 9 : 0);
        p++;

        //
        // If there's another hex digit, take it.
        //
        if (isxdigit(*p))
        {
          c = (c << 4) + (*p & 0xF) + ((*p >= 'A') ? 9 : 0);
          p++;
        }

        debug_string[debug_string_len++] = c;
        p--;  // Counteract the increment which the for loop will do.
      }
      else if (*p == '\0')
      {
        //
        // A backslash at the end of the string is taken literally.
        //
        debug_string[debug_string_len++] = '\\';
        break;
      }
      else
        //
        // Any other character after a backslash is taken literally.
        //
        debug_string[debug_string_len++] = *p;
    }
    else
      //
      // Not an escape sequence, just take the character.
      //
      debug_string[debug_string_len++] = *p;
  }

  //
  // Now we compute the shift table for the string.  This is used to figure
  // out how much of the currently-being-matched input string should be
  // sent on to the input buffer when we get a character which doesn't
  // match the debug string.  For example:
  //
  // - Say our debug string is "abacab", and we receive "aba".  So far
  //   we've matched the first three characters of the debug string.
  //   We haven't put the "aba" into the input buffer yet, since if we
  //   match the whole debug string we don't want to take it as input.
  //
  // - Now we get "b".  That doesn't match the fourth character of the
  //   string, so we know we aren't going to match it starting with all
  //   the characters we've been holding on to.  Some of those characters
  //   can be sent on to the input.  But how many?  In this case, we don't
  //   want to send on "abab", since that second "a" could be the start
  //   of a successful debug string match.  We just want to send the first
  //   "ab" on to the input.
  //
  // The shift table makes that "how many" decision for you.
  // debug_string_shift[i] tells you how many characters to shift off the
  // current partially-matched string to the input buffer when character
  // number i turns out to not match.  For "abacab", the shift table is
  // {0, 1, 2, 2, 4, 4}.
  //
  // Note that after the shift, you then need to check the new character
  // against the spot in the debug string after the retained characters,
  // and you may end up shifting again.  For instance, if we got "abax",
  // we'd first shift the first two characters to the input; then we'd notice
  // that "x" doesn't match the second character of the debug string, and
  // would shift one more; finally we'd notice that "x" doesn't match the
  // first character, either, and would shift one final time.
  //
  for (int i = 0; i < debug_string_len; i++)
  {
    debug_string_shift[i] = i;
    for (int j = 1; j < i; j++)
      if (!strncmp(debug_string + j, debug_string, i - j))
      {
         debug_string_shift[i] = j;
         break;
      }
  }
}


/** Write a character to the UART.
 * @param c Character to write.
 */
static void
uart_putchar(char c)
{
  uint_reg_t status;

  // Wait for space to be available. I'm not sure from the docs
  // what the difference between the transmit and write FIFO is, so
  // we'll just wait until neither of them is full.
  status = cfg_rd(uart_dest, uart_chan, UART_FLAG);
  while (status & (UART_FLAG__WFIFO_FULL_MASK | UART_FLAG__TFIFO_FULL_MASK))
    status = cfg_rd(uart_dest, uart_chan, UART_FLAG);

  // Write character.
  cfg_wr(uart_dest, uart_chan, UART_TRANSMIT_DATA, (uint32_t) c);
}


/** Read a character from the UART.
 * @return Character read, or -1 if no character is available.
 */
static int
uart_getchar_nb()
{
  uint32_t data;

  //
  // If we're interrupt-driven, get character from uart input buffer instead.
  //
  if (uart_intr_enabled)
    return (uart_get_from_buf(&inbuf));

  // See if there are any input characters; if not, return -1.
  data = cfg_rd(uart_dest, uart_chan, UART_FLAG);
  if (data & UART_FLAG__RFIFO_EMPTY_MASK)
    return (-1);

  // Read and return character.
  return (cfg_rd(uart_dest, uart_chan, UART_RECEIVE_DATA));
}

/** Fill the UART tx fifo as much as possible.
 */
static void
uart_fill_txfifo(uart_buf_t *buf)
{
  uint_reg_t flag = 0;

  while (!is_uart_buf_empty(&outbuf))
  {
    flag = cfg_rd(uart_dest, uart_chan, UART_FLAG);
    if (flag & UART_FLAG__TFIFO_FULL_MASK)
      break;
    else
    {
      char c = uart_get_from_buf(buf);
      uart_putchar(c);
    }
  }

  //
  // If we notice pending input characters, we drain them.
  //
  if (!(flag & UART_FLAG__RFIFO_EMPTY_MASK))
    uart_drain_input();
}


/** Write to the UART console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param offset File offset (not used by this device).
 * @param private Private file state (not used by this device).
 * @return Number of characters written.
 */
static int
uart_cons_write(char* s, int len, unsigned int offset, void* private)
{
  int origlen = len;

  //
  // If we've gone into protocol mode, we don't want to actually write to
  // the console, so as not to interfere with any ongoing debugging
  // operation.  However, we'd like the debugger to be able to turn off
  // protocol mode and have things go back to normal, so before ignoring
  // the write request, we check to see if the hardware still thinks it's
  // in protocol mode; if not, we turn off our flag.
  //
  if (in_protocol_mode)
  {
    uint32_t modereg = cfg_rd(uart_dest, uart_chan, UART_MODE);
    if (modereg != UART_MODE__BYPASS_MASK)
      in_protocol_mode = 0;
    else
      return (origlen);
  }

  // Make sure the IDN isn't already in use; if it is, we can't do the write.
  if (IDN0_IS_BUSY())
    return (0);

  if (uart_intr_enabled)
  {
    for (len = 0; len < origlen; len++)
    {
      char c = *s++;

      // Hypervisor output.
      if (private)
      {
        while (uart_buf_free_slot(&outbuf) < 2)
          uart_fill_txfifo(&outbuf);

        if (c == '\n')
          uart_add_to_buf(&outbuf, '\r');
        uart_add_to_buf(&outbuf, c);
      }
      // Client output.
      else
      {
        if (uart_buf_free_slot(&outbuf) > 0)
          uart_add_to_buf(&outbuf, c);
        else
        {
          if (console_ipi_addr != 0)
          {
            // If the pending console IPI flag was set already,
            // return immediately.
            if (IS_PENDING_IPI_OUTPUT)
              return len;

            // I can't fully meet the client's output request because output
            // buffer is full, set the console IPI flag and return now.
            // Hypervisor will send an IPI to the client when the output
            // buffer space is available later.
            SET_PENDING_IPI_OUTPUT();
            break;
          }
          else
          {
            while (uart_buf_free_slot(&outbuf) <= 0)
              uart_fill_txfifo(&outbuf);
            uart_add_to_buf(&outbuf, c);
          }
        }
      }
    }

    // Fill the UART tx fifo and return.
    uart_fill_txfifo(&outbuf);
    return len;
  }
  else
  {
    while (len--)
    {
      char c = *s++;

      if (c == '\n' && private)
        uart_putchar('\r');
      uart_putchar(c);
    }

    return (origlen);
  }
}

/** Read from the UART console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param offset File offset (not used by this device).
 * @param private Private file state (not used by this device).
 * @return Number of characters read.
 */
static int
uart_cons_read(char* s, int len, unsigned int offset, void* private)
{
  int retval = 0;

  // Make sure the IDN isn't already in use; if it is, we can't do the read.
  if (IDN0_IS_BUSY())
    return (0);

  while (len--)
  {
    int c = uart_getchar_nb();
    if (c != -1)
    {
      *s++ = c;
      retval++;
    }
    else
      break;
  }

  return (retval);
}


/** Wait for the UART console to drain.
 * @param private Private file state (not used by this device).
 * @return Zero if all data was successfully flushed.
 */
static int
uart_cons_sync(void* private)
{
  uint_reg_t status;

  //
  // Flush the UART output buffer.
  //
  while (!is_uart_buf_empty(&outbuf))
    uart_fill_txfifo(&outbuf);

  //
  // Wait for both the write and transmit FIFOs to be empty.
  //
  status = cfg_rd(uart_dest, uart_chan, UART_FLAG);
  while (!(status & UART_FLAG__WFIFO_EMPTY_MASK) ||
         !(status & UART_FLAG__TFIFO_EMPTY_MASK))
  {
    //
    // This is kind of a hack, necessary because our output isn't yet
    // interrupt-driven, but our input is.  Since we're blocking here until
    // the output FIFOs are empty, we could keep the input interrupt
    // routine from running and lose input characters.  To make this harder
    // to do, if we notice pending input characters, we drain them.
    //
    if (uart_intr_enabled && !(status & UART_FLAG__RFIFO_EMPTY_MASK))
      uart_drain_input();

    status = cfg_rd(uart_dest, uart_chan, UART_FLAG);
  }

  return (0);
}


/** UART console output file operations vector. */
static struct _file_ops uart_fops =
{
  .write = uart_cons_write,
  .read = uart_cons_read,
  .sync = uart_cons_sync,
};

/** Buffer for UART console output file. */
static char uart_outbuf[256];

/** UART console output file. */
FILE uart_out =
{
  .buf = uart_outbuf,
  .len = sizeof (uart_outbuf),
  .ptr = uart_outbuf,
  .wrem = sizeof (uart_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &uart_fops
};

/** Buffer for UART console input file.  This is tiny in order to prevent
 *  out-of-order characters being delivered when the supervisor is reading
 *  from the tile which owns the UART and tiles which access it remotely.
 *  (See more explanation of this problem in cons_remote.c.) Note that we're
 *  already doing plenty of buffering as part of our interrupt-driven input
 *  handling.
 */
static char uart_inbuf[1];

/** UART console input file. */
FILE uart_in =
{
  .buf = uart_inbuf,
  .len = sizeof (uart_inbuf),
  .ptr = uart_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &uart_fops
};

/** Buffer for UART console output file, with nl->crnl translation. */
static char uart_outbuf_onlcr[256];

/** UART console output file, with nl->crnl translation. */
FILE uart_out_onlcr =
{
  .buf = uart_outbuf_onlcr,
  .len = sizeof (uart_outbuf_onlcr),
  .ptr = uart_outbuf_onlcr,
  .wrem = sizeof (uart_outbuf_onlcr),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &uart_fops,
  .pvt = (void *) 1,
};

//
// Interrupt-driven UART support.
//


/** Check if the buffer is empty.
 * @param buf Pointer to the buffer structure.
 * @return 1 if the buffer is empty, otherwise 0.
 */
static int
is_uart_buf_empty(uart_buf_t *buf)
{
  return uart_buf_free_slot(buf) == UART_BUF_SIZE - 1 ? 1 : 0;
}


/** Add a character to the input or output buffer.
 * @param buf Pointer to the buffer structure.
 * @param c Character to add to the buffer.
 * @return 0 on success, -1 if can't fit in the buffer
 *         because the buffer is full.
 */
static int
uart_add_to_buf(uart_buf_t *buf, char c)
{
  int next_wptr = (buf->wptr >= UART_BUF_SIZE - 1) ? 0 : buf->wptr + 1;

  if (buf->rptr != next_wptr)
  {
    buf->buf[buf->wptr] = c;
    buf->wptr = next_wptr;
    return 0;
  }

  return -1;
}


/** State of the debug string match process, and of the processing of a
 *  multi-character console command.  This is a hybrid value; if it's
 *  non-negative, it's the number of characters which have already
 *  been matched in debug_string[].  Otherwise, it's one of the states
 *  listed in the enum, which all have negative values. */
static enum debug_string_states
{
  DEBUG_ALL_CHARS_MATCHED = -1,
  DEBUG_PARSING_CLIENT = -2,
}
debug_string_state = 0;

/** Client number that we're in the middle of parsing, for the 'c' command. */
static int debug_client_number = 0;


/** Enter protocol mode on the UART.
 * @param stop_output If nonzero, set in_protocol_mode, which will prevent
 *  further console output.
 */
static void
uart_enter_protocol_mode(int stop_output)
{
  fprintf(&uart_out_onlcr, "Forcing UART protocol mode, %s be functional\n",
          stop_output ? "the console will no longer"
                      : "console input will not");
  cfg_wr(uart_dest, uart_chan, UART_MODE, UART_MODE__BYPASS_MASK);
  //
  // On Gx, when entering protocol mode, we reset the UART to the hardware
  // reset values (115200,8E2); this allows a freshly started copy of the
  // BTK to just work without needing any special reconfiguration.
  //
  cfg_wr(uart_dest, uart_chan, UART_DIVISOR, 0x44);
  cfg_wr(uart_dest, uart_chan, UART_TYPE,
         (UART_TYPE__PTYPE_VAL_EVEN << UART_TYPE__PTYPE_SHIFT) |
         (UART_TYPE__DBITS_VAL_EIGHT_DBITS << UART_TYPE__DBITS_SHIFT) |
         (UART_TYPE__SBITS_VAL_TWO_SBITS << UART_TYPE__SBITS_SHIFT));
  in_protocol_mode = stop_output;
}

/** Drain the UART's input FIFO into our input buffer, and process any
 *  console escape commands that we might find in the input stream.
 */
static void
uart_drain_input()
{
  while (!(cfg_rd(uart_dest, uart_chan, UART_FLAG) &
           UART_FLAG__RFIFO_EMPTY_MASK))
  {
    char c = cfg_rd(uart_dest, uart_chan, UART_RECEIVE_DATA);

    switch (debug_string_state)
    {
    case DEBUG_ALL_CHARS_MATCHED:
      //
      // If we've seen the escape prefix, then the next character is a
      // command that we need to execute.
      //
      switch (c)
      {
      // Enter protocol mode.
      case 'P':
        {
          uart_enter_protocol_mode(1);
          debug_string_state = 0;
          break;
        }

      // Connect the console to one of clients 0-9; for ones after that, you
      // need to use the 'c' command.
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        cons_set_active_client(c - '0', 0);
        debug_string_state = 0;
        break;

      // Connect the console to the next available client.
      case '+':
        cons_set_active_client(CONS_NEXT_CLIENT, 0);
        debug_string_state = 0;
        break;

      // Connect the console to the previous available client.
      case '-':
        cons_set_active_client(CONS_PREV_CLIENT, 0);
        debug_string_state = 0;
        break;

      // Connect the console to a client which the user will specify with
      // subsequent keystrokes.
      case 'c':
        {
          fprintf(&uart_out_onlcr, "\nEnter client number: ");
          fflush(stdout);
          debug_client_number = 0;
          debug_string_state = DEBUG_PARSING_CLIENT;
        }
        break;

      // Dump statistics to the console.
      case 'd':
        {
          dump_hv_stats(0);
          debug_string_state = 0;
          break;
        }

      // Clear statistics.
      case 'C':
        {
          dump_hv_stats(1);
          debug_string_state = 0;
          break;
        }

      // Print a list of the clients which are available.
      case 'l':
        {
          fprintf(&uart_out_onlcr, "\nhv: available clients:\n");
          for (int i = 0; i < config.nclients; i++)
          {
            int height = config.clients[i].lrhc.bits.x -
                         config.clients[i].ulhc.bits.x + 1;
            int width = config.clients[i].lrhc.bits.y -
                        config.clients[i].ulhc.bits.y + 1;
            int ntiles = pcnt_tile_mask(&config.clients[i].tiles);

            fprintf(&uart_out_onlcr, "%3d. %dx%d @ %d,%d, %d tile%s",
                    i, 
                    width, height,
                    UXY(config.clients[i].ulhc),
                    ntiles,
                    (ntiles == 1) ? "" : "s");

            if (config.clients[i].flags & CLIENT_BME)
              fprintf(&uart_out_onlcr, ", BME client");
            if (cons_client_is_active(i))
              fprintf(&uart_out_onlcr, ", console connected");

            fprintf(&uart_out_onlcr, "\n");
          }
          debug_string_state = 0;
        }
        break;

      // Reboot.
      case 'R':
        {
          RSH_BOOT_CONTROL_t rbc =
            {{ .boot_mode = RSH_BOOT_CONTROL__BOOT_MODE_VAL_NONE }};
          if (rshims[0])
            rbc.word = cfg_rd(rshims[0]->idn_ports[0].word, rshims[0]->channel,
                              RSH_BOOT_CONTROL);
          if (rbc.boot_mode == RSH_BOOT_CONTROL__BOOT_MODE_VAL_SPI)
          {
            fprintf(&uart_out_onlcr, "Resetting chip and restarting.\n");
            //
            // Delay to allow the UART FIFO to drain.
            //
            drv_udelay(200000);
            reset_chip(0);
          }
          else
            fprintf(&uart_out_onlcr, "Can't reboot unless booted from SROM.\n");

          debug_string_state = 0;
        }
        break;

      // Print a list of the console escape commands which are available.
      case 'h':
      case '?':
      default:
        {
          fprintf(&uart_out_onlcr, "\n"
                  "hv: console command help:\n"
                  "    0-9     connect console to client #\n"
                  "    c#<CR>  connect console to client #\n"
                  "    +/-     connect console to next/prev client\n"
                  "    l       list clients\n"
                  "    d       dump statistics to console\n"
                  "    C       clear statistics\n"
                  "    R       reboot (if booted from SROM)\n"
                  "    P       enter protocol mode\n");
          debug_string_state = 0;
        }
        break;
      }
      break;

    case DEBUG_PARSING_CLIENT:
      //
      // We've seen the escape prefix and the 'c' command, so now we're
      // collecting digits to make up the new client number.
      //
      switch (c)
      {
      // Add a digit to the number.
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        debug_client_number = 10 * debug_client_number + c - '0';
        fprintf(&uart_out_onlcr, "%c", c);
        fflush(stdout);
        break;

      // Backspace or delete; forget the last digit and erase it from the
      // screen.
      case '\b':
      case 0177:
        debug_client_number /= 10;
        fprintf(&uart_out_onlcr, "\b \b");
        fflush(stdout);
        break;

      // Entry of the number complete, so go do the connect.
      case '\n':
      case '\r':
        {
          fprintf(&uart_out_onlcr, "\n");
          cons_set_active_client(debug_client_number, 0);
          debug_string_state = 0;
        }
        break;

      // For anything else, we just give up and make them start over.  You
      // could argue we should give the user a chance to backspace over
      // this, but then we'd have to save everything they entered to make
      // that work right, and it doesn't seem worth the trouble when
      // they're entering a 2-digit number.
      default:
        {
          fprintf(&uart_out_onlcr, "%c\n"
                 "hv: invalid character in client number\n", c);
          debug_string_state = 0;
        }
        break;
      }
      break;

    default:
      assert(debug_string_state >= 0);

      //
      // Check to see if this character adds to a partially-matched debug
      // string.  We loop because we may back up the match process to an
      // earlier spot in the debug string, and try again with the same
      // input character.
      //
      while (1)
      {
        if (debug_string_len && c == debug_string[debug_string_state])
        {
          //
          // We matched.  Record that, and then if we've matched the whole
          // string, make the appropriate state transition.
          //
          debug_string_state++;
          if (debug_string_state == debug_string_len)
          {
            debug_string_state = DEBUG_ALL_CHARS_MATCHED;
          }
          break;
        }
        else if (!debug_string_state)
        {
          //
          // We didn't match (from the previous if), and there's no
          // partially-matched input ahead of us, so send this to the input.
          //
          uart_add_to_buf(&inbuf, c);
          break;
        }
        else
        {
          //
          // This character doesn't match.  Use the shift table to move the
          // characters that can no longer be part of the debug string to the
          // input buffer.  See the comment in init_uart_debug_string for an
          // explanation of how this works.
          //
          int nshift = debug_string_shift[debug_string_state];

          for (int i = 0; i < nshift; i++)
            uart_add_to_buf(&inbuf, debug_string[i]);

          debug_string_state -= nshift;
        }
        break;
      }
    }
  }
}


/** UART interrupt routine.
 */
void
uart_intr(void* intarg, void* msg, int len)
{
  UART_INTERRUPT_STATUS_t intr_stat;

  intr_stat.word = cfg_rd(uart_dest, uart_chan, UART_INTERRUPT_STATUS);

  // Handle receive interrupt.
  if (intr_stat.rfifo_we)
  {
    //
    // First, drain the UART.
    //
    uart_drain_input();

    //
    // Reset the "byte received" interrupt so we get it again.
    //
    cfg_wr(uart_dest, uart_chan, UART_INTERRUPT_STATUS,
           UART_INTERRUPT_STATUS__RFIFO_WE_MASK);

    //
    // Finally, look for more input characters in case some came in between
    // the drain and the interrupt reset.  (If any come in _after_ the interrupt
    // reset, that's OK, since we'll get another interrupt for them later.)
    //
    uart_drain_input();

    // Set the console IPI flag when characters received from the UART.
    SET_PENDING_IPI_INPUT();
  }

  // Handle transmit interrupt.
  if (intr_stat.tfifo_aempty)
  {
    uart_fill_txfifo(&outbuf);

    //
    // Reset the "tx fifo almost empty" interrupt so we get it again.
    //
    cfg_wr(uart_dest, uart_chan, UART_INTERRUPT_STATUS,
           UART_INTERRUPT_STATUS__TFIFO_AEMPTY_MASK);
  }

  // Send an IPI to the client if characters received or
  // the last output request from the client was not fully met, because of
  // insufficient buffering.
  if (console_ipi_pending != 0 && console_ipi_addr != 0)
  {
    if (intr_stat.rfifo_we)
      CLR_PENDING_IPI_INPUT();
    if (intr_stat.tfifo_aempty)
      CLR_PENDING_IPI_OUTPUT();

    cfg_wr(my_ipi_pos.word, 0, console_ipi_addr, 0);
  }
}


/** Enable UART interrupts.
 */
void
enable_uart_intr()
{
  //
  // We haven't yet implemented UART interrupts in gsim.
  //
  if (sim_is_simulator())
    return;

  //
  // Allocate the input and output buffers.
  // If we can't get the memory we just return without enabling interrupts.
  //
  char *bufs = local_alloc(2 * UART_BUF_SIZE, sizeof (char));
  if (!bufs)
    return;

  inbuf.buf = bufs;
  outbuf.buf = bufs + UART_BUF_SIZE;
  inbuf.rptr = 0;
  inbuf.wptr = 0;
  outbuf.rptr = 0;
  outbuf.wptr = 0;

  //
  // Install our interrupt handler.  Note that since we aren't actually a
  // device driver, we use a statically-allocated channel number; this will
  // change eventually.
  //
  if (drv_register_intr(uart_intr, NULL, DRV_INTR_DELAYED, DRV_CHAN_UART))
    return;

  //
  // Note that we're in interrupt mode.
  //
  uart_intr_enabled = 1;

  //
  // Set up the shim to send interrupts to us.  The register that we need
  // to write for this is in the base RShim itself, not the UART shim.
  //
  RSH_INT_BIND_t rib =
  {{
    .dev_sel = uart_bind_dev_sel,
    .evt_num = DRV_CHAN_UART,
    .int_num = HV_PL,
    .tileid = my_pos.bits.x << 4 | my_pos.bits.y,
    .mode = 0,
    .enable = 1,
  }};

  cfg_wr(uart_dest, 0, RSH_INT_BIND, rib.word);


  // Set the tx fifo almost empty threshold.
  UART_BUFFER_THRESHOLD_t buf_threshold;
  buf_threshold.word = cfg_rd(uart_dest, uart_chan, UART_BUFFER_THRESHOLD);
  buf_threshold.tfifo_aempty = 16;
  cfg_wr(uart_dest, uart_chan, UART_BUFFER_THRESHOLD, buf_threshold.word);

  // Reset the "byte received" and "tx fifo almost empty" interrupts in case
  // they are set.
  cfg_wr(uart_dest, uart_chan, UART_INTERRUPT_STATUS,
         UART_INTERRUPT_STATUS__RFIFO_WE_MASK |
         UART_INTERRUPT_STATUS__TFIFO_AEMPTY_MASK);

  // Enable the "byte received" and "tx fifo almost empty" interrupts.
  UART_INTERRUPT_MASK_t intr_mask;
  intr_mask.word = cfg_rd(uart_dest, uart_chan, UART_INTERRUPT_MASK);
  intr_mask.rfifo_we = 0;
  intr_mask.tfifo_aempty = 0;
  cfg_wr(uart_dest, uart_chan, UART_INTERRUPT_MASK, intr_mask.word);

  //
  // Force interrupt mode unless we're using protocol mode for debugging.
  //
  if (debug_flags & DEBUG_UART_PROTO)
    uart_enter_protocol_mode(0);
  else
    cfg_wr(uart_dest, uart_chan, UART_MODE,
           UART_MODE__UART_MODE_MASK | UART_MODE__BYPASS_MASK);
}


/** Get the free slot number of the buffer.
 * @param buf Pointer to the buffer structure.
 * @return Free slot number.
 */
static int
uart_buf_free_slot(uart_buf_t *buf)
{
  if (buf->wptr >= buf->rptr)
    return (UART_BUF_SIZE - 1) - (buf->wptr - buf->rptr);
  else
    return (buf->rptr - buf->wptr) - 1;
}


/** Read a character from the buffer.
 * @return Character read, or -1 if no character is available.
 */
static int
uart_get_from_buf(uart_buf_t *buf)
{
  if (buf->rptr == buf->wptr)
    return (-1);

  int retval = (unsigned char) buf->buf[buf->rptr];
  if (buf->rptr >= UART_BUF_SIZE - 1)
    buf->rptr = 0;
  else
    buf->rptr++;

  return (retval);
}
