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
 * Board information access routines.
 */

#include <stdio.h>
#include <string.h>
#include <util.h>

#include <arch/sim.h>

#include "board_info.h"
#include "devices.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "i2c_acc.h"
#include "mapping.h"
#include "srom_acc.h"
#include "types.h"

/** Address of an override BIB; if linked in, this will be used instead
 *  of searching I2C and SROM devices. */
extern uint32_t bib_override[];
#pragma weak bib_override

/** Length of an override BIB; if linked in, this will be used instead
 *  of searching I2C and SROM devices. */
extern const int bib_override_bytes;
#pragma weak bib_override_bytes

/** Read in a board information block.
 * @param blockbuf Buffer where the block will be placed.
 * @param blocklen Length in bytes of *blockbuf.
 * @param func Function which will be used to read the data.
 * @param pos Coordinates of the target shim.
 * @param chan Channel of the target shim.
 * @param dev Address of the device to read from, for shims which support
 *        multiple devices.
 * @param devlen If known, number of bytes in the target device; if not
 *        known, specify zero.
 * @param no_load If true, don't load the block, just return its length.
 * @param is_acc If true, look for an accessory information block, not
 *        a board information block.
 * @return Number of bytes in the block, -1 if no valid block found, or -2
 *        if the block found is bigger than will fit in blockbuf; the latter
 *        error will not happen if no_load is true.
 */
int
bi_read(uint32_t* blockbuf, int blocklen, bi_read_func func, pos_t pos,
        unsigned long chan, int dev, int devlen, int no_load, int is_acc)
{
  struct board_info_header head;
  struct board_info_trailer tail;
  const uint32_t *bi_magic;
  static const uint32_t aib_magic[] = { AI_MAGIC_0, AI_MAGIC_1 };
  static const uint32_t bib_magic[] = { BI_MAGIC_0, BI_MAGIC_1 };
  if (is_acc)
    bi_magic = aib_magic;
  else
    bi_magic = bib_magic;

  //
  // Read in the potential trailer.
  //
  if (func(pos, chan, dev, devlen - sizeof (tail), sizeof (tail), &tail) !=
      sizeof (tail))
    return (-1);

  //
  // See if it looks like a trailer.
  //
  if (le32_to_cpu(tail.magic1) != bi_magic[1])
    return (-1);

  //
  // See if the data would fit our buffer.
  //
  int size = le32_to_cpu(tail.nwords) * sizeof (uint32_t);
  if (size > blocklen && !no_load)
    return (-2);

  //
  // Read in the header.
  //
  if (func(pos, chan, dev, devlen - (sizeof (head) + size + sizeof (tail)),
           sizeof (head), &head) != sizeof (head))
    return (-1);

  //
  // Make sure it is a header, it matches the trailer, and it's the right
  // version.
  //
  if (le32_to_cpu(head.magic0) != bi_magic[0] || head.nwords != tail.nwords ||
      le32_to_cpu(head.version) != BI_VERSION)
    return (-1);

  //
  // If we're just checking the length, return now.
  //
  if (no_load)
    return (size);

  //
  // Read in the actual block.
  //
  if (func(pos, chan, dev, devlen - (size + sizeof (tail)), size, blockbuf) !=
      size)
    return (-1);

  //
  // Make sure the CRC is correct.
  //
  int crc = ~0;
  for (int i = 0; i < le32_to_cpu(tail.nwords); i++)
    crc = __insn_crc32_32(crc, le32_to_cpu(blockbuf[i]));

  if (~crc != le32_to_cpu(tail.crc))
    return (-1);

  //
  // Looks good, byte-swap the block if needed and return it.
  // 
#ifdef __BIG_ENDIAN__
  bi_buf_to_be(blockbuf, size);
#endif

  return (size);
}


//
// If we haven't yet tried to load the board info block, both board_info_buf
// and board_info_len are zero.  If we've tried but failed, board_info_buf
// is zero and board_info_len is negative.
//
static uint32_t* board_info_buf; /**< System board information block */
static int board_info_len;       /**< System board information block length */

#ifndef L1BOOT
//
// The shared versions of these values are filled in by the boot master
// tile, and are copied to each tile's non-shared versions when they call
// bi_load().  We do this, instead of just having everyone use the shared
// versions directly, so that bi_getparam() can panic if it's called
// before the block is loaded, without potentially touching shared memory
// before it's initialized; that's much harder to debug.
//
/** Shared version of system board information block */
static uint32_t* shared_board_info_buf _SHARED;
/** Shared version of system board information block length */
static int shared_board_info_len _SHARED;
#endif

