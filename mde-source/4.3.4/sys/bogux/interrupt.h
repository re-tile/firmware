/**
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
 *
 * Hypervisor interrupt support (currently used only by the test for the
 * hypervisor driver framework).
 * @file
 */

#ifndef _SYS_BOGUX_INTERRUPT_H
#define _SYS_BOGUX_INTERRUPT_H

#include <hv/hypervisor.h>

void init_interrupts(void);

void put_msg_int(HV_IntrMsg* him);
int get_msg_int(HV_IntrMsg* him);

#endif /* !_SYS_BOGUX_INTERRUPT_H */
