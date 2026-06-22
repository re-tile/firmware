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
 * Definitions for the RSHIM WATCHDOG driver.
 */

#ifndef _SYS_HV_DRIVERS_WATCHDOG_WATCHDOG_H
#define _SYS_HV_DRIVERS_WATCHDOG_WATCHDOG_H

#include <hv/drv_watchdog_intf.h>
#include "drvintf.h"

// Forward reference, defined below
struct watchdog_state;

/** Max number of watchdog timers. */
#define MAX_NUM_WATCHDOGS 3

/** Structure that describes the watchdog device attributes. */
typedef struct watchdog_attr
{
  /** Watchdog state pointer. */
  struct watchdog_state *ws;
  /** RSHIM timer instance number. */
  uint16_t instance;
  /** Timer countdown value, to be reloaded to timer. */
  uint32_t countdown;
  /** Pointer to watchdog access methods. */
  struct wdt_ops* ops;
}
watchdog_attr_t;

/** Typedef for wdt configuration function. */
typedef int wdt_config_func(int instance, struct watchdog_attr* wdt,
                            struct watchdog_state* ws);

/** Typedef for wdt countdown setting function. */
typedef int wdt_set_countdown_func(int instance, int seconds,
                                   struct watchdog_attr* wdt);

/** Typedef for wdt patting function. */
typedef int wdt_keep_alive_func(int instance, struct watchdog_attr* wdt);

/** Watchdog operations structure. */
typedef struct wdt_ops
{
  /** Routine to config the wd, e.g. intr, src freq, etc. */
  wdt_config_func* config;

  /** Routine to set the timer countdown value. */
  wdt_set_countdown_func* set_countdown;

  /** Routine to reset the timer. */
  wdt_keep_alive_func* keep_alive;
} wdt_ops_t;

/** Device state structure. */
typedef struct watchdog_state
{
  /** Must hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** rshim address. */
  uint32_t rshim;

  /** WATCHDOG device table. */
  struct watchdog_attr watchdog_attr_table[MAX_NUM_WATCHDOGS];
}
watchdog_state_t;

#endif /* _SYS_HV_DRIVERS_WATCHDOG_WATCHDOG_H */
