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
// Bare metal environment default interrupt vectors.
//

#include <arch/abi.h>
#include <arch/chip.h>
#include <arch/interrupts.h>
#include <arch/spr.h>

#include "asm_inc.h"
#include "bme_pl.h"
#include "interrupts_asm.h"

/** Interrupts the BME handles, and which should be masked when calling
 *  the C routine to service an interrupt */
// FIXME: need better documentation about when/how users should add
// to these tables
#define BME_INTS_MASKED_DURING_INTHANDLER ( \
        0)

// FIXME: the register/save restore stuff needs to be factored out into a
// macro so we can use it in two places.

	.altmacro	// Necessary for \xx& macro substitution feature

	//
	// Put the handle-while-in-supervisor interrupt mask, plus the
	// specified vector, into the two specified registers.  (If the
	// vector is less than zero, ignore it and just store the mask.)
	//
	.macro int_mask vec, reg
	.iflt   \vec
	 moveli \reg, hw2_last(BME_INTS_MASKED_DURING_INTHANDLER)
	 shl16insli \reg, \reg, hw1(BME_INTS_MASKED_DURING_INTHANDLER)
	 shl16insli \reg, \reg, hw0(BME_INTS_MASKED_DURING_INTHANDLER)
	.else
	 moveli \reg, hw3(BME_INTS_MASKED_DURING_INTHANDLER | INT_MASK(\vec))
	 shl16insli \reg, \reg, hw2(BME_INTS_MASKED_DURING_INTHANDLER | \
                                    INT_MASK(\vec))
	 shl16insli \reg, \reg, hw1(BME_INTS_MASKED_DURING_INTHANDLER | \
                                    INT_MASK(\vec))
	 shl16insli \reg, \reg, hw0(BME_INTS_MASKED_DURING_INTHANDLER | \
                                    INT_MASK(\vec))
	.endif
	.endm


	//
	// Define a label for the start of the array of pointers to interrupt
	// names; we'll fill this in via the vec_start macro.
	//
	.pushsection .rodata.int_names, "a"
	.global	int_names
	.align 8
int_names:
	.org	0
	.popsection
	
	//
	// Start an interrupt vector.  
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	//
	.macro vec_start vecnum, vecname

	.pushsection .rodata, "a"
intname_\vecname:
	.asciz "\vecname"
	.popsection

	.pushsection .rodata.int_names, "a"
	.org	(\vecnum << 3)
	.quad	intname_\vecname
	.popsection

	.org	(\vecnum << 8)

	.endm

	//
	// Default interrupt handler.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// c_routine is the C routine we'll call.
	//
	.macro	int_hand vecnum, vecname, c_routine

	vec_start \vecnum, \vecname

