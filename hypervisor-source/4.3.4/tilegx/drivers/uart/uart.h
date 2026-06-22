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
 * Definitions for the uart driver.
 */

#ifndef _SYS_HV_DRV_UART_H
#define _SYS_HV_DRV_UART_H

#include <hv/drv_uart_intf.h>

#include "drvintf.h"
#include "lock.h"


/** Number of instances supported. */
#define TILEGX_UART_NR          2

/** Device handle. */
#define TILEGX_UART_DEV_HANDLE  0

/** The mmap file base (PA) of the UART0 MMIO region. */
#define UART0_MMIO_BASE \
  ((unsigned long long)RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_UART0 << \
   RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

/** The mmap file base (PA) of the UART1 MMIO region. */
#define UART1_MMIO_BASE \
  ((unsigned long long)RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_UART1 << \
   RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

/** The device state. */
typedef struct
{
  /** Hold this lock to modify shared data. */
  drv_spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;

  /** Device instance. */
  int instance;

  /** UART Device channel. */
  unsigned long chan;

  /** Flag used to ensure only kernel gets mapping to UART register space. */
  unsigned int mmio_mapped;
} uart_state_t;

#endif /* _SYS_HV_DRV_UART_H */
