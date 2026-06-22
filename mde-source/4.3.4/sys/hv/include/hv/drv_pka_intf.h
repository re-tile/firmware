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
 * Interface definitions for the general pka driver.
 */

#ifndef _SYS_HV_DRV_PKA_INTF_H
#define _SYS_HV_DRV_PKA_INTF_H

#ifndef __DOXYGEN_API_REF__

/** The maximum number of PKA shims. */
#define HV_PKA_NUM_SHIMS 2

/** Offset for the PKA register MMIO region. */
#define HV_PKA_MMIO_OFFSET 0

/** Size of the PKA register MMIO region. */
#define HV_PKA_REGS_MMIO_SIZE  0x100000

/** Size of the PKA window RAM MMIO region. */
#define HV_PKA_DATA_MMIO_SIZE  0x10000

#endif /* !__DOXYGEN_API_REF__ */

#endif /* _SYS_HV_DRV_PKA_INTF_H */
