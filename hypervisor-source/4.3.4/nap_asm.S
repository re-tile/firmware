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

//
// Napping routines.
//

#include <hv/hypervisor.h>

#include "asm_inc.h"

	.global nap_idn_ca_msg
	.section .text.nap_idn_ca_msg, "ax"
	.type nap_idn_ca_msg,@function
nap_idn_ca_msg:			// int nap_idn_ca_msg(timeout, ...)
	{
	 mfspr	r10, INTERRUPT_CRITICAL_SECTION
	 move	r11, r0
	}
	{
	 movei	r0, 0
	 mtspr	INTERRUPT_CRITICAL_SECTION, zero
	}
	{
	 cmpeqi	r12, r11, -1   // ULONG_MAX
	 mfspr	r13, CYCLE
	}
	{
	 blbc	r12, 1f
	 add	r11, r13, r11
	}
0:
	nap
	j	0b
1:
	mfspr	r12, CYCLE
	cmpltu	r12, r12, r11
	blbs	r12, 1b
	moveli	r0, HV_EAGAIN

	.global nap_idn_ca_msg_complete
nap_idn_ca_msg_complete:
	mtspr	INTERRUPT_CRITICAL_SECTION, r10
	jrp	lr
	.size nap_idn_ca_msg,.-nap_idn_ca_msg













