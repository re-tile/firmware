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
 * SPI Flash ROM access routines.
 */

#include <stdio.h>

#include <arch/srom.h>

#include "bits.h"
#include "cfg.h"
#include "debug.h"
#include "drvintf.h"
#include "srom_acc.h"
#include "srom_table.h"
#include "types.h"

#ifdef L1BOOT
#include "hv_l1boot.h"
#endif

//
// Status bits for RDSR instruction.
//
#define SROM_SPI_SR_WIP 1  /**< Write in progress. */


#ifndef L1BOOT

/** Wait until any previously-performed SROM operation has completed.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @return Zero if the wait was successful, or a negative error code.
 */
static int
srom_wait_ready(pos_t pos, unsigned long chan)
{
  int sr;
  uint64_t timer;

  //
  // First we make sure that the shim isn't processing a command.
  //
  timer = drv_timer_start(1000000);
  while (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__BUSY_MASK)
    if (drv_timer_done(timer))
      return (-1);

  //
  // Now we make sure the SROM chip itself isn't still processing a write.
  //
  do
  {
    cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_RDSR);
    timer = drv_timer_start(2000000); // An erase can take up to 1.5 seconds.
    while (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__RFIFO_EMPTY_MASK)
      if (drv_timer_done(timer))
        return (-2);
    sr = cfg_rd(pos.word, chan, SROM_READ_DATA);
  } while (sr & SROM_SPI_SR_WIP);

  return (0);
}


/** Is the srom busy?
 *  Caller should promise that they will keep calling until
 *  a non-busy status is returned.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @return Nonzero if the device is busy.
 */
int
srom_is_busy(pos_t pos, unsigned long chan)
{
  int sr;
  static int inst_rdsr_issued;

  //
  // First we make sure that the shim isn't processing a command.
  //
  if (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__BUSY_MASK)
    return (-1);

  //
  // Now we make sure the SROM chip itself isn't still processing a write.
  //
  if (inst_rdsr_issued == 0)
  {
    cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_RDSR);
    inst_rdsr_issued = 1;
  }

  if (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__RFIFO_EMPTY_MASK)
    return (-2);

  sr = cfg_rd(pos.word, chan, SROM_READ_DATA);
  if (sr & SROM_SPI_SR_WIP)
  {
    inst_rdsr_issued = 0;
    return (-3);
  }
  else
  {
    inst_rdsr_issued = 0;
    return (0);
  }
}


/** Do a software boot operation on the SPI Flash ROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 */
void
srom_boot(pos_t pos, unsigned long chan)
{
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_BOOT);
}

#endif


/** Are we currently booting from the SPI ROM?
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @return Nonzero if we're currently booting from the ROM.
 */
int
srom_is_booting(pos_t pos, unsigned long chan)
{
  return ((cfg_rd(pos.word, chan, SROM_FLAG) &
           SROM_FLAG__BOOT_BUSY_MASK) != 0);
}


#ifndef L1BOOT

//
// We cache data about the SROM so we don't do the probe more
// than once per tile.
//
/** Pointer to the SROM table entry. */
static struct srom* srom_info;
/** ID value retrieved from the SROM. */
uint64_t srom_id;


/** Look up the SROM's entry in the SROM table.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @return Pointer to the SROM table entry that matches the installed
 *  SROM, or NULL if none exists.
 */
static struct srom*
srom_get_table_entry(pos_t pos, unsigned long chan)
{
  if (srom_info)
    return (srom_info);

  struct srom* retval = NULL;

  cfg_wr(pos.word, chan, SROM_BYTE, 5);
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_RDID0);
  while (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__RFIFO_EMPTY_MASK)
    ;
  srom_id = cfg_rd(pos.word, chan, SROM_READ_DATA);

  for (struct srom* ptr = srom_table; ptr < srom_table_end; ptr++)
  {
    if (((srom_id ^ ptr->id) & ptr->id_mask) == 0)
    {
      retval = ptr;
      break;
    }
  }

  srom_info = retval;

  return (retval);
}


