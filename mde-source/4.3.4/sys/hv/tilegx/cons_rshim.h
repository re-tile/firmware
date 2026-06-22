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
 * Routines to manage the rshim early console.
 */

#ifndef _SYS_HV_TILEGX_CONS_RSHIM_H
#define _SYS_HV_TILEGX_CONS_RSHIM_H

#include "types.h"


//
// Rshim Early Console Purpose and Protocol
//
// The rshim early console is used to provide console services for the
// booter, and the very early stages of the hypervisor, when we cannot use
// the UART.  We do this in two cases:
//
// - When we are booting over the console UART via the BTK.  This allows
//   the BTK to read chip registers during the boot without being confused by
//   console output, and that in turn means we can use the rshim boot FIFO's
//   flow control features, allowing us to improve both boot speed and
//   robustness.
//
// - When we are running the console over the tile-monitor FIFO (e.g.,
//   console-over-USB).  In this case, we don't have a connection to the
//   UART at all, so we obviously can't use it.
//
// Data is conveyed between the tile and host via circular buffers, kept in
// the rshim scratch buffer.  We use scratchpad registers from the UART
// shims to configure the console and to hold the pointers which turn the
// scratch buffer into a pair of producer-consumer queues.  This means that
// the host must read a scratchpad register periodically to see whether new
// console output is available; while polling is not ideal, it's acceptable
// for the short time the rshim console is used.
//
// The host controls whether the rshim console is enabled, and, if so, the
// sizes of the circular buffers.  (This allows the use of the entire
// buffer for output if the host chooses not to support console input, as
// is true for the BTK boot-over-UART support; it also allows us to decide
// to use part of the scratch buffer for other purposes in the future.) It
// does this by writing the UART 1 scratchpad register, after the chip has
// been reset but before the boot stream is injected.  Bit 31 of the
// register, if set, tells the tile to use the rshim console for output.
// Bits [30:28] contain the log base 2 of the number of words of the
// scratch buffer to use for the console's circular output buffer, so a
// value of 7 uses the entire 128-word area.  Similarly, bit 27 tells the
// tile to use the rshim console for input, and bits [26:24] contain the
// log base 2 of the number of words of the scratch buffer to use for the
// console's circular input buffer.  The output circular buffer starts at
// word 0 of the scratch buffer, and is immediately followed by the input
// circular buffer.
//
// The circular buffers themselves are standard producer-consumer queues.
// For output, there is a write pointer, updated by the tile and read by
// the host, and a read pointer, updated by the host and read by the tile.
// The write pointer resides in the low 10 bits of the UART 0 scratchpad,
// while the read pointer resides in the low 10 bits of the UART 1
// scratchpad.  Similarly, for input, the write pointer (updated by the
// host and read by the tile) is in bits [21:12] of the UART 1 scratchpad,
// and the read pointer (updated by the tile and read by the host) is in
// bits [21:12] of the UART 0 scratchpad.  Each pointer addresses a byte of
// the circular buffer, relative to its base, so if the entire buffer were
// split evenly between input and output, all pointers would range from 0
// to 511 and then back to zero.  Each pointer points to the next byte to
// be read or written, respectively.  If the pointers are equal, the buffer
// is empty.
//
// The rshim scratch buffer is not random-access; it has a pointer and data
// register, both of which must be used to move words in and out.  This
// means that the host and tile side may not simultaneously access the
// scratch buffer.  To ensure this, the first rshim semaphore (semaphore0)
// is used as a lock.  Neither side may read or write either of the rshim
// scratch buffer registers unless it holds this semaphore.  "Holding"
// means that you read the semaphore, and got zero; if you get another
// value, the semaphore is currently held by the other side and you must
// try again.  When done with the scratch buffer, the semaphore is released
// by writing a zero to it.
//
// The tile is responsible for closing the console as soon as is practical
// after the boot stream has been consumed, and switching to the UART or
// the tile-monitor FIFO for console output.  This is done by setting bit
// 10 in the UART 0 scratchpad.  The host is responsible for acknowledging
// this request after it has ceased to use the UART for any BTK operations;
// it does so by setting bit 10 in the UART 1 scratchpad.
//
// Bit 23 of the UART 1 scratchpad is used as a flag to tell the tile
// that it should use the tile-monitor FIFO console after it closes the
// rshim early console.
//
// Bits [4:0] of RSH_BREADCRUMB1 are used to tell the tile that it should
// wait the given number of seconds for the early console to be enabled
// before continuing with the boot.  This is typically not necessary, since
// the UART 1 scratchpad is set before the boot stream is injected;
// however, that is not possible when a system is booting from SROM.  The
// breadcrumb register is used since it is not reset upon soft reset.
//
// Bits in the two UART scratchpad registers which are not already
// allocated by the protocol as described above are reserved; they must be
// ignored on read and written as zero.
//
// Bits in the breadcrumb registers which are not already allocated by the
// protocol as described above are reserved; they must be ignored on read
// and preserved on write.
//

