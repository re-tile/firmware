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
 * The device table.
 */

#ifndef _SYS_HV_DEVICE_TABLE_H
#define _SYS_HV_DEVICE_TABLE_H

#include "config.h"
#include "drvintf.h"
#include "param.h"
#include "tile_mask.h"

/** Generic device descriptor. */
typedef struct device
{
  /** Device shim code.  For a real hardware device, this is the code from
   *  the shim header register (Pro) or the dev_info register (Gx).  For
   *  pseudo-devices, this is zero, or a DEV_PSEUDO_xxx code; the latter
   *  is only required if this is a system device, or has an automatic
   *  driver. */
  const uint32_t shim_type;

  /** Device name with instance number. */
  const char* const name;

  /** Instance number. */
  const int instance;

  /** Device description. */
  const char* const desc;

  /** Device flags (DEV_FLG_xxx). */
  const uint32_t flags;
  
#if MAX_DEVICE_CLOCKS > 0
  /** Number of device clocks. */
  const int n_clocks;

  /** Index into the F2V table in the fuses, pointing to the first
   *  entry for each clock. */
  const int f2v_index[MAX_DEVICE_CLOCKS];
#endif

  //
  // Members after this point will be set by the device probe/config code and
  // must not be initialized in device table entries.
  //

  /** Descriptor for argument string. */
  struct hvfs_str arg;

  /** Nonzero if device found during probe. */
  uint32_t probed:1;

  /** Driver we're using for this device; if NULL, device is not
   *  present/usable. */
  driver_t* drv;

  /** Device information. */
  struct dev_info info;

  /** State pointer for use by the driver. */
  void* drv_state;

  /** Index of client which exclusively owns this device; if the device
   *  is shared between multiple clients, this is -1. */
  int client_owner;

  /** Set of client tiles allowed to use this device. */
  tile_mask tiles;
}
device_t;

/** Define device table entries which are placed at the beginning of the table.
 *  Used for real hardware devices. */
#define __DEVICE_ATTR_BASE __attribute__((used, section(".device_table_base")))

/** Define a device table entry; used for pseudo-devices which are added
 *  to the hypervisor via Makefile variables. */
#define __DEVICE_ATTR __attribute__((used, section(".device_table")))

/** Define device table entries which are placed at the end of the table.
 *  Used for the the terminating (NULL_named) entry. */
#define __DEVICE_ATTR_END __attribute__((used, section(".device_table_end")))

//
// Device flags
//

/** This is a system device; the first available driver will be used for
 *  it, its device init routine will be called at probe time, and it will
 *  be instantiated even if there is no config file line for it. */
#define DEV_FLG_SYSDEV     0x1

/** No real hardware required. */
#define DEV_FLG_PSEUDO     0x2

/** Don't configure this device's MDN ports. */
#define DEV_FLG_NO_MDN_CFG 0x4

/** Device has multi-client support. */
#define DEV_FLG_SHAREABLE  0x8

//
// Pseudo-device types
//
//
/** Tile-monitor FIFO device. */
#define DEV_PSEUDO_TMFIFO   0x1000

/** Core PLL device. */
#define DEV_PSEUDO_COREPLL  0x1001

/** Watchdog device. */
#define DEV_PSEUDO_WATCHDOG 0x1002

/** Sensor device. */
#define DEV_PSEUDO_SENSOR   0x1003

#endif /* _SYS_HV_DEVICE_TABLE_H */
