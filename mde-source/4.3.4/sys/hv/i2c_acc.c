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
 * I2C access routines.
 */

#include <util.h>

#include <arch/i2cm.h>

#include "cfg.h"
#include "devices.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "lock.h"
#include "types.h"

//
// There are a few odd differences between the Pro and Gx register names;
// we define some common symbols here to avoid #ifdefs below.
//
/** Interrupt status register */
#define I2CM_INT_STATUS        I2CM_INT_VEC_W1TC
/** Read acknowledge error */
#define I2CM_INT_STATUS_RACK   I2CM_INT_VEC_W1TC__RACK_ERR_MASK
/** Write acknowledge error */
#define I2CM_INT_STATUS_WACK   I2CM_INT_VEC_W1TC__WACK_ERR_MASK


/** Clock speed for each I2C bus. */
#ifdef L1BOOT
static long i2c_freq[MAX_I2CMS];
#else
static long i2c_freq[MAX_I2CMS] _SHARED;
#endif

/** Calculate an I2C bus frequency.
 * @param presc I2C controller prescaler value.
 * @param glitch I2C controller glitch mask value.
 * @return Frequency in hertz.
 */
static long
i2c_presc_to_hz(long presc, long glitch)
{
  return REFCLK / (2 * presc + 3 + glitch);
}

/** Calculate an I2C bus prescaler value.
 * @param hz I2C bus frequency in hertz.
 * @param glitch I2C controller glitch mask value.
 * @return Prescaler value.
 */
static long
i2c_hz_to_presc(long hz, long glitch)
{
  hz = hz ? hz : 1;
  long rv = ((REFCLK / hz) - (3 + glitch)) / 2;
  return (i2c_presc_to_hz(rv, glitch) <= hz) ? rv : rv + 1;
}

/** Prepare to use an I2C bus.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param use_bib If nonzero, use the BIB to configure various controller
 *  properies.
 */
static void
_i2c_enable(pos_t pos, unsigned long chan, int use_bib)
{
  I2CM_DEV_INFO_t idi = { .word = cfg_rd(pos.word, chan, I2CM_DEV_INFO) };
  int shim = idi.instance;

  cfg_wr(pos.word, chan, I2CM_BASELINE_CTL, I2CM_BASELINE_CTL__ENABLE_MASK);

  //
  // See if we have any controller settings in the BIB.
  //
  bi_ptr_t bufptr;
  uint32_t desc = use_bib ? bi_getparam(BI_TYPE_I2CM_CTL_CFG, shim, &bufptr, 0)
                          : BI_NULL;
  struct bi_i2cm_ctl_cfg* bi = (desc == BI_NULL) ? NULL : bufptr;

  //
  // On Gx we have to unmask the relevant interrupt bits in order to see
  // them in the status register.  We also raise the drive strength, if the
  // BIB doesn't specify it, since the default is a bit low for some
  // configurations.
  //
  cfg_wr(pos.word, chan, I2CM_INT_VEC_MASK,
         ~(I2CM_INT_STATUS_RACK | I2CM_INT_STATUS_WACK));

  I2CM_ELECTRICAL_CONTROL_t iec =
    { .word = cfg_rd(pos.word, chan, I2CM_ELECTRICAL_CONTROL) };
  if (bi)
    iec.word = bi->elec;
  else
    iec.elec_strength = 6;  // 12 mA
  cfg_wr(pos.word, chan, I2CM_ELECTRICAL_CONTROL, iec.word);

  //
  // Calculate, potentially set, and save the current clock speed, the
  // last for use in error recovery.
  //
  long glitch;
  long prescaler;

  if (bi)
  {
    glitch = bi->glitch;
    prescaler = i2c_hz_to_presc(bi->freq_khz * 1000, glitch);

    cfg_wr(pos.word, chan, I2CM_GLITCH_MASK, glitch);
    cfg_wr(pos.word, chan, I2CM_PRESCALER, prescaler);
  }
  else
  {
    glitch = cfg_rd(pos.word, chan, I2CM_GLITCH_MASK);
    prescaler = cfg_rd(pos.word, chan, I2CM_PRESCALER);
  }

  i2c_freq[shim] = i2c_presc_to_hz(prescaler, glitch);
}


/** Prepare to use an I2C bus.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 */
void
i2c_enable(pos_t pos, unsigned long chan)
{
  _i2c_enable(pos, chan, 0);
}


