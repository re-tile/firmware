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
 * Miscellaneous stuff useful in assembly code.
*/

// FIXME: compare this and all assembly against hv to see if there's something we missed in translation.

#ifndef _SYS_BME_ASM_INC_H
#define _SYS_BME_ASM_INC_H

#include <arch/abi.h>

#define BUNDLE_SIZE     8       ///< Bytes per instruction bundle

#ifndef __DOXYGEN__
	.altmacro	// For "local" pseudo-op

	// Clear two registers.

	.macro	clr2 rnum1, rnum2
	{
	 move	\rnum1, zero
	 move	\rnum2, zero
	}
	.endm


	//
	// Push/pop registers on/off a stack.
	//
	// Note that the the ABI specifies that when calling a function,
	// the sp points at the bottom word of the caller's stack frame.
	// This macro always decrements after storing, instead of before, so
	// you'll see that when we push a bunch of stuff on the stack, we
	// first decrement the sp; then we do a bunch of push_regs; then the
	// last thing that gets pushed gets pushed with "st sp, <foo>" rather
	// than "push_reg <foo>".  This makes the stack pointer end up
	// in the right spot.  The pop_reg macro does not have this problem,
	// and doesn't need any special pre- or postamble.
	//
	.macro	push_reg reg, ptr=sp
	{
	 st	\ptr, \reg
	 addi	\ptr, \ptr, -8
	}
	.endm

	.macro	pop_reg reg, ptr=sp
	{
	 ld	\reg, \ptr
	 addi	\ptr, \ptr, 8
	}
	.endm


	//
	// Assembly function entry/exit macros.
	//
	// FIXME: the code which uses push_reg/pop_reg should probably be
	// rewritten to use the macros below, or to at least be more
	// culturally compatible with them.
	//

	//
	// Helper macros for function_entry/function_exit
	//
	.macro	store_and_inc  ptr, reg, extra_inc=0
	{
	 st	\ptr, \reg
	 addi	\ptr, \ptr, 8 + \extra_inc
	}
	.endm

	.macro	load_and_inc  reg, ptr
	{
	 ld	\reg, \ptr
	 addi	\ptr, \ptr, 8
	}
	.endm

	// Number of registers saved via store_and_inc below.
	_asm_inc_num_regs = 24

	//
	// Save all of the callee-saved registers on the stack, so we can
	// eventually restore them when we exit.
	//
	// @param local_base Register which will point to the base of the
	//        allocated local storage after completion of the macro; will
	//        be 8-byte-aligned.  This is used as a temporary register
	//        within the macro, so it must be specified even if no local
	//        storage is allocated.  This must be a caller-saved register.
	// @param local_size Number of bytes to allocate on stack for local
	//        storage (in addition to the register save area); if not
	//        specified, no local storage is allocated.
	//
	.macro	function_entry local_base, local_size=0
	local actual_local_size

	// Actual size we'll reserve for local storage (rounded up).
	actual_local_size = (\local_size + 7) & -8

	{
	 // Save lr in previous stack frame.
	 st	sp, lr
	 // Point to frame pointer area.
	 addli	\local_base, sp, -(_asm_inc_num_regs * 8 + \
				   (\actual_local_size) + \
				   8)
	}
	{
	 // Save frame pointer.
	 st	\local_base, sp
	 // Point to register save area; this is the lowest-addressed part of
	 // the locals.
	 addi	\local_base, \local_base, 8
	}
	{
	 // Point sp at base of new stack frame.
	 addli	sp, sp, -(_asm_inc_num_regs * 8 + \
			  (\actual_local_size) + \
			  C_ABI_SAVE_AREA_SIZE)
	}
	//
	// Save registers.  If this list is modified, you must change
	// _asm_inc_num_regs above, as well as the function_exit macro.
	//
	store_and_inc \local_base, r30
	store_and_inc \local_base, r31
	store_and_inc \local_base, r32
	store_and_inc \local_base, r33
	store_and_inc \local_base, r34
	store_and_inc \local_base, r35
	store_and_inc \local_base, r36
	store_and_inc \local_base, r37
	store_and_inc \local_base, r38
	store_and_inc \local_base, r39
	store_and_inc \local_base, r40
	store_and_inc \local_base, r41
	store_and_inc \local_base, r42
	store_and_inc \local_base, r43
	store_and_inc \local_base, r44
	store_and_inc \local_base, r45
	store_and_inc \local_base, r46
	store_and_inc \local_base, r47
	store_and_inc \local_base, r48
	store_and_inc \local_base, r49
	store_and_inc \local_base, r50
	store_and_inc \local_base, r51
	store_and_inc \local_base, r52
	store_and_inc \local_base, tp

	//
	// At this point we've advanced local_base past the register save
	// area, so it's now pointing at the base of the extra local space.
	//
	.endm

	//
	// Get the callee-saved registers from the stack, and return.
	//
	// @param tmp Temporary register to use while restoring.  This must
	//        be a caller-saved register.
	// @param local_size Number of bytes which were allocated on the
	//        stack for local storage (in addition to the register save
	//	  area).  Must match the value passed to function_entry.
	//
	.macro	function_exit tmp, local_size=0 
	local actual_local_size

	// Actual size we'll reserve for local storage (rounded up).
	actual_local_size = (\local_size + 7) & -8

	{
	 // Point tmp at original SP value.
	 addli	\tmp, sp, _asm_inc_num_regs * 8 + \
			  (\actual_local_size) + \
			  C_ABI_SAVE_AREA_SIZE
	}
	{
	 // Get lr back from previous stack frame.
	 ld	lr, \tmp
	 // Point to start of register save area.
	 addi	\tmp, sp, C_ABI_SAVE_AREA_SIZE
	}
	//
	// Restore registers.  If this list is modified, you must change
	// the function_entry macro.
	//
	load_and_inc r30, \tmp
	load_and_inc r31, \tmp
	load_and_inc r32, \tmp
	load_and_inc r33, \tmp
	load_and_inc r34, \tmp
	load_and_inc r35, \tmp
	load_and_inc r36, \tmp
	load_and_inc r37, \tmp
	load_and_inc r38, \tmp
	load_and_inc r39, \tmp
	load_and_inc r40, \tmp
	load_and_inc r41, \tmp
	load_and_inc r42, \tmp
	load_and_inc r43, \tmp
	load_and_inc r44, \tmp
	load_and_inc r45, \tmp
	load_and_inc r46, \tmp
	load_and_inc r47, \tmp
	load_and_inc r48, \tmp
	load_and_inc r49, \tmp
	load_and_inc r50, \tmp
	load_and_inc r51, \tmp
	load_and_inc r52, \tmp
	load_and_inc tp, \tmp
	{
	 // Reset sp back to its original value and return.
	 addli	sp, sp, _asm_inc_num_regs * 8 + \
			(\actual_local_size) + \
			C_ABI_SAVE_AREA_SIZE
	 jrp	lr
	}
	.endm

	// Panic when our stack pointer isn't valid.
