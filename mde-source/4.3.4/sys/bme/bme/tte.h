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
 *
 * Bare Metal Environment translation table entry (TTE) support.  A translation
 * table entry is used in either the instruction or data TLB to translate
 * virtual addresses to physical addresses.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_TTE_H
#define _SYS_BME_TTE_H

#include <features.h>

#include <arch/spr.h>

__BEGIN_DECLS

/** Extract a virtual page frame number from a VA */
#define VPFN(va)        ((va) >> 12)
/** Extract low page frame number bits from a physical address */
#define PFN_LO(pa)      (((pa) & 0xFFFFFFFF) >> 12)
/** Extract high page frame number bits from a physical address */
#define PFN_HI(pa)      ((pa) >> 32)

// Define tte codes for all valid page sizes.

#define TTE_PS_4K       0  /**< 4K page size code */
#define TTE_PS_16K      1  /**< 16K page size code */
#define TTE_PS_64K      2  /**< 64K page size code */
#define TTE_PS_256K     3  /**< 256K page size code */
#define TTE_PS_1M       4  /**< 1M page size code */
#define TTE_PS_4M       5  /**< 4M page size code */
#define TTE_PS_16M      6  /**< 16M page size code */

// Define shift values for all valid page sizes.

#define TTE_PG_SHIFT_4K     12  /**< 4K page shift */
#define TTE_PG_SHIFT_16K    14  /**< 16K page shift */
#define TTE_PG_SHIFT_64K    16  /**< 64K page shift */
#define TTE_PG_SHIFT_256K   18  /**< 256K page shift */
#define TTE_PG_SHIFT_1M     20  /**< 1M page shift */
#define TTE_PG_SHIFT_4M     22  /**< 4M page shift */
#define TTE_PG_SHIFT_16M    24  /**< 16M page shift */

/** Convert log2 of a page size to its page size code */
#define TTE_SHIFT_TO_PS(x)      (((x) - 12) >> 1)
/** Covert a page size code to log2 of the size of the page */
#define TTE_PS_TO_SHIFT(x)      (((x) << 1) + 12)

/** The full data translation table entry.  Note that while we are using the
 *  DTLB versions of the registers here, this entry is usable for the ITLB
 *  as well, since the DTLB is a strict superset of the ITLB. */
/** The full translation table entry. */
typedef struct {
  SPR_DTLB_CURRENT_ATTR_t w0;   /**< Word 0 of the TTE */
  SPR_DTLB_CURRENT_VA_t w1;     /**< Word 1 of the TTE */



  SPR_DTLB_CURRENT_PA_t w2;     /**< Word 2 of the TTE */

} tte_t;

__END_DECLS

#endif

/** @} */