/** Prepare to use an I2C bus, and configure it using data from the BIB.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 */
void
i2c_enable_bib(pos_t pos, unsigned long chan)
{
  _i2c_enable(pos, chan, 1);
}


// Read data from an I2C device.  For parameter documentation, see
// i2c_rd(), below.
static inline int
_i2c_rd_unlocked(pos_t pos, unsigned long chan, int dev, int addr,
                 int len, void* buf, int shim)
{
  int retries = 0;

  uint8_t* charbuf = buf;

  //
  // Wait until the previous request, if any, is complete.
  //
  while (cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__BUSY_MASK)
    if (retries++ > 1000 * 1000)
      return (-1);

  //
  // Set up the read.
  //
  cfg_wr(pos.word, chan, I2CM_SLAVE_ADDRESS, dev & I2C_DEV_ADDR_MASK);

  cfg_wr(pos.word, chan, I2CM_ADDR_SEL,
         (dev & I2C_DEV_16BIT) ? I2CM_ADDR_SEL__ADDR_16_BIT_MASK :
         (dev & I2C_DEV_NOADDR) ? I2CM_ADDR_SEL__ADDR_DIS_MASK :
         0);
  cfg_wr(pos.word, chan, I2CM_ADDRESS, addr);
  cfg_wr(pos.word, chan, I2CM_BYTE, len);
  cfg_wr(pos.word, chan, I2CM_INSTRUCTION, I2CM_INSTRUCTION__INST_VAL_READ);

  //
  // Read the words from the shim.
  //
  int retlen = 0;
  int remlen = len;

  while (remlen)
  {
    //
    // Wait until we get a read error or the read FIFO isn't empty.
    //
    retries = 0;

    int bytesthispass = remlen;
    if (bytesthispass > sizeof (uint_reg_t))
      bytesthispass = sizeof (uint_reg_t);

    while (1)
    {
      if (!(cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__RFIFO_EMPTY_MASK))
      {
        if (cfg_rd(pos.word, chan, I2CM_INT_STATUS) & I2CM_INT_STATUS_RACK)
        {
          //
          // Wait until the shim thinks it's done.
          //
          while (cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__BUSY_MASK)
            ;

          //
          // Wait until the I2C IP is done with a possible trailing
          // request; to do this we figure out how many bits might be left.
          // We think that the worst case is when we get NAKed after
          // sending the device address for the initial dummy write, so we
          // still have the address bytes, a start bit, then the device
          // address for the read, the read bytes, and the stop bit
          // remaining.  Each byte is 9 bits (8 data bits and one ACK bit).
          // Finally we scale by the clock rate.
          //
          int addr_size = (dev & I2C_DEV_16BIT) ? 2
                                                : (dev & I2C_DEV_NOADDR) ? 0
                                                                         : 1 ;
          int ip_len = min(len, 8);
          drv_udelay((1000L * 1000 * ((addr_size + 1 + ip_len) * 9 + 2) +
                      i2c_freq[shim] - 1) / i2c_freq[shim]);

          //
          // Now drain the read FIFO and reset the error interrupt.
          //
          while (!(cfg_rd(pos.word, chan, I2CM_FLAG) &
                   I2CM_FLAG__RFIFO_EMPTY_MASK))
            (void) cfg_rd(pos.word, chan, I2CM_READ_DATA);

          cfg_wr(pos.word, chan, I2CM_INT_STATUS, I2CM_INT_STATUS_RACK);
          return (retlen);
        }
        else
          break;
      }

      if (retries++ > 1000 * 1000)
        return (retlen);
    }

    uint_reg_t word = cfg_rd(pos.word, chan, I2CM_READ_DATA);

    word >>= (addr & (sizeof (uint_reg_t) - 1)) * 8;
    for (int i = 0; i < bytesthispass; i++)
    {
      *charbuf++ = word & 0xFF;
      word >>= 8;
    }
    retlen += bytesthispass;
    remlen -= bytesthispass;
    addr += bytesthispass;
  }

  return (retlen);
}


