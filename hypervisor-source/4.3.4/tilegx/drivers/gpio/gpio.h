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
 * Definitions for the gpio driver.
 */

#ifndef _SYS_HV_DRV_TILEGX_GPIO_H
#define _SYS_HV_DRV_TILEGX_GPIO_H

#include <hv/drv_gpio_gxio_intf.h>
#include <hv/iorpc.h>

#include "drvintf.h"

/** GPIO interrupts for pollable file descriptors.  If both bitmaps are
 *  zero, the interrupt is inactive. */
typedef struct
{
  /** Bitmap of pins whose assertion causes the interrupt. */
  uint64_t on_assert;

  /** Bitmap of pins whose deassertion causes the interrupt. */
  uint64_t on_deassert;
}
gpio_intr_t;

/** Number of GPIO interrupts for pollable file descriptors per service
 *  domain.  This is pretty arbitrary and could be easily changed. */
#define GPIO_POLLFD_INTR_PER_SD 8


/** Device state structure. */
typedef struct
{
  /** Must hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;
 
  /** Shim channel. */
  unsigned long shim_chan;
 
  /** Pins which can be used for input. */
  uint64_t input_pins;

  /** Pins which can be used for output. */
  uint64_t output_pins;

  /** Pins which can be used for output in open-drain mode. */
  uint64_t output_od_pins;

  /** Set of reserved service domains. */
  uint32_t reserved_svc_dom_bitmask;

  /** Pins attached by each service domain. */
  uint64_t attached_pins[HV_GPIO_NUM_SVC_DOM];

  /** Interrupts for pollable file descriptors. */
  gpio_intr_t pollfd_intrs[HV_GPIO_NUM_SVC_DOM][GPIO_POLLFD_INTR_PER_SD];
}
gpio_state_t;

#endif /* _SYS_HV_DRV_TILEGX_GPIO_H */
