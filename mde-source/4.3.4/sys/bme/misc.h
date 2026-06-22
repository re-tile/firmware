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
 * Routines to do miscellaneous operations.
 */

#ifndef _SYS_BME_MISC_H
#define _SYS_BME_MISC_H

#include <bme/types.h>

/** Default VA to be used for shared spin lock page. */
#define BME_HVBME_SPINLOCK_PAGE_VA 0xab000000

/** Flush the whole I-cache. */
void bme_flush_l1i(void);

/** Probe the DTLB.
 * @param va Address to probe.
 * @return Bitmask of DTLB entries which match the given address.
 */
uint32_t dtlb_probe(VA va);

/** Fence to guarantee visibility of stores to incoherent memory. */
void mf_incoherent(void);

#endif /* _SYS_BME_MISC_H */