/** Return information about the SPI Flash ROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param srom_id_ptr Pointer to returned SROM ID.
 * @param page_size_ptr Pointer to returned page size, in bytes.
 * @param sector_size_ptr Pointer to returned sector size, in bytes.
 * @param srom_size_ptr Pointer to returned SROM size, in bytes.
 * @return 1 if we recognized the SROM, 0 otherwise.
 */
int
srom_getinfo(pos_t pos, unsigned long chan, uint64_t* srom_id_ptr,
             uint32_t* page_size_ptr, uint32_t* sector_size_ptr,
             uint32_t* srom_size_ptr)
{
  struct srom* sip = srom_get_table_entry(pos, chan);
  *srom_id_ptr = srom_id;

  if (!sip)
    return (0);

  *page_size_ptr = sip->page_size;
  *sector_size_ptr = sip->sector_size;
  *srom_size_ptr = sip->srom_size;

  INIT_TRACE("Found srom: %s\n", sip->name);

  //
  // Set the page size.
  //
  cfg_wr(pos.word, chan, SROM_PAGE_SIZE, __insn_ctz(sip->page_size) - 2);

  return (1);
}

//
// Macros to construct/break out the SROM dev cookie value.  Note that this
// layout is private to routines in this file, and may or may not match
// that used by the SROM booter.
//

/** Create an SROM dev cookie value. */
#define MK_DEV_ADDR(srom_size, a24_rcmd, a24_wcmd, a24_wren) \
  (((a24_wcmd) & 0xFF) | \
   (((a24_rcmd) & 0xFF) << 8) | \
   (__builtin_ctz(srom_size) << 16) | \
   ((a24_wren != 0) << 22))

/** Extract the A24 write command from an SROM dev cookie value. */
#define DEV_ADDR_2_A24_WCMD(dev_addr) ((dev_addr) & 0xFF)

/** Extract the A24 read command from an SROM dev cookie value. */
#define DEV_ADDR_2_A24_RCMD(dev_addr) (((dev_addr) >> 8) & 0xFF)

/** Extract the A24 write-needs-WREN flag from an SROM dev cookie value. */
#define DEV_ADDR_2_A24_WREN(dev_addr) (((dev_addr) >> 22) & 1)

/** Extract the SROM size from an SROM dev cookie value. */
#define DEV_ADDR_2_SROM_SIZE(dev_addr) (1 << (((dev_addr) >> 16) & 0x3F))

/** Extract the SROM address mask from an SROM dev cookie value. */
#define DEV_ADDR_2_SROM_ADDR_MASK(dev_addr) (DEV_ADDR_2_SROM_SIZE(dev_addr) - 1)


/** Set the high 8 bits of the SROM byte address, if needed.  Note that
 *  this routine expects that any previous SROM commands have completed
 *  before it was called; it doesn't return until any command it issues has
 *  finished.  Thus, it's safe to insert a call to this routine between a
 *  call to srom_wait_ready, and code that launches a new command.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param addr Byte address within the srom at which to start.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param Zero if operation successful, nonzero otherwise.
 */
