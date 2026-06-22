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
 * Routines to manage the console.
 */

#ifndef _SYS_HV_CONSOLE_H
#define _SYS_HV_CONSOLE_H

#include "types.h"
#include "hv/hypervisor.h"

void syscall_console_putc(int byte);
int syscall_console_write(char* bytes, int len);
int syscall_console_read_if_ready(void);
int syscall_console_set_ipi(int ipi, int event, HV_Coord coord);

//
// Define all of our possible consoles.
//


// Null (throws away output, never provides input).  Used very early in
// the boot process.
extern FILE null_out;
extern FILE null_in;

// The UART.  Normally referenced by the switchable console and not used
// directly.
extern FILE uart_out;
extern FILE uart_out_onlcr;
extern FILE uart_in;

// Hypervisor output to a remote tile.  Used as stdout/in on all tiles
// except the console master tile.
extern FILE remote_out;
extern FILE remote_in;

// Client supervisor output to a remote tile.  Used as client_stdout/in
// on all tiles except the console master tile.
extern FILE remote_client_out;
extern FILE remote_client_in;

// Client supervisor output to this tile.  Used as client_stdout/in
// on the console master tile.
extern FILE client_out;
extern FILE client_in;

// The rshim early console.  Used as stdout/in on the console master tile
// only during the first part of boot, before the boot stream is consumed,
// when the rshim early console is active.  Note that this only used by the
// hypervisor, not the client, so there is no non-onlcr version.
extern FILE rshim_out_onlcr;
extern FILE rshim_in;

// The tile-monitor FIFO.  Normally referenced by the switchable console and
// not used directly.
extern FILE tmfifo_out;
extern FILE tmfifo_out_onlcr;
extern FILE tmfifo_in;

// The switchable console, which dispatches read/write requests to either
// the UART, or the tile-monitor FIFO, depending on whether the latter is
// currently enabled.  Used as stdout/in on the console master tile.
extern FILE switch_out;
extern FILE switch_out_onlcr;
extern FILE switch_in;

//
// These are indirect (i.e., they're always set to one of the consoles
// above).
//

// Standard output/input for client supervisors.
extern FILE* client_stdout;
extern FILE* client_stdin;

// Real output/input devices for the client supervisors.  These are used
// by the code which handles requests to [remote_]client_{in,out}.
extern FILE* client_outdev;
extern FILE* client_indev;

// PA to send IPI for console event, zero means that we won't send any IPI.
extern PA console_ipi_addr;

// Pending console IPI flag, zero to clear. If the flag is not clear,
// hypervisor will send an IPI to the client in the following situations:
// - When the output buffer space is available, in case of the
//   PENDING_IPI_OUTPUT flag was set; or
// - When the console device input interrupt handling is done, in case of the
//   PENDING_IPI_INPUT flag was set.
extern int console_ipi_pending;

#define PENDING_IPI_OUTPUT        1 /**< Pending console IPI on output. */
#define PENDING_IPI_INPUT         2 /**< Pending console IPI on input. */
/** Is pending console IPI output flag set? */
#define IS_PENDING_IPI_OUTPUT     (console_ipi_pending & PENDING_IPI_OUTPUT)
/** Is pending console IPI input flag set? */
#define IS_PENDING_IPI_INPUT      (console_ipi_pending & PENDING_IPI_INPUT)
/** Set pending console IPI output flag. */
#define SET_PENDING_IPI_OUTPUT()  (console_ipi_pending |= PENDING_IPI_OUTPUT)
/** Set pending console IPI input flag. */
#define SET_PENDING_IPI_INPUT()   (console_ipi_pending |= PENDING_IPI_INPUT)
/** Clear pending console IPI output flag. */
#define CLR_PENDING_IPI_OUTPUT()  (console_ipi_pending &= ~(PENDING_IPI_OUTPUT))
/** Clear pending console IPI input flag. */
#define CLR_PENDING_IPI_INPUT()   (console_ipi_pending &= ~(PENDING_IPI_INPUT))
/** Clear pending console IPI flag. */
#define CLR_PENDING_IPI_ALL()     (console_ipi_pending = 0)

extern int cons_write_to_output_buffer(int client, const char* buf, int len);
extern int cons_read_from_input_buffer(int client, char* buf, int len);
extern int cons_set_active_client(int new_client, int silent);

/** When passed to cons_set_active_client as the new client number, move
 *  the console to the next client. */
#define CONS_NEXT_CLIENT -1

/** When passed to cons_set_active_client as the new client number, move
 *  the console to the previous client. */
#define CONS_PREV_CLIENT -2

extern int cons_client_is_active(int client);
extern void cons_alloc_output_buffers(void);

#endif /* _SYS_HV_CONSOLE_H */
