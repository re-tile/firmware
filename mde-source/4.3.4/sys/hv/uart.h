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
 * Definitions for the UART hardware in the rshim.  Note that there are some
 * more parameters in param.h.
 */

#ifndef _SYS_HV_UART_H
#define _SYS_HV_UART_H


#include <arch/rsh.h>

/** RSHIM channel for UART, port 0. */
#define UART_CHANNEL ((uint64_t) RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_UART0 << \
                      RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

/** RSHIM channel for UART, port 1. */
#define UART_CHANNEL_PORT1 \
                     ((uint64_t) RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_UART1 << \
                      RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)


#endif /* _SYS_HV_UART_H */
