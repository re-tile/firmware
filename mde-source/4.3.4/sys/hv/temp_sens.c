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
 * Routines to read temperature sensors.
 */
#include <stdio.h>
#include <string.h>
#include <util.h>

#include <hv/hypervisor.h>

#include "board_info.h"
#include "debug.h"
#include "devices.h"
#include "drivers/sensor/sensor.h"
#include "hw_config.h"
#include "i2c_acc.h"

#include "temp_sens.h"


/** The temperature sensors that we've configured. */
sens_inst_t* temp_sensor[SENSOR_NUM] _SHARED = { 0 };

/** Stub function for reading a temperature from temp sensors that can't
 *  be identified.
 * @param sensor The sensor chip instance.
 * @return -1, to indicate error.
 */
static long
unknown_read_temp(void* sensor)
{
  return -1;
}

/** Stub function for reading the unknown temperature sensor.
 * @param sensor The sensor chip instance.
 */
static int
unknown_read(void* sensor, int chan, int off, void* buf, int count)
{
  return -1;
}

/** Stub function for writing the unknown temperature sensor.
 * @param sensor The sensor chip instance.
 */
static int
unknown_write(void* sensor, int chan, int off, void* buf, int count)
{
  return -1;
}

/** Stub function for configuring the unknown temperature sensor.
 * @param sensor The sensor chip instance.
 */
static void
unknown_config(void* sensor)
{
}

/** temp_sensor struct for the unknown temperature sensor.  Explicitly used
 *  by setup_temp_sens, so not in the temp_sensor_table. */

/** Add a new temp_sensor_table entry. */
static const temp_sensor_t unknown_temp_sensor =
{
  .bib_type = BI_TEMP_CFG_TYPE__VAL_UNKNOWN,
  .config = unknown_config,
  .read_cpu_temp = unknown_read_temp,
  .read_board_temp = unknown_read_temp,
  .read = unknown_read,
  .write = unknown_write,
  .name = "unknown",
};

/** The unknown temperature sensor instance. */
static sens_inst_t unknown_sensor_inst =
{
  .sensor = &unknown_temp_sensor,
};

/** Add one temperature sensor to the table.
 * @param resptr Pointer to the sensor's BIB entry.
 * @param desc Descriptor for the sensor's BIB entry.
 * @param idx Pointer to index at which to add the new sensor.  If the
 *   index is out of range, no sensor is added.  When a sensor is added,
 *   the index is incremented.
 */
static void
add_one_temp_sens(bi_ptr_t resptr, uint32_t desc, int* idx)
{
  struct bi_temp_cfg* bi = resptr;
  uint32_t type = bi->type;
  extern temp_sensor_t temp_sensor_table_start[], temp_sensor_table_end[];

  if (*idx >= SENSOR_NUM)
  {
    static int warned = 0;

    if (!warned)
      tprintf("hv_warning: BIB contains more than %d TMP_CFG items, "
              "excess items ignored\n", SENSOR_NUM);
    warned = 1;
    return;
  }

  for (const temp_sensor_t* s = temp_sensor_table_start;
       s < temp_sensor_table_end; s++)
  {
    // TODO: Check whether the sensor chip is owned by the BMC or GX,
    // it requires the BIB support.

    if (type == s->bib_type)
    {
      if (i2cm_info[bi->addr.bus] != NULL)
      {
        int err = s->init((void**)&temp_sensor[*idx], desc, resptr);
        if (err)
          tprintf("hv_warning: %s temp sensor at bus %d addr 0x%x couldn't be "
                  "configured (err %d), item ignored\n", s->name,
                  bi->addr.bus, bi->addr.dev_addr << 1, err);
        else
          temp_sensor[(*idx)++]->sensor = s;
      }
      else
        tprintf("hv_warning: TEMP_CFG BIB item references unconfigured "
                "I2C bus %d, item ignored\n", bi->addr.bus);
      return;
    }
  }

  tprintf("hv_warning: unknown type 0x%x in TEMP_CFG BIB item, "
          "item ignored\n", type);
}

/** Set up function pointers to temperature read functions based on
 * the type of temperature sensor found on the i2c bus.
 */
static void
setup_temp_sens()
{
  bi_ptr_t resptr;
  uint32_t desc;

  int i = 0;
  int offset = 0;

  //
  // The sensor with instance 0 controls the CPU temperature and fan.
  // Various things expect that one to be first in the sensor list, so
  // we do two passes through the BIB to ensure that's the case.  Note
  // that if there are multiple sensors with instance 0 we add them in
  // the order they appear.
  //
  while ((desc = bi_getparam(BI_TYPE_TEMP_CFG, 0, &resptr, &offset)) != BI_NULL)
    add_one_temp_sens(resptr, desc, &i);

  offset = 0;

  while ((desc = bi_getparam(BI_TYPE_TEMP_CFG, -1, &resptr, &offset)) !=
         BI_NULL)
    if (BI_INST(desc) != 0)
      add_one_temp_sens(resptr, desc, &i);

  for (; i < SENSOR_NUM; i++)
    temp_sensor[i] = &unknown_sensor_inst;
}

/** Initialize the temperature sensor chip with default values.
 */
void
init_temp_sens()
{
  setup_temp_sens();
  for (int i = 0; i < SENSOR_NUM; i++)
  {
    temp_sensor[i]->sensor->config(temp_sensor[i]);
    INIT_TRACE("Initialized temperature sensor: %s\n",
               temp_sensor[i]->sensor->name);
  }
}