static int
srom_set_a24(pos_t pos, unsigned long chan, int addr, int dev)
{
  int rv = 0;

  //
  // If dev is zero, this device is 24 bits of address or less, so
  // there's nothing to do here.
  //
  if (dev)
  {
    //
    // On some chips we need to do write-enable before writing the A24
    // register.
    //
    if (DEV_ADDR_2_A24_WREN(dev))
    {
      cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_WREN);
      rv = srom_wait_ready(pos, chan);
      if (rv)
        return (rv);
    }

    //
    // We mask the address to handle the case where the BIB reading code
    // passes us -4.  On some chips, the A24 command writes both the high
    // address bits, and some other random bits up at the top of the byte;
    // we don't want to set those other bits.
    //
    addr &= DEV_ADDR_2_SROM_ADDR_MASK(dev);

    int a24 = (addr >> 24) & 0xFF;
    int a24_wcmd = DEV_ADDR_2_A24_WCMD(dev);

    //
    // Make the WRSR command match the device's A24 write command.
    //
    cfg_wr(pos.word, chan, SROM_CODE_WRSR, a24_wcmd);

    //
    // Issue the A24 write command.
    //
    cfg_wr(pos.word, chan, SROM_WRITE_DATA, a24);
    cfg_wr(pos.word, chan, SROM_INSTRUCTION, a24_wcmd);

    rv = srom_wait_ready(pos, chan);

    //
    // Set the WRSR command back to a real WRSR command.
    //
    cfg_wr(pos.word, chan, SROM_CODE_WRSR, SROM_INSTRUCTION__INST_VAL_WRSR);
  }

  return (rv);
}


/** Get the high 8 bits of the SROM byte address.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @return Byte address, containing just the high address bits (i.e., the
 *  low 24 bits are 0).
 */
static int
srom_get_a24(pos_t pos, unsigned long chan, int dev)
{
  int rv = 0;

  //
  // If dev is zero, this device is 24 bits of address or less, so
  // there's nothing to do here.
  //
  if (dev)
  {
    int a24_rcmd = DEV_ADDR_2_A24_RCMD(dev);

    //
    // Make the RDSR command match the device's A24 read command.
    //
    cfg_wr(pos.word, chan, SROM_CODE_RDSR, a24_rcmd);

    //
    // Issue the A24 command.
    //
    cfg_wr(pos.word, chan, SROM_INSTRUCTION, a24_rcmd);

    //
    // Wait until we get a byte in the read FIFO.
    //
    while (cfg_rd(pos.word, chan, SROM_FLAG) & SROM_FLAG__RFIFO_EMPTY_MASK)
      ;

    //
    // Shift the returned value up, and mask off anything that isn't part
    // of the address.
    //
    rv = (cfg_rd(pos.word, chan, SROM_READ_DATA) << 24) &
         DEV_ADDR_2_SROM_ADDR_MASK(dev);

    //
    // Set the RDSR command back to a real RDSR command.
    //
    cfg_wr(pos.word, chan, SROM_CODE_RDSR, SROM_INSTRUCTION__INST_VAL_RDSR);
  }

  return (rv);
}


/** Return the device cookie for the SROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @return dev cookie to be passed to other SROM routines.
 */
int
srom_get_dev(pos_t pos, unsigned long chan)
{
  struct srom* sip = srom_get_table_entry(pos, chan);

  if (!sip || !sip->a24_wcmd)
    return (0);

  //
  // Figure out if we need WREN before setting the A24 register.
  //
  int dev = MK_DEV_ADDR(sip->srom_size, sip->a24_rcmd, sip->a24_wcmd, 0);

  int old_a24 = srom_get_a24(pos, chan, dev);
  srom_set_a24(pos, chan, old_a24 ^ 0x1000000, dev);

  //
  // If the value didn't change, we need the WREN, so change the dev cookie.
  // Otherwise, set the register back to its original value.
  //
  if (srom_get_a24(pos, chan, dev) == old_a24)
    dev = MK_DEV_ADDR(sip->srom_size, sip->a24_rcmd, sip->a24_wcmd, 1);
  else
    srom_set_a24(pos, chan, old_a24, dev);

  return (dev);
}


/** Reset the SROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 */
void
srom_reset(pos_t pos, unsigned long chan, int dev)
{
  srom_set_a24(pos, chan, 0, dev);
}

#endif

#ifdef L1BOOT

/** Read data from the SPI Flash ROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param addr Byte address within the srom at which to start.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param len Number of bytes to read; must be a multiple of 4.
 * @param buf Buffer where the data read will be placed; must be word-aligned.
 * @return Number of bytes read, or a negative value if an error occurred.
 */
