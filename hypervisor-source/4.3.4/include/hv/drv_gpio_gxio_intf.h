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
 * Interface definitions for the GPIO driver.
 */

#ifndef _SYS_HV_DRV_GPIO_GXIO_INTF_H
#define _SYS_HV_DRV_GPIO_GXIO_INTF_H

#include <arch/gpio.h>

#ifndef __DOXYGEN_API_REF__

/** Offset for the register MMIO region. */
#define HV_GPIO_MMIO_OFFSET(svc_dom)     \
   ((uint64_t) (svc_dom) << GPIO_MMIO_ADDRESS_SPACE__SVC_DOM_SHIFT)

/** Size of the register MMIO region. */
#define HV_GPIO_MMIO_SIZE ((uint64_t) 1 << \
                           GPIO_MMIO_ADDRESS_SPACE__OFFSET_WIDTH)

/** The number of service domains supported by the GPIO shim. */
#define HV_GPIO_NUM_SVC_DOM 8

#endif /* !__DOXYGEN_API_REF__ */

/** Maximum number of characters in a pinset name. */
#define GXIO_GPIO_PINSET_NAME_LEN  64

#endif /* _SYS_HV_DRV_GPIO_GXIO_INTF_H */
