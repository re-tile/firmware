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
// Test driver interrupt routine.
//
	.section .text.testdrv_instant_intr, "ax"
	.global	testdrv_instant_intr
	.type testdrv_instant_intr,@function
	//
	// r0 holds our argument, which is a pointer to where the interrupt
	// word needs to get stored, and r1 holds the first (and only) word of
	// the interrupt.
	//
testdrv_instant_intr:
	st4	r0, r1
	j	drv_intr_exit
	.size testdrv_instant_intr,.-testdrv_instant_intr


	.section .text.testdrv_fastio_a, "ax"
	.global	testdrv_fastio_a
	.type testdrv_fastio_a,@function
	//
	// r0 holds our driver-supplied argument, and r1-r9 hold any parameters
	// passed in by the caller.
	//
testdrv_fastio_a:
	st4	r0, r1
	{
	 move	r0, zero
	 jrp	lr
	}
	.size testdrv_fastio_a,.-testdrv_fastio_a
