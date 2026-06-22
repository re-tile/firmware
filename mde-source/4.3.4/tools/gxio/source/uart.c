/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

/**
 * @file
 * Implementation of UART gxio calls.
 */


#include "gxio/uart.h"
#include "uart_rpc_call.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/mman.h>
#include <sys/stat.h>



int
gxio_uart_init(gxio_uart_context_t* context, int uart_index)
{
  char file[32];
  int fd;
  

  snprintf(file, sizeof (file), "/dev/iorpc/uart%d", uart_index);

  fd = open(file, O_RDWR);

  if (fd < 0)
  {
    return -errno;
  }

  context->fd = fd;

  /* Map in our MMIO space. */
  context->mmio_base =
    mmap(NULL, HV_UART_MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (context->mmio_base == MAP_FAILED)
  {
    close(fd);
    context->fd = -1;
    return -errno;
  }


  return 0;
}


int
gxio_uart_destroy(gxio_uart_context_t* context)
{
  munmap(context->mmio_base, HV_UART_MMIO_SIZE);
  close(context->fd);

  context->mmio_base = NULL;
  context->fd = -1;

  return 0;
}


/** UART register write wrapper. */
void
gxio_uart_write(gxio_uart_context_t *context, uint64_t offset, uint64_t word)
{
	__gxio_mmio_write(context->mmio_base + offset, word);
}


/** UART register read wrapper. */
uint64_t
gxio_uart_read(gxio_uart_context_t *context, uint64_t offset)
{
	return __gxio_mmio_read(context->mmio_base + offset);
}