// Write data to an I2C device.  For parameter documentation, see
// i2c_wr(), below.
static inline int
_i2c_wr_unlocked(pos_t pos, unsigned long chan, int dev, int addr,
                 int len, void* buf, int shim)
{
  int retries = 0;

  //
  // Make sure things are aligned appropriately.
  //
  if ((addr & (sizeof (uint_reg_t) - 1)) &&
      len + (addr & (sizeof (uint_reg_t) - 1)) > sizeof (uint_reg_t))
    return (-2);

  uint8_t* charbuf = buf;

  //
  // Wait until the previous request, if any, is complete.
  //
  while (cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__BUSY_MASK)
    if (retries++ > 1000 * 1000)
      return (-1);

  //
  // Set up the write.
  //
  cfg_wr(pos.word, chan, I2CM_SLAVE_ADDRESS, dev & I2C_DEV_ADDR_MASK);
  cfg_wr(pos.word, chan, I2CM_ADDR_SEL,
         (dev & I2C_DEV_16BIT) ? I2CM_ADDR_SEL__ADDR_16_BIT_MASK :
         (dev & I2C_DEV_NOADDR) ? I2CM_ADDR_SEL__ADDR_DIS_MASK :
         0);
  cfg_wr(pos.word, chan, I2CM_ADDRESS, addr);
  cfg_wr(pos.word, chan, I2CM_BYTE, len);
  cfg_wr(pos.word, chan, I2CM_INSTRUCTION, I2CM_INSTRUCTION__INST_VAL_WRITE);

  //
  // Write the words to the shim.
  //
  int retlen = 0;
  int remlen = len;

  while (remlen)
  {
    retries = 0;

    int bytesthispass = remlen;
    if (bytesthispass > sizeof (uint_reg_t))
      bytesthispass = sizeof (uint_reg_t);

    //
    // Wait until the write FIFO is not full.
    //
    while (1)
    {
      if ((cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__WFIFO_FULL_MASK) == 0)
        break;

      if (retries++ > 1000 * 1000)
        return (retlen);
    }

    //
    // Get a word and send it out.
    //
    uint_reg_t word = 0;

    for (int i = 0; i < bytesthispass; i++)
      word |= (uint_reg_t)(*charbuf++) << (8 * i);

    word <<= (addr & (sizeof (uint_reg_t) - 1)) * 8;

    cfg_wr(pos.word, chan, I2CM_WRITE_DATA, word);

    retlen += bytesthispass;
    remlen -= bytesthispass;
    addr += bytesthispass;
  }

  //
  // Wait until the write is complete.
  //
  retries = 0;
  while (cfg_rd(pos.word, chan, I2CM_FLAG) & I2CM_FLAG__BUSY_MASK)
    if (retries++ > 1000 * 1000)
      return (-1);

  //
  // Check for a write error.
  //
  if (cfg_rd(pos.word, chan, I2CM_INT_STATUS) & I2CM_INT_STATUS_WACK)
  {
    //
    // We got a write error, so wait until the I2C IP is done with a
    // possible trailing request; to do this we figure out how many bits
    // might be left (8 data bits and one ACK bit for each of the address
    // and data bytes, plus one stop bit) and then scale by the clock rate.
    // Note that we don't count the initial stop bit and device address
    // because we don't wait for an ACK, and thus can't get an error, until
    // at least those have been sent.
    //
    int addr_size = (dev & I2C_DEV_16BIT) ? 2
                                          : (dev & I2C_DEV_NOADDR) ? 0
                                                                   : 1 ;
    int ip_len = min(len, 8);
    drv_udelay((1000L * 1000 * ((addr_size + ip_len) * 9 + 1) +
                i2c_freq[shim] - 1) / i2c_freq[shim]);
     
    //
    // Reset the error interrupt, and return bad status.  (We don't
    // actually know how many bytes were successfully written, and couldn't
    // even if we were checking in the loop above, so we can't just return
    // a short write.)
    //
    cfg_wr(pos.word, chan, I2CM_INT_STATUS, I2CM_INT_STATUS_WACK);
    return (-3);
  }

  return (retlen);
}

#ifndef L1BOOT
//
// In the hypervisor, we protect the access routines with a per-controller
// lock, since otherwise if multiple tiles try to do I2C access concurrently,
// things will not work.  In the booter, we obviously don't need this.
//
static spinlock_t i2c_lock[MAX_I2CMS] _SHARED;
#endif

/** Read data from an I2C device.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev 8-bit address of the device to read from.  Includes I2C_DEV_16BIT
 *        if the device takes 16-bit byte addresses.
 * @param addr Byte address within the ROM at which to start.  If not a
 *        multiple of the system word size, then the request must not cross a
 *        word boundary.
 * @param len Number of bytes to read.
 * @param buf Buffer where the data read will be placed; must be word-aligned.
 * @return Number of bytes read, or a negative value if an error occurred.
 */