#ifdef L1BOOT

/** Should we try to load the board information block?
 * @return Non-zero if the board information block has not been loaded;
 *   zero if it has, or if we've already tried and failed.
 */
int
bi_needs_loading()
{
  return (board_info_buf == 0 && board_info_len == 0);
}


/** Look through all of the PROMs for the board information block.
 * @param rshim Coordinates of the rshim.
 * @param addr Pointer at which we'll write the address of the device in
 *             which we found the block.  This is either a positive I2C
 *             address, or -1 if the block was found in the SPI ROM.
 * @return Length of the block in bytes, or a negative value if no valid
 *         block is found.
 */
int
bi_locate_boot(pos_t rshim, int* addr)
{
  if (&bib_override && &bib_override_bytes)
  {
    boot_printf("Overriding board's BIB\n");
    return bib_override_bytes;
  }

  for (int i = 0; i < I2C_NUM_ROMS; i++)
  {
    int tryaddr  = I2C_DEV_16BIT | I2C_ROM_ADDR(i);

    //
    // First read one byte to see if a PROM is there.  We have to do this
    // because the hardware can't cope with a multi-byte read to a device
    // that doesn't respond.
    //
    char dummy;
    if (i2c_rd(rshim, I2CMS_CHAN(0), tryaddr, 0, 1, &dummy) != 1)
      continue;

    //
    // Now see if that PROM contains a board info block.
    //
    int retval = bi_read(NULL, 0, i2c_rd, rshim, I2CMS_CHAN(0), tryaddr,
                         0, 1, 0);

    if (retval >= 0)
    {
      *addr = tryaddr;
      return (retval);
    }
  }

  //
  // None of the I2C ROMs had one; try the SROM.
  //
  int srom_dev = early_srom_get_dev(rshim.word);
  int retval = bi_read(NULL, 0, srom_rd, rshim, SROM_CHAN, srom_dev, 0, 1, 0);

  if (retval >= 0)
  {
    *addr = -1;
    return (retval);
  }

  return (-1);
}


/** Load the board information block from the I2C PROM.
 * @param rshim Coordinates of the rshim.
 * @param buf Buffer to read block into.
 * @param len Length of buf.
 * @param addr Address of the device in which the block resides (from
 *             bi_locate_boot()).
 * @return Length of the block in bytes, or a negative value if no valid
 *         block is found.
 */
