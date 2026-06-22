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
 * Definitions relating to pages and page tables.
 */

#ifndef _SYS_HV_PAGE_H
#define _SYS_HV_PAGE_H

#include <hv/hypervisor.h>

#include "bits.h"
#include "param.h"
#include "hvbme/shared_lock.h"

// Define shift values for all valid page sizes.

#define PG_SHIFT_4K     12  /**< 4 K page shift */
#define PG_SHIFT_16K    14  /**< 16 K page shift */
#define PG_SHIFT_64K    16  /**< 64 K page shift */
#define PG_SHIFT_256K   18  /**< 256 K page shift */
#define PG_SHIFT_1M     20  /**< 1 M page shift */
#define PG_SHIFT_4M     22  /**< 4 M page shift */
#define PG_SHIFT_16M    24  /**< 16 M page shift */
#define PG_SHIFT_64M    26  /**< 64 M page shift */
#define PG_SHIFT_256M   28  /**< 256 M page shift */
#define PG_SHIFT_1G     30  /**< 1 G page shift */
#define PG_SHIFT_4G     32  /**< 4 G page shift */
#define PG_SHIFT_16G    34  /**< 16 G page shift */
#define PG_SHIFT_64G    36  /**< 64 G page shift */

#define PG_SHIFT_MAX    PG_SHIFT_64G  /**< Largest page size */

/** Hypervisor code page size */
#define HV_CODE_PAGE_SIZE    (1 << HV_CODE_PAGE_SHIFT)
/** Hypervisor data page size */
#define HV_DATA_PAGE_SIZE    (1 << HV_DATA_PAGE_SHIFT)
/** Hypervisor filesystem page size */
#define HV_FS_PAGE_SIZE      (1 << HV_FS_PAGE_SHIFT)
/** Hypervisor shared page size */
#define HV_SHARED_PAGE_SIZE  (1 << HV_SHARED_PAGE_SHIFT)
/** Hypervisor flush page size */
#define HV_FLUSH_PAGE_SIZE   (1 << HV_FLUSH_PAGE_SHIFT)
/** Hypervisor client-shared page size */
#define HV_CLIENT_SHARED_PAGE_SIZE  (1 << HV_CLIENT_SHARED_PAGE_SHIFT)

#ifndef __DOXYGEN__
#if HV_FLUSH_PAGE_SIZE < (2 * CHIP_L2_CACHE_SIZE())
#error HV flush page size must be increased
#endif
#endif

/**
 * This is the VA we map our temporary L2-flushing page into.  It's the very
 * end of our address space, except for the region used by client-shared pages.
*/
#define HV_FLUSH_VA  (ROUND_DN((HV_VA_LIMIT - (HV_NUM_CLIENT_SHARED_PAGES << \
                                               HV_CLIENT_SHARED_PAGE_SHIFT)), \
                               HV_FLUSH_PAGE_SIZE) - HV_FLUSH_PAGE_SIZE)

/**
 * This is the VA into which we map the memory for spin locks shared between
 * the HV and the BME.
 */
#define HV_BME_SHARED_PAGE_VA (ROUND_DN(HV_FLUSH_VA, \
                                        HV_BME_SHARED_PAGE_SIZE) - \
                               HV_BME_SHARED_PAGE_SIZE)

/**
 * This is the VA into which we map the HVFS, a page at a time.
 */
#define HV_FS_VA (ROUND_DN(HV_BME_SHARED_PAGE_VA, HV_FS_PAGE_SIZE) - \
                  HV_FS_PAGE_SIZE)

/** Number of pages allocated for mapping remote tiles' memory. */
#define HV_PDATA_NPAGES 2

/** This is the start of the VA range into which we map other tiles'
 *  private data. */
#define HV_PDATA_VA (ROUND_DN(HV_FS_VA, HV_DATA_PAGE_SIZE) - \
                     HV_PDATA_NPAGES * HV_DATA_PAGE_SIZE)

/** VA where the VA allocator starts (we build down from here). */
#define HV_ALLOC_VA HV_PDATA_VA

#if HV_CODE_PAGE_SHIFT < HV_DATA_PAGE_SHIFT
/** Shift for page size used by the hypervisor to map our
 * code into data space; used for debugging, etc. */
#define HV_CODE_AS_DATA_PAGE_SHIFT HV_CODE_PAGE_SHIFT
#else
/** Shift for page size used by the hypervisor to map our
 * code into data space; used for debugging, etc. */
#define HV_CODE_AS_DATA_PAGE_SHIFT HV_DATA_PAGE_SHIFT
#endif

/** Page size which by the hypervisor to map our
 * code into data space; used for debugging, etc. */
#define HV_CODE_AS_DATA_PAGE_SIZE (1 << HV_CODE_AS_DATA_PAGE_SHIFT)

/** Size of memory that is occupied by HV code and data pages. */ 
#define HV_CODE_DATA_MEM_SIZE(UL, LR) \
                              (HV_CODE_PAGE_SIZE + (HV_DATA_PAGE_SIZE * \
                              ((LR).bits.x - (UL).bits.x + 1) * \
                              ((LR).bits.y - (UL).bits.y + 1)))

#endif /* _SYS_HV_PAGE_H */
