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
 * Routines for spin locks shared between the HV and the BME.
 */

#ifndef _SYS_COMMON_HVBME_SHARED_LOCK_H
#define _SYS_COMMON_HVBME_SHARED_LOCK_H

#ifndef __ASSEMBLER__

/** Page size of memory for shared locks.  Doesn't need to be really big,
 *  so it's 4K on Pro.  However, on Gx, it's 64K to avoid the need to 
 *  do page coloring on it. */
#define HV_BME_SHARED_PAGE_SIZE (1 << 16)

/** Lock number for the SERDES reset. */
#define HVBME_SPINLOCK_SERDES 0

/** Lock number for shared MDIO operations. We could theoretically get a
 *  tiny bit more parallelism in some configurations by having one of these
 *  for GbE and one for XGbE, but that doesn't seem worth the trouble. */
#define HVBME_SPINLOCK_MDIO 1

/** Grab the numbered spin lock (shared between the HV and the BME).
 * @param lock_number Specifies which numbered lock to use.
 */
void hvbme_spin_lock(int lock_number);

/** Release the spin lock (shared between the HV and the BME).
 * @param lock_number Specifies which numbered lock to use.
 */
void hvbme_spin_unlock(int lock_number);

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_COMMON_HVBME_SHARED_LOCK_H */
