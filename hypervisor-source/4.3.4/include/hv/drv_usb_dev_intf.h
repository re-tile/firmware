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
 * Interface definitions for the USB device driver.
 */

#ifndef _SYS_HV_DRV_USB_DEV_INTF_H
#define _SYS_HV_DRV_USB_DEV_INTF_H

#include <arch/usb_device.h>

#ifndef __DOXYGEN_API_REF__

/** Offset for the MMIO region; we export the MAC registers only. */
#define HV_USB_DEV_MMIO_OFFSET  0x10000

/** Size of the register MMIO region. */
#define HV_USB_DEV_MMIO_SIZE    ((uint64_t) 0x10000)

/** The number of service domains supported by the USB device shim. */
#define HV_USB_DEV_NUM_SVC_DOM 1

/** The number of logical endpoints supported. */
#define HV_USB_DEV_NUM_EP      6

#endif /* !__DOXYGEN_API_REF__ */

#endif /* _SYS_HV_DRV_USB_DEV_INTF_H */
