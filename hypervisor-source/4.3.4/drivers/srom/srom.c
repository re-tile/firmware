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
srom_probe(const char* drvname, int instance,
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
srom_init(const char* drvname, void** statepp, int instance, int tileno,
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
  // If we're the shared tile, get the flash characteristics, then allocate
  // our sector state.
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

    ms->pages_per_sector = ms->sector_size / ms->page_size;
    ms->sector_buf = drv_state_alloc(ms->sector_size, 0);
    ms->page_valid = drv_state_zalloc(ms->pages_per_sector / 8, 0);
    ms->page_dirty = drv_state_zalloc(ms->pages_per_sector / 8, 0);

    //
    // Right now we break the SROM into four areas.  The last two sectors
    // are reserved for use by the board info block on the BuB, and may in
    // the future be used on the PCI-E card for hypervisor-owned modifiable
    // data, like UART configuration info; they aren't accessible via this
    // driver.  The two sectors before that are used for bootloader state,
    // and are assigned to srom/0/2.  The two sectors before that are
    // reserved for user/application data, and are assigned to srom/0/1.
    // The rest of the sectors are used for the bootable image itself,
    // and are assigned to srom/0/0.  We might eventually want to make
    // this changeable via an args line in the hv config.
    //
    ms->base[0] = 0;
    ms->size[0] = ms->srom_size - 6 * ms->sector_size;
    ms->base[1] = ms->base[0] + ms->size[0];
    ms->size[1] = 2 * ms->sector_size;
    ms->base[2] = ms->base[1] + ms->size[1];
    ms->size[2] = 2 * ms->sector_size;
  }

  return (0);
}


/** SPI Flash ROM driver open routine. */
static int
srom_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
          pos_t tile)
{
  srom_state_t* ss = statep;

  DEVICE_TRACE("srom_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ss->fwd)
    return (drv_open_remote(devhdl, suffix, flags, ss->fwd_tile));

  //
  // If we're here, we're the shared tile.
  //
  long partition;
  char* endptr;

  if (suffix[0] == '/' &&
      !str2l(&suffix[1], &endptr, 10, &partition) &&
      *endptr == '\0' &&
      partition < SROM_PARTITIONS)
    return (partition);

  return (HV_ENODEV);
}


/** SPI Flash ROM driver close routine. */
static int
srom_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("srom_close: devhdl %#x\n", devhdl);

  // Nothing to do on close.

  return (0);
}

//
// Helper routines for read/write.
//

/** Compute the length we'll actually act on by clipping it to not go past
 *  the end of the partition, or off the end of a sector; adjust the offset
 *  from the partition-relative value to its absolute value; and also compute
 *  the address of the first byte in the sector.
 * @param ms Master state pointer.
 * @param instance Device instance number.
 * @param len Pointer to requested length, may be modified on return.
 * @param offset Pointer to offset within ROM, may be modified on return.
 * @param secaddr Pointer to returned sector address.
 */
static void
clip_operation(srom_mst_state_t* ms, uint32_t instance,
               uint32_t* len, uint64_t* offset, uint32_t* secaddr)
{
  assert(instance < SROM_PARTITIONS);

  //
  // Don't try to write off of the end of the partition.
  //
  if (*offset >= ms->size[instance])
  {
    *len = 0;
    return;
  }

  if (*offset + *len > ms->size[instance])
    *len = ms->size[instance] - *offset;

  //
  // Convert to the absolute offset.
  //
  *offset += ms->base[instance];

  //
  // Compute sector address.
  //
  *secaddr = (*offset / ms->sector_size) * ms->sector_size;

  //
  // Don't try to write off of the end of the sector.
  //
  if (*offset + *len > *secaddr + ms->sector_size)
    *len = *secaddr + ms->sector_size - *offset;
}


/** Translate a byte offset to a page number.
 * @param ms Master state pointer.
 * @param offset Offset within ROM.
 * @return Page number within sector.
 */
static inline uint32_t
offset2page(srom_mst_state_t* ms, uint64_t offset)
{
  assert(offset >= ms->sector_addr &&
         offset < ms->sector_addr + ms->sector_size);
  return ((offset - ms->sector_addr) / ms->page_size);
}


/** Translate a byte offset to a pointer into the sector buffer.
 * @param ms Master state pointer.
 * @param offset Offset within ROM.
 * @return Pointer into sector buffer.
 */
static inline char*
offset2secbuf(srom_mst_state_t* ms, uint64_t offset)
{
  assert(offset >= ms->sector_addr &&
         offset < ms->sector_addr + ms->sector_size);
  return ((char*) ms->sector_buf + (offset - ms->sector_addr));
}


/** Translate a page number to a pointer into the sector buffer.
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @return Pointer into sector buffer.
 */
