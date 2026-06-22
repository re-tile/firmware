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
 * Definitions for the temperature sensor and fan controller pseudo driver.
 */

#ifndef _SYS_HV_DRV_SENSOR_H
#define _SYS_HV_DRV_SENSOR_H

#include <hv/drv_sensor_intf.h>

#include "drvintf.h"
#include "lock.h"
#include "temp_sens.h"


/** The max temperature sensor channel. */
#define TEMP_CHAN_NUM   (SENSOR_NUM * TEMP_CHAN_MAX)

/** The max fan controller channel. */
#define FAN_CHAN_NUM    (SENSOR_NUM * FAN_CHAN_MAX)


/** Temperature sensor channel setting. */
struct temp_struct
{
  /** Sensor chip index of the temperature diode belongs to. */
  int chip;

  /** The physical channel index of the temperature diode on the sensor chip. */
  int chan;
};

/** Fan regulator channel setting. */
struct fan_struct
{
  /** Sensor chip index of the fan regulator belongs to. */
  int chip;

  /** The physical channel index of the fan regulator on the sensor chip. */
  int chan;

  /** The virtual channel index of the first temperature diode, which belongs
   * to the same physical sensor chip of this fan regulator.
   */
  int off;
};

/** Sensor chip configuration. */
struct sensor_conf {
   /** Enabled number of the temperature sensor channels. */
  int temp_chan : 6;

  /** Enabled number of the fan regulator channels. */
  int fan_chan : 6;
};

/** The device state. */
typedef struct
{
  /** Hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** Detected temperature sensor channel. */
  int temp_num;

  /** Detected fan channel. */
  int fan_num;

  /** Setting of the temperature diodes. */
  struct temp_struct temp[TEMP_CHAN_NUM];

  /** Setting of the fan regulators. */
  struct fan_struct fan[FAN_CHAN_NUM];

  /** Chip configuration of the temperature sensors or fan regulators. */
  struct sensor_conf conf[SENSOR_NUM];
} sensor_state_t;

/** Temperature sensor and fan regulator chip instance. */
typedef struct sens_inst
{
  /** Pointer to the sensor plugin. */
  const temp_sensor_t* sensor;

  /** Sensor instance number from the BIB. */
  int bib_instance;

  /** I2C address of the sensor chip. */
  uint32_t i2c_addr;

  /** I2C bus number of the sensor chip. */
  int bus;

  /** I2C switch instance number for the sensor chip. */
  int switch_inst;

  /** I2C switch channel for the sensor chip. */
  int switch_chan;

  /** Fan descriptors from the BIB. */
  struct bi_fan_info fan_descs[FAN_CHAN_MAX];

  /** Extra signals need to assert when initialize the sensor chip. */
  struct bi_signal extra_sigs[FAN_CHAN_MAX];
} sens_inst_t;

extern sens_inst_t* temp_sensor[SENSOR_NUM];

#endif /* _SYS_HV_DRV_SENSOR_H */