int
i2c_rd(pos_t pos, unsigned long chan, int dev, int addr, int len, void* buf)
{
  I2CM_DEV_INFO_t idi =
    { .word = cfg_rd(pos.word, chan, I2CM_DEV_INFO) };
  int shim = idi.instance;
#ifndef L1BOOT
  spin_lock(&i2c_lock[shim]);
#endif
  //
  // If we're doing an unaligned read (one that doesn't start on a word
  // boundary, and doesn't end within the word it starts in), then split
  // it up and do it in two reads.
  //
  int retval;

  if ((addr & (sizeof (uint_reg_t) - 1)) &&
      len + (addr & (sizeof (uint_reg_t) - 1)) > sizeof (uint_reg_t))
  {
    int first_len = sizeof (uint_reg_t) - (addr & (sizeof (uint_reg_t) - 1));

    retval = _i2c_rd_unlocked(pos, chan, dev, addr, first_len, buf, shim);

    if (retval == first_len)
    {
      retval =_i2c_rd_unlocked(pos, chan, dev, addr + first_len,
                               len - first_len, buf + first_len, shim);
      if (retval >= 0)
        retval += first_len;
    }
  }
  else
    retval = _i2c_rd_unlocked(pos, chan, dev, addr, len, buf, shim);
#ifndef L1BOOT
  spin_unlock(&i2c_lock[shim]);
#endif
  return retval;
}

/** Write data to an I2C device.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev 8-bit address of the device to write to.  Includes I2C_DEV_16BIT
 *        if the device takes 16-bit byte addresses.
 * @param addr Byte address within the ROM at which to start.  If not a
 *        multiple of the system word size, then the request must not cross
 *        a word boundary.
 * @param len Number of bytes to write.
 * @param buf Buffer where the data written will be taken from; must be
 *        word-aligned.
 * @return Number of bytes written, or a negative value if an error occurred.
 */
int
i2c_wr(pos_t pos, unsigned long chan, int dev, int addr, int len, void* buf)
{
  I2CM_DEV_INFO_t idi =
    { .word = cfg_rd(pos.word, chan, I2CM_DEV_INFO) };
  int shim = idi.instance;
#ifndef L1BOOT
  spin_lock(&i2c_lock[shim]);
#endif
  //
  // If we're doing an unaligned write (one that doesn't start on a word
  // boundary, and doesn't end within the word it starts in), then split
  // it up and do it in two writes.
  //
  int retval;

  if ((addr & (sizeof (uint_reg_t) - 1)) &&
      len + (addr & (sizeof (uint_reg_t) - 1)) > sizeof (uint_reg_t))
  {
    int first_len = sizeof (uint_reg_t) - (addr & (sizeof (uint_reg_t) - 1));

    retval = _i2c_wr_unlocked(pos, chan, dev, addr, first_len, buf, shim);

    if (retval == first_len)
    {
      retval =_i2c_wr_unlocked(pos, chan, dev, addr + first_len,
                               len - first_len, buf + first_len, shim);
      if (retval >= 0)
        retval += first_len;
    }
  }
  else
    retval = _i2c_wr_unlocked(pos, chan, dev, addr, len, buf, shim);
#ifndef L1BOOT
  spin_unlock(&i2c_lock[shim]);
#endif
  return retval;
}

#ifndef L1BOOT
/** Write data to an I2C device, with extended settings.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param dev 8-bit address of the device to write to.  Includes I2C_DEV_16BIT
 *        if the device takes 16-bit byte addresses.
 * @param addr Byte address within the ROM at which to start.  If not a
 *        multiple of the system word size, then the request must not cross
 *        a word boundary.
 * @param len Number of bytes to write.
 * @param buf Buffer where the data written will be taken from; must be
 *        word-aligned.
 * @param page_size Page size in bytes.
 * @param write_cycle Write cycle time in milliseconds (with a few exceptions,
 *        as defined by thte I2CM_WRITE_CYCLE register definition).
 * @return Number of bytes written, or a negative value if an error occurred.
 */