int
srom_rd(pos_t pos, unsigned long chan, int dev, int addr, int len, void* buf)
{
  //
  // The L1 booter implementation of srom_rd just calls the assembly
  // SROM read routine, since it's already there for SROM boot and we
  // don't want a duplicate set of code.  Note that we ignore some of
  // the passed-in parameters, since the assembly access routine doesn't
  // allow us to set them.  Note also that the assembly routine works in
  // terms of 4-byte words, even on Gx.
  //
  srom_set_addr(pos.word, dev, addr);
  early_srom_read(pos.word, dev, buf, ROUND_UP(len, 4) / 4);

  return (len);
}

#else /* L1BOOT */

/** Read data from the SPI Flash ROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param addr Byte address within the srom at which to start.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param len Number of bytes to read; must be a multiple of 4.
 * @param buf Buffer where the data read will be placed; must be 4-byte
 *        aligned.
 * @return Number of bytes read, or a negative value if an error occurred.
 */
int
srom_rd(pos_t pos, unsigned long chan, int dev, int addr, int len, void* buf)
{
  //
  // Make sure our buffer is aligned.
  //
  if ((len | (intptr_t) buf) & 3)
    return (-2);

  uint32_t* wdbuf = buf;

  //
  // Wait until any previously-performed write has completed.
  //
  if (srom_wait_ready(pos, chan))
    return (-3);

  int len_undone = len;

  while (len_undone)
  {
    //
    // The shim only handles up to a megabyte per read, so we may have to
    // do more than one.
    //
    int len_this_pass = 1 << 20;

    if (len_this_pass > len_undone)
      len_this_pass = len_undone;

    //
    // Set up the read.
    //
    if (srom_set_a24(pos, chan, addr, dev))
      return (-4);

    cfg_wr(pos.word, chan, SROM_ADDRESS, addr);
    cfg_wr(pos.word, chan, SROM_BYTE, len_this_pass);
    cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_READ);

    //
    // Read the words from the shim.
    //
    for (int i = len_this_pass; i > 0; i -= sizeof (uint_reg_t))
    {
      //
      // Wait until the read FIFO isn't empty.
      //
      int flag;

      uint64_t timer = drv_timer_start(1000000);
      while ((flag = cfg_rd(pos.word, chan, SROM_FLAG)) &
             SROM_FLAG__RFIFO_EMPTY_MASK)
        if (drv_timer_done(timer))
          return (-1);

      //
      // We don't bother doing the full-FIFO optimization here; we might
      // in the future, but the Gx FIFO holds 8 words so it's a bit more
      // work.  Also, we're spinning anyway, and the tile is going to be
      // much faster than the SROM shim, so all it really saves us is an
      // MMIO read per word on a very infrequently used device.
      //
      uint_reg_t data = cfg_rd(pos.word, chan, SROM_READ_DATA);
      if (i > 4)
        *wdbuf++ = data;
      *wdbuf++ = data >> 32;
    }
    len_undone -= len_this_pass;
    addr += len_this_pass;
  }

  return (len);
}

#endif

#ifndef L1BOOT

/** Write up to a page of data to the SPI Flash ROM.  The caller is responsible
 *  for ensuring the requested write is contained within one SROM page.
 *  Note that this routine does _not_ do any erase operations, so to write
 *  arbitrary data it must be combined with the use of srom_erase().
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param addr Byte address within the srom at which to start.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param len Number of bytes to write; must be a multiple of 4.
 * @param buf Buffer from which the data written will be taken; must be
 *        4-byte aligned.
 * @return Number of bytes written, or a negative value if an error occurred.
 */