int
bi_load_boot(pos_t rshim, uint32_t* buf, int len, int addr)
{
  if (&bib_override && &bib_override_bytes)
  {
    board_info_buf = bib_override;
    board_info_len = bib_override_bytes;
    return (board_info_len);
  }

  if (addr < 0)
  {
    int srom_dev = early_srom_get_dev(rshim.word);
    board_info_len = bi_read(buf, len, srom_rd, rshim, SROM_CHAN, srom_dev, 0,
                             0, 0);
  }
  else
    board_info_len = bi_read(buf, len, i2c_rd, rshim, I2CMS_CHAN(0), addr,
                             0, 0, 0);

  if (board_info_len <= 0)
    return -1;

  board_info_buf = buf;

  //
  // Now go through the block to see if we have any AIB items; if so,
  // for each one, try to find the accessory information block.  If
  // we find one, splice it into the BIB, replacing the original
  // AIB item, and modifying per-accessory items in the imported AIB
  // to have the instance number from the AIB item.
  //
  // Note that we only do this on Gx, since the Pro booter is far more
  // memory-limited; also, we historically haven't done this there, so
  // clearly it hasn't been something that people needed.
  //
  uint32_t *aiptr;
  uint32_t aidesc;
  int aipos = 0;

  while ((aidesc = bi_find(board_info_buf, board_info_len, BI_TYPE_AIB, -1,
                           &aiptr, &aipos)) != BI_NULL)
  {
    struct bi_aib* bia = (struct bi_aib*) aiptr;
    uint32_t type = bia->type;

    switch (type)
    {
    case BI_AIB_TYPE__VAL_I2C:
    {
      int aibaddr  = I2C_DEV_16BIT | (bia->u.i2c.addr.dev_addr << 1);
      //
      // We need to save these now, because once we start modifying the BIB
      // below, *bia is going to be replaced with other data.
      //
      int bus = bia->u.i2c.addr.bus;
      int inst = bia->u.i2c.addr.switch_inst;
      int chan = bia->u.i2c.addr.switch_chan;

      i2c_switch_swing_boot(rshim, I2CMS_CHAN(bus), bus, inst, chan);

      //
      // As above, first read one byte to see if a PROM is there.
      //
      char dummy;
      int aib_len = 0;
      if (i2c_rd(rshim, I2CMS_CHAN(bus), aibaddr, 0, 1, &dummy) == 1)
      {
        aib_len = bi_read(NULL, 0, i2c_rd, rshim, I2CMS_CHAN(bus),
                          aibaddr, 0, 1, 1);
      }

      if (aib_len > 0)
      {
        //
        // Move remainder of BIB down to make room for accessory items.
        //
        memmove(aiptr - 1 +
                ROUND_UP(aib_len, sizeof (*aiptr)) / sizeof (*aiptr),
                aiptr + BI_WDS(aidesc),
                board_info_len - (aiptr - 1 - board_info_buf) * sizeof (*aiptr) -
                (1 + BI_BYTES(aidesc)));

        //
        // Adjust our position so we hit the first of the newly added items
        // on our next probe.  Note that aipos is a word index within the
        // BIB, so we subtract 1 for the item descriptor and the number of
        // words in the payload.
        //
        aipos -= 1 + BI_WDS(aidesc);

        //
        // Read accessory items into the space we just made.
        //
        aib_len = bi_read(aiptr - 1, aib_len, i2c_rd, rshim, I2CMS_CHAN(bus),
                          aibaddr, 0, 0, 1);
        board_info_len += aib_len - 4 - BI_BYTES(aidesc);

        //
        // Patch instance numbers in accessory block.  Note that aiptr points
        // to the payload of the item we just replaced, so we need to back
        // it up by 1 word in order to start at the descriptor for the first
        // item.
        //
        int ai_idx = 0;
        uint32_t* ai = aiptr - 1;
        while (ai_idx < ROUND_UP(aib_len, sizeof (*aiptr)) / sizeof (*aiptr))
        {
          if (BI_INST(ai[ai_idx]) == BI_INST_AIB)
            ai[ai_idx] = BI_MKDESC(BI_TYPE(ai[ai_idx]), BI_INST(aidesc),
                                   BI_WDS(ai[ai_idx]));
          ai_idx += 1 + BI_WDS(ai[ai_idx]);
        }
      }

      i2c_switch_release_boot(rshim, I2CMS_CHAN(bus), bus, inst);

      break;
    }

    default:
      break;
    }

  }

  return (board_info_len);
}


uint32_t
bi_getparam(int type, int instance, bi_ptr_t* resbuf, int* offset)
{
  //
  // If the block hasn't been loaded, fail.
  //
  if (!board_info_buf)
    return (BI_NULL);

  //
  // Do the requested search against the system block.
  //
  return (bi_find(board_info_buf, board_info_len, type, instance,
                  (uint32_t**) resbuf, offset));
}

#else /* L1BOOT */

static int board_info_in_srom _SHARED;   /**< Nonzero if BIB came from SROM */

/** Return data which should be prepended to a hardware BIB, in order to
 *  augment or override the items found there.  This is primarily used for
 *  older cards whose BIBs might not have contained all of the data needed to
 *  describe the card; this allows the rest of the hypervisor to deal with
 *  them in a uniform manner.
 * @param blockbuf Block to prepend to.
 * @param blocklen Length in bytes of block to prepend to.
 * @param prepend_ptr Pointer to location filled in with pointer to data to
 *        be prepended.
 * @return Number of bytes of data to prepend, or 0 if no prepend is needed.
 *        This will always be an integral number of words.
 */
static int
bi_get_prepend(uint32_t* bi, int bi_len, const uint32_t** prepend_ptr)
{
  //
  // Table translating part/rev numbers to sets of prepended items.
  //
  static const struct rev2prepend
  {
    const char* pn;            /** Part number (NULL for end of table) */
    const char* rev;           /** Revision (NULL matches any) */
    const uint32_t* prepend;   /** Pointer to items to prepend */
    const int prepend_len;     /** Size in bytes of prepended items */
  }
  r2p_table[] =
  {
    // End of list
    {
      .pn = NULL,
    },
  };

  //
  // Now do the actual translation.
  //
  uint32_t *pnptr;
  uint32_t pndesc = bi_find(bi, bi_len, BI_TYPE_BOARD_PART_NUM, -1, &pnptr,
                            NULL);

  if (pndesc != BI_NULL)
  {
    uint32_t *revptr;
    uint32_t revdesc = bi_find(bi, bi_len, BI_TYPE_BOARD_REV, -1, &revptr,
                               NULL);

    const struct rev2prepend* r2p;
    for (r2p = r2p_table; r2p->pn; r2p++)
    {
      if (!strncmp(r2p->pn, (char*) pnptr, BI_BYTES(pndesc)))
      {
        if (!r2p->rev)
          break;
        if (revdesc == BI_NULL)
          continue;
        if (!strncmp(r2p->rev, (char*) revptr, BI_BYTES(revdesc)))
          break;
      }
    }

    if (r2p->pn)
    {
      *prepend_ptr = r2p->prepend;
      return (r2p->prepend_len);
    }
  }

  return (0);
}


