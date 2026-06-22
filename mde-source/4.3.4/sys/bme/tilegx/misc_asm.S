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
// Miscellaneous assembly routines.
//

#include <arch/chip.h>
#include <arch/rsh.h>
#include <arch/spr.h>

#include "asm_inc.h"

	//
	// Invalidate part of the L2 cache via displacement flush.  We take
	// two parameters: the virtual address of the block we'll read
	// to do the flush, and the length.  The block address must be
	// word-aligned, and the length is assumed to be a multiple of 1k.
	// This routine is not suitable for use on its own; it's really a
	// helper for the BME version of tmc_mem_flush_l2() which does
	// actually flush the entire L1 and L2 properly.
        //
	.section .text._bme_disp_flush, "ax"
	.global _bme_disp_flush
	.type _bme_disp_flush,@function
_bme_disp_flush:	// void _bme_disp_flush(VA addr, int len)

	//
	// Save the start address, and calculate the number of times through
	// the loop.
	//
	{
	 move	r16, r0
	 shrui	r17, r1, 4 + CHIP_L2_LOG_LINE_SIZE()  // Loop unrolled 16 times
	}
	
	//
	// Now do the flush.
	//
0:
	{
	 ld	r0, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r1, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r2, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r3, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r4, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r5, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r6, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r7, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r8, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r9, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r10, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r11, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r12, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r13, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r14, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	}
	{
	 ld	r15, r16
	 addi	r16, r16, CHIP_L2_LOG_LINE_SIZE()
	 addi	r17, r17, -1
	}
	bgtz	r17, 0b

	jrp	lr
	.size _bme_disp_flush,.-_bme_disp_flush


	//
	// Flush the whole L1I.
	//
	.section .text.bme_flush_l1i, "ax"
	.global bme_flush_l1i
	.type bme_flush_l1i,@function
bme_flush_l1i:	// void bme_flush_l1i(void)
#define LAST_LINE_OF_ICACHE (CHIP_L1I_CACHE_SIZE() - CHIP_L1I_LINE_SIZE())
	{
	 move	r0, zero
	 moveli	r1, hw2_last(LAST_LINE_OF_ICACHE)
	}
	{
	 shl16insli r1, r1, hw1(LAST_LINE_OF_ICACHE)
	 mf
	}
	shl16insli r1, r1, hw0(LAST_LINE_OF_ICACHE)
	drain
	cmpltu	r3, r0, r1	// Set up loop invariant
1:	{
	 addli	r0, r0, (1 << CHIP_L1I_LOG_LINE_SIZE())	// Advance buffer
	 icoh	r0
	}
	{
	 cmpltu	r3, r0, r1	// Check if next position < end
	 bnezt	r3, 1b		// Loop if current position < end
	}
	drain
	jrp lr

	.size bme_flush_l1i,.-bme_flush_l1i


	// FIXME: decide if we need this
	//
	// Probe the DTLB.
        //
	.section .text.bme_dtlb_probe, "ax"
	.global bme_dtlb_probe
	.type bme_dtlb_probe,@function
bme_dtlb_probe:	// uint32_t bme_dtlb_probe(VA va)
	dtlbpr	r0
	mfspr	r0, DTLB_MATCH_0
	jrp	lr

	.size bme_dtlb_probe,.-bme_dtlb_probe


#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
        //
        // Implement the last part of the mf_incoherent flow, which reads
	// something from every memory controller and waits for the reads
	// to complete.
	//
	.section .text.__tmc_mem_sys_fence_incoherent, "ax"
	.global __tmc_mem_sys_fence_incoherent
	.type __tmc_mem_sys_fence_incoherent,@function
__tmc_mem_sys_fence_incoherent:
        fence_incoherent _bme_fence_incoherent_pas, 1
         jrp	lr

	.size __tmc_mem_sys_fence_incoherent,.-__tmc_mem_sys_fence_incoherent
#endif


	//
	// Get the BME state pointer (kept in the tp register).
	//
	.section .text._bme_get_state, "ax"
	.global _bme_get_state
	.type _bme_get_state,@function
_bme_get_state:	// _bme_state_t* _bme_get_state(void)
	{
	 move	r0, tp
	 jrp	lr
	}
	.size _bme_get_state,.-_bme_get_state


	//
	// Set the BME state pointer (kept in the tp register).
	//
	.section .text._bme_set_state, "ax"
	.global _bme_set_state
	.type _bme_set_state,@function
_bme_set_state:	// void _bme_set_state(_bme_state_t* state)
	{
	 move	tp, r0
	 jrp	lr
	}
	.size _bme_set_state,.-_bme_set_state

	//
	// Get the user state pointer (pointed to by the tp register).
	//
	.section .text.bme_get_pertile_state, "ax"
	.global bme_get_pertile_state
	.type bme_get_pertile_state,@function
bme_get_pertile_state:	// void* bme_get_pertile_state(void)
	ld	r0, tp
	jrp	lr
	.size bme_get_pertile_state,.-bme_get_pertile_state


	//
	// Set the user pertile_state pointer (pointed to by the tp register).
	//
	.section .text.bme_set_pertile_state, "ax"
	.global bme_set_pertile_state
	.type bme_set_pertile_state,@function
bme_set_pertile_state:	// void bme_set_pertile_state(void* state)
	st	tp, r0
	jrp	lr
	.size bme_set_pertile_state,.-bme_set_pertile_state
