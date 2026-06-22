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
 * Definitions for the I2C Slave driver.
 */

#ifndef _SYS_HV_DRIVERS_I2CS_I2CS_H
#define _SYS_HV_DRIVERS_I2CS_I2CS_H

#include <hv/drv_i2cs_intf.h>

#include "drvintf.h"

/** HV device handle. */
#define I2CS_DEV_HANDLE 0x0

/** Device state structure. */
typedef struct i2cs_state
{
  /** Must hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;

  /** Device channel. */
  unsigned long chan;

  /** Flag used to ensure only kernel gets mapping to i2CS register space. */
  unsigned int is_mmio_mapped;
}
i2cs_state_t;

#endif /* _SYS_HV_DRIVERS_I2CS_I2CS_H */

