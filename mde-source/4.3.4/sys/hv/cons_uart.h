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
 * Routines to manage the UART console.
 */


#ifndef _SYS_HV_CONS_UART_H
#define _SYS_HV_CONS_UART_H

#include "types.h"

void init_uart_console(uint32_t dest, int first_init, int reconfig_uart,
                       int uart_port);
void init_uart_debug_string(char* str);
void enable_uart_intr(void);

#endif /* _SYS_HV_CONS_UART_H */