static inline char*
page2secbuf(srom_mst_state_t* ms, uint32_t page)
{
  return (offset2secbuf(ms, ms->sector_addr + page * ms->page_size));
}


/** Is page valid?
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @return Nonzero iff page is valid.
 */
static inline int
is_page_valid(srom_mst_state_t* ms, uint32_t page)
{
  return (ms->page_valid[page >> 3] & (1 << (page & 7)));
}


/** Is page dirty?
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @return Nonzero iff page is dirty.
 */
static inline int
is_page_dirty(srom_mst_state_t* ms, uint32_t page)
{
  return (ms->page_dirty[page >> 3] & (1 << (page & 7)));
}


/** Set page valid state.
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @param is_valid If 1, make page valid; if 0, make it invalid.
 */
static inline void
set_page_valid(srom_mst_state_t* ms, uint32_t page, int is_valid)
{
  if (is_valid)
    ms->page_valid[page >> 3] |= 1 << (page & 7);
  else
    ms->page_valid[page >> 3] &= ~(1 << (page & 7));
}


/** Set page dirty state.
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @param is_dirty If 1, make page dirty, if 0, make it clean.
 */
static inline void
set_page_dirty(srom_mst_state_t* ms, uint32_t page, int is_dirty)
{
  if (is_dirty)
  {
    ms->page_dirty[page >> 3] |= 1 << (page & 7);
    ms->sector_dirty = 1;
  }
  else
    ms->page_dirty[page >> 3] &= ~(1 << (page & 7));

}


/** Set all pages to be invalid.
 * @param ms Master state pointer.
 */
static inline void
set_all_pages_invalid(srom_mst_state_t* ms)
{
  memset(ms->page_valid, 0, ms->pages_per_sector / 8);
}


/** Set all pages to be clean.
 * @param ms Master state pointer.
 */
static inline void
set_all_pages_clean(srom_mst_state_t* ms)
{
  memset(ms->page_dirty, 0, ms->pages_per_sector / 8);
  ms->sector_dirty = 0;
}


/** Load a page.
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @return Nonzero iff the page could not be loaded.
 */
static inline int
load_page(srom_mst_state_t* ms, uint32_t page)
{
  if (!is_page_valid(ms, page))
  {
    SROM_TRACE("srom: loading page %d from offset %#x to addr %p\n", page,
               ms->sector_addr + page * ms->page_size, page2secbuf(ms, page));
    int rv = srom_rd(ms->s_state->infop->idn_ports[0],
                     ms->s_state->infop->channel, ms->srom_dev,
                     ms->sector_addr + page * ms->page_size,
                     ms->page_size, page2secbuf(ms, page));
    if (rv != ms->page_size)
    {
      printf("hv_warning: unexpected error %d from srom_rd\n", rv);
      return (1);
    }

    set_page_valid(ms, page, 1);
  }

  return (0);
}


/** Flush a page.  Note that this routine is only really suitable for use by
 *  flush_sector, which does the necessary sector erase operation.
 * @param ms Master state pointer.
 * @param page Page number within sector.
 * @return flush page status (FLUSH_PAGE_*. See definitions in srom.h)
 */
static inline int
flush_page(srom_mst_state_t* ms, uint32_t page)
{
  if (is_page_valid(ms, page) && is_page_dirty(ms, page))
  {
    SROM_TRACE("srom: flushing page %d from addr %p to offset %#x\n", page,
               page2secbuf(ms, page),
	       ms->sector_addr + page * ms->page_size);
    int rv = srom_wr_pg(ms->s_state->infop->idn_ports[0],
                        ms->s_state->infop->channel, ms->srom_dev,
                        ms->sector_addr + page * ms->page_size,
                        ms->page_size, page2secbuf(ms, page));
    if (rv != ms->page_size)
    {
      printf("hv_warning: unexpected error %d from srom_wr_pg\n", rv);
      return (FLUSH_PAGE_ERROR);
    }

    set_page_dirty(ms, page, 0);

    return (FLUSH_PAGE_PAGE_WRITTEN);
  }

  return (FLUSH_PAGE_NOT_WRITTEN);
}


/** Flush the current sector to the ROM.
 * @param ms Master state pointer.
 * @return Flush sector status (FLUSH_SECTOR__*. See definitions in srom.h)
 */