//
// Protocol bit definitions.  If these are changed, the corresponding BTK
// code (see check_console() and boot_load_wordlist() in tools/gxbtk/chip.py)
// and host-side USB driver code (see tools/drivers/tilegx/usb) will also
// need to be changed.
//

//
// Tile-to-host bits (UART 0 scratchpad).
//
/** Output write pointer mask.  Note that this is the maximum size; the
 *  write pointer may be smaller if requested by the host. */
#define CONS_RSHIM_T2H_OUT_WPTR_MASK     0x3FF

/** Tile is done mask. */
#define CONS_RSHIM_T2H_DONE_MASK         0x400

/** Input read pointer mask.  Note that this is the maximum size; the read
 *  pointer may be smaller if requested by the host. */
#define CONS_RSHIM_T2H_IN_RPTR_MASK      0x1FF800

/** Input read pointer shift. */
#define CONS_RSHIM_T2H_IN_RPTR_SHIFT     11

/** Tile is done mask. */
#define CONS_RSHIM_T2H_DONE_MASK         0x400

//
// Host-to-tile bits (UART 1 scratchpad).
//
/** Output enable mask. */
#define CONS_RSHIM_H2T_OUT_ENABLE_MASK   0x80000000

/** Output buffer size mask.  This is the log base 2 of the buffer
 *  size in words. */
#define CONS_RSHIM_H2T_OUT_BUFSIZ_MASK   0x70000000

/** Output buffer size shift. */
#define CONS_RSHIM_H2T_OUT_BUFSIZ_SHIFT  28

/** Input enable mask. */
#define CONS_RSHIM_H2T_IN_ENABLE_MASK    0x8000000

/** Input buffer size mask.  This is the log base 2 of the buffer
 *  size in words. */
#define CONS_RSHIM_H2T_IN_BUFSIZ_MASK    0x7000000

/** Input buffer size shift. */
#define CONS_RSHIM_H2T_IN_BUFSIZ_SHIFT   24

/** Tile should use TMFIFO console once early console is closed. */
#define CONS_RSHIM_H2T_USE_TMF_CON_MASK  0x800000

/** Output buffer read pointer mask.  Note that this is the maximum size;
 *  the read pointer may be smaller if requested by the host. */
#define CONS_RSHIM_H2T_OUT_RPTR_MASK     0x3FF

/** Input write pointer mask.  Note that this is the maximum size; the read
 *  pointer may be smaller if requested by the host. */
#define CONS_RSHIM_T2H_IN_WPTR_MASK      0x1FF800

/** Input write pointer shift. */
#define CONS_RSHIM_T2H_IN_WPTR_SHIFT     11

/** Host is done mask. */
#define CONS_RSHIM_H2T_DONE_MASK         0x400

//
// Host to tile bits (rshim breadcrumb 1).
//
/** Seconds to wait for rshim early console before giving up and using the
 *  UART. */
#define CONS_RSHIM_BC1_DELAY_MASK        0x1F

int rshim_console_init(int* use_tmf_con);
void rshim_console_fini(int wait);
void rshim_putchar(char c);
int rshim_getchar_timeout(int msecs);
void rshim_flush_output(void);

#endif /* _SYS_HV_TILEGX_CONS_RSHIM_H */