int
i2c_wrx(pos_t pos, unsigned long chan, int dev, int addr, int len, void* buf,
        int page_size, int write_cycle)
{
  int retval = 0;
  int no_stop = 0;  // Nonzero if controller NO_STOP currently enabled

  I2CM_DEV_INFO_t idi =
    { .word = cfg_rd(pos.word, chan, I2CM_DEV_INFO) };
  int shim = idi.instance;
  spin_lock(&i2c_lock[shim]);

  cfg_wr(pos.word, chan, I2CM_PAGE_SIZE, __builtin_ctz(min(page_size, 64)) - 2);
  cfg_wr(pos.word, chan, I2CM_WRITE_CYCLE, write_cycle);

  //
  // If the page size is larger than that supported by hardware, we break
  // the write up into smaller writes, but keep the controller from putting
  // a STOP condition on the bus until after the last write; this
  // simulation of a longer write is helpful for some devices.  Otherwise
  // we let the controller break it up into multiple writes, based on the
  // page size, each of which will end with a STOP.
  //
  if (page_size > 64)
  {
    //
    // If we're doing an unaligned write (one that doesn't start on a word
    // boundary, and doesn't end within the word it starts in), then our
    // first write needs to handle just the first partial word.
    //
    int len_this_pass = min(len, 64);

    if ((addr & (sizeof (uint_reg_t) - 1)) &&
        len + (addr & (sizeof (uint_reg_t) - 1)) > sizeof (uint_reg_t))
      len_this_pass = sizeof (uint_reg_t) - (addr & (sizeof (uint_reg_t) - 1));

    //
    // If we'll be doing more than one write transaction, we don't want a
    // trailing STOP on any but the last.
    //
    if (len_this_pass < len)
    {
      cfg_wr(pos.word, chan, I2CM_NO_STOP, 1);
      no_stop = 1;
    }

    while (len)
    {
      int err = _i2c_wr_unlocked(pos, chan, dev, addr, len_this_pass, buf,
                                 shim);

      if (err != len_this_pass)
      {
        retval = -1;
        if (no_stop)
        {
          //
          // We won't be doing any more writes, so if we didn't end with a
          // STOP, reset the bus to get it back into a sane state.
          //
          cfg_wr(pos.word, chan, I2CM_NO_STOP, 0);
          cfg_wr(pos.word, chan, I2CM_BASELINE_CTL, 0);
          drv_udelay(120);
          cfg_wr(pos.word, chan, I2CM_BASELINE_CTL, 1);
        }
        break;
      }
      else
      {
        retval += len_this_pass;
        len -= len_this_pass;
        buf += len_this_pass;
        addr += len_this_pass;

        len_this_pass = min(64, len);
      }

      //
      // If we're about to do our last transaction, it needs to end with a
      // STOP.
      //
      if (len <= 64)
      {
        cfg_wr(pos.word, chan, I2CM_NO_STOP, 0);
        no_stop = 0;
      }
    }
  }
  else
  {
    //
    // If we're doing an unaligned write (one that doesn't start on a word
    // boundary, and doesn't end within the word it starts in), then split
    // it up and do it in two writes.
    //

    if ((addr & (sizeof (uint_reg_t) - 1)) &&
        len + (addr & (sizeof (uint_reg_t) - 1)) > sizeof (uint_reg_t))
    {
      int first_len = sizeof (uint_reg_t) - (addr & (sizeof (uint_reg_t) - 1));

      retval = _i2c_wr_unlocked(pos, chan, dev, addr, first_len, buf, shim);

      if (retval == first_len)
      {
        retval =_i2c_wr_unlocked(pos, chan, dev, addr + first_len,
                                 len - first_len, buf + first_len, shim);
        if (retval >= 0)
          retval += first_len;
      }
    }
    else
      retval = _i2c_wr_unlocked(pos, chan, dev, addr, len, buf, shim);
  }

  cfg_wr(pos.word, chan, I2CM_WRITE_CYCLE, I2CM_WRITE_CYCLE__WC_RESET_VAL);
  cfg_wr(pos.word, chan, I2CM_PAGE_SIZE, I2CM_PAGE_SIZE__PAGE_SIZE_RESET_VAL);

  spin_unlock(&i2c_lock[shim]);
  return retval;
}
#endif

/** Switch control routine for the PCA954x I2C switch chips.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param switch_addr Switch I2C address.
 * @param switch_chan Switch channel number to enable, or -1 if all
 *   channels should be disabled.
 * @return Zero if successful, or -1 if an error occurred.
 */
static int
pca954x_switch_config(pos_t pos, unsigned long chan, uint8_t switch_addr,
                      int switch_chan)
{
  uint8_t reg_val = (switch_chan < 0) ? 0 : 1 << switch_chan;

  if (i2c_wr(pos, chan, switch_addr | I2C_DEV_NOADDR, 0, 1, &reg_val) != 1)
    return (-1);

  return 0;
}