static int
flush_sector(srom_mst_state_t* ms)
{
  if (!ms->sector_valid)
    return (FLUSH_SECTOR_DONE);

  if (ms->sector_needs_erase)
  {
    if (!ms->erase_in_progress)
    {
      for (uint32_t page = 0; page < ms->pages_per_sector; page++)
      {
        if (load_page(ms, page))
          return (FLUSH_SECTOR_ERROR);
        set_page_dirty(ms, page, 1);
      }

      SROM_TRACE("srom: erasing sector at %#x\n", ms->sector_addr);
      ms->erase_in_progress = 1;
      int rv = srom_erasea(ms->s_state->infop->idn_ports[0],
                           ms->s_state->infop->channel, ms->srom_dev,
                           ms->sector_addr);
      if (rv != 0)
      {
        printf("hv_warning: unexpected error %d from srom_erase\n", rv);
        return (FLUSH_SECTOR_ERROR);
      }
    }

    return (FLUSH_SECTOR_ERASING_SECTOR);
  }

  int pages_written_num = 0;
  uint32_t page;
  for (page = 0; page < ms->pages_per_sector &&
    pages_written_num < SROM_WRITE_CHUNK_PAGE; page++)
  {
    int rv = flush_page(ms, page);

    if (rv == FLUSH_PAGE_PAGE_WRITTEN)
    {
      pages_written_num++;
    }
    else if (rv == FLUSH_PAGE_ERROR)
    {
      return (FLUSH_SECTOR_ERROR);
    }
  }

  return (page >= ms->pages_per_sector) ? FLUSH_SECTOR_DONE:
         FLUSH_SECTOR_WRITE_PAGE_AGAIN;
}


/** Load a new sector.  (Note that what this really does is to set up all of
 *  the data structures so that later load_page() calls will do the right
 *  thing; we don't read in any data as a result of this, unless we have to
 *  flush a previous sector.)
 * @param ms Master state pointer.
 * @param new_sector_addr Address of the first byte in the new sector.
 * @return Flush sector status. (i.e. FLUSH_SECTOR_*. See Definitions in srom.h)
 */
static inline int
load_sector(srom_mst_state_t* ms, uint32_t new_sector_addr)
{
  if (!ms->sector_valid || ms->sector_addr != new_sector_addr)
  {
    if (ms->sector_valid && ms->sector_dirty)
    {
      int rv = flush_sector(ms);

      if (rv != 0)
        return (rv);

      //
      // Flushing sector done here.
      //
    }

    set_all_pages_invalid(ms);
    set_all_pages_clean(ms);

    ms->sector_addr = new_sector_addr;
    ms->sector_needs_erase = 0;
    ms->sector_valid = 1;
  }

  return (FLUSH_SECTOR_DONE);
}


/** Copy a buffer, and check to see if by doing so we're changing any bits
 *  in the destination from 0 to 1.
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param len Number of bytes to copy.
 * @return 1 if the copy changed bits from 0 to 1, 0 if not.
 */
static int
copy_and_check_bitset(char* dst, char* src, int len)
{
    uint8_t bits_set = 0;

    for (int i = 0; i < len; i++)
    {
      bits_set |= ((*src ^ *dst) & *src);
      *dst++ = *src++;
    }

    return (bits_set != 0);
}


/** SPI Flash ROM driver read routine.  */
static int
srom_pread(int devhdl, void* statep, uint32_t flags, char* va,
           uint32_t len, uint64_t offset, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  srom_state_t* ss = statep;
  srom_mst_state_t* ms = ss->mst_state;

  DEVICE_TRACE("srom_pread: devhdl %#x flags %#x va %p len %d "
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
    if (len != sizeof (ms->size[instance]))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &ms->size[instance], len, flags))
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
  // Check whether the device is busy.
  //
  if (ms->erase_in_progress)
  {
    ms->erase_in_progress = srom_is_busy(ss->infop->idn_ports[0],
                                         ss->infop->channel);

    if (ms->erase_in_progress != 0)
      return (HV_EBUSY);
    else
      ms->sector_needs_erase = 0;
  }

  //
  // Make sure we won't read off the end of the chip or sector.
  //
  uint32_t secaddr;
  clip_operation(ms, instance, &len, &offset, &secaddr);
  if (len == 0)
    return (0);

  //
  // Make sure the sector being read is the current sector, and that the
  // pages we're going to read are valid.
  //
  int rv = load_sector(ms, secaddr);

  //
  // load_sector() is not done unless FLUSH_SECTOR_DONE is returned, so 
  // need to check the return value to determine what this function or its
  // caller (by returning an indication) should do next.
  //
  switch (rv)
  {
    case FLUSH_SECTOR_WRITE_PAGE_AGAIN:
      // Tell client needs to try again.
      return (HV_EAGAIN);
    case FLUSH_SECTOR_ERASING_SECTOR:
      // Tell client we are busy, please try again later.
      return (HV_EBUSY);
    case FLUSH_SECTOR_ERROR:
      return (HV_EIO);
    case FLUSH_SECTOR_DONE:
      break;
    default:
      // Should never reach here.
      return (HV_EIO);
  }

  uint32_t first_page = offset2page(ms, offset);
  uint32_t last_page = offset2page(ms, offset + len - 1);

  for (int page = first_page; page <= last_page; page++)
    if (load_page(ms, page))
      return (HV_EIO);

  //
  // Now copy the data from the sector buffer to the user buffer.
  //
  if (drv_copy_to_client(va, offset2secbuf(ms, offset), len, flags))
    return (HV_EFAULT);

  return (len);
}


