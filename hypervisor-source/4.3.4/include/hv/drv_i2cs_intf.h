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
 * Interface definitions for the I2CS driver.
 */

#ifndef _SYS_HV_INCLUDE_DRV_I2CS_INTF_H
#define _SYS_HV_INCLUDE_DRV_I2CS_INTF_H

#include <arch/rsh.h>
#include <arch/i2cs.h>


/** The mmap file offset (PA) of the I2CS MMIO region. */
#define HV_I2CS_MMIO_OFFSET \
  ((unsigned long long)RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_I2CS << \
    RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

/** The maximum size of the I2CS MMIO region. */
#define HV_I2CS_MMIO_SIZE \
  (1ULL << RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

/** The maximum size of the I2CS MMIO region mapped into client. */
#define HV_I2CS_IOREMAP_SIZE \
  (1ULL << I2CS_DEV_INFO__TYPE_WIDTH)

#endif /* _SYS_HV_INCLUDE_DRV_I2CS_INTF_H */
