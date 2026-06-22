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
 * Board information definitions common to all chips.
 */

#ifndef _SYS_COMMON_HVBME_BOARD_INFO_COMMON_H
#define _SYS_COMMON_HVBME_BOARD_INFO_COMMON_H

#include <stdint.h>

#define BI_MAX_WDS 1024        /**< Maximum length of an info block. */

/** Descriptor which means "item not found". */
#define BI_NULL  ~0

/** Find a descriptor in a board information block.
 * @param blockbuf Block to search.
 * @param blocklen Length in bytes of the block.
 * @param type Type of descriptor to look for, or -1 to match any.
 * @param instance Instance number to look for, or -1 to match any.
 * @param resbuf Pointer to where a pointer to the found data is placed.
 * @param offset If NULL, the search will start at the beginning of the
 *        block.  Otherwise, points to an offset at which the search will
 *        start; if the item requested is found, the offset will be updated
 *        so that a subsequent search will start after the last-found item.
 *        This offset is opaque to the user; the only defined value is zero,
 *        which means "start searching at the beginning of the block".
 * @return The descriptor for the item, if found, or BI_NULL otherwise.
 */
uint32_t bi_find(uint32_t* blockbuf, int blocklen, int type, int instance,
                 uint32_t** resbuf, int* offset);

/** Dump out a board information block.
 * @param blockbuf Block to dump.
 * @param blocklen Length in bytes of the block.
 */
void bi_dumpbuf(uint32_t* blockbuf, int blocklen);

#ifdef __BIG_ENDIAN__

/** Translate a board information block to big-endian format.
 * @param blockbuf Block to translate.
 * @param blocklen Length in bytes of the block.
 */
void bi_buf_to_be(uint32_t* blockbuf, int blocklen);

#endif

/** Pointer to board information block item data. */
typedef void* bi_ptr_t;

/** Find a descriptor in the system board information block.
 * @param type Type of descriptor to look for, or -1 to match any.
 * @param instance Instance number to look for, or -1 to match any.
 * @param resbuf Pointer to where a pointer to the found data is placed.
 * @param offset If NULL, the search will start at the beginning of the
 *        block.  Otherwise, points to an offset at which the search will
 *        start; if the item requested is found, the offset will be updated
 *        so that a subsequent search will start after the last-found item.
 *        This offset is opaque to the user; the only defined value is zero,
 *        which means "start searching at the beginning of the block".
 * @return The descriptor for the item, if found, or BI_NULL otherwise.
 */
uint32_t bi_getparam(int type, int instance, bi_ptr_t* resbuf, int* offset);

#endif /* _SYS_COMMON_HVBME_BOARD_INFO_COMMON_H */
