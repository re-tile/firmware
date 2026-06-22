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
 * Definitions for the I2C master driver.
 */

#ifndef _SYS_HV_DRV_I2CM_H
#define _SYS_HV_DRV_I2CM_H

#include "drvintf.h"
#include <hv/drv_i2cm_intf.h>

/** Maximum number of I2C devices that are visible to the client OS, per I2C
 *  bus. */
#define MAX_NUM_I2C_DEVS 16

/** Structure that describes the I2C device attributes. */
typedef struct i2c_dev_attr
{
  /** I2C device descriptor. */
  tile_i2c_desc_t dev_desc;

  /** Size, in bytes, of the device's memory address. */
  uint16_t addr_size;

  /** Address/offset within the I2C device. */
  uint16_t dev_addr;

  /** Switch instance number. */
  uint8_t switch_inst;

  /** Switch instance channel. */
  uint8_t switch_chan;

  /** Page size. */
  uint8_t page_size;

  /** Write cycle. */
  uint8_t write_cycle;
}
i2c_dev_attr_t;

/** Device state structure. */
typedef struct i2cm_state
{
  /** Device instance number. */
  int instance;

  /** Shim location. */
  pos_t shim_pos;
     
  /** Shim channel. */
  unsigned long shim_chan;

  /** Number of valid I2C devices in i2c_dev_table. */
  uint32_t num_devs;

  /** I2C device table. */
  struct i2c_dev_attr i2c_dev_table[MAX_NUM_I2C_DEVS];
}
i2cm_state_t;

#endif /* _SYS_HV_DRV_I2CM_H */
