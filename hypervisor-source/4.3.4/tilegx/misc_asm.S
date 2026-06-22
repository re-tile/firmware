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
#include <arch/interrupts.h>
#include <arch/spr.h>

#include "asm_inc.h"
#include "hw_config.h"
#include "hv.h"
#include "tte.h"

	//
	// Jump to a virtual address at a specified PL; used to jump to
	// client code.  Note that the client is responsible for setting up
	// its own stack.
        //
        // Note: the caller state save area (of size C_ABI_SAVE_AREA_SIZE)
        // is assumed to be already allocated on the stack frame upon entry
        // to this entry point.
	//
	.section .text.jump2_vapl, "ax"
	.global jump2_vapl
	.type jump2_vapl,@function
jump2_vapl:	// void jump2_vapl(VA pc, uint32_t pl)

	//
	// Enable interrupt critical section so we can mess with the
	// interrupt state.
	//
	movei	r30, 1
	mtspr	INTERRUPT_CRITICAL_SECTION, r30

	//
	// Enable the only-handled-in-client interrupts.
	//
	moveli	r31, hw3(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw2(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw1(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw0(HV_INTS_HANDLED_SV)
	mtspr	INTERRUPT_MASK_RESET_HV, r31

	//
	// Set up context to return to the client.
	//
	mtspr	EX_CONTEXT_HV_0, r0
	mtspr	EX_CONTEXT_HV_1, r1

	//
	// Clear all of the registers, to avoid leaking any data to the
	// client, and to provide a reproducible initial state.
	//
	clr2	r0,  r28
	clr2	r1,  r29
	clr2	r2,  r30
	clr2	r3,  r31
	clr2	r4,  r32
	clr2	r5,  r33
	clr2	r6,  r34
	clr2	r7,  r35
	clr2	r8,  r36
	clr2	r9,  r37
	clr2	r10, r38
	clr2	r11, r39
	clr2	r12, r40
	clr2	r13, r41
	clr2	r14, r42
	clr2	r15, r43
	clr2	r16, r44
	clr2	r17, r45
	clr2	r18, r46
	clr2	r19, r47
	clr2	r20, r48
	clr2	r21, r49
	clr2	r22, r50
	clr2	r23, r51
	clr2	r24, r52
	clr2	r25, tp
	clr2	r26, sp
	clr2	r27, lr

	//
	// Jump to the client.
	//
	iret
	.size jump2_vapl,.-jump2_vapl


	//
	// Jump to a virtual address at a specified PL; used to jump to
	// client code.  Note that the client is responsible for setting up
	// its own stack.
        //
        // Note: the caller state save area (of size C_ABI_SAVE_AREA_SIZE)
        // is assumed to be already allocated on the stack frame upon entry
        // to this entry point.
	//
	.section .text.jump2_vaplsp, "ax"
	.global jump2_vaplsp
	.type jump2_vaplsp,@function
jump2_vaplsp:	// void jump2_vaplsp(VA pc, uint32_t pl)

	//
	// Enable interrupt critical section so we can mess with the
	// interrupt state.
	//
	movei	r30, 1
	mtspr	INTERRUPT_CRITICAL_SECTION, r30

	//
	// Enable the only-handled-in-client interrupts.
	//
	moveli	r31, hw3(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw2(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw1(HV_INTS_HANDLED_SV)
	shl16insli r31, r31, hw0(HV_INTS_HANDLED_SV)
	mtspr	INTERRUPT_MASK_RESET_HV, r31

	//
	// Set up context to return to the client.
	//
	mtspr	EX_CONTEXT_HV_0, r0
	mtspr	EX_CONTEXT_HV_1, r1
        move    sp, r2

	//
	// Clear all of the registers, to avoid leaking any data to the
	// client, and to provide a reproducible initial state.
	//
	clr2	r0,  r28
	clr2	r1,  r29
	clr2	r2,  r30
	clr2	r3,  r31
	clr2	r4,  r32
	clr2	r5,  r33
	clr2	r6,  r34
	clr2	r7,  r35
	clr2	r8,  r36
	clr2	r9,  r37
	clr2	r10, r38
	clr2	r11, r39
	clr2	r12, r40
	clr2	r13, r41
	clr2	r14, r42
	clr2	r15, r43
	clr2	r16, r44
	clr2	r17, r45
	clr2	r18, r46
	clr2	r19, r47
	clr2	r20, r48
	clr2	r21, r49
	clr2	r22, r50
	clr2	r23, r51
	clr2	r24, r52
	clr2	r25, tp
	clr2	r26, lr
	move	r27, zero

	//
	// Jump to the client.
	//
	iret
	.size jump2_vaplsp,.-jump2_vaplsp


	//
	// Jump to a virtual address at a specified PL, after installing a
	// stack pointer and loading sets of mappings into the ITLB and DTLB,
	// replacing the current mappings.  This is used to jump to BME client
	// code.  This is a helper routine which should be called only from
	// jmp2_vaplsp_mappings().  It assumes that its code is mapped via the
	// last ITLB entry, and that the input arrays of TTEs are mapped via
	// the last DTLB entry.  It also assumes that interrupts are disabled
	// upon entry, since our caller has added temporary entries to the
	// TLBs.
        //
	.section .text._jump2_vaplsp_mappings, "ax"
	.global _jump2_vaplsp_mappings
	.type _jump2_vaplsp_mappings,@function
_jump2_vaplsp_mappings: // void _jump2_vaplsp_mappings(VA pc, uint32_t pl,
                        // VA sp, VA arg0, const tte_t itte[], int n_itte,
			// const tte_t dtte[], int n_dtte);


	//
	// Invalidate all of the wired entries from both TLBs.  (Our caller
	// eliminated all of the non-wired entries, then added temporary
	// non-wired entries which map our code and data.)  Note that we know
	// there must be at least one wired entry in each TLB, so the loops
	// assume this.
	//
	mfspr	r30, WIRED_ITLB
	addi	r30, r30, -1
0:
	mtspr	ITLB_INDEX, r30
	mtspr	ITLB_CURRENT_VA, zero
	mtspr	ITLB_CURRENT_PA, zero
	mtspr	ITLB_CURRENT_ATTR, zero
	{
	 bnezt	r30, 0b
	 addi	r30, r30, -1
	}

	mfspr	r30, WIRED_DTLB
	addi	r30, r30, -1
0:
	mtspr	DTLB_INDEX, r30
	mtspr	DTLB_CURRENT_VA, zero
	mtspr	DTLB_CURRENT_PA, zero
	mtspr	DTLB_CURRENT_ATTR, zero
	{
	 bnezt	r30, 0b
	 addi	r30, r30, -1
	}

	//
	// Install the lists of TTEs we were given into both TLBs, and mark
	// them as wired.
	//
	mtspr	WIRED_ITLB, r5
	move	r30, zero
	move	r31, r4
0:
	{
	 mtspr	ITLB_INDEX, r30
	 addi	r30, r30, 1
	}
	{
	 ld	r40, r31
	 addi	r31, r31, 8
	}
	{
	 ld	r41, r31
	 addi	r31, r31, 8
	}
	{
	 ld	r42, r31
	 addi	r31, r31, 8
	}
	mtspr	ITLB_CURRENT_VA, r41
	mtspr	ITLB_CURRENT_PA, r42
	{
	 mtspr	ITLB_CURRENT_ATTR, r40
	 addi	r5, r5, -1
	}
	bnezt	r5, 0b


	mtspr	WIRED_DTLB, r7
	move	r30, zero
	move	r31, r6
0:
	{
	 mtspr	DTLB_INDEX, r30
	 addi	r30, r30, 1
	}
	{
	 ld	r40, r31
	 addi	r31, r31, 8
	}
	{
	 ld	r41, r31
	 addi	r31, r31, 8
	}
	{
	 ld	r42, r31
	 addi	r31, r31, 8
	}
	mtspr	DTLB_CURRENT_VA, r41
	mtspr	DTLB_CURRENT_PA, r42
	{
	 mtspr	DTLB_CURRENT_ATTR, r40
	 addi	r7, r7, -1
	}
	bnezt	r7, 0b

	//
	// Mask all interrupts.
	//
	movei	r30, -1
	mtspr	INTERRUPT_MASK_SET_HV, r30

        // Put the default interrupt vector address in the
        // interrupt base address register.
	// FIXME: why can't I movei 0xfffffffffe000000?
	//
        movei   r30, 0xfffffffffe
	shli    r30, r30, 24
        mtspr   INTERRUPT_VECTOR_BASE_2, r30

	//
	//
	// Set up context to return to the client.
	//
	mtspr	EX_CONTEXT_HV_0, r0
	mtspr	EX_CONTEXT_HV_1, r1
	move	r0, r3
        move    sp, r2

	//
	// Clear all of the registers (other than r0 and sp), to avoid leaking
	// any data to the client, and to provide a reproducible initial state.
	//
	clr2	r1,  r28
	clr2	r2,  r29
	clr2	r3,  r30
	clr2	r4,  r31
	clr2	r5,  r32
	clr2	r6,  r33
	clr2	r7,  r34
	clr2	r8,  r35
	clr2	r9,  r36
	clr2	r10, r37
	clr2	r11, r38
	clr2	r12, r39
	clr2	r13, r40
	clr2	r14, r41
	clr2	r15, r42
	clr2	r16, r43
	clr2	r17, r44
	clr2	r18, r45
	clr2	r19, r46
	clr2	r20, r47
	clr2	r21, r48
	clr2	r22, r49
	clr2	r23, r50
	clr2	r24, r51
	clr2	r25, r52
	clr2	r26, tp
	clr2	r27, lr

	//
	// Jump to the client.
	//
	iret
	.size _jump2_vaplsp_mappings,.-_jump2_vaplsp_mappings




	//
	// Flush the whole L1I.
	//
	.section .text.flush_icache, "ax"
	.global flush_icache
	.type flush_icache,@function
flush_icache:	// void flush_icache(void)
#define LAST_LINE_OF_ICACHE (CHIP_L1I_CACHE_SIZE() - \
                             (1 << CHIP_L1I_LOG_LINE_SIZE()))
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

	.size flush_icache,.-flush_icache


	//
	// Probe the DTLB.
        //
	.section .text.dtlb_probe, "ax"
	.global dtlb_probe
	.type dtlb_probe,@function
dtlb_probe:	// uint32_t dtlb_probe(VA va)
	dtlbpr	r0
	mfspr	r0, DTLB_MATCH_0
	jrp	lr

	.size dtlb_probe,.-dtlb_probe


	//
	// Null client program, used by load_null_client().
        //
	.section .text.null_client, "ax"
	.global null_client
	.global null_client_end
	.type null_client,@function
null_client:	// void null_client(void)
	nap
	j	null_client
null_client_end:

	.size null_client,.-null_client


        //
        // Implement the last part of the mf_incoherent flow, which reads
	// something from every memory controller and waits for the reads
	// to complete.
	//
	.section .text.__mf_incoherent, "ax"
	.global __mf_incoherent
	.type __mf_incoherent,@function
__mf_incoherent:
        fence_incoherent hv_fence_incoherent_pas, 1
        jrp	lr

	.size __mf_incoherent,.-__mf_incoherent
