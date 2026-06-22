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
 * Definition of the board information block (and the accessory information
 * block), plus definition of routines to access board information.
 */

#ifndef _SYS_HV_BOARD_INFO_H
#define _SYS_HV_BOARD_INFO_H

#include "types.h"

/** Header for board/accessory information block. */
#include "hvbme/board_info.h"

// Magic numbers for a board information block.
#define BI_MAGIC_0 0x30426942  /**< Header magic number ("BiB0"). */
#define BI_MAGIC_1 0x31426942  /**< Trailer magic number ("BiB1"). */

// Magic numbers for an accessory information block.
#define AI_MAGIC_0 0x30624961  /**< Header magic number ("aIb0"). */
#define AI_MAGIC_1 0x31624961  /**< Trailer magic number ("aIb1"). */

#define BI_VERSION 1           /**< Version number of block structure. */

/** Header for board information block. */
struct board_info_header
{
  /** Magic number (BI_MAGIC_0/AI_MAGIC_0). */
  uint32_t magic0;
  /** Version (BI_VERSION). */
  uint32_t version;
  /** Number of words in block, not counting header or trailer. */
  uint32_t nwords;
};

/** Trailer for board/accessory information block. */
struct board_info_trailer
{
  /** CRC32 of words in block, not including header or trailer. */
  uint32_t crc;
  /** Number of words in block, not counting header or trailer. */
  uint32_t nwords;
  /** Magic number (BI_MAGIC_1/AI_MAGIC_1). */
  uint32_t magic1;
};


/** Function which is called to read data from the device on which the block
 *  is resident.
 * @param pos Coordinates of the target shim.
 * @param chan Channel number of the target shim.
 * @param addr Byte address within the device at which to start.
 * @param dev Address of the device to read from, for shims which support
 *            multiple devices.
 * @param len Number of bytes to read.
 * @param buf Buffer where the data read will be placed.
 * @return Number of bytes read, or a negative value if an error occurred.
 */
typedef int (*bi_read_func)(pos_t pos, unsigned long chan, int dev, int
                            addr, int len, void* buf);

int bi_read(uint32_t* blockbuf, int blocklen, bi_read_func func, pos_t pos,
            unsigned long chan, int dev, int devlen, int no_load, int is_acc);

#ifdef L1BOOT
int bi_needs_loading(void);
int bi_locate_boot(pos_t rshim, int* addr);
int bi_load_boot(pos_t rshim, uint32_t* buf, int len, int addr);
#else
void bi_load(void);
#endif

/** Dump out the system board information block. */
void bi_dump(void);

/** Get the length of the board information block.
 * @return The length of the board information block, in bytes.
 */
int bi_block_length(void);

/** Copy the board information block from memory to the destination buffer.
 * @param buf Buffer to read block into.
 * @param len Length of buf.
 * @return Length of the copy in bytes, or a negative value if no valid
 *         block is found.
 */
int bi_block_copy(uint32_t* buf, int len);

/** Determine whether the board information block came from SROM.
 * @return Nonzero if the BIB came from ROM, zero otherwise.
 */
int bi_in_srom(void);

#endif /* _SYS_HV_BOARD_INFO_H */