/** Switch control routine for the PCA954x I2C multiplexer chip.
 *  This routine does not support the PCA9547; see below.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param switch_addr Switch I2C address.
 * @param switch_chan Switch channel number to enable, or -1 if all
 *   channels should be disabled.
 * @return Zero if successful, or -1 if an error occurred.
 */
static int
pca954x_mux_config(pos_t pos, unsigned long chan, uint8_t switch_addr,
                   int switch_chan)
{
  uint8_t reg_val = (switch_chan < 0) ? 0 : 0x4 | switch_chan;

  if (i2c_wr(pos, chan, switch_addr | I2C_DEV_NOADDR, 0, 1, &reg_val) != 1)
    return (-1);

  return 0;
}

/** Switch control routine for the PCA9547 I2C multiplexer chip.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param switch_addr Switch I2C address.
 * @param switch_chan Switch channel number to enable, or -1 if all
 *   channels should be disabled.
 * @return Zero if successful, or -1 if an error occurred.
 */
static int
pca9547_config(pos_t pos, unsigned long chan, uint8_t switch_addr,
               int switch_chan)
{
  uint8_t reg_val = (switch_chan < 0) ? 0 : 0x8 | switch_chan;

  if (i2c_wr(pos, chan, switch_addr | I2C_DEV_NOADDR, 0, 1, &reg_val) != 1)
    return (-1);

  return 0;
}

/** Switch control routine for the PCA9541 I2C switch chip.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param switch_addr Switch I2C address.
 * @param switch_chan If nonzero, enable the bus, with this master controlling
 *  it; otherwise, disable the bus.
 * @return Zero if successful, or a negative value if an error occurred.
 */
static int
pca9541_config(pos_t pos, unsigned long chan, uint8_t switch_addr,
               int switch_chan)
{
  uint8_t cur_reg_val, new_reg_val;

  if (i2c_rd(pos, chan, switch_addr, 1, 1, &cur_reg_val) != 1)
    return (-2);

  //
  // To enable the bus, bit 2 needs to match bit 3.  To give us control
  // of the bus, bit 0 needs to be the opposite of bit 1.
  //
  if (switch_chan)
    new_reg_val = (cur_reg_val & 0xF0) | (((cur_reg_val & 0x0A) >> 1) ^ 0x04); 
  else
    new_reg_val = (cur_reg_val & 0xF1) | ((cur_reg_val & 0x08) >> 1); 

  if (cur_reg_val != new_reg_val && i2c_wr(pos, chan, switch_addr, 1, 1,
                                           &new_reg_val) != 1)
    return (-1);

  return 0;
}

/** Configure an I2C switch to enable a channel.
 * @param pos Address of the I2C master shim.
 * @param chan Channel number of the I2C master shim.
 * @param switch_addr I2C address of the switch.
 * @param switch_chan The switch channel to be enabled, or -1 if all
 *   channels should be disabled.
 * @return Zero if the func is successful, or a negative error.
 */
typedef int i2c_switch_config_func(pos_t pos, unsigned long chan,
                                   uint8_t switch_addr, int switch_chan);

/** I2C switch descriptor structure. */
struct i2c_switch_desc
{
  uint8_t init;                     /**< Nonzero if struct initialized */
  uint8_t i2c_addr;                 /**< Switch I2C address */
  uint8_t conflict_ports;           /**< Ports to be disabled after use */
  i2c_switch_config_func* config;   /**< Config routine */
  sigdesc_t reset_signal;           /**< Reset signal descriptor */
};

/** I2C arbiter descriptor structure. */
struct i2c_arbiter_desc
{
  uint8_t init;                     /**< Nonzero if we've already checked the
                                         BIB to see if there is an arbiter */
  uint8_t present;                  /**< Nonzero if there is an arbiter */
  uint8_t i2c_addr;                 /**< Arbiter I2C address */
  i2c_switch_config_func* config;   /**< Config routine */
  sigdesc_t reset_signal;           /**< Reset signal descriptor */
  sigdesc_t req_signal;             /**< Request signal descriptor */
  sigdesc_t grant_signal;           /**< Grant signal descriptor */
};

#ifdef L1BOOT
static struct i2c_switch_desc i2c_switch[MAX_I2CMS][MAX_I2C_SWITCHES];
static struct i2c_arbiter_desc i2c_arbiter[MAX_I2CMS];
#else
static struct i2c_switch_desc i2c_switch[MAX_I2CMS][MAX_I2C_SWITCHES] _SHARED;
static struct i2c_arbiter_desc i2c_arbiter[MAX_I2CMS] _SHARED;
/** This per-bus lock is held between a successful call to switch_swing and
 *  a call to switch_release, to make sure that nobody else switches the
 *  bus while we're trying to use it.  In the future we might consider
 *  merging this with i2c_lock. */