intvec_\vecname:
	// Temporarily save a register so we have somewhere to work.

	mtspr	SYSTEM_SAVE_BME_0, r0

	//
	// Unlike in a multi-PL environment like the hypervisor or Linux, in
	// BME interrupts run on the current stack.  If an application wants
	// to use multiple PLs, and doesn't want to trust that a lower PL has
	// a valid SP, this might need revision, along the lines of the
	// following commented-out code:
	//
	// FIXME:        this is a tilepro example
        //
	//   //
	//   // If we're coming from an MPL less than that of the BME,
	//   // then point to the base of the interrupt stack.  Otherwise, we
	//   // assume that the stack pointer is already valid.  Note that we
	//   // can check for the whole register being equal to the BME's MPL
	//   // here, without masking off the ICS bit; if the ICS bit is set,
	//   // and this is coming from the BME PL, that would have to be a
	//   // double fault interrupt, which doesn't use this generic code.
	//   //
	//
	//   mfspr	r0, EX_CONTEXT_BME_1
	//   seqi	r0, r0, BME_PL
	//   {
	//    bbs	r0, 0f
	//    addi	r0, sp, -8
	//   }
	//   
	//   moveli	r0, lo16(_estack - 4)
	//   auli	r0, r0, ha16(_estack - 4)
	// 0:
	//
	// which would replace the "addi r0, sp, -8" below.
	//

	addi	r0, sp, -8

	// Push the parameter-passing registers on the stack, so that we can
	// set them to vector-unique values before we jump to the common code.
	// Registers are pushed on the stack in this order:
	//
	// (untouched)		 <- initial stack pointer value, highest
	//                          address
	// sp
	// lr
	// r0
	// r1
	// r2
	// .
	// .
	// .
	// r12			 <- last register saved by this macro
	// ------------------------------------------------------------------
	// r13			 <- first register saved by handle_interrupt;
 	//			    sp is here when we jump to handle_interrupt
	// .
	// .
	// .
	// r29
	// EX_CONTEXT_BME_1
	// EX_CONTEXT_BME_0
	// Interrupt mask
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address


	push_reg sp, r0

	push_reg lr, r0
	{
	 mfspr	r0, SYSTEM_SAVE_BME_0    // Original r0
	 move	sp, r0
	}
	push_reg r0
	push_reg r1
	push_reg r2
	push_reg r3
	push_reg r4
	push_reg r5
	push_reg r6
	push_reg r7
	push_reg r8
	push_reg r9
	push_reg r10
	push_reg r11
	push_reg r12

	//
	// We pass one parameter to the eventual C routine: the number of
	// the interrupt, in r0.  (handle_interrupt may pass more values
	// in addition to those.)  We also pass two values to handle_interrupt
	// itself:
	//
	// - The address of the C routine to jump to (in r10).
	//
	// - A set of interrupts we want masked while we're in the C routine
	//   and not in an interrupt critical section (low word in r11, high
	//   word in r12).  We always mask the interrupt we're handling;
	//   in addition we mask a set of standard things which we never want
	//   to handle at our PL.
	//

	{
	 moveli	r0, \vecnum
	 moveli	r10, hw2_last(\c_routine)
	}

	int_mask \vecnum, r11
	shl16insli r10, r10, hw1(\c_routine)

	// Jump to remainder of interrupt handling code.

	{
	 shl16insli r10, r10, hw0(\c_routine)
	 j	handle_interrupt
	}

	.size intvec_\vecname,.-intvec_\vecname
	.endm

	//
	// Interrupt handler for faults where we save the full register set.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// c_routine is the C routine we'll call.
	//
	.macro  int_hand_full vecnum, vecname, c_routine

	vec_start \vecnum, \vecname

intvec_\vecname:
	// Temporarily save a register so we have somewhere to work.

	mtspr	SYSTEM_SAVE_BME_0, r0

#if 0	
	.ifeq \vecnum - INT_DOUBLE_FAULT

	//
	// For a double fault, we don't trust the current sp; its being
	// hosed is a great way to get here.  We always use a dedicated
	// double-fault stack area.  Right now this is just allocated in
	// static data, which means that under the shared data model, bad
	// things will happen if multiple tiles double fault at once.  It's
	// likely that we'll eventually move to __thread-style allocation
	// of static data which would fix this problem.
	//

	.pushsection .bss._bme_dblflt_stack, "aw"
_bme_dblflt_stack:
	.align	8
	.skip	4096
_bme_dblflt_stack_top:
	.popsection

	moveli	r0, hw2_last(_bme_dblflt_stack_top)
	shl16insli r0, r0, hw1(_bme_dblflt_stack_top)
	shl16insli r0, r0, hw0(_bme_dblflt_stack_top)
	
	.else
	//
	// Unlike in a multi-PL environment like the hypervisor or Linux, in
	// BME interrupts run on the current stack.  If an application wants
	// to use multiple PLs, and doesn't want to trust that a lower PL has
	// a valid SP, this might need revision, along the lines of the
	// following commented-out code:
	//
        // FIXME:        this is a tilepro example
	//
	//   //
	//   // If we're coming from an MPL less than that of the BME,
	//   // then point to the base of the interrupt stack.  Otherwise, we
	//   // assume that the stack pointer is already valid.  Note that we
	//   // can check for the whole register being equal to the BME's MPL
	//   // here, without masking off the ICS bit; if the ICS bit is set,
	//   // and this is coming from the BME PL, that would have to be a
	//   // double fault interrupt, which doesn't use this generic code.
	//   //
	//
	//   mfspr	r0, EX_CONTEXT_BME_1
	//   seqi	r0, r0, BME_PL
	//   {
	//    bbs	r0, 0f
	//    addi	r0, sp, -4
	//   }
	//   
	//   moveli	r0, lo16(_estack - 4)
	//   auli	r0, r0, ha16(_estack - 4)
	// 0:
	//
	// which would replace the "addi r0, sp, -4" below.
	//

	addi	r0, sp, -8
	.endif
