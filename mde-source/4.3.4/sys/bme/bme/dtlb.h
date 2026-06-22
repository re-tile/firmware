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
 * Bare Metal Environment DTLB miss handling support.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_DTLB_H
#define _SYS_BME_DTLB_H

#include <bme/types.h>

__BEGIN_DECLS

/** Memory mapping flags */
/** Map memory as writeable */
#define BME_MEMORY_MAP_FLAGS_WRITEABLE 1

/** Local cache mode */
#define BME_CACHE_MODE_LOCAL    0
/** Hash-for-home cache mode */
#define BME_CACHE_MODE_HASH     1
/** Do not cache */
#define BME_CACHE_MODE_NONE     2
/** Location override cache mode */
#define BME_CACHE_MODE_COORDS   3

/** Structure to describe caching attributes of mapped memory.
 */
typedef struct bme_memory_attr {
  int flags;          /**< writeable, etc. */
  int cache_mode;     /**< Cache mode (local, none, hfh, coords) */
  pos_t cache_coords; /**< Cache coordinates, used if mode is coords */ 
} bme_memory_attr_t;

/** Typedef for miss handling function */
typedef int bme_dtlb_miss_handler_t(VA va);

/** Initialize and install the default DTLB miss handling system.
 * @returns 0 if successful, negative number if not.
 */
int bme_default_dtlb_handler_init(void);

/** Install a DTLB miss handler.
 * @param va Virtual address of 16MB range for which to use this handler.
 * @param miss_handler Function for servicing DTLB misses in this 16MB range.
 * @returns 0 if successful, negative number if not.
 */
int bme_install_dtlb_handler(VA va, bme_dtlb_miss_handler_t* miss_handler);

/** Uninstall a DTLB miss handler.
 * @param va Virtual address of 16M range for which to uninstall handler.
 * @returns 0 if successful, negative number if not.
 */
int bme_uninstall_dtlb_handler(VA va);

/** Create a memory mapping for the given VA, PA, number of pages,
 * and page size with the given caching attributes.
 * The first call to this function sets the page size to be used for 
 * the 16MB range of VA space corresponding to this VA, beginning at a
 * 16MB-aligned virtual address.  Subsequent calls to this function must
 * use the same page size if any of the memory overlaps an initialized
 * range.
 * @param va Virtual address of range of memory to be mapped.
 * @param pa Physical address of range of memory to be mapped.
 * @param page_size Size of pages to be used for this range.
 * @param num_pages Number of pages to map for this range.
 * @param mem_attr Caching and memory attributes of this memory.
 * @param wired If non-zero, wire the entry in the DTLB.
 * @returns 0 if successful, negative number if not.
 */
int bme_memory_map(VA va, PA pa, int page_size, int num_pages,
                   bme_memory_attr_t* mem_attr, int wired);

/** Unmap the memory starting at the given VA.  If entries are wired
 * in the TLB, remove them.
 * @param va Virtual address of range of memory to be unmapped.
 * @param size Size of memory, in bytes, to unmap.
 * @returns 0 if successful, negative number if not.
 */
int bme_memory_unmap(VA va, int size);

__END_DECLS

#endif /* _SYS_BME_DTLB_H */

/** @} */
