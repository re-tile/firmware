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
 * Definitions for the USB device-mode driver.
 */

#ifndef _SYS_HV_DRV_TILEGX_USB_DEV_H
#define _SYS_HV_DRV_TILEGX_USB_DEV_H

#include <hv/drv_usb_dev_intf.h>
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

  /** Did we boot in standalone mode? */
  unsigned int standalone:1;

  /** Is this device busy? */
  unsigned int busy:1;
}
usb_dev_state_t;

#endif /* _SYS_HV_DRV_TILEGX_USB_DEV_H */