#endif

	// Push the parameter-passing registers on the stack, so that we can
	// set them to vector-unique values before we jump to the common code.
	// Registers are pushed on the stack in this order:
	//
	// (untouched)	      <- initial stack pointer value, highest
	//                       address
	// sp
	// lr
	// r0
	// r1
	// r2
	// .
	// .
	// .
	// r12		      <- last register saved by this macro
	// ------------------------------------------------------------------
	// r13		      <- first register saved by handle_intr_full;
 	//		         sp is here when we jump to handle_intr_full
	// .
	// .
	// .
	// r52
	// r53/tp
	// EX_CONTEXT_BME_1
	// EX_CONTEXT_BME_0
	// Interrupt mask
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address

       	push_reg sp, r0

	push_reg lr, r0
	{
	 mfspr	r0, SYSTEM_SAVE_BME_0    // Original r0
	 move	sp, r0
	}
	push_reg r0
	push_reg r1
	push_reg r2
	push_reg r3
	push_reg r4
	push_reg r5
	push_reg r6
	push_reg r7
	push_reg r8
	push_reg r9
	push_reg r10
	push_reg r11
	push_reg r12

	//
	// We pass one parameter to the eventual C routine: the number of
	// the interrupt, in r0.  (handle_intr_full may pass more values
	// in addition to those.)  We also pass two values to
	// handle_intr_full itself:
	//
	// - The address of the C routine to jump to (in r10).
	//
	// - A set of interrupts we want masked while we're in the C routine
	//   and not in an interrupt critical section (low word in r11, high
	//   word in r12).  We always mask the interrupt we're handling;
	//   in addition we mask a set of standard things which we never want
	//   to handle at our PL.
	//

	{
         moveli	r0, \vecnum
	 moveli	r10, hw2_last(\c_routine)
	}
	int_mask \vecnum, r11
	shl16insli r10, r10, hw1(\c_routine)

	// Jump to remainder of interrupt handling code.

	{
	 j	handle_intr_full
	 shl16insli r10, r10, hw0(\c_routine)
	}

	.size intvec_\vecname,.-intvec_\vecname
	.endm

//
// Done with macro definitions, here are our vectors
//
	.section DOT_INTRPT_BME, "ax"

	//
	// This symbol is referenced by the BME runtime startup code to force
	// the object module containing the vectors to be linked in to the BME
	// application.  If an application wants to define its own vectors
	// statically, it should define this symbol in those vectors so that
	// both sets aren't linked in.
	//
	.global interrupt_vectors
interrupt_vectors:


        int_hand_full INT_MEM_ERROR, MEM_ERROR, bme_bad_intr
	int_hand_full INT_SINGLE_STEP_3, SINGLE_STEP_3, bme_bad_intr
	int_hand_full INT_SINGLE_STEP_2, SINGLE_STEP_2, bme_bad_intr
	int_hand_full INT_SINGLE_STEP_1, SINGLE_STEP_1, bme_bad_intr
	int_hand_full INT_SINGLE_STEP_0, SINGLE_STEP_0, bme_bad_intr
	int_hand_full INT_IDN_COMPLETE, IDN_COMPLETE, bme_bad_intr
	int_hand_full INT_UDN_COMPLETE, UDN_COMPLETE, bme_bad_intr
	int_hand_full INT_ITLB_MISS, ITLB_MISS, bme_bad_intr
	int_hand_full INT_ILL, ILL, bme_bad_intr
	int_hand_full INT_GPV, GPV, bme_bad_intr
	int_hand_full INT_IDN_ACCESS, IDN_ACCESS, bme_bad_intr
	int_hand_full INT_UDN_ACCESS, UDN_ACCESS, bme_bad_intr
	int_hand_full INT_SWINT_3, SWINT_3, bme_bad_intr
	int_hand_full INT_SWINT_2, SWINT_2, bme_bad_intr
	int_hand_full INT_SWINT_1, SWINT_1, bme_bad_intr
	int_hand_full INT_SWINT_0, SWINT_0, bme_bad_intr
	int_hand_full INT_ILL_TRANS, ILL_TRANS, bme_bad_intr
	int_hand_full INT_UNALIGN_DATA, UNALIGN_DATA, bme_bad_intr
	int_hand_full INT_DTLB_MISS, DTLB_MISS, bme_bad_intr
	int_hand_full INT_DTLB_ACCESS, DTLB_ACCESS, bme_bad_intr
	int_hand_full INT_IDN_FIREWALL, IDN_FIREWALL, bme_bad_intr
	int_hand_full INT_UDN_FIREWALL, UDN_FIREWALL, bme_bad_intr
	int_hand_full INT_TILE_TIMER, TILE_TIMER, bme_bad_intr
	int_hand_full INT_IDN_TIMER, IDN_TIMER, bme_bad_intr
	int_hand_full INT_UDN_TIMER, UDN_TIMER, bme_bad_intr
	int_hand_full INT_IDN_AVAIL, IDN_AVAIL, bme_bad_intr
	int_hand_full INT_UDN_AVAIL, UDN_AVAIL, bme_bad_intr
	int_hand_full INT_IPI_3, IPI_3, bme_bad_intr
	int_hand_full INT_IPI_2, IPI_2, bme_bad_intr
	int_hand_full INT_IPI_1, IPI_1, bme_bad_intr
	int_hand_full INT_IPI_0, IPI_0, bme_bad_intr
	int_hand_full INT_PERF_COUNT, PERF_COUNT, bme_bad_intr
	int_hand_full INT_AUX_PERF_COUNT, AUX_PERF_COUNT, bme_bad_intr
	int_hand_full INT_INTCTRL_3, INTCTRL_3, bme_bad_intr
	int_hand_full INT_INTCTRL_2, INTCTRL_2, bme_bad_intr
	int_hand_full INT_INTCTRL_1, INTCTRL_1, bme_bad_intr
	int_hand_full INT_INTCTRL_0, INTCTRL_0, bme_bad_intr
	int_hand_full INT_BOOT_ACCESS, BOOT_ACCESS, bme_bad_intr
	int_hand_full INT_WORLD_ACCESS, WORLD_ACCESS, bme_bad_intr
	int_hand_full INT_I_ASID, I_ASID, bme_bad_intr
	int_hand_full INT_D_ASID, D_ASID, bme_bad_intr
	int_hand_full INT_DOUBLE_FAULT, DOUBLE_FAULT, bme_bad_intr
	
	//
	// Common code for most interrupts.  The C function we're eventually
	// going to is in r10, and its args are in r0...r9; the original
	// values for all of those registers, plus some others, are on the
	// stack (see stack layout above in the int_hand macro).
	//
