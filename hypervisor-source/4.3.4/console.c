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

#include <stdio.h>
#include <string.h>

#include <arch/ipi.h>
#include <arch/sim.h>
#include <hv/hypervisor.h>

#include "client_obj.h"
#include "config.h"
#include "console.h"
#include "debug.h"
#include "fault.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "misc.h"

/** IPI event number for console interrupt. */
PA console_ipi_addr _SHARED;

/** Pending console IPI flag. */
int console_ipi_pending _SHARED;

/** Write a character to the console.
 * @param byte Character to write.
 */
void
syscall_console_putc(int byte)
{
  char c = byte & 0xFF;
  putchar(c);
  fflush(stdout);
}

#ifdef CONSOLE_DEBUG
char last_inbuf[64 * 1024];
int last_inbuf_pos = 0;
#endif

/** Write a string to the console.
 * @param bytes Pointer to characters to write.
 * @param len Number of characters to write.
 * @return Number of characters written, or a negative error code.
 */
int
syscall_console_write(char* bytes, int len)
{
  int retval = 0;

  SYSCALL_TRACE("console_write(bytes=%p, len=%d)\n", bytes, len);

  while (len)
  {
    char buf[256];

    int bytesthispass = len;
    if (bytesthispass > sizeof (buf))
      bytesthispass = sizeof (buf);

    ON_FAULT_RETURN_EFAULT(bytes, len);
    memcpy(buf, bytes, bytesthispass);
    FAULT_END();

    int bytes_written = client_stdout->ops->write(buf, bytesthispass,
                                                  0, client_stdout->pvt);

#ifdef CONSOLE_DEBUG
    int last_avail = sizeof (last_inbuf) - last_inbuf_pos;
    int last_bytes = bytes_written;
    if (last_bytes > last_avail)
      last_bytes = last_avail;

    memcpy(&last_inbuf[last_inbuf_pos], buf, last_bytes);
    flush_range((VA) &last_inbuf[last_inbuf_pos], last_bytes);
    last_inbuf_pos += last_bytes;
    flush_range((VA) &last_inbuf_pos, 4);
#endif

    bytes += bytes_written;
    len -= bytes_written;
    retval += bytes_written;

    if (bytes_written < bytesthispass)
      break;
  }

  fflush(client_stdout);
  return (retval);
}


/** Read a character from the console if available.
 * @return byte Character read, or -1 if none available.
 */
int
syscall_console_read_if_ready()
{
  int retval = getc(client_stdin);
#ifdef DEBUG
  if (retval >= 0)
    SYSCALL_TRACE("console_read_if_ready returns %d\n", retval);
#endif
  return (retval);
}


/** Configure the console interrupt.
 * @param ipi Index of the IPI register which will receive the interrupt.
 * @param event IPI event number for console interrupt. If less than 0,
 *        disable the console IPI interrupt.
 * @param coord Tile to be targeted for console interrupt.
 * @return 0 on success, otherwise, HV_EINVAL if illegal parameter,
 *         HV_ENOTSUP if console interrupt are not available.
 */
int
syscall_console_set_ipi(int ipi, int event, HV_Coord coord)
{
  Lotar client_lotar = HV_XY_TO_LOTAR(coord.x, coord.y);
  Lotar real_lotar;

  // Console interrupts not supported in gsim yet.
  if (sim_is_simulator())
    return HV_ENOTSUP;

  // Verify "x" and "y".
  if (c2r_lotar(client_lotar, &real_lotar))
    return (HV_EINVAL);

  // Verify "ipi".
  if (ipi > CLIENT_PL)
    return (HV_EINVAL);

  if (event >= CHIP_IPI_EVENTS())
    return HV_EINVAL;

  //
  // We don't support console interrupts with multiple clients.
  //
  if (config.nregclients > 1)
    return HV_ENOTSUP;

  if (event >= 0)
  {










    IPI_REMOTE_TRIGGER_ADDR_t addr = {{
        .tile_y = HV_LOTAR_Y(real_lotar),
        .tile_x = HV_LOTAR_X(real_lotar),
        .ipi = ipi,
        .event = event,
    }};


    console_ipi_addr = addr.word;
  }
  else
    console_ipi_addr = 0;

  return 0;
}