/** If we're the master, load the board information block into the shared
 *  buffer; no matter who we are, point our static pointers at that buffer.
 */
void
bi_load()
{
  //
  // If we aren't the master, we just need to copy the shared pointers.
  //
  if (!is_master)
  {
    board_info_buf = shared_board_info_buf;
    board_info_len = shared_board_info_len;
    return;
  }

  //
  // On Gx, we can actually see the SROM in the simulator, but we don't
  // want to try to load the BIB from there, so we'll just return.
  //
  if (sim_is_simulator())
  {
    board_info_len = shared_board_info_len = -1;
    return;
  }

  //
  // Load the block onto the stack, fail if we can't do so.  We look in
  // the I2C PROMs if the board flags tell us to, otherwise in the SPI PROM.
  // Note that we always enable I2C if we found the shim, even if the BIB
  // isn't there, since AIBs might be.
  //
  uint32_t bi[BI_MAX_WDS];

  for (int i = 0; i < MAX_I2CMS; i++)
    if (i2cm_info[i])
      i2c_enable(i2cm_info[i]->idn_ports[0], i2cm_info[i]->channel);

  if (&bib_override && &bib_override_bytes)
  {
    shared_board_info_buf = board_info_buf = bib_override;
    shared_board_info_len = board_info_len = bib_override_bytes;
    return;
  }
  else if (board_flags & BOARD_BI_I2C)
  {
    if (!i2cm_info[0])
    {
      board_info_len = shared_board_info_len = -1;
      return;
    }

    for (int i = 0; i < I2C_NUM_ROMS; i++)
    {
      int tryaddr  = I2C_DEV_16BIT | I2C_ROM_ADDR(i);

      //
      // First read one byte to see if a PROM is there.  We have to do this
      // because the hardware can't cope with a multi-byte read to a device
      // that doesn't respond.
      //
      char dummy;
      if (i2c_rd(i2cm_info[0]->idn_ports[0], i2cm_info[0]->channel,
                 tryaddr, 0, 1, &dummy) != 1)
        continue;

      board_info_len = bi_read(bi, sizeof (bi), i2c_rd,
                               i2cm_info[0]->idn_ports[0],
                               i2cm_info[0]->channel, tryaddr, 0, 0, 0);
      if (board_info_len > 0)
        break;
    }
  }
  else
  {
    if (!srom_info)
    {
      board_info_len = shared_board_info_len = -1;
      return;
    }

    int srom_dev = srom_get_dev(srom_info->idn_ports[0], srom_info->channel);
    board_info_len = bi_read(bi, sizeof (bi), srom_rd,
                             srom_info->idn_ports[0], srom_info->channel,
                             srom_dev, 0, 0, 0);
    if (board_info_len > 0)
      board_info_in_srom = 1;
  }

  if (board_info_len < 0)
  {
    shared_board_info_len = board_info_len;
    return;
  }

  //
  // See whether we need to prepend anything to the block.
  //
  const uint32_t* prepend_ptr = NULL;
  int prepend_len = bi_get_prepend(bi, board_info_len, &prepend_ptr);

  //
  // If we have anything to prepend, then move the block down, and
  // copy the prepended data in front of it.  We need to do this here
  // so that we can prepend AIB items.
  //
  if (prepend_len)
  {
    memmove((char *) bi + prepend_len, bi, board_info_len);
    memcpy(bi, prepend_ptr, prepend_len);
    board_info_len += prepend_len;
  }

  //
  // Now go through the block to see if we have any AIB items; if so,
  // for each one, try to find the accessory information block.  If
  // we find one, splice it into the BIB, replacing the original
  // AIB item, and modifying per-accessory items in the imported AIB
  // to have the instance number from the AIB item.
  //
  uint32_t *aiptr;
  uint32_t aidesc;
  int aipos = 0;
  uint32_t ai[BI_MAX_WDS];

  //
  // Calls to i2c_switch_swing may do BIB lookups, so we need to make
  // our BIB available while it's being built.
  //
  board_info_buf = bi;

  while ((aidesc = bi_find(bi, board_info_len, BI_TYPE_AIB, -1, &aiptr,
                           &aipos)) != BI_NULL)
  {
    int aib_len = 0;
    struct bi_aib* bia = (struct bi_aib*) aiptr;
    uint32_t type = bia->type;

    switch (type)
    {
    case BI_AIB_TYPE__VAL_I2C:
    {
      int aibaddr = I2C_DEV_16BIT | (bia->u.i2c.addr.dev_addr << 1);
      int bus = bia->u.i2c.addr.bus;

      i2c_switch_swing(bia->u.i2c.addr.bus, bia->u.i2c.addr.switch_inst,
                       bia->u.i2c.addr.switch_chan);

      //
      // As above, first read one byte to see if a PROM is there.
      //
      char dummy;
      if (i2c_rd(i2cm_info[bus]->idn_ports[0], i2cm_info[bus]->channel,
                 aibaddr, 0, 1, &dummy) == 1)
        aib_len = bi_read(ai, sizeof (ai), i2c_rd,
                          i2cm_info[bus]->idn_ports[0],
                          i2cm_info[bus]->channel, aibaddr, 0, 0, 1);

      i2c_switch_release(bia->u.i2c.addr.bus, bia->u.i2c.addr.switch_inst);

      if (aib_len <= 0)
        continue;

      //
      // Move remainder of BIB down to make room for accessory items
      //
      memmove(aiptr - 1 +
              ROUND_UP(aib_len, sizeof (*aiptr)) / sizeof (*aiptr),
              aiptr + BI_WDS(aidesc),
              board_info_len - (aiptr - 1 - bi) * sizeof (*aiptr) -
              (1 + BI_BYTES(aidesc)));

      //
      // Adjust our position so we hit the first of the newly added items
      // on our next probe.  Note that aipos is a word index within the
      // BIB, so we subtract 1 for the item descriptor and the number of
      // words in the payload.
      //
      aipos -= 1 + BI_WDS(aidesc);

      //
      // Patch instance numbers in accessory block
      //
      int ai_idx = 0;
      while (ai_idx < ROUND_UP(aib_len, sizeof (*aiptr)) / sizeof (*aiptr))
      {
        if (BI_INST(ai[ai_idx]) == BI_INST_AIB)
          ai[ai_idx] = BI_MKDESC(BI_TYPE(ai[ai_idx]), BI_INST(aidesc),
                                 BI_WDS(ai[ai_idx]));
        ai_idx += 1 + BI_WDS(ai[ai_idx]);
      }

      //
      // Copy in patched accessory items
      //
      memcpy(aiptr - 1, ai, aib_len);
      board_info_len += aib_len - 4 - BI_BYTES(aidesc);

      break;
    }

    default:
      break;
    }
  }

  //
  // Allocate permanent storage for the block and copy it there.
  //
  board_info_buf = shared_alloc(board_info_len, 0);
  if (!board_info_buf)
  {
    shared_board_info_len = board_info_len = -1;
    return;
  }

  memcpy(board_info_buf, bi, board_info_len);

  shared_board_info_buf = board_info_buf;
  shared_board_info_len = board_info_len;
}