handle_interrupt:
	// Save the rest of the registers that we didn't save in the actual	
	// vector itself

	push_reg r13
	push_reg r14
	push_reg r15
	push_reg r16
	push_reg r17
	push_reg r18
	push_reg r19
	push_reg r20
	push_reg r21
	push_reg r22
	push_reg r23
	push_reg r24
	push_reg r25
	push_reg r26
	push_reg r27
	push_reg r28
	push_reg r29
	mfspr	r29, EX_CONTEXT_BME_1
	push_reg r29
	mfspr	r29, EX_CONTEXT_BME_0
	push_reg r29

	// Save the current interrupt masks

	mfspr	r20, INTERRUPT_MASK_BME
//	push_reg r20	 // First interrupt mask word
        st sp, r20	 // Interrupt mask word

	// Then mask the requested interrupts

	mtspr	INTERRUPT_MASK_SET_BME, r11

        {
	 // Pass the state pointer to the C routine as the second argument, so
	 // that it can get at the saved registers.
	 move	r1, sp
	 addi	sp, sp, -C_ABI_SAVE_AREA_SIZE // Allocate caller state save area
        }

	// Everything is saved now; exit from the critical section

	mtspr	INTERRUPT_CRITICAL_SECTION, zero

	// Jump to the C routine
	jalr	r10

	// Upon return, fall through to...

return_from_interrupt:
	// Enable critical section bit

	movei	r29, 1

        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, r29
	 addi	sp, sp, C_ABI_SAVE_AREA_SIZE
        }

	// Restore the previous interrupt masks, the EX_CONTEXT values, and
	// the registers.

	pop_reg r29
	mtspr	INTERRUPT_MASK_BME, r29
	pop_reg r29
	mtspr	EX_CONTEXT_BME_0, r29
	pop_reg r29
	mtspr	EX_CONTEXT_BME_1, r29
	pop_reg r29
	pop_reg r28
	pop_reg r27
	pop_reg r26
	pop_reg r25
	pop_reg r24
	pop_reg r23
	pop_reg r22
	pop_reg r21
	pop_reg r20
	pop_reg r19
	pop_reg r18
	pop_reg r17
	pop_reg r16
	pop_reg r15
	pop_reg r14
	pop_reg r13
	pop_reg r12
	pop_reg r11
	pop_reg r10
	pop_reg r9
	pop_reg r8
	pop_reg r7
	pop_reg r6
	pop_reg r5
	pop_reg r4
	pop_reg r3
	pop_reg r2
	pop_reg r1
	pop_reg r0
	pop_reg lr
	ld	sp, sp

	iret


	//
	// Code to handle interrupts which save the full register set.
	// The C function we're eventually going to is in r10, and its args
	// are in r0...r9; the original values for all of those registers,
	// plus some others, are on the stack (see stack layout above in
	// the int_hand_full macro).
	//
