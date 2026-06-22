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
 *
 * Implementation of USB device gxio calls.
 */


#include "gxio/usb_dev.h"
#include "usb_dev_rpc_call.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/mman.h>
#include <sys/stat.h>


int
gxio_usb_dev_init(gxio_usb_dev_context_t* ctx, int usb_dev_index)
{
  char file[32];
  int fd, i;

  memset(ctx, 0, sizeof (*ctx));
  for (i = 0; i < 2 * HV_USB_DEV_NUM_EP; i++)
    ctx->fifo_offset[i] = 0x800;


  snprintf(file, sizeof (file), "/dev/iorpc/usb_dev%d", usb_dev_index);

  fd = open(file, O_RDWR);

  if (fd < 0)
  {
    return -errno;
  }

  ctx->fd = fd;

  // Map in our MMIO space.
  ctx->mmio_base =
    mmap(NULL, HV_USB_DEV_MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
         fd, 0);
  if (ctx->mmio_base == MAP_FAILED)
  {
    close(fd);
    return -errno;
  }


  return 0;
}


int
gxio_usb_dev_destroy(gxio_usb_dev_context_t* ctx)
{
  munmap(ctx->mmio_base, HV_USB_DEV_MMIO_SIZE);
  close(ctx->fd);

  ctx->mmio_base = NULL;
  ctx->fd = -1;

  return 0;
}


void *
gxio_usb_dev_get_reg_start(gxio_usb_dev_context_t* ctx)
{
  return ctx->mmio_base;
}


size_t
gxio_usb_dev_get_reg_len(gxio_usb_dev_context_t* ctx)
{
  return HV_USB_DEV_MMIO_SIZE;
}


//
// delta is new fifo size in words minus old fifo size in words.
//
void
__gxio_usb_dev_adjust_fifo_ptrs(gxio_usb_dev_context_t* ctx, int ep,
                                int is_tx, int delta)
{
  int i;

  for (i = 1 + ep + (is_tx ? HV_USB_DEV_NUM_EP : 0);
       i < 2 * HV_USB_DEV_NUM_EP; i++)
    ctx->fifo_offset[i] += 4 * delta;
}


void
gxio_usb_dev_read_rx_fifo(gxio_usb_dev_context_t* ctx, int ep, void* dest,
                          int len)
{
  int inptr;

  if (len <= 0)
  {
    gxio_usb_dev_rd_cfrm(ctx, ep);
    return;
  }

  if (((uintptr_t) dest & 3) == 0)
  {
    for (inptr = 0; len >= 4; len -= 4, inptr += 4)
    {
      uint32_t inword = __gxio_mmio_read32(ctx->mmio_base +
                                           ctx->fifo_offset[ep] + inptr);
      *((uint32_t*) dest) = inword;
      dest += 4;
    }
  }
  else if (((uintptr_t) dest & 1) == 0)
  {
    for (inptr = 0; len >= 4; len -= 4, inptr += 4)
    {
      uint32_t inword = __gxio_mmio_read32(ctx->mmio_base +
                                           ctx->fifo_offset[ep] + inptr);
      *((uint16_t*) dest) = inword;
      dest += 2;
      *((uint16_t*) dest) = inword >> 16;
      dest += 2;
    }
  }
  else
  {
    for (inptr = 0; len >= 4; len -= 4, inptr += 4)
    {
      uint32_t inword = __gxio_mmio_read32(ctx->mmio_base +
                                           ctx->fifo_offset[ep] + inptr);
      *((uint8_t*) dest++) = inword;
      *((uint8_t*) dest++) = inword >> 8;
      *((uint8_t*) dest++) = inword >> 16;
      *((uint8_t*) dest++) = inword >> 24;
    }
  }

  //
  // This handles any non-full-word residue.
  //
  for (; len; len--)
    *(uint8_t*) dest++ =
      __gxio_mmio_read8(ctx->mmio_base + ctx->fifo_offset[ep] + inptr++);
}

void
gxio_usb_dev_write_tx_fifo(gxio_usb_dev_context_t* ctx, int ep, void* src,
                           int len)
{
  int outptr;

  if (((uintptr_t) src & 3) == 0)
  {
    for (outptr = 0; len >= 4; len -= 4, outptr += 4)
    {
      uint32_t outword = *((uint32_t*) src);
      src += 4;
      __gxio_mmio_write32(ctx->mmio_base +
                          ctx->fifo_offset[HV_USB_DEV_NUM_EP + ep] +
                          outptr, outword);
    }
  }
  else if (((uintptr_t) src & 1) == 0)
  {
    for (outptr = 0; len >= 4; len -= 4, outptr += 4)
    {
      uint32_t outword = *((uint16_t*) src);
      src += 2;
      outword |= *((uint16_t*) src) << 16;
      src += 2;
      __gxio_mmio_write32(ctx->mmio_base +
                          ctx->fifo_offset[HV_USB_DEV_NUM_EP + ep] +
                          outptr, outword);
    }
  }
  else
  {
    for (outptr = 0; len >= 4; len -= 4, outptr += 4)
    {
      uint32_t outword = *(uint8_t*) src++;
      outword |= *(uint8_t*) src++ << 8;
      outword |= *(uint8_t*) src++ << 16;
      outword |= *(uint8_t*) src++ << 24;
      __gxio_mmio_write32(ctx->mmio_base +
                          ctx->fifo_offset[HV_USB_DEV_NUM_EP + ep] +
                          outptr, outword);
    }
  }

  //
  // This handles any non-full-word residue.
  //
  for (; len; len--)
    __gxio_mmio_write8(ctx->mmio_base +
                       ctx->fifo_offset[HV_USB_DEV_NUM_EP + ep] +
                       outptr++, *(uint8_t*) src++);

  gxio_usb_dev_wr_cfrm(ctx, ep);
}