uint32_t
bi_getparam(int type, int instance, bi_ptr_t* resbuf, int* offset)
{
  //
  // If we haven't tried to load the system block yet, panic.
  //
  if (!board_info_buf && !board_info_len)
    panic("bi_getparam() called before bi_load()\n");

  //
  // Do the requested search against the system block.
  //
  return (bi_find(board_info_buf, board_info_len, type, instance,
                  (uint32_t**) resbuf, offset));
}


/** Dump out the system board information block. */
void
bi_dump()
{
  //
  // If the system block is loaded, dump it out, else complain.
  //
  if (board_info_buf)
    bi_dumpbuf(board_info_buf, board_info_len);
  else
    printf("Can't dump board information block, no or invalid block present\n");
}

/** Get the length of the board information block.
 * @return The length of the board information block.
 */
int
bi_block_length()
{
  if (!board_info_buf)
    return 0;

  return board_info_len;
}

/** Copy the board information block from memory to the destination buffer.
 * @param buf Buffer to read block into.
 * @param len Length of buf.
 * @return Length of the copy in bytes, or a negative value if no valid
 *         block is found.
 */
int
bi_block_copy(uint32_t* buf, int len)
{
  if (!board_info_buf)
    return -1;

  int copy_len = len < board_info_len ? len : board_info_len;

  memcpy(buf, board_info_buf, copy_len);
  return copy_len;
}

/** Determine whether the board information block came from SROM.
 * @return Nonzero if the BIB came from ROM, zero otherwise.
 */
int
bi_in_srom()
{
  return board_info_in_srom;
}

#endif /* L1BOOT */
