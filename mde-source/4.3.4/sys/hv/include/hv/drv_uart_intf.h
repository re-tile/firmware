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
 * Interface definitions for the UART driver.
 */

#ifndef _SYS_HV_DRV_UART_INTF_H
#define _SYS_HV_DRV_UART_INTF_H

#include <arch/uart.h>

/** Number of UART ports supported. */
#define TILEGX_UART_NR        2

/** The mmap file offset (PA) of the UART MMIO region. */
#define HV_UART_MMIO_OFFSET   0

/** The maximum size of the UARTs MMIO region (64K Bytes). */
#define HV_UART_MMIO_SIZE     (1UL << 16)

#endif /* _SYS_HV_DRV_UART_INTF_H */