#ifdef FPGA
	// We set PASS as well as FAIL because we don't get visibility
	// into FAIL on the FPGA.
#endif

	.macro  panic_nostk arg
	{
	 movei  r1, 1
	 movei  r2, \arg
	}
	mtspr   PASS, r2
	mtspr   FAIL, r2
	mtspr   DONE, r1
	j       .
	.endm


	// Call panic with the supplied string(s) as the first argument.  If
	// you want to specify more arguments, load them into the appropriate
	// registers before calling this macro.

	.macro  panic str, str2, str3
	local panic_str
	.pushsection .rodata, "a"
panic_str:
	.ascii  \str, \str2, \str3
	.byte 0
	.popsection
	moveli r0, hw2_last(panic_str)
	shl16insli r0, r0, hw1(panic_str)
	{
	 shl16insli r0, r0, hw0(panic_str)
	 jal    panic
	}
	.endm

	//
	// The fence_incoherent flow uses physical memory mode to issue
	// an uncacheable load to each of the client's memory controllers.
	// We unroll this loop manually in order to use a different register
	// for each load, so that they can issue in parallel.  This macro
	// is used to implement both the sys_fence_incoherent syscall, and
	// the hypervisor's __mf_incoherent routine.
	//
	// @param table_name Name of the table of physical addresses to use.
	// @param disable_intr If nonzero, enable INTERRUPT_CRITICAL_SECTION
	//  during the fence operation.  (This should only be zero if we
	//  know that ICS is set on entry to this macro; otherwise we could
	//  be interrupted by code which isn't prepared to run in physical
	//  memory mode.)
	// 
	// Register usage:
	//
	// r0      Saved value of INTERRUPT_CRITICAL_SECTION, if disable_intr
	//         is nonzero.
	// r1      Pointer to the named table of physical addresses.
	// r2      1, to set PMM and ICS SPRs.
	// r3-r10  Address values from the table (see comment below); also
	//         destinations for the loads.
	//
	.macro fence_incoherent table_name, disable_intr
	.local fence_incoherent_exit

#define AAR_VALUE \
        ((SPR_AAR__PHYSICAL_MEMORY_MODE_MASK) | \
         (SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE << \
          SPR_AAR__MEMORY_ATTRIBUTE_SHIFT))

	.ifne \disable_intr
	{
	 mfspr	r0, INTERRUPT_CRITICAL_SECTION
	 movei  r2, 1       // To enable ICS.
	}
	{
	 moveli r1, hw2_last(\table_name)
	 mtspr	INTERRUPT_CRITICAL_SECTION, r2
	}
        {
	 moveli r3, hw2_last(AAR_VALUE)
        }
	.else
        {
	 moveli r1, hw2_last(\table_name)
	 moveli r3, hw2_last(AAR_VALUE)
        }
	.endif
	 
        {
	 shl16insli r1, r1, hw1(\table_name)
	 shl16insli r3, r3, hw1(AAR_VALUE)
        }
        {
	 shl16insli r1, r1, hw0(\table_name)
	 shl16insli r3, r3, hw0(AAR_VALUE)
        }

	// Read the table to get the addresses to load from.
	pop_reg r4, r1
	pop_reg r5, r1
	pop_reg r6, r1
	pop_reg r7, r1

	// We check for the -1 'no more mshims' value before issuing a load.
	mtspr   AAR, r3

	bltz     r4, fence_incoherent_exit
	ld1u     r8, r4
	
	bltz     r5, fence_incoherent_exit
	ld1u     r9, r5
	
	bltz     r6, fence_incoherent_exit
	ld1u     r10, r6

	bltz     r7, fence_incoherent_exit
	ld1u     r11, r7

fence_incoherent_exit:
	mtspr   AAR, zero
	mf      // Make sure all those loads come back.

	.ifne	\disable_intr
	mtspr	INTERRUPT_CRITICAL_SECTION, r0
	.endif	

	.endm

#endif /* __DOXYGEN__ */

#endif /* _SYS_BME_ASM_INC_H */
