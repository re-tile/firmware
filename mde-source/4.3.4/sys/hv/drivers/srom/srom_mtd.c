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
 * SPI Flash ROM driver.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <arch/srom.h>


#include "sys/libc/include/util.h"

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "fault.h"
#include "hv.h"
#include "srom.h"
#include "srom_acc.h"
#include "types.h"

#ifdef SROM_DEBUG
/** Page-level debug tracing output */
#define SROM_TRACE tprintf
#else
/** No debug tracing output */
#define SROM_TRACE(...)
#endif



/** SPI Flash ROM driver probe routine. */
static int
srom_mtd_probe(const char* drvname, int instance,
           pos_t tile, const struct dev_info* info)
{
  //
  // Save info pointer in global variable for use by board info block code.
  //
  srom_info = info;

  return (0);
}


/** SPI Flash ROM driver init routine. */
static int
srom_mtd_init(const char* drvname, void** statepp, int instance, int tileno,
          pos_t tile, const struct dev_info* info, const char* args)
{
  //
  // Allocate our state.
  //
  srom_state_t* ss = drv_state_zalloc(sizeof (*ss), 0);
  *statepp = ss;

  //
  // If we're a shared tile, allocate more state.
  //
  if (tileno < 0)
  {
    ss->mst_state = drv_state_zalloc(sizeof(*(ss->mst_state)), 0);
    ss->mst_state->s_state = ss;
  }

  ss->my_pos = tile;
  ss->infop = info;
  ss->fwd_tile = info->stiles[0];
  ss->fwd = (ss->fwd_tile.word != tile.word);

  //
  // If we're the shared tile, get the flash characteristics.
  //
  if (tileno < 0)
  {
    srom_mst_state_t* ms = ss->mst_state;

    if (!srom_getinfo(ss->infop->idn_ports[0], ss->infop->channel, &ms->srom_id,
        &ms->page_size, &ms->sector_size, &ms->srom_size))
    {
      printf("hv_warning: unknown SROM device type %#llx, ignoring\n",
             ms->srom_id);
      return (HV_ENODEV);
    }

    ms->srom_dev = srom_get_dev(ss->infop->idn_ports[0], ss->infop->channel);

    //
    // If the BIB came from SROM, it's in the last sector; don't hand that
    // out to Linux.
    //
    if (bi_in_srom())
      ms->srom_size -= ms->sector_size;

    ms->pages_per_sector = ms->sector_size / ms->page_size;
  }

  return (0);
}


/** SPI Flash ROM driver open routine. */
static int
srom_mtd_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
          pos_t tile)
{
  srom_state_t* ss = statep;

  DEVICE_TRACE("srom_mtd_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ss->fwd)
    return (drv_open_remote(devhdl, suffix, flags, ss->fwd_tile));

  //
  // If we're here, we're the shared tile.
  //
  if (!strcmp(suffix, "/mtd"))
    return (0);

  return (HV_ENODEV);
}


/** SPI Flash ROM driver close routine. */
static int
srom_mtd_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("srom_mtd_close: devhdl %#x\n", devhdl);

  // Nothing to do on close.

  return (0);
}