static spinlock_t i2c_bus_lock[MAX_I2CMS] _SHARED;
#endif

/** Nonzero if we need to disable the last-enabled switch when it's released. */
static uint8_t i2c_switch_conflict[MAX_I2CMS];

//
// This routine only exists in the booter; otherwise, it gets absorbed
// into the following routine.
//
#ifndef L1BOOT
static inline
#endif
int
i2c_switch_swing_boot(pos_t pos, unsigned long chan, int bus,
                      int switch_inst, int switch_chan)
{
  bi_ptr_t bufptr;
  uint32_t desc;

  if (bus < 0 || bus >= MAX_I2CMS || switch_inst < 0 ||
      (switch_inst >= MAX_I2C_SWITCHES && switch_inst !=
       BI_I2C_ADDR_SWITCH_INST__VAL_NONE))
    return -13;

  //
  // First handle any potential I2C arbiter.
  //
  if (!i2c_arbiter[bus].init)
  {
    desc = bi_getparam(BI_TYPE_I2C_ARBITER, bus, &bufptr, 0);
    i2c_arbiter[bus].init = 1;

    if (desc != BI_NULL)
    {
      //
      // Note that in the case where we don't recognize the type, we set
      // the I2C address and signals but never set present, so we won't
      // use them.
      //
      struct bi_i2c_arbiter* bi = bufptr;
      int type = bi->type;
      i2c_arbiter[bus].i2c_addr = bi->dev_addr << 1;
      i2c_arbiter[bus].reset_signal = bi->reset;
      i2c_arbiter[bus].req_signal = bi->req;
      i2c_arbiter[bus].grant_signal = bi->grant;

      // 
      // We need to initialize the signals; doing it here is a good way
      // to make sure it only gets done once.
      //
      set_signal(i2c_arbiter[bus].req_signal, SIGNAL_DEASSERT | SIGNAL_INIT);
      get_signal(i2c_arbiter[bus].grant_signal, SIGNAL_DEASSERT | SIGNAL_INIT);

      switch (type)
      {
      case BI_I2C_ARBITER_TYPE__VAL_PCA9541:
        i2c_arbiter[bus].config = pca9541_config;
        i2c_arbiter[bus].present = 1;
        break;

      default:
        return -14;
        break;
      }
    }
  }

  if (i2c_arbiter[bus].present)
  {
    //
    // Assert request and wait for grant from the arbiter.
    //
    set_signal(i2c_arbiter[bus].req_signal, SIGNAL_ASSERT);

    //
    // We might consider adding a timeout here, although it's not clear
    // what we really want to do in that case.
    //
    while (!get_signal(i2c_arbiter[bus].grant_signal, SIGNAL_ASSERT))
      ;

    //
    // Now reconfigure the actual switch.
    //
    i2c_arbiter[bus].config(pos, chan, i2c_arbiter[bus].i2c_addr, 1);
  }

  //
  // Now handle a switch.
  //
  if (switch_inst == BI_I2C_ADDR_SWITCH_INST__VAL_NONE) 
    return 0;

  if (!i2c_switch[bus][switch_inst].init)
  {
    union
    {
      bi_inst_t inst;
      struct bi_i2c_switch_inst s_inst;
    }
    u =
    {
      .s_inst.shim = bus, 
      .s_inst.switch_inst = switch_inst, 
    };
    int inst = u.inst;
    desc = bi_getparam(BI_TYPE_I2C_SWITCH, inst, &bufptr, 0);

    if (desc == BI_NULL)
    {
      return -11;
    }
    else
    {
      //
      // Note that in the case where we don't recognize the type, we
      // set the I2C address and reset signal but never set config, so
      // we won't use them.
      //
      struct bi_i2c_switch* bi = bufptr;
      int type = bi->type;
      i2c_switch[bus][switch_inst].i2c_addr = bi->dev_addr << 1;
      i2c_switch[bus][switch_inst].reset_signal = bi->reset;
      i2c_switch[bus][switch_inst].conflict_ports = bi->conflict_ports;

      switch (type)
      {
      case BI_I2C_SWITCH_TYPE__VAL_PCA954X_SWITCH:
        i2c_switch[bus][switch_inst].config = pca954x_switch_config;
        i2c_switch[bus][switch_inst].init = 1;
        break;

      case BI_I2C_SWITCH_TYPE__VAL_PCA954X_MUX:
        i2c_switch[bus][switch_inst].config = pca954x_mux_config;
        i2c_switch[bus][switch_inst].init = 1;
        break;

      case BI_I2C_SWITCH_TYPE__VAL_PCA9547:
        i2c_switch[bus][switch_inst].config = pca9547_config;
        i2c_switch[bus][switch_inst].init = 1;
        break;

      default:
        return -10;
        break;
      }
    }
  }

  if (!i2c_switch[bus][switch_inst].config)
    return -15;

  i2c_switch_conflict[bus] =
    i2c_switch[bus][switch_inst].conflict_ports & (1 << switch_chan);

  return i2c_switch[bus][switch_inst].config(pos, chan,
                                             i2c_switch[bus][switch_inst].
                                             i2c_addr,
                                             switch_chan);
}

