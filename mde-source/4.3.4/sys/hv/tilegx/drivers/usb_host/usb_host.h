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
 * Definitions for the USB host driver.
 */

#ifndef _SYS_HV_DRV_TILEGX_USB_HOST_H
#define _SYS_HV_DRV_TILEGX_USB_HOST_H

#include <hv/drv_usb_host_intf.h>
#include <hv/iorpc.h>

#include "drvintf.h"

/** Device state structure. */
typedef struct
{
  /** Must hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;
 
  /** Shim channel. */
  unsigned long shim_chan;

  /** Device instance. */
  int instance;

  /** Device port. */
  int port;

  /** Nonzero if this port is already open; indexed by is_ehci. */
  int busy[2];
}
usb_host_state_t;

#endif /* _SYS_HV_DRV_TILEGX_USB_HOST_H */