/** SPI Flash ROM driver write routine. */
static int
srom_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  srom_state_t* ss = statep;
  srom_mst_state_t* ms = ss->mst_state;
  int rv;

  DEVICE_TRACE("srom_pwrite: devhdl %#x flags %#x va %p len %d "
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
  // Check whether the device is busy.
  //
  if (ms->erase_in_progress)
  {
    ms->erase_in_progress = srom_is_busy(ss->infop->idn_ports[0],
      ss->infop->channel);

    if (ms->erase_in_progress != 0)
      return (HV_EBUSY);
    else
      ms->sector_needs_erase = 0;
  }

  //
  // If they're asking for a flush, do it.
  //
  if (offset == SROM_FLUSH_OFF)
  {
    rv = flush_sector(ms);

    switch (rv)
    {
      case FLUSH_SECTOR_WRITE_PAGE_AGAIN:
        // Client needs to try again.
        return (HV_EAGAIN);
      case FLUSH_SECTOR_ERASING_SECTOR:
        // Tell client we are busy, please either try again.
        return (HV_EBUSY);
      case FLUSH_SECTOR_DONE:
        return (len);
      case FLUSH_SECTOR_ERROR:
        return (HV_EIO);
      default:
        // Should never reach here.
        return (HV_EIO);
    }
  }

  //
  // Make sure we won't write off the end of the chip or sector.
  //
  uint32_t secaddr;
  clip_operation(ms, instance, &len, &offset, &secaddr);
  if (len == 0)
    return (HV_EINVAL);

  //
  // Make sure the sector being written is the current sector, and that the
  // pages we're going to touch are valid.  Also, set the pages to be dirty;
  // this is arguably a bit premature, since we could potentially fault on
  // the user buffer and not actually change them, but it's guaranteed to
  // be safe.
  //
  rv = load_sector(ms, secaddr);

  //
  // load_sector() is not done unless FLUSH_SECTOR_DONE is returned, so 
  // need to check the return value to determine what this function or its
  // caller (by returning an indication) should do next.
  //
  switch (rv)
  {
    case FLUSH_SECTOR_WRITE_PAGE_AGAIN:
      // Tell client we are not ready, please try again.
      return (HV_EAGAIN);
    case FLUSH_SECTOR_ERASING_SECTOR:
      // Tell client we are busy, please try again later.
      return (HV_EBUSY);
    case FLUSH_SECTOR_ERROR:
      return (HV_EIO);
    case FLUSH_SECTOR_DONE:
      break;
    default:
      // Should never reach here.
      return (HV_EIO);
  }

  uint32_t first_page = offset2page(ms, offset);
  uint32_t last_page = offset2page(ms, offset + len - 1);

  for (int page = first_page; page <= last_page; page++)
  {
    if (load_page(ms, page))
      return (HV_EIO);
    set_page_dirty(ms, page, 1);
  }

  //
  // Now copy the data from the user buffer to the sector buffer.  If the
  // sector already needs to be erased, we just use copy_from_client, but
  // if it doesn't, we do the copy ourselves and see whether we're setting
  // any 1 bits.
  //
  if (ms->sector_needs_erase)
  {
    if (drv_copy_from_client(offset2secbuf(ms, offset), va, len, flags))
        return (HV_EFAULT);
  }
  else if (!(flags & DRV_FLG_HVADDR))
  {
    ON_FAULT_RETURN_EFAULT(va, len);

    // Set this pessimistically, in case we fault in the middle of the copy
    ms->sector_needs_erase = 1;

    int bitset = copy_and_check_bitset(offset2secbuf(ms, offset), va, len);

    ms->sector_needs_erase = bitset;

    FAULT_END();
  }
  else
  {
    ms->sector_needs_erase =
      copy_and_check_bitset(offset2secbuf(ms, offset), va, len);
  }

  return (len);
}


/** SPI Flash ROM driver operations vector */
static struct drv_ops srom_ops = {
  .probe       = srom_probe,
  .init        = srom_init,
  .open        = srom_open,
  .close       = srom_close,
  .pread       = srom_pread,
  .pwrite      = srom_pwrite,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_srom = {
  .shim_type  = SROM_DEV_INFO__TYPE_VAL_SROM,
  .name       = "srom",
  .desc       = "SPI Flash ROM",
  .ops        = &srom_ops,
  .stilereq   = 1,
};