#ifndef L1BOOT
static inline
#endif
void
i2c_switch_release_boot(pos_t pos, unsigned long chan, int bus,
                        int switch_inst)
{
  if (i2c_arbiter[bus].present)
  {
    //
    // Negate our arbiter request.  We could also decide to release the bus
    // just for neatness; not sure if that's worth the extra latency.
    //
    set_signal(i2c_arbiter[bus].req_signal, SIGNAL_DEASSERT);
  }
  
  if (i2c_switch_conflict[bus])
  {
    i2c_switch[bus][switch_inst].config(pos, chan,
                                        i2c_switch[bus][switch_inst].i2c_addr,
                                        -1);
    i2c_switch_conflict[bus] = 0;
  }
}

#ifndef L1BOOT

/** Configure an I2C switch to enable a channel.  This function locks the
 *  switch, so that other tiles will not reconfigure the switch while
 *  accesses are being made to devices through it; i2c_switch_release()
 *  must be called to release the lock when done.
 * @param bus I2C bus number.
 * @param switch_inst Instance number of the switch to be swung; relative
 *   to bus.
 * @param switch_chan Switch channel to be enabled.
 * @return Zero if the func is successful, or a negative error.
 */
int
i2c_switch_swing(int bus, int switch_inst, int switch_chan)
{
  spin_lock(&i2c_bus_lock[bus]);

  return i2c_switch_swing_boot(i2cm_info[bus]->idn_ports[0],
                               i2cm_info[bus]->channel, bus,
                               switch_inst, switch_chan);
}

/** Release the lock on an I2C switch.
 * @param bus I2C bus number.
 * @param switch_inst Instance number of the switch to be released;
 *   relative to bus.
 */
void
i2c_switch_release(int bus, int switch_inst)
{
  i2c_switch_release_boot(i2cm_info[bus]->idn_ports[0],
                          i2cm_info[bus]->channel,
                          bus, switch_inst);

  spin_unlock(&i2c_bus_lock[bus]);
}

/** Read data from an I2C device.
 * @param bus I2C master bus number.
 * @param dev 8-bit address of the device to read from.  Includes I2C_DEV_16BIT
 *        if the device takes 16-bit byte addresses.
 * @param addr Byte address within the ROM at which to start.  If not a
 *        multiple of the system word size, then the request must not cross a
 *        word boundary.
 * @param len Number of bytes to read.
 * @param buf Buffer where the data read will be placed; must be word-aligned.
 * @return Number of bytes read, or a negative value if an error occurred.
 */
int
i2c_rd_bus(int bus, int dev, int addr, int len, void* buf)
{
  return i2c_rd(i2cm_info[bus]->idn_ports[0],
                i2cm_info[bus]->channel,
                dev, addr, len, buf);
}

/** Write data to an I2C device.
 * @param bus I2C master bus number.
 * @param dev 8-bit address of the device to write to.  Includes I2C_DEV_16BIT
 *        if the device takes 16-bit byte addresses.
 * @param addr Byte address within the ROM at which to start.  If not a
 *        multiple of the system word size, then the request must not cross
 *        a word boundary.
 * @param len Number of bytes to write.
 * @param buf Buffer where the data written will be taken from; must be
 *        word-aligned.
 * @return Number of bytes written, or a negative value if an error occurred.
 */
int
i2c_wr_bus(int bus, int dev, int addr, int len, void* buf)
{
  return i2c_wr(i2cm_info[bus]->idn_ports[0],
                i2cm_info[bus]->channel,
                dev, addr, len, buf);
}

#endif