/** SPI Flash ROM driver read routine.  */
static int
srom_mtd_pread(int devhdl, void* statep, uint32_t flags, char* va,
           uint32_t len, uint64_t offset, pos_t tile)
{
  srom_state_t* ss = statep;
  srom_mst_state_t* ms = ss->mst_state;
  int rv;
  void *rd_buf;

  DEVICE_TRACE("srom_mtd_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ss->fwd)
    return (drv_pread_remote(devhdl, flags, va, len, offset, ss->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // If the memory buffer is bad, we need to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  //
  // Handle the special case of reading the device length.
  //
  if (offset == SROM_TOTAL_SIZE_OFF)
  {
    if (len != sizeof (ms->srom_size))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &ms->srom_size, len, flags))
       return (HV_EFAULT);

    return (len);
  }

  //
  // Handle the special case of reading the sector length.
  //
  if (offset == SROM_SECTOR_SIZE_OFF)
  {
    if (len != sizeof (ms->sector_size))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &ms->sector_size, len, flags))
       return (HV_EFAULT);

    return (len);
  }

  //
  // Handle the special case of reading the page length.
  //
  if (offset == SROM_PAGE_SIZE_OFF)
  {
    if (len != sizeof (ms->page_size))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &ms->page_size, len, flags))
       return (HV_EFAULT);

    return (len);
  }

  //
  // The SROM-MTD driver only supports single sector reads from page
  // boundaries.  If this becomes a performance issue, it would be trivial
  // to crank up the page count -- but in the mean time, KISS...
  //
  if (len != ms->page_size)
    return (HV_EINVAL);

  if (offset & (ms->page_size - 1))
    return (HV_EINVAL);

  //
  // Read buffer can be stacked, since it's nice and small (256 bytes).
  //
  rd_buf = alloca(ms->page_size);
  rv = srom_rd(ms->s_state->infop->idn_ports[0],
               ms->s_state->infop->channel, ms->srom_dev,
               offset, len, rd_buf);
  if (rv != len)
  {
    printf("hv_warning: unexpected error %d from srom_rd\n", rv);
    return(HV_EIO);
  }

  //
  // Now copy the data from the sector buffer to the user buffer.
  //
  if (drv_copy_to_client(va, rd_buf, len, flags))
    return (HV_EFAULT);

  return (len);
}


/** SPI Flash ROM driver write routine. */
static int
srom_mtd_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  srom_state_t* ss = statep;
  srom_mst_state_t* ms = ss->mst_state;
  int rv;
  void *wr_buf;

  DEVICE_TRACE("srom_mtd_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ss->fwd)
    return (drv_pwrite_remote(devhdl, flags, va, len, offset, ss->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // If the memory buffer is bad, we need to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  //
  // If they're asking for an erase, we'll do that.
  //
  if ((offset & 0xFF000000) == SROM_ERASE_OFF)
  {
    offset &= 0xFFFFFF;

    //
    // Offset must be the very start of a sector.
    //
    if (offset & (ms->sector_size - 1))
      return (HV_EINVAL);

    rv = srom_erase(ms->s_state->infop->idn_ports[0],
                    ms->s_state->infop->channel, ms->srom_dev,
                    offset);
    if (rv != 0)
    {
      printf("hv_warning: unexpected error %d from srom_erase\n", rv);
      return (HV_EIO);
    }

    return (len);
  }

  //
  // If we're here, we didn't do an erase; we're doing a real write.
  //

  //
  // The SROM-MTD driver only supports single sector writes on page
  // boundaries.  If this becomes a performance issue, it would be trivial
  // to crank up the page count -- but in the mean time, KISS...
  //
  if (len != ms->page_size)
    return (HV_EINVAL);

  if (offset & (ms->page_size - 1))
    return (HV_EINVAL);

  //
  // Write buffer can be stacked, since it's nice and small (256 bytes).  We
  // can always use copy_from_client, since we're not probing for 0->1
  // transitions like we do in the normal SROM driver.
  //
  wr_buf = alloca(ms->page_size);

  if (drv_copy_from_client(wr_buf, va, len, flags))
    return (HV_EFAULT);

  rv = srom_wr_pg(ms->s_state->infop->idn_ports[0],
                  ms->s_state->infop->channel, ms->srom_dev,
                  offset, len, wr_buf);
  if (rv != len)
  {
    printf("hv_warning: unexpected error %d from srom_wr_pg\n", rv);
    return (HV_EIO);
  }

  return (len);
}


/** SPI Flash ROM driver operations vector */
static struct drv_ops srom_mtd_ops = {
  .probe       = srom_mtd_probe,
  .init        = srom_mtd_init,
  .open        = srom_mtd_open,
  .close       = srom_mtd_close,
  .pread       = srom_mtd_pread,
  .pwrite      = srom_mtd_pwrite,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_srom_mtd = {
  .shim_type  = SROM_DEV_INFO__TYPE_VAL_SROM,
  .name       = "srom_mtd",
  .desc       = "SPI Flash ROM for Linux MTD",
  .ops        = &srom_mtd_ops,
  .stilereq   = 1,
};
