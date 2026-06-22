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
 * The SROM device table.
 */

#ifndef _SYS_HV_SROM_TABLE_H
#define _SYS_HV_SROM_TABLE_H

#include "types.h"

/** SPI ROM descriptor. */
struct srom
{
  /** Manufacturer/model information for this SROM. */
  char* name;

  /** ID bytes read from the SROM.  5 bytes, in big-endian format (so the
   *  low byte is the last byte read from the device, and bits <39:32> are
   *  the first byte read from the device). */
  uint64_t id;

  /** Mask of ID bits to consider when matching against this entry; same
   *  format as the ID. */
  uint64_t id_mask;

  /** SROM page size (maximum bytes written with PAGE PROGRAM command), in
   *  bytes. */
  uint32_t page_size;

  /** SROM sector size (number of bytes erased with the SE0 command), in
   *  bytes. */
  uint32_t sector_size;

  /** Total size of the SROM, in bytes. */
  uint32_t srom_size;

  /** Instruction code for a RDSR-like instruction that reads the high bits
   *  of the SROM address for SROMs with more than 24 address bits (i.e.,
   *  larger than 16 MB). */
  uint8_t a24_rcmd;

  /** Instruction code for a WRSR-like instruction that writes the high bits
   *  of the SROM address for SROMs with more than 24 address bits (i.e.,
   *  larger than 16 MB). */
  uint8_t a24_wcmd;
};

/** SPI ROM descriptor, cut-down version for the booter.  Note that there
 *  is code in the assembly SROM accessors (tilegx/srom_acc_asm.h) which
 *  knows the format of this structure; any changes made here must be
 *  reflected there.  The dependencies are either in srom_get_dev(), or in
 *  references to the r_srom_dev register.
 */
struct srom_boot
{
  /** ID bytes read from the SROM.  5 bytes, in big-endian format (so the
   *  low byte is the last byte read from the device, and bits <39:32> are
   *  the first byte read from the device). */
  uint64_t id;

  /** Mask of ID bits to consider when matching against this entry; same
   *  format as the ID. */
  uint64_t id_mask;

  /** Shift count which, when applied as a right shift to a 64-bit word of all
   *  1's, produces a mask the width of the srom address. */
  uint8_t srom_size_mask_shift;

  /** Instruction code for a RDSR-like instruction that reads the high bits
   *  of the SROM address for SROMs larger than 16 MB. */
  uint8_t a24_rcmd;

  /** Instruction code for a WRSR-like instruction that writes the high bits
   *  of the SROM address for SROMs larger than 16 MB. */
  uint8_t a24_wcmd;
};

/** The SROM table. */
extern struct srom srom_table[];

/** End of the SROM table. */
extern struct srom srom_table_end[];

/** Attribute for an SROM table. */
#define __SROM_ATTR __attribute__((used, section(".srom_table")))


#ifdef L1BOOT

/** Start an SROM table. */
#define SROM_TABLE_START \
  static const __SROM_ATTR struct srom_boot _srom_table[] = {

/** Finish an SROM table. */
#define SROM_TABLE_END \
  };

/** Define a standard SROM entry (size <= 16 MB).  See the definition of
 *  struct srom above for precise meanings of each parameter. */
#define SROM_ENTRY(_name, _id, _id_mask, _page_size, _sector_size, \
                   _srom_size)

/** Define a large SROM entry (size > 16 MB).  See the definition of
 *  struct srom above for precise meanings of each parameter. */
#define SROM_ENTRY_LG(_name, _id, _id_mask, _page_size, _sector_size, \
                      _srom_size, _a24_rcmd, _a24_wcmd) \
{ \
  .id = _id, \
  .id_mask = _id_mask, \
  .srom_size_mask_shift = 64 - __builtin_ctz(_srom_size), \
  .a24_rcmd = _a24_rcmd, \
  .a24_wcmd = _a24_wcmd, \
},

#else // L1BOOT

/** Start an SROM table. */
#define SROM_TABLE_START \
  static const __SROM_ATTR struct srom _srom_table[] = {

/** Finish an SROM table. */
#define SROM_TABLE_END \
  };

/** Define a standard SROM entry (size <= 16 MB).  See the definition of
 *  struct srom above for precise meanings of each parameter. */
#define SROM_ENTRY(_name, _id, _id_mask, _page_size, _sector_size, \
                   _srom_size) \
{ \
  .name = _name, \
  .id = _id, \
  .id_mask = _id_mask, \
  .page_size = _page_size, \
  .sector_size = _sector_size, \
  .srom_size = _srom_size, \
},

/** Define a large SROM entry (size > 16 MB).  See the definition of
 *  struct srom above for precise meanings of each parameter. */
#define SROM_ENTRY_LG(_name, _id, _id_mask, _page_size, _sector_size, \
                      _srom_size, _a24_rcmd, _a24_wcmd) \
{ \
  .name = _name, \
  .id = _id, \
  .id_mask = _id_mask, \
  .page_size = _page_size, \
  .sector_size = _sector_size, \
  .srom_size = _srom_size, \
  .a24_rcmd = _a24_rcmd, \
  .a24_wcmd = _a24_wcmd, \
},

#endif // L1BOOT

#endif /* _SYS_HV_SROM_TABLE_H */
