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
 * Routines to read the temperature sensor.
 */

#ifndef TEMP_SENS_H
#define TEMP_SENS_H

/** The maximum number of sensor chips we support. */
#define SENSOR_NUM    10

/** The maximum number of temperature channels each sensor chip supports. */
#define TEMP_CHAN_MAX 4

/** The maximum number of fan controller channels each sensor chip supports. */
#define FAN_CHAN_MAX  4


/** Info about a temperature sensor plugin. */
typedef struct
{
  /** Sensor type, from the BIB. */
  int bib_type;

  /** init function; passed the BIB entry for the device (the descriptor,
   *  and the body of the item).  Run on every tile which will call the
   *  config or read routines, before those routines are called. */
  int (*init)(void** instancepp, uint32_t desc, bi_ptr_t resptr);

  /** config function.  Configures and initializes the device; run exactly
   *  once per chip, before any calls are made to the read functions on any
   *  tile, even if those functions are never called. */
  void (*config)(void* instance);

  /** Read CPU temperature function.  Should use plugin's read, keep this
   * function only for backward-compatibility reasons for customer plugins. */
  long (*read_cpu_temp)(void* instance);

  /** Read board temperature function.  Should use plugin's read, keep this
   * function only for backward-compatibility reasons for customer plugins. */
  long (*read_board_temp)(void* instance);

  /** Read sensor chip register function. */
  int (*read)(void* instance, int chan, int off, void* buf, int count);

  /** Write sensor chip register function. */
  int (*write)(void* instance, int chan, int off, void* buf, int count);

  /** Name of the sensor. */
  const char* name;
} temp_sensor_t;

/** Magic attribute for temp_sensor_t definitions. */
#define __TEMP_SENSOR_ATTR __attribute__((used, section(".temp_sensor_table")))

extern void init_temp_sens(void);

#endif
