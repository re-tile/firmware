/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
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
 * Interface definitions for the USB host driver.
 */

#ifndef _SYS_HV_DRV_USB_HOST_INTF_H
#define _SYS_HV_DRV_USB_HOST_INTF_H

#include <arch/usb_host.h>

#ifndef __DOXYGEN_API_REF__

/** Offset for the EHCI register MMIO region. */
#define HV_USB_HOST_MMIO_OFFSET_EHCI ((uint64_t) USB_HOST_HCCAPBASE_REG)

/** Offset for the OHCI register MMIO region. */
#define HV_USB_HOST_MMIO_OFFSET_OHCI ((uint64_t) USB_HOST_OHCD_HC_REVISION_REG)

/** Size of the register MMIO region.  This turns out to be the same for
 *  both EHCI and OHCI. */
#define HV_USB_HOST_MMIO_SIZE ((uint64_t) 0x1000)

/** The number of service domains supported by the USB host shim. */
#define HV_USB_HOST_NUM_SVC_DOM 1

#endif /* !__DOXYGEN_API_REF__ */

#endif /* _SYS_HV_DRV_USB_HOST_INTF_H */