handle_intr_full:
	// Save the rest of the registers that we didn't save in the actual	
	// vector itself

	push_reg r13
	push_reg r14
	push_reg r15
	push_reg r16
	push_reg r17
	push_reg r18
	push_reg r19
	push_reg r20
	push_reg r21
	push_reg r22
	push_reg r23
	push_reg r24
	push_reg r25
	push_reg r26
	push_reg r27
	push_reg r28
	push_reg r29
	push_reg r30
	push_reg r31
	push_reg r32
	push_reg r33
	push_reg r34
	push_reg r35
	push_reg r36
	push_reg r37
	push_reg r38
	push_reg r39
	push_reg r40
	push_reg r41
	push_reg r42
	push_reg r43
	push_reg r44
	push_reg r45
	push_reg r46
	push_reg r47
	push_reg r48
	push_reg r49
	push_reg r50
	push_reg r51
	push_reg r52
	push_reg tp
	mfspr	r52, EX_CONTEXT_BME_1
	push_reg r52
	mfspr	r52, EX_CONTEXT_BME_0
	push_reg r52

	// Calculate the address of the interrupt routine.  It is stored
	// in a table in the BME state structure, which is pointed to
	// by the tp register (r53).  Store it in r10.

	// Get the address of the interrupt pointer table, which is just an
        // offset from the start of the _bme_state_t.
	addi    r7, tp, BS_INT_HANDLER_OFF

	// Get the address of the handler for the interrupt we are
	// servicing.  The interrupt number is in r0.
	
	// Calculate the offset into the table of pointers to interrupt
        // handlers (sizeof(ptr) * interrupt_number).
	shl3add r7, r0, r7

	// Load the address of the interrupt handler into r10 if it
        // is not 0.  If it is 0, use the default which is already in r10.
	ld      r7, r7
	cmovnez r10, r7, r7

	// Save the current interrupt masks

	mfspr	r20, INTERRUPT_MASK_BME
	st	sp, r20  // Interrupt mask word

	// Then mask the requested interrupts

	mtspr	INTERRUPT_MASK_SET_BME, r11

        {
	 // Pass the state pointer to the C routine as the second argument, so
	 // that it can get at the saved registers.
	 move	r1, sp
	 addi	sp, sp, -C_ABI_SAVE_AREA_SIZE // Allocate caller state save area
        }

	// Everything is saved now; exit from the critical section

	mtspr	INTERRUPT_CRITICAL_SECTION, zero

	// Jump to the C routine
	jalr	r10

	// Upon return, fall through to...

return_from_intr_full:
	// Enable critical section bit

	movei	r52, 1

        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, r52
	 addi	sp, sp, C_ABI_SAVE_AREA_SIZE
        }

	// Restore the previous interrupt masks, the EX_CONTEXT values, and
	// the registers.

	pop_reg r52
	mtspr	INTERRUPT_MASK_BME, r52
	pop_reg r52
	mtspr	EX_CONTEXT_BME_0, r52
	pop_reg r52
	mtspr	EX_CONTEXT_BME_1, r52
	pop_reg tp
	pop_reg r52
	pop_reg r51
	pop_reg r50
	pop_reg r49
	pop_reg r48
	pop_reg r47
	pop_reg r46
	pop_reg r45
	pop_reg r44
	pop_reg r43
	pop_reg r42
	pop_reg r41
	pop_reg r40
	pop_reg r39
	pop_reg r38
	pop_reg r37
	pop_reg r36
	pop_reg r35
	pop_reg r34
	pop_reg r33
	pop_reg r32
	pop_reg r31
	pop_reg r30
	pop_reg r29
	pop_reg r28
	pop_reg r27
	pop_reg r26
	pop_reg r25
	pop_reg r24
	pop_reg r23
	pop_reg r22
	pop_reg r21
	pop_reg r20
	pop_reg r19
	pop_reg r18
	pop_reg r17
	pop_reg r16
	pop_reg r15
	pop_reg r14
	pop_reg r13
	pop_reg r12
	pop_reg r11
	pop_reg r10
	pop_reg r9
	pop_reg r8
	pop_reg r7
	pop_reg r6
	pop_reg r5
	pop_reg r4
	pop_reg r3
	pop_reg r2
	pop_reg r1
	pop_reg r0
	pop_reg lr
	ld	sp, sp

	iret
