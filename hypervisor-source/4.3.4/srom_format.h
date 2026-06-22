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
 * Format of an SROM which uses the multi-image SROM booter.
 */

#ifndef _SROM_FORMAT_H
#define _SROM_FORMAT_H

#include <stdint.h>

#include <arch/boot_shm.h>

//
// The overall format of a multi-image-boot SROM is as follows:
//
// - At the very start of the SROM comes the overall SROM header
//   (struct srom_overall_header).  This contains data about the SROM's
//   characteristics, as well as data about the SROM booter.
//
// - After the header comes the SROM booter itself.  The first part of
//   the booter is encoded in the normal hardware level-0 boot format;
//   there may be later code which is encoded in the hypervisor boot format.
//
// - After the SROM booter comes the boot trailer (struct
//   srom_boot_trailer).  This is presented to the SROM booter via the
//   network boot stream, so, to make things easier for the booter,
//   it replicates some of the configuration values from the overall header.
//   It also includes pointers to the actual boot images themselves.
//
// - The boot images are placed in arbitrary locations in the SROM.
//   (To make them easier to replace, it's advisable to ensure that any
//   SROM sector is only part of one boot image, but the booter does not
//   currently depend upon this.)
//
// - Each boot image consists of a header (struct srom_image_header),
//   which contains data describing the image, and the image itself.
//   The header contains an offset to the image, so the two do not need
//   to be contiguous, but typically the image is placed right after
//   the header.
//

//
// Warning:
//
// SROM booter images installed in SROMs in customer systems will depend
// upon these data structures.  It may be difficult or impossible for those
// booters to be updated; thus, incompatible changes to the structures
// are inadvisable.
//

/** Magic number for the overall SROM header. */
#define SROM_HEADER_MAGIC     0x0148734f   // "OsH\001"

/** Header at the very start of the SROM. */
struct srom_overall_header
{
  /** Hardware boot header. */
  BOOT_ROM_HEADER_t rom_header;

  /** Hardware segment header; pipes all of the following words to the
      Rshim scratchpad register. */
  BOOT_ROM_DESC_t rom_seg_header;

  /** Magic number (SROM_IMAGE_MAGIC). */
  uint32_t magic;

  /** SROM sector size in bytes. */
  int32_t sector_size;

  /** SROM page size in bytes. */
  int32_t page_size;

  /** SROM total size in bytes. */
  int32_t srom_size;

  /** Number of image slots in the SROM. */
  int32_t num_images;

  /** Byte offset to first word of the SROM booter, from the first word
      of this header. */
  uint32_t offset;

  /** Length in words of the SROM booter, not including this header, but
      including all of the RomSegHeaders as well as the trailer which follows
      the booter. */
  int32_t booter_words;

  /** CRC of the SROM booter, not including this header but including the
      things covered by booter_words, above. */
  uint32_t booter_crc;

  /** Comment string.  Null-terminated unless there's no room for a null. */
  char comment[80];

  /** Flags (SROM_HDR_FLG_xxx). */
  uint32_t flags;

  /** Time that the SROM booter was originally added to the ROM; this is a
      standard Unix time_t.  May be zero, and thus invalid, if we didn't
      think the clock was accurate at that time. */
  uint32_t timestamp;

  /** Dummy word to make the length of this structure a multiple of 8; this
   * isn't actually needed for Tile code, since there's a 64-bit ROM header
   * at the top of the structure, but when building tile-sbim on x86, that
   * doesn't force the alignment and thus the size we want.  If items are
   * added to this structure, this padding might be removable. */
  uint32_t padding;

  /** CRC of the entire header (except for this word). */
  uint32_t header_crc;
};

/** If this flag is set, the space between this header and the booter
 *  contains extra ROM segments; the last word of that region is the CRC
 *  of the previous words in the region. */
#define SROM_HDR_FLG_XSEG     0x1

/** Magic number for SROM booter trailer. */
#define SROM_TRAILER_MAGIC   0x01546253   // "SbT\001"

/** Data which follows the SROM booter. */
struct srom_boot_trailer
{
  /** Magic number (SROM_TRAILER_MAGIC). */
  uint32_t magic;

  /** SROM sector size. */
  int32_t sector_size;

  /** SROM page size. */
  int32_t page_size;

  /** SROM total size. */
  int32_t srom_size;

  /** Number of image slots in the SROM. */
  int32_t num_images;

  //
  // This structure is followed by:
  // - One word for each image slot, giving the absolute byte address of
  //   its header.
  // - A CRC of the entire trailer including the slot addresses.
  //
};

/** Size of the complete trailer including image pointers and CRC. */
#define SROM_TRAILER_SIZE(num_images) (sizeof (struct srom_boot_trailer) + \
                                       4 * (1 + (num_images)))


/** Magic number for SROM image headers. */
#define SROM_IMAGE_MAGIC     0x01486953   // "SiH\001"

/** Header for each SROM boot image. */
struct srom_image_header
{
  /** Magic number (SROM_IMAGE_MAGIC). */
  uint32_t magic;

  /** Generation number of this SROM image.  Whenever a new image is added
      or promoted, it gets a generation number larger than any currently
      valid number. */
  int32_t generation;

  /** Byte offset to first word of this SROM image, from the first word
      of this header.  Note that this is signed, so the image might be
      earlier in the ROM than the header is.  */
  int32_t offset;

  /** Length in words of the level-0-bootable part of this SROM image. */
  int32_t l0_boot_words;

  /** CRC of the level-0-bootable part of this SROM image. */
  uint32_t l0_boot_crc;

  /** Length in words of this SROM image, not including this header. */
  int32_t total_words;

  /** CRC of the SROM image, not including this header. */
  uint32_t total_crc;

  /** Comment string.  Null-terminated unless there's no room for a null. */
  char comment[80];

  /** Flags (SROM_IMAGE_FLG_xxx). */
  uint32_t flags;

  /** Time that this image was originally added to the ROM; this is a
      standard Unix time_t.  May be zero, and thus invalid, if we didn't
      think the clock was accurate at that time. */
  uint32_t timestamp;

  /** CRC of the header up to this point (except for this word).  Kept
   *  for compatibility with old versions of sbim. */
  uint32_t header_crc;

  //
  // First set of extension items.  These were not present in the initial
  // version of the image header format, and are only present if
  // SROM_IMAGE_FLG_EXT1 is set in flags.
  //

  /** SHA-1 digest of the SROM image, not including any headers. */
  uint8_t sha1_digest[20];

  /** Reserved for future use; must be zero. */
  uint8_t reserved[40];

  /** CRC of the entire header (except for this word). */
  uint32_t header_crc_ext1;
};

/** If this flag is set, this image should not be selected for automatic
 *  replacement. */
#define SROM_IMAGE_FLG_LOCK     0x1

/** If this flag is set, the first set of extension items are present in
 *  the header. */
#define SROM_IMAGE_FLG_EXT1     0x2

#endif /* _SROM_FORMAT_H */