int
srom_wr_pg(pos_t pos, unsigned long chan, int dev, int addr, int len,
           void* buf)
{
  //
  // Make sure our buffer is aligned.
  //
  if ((len | (intptr_t) buf) & 3)
    return (-2);

  uint32_t* wdbuf = buf;

  //
  // Set the upper address bits.  This clears the write-enable, so it must
  // be done first.
  //
  if (srom_wait_ready(pos, chan))
    return (-3);
  if (srom_set_a24(pos, chan, addr, dev))
    return (-6);

  //
  // Do the write-enable.
  //
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_WREN);

  //
  // Set up the page program command.
  //
  if (srom_wait_ready(pos, chan))
    return (-5);
  cfg_wr(pos.word, chan, SROM_ADDRESS, addr);
  cfg_wr(pos.word, chan, SROM_BYTE, len);
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_PP);

  //
  // Write the words to the shim.
  //
  for (int i = len; i > 0; i -= sizeof (uint_reg_t))
  {
    //
    // Wait until the write FIFO isn't full.
    //
    int flag;

    uint64_t timer = drv_timer_start(10000);
    while ((flag = cfg_rd(pos.word, chan, SROM_FLAG)) &
           SROM_FLAG__WFIFO_FULL_MASK)
      if (drv_timer_done(timer))
        return (-1);

    //
    // See comments in srom_rd() about why we aren't doing the empty-FIFO
    // optimization on Gx.
    //
    uint_reg_t data = *wdbuf++;
    if (i > 4)
      data = ((uint_reg_t) *wdbuf++ << 32) | data;
    cfg_wr(pos.word, chan, SROM_WRITE_DATA, data);
  }

  //
  // Wait until the write has completed.
  //
  if (srom_wait_ready(pos, chan))
    return (-4);

  return (len);
}


/** Erase a sector in the SPI Flash ROM.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param addr Byte address of the sector to erase.
 * @return Zero is successful, or a negative value if an error occurred.
 */
int
srom_erase(pos_t pos, unsigned long chan, int dev, int addr)
{
  //
  // Set the upper address bits.  This clears the write-enable, so it must
  // be done first.
  //
  if (srom_wait_ready(pos, chan))
    return (-3);
  if (srom_set_a24(pos, chan, addr, dev))
    return (-2);

  //
  // Do the write-enable.
  //
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_WREN);

  //
  // Do the the erase.
  //
  if (srom_wait_ready(pos, chan))
    return (-5);
  cfg_wr(pos.word, chan, SROM_ADDRESS, addr);
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_SE0);

  //
  // Wait until the erase has completed.
  //
  if (srom_wait_ready(pos, chan))
    return (-4);

  return (0);
}


/** Erase a sector asynchronously in the SPI Flash ROM. We just issue
 *  erase command here, srom_is_busy() needs to be called to determine
 *  whether erasing is done.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @param addr Byte address of the sector to erase.
 * @return Zero if successful, or a negative value if an error occurred.
 */
int
srom_erasea(pos_t pos, unsigned long chan, int dev, int addr)
{
  //
  // Set the upper address bits.  This clears the write-enable, so it must
  // be done first.
  //
  if (srom_wait_ready(pos, chan))
    return (-3);
  if (srom_set_a24(pos, chan, addr, dev))
    return (-2);

  //
  // Do the write-enable.
  //
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_WREN);

  //
  // Do the the erase.
  //
  if (srom_wait_ready(pos, chan))
    return (-5);

  cfg_wr(pos.word, chan, SROM_ADDRESS, addr);
  cfg_wr(pos.word, chan, SROM_INSTRUCTION, SROM_INSTRUCTION__INST_VAL_SE0);

  // Not write erasing done. Instead, return immediately.
  return (0);
}

/** Return the current contents of the SROM address register.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev SROM device cookie; must be the return value from srom_get_dev().
 * @return Current contents of the SROM address register.
 */
int
srom_get_bootstream_addr(pos_t pos, unsigned long chan, int dev)
{
  int rv = cfg_rd(pos.word, chan, SROM_ADDRESS);
  if (dev)
    rv |= srom_get_a24(pos, chan, dev);

  return (rv);
}

#endif /* L1BOOT */
