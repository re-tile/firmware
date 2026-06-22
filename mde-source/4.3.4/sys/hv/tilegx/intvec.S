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
// Hypervisor interrupt vectors.
//
#include <arch/interrupts.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "downcall_asm.h"
#include "drvchan.h"
#include "fastio.h"
#include "hv.h"
#include "hw_config.h"
#include "tsb.h"
#include "syscall.h"

#include "asm_inc.h"
#include "patch.h"

// FIXME: the register/save restore stuff needs to be factored out into a
// macro so we can use it in two places.

// Warning: The ABI requires that sp be 16-byte aligned at call
// boundaries.  The initial stack offset is calculated based on the
// amount of data pushed on the stack afterward, so that the final
// offset, before we jump to a C routine, is 16-byte aligned; if the
// number of words saved changes, the initial offset may also need to
// change.

	.altmacro	// Necessary for \xx& macro substitution feature

	//
	// Put the handle-while-in-supervisor interrupt mask, plus the
	// specified vector, into the specified register.  (If the
	// vector is less than zero, ignore it and just store the mask.)
	//
	// FIXME: GX: since this expands to 3 half-full bundles, we might
	// want to just inline it in the 5 or so places it's used.
	//
	.macro int_mask vec, reg
	.iflt   \vec
	 moveli \reg, hw2_last(HV_INTS_HANDLED_SV)
	 shl16insli \reg, \reg, hw1(HV_INTS_HANDLED_SV)
	 shl16insli \reg, \reg, hw0(HV_INTS_HANDLED_SV)
	.else
	 moveli \reg, hw2_last(HV_INTS_HANDLED_SV | INT_MASK(\vec))
	 shl16insli \reg, \reg, hw1(HV_INTS_HANDLED_SV | INT_MASK(\vec))
	 shl16insli \reg, \reg, hw0(HV_INTS_HANDLED_SV | INT_MASK(\vec))
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

	patch_init stats

	.pushsection .bss.intr_stats, "aw"
	.global intr_stats
intr_stats:
	//
	// For each interrupt, we have the total number of cycles used in
	// processing all occurrences, followed by the maximum number of
	// cycles used to process one occurrence, followed by a count of
	// times we've seen it.
	//
	.skip 3 * 8 * NUM_INTERRUPTS
	.size intr_stats,.-intr_stats
	.popsection

	//
	// Define a label for the start of the array of pointers to syscall
	// names; we'll fill this in via the syscall macro.  (Unlike the
	// interrupt names, which are used in the default hypervisor config,
	// syscall names are only used if syscall statistics are enabled.)
	//
	.pushsection .rodata.syscall_names, "a"
	.global	syscall_names
	.align 8
syscall_names:
	.org	0
	.popsection

	.pushsection .bss.syscall_stats, "aw"
	.global syscall_stats
syscall_stats:
	//
	// For each syscall, we have the total number of cycles used in
	// processing all occurrences, followed by the maximum number of
	// cycles used to process one occurrence, followed by a count of
	// times we've seen it.
	//
	.skip 3 * 8 * (HV_MAX_SYSCALL + 1)
	.size syscall_stats,.-syscall_stats
	.popsection

	//
	// Push data on the stack at the start of an event that we want to
	// collect hypervisor stats on.
	//
	// tmp_reg - temporary register.
	// stk_reg - register which points to stack; will be updated.
	// base - base of array of hv_stats structures.
	// index - index into array of hv_stats structures.
	// is_syscall - if nonzero, this is the syscall entry
	// save_tmp - if not blank, name of a system save register where
	//  we'll save/restore tmp_reg before/after using it.
	//
	.macro	stats_enter tmp_reg, stk_reg, base, index, is_syscall, save_tmp
	//
	// If needed, save temporary register.
	//
	.ifnb	\save_tmp
	mtspr	\save_tmp, \tmp_reg
	.endif

	//
	// Get cycle count, push on stack.
	//
	mfspr	\tmp_reg, CYCLE
	push_reg \tmp_reg, \stk_reg

	.ifne \is_syscall
	//
	// In the syscall vector, we only have room for one bundle to push
	// the identifying data, so we push the (rotated) syscall number.
	//
	push_reg TREG_SYSCALL_NR_NAME, \stk_reg
	.else
	//
	// Otherwise, compute the address of event struct, push on stack.
	//
	moveli	\tmp_reg, hw2_last(\base + 24 * \index)
	shl16insli \tmp_reg, \tmp_reg, hw1(\base + 24 * \index)
	shl16insli \tmp_reg, \tmp_reg, hw0(\base + 24 * \index)
	push_reg \tmp_reg, \stk_reg
	.endif

	//
	// If needed, restore temp register.
	//
	.ifnb	\save_tmp
	mfspr	\tmp_reg, \save_tmp
	.endif

	.endm

	//
	// Update the stats structure at the end of a monitored event.
	//
	// stack_skip - where the data saved on the stack by stats_enter
	//   is, in words relative to sp.
	// tmp0-tmp3 - temporary registers.
	// is_sycall - nonzero if this is the syscall return path.
	//
	.macro	stats_exit stack_skip, tmp0, tmp1, tmp2, tmp3, is_syscall
	{
	 // Get end cycle counter.
	 mfspr	\tmp2, CYCLE
	 // Skip to-be-restored regs on stack to get to our state.
	 addi	\tmp0, sp, \stack_skip * 8
	}
	{
	 // Get state pointer (or rotated syscall number).
	 ld	\tmp1, \tmp0
	 // Point to start cycle count
	 addi	\tmp0, \tmp0, 8
	}

	.ifne	\is_syscall
	//
	// In the syscall interrupt vector, because we're tight on space,
	// we only save the rotated syscall number to the stack, not a
	// full pointer to the corresponding hv_stats structure.  So, here
	// we need to create that pointer.
	//
	{
	 // Unrotate syscall number.
	 rotli	\tmp1, \tmp1, HV_SYS_FAST_PLO_SHIFT
	 // Get multiplier (size of struct hv_stats).
	 movei	\tmp3, 3 * 8
	}
	{
	 // Compute offset into array.
	 mul_lu_lu \tmp1, \tmp1, \tmp3
	 // Start building base address.
	 moveli	\tmp3, hw2_last(syscall_stats)
	}
	// Finish base address.
	shl16insli \tmp3, \tmp3, hw1(syscall_stats)
	shl16insli \tmp3, \tmp3, hw0(syscall_stats)
	// Compute final address.
	add	\tmp1, \tmp3, \tmp1
	.endif

	// Get starting cycle counter.
	ld	\tmp0, \tmp0

	{
	 // Compute cycles for this instance.
	 sub	\tmp0, \tmp2, \tmp0
	 // Get old total cycles.
	 ld	\tmp2, \tmp1
	}

	// Compute new total cycles.
	add	\tmp2, \tmp2, \tmp0

	{
	 // Store new total cycles.
	 st	\tmp1, \tmp2
	 // Point to old max cycles/instance.
	 addi	\tmp1, \tmp1, 8
	}

	// Get old max cycles/instance.
	ld	\tmp2, \tmp1
	// Were there more cycles for current instance?.
	cmpleu	\tmp3, \tmp2, \tmp0
	// If so, set tmp2 to the new number.
	cmovnez	\tmp2, \tmp3, \tmp0
	{
	 // Store new max cycles/instance.
	 st	\tmp1, \tmp2
	 // Point to number of events.
	 addi	\tmp1, \tmp1, 8
	}
	// Get number of events.
	ld	\tmp2, \tmp1
	// Bump number of events.
	addi	\tmp2, \tmp2, 1
	// Store number of events.
	st	\tmp1, \tmp2

	.endm

	//
	// Create a pointer to the base of our stack, and put it in register
	// reg.  We check to see if we were already in the hypervisor; if
	// so, we just use the current stack pointer.  Note that when we
	// test against the hypervisor's PL, we don't mask off the ICS bit
	// from EX_CONTEXT_1; that's because that would only be set if we
	// were handling a double fault interrupt, and we don't use this
	// macro there (we have a dedicated double-fault stack).  The offset
	// parameter specifies the amount to subtract from the stack.  It is
	// used to properly initialize the stack pointer so that after all
	// the data is pushed, the final stack offset before we jump to a C
	// routine is 16-byte aligned.  (The default offset of 8 bytes sets
	// the pointer to the last word of the stack, so that it can be used
	// directly to store a word onto the stack.)
	//
	.macro	get_stack reg, offset=8
	local	use_current_stack

	mfspr	\reg, EX_CONTEXT_HV_1
	cmpeqi	\reg, \reg, HV_PL
	{
	 blbs	\reg, use_current_stack
	 addi	\reg, sp, - \offset
	}
	moveli	\reg, hw2_last(_estack - \offset)
	shl16insli \reg, \reg, hw1(_estack - \offset)
	shl16insli \reg, \reg, hw0(_estack - \offset)
use_current_stack:

	.endm

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
	// Null TLB miss handler; this doesn't check the TSB, but always
	// just hands off to the TSB miss handler.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// which_tlb is 'i' or 'd'.
	//
	.macro	tlb_miss_null vecnum, vecname, which_tlb

	//
	// Make our SPR numbers local, to prevent definitions in one macro
	// invocation from affecting others
	//
	local REPLACEMENT_xTLB, xTLB_INDEX

	//
	// Set the SPRs we'll use, based on which TLB we're handling
	//
	.ifeqs	"\which_tlb", "i"
	REPLACEMENT_xTLB = SPR_REPLACEMENT_ITLB
	xTLB_INDEX = SPR_ITLB_INDEX
	.endif

	.ifeqs	"\which_tlb", "d"
	REPLACEMENT_xTLB = SPR_REPLACEMENT_DTLB
	xTLB_INDEX = SPR_DTLB_INDEX
	.endif

	vec_start \vecnum, \vecname

\which_tlb&tlb_miss:

	 mtspr	SYSTEM_SAVE_HV_0, r0
	 mtspr	SYSTEM_SAVE_HV_1, r1
	 mfspr	r0, REPLACEMENT_xTLB
	 mtspr	xTLB_INDEX, r0
	 j	\which_tlb&tsb_miss
	.endm

	//
	// The tlb_miss_1 and tlb_miss_2 macros implement the TLB miss handler.
	// Each must be instantiated exactly once for each of the 2 TLB
	// interrupts: DTLB and ITLB.
	//
	// tlb_miss_1 handles the small page case; if a particular miss isn't
	// found in the small page side of the TSB, the code in tlb_miss_2 is
	// called to handle the large page case.  The theory here is that large
	// pages will be missed on less frequently, so it's OK for them to
	// take longer to handle.
	//
	// tlb_miss_1 expands to code which lives in the interrupt vector
	// space for the relevant TLB miss interrupt.  To save space,
	// tlb_miss_2 expands to code which lives in a subsequent unused
	// interrupt vector, although there's no reason why that code
	// couldn't be placed at any virtual address which was locked in
	// the ITLB and which was reachable from the tlb_miss_1 code via
	// a branch.
	//
	// Assumptions made by the TSB miss handler which this code must meet:
	//
	// - On any entry to the TSB miss handler, the appropriate TLB
	//   index register has already been loaded from the appropriate
	//   replacement register.  (This allows the TSB miss handler to
	//   actually fill the TLB if it so desires.)
	//

	//
	// Part 1 of the TLB miss handler (small page case).
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// which_tlb is 'i' or 'd'.
	//
	.macro	tlb_miss_1 vecnum, vecname, which_tlb

// Also see similar test in do_install_context().
#if HV_CLIENT_SHARED_PAGE_SHIFT < HV_LOG2_DEFAULT_PAGE_SIZE_SMALL
#error Need to implement DO_PAGE_EXTENSION code here, see chip0/1 version
#endif

	//
	// Make our SPR numbers local, to prevent definitions in one macro
	// invocation from affecting others
	//
	local xTLB_INDEX, xTSB_ADDR
	local xTLB_CURRENT_PA, xTLB_CURRENT_VA
	local xTLB_CURRENT_ATTR_MATCH, xTLB_CURRENT_ATTR_FILL

	//
	// Set the SPRs we'll use, based on which TLB we're handling
	//
	.ifeqs	"\which_tlb", "i"
	xTLB_INDEX = SPR_ITLB_INDEX
	xTSB_ADDR = SPR_ITLB_TSB_ADDR_0
	xTLB_CURRENT_PA = SPR_ITLB_CURRENT_PA
	xTLB_CURRENT_VA = SPR_ITLB_CURRENT_VA
	xTLB_CURRENT_ATTR = SPR_ITLB_CURRENT_ATTR
	xTLB_CURRENT_ATTR_MATCH = SPR_ITLB_TSB_FILL_MATCH
	xTLB_CURRENT_ATTR_FILL = SPR_ITLB_TSB_FILL_CURRENT_ATTR
	.endif

	.ifeqs	"\which_tlb", "d"
	xTLB_INDEX = SPR_DTLB_INDEX
	xTSB_ADDR = SPR_DTLB_TSB_ADDR_0
	xTLB_CURRENT_PA = SPR_DTLB_CURRENT_PA
	xTLB_CURRENT_VA = SPR_DTLB_CURRENT_VA
	xTLB_CURRENT_ATTR = SPR_DTLB_CURRENT_ATTR
	xTLB_CURRENT_ATTR_MATCH = SPR_DTLB_TSB_FILL_MATCH
	xTLB_CURRENT_ATTR_FILL = SPR_DTLB_TSB_FILL_CURRENT_ATTR
	.endif

// These are the bits from SPR_DTLB_CURRENT_ATTR__PS_MASK that allow
// specifying page sizes from 4K through 64M.  As long as values
// strictly less than page_size_large = HV_DEFAULT_PAGE_SIZE_LARGE
// are covered by this range, we can optimize and use this value.
#define SMALL_PS_MASK (0x7 << SPR_DTLB_CURRENT_ATTR__PS_SHIFT)

	vec_start \vecnum, \vecname

	//
	// To avoid taking any more cache misses than necessary, this code
	// saves two registers (r0 & r1) in the system save SPRs, then
	// uses only those registers.  Comments after each bundle note
	// the current contents of each register.
	//
\which_tlb&tlb_miss:
	// 0: client reg, 1: client reg
	{
	 mtspr	SYSTEM_SAVE_HV_0, r0
	 movei	r0, 1
	}
	// 0: 1, 1: client reg
	{
	 mtspr	SYSTEM_SAVE_HV_1, r1
	 shli	r0, r0, SPR_DTLB_INDEX__L_SHIFT
	}
	// 0: Index L bit, 1: -
	{
	 mtspr	xTLB_INDEX, r0
	}
	// 0: -, 1: -
	{
	 mfspr	r0, xTSB_ADDR
	}
	// 0: TTE VAPA pointer, 1: -
	{
	 ld	r1, r0
	 addi	r0, r0, CTTE_ATTR_OFFSET - CTTE_VAPA_OFFSET
	}
	// 0: TTE attr pointer, 1: TTE VAPA@PA
	{
	 ld	r0, r0
	}
	// 0: TTE attr, 1: TTE VAPA@PA
	{
	 mtspr	xTLB_CURRENT_PA, r1
	 bfexts r1, r1, CTTE_VAPA_VA_FIELD
	}
	// 0: TTE attr, 1: TTE VAPA@VA
	{  
	 mtspr	xTLB_CURRENT_VA, r1
	 andi	r1, r0, SMALL_PS_MASK
	}
	// 0: TTE attr, 1: TTE attr page size bits only
	{
	 mtspr	xTLB_CURRENT_ATTR, r1
	}
	// 0: TTE attr, 1: -
	{
	 mtspr	xTLB_CURRENT_ATTR_FILL, r0
	}
	// 0: -, 1: -
	{
	 mfspr	r0, xTLB_CURRENT_ATTR_MATCH
	}
	// 0: Was TLB filled?, 1: -
	{
         blbc   r0, \which_tlb&tlb_probe_l
	}
	// 0: -, 1: -
	{
	 mfspr	r0, SYSTEM_SAVE_HV_0
	}
	// 0: client reg, 1: -
	{
	 mfspr	r1, SYSTEM_SAVE_HV_1
	}
	// 0: client reg, 1: client reg
	{
	 iret
	}
	.size \which_tlb&tlb_miss,.-\which_tlb&tlb_miss
	.endm

	//
	// Part 2 of the TLB miss handler (large/jumbo page case).
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// which_tlb is 'i', or 'd'.
	//
	.macro	tlb_miss_2 vecnum, vecname, which_tlb

	local xTSB_ADDR, xFAULT_VA
	local xTLB_CURRENT_PA, xTLB_CURRENT_VA
	local xTLB_CURRENT_ATTR_MATCH, xTLB_CURRENT_ATTR_FILL

	.ifeqs	"\which_tlb", "i"
	xTSB_ADDR = SPR_ITLB_TSB_ADDR_1
	xTLB_CURRENT_PA = SPR_ITLB_CURRENT_PA
	xTLB_CURRENT_VA = SPR_ITLB_CURRENT_VA
	xTLB_CURRENT_ATTR = SPR_ITLB_CURRENT_ATTR
	xTLB_CURRENT_ATTR_MATCH = SPR_ITLB_TSB_FILL_MATCH
	xTLB_CURRENT_ATTR_FILL = SPR_ITLB_TSB_FILL_CURRENT_ATTR
	xFAULT_VA = EX_CONTEXT_HV_0
	.endif

	.ifeqs	"\which_tlb", "d"
	xTSB_ADDR = SPR_DTLB_TSB_ADDR_1
	xTLB_CURRENT_PA = SPR_DTLB_CURRENT_PA
	xTLB_CURRENT_VA = SPR_DTLB_CURRENT_VA
	xTLB_CURRENT_ATTR = SPR_DTLB_CURRENT_ATTR
	xTLB_CURRENT_ATTR_MATCH = SPR_DTLB_TSB_FILL_MATCH
	xTLB_CURRENT_ATTR_FILL = SPR_DTLB_TSB_FILL_CURRENT_ATTR
	xFAULT_VA = SPR_DTLB_BAD_ADDR
	.endif

	//
	// This code is essentially identical to the code in tlb_miss_1,
	// except that:
	//
	// - We don't need to save any registers, since tlb_miss_1 did that;
	//   this means the first few bundles are somewhat rearranged and
	//   compressed.
	//
	// - All of the page-dependent constants are the large page
	//   values or the jumbo page value.
	//
	// - On a miss, we branch first from the large page handler to
	//   the jumbo page handler, then to the TSB miss handler.
	//

	vec_start \vecnum, \vecname

\which_tlb&tlb_probe_l:
	// 0: -, 1: -
	{
	 mfspr	r0, xTSB_ADDR
	}
	// 0: TTE VAPA pointer, 1: -
	{
	 ld	r1, r0
	 addi	r0, r0, CTTE_ATTR_OFFSET - CTTE_VAPA_OFFSET
	}
	// 0: TTE attr pointer, 1: TTE VAPA@PA
	{
	 ld	r0, r0
	}
	// 0: TTE attr, 1: TTE VAPA@PA
	{
	 mtspr	xTLB_CURRENT_PA, r1
	 bfexts r1, r1, CTTE_VAPA_VA_FIELD
	}
	// 0: TTE attr, 1: TTE VAPA@VA
	{
	 mtspr	xTLB_CURRENT_VA, r1
	 bfextu	r1, r0, SPR_DTLB_CURRENT_ATTR__PS_FIELD
	}
	// 0: TTE attr, 1: extracted TTE attr page size bits
	{
	 shli	r1, r1, SPR_DTLB_CURRENT_ATTR__PS_SHIFT
	}
	// 0: TTE attr, 1: TTE attr page size bits only
	{
	 mtspr	xTLB_CURRENT_ATTR, r1
	}
	// 0: TTE attr, 1: -
	{
	 mtspr	xTLB_CURRENT_ATTR_FILL, r0
	}
	// 0: -, 1: -
	{
	 mfspr	r0, xTLB_CURRENT_ATTR_MATCH
	}
	// 0: Was TLB filled?, 1: -
	{
	 blbc	r0, \which_tlb&tlb_probe_j
	}
	// 0: -, 1: -
	{
	 mfspr	r0, SYSTEM_SAVE_HV_0
	}
	// 0: client reg, 1: -
	{
	 mfspr	r1, SYSTEM_SAVE_HV_1
	}
	// 0: client reg, 1: client reg
	{
	 iret
	}
	.size \which_tlb&tlb_probe_l,.-\which_tlb&tlb_probe_l

\which_tlb&tlb_probe_j:
	// 0: -, 1: -
	{
	 mfspr	r0, xFAULT_VA
	 moveli	r1, hw2_last(tsb + (TSB_J_START << CTTE_SHIFT))
	}
	// 0: VA of fault, 1: building up &tsb[TSB_J_START]
	{
	 bfextu	r0, r0, HV_LOG2_DEFAULT_PAGE_SIZE_JUMBO, \
	          HV_LOG2_DEFAULT_PAGE_SIZE_JUMBO + TSB_J_IDX_WIDTH - 1
	 shl16insli r1, r1, hw1(tsb + (TSB_J_START << CTTE_SHIFT))
	}
	// 0: TSB index for TTE for fault, 1: building up &tsb[TSB_J_START]
	{
	 shl16insli r1, r1, hw0(tsb + (TSB_J_START << CTTE_SHIFT))
	 shli	r0, r0, CTTE_SHIFT
	}
	// 0: TSB byte offset for TTE for fault, 1: &tsb[TSB_J_START]
	{
	 add	r0, r0, r1
	}
	// 0: TTE VAPA pointer, 1: -
	{
	 ld	r1, r0
	 addi	r0, r0, CTTE_ATTR_OFFSET - CTTE_VAPA_OFFSET
	}
	// 0: TTE attr pointer, 1: TTE VAPA@PA
	{
	 ld	r0, r0
	}
	// 0: TTE attr, 1: TTE VAPA@PA
	{
	 mtspr	xTLB_CURRENT_PA, r1
	 bfexts r1, r1, CTTE_VAPA_VA_FIELD
	}
	// 0: TTE attr, 1: TTE VAPA@VA
	{
	 mtspr	xTLB_CURRENT_VA, r1
	 bfextu	r1, r0, SPR_DTLB_CURRENT_ATTR__PS_FIELD
	}
	// 0: TTE attr, 1: extracted TTE attr page size bits
	{
	 shli	r1, r1, SPR_DTLB_CURRENT_ATTR__PS_SHIFT
	}
	// 0: TTE attr, 1: TTE attr page size bits only
	{
	 mtspr	xTLB_CURRENT_ATTR, r1
	}
	// 0: TTE attr, 1: -
	{
	 mtspr	xTLB_CURRENT_ATTR_FILL, r0
	}
	// 0: -, 1: -
	{
	 mfspr	r0, xTLB_CURRENT_ATTR_MATCH
	}
	// 0: Was TLB filled?, 1: -
	{
	 blbc	r0, \which_tlb&tsb_miss
	}
	// 0: -, 1: -
	{
	 mfspr	r0, SYSTEM_SAVE_HV_0
	}
	// 0: client reg, 1: -
	{
	 mfspr	r1, SYSTEM_SAVE_HV_1
	}
	// 0: client reg, 1: client reg
	{
	 iret
	}
	.size \which_tlb&tlb_probe_j,.-\which_tlb&tlb_probe_j
	.endm


	//
	// TLB access violation handler.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	// which_tlb is 'd', or 'dma'.
	//
	.macro	tlb_accvio vecnum, vecname, which_tlb

	//
	// Make our SPR numbers local, to prevent definitions in one macro
	// invocation from affecting others
	//
	local xTLB_BAD_ADDR, TSB_x

	//
	// Set the SPRs we'll use, based on which TLB we're handling
	//
	.ifeqs	"\which_tlb", "d"
	xTLB_BAD_ADDR = SPR_DTLB_BAD_ADDR
	TSB_x =  TSB_D
	PL_REG = EX_CONTEXT_HV_1
	.endif

	vec_start \vecnum, \vecname

	//
	// We really don't do much here, but we save 4 registers anyway so
	// that we can use common code to jump into the C routine.
	// FIXME: this could be made a bit faster.
	//
\which_tlb&tlb_accvio:
	// 0: client reg, 1: client reg, 2: client reg, 3: client reg
	{
	 mtspr	SYSTEM_SAVE_HV_3, r3
	 moveli	r3, hw2_last(tsb_access)
	}
	// 0: client reg, 1: client reg, 2: client reg, 3: half of tsb_access
	{
	 mtspr	SYSTEM_SAVE_HV_0, r0
	 shl16insli r3, r3, hw1(tsb_access)
	}
	// 0: -, 1: client reg, 2: client reg, 3: tsb_access addr
	{
	 mfspr	r0, xTLB_BAD_ADDR
	 shl16insli r3, r3, hw0(tsb_access)
	}
	// 0: fault addr, 1: client reg, 2: client reg, 3: tsb_access addr
	{
	 mtspr	SYSTEM_SAVE_HV_1, r1
	}
	// 0: fault addr, 1: -, 2: client reg, 3: tsb_access addr

	patch_insert_after stats
	//
	// If interrupt stats are on, we need to stuff the cycle count
	// and a pointer to the DTLB_ACCESS stats bucket on the stack.
	// To do that, we need to compute where the stack will be, and
	// once we've done that it's silly for the code we call to do it
	// too, so we leave the sp in r2.
	//
	mtspr	SYSTEM_SAVE_HV_2, r2
	get_stack r2, 16

	stats_enter r1, r2, intr_stats, INT_DTLB_ACCESS, 0
	patch_end

	// 0: fault addr, 1: -, 2: client reg/sp, 3: tsb_access addr
	{
	 mfspr	r1, PL_REG
	}
	// 0: fault addr, 1: pl reg, 2: client reg/sp, 3: tsb_access addr

	patch_replace stats
	{
	 mtspr	SYSTEM_SAVE_HV_2, r2
	 andi	r1, r1, TSB_MISS_PL_MASK
	}
	patch_with
	{
	 andi	r1, r1, TSB_MISS_PL_MASK
	}
	patch_end

	// 0: fault addr, 1: miss pl, 2: -/sp, 3: tsb_access addr
	patch_replace stats
	{
	 j	call_tsb_handler
	 ori	r1, r1, (TSB_x << TSB_MISS_TYPE_SHIFT)
	}
	patch_with
	{
	 j	call_tsb_handler_with_stack
	 ori	r1, r1, (TSB_x << TSB_MISS_TYPE_SHIFT)
	}
	patch_end
	.size \which_tlb&tlb_accvio,.-\which_tlb&tlb_accvio
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

	mtspr	SYSTEM_SAVE_HV_0, r0


	.ifeq (\vecnum - INT_IDN_AVAIL) * (\vecnum - INT_IDN_TIMER)
	// As a special case, for IDN_AVAIL we must check to see if data is
	// available on idn1; if so, we jump to the fast-path interrupt
	// handler.
	mfspr	r0, IDN_DATA_AVAIL
	andi	r0, r0, SPR_IDN_DATA_AVAIL__AVAIL_1_MASK
	bnez	r0, fast_idn_intr
	.endif


	get_stack r0, 16

	// Push the parameter-passing registers on the stack, so that we can
	// set them to vector-unique values before we jump to the common code.
	// Registers are pushed on the stack in this order:
	//
	// (untouched)		 <- initial stack pointer value, highest
	//                          address
	// Padding to make the final stack pointer value 16-byte aligned
	// cycle counter       (only if STATS enabled)
	// statistics pointer  (only if STATS enabled)
	// sp
	// lr

	// IDN_AVAIL_EN

	// r0
	// r1
	// r2
	// .
	// .
	// .
	// r11			 <- last register saved by this macro
	// ------------------------------------------------------------------
	// r12			 <- first register saved by handle_interrupt;
 	//			    sp is here when we jump to handle_interrupt
	// .
	// .
	// .
	// r29
	// EX_CONTEXT_HV_1
	// EX_CONTEXT_HV_0



	// Interrupt mask
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address

	patch_insert_before stats
	stats_enter r1, r0, intr_stats, \vecnum, 0, SYSTEM_SAVE_HV_1
	patch_end

	push_reg sp, r0

	push_reg lr, r0

	mfspr	lr, IDN_AVAIL_EN
	.ifeq (\vecnum - INT_IDN_AVAIL) * (\vecnum - INT_IDN_TIMER)
	{
	 st	r0, lr
	 andi	lr, lr, ~SPR_IDN_AVAIL_EN__EN_0_MASK
	 addi	r0, r0, -8
	}
	mtspr	IDN_AVAIL_EN, lr
	.else
	push_reg lr, r0
	.endif

	{
	 mfspr	r0, SYSTEM_SAVE_HV_0    // Original r0
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

	//
	// We pass one parameter to the eventual C routine: the number of the
	// interrupt, in r0.  (handle_interrupt may pass more values in
	// addition to those.)  We also pass two values to handle_interrupt
	// itself:
	//
	// - The address of the C routine to jump to (in r10).
	//
	// - A set of interrupts we want masked while we're in the C routine
	//   and not in an interrupt critical section (r11).  We always mask
	//   the interrupt we're handling; in addition we mask a set of
	//   standard things which we never want to handle at our PL.
	//


	.ifeq (\vecnum - INT_IDN_AVAIL) * (\vecnum - INT_IDN_TIMER)
	// In the IDN_AVAIL interrupt, we've already disabled interrupts on
	// idn0 above, and we want to continue to enable interrupts on idn1.
	// However, we need to block IDN_TIMER interrupts.  Otherwise, in some
	// odd situations (e.g., waiting for an MMIO read to complete), we
	// could be delayed in the IDN_AVAIL handler long enough for the
	// IDN_TIMER interrupt to go off, and end up in an endless loop
	// reexecuting the IDN_TIMER handler.  We've done an in-place expansion
	// of the int_mask macro, because that allows us to coschedule with
	// other instructions and thus fit in the vector.
	local ints_to_block
	ints_to_block = HV_INTS_HANDLED_SV | INT_MASK(INT_IDN_TIMER)
	{
	 moveli	r0, \vecnum
	 moveli r11, hw2_last(ints_to_block)
	}
	{
	 moveli	r10, hw2_last(\c_routine)
	 shl16insli r11, r11, hw1(ints_to_block)
	}
	{
	 shl16insli r10, r10, hw1(\c_routine)
	 shl16insli r11, r11, hw0(ints_to_block)
	}

	.else

	{
	 moveli	r0, \vecnum
	 moveli	r10, hw2_last(\c_routine)
	}
	shl16insli r10, r10, hw1(\c_routine)

	int_mask \vecnum, r11


	.endif


	// Jump to remainder of interrupt handling code.

	{
	 j	handle_interrupt
	 shl16insli r10, r10, hw0(\c_routine)
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
	.macro	int_hand_full vecnum, vecname, c_routine

	vec_start \vecnum, \vecname

	.global intvec_\vecname
intvec_\vecname:
	// Temporarily save a register so we have somewhere to work.

	mtspr	SYSTEM_SAVE_HV_0, r0

#ifdef QUIESCE_ON_MEM_ERROR
	.ifeq \vecnum - INT_MEM_ERROR
	//
	// Trigger broadcast wire #0, to quiesce the chip.
	//
	movei	r0, 1 << SPR_DIAG_BCST_TRIGGER__LEVEL_SHIFT
	mtspr	DIAG_BCST_TRIGGER, r0
	nap
	.endif
#endif

#ifdef QUIESCE_ON_ILL
	.ifeq \vecnum - INT_ILL
	//
	// Trigger broadcast wire #0, to quiesce the chip.
	//
	movei	r0, 1 << SPR_DIAG_BCST_TRIGGER__LEVEL_SHIFT
	mtspr	DIAG_BCST_TRIGGER, r0
	nap
	.endif
#endif

	.ifeq \vecnum - INT_UNALIGN_DATA
	// For UNALIGN_DATA, we clear the AAR, since we currently do
	// MMIO accesses in physical memory mode.  Specifying a unaligned
	// MMIO address thus causes us to take an unmaskable interrupt,
	// and if we didn't clear the AAR the handler would try to use
	// MMIO space for the stack.
	mtspr   AAR, zero
	.endif

















	.ifeq \vecnum - INT_DOUBLE_FAULT
	// For a double fault, we don't trust the current sp; its being
	// hosed is a great way to get here.  We always use the dedicated
	// double-fault stack area.
	moveli	r0, hw2_last(_dblflt_stack - 16)
	shl16insli r0, r0, hw1(_dblflt_stack - 16)
	shl16insli r0, r0, hw0(_dblflt_stack - 16)
	.else
	get_stack r0, 16
	.endif

	// Push the parameter-passing registers on the stack, so that we can
	// set them to vector-unique values before we jump to the common code.
	// Registers are pushed on the stack in this order:
	//
	// (untouched)	      <- initial stack pointer value, highest
	//                       address
	// Padding to make the final stack pointer value 16-byte aligned
	// cycle counter       (only if STATS enabled)
	// statistics pointer  (only if STATS enabled)
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
	// EX_CONTEXT_HV_1
	// EX_CONTEXT_HV_0



	// Interrupt mask
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address

	patch_insert_before stats
	stats_enter r1, r0, intr_stats, \vecnum, 0, SYSTEM_SAVE_HV_1
	patch_end

	push_reg sp, r0

	push_reg lr, r0
	{
	 mfspr	r0, SYSTEM_SAVE_HV_0    // Original r0
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
	// We pass one parameter to the eventual C routine: the number of the
	// interrupt, in r0.  (handle_intr_full may pass more values in
	// addition to those.)  We also pass two values to handle_intr_full
	// itself:
	//
	// - The address of the C routine to jump to (in r10).
	//
	// - A set of interrupts we want masked while we're in the C routine
	//   and not in an interrupt critical section (in r11).  We always mask
	//   the interrupt we're handling; in addition we mask a set of
	//   standard things which we never want to handle at our PL.
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
	// System call interrupt handler, part 1.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector to use.
	//
	.macro	syscall_hand vecnum, vecname

	vec_start \vecnum, \vecname

intvec_\vecname:
	//
	// The hypervisor processes four different types of swint2 calls; we
	// use a slightly different format in the syscall register for each.
	//
	// - Normal syscalls which are handled by hypervisor C code,
	//   accessed through the syscall dispatch.  The syscall number
	//   here is positive, and the HV_SYS_FAST bit in the number is off.
	//
	// - Fast syscalls which are handled by hypervisor assembly code
	//   in this file.  The syscall number here is positive, and the
	//   HV_SYS_FAST bit in the number is on.
	//
	// - The downcall_dispatch service is a unique case.  It's handled
	//   directly by hypervisor assembly code, but it has an unusual
	//   calling sequence, so it's handled specially.  The syscall
	//   number here is zero.
	//
	// - Fast I/O traps which are handled by hypervisor device
	//   driver routines accessed through the fastio dispatch table.
	//   The syscall number here is negative, and the HV_SYS_FAST
	//   bit in the number is off.
	//
	// In addition, some syscalls can be invoked with the
	// HV_SYS_GUEST_MASK bit set, in which case we "approve" a small
	// set of syscalls even if called from PL1 instead of PL2.

	// Check to see if this is the downcall_dispatch syscall; if so,
	// we're not going to C, and will handle it separately.  In the
	// process, rotate the syscall register so that the fast syscall
	// bit is at the bottom, so we can test it in the next bundle.

	{
	 beqz	TREG_SYSCALL_NR_NAME, handle_downcall_dispatch
	 rotli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		64 - HV_SYS_FAST_SHIFT
	}

	// See if this is a fast syscall (one implemented in assembly,
	// right in this file) by testing the flag bit (now the low bit),
	// and if so go to the handlers for those.  While we're at it,
	// rotate the syscall number so that the HV_SYS_FAST_PL0 flag is
        // in the low bit.

	{
	 blbs	TREG_SYSCALL_NR_NAME, handle_fast_syscall
	 rotli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
                HV_SYS_FAST_SHIFT - HV_SYS_FAST_PLO_SHIFT
	}

	// Point to the base of the interrupt stack.  (We assume we won't
	// be gettting any swint calls from our own PL, so we never try
	// to just re-use the current sp.)  Note that we don't save many
	// registers here; we assume that the client called the hypervisor
	// glue routine (which issued the swint) using the normal C calling
	// conventions, so we can use any of the caller-saved registers.
	// We only need to save sp, lr, the two EX_CONTEXT registers (because
	// we could potentially take another interrupt, like a TLB miss,
	// while we're handling the syscall), and the current interrupt mask.
	// Registers are pushed on the stack in this order:
	//
	// (untouched)		<- top of interrupt stack, highest address
	// Padding to make the final stack pointer value 16-byte aligned
	// cycle counter       (only if STATS enabled)
	// rotated syscall #   (only if STATS enabled)
	// sp
	// lr
	// EX_CONTEXT_HV_1
	// EX_CONTEXT_HV_0
	//
	// (Note that the stack pointer points at the next word if we call
	// handle_fastio; subsequent items are not pushed in that case.)
	//



	// Interrupt mask
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address
	//
	// If this stack layout is changed, fix	syscall_install_context, below.

	// Stash sp temporarily.  Also start setting up r27 in case we end up
	// doing a fast I/O trap.

	{
	 move	r29, sp
	 moveli	r27, hw2_last(fastio_table)
	}
	{
	 moveli	sp, hw2_last(_estack - 16)
	 moveli	r20, hw2_last(syscall_table)
	}
	{
	 shl16insli sp, sp, hw1(_estack - 16)
	 shl16insli r20, r20, hw1(syscall_table)
	}
	{
	 shl16insli r20, r20, hw0(syscall_table)
	 shl16insli sp, sp, hw0(_estack - 16)
	}

	patch_insert_after stats
	stats_enter r19, sp, , , 1
	patch_end

        // This bundle is a push_reg of r29 (the original sp), plus
        // restoring the syscall number to its original value.
	{
	 st	sp, r29
	 addi	sp, sp, -8
	 rotli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
                HV_SYS_FAST_PLO_SHIFT
	}
	push_reg lr
	{
	 mfspr	r22, EX_CONTEXT_HV_1
	 bfextu	r26, TREG_SYSCALL_NR_NAME, 0, FASTIO_INDEX_WIDTH - 1
	}
	{
	// Set r28 nonzero if we came from CLIENT_PL.  Doing an AND here works
	// as long as CLIENT_PL is 1 or 2, and we don't ever come here from
	// anything higher than CLIENT_PL; both of those are expected to be
	// true for any reasonable configuration, but we check just to be
	// sure.
#if CLIENT_PL != 2 && CLIENT_PL != 1
#error Code here assumes CLIENT_PL is 1 or 2, must fix
#endif
	 andi	r28, r22, CLIENT_PL
	 // Continue preparing for fast I/O
	 shl16insli r27, r27, hw1(fastio_table)
	}
	push_reg r22
	{
	 mfspr	r21, EX_CONTEXT_HV_0
	 bfextu	r25, r26, FASTIO_USER_SHIFT, FASTIO_USER_SHIFT
	}
	// Bump the saved PC by one bundle so that when we return, we won't
	// execute the same swint instruction again.  We need to do this while
	// we're still in the critical section.  Also set r25 in anticipation
	// of a potential fast I/O call; if we end up doing one, r25 is nonzero
	// if this is a permitted call (either it's a user-accessible index or
	// we came from CLIENT_PL).
	{
	 addi	r21, r21, BUNDLE_SIZE
	 or	r25, r25, r28
	}
	push_reg r21
	//
	// Handle fast I/O requests.  Note that we do this test before we've
	// put the interrupt masks on the stack.  We do actually need to mask
	// INTCTRL_HV; otherwise, we could potentially run msg_proc(), which
	// will write to the IODN, while a fastio handler is in the middle of
	// sending an IODN message.  However, because chip-0/chip-1 interrupt
	// mask accesses are so slow, we've historically avoided doing the full
	// interrupt masking thing; instead, we're going to just set and reset
	// the INTCTRL_HV bit in one mask register explicitly.  This isn't as
	// much of a big deal on tilegx but we don't expect to use a lot of
	// fast I/O requests there anyway.  Note that because of this, we may
	// take the other handle-by-HV/in-SV interrupts as if we were still in
	// the supervisor, but since a fastio handler isn't allowed to do
	// anything to most hypervisor data structures this shouldn't pose a
	// problem.
	//
	// When we get there, r27 will be the base of the fast I/O table, r26
	// will be the fast I/O index, and r25 will be nonzero if the call
	// should be permitted.
	//
	{
	 bltz	TREG_SYSCALL_NR_NAME, handle_fastio
	 shl16insli r27, r27, hw0(fastio_table)
	}













	// Save current interrupt mask on the stack

	{
	 mfspr	r21, INTERRUPT_MASK_HV
	 shrui	r25, TREG_SYSCALL_NR_NAME, HV_SYS_GUEST_SHIFT
	}
        {
	 st	sp, r21	// Interrupt mask word
	 addi	sp, sp, -C_ABI_SAVE_AREA_SIZE // Caller state save area
        }

	// Then mask the standard set of interrupts

	int_mask -1, r21
	mtspr	INTERRUPT_MASK_SET_HV, r21
	blbs	r25, handle_guest_syscall

	// Now check to make sure we didn't come from something below
	// CLIENT_PL; if we did, just return without doing anything
	beqz	r28, 0f

guest_syscall_ok:
	// Now that our sp is valid, we can enable further interrupts, and
	// then get the address of the function we're going to call.  We
	// ensure that the syscall number is within the legal range, and
	// that the function pointer we retrieve from the table is non-NULL.
	{
	 mtspr	INTERRUPT_CRITICAL_SECTION, zero
	 cmpltui r21, TREG_SYSCALL_NR_NAME, HV_MAX_SYSCALL + 1
	}
	{
	 blbc	r21, invalid_syscall_panic
	 shl3add r20, TREG_SYSCALL_NR_NAME, r20
	}
	ld	r20, r20
	beqz	r20, unimp_syscall_panic

	// Jump to syscall handler; on return, jump to return-from-syscall
	// code (which won't all fit in the interrupt vector).

	jalr	r20

0:
	j	return_from_syscall
	.size intvec_\vecname,.-intvec_\vecname
	.endm

	//
	// Macros to build the syscall table, used below.
	//
	.macro	syscall number routine

	.org	syscall_table + ((\number) * 8)
	.quad	syscall_\routine

	.pushsection .rodata, "a"
syscallname_\routine:
	.asciz "\routine"
	.popsection

	.pushsection .rodata.syscall_names, "a"
	.org	(\number << 3)
	.quad	syscallname_\routine
	.popsection

	.endm


	//
	// Fast IDN interrupt handler.
	//
fast_idn_intr:
	//
	// In order to get to the driver's interrupt handler as soon as
	// possible, we don't set up any environment to speak of.  We save
	// r0-r3, and retrieve the appropriate arguments for the handler
	// from the table, but that's it.  In particular, we don't set up
	// a stack or save lr, so the handler can't call anything, and we
	// don't exit the interrupt critical section, so the handler can't
	// take any other interrupts, including TLB misses.  (A version of
	// this vector which does all of the proper register saving and SPR
	// configuration to do all of that extra stuff takes about 50 cycles
	// longer than this one.)
	//

	// Note that the original r0 was already saved in SYSTEM_SAVE_HV_0 by
	// our caller (the int_hand macro).
	{
	 moveli	r0, hw2_last(instant_intr_table)
	 mtspr	SYSTEM_SAVE_HV_1, r1
	}
	{
	 shl16insli r0, r0, hw1(instant_intr_table)
	 move	r1, idn1		// Second arg is the first idn word
	}
	{
	 mtspr	SYSTEM_SAVE_HV_3, r3
	 andi	r3, r1, DRV_CHAN_RMASK	// Extract the channel number
	}
	{
	 shl16insli r0, r0, hw0(instant_intr_table)
	 shli	r3, r3, 4 		// Elements in table are 16 bytes long
	}
	{
	 mtspr	SYSTEM_SAVE_HV_2, r2
	 add	r0, r3, r0
	}
	{
	 ld	r2, r0			// Get address to jump to
	 addi	r0, r0, 8
	}
	ld	r0, r0			// Get first arg from table
	//
	// Jump to the handler.  Note that we don't check to make sure it's
	// nonzero; to save the cycle for the branch, we require that unused
	// entries in the table are stuffed with valid addresses.
	//
	jr	r2
	.size fast_idn_intr,.-fast_idn_intr


	//
	// There's no particular reason this return-from-interrupt code needs
	// to live in the vector, but it fits, and this probably makes it
	// slightly more likely that we won't have to take a cache miss on
	// it, so here it is.  This would have to be moved if we ever invoked
	// this macro more than once.
	//
	.global	drv_intr_exit
drv_intr_exit:
	mfspr	r0, SYSTEM_SAVE_HV_0
	mfspr	r1, SYSTEM_SAVE_HV_1
	mfspr	r2, SYSTEM_SAVE_HV_2
	mfspr	r3, SYSTEM_SAVE_HV_3
	iret
	.size drv_intr_exit,.-drv_intr_exit

	.global	drv_intr_again
drv_intr_again:
	//
	// We jump to this label when the driver interrupt handler has
	// verified that there's more data on idn1.  It's already read the
	// first word of the next interrupt into r1.
	//
	moveli	r0, hw2_last(instant_intr_table)
	{
	 shl16insli r0, r0, hw1(instant_intr_table)
	 andi	r3, r1, DRV_CHAN_RMASK	// Extract the channel number
	}
	{
	 shli	r3, r3, 4		// Elements in table are 8 bytes long
	 shl16insli r0, r0, hw0(instant_intr_table)
	}
	add	r0, r3, r0
	{
	 ld	r2, r0			// Get address to jump to
	 addi	r0, r0, 8
	}
	ld	r0, r0			// Get first arg from table
	//
	// Jump to the handler.  Note that we don't check to make sure it's
	// nonzero; to save the cycle for the branch, we require that unused
	// entries in the table are stuffed with valid addresses.
	//
	jr	r2
	.size drv_intr_again,.-drv_intr_again

//
// These stub routines overwrite the saved ex0 with the saved lr
// to make it easier to return to a completely changed VA mapping.
//
	.text



	syscall_stack_offset = 0

	.global syscall_install_context
syscall_install_context:
	addi	r11, sp, 40 + syscall_stack_offset
	{
	  ld	r12, r11
	  addi	r11, sp, 24 + syscall_stack_offset
	}
	st	r11, r12
	j	do_install_context
	.size	syscall_install_context,.-syscall_install_context

	.global syscall_install_guest_context
syscall_install_guest_context:
	addi	r11, sp, 40 + syscall_stack_offset
	{
	  ld	r12, r11
	  addi	r11, sp, 24 + syscall_stack_offset
	}
	st	r11, r12
	j	do_install_guest_context
	.size	syscall_install_guest_context,.-syscall_install_guest_context
	
//
// Done with macro definitions, here are our vectors
//
	.section .intrpt, "ax"

#ifdef NO_TLB_FAST_PATH  // Disable fast-path TLB handling if requested.
#define tlb_miss_1 tlb_miss_null
#endif

	int_hand_full INT_MEM_ERROR, MEM_ERROR, bad_intr
	int_hand_full INT_SINGLE_STEP_3, SINGLE_STEP_3, bad_intr
	int_hand_full INT_SINGLE_STEP_2, SINGLE_STEP_2, bad_intr
	int_hand_full INT_SINGLE_STEP_1, SINGLE_STEP_1, bad_intr
	int_hand_full INT_SINGLE_STEP_0, SINGLE_STEP_0, bad_intr
	int_hand_full INT_IDN_COMPLETE, IDN_COMPLETE, bad_intr
	int_hand_full INT_UDN_COMPLETE, UDN_COMPLETE, bad_intr
	tlb_miss_1    INT_ITLB_MISS, ITLB_MISS, i
	int_hand_full INT_ILL, ILL, bad_intr
	int_hand_full INT_GPV, GPV, bad_intr
	int_hand_full INT_IDN_ACCESS, IDN_ACCESS, bad_intr
	int_hand_full INT_UDN_ACCESS, UDN_ACCESS, bad_intr
	int_hand_full INT_SWINT_3, SWINT_3, bad_intr // was hvgdb_init_tlbs
	syscall_hand  INT_SWINT_2, SWINT_2
	int_hand_full INT_SWINT_1, SWINT_1, bad_intr
	int_hand_full INT_SWINT_0, SWINT_0, bad_intr
	int_hand_full INT_ILL_TRANS, ILL_TRANS, bad_intr
	int_hand_full INT_UNALIGN_DATA, UNALIGN_DATA, bad_intr
	tlb_miss_1    INT_DTLB_MISS, DTLB_MISS, d
	tlb_accvio    INT_DTLB_ACCESS, DTLB_ACCESS, d
	int_hand_full INT_IDN_FIREWALL, IDN_FIREWALL, bad_intr
	int_hand_full INT_UDN_FIREWALL, UDN_FIREWALL, bad_intr
	int_hand_full INT_TILE_TIMER, TILE_TIMER, bad_intr



	int_hand      INT_IDN_TIMER, IDN_TIMER, msg_avail

	int_hand_full INT_UDN_TIMER, UDN_TIMER, bad_intr



	int_hand      INT_IDN_AVAIL, IDN_AVAIL, msg_avail

	int_hand_full INT_UDN_AVAIL, UDN_AVAIL, bad_intr
#if HV_PL == 3
	int_hand_full INT_IPI_3, IPI_3, ipi_proc
	int_hand_full INT_IPI_2, IPI_2, bad_intr
#else
	int_hand_full INT_IPI_3, IPI_3, bad_intr
	int_hand_full INT_IPI_2, IPI_2, ipi_proc
#endif
	int_hand_full INT_IPI_1, IPI_1, bad_intr
	int_hand_full INT_IPI_0, IPI_0, bad_intr
	int_hand_full INT_PERF_COUNT, PERF_COUNT, bad_intr
	int_hand_full INT_AUX_PERF_COUNT, AUX_PERF_COUNT, bad_intr
#if HV_PL == 3
	int_hand      INT_INTCTRL_3, INTCTRL_3, msg_proc
	int_hand_full INT_INTCTRL_2, INTCTRL_2, bad_intr
#else
	int_hand_full INT_INTCTRL_3, INTCTRL_3, bad_intr // was hvgdb_init_tlbs
	int_hand      INT_INTCTRL_2, INTCTRL_2, msg_proc
#endif
	int_hand_full INT_INTCTRL_1, INTCTRL_1, bad_intr
	int_hand_full INT_INTCTRL_0, INTCTRL_0, bad_intr
	int_hand_full INT_BOOT_ACCESS, BOOT_ACCESS, bad_intr
	int_hand_full INT_WORLD_ACCESS, WORLD_ACCESS, bad_intr
	tlb_miss_2    INT_I_ASID, I_ASID, i
	tlb_miss_2    INT_D_ASID, D_ASID, d
	int_hand_full INT_DOUBLE_FAULT, DOUBLE_FAULT, bad_intr

//
// Routines which follow are called by the various macros instantiated above.
//

	//
	// System call interrupt handler, part 2.
	//

	// Enter interrupt critical section, restore the interrupt masks and
	// the EX_CONTEXT registers.

return_from_syscall:
	movei	r20, 1
        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, r20
         addi	sp, sp, C_ABI_SAVE_AREA_SIZE // Deallocate caller save area
        }
	pop_reg	r21
	mtspr	INTERRUPT_MASK_HV, r21




	pop_reg	r21
	mtspr	EX_CONTEXT_HV_0, r21
	pop_reg	r21
	mtspr	EX_CONTEXT_HV_1, r21

	patch_insert_after stats
	stats_exit 2, r20, r21, r22, r23, 1
	patch_end

	// Get sp and lr back from the stack.

	pop_reg	lr
	ld	sp, sp

	// Return from interrupt.
	iret


	//
	// Panic messages for the syscall handler; these don't fit in the
	// interrupt vector.
	//
invalid_syscall_panic:
	move	r1, TREG_SYSCALL_NR_NAME
	panic	"got invalid syscall #%ld"

unimp_syscall_panic:
	move	r1, TREG_SYSCALL_NR_NAME
	panic	"got unimplemented syscall #%ld"

	.pushsection	".fasttext.handle_fastio", "ax"

	//
	// Handler for fast I/O traps.  We got here through the syscall
	// handler.  On entry, r27 is the base of the fast I/O table, r26 is
	// the fast I/O index, and r25 is nonzero if the call should be
	// permitted; and the old sp/lr/ex_context_0/ex_context_1 are saved on
	// the stack.  The stack pointer is actually 8 less than it should be,
	// so we need to fix that before we try to return.
	//
handle_fastio:
	shli	r26, r26, 4   		// Elements in table are 16 bytes long
	add	r27, r26, r27
	{
	 ld	r10, r27		// Get address to jump to
	 addi	sp, sp, 8-C_ABI_SAVE_AREA_SIZE // Caller state save area, with
					// the extra sp adjustment (see above)
	 movei	r11, INT_MASK(INT_INTCTRL_HV)
	}










	{
	 mtspr	INTERRUPT_MASK_SET_HV, r11
	 addi	r27, r27, 8		// Point to arguments
	}

	mtspr	INTERRUPT_CRITICAL_SECTION, zero
	{
	 beqz	r25, 0f
	 moveli	r0, HV_EPERM
	}
	ld	r0, r27			// Get first arg from table
	//
	// Jump to the handler.  Note that we don't check to make sure it's
	// nonzero; to save the cycle for the branch, we require that unused
	// entries in the table are stuffed with no_fastio.
	//
	jalr	r10

	// Get back in the critical section and restore the ex_context
	// registers.
0:
	{
	 movei	r20, 1
	 movei	r11, INT_MASK(INT_INTCTRL_HV)
	}
	{
	 mtspr	INTERRUPT_CRITICAL_SECTION, r20
	 addi	sp, sp, C_ABI_SAVE_AREA_SIZE // Caller state save area
	}







	mtspr	INTERRUPT_MASK_RESET_HV, r11

	pop_reg	r21
	mtspr	EX_CONTEXT_HV_0, r21
	pop_reg	r21
	mtspr	EX_CONTEXT_HV_1, r21

	// Get sp and lr back from the stack.

	pop_reg	lr
	ld	sp, sp

	// Return from interrupt.
	iret

	.global no_fastio
no_fastio:
	{
	 moveli	r0, HV_ENOSYS
	 jrp	lr
	}
	.popsection














































	//
	// Common code for most interrupts.  The C function we're eventually
	// going to is in r10, and its args are in r0...r9; the original
	// values for all of those registers, plus some others, are on the
	// stack (see stack layout above in the int_hand macro).
	//
        .align	CHIP_L1I_LINE_SIZE()
handle_interrupt:
	// Save the rest of the registers that we didn't save in the actual	
	// vector itself

	push_reg r12
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
	mfspr	r29, EX_CONTEXT_HV_1
	push_reg r29
	mfspr	r29, EX_CONTEXT_HV_0
	push_reg r29












	// Save the current interrupt mask

	mfspr	r20, INTERRUPT_MASK_HV
	st	sp, r20  // Interrupt mask word

	// Then mask the requested interrupts

	mtspr	INTERRUPT_MASK_SET_HV, r11

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

	.size	handle_interrupt,.-handle_interrupt
        
	// Upon return, fall through to...

return_from_interrupt:
	// Enable critical section bit

	movei	r29, 1

        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, r29
	 addi	sp, sp, C_ABI_SAVE_AREA_SIZE
        }

	// Restore the previous interrupt mask, the EX_CONTEXT values, and
	// the registers.

	pop_reg r29
	mtspr	INTERRUPT_MASK_HV, r29




	pop_reg r29
	mtspr	EX_CONTEXT_HV_0, r29
	pop_reg r29
	mtspr	EX_CONTEXT_HV_1, r29
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
	patch_insert_after stats



	stats_exit 7, r0, r1, r2, r3, 0

	patch_end
	pop_reg r3
	pop_reg r2
	pop_reg r1
	pop_reg r0

	pop_reg lr
	mtspr	IDN_AVAIL_EN, lr

	pop_reg	lr
	ld	sp, sp

	iret
	.size	return_from_interrupt,.-return_from_interrupt


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
	mfspr	tp, EX_CONTEXT_HV_1
	push_reg tp
	mfspr	tp, EX_CONTEXT_HV_0
	push_reg tp

	// Save the current interrupt mask












	mfspr	r20, INTERRUPT_MASK_HV
	st	sp, r20  // Interrupt mask word

	// Then mask the requested interrupts

	mtspr	INTERRUPT_MASK_SET_HV, r11

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

	.size	handle_intr_full,.-handle_intr_full

	// Upon return, fall through to...

return_from_intr_full:
	// Enable critical section bit

	movei	tp, 1

        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, tp
	 addi	sp, sp, C_ABI_SAVE_AREA_SIZE
        }

	// Restore the previous interrupt mask, the EX_CONTEXT values, and
	// the registers.

	pop_reg tp
	mtspr	INTERRUPT_MASK_HV, tp




	pop_reg tp
	mtspr	EX_CONTEXT_HV_0, tp
	pop_reg tp
	mtspr	EX_CONTEXT_HV_1, tp
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
	patch_insert_after stats
	stats_exit 6, r0, r1, r2, r3, 0
	patch_end
	pop_reg r3
	pop_reg r2
	pop_reg r1
	pop_reg r0
	pop_reg lr
	ld	sp, sp

	iret

	.size	return_from_intr_full,.-return_from_intr_full


	//
	// Handle the downcall dispatch service.  On entry, the client's
	// system save register 3 holds the original contents of
	// REG_SYSCALL_NR_NAME, which we need to restore before we iret to
	// the correct interrupt vector.
	//
handle_downcall_dispatch:

	// If we were called from below CLIENT_PL, just return.

	mfspr	TREG_SYSCALL_NR_NAME, EX_CONTEXT_HV_1
	{
	 // Note that we're using the hack here of ANDing with CLIENT_PL
	 // to see if we were at that PL; see explanation above in
	 // the syscall_hand macro.
	 andi	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, CLIENT_PL
	 mtspr	SYSTEM_SAVE_HV_3, r8	 // Save one more register.
	}
	{
	 beqz	TREG_SYSCALL_NR_NAME, downcall_early_return
					// Note: doesn't restore r8, since we
					// haven't written it yet
	 moveli	TREG_SYSCALL_NR_NAME, hw2_last(downcall_info + \
					       DOWNCALL_PENDING_DOWNCALLS_OFF)
	}

	// Load the bitmap of pending downcalls.

	shl16insli TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		   hw1(downcall_info + DOWNCALL_PENDING_DOWNCALLS_OFF)
	shl16insli TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		   hw0(downcall_info + DOWNCALL_PENDING_DOWNCALLS_OFF)
	ld4u	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME

	//
	// Now define the macro we'll instantiate for each type of downcall.
	// first is non-empty if this is the first downcall, name is the
	// name of this downcall, and nextname the name of the next one
	// to check (leave empty if this is the last one).  arg0 is the
	// address of an argument to be passed to the client in its system
	// save register 2; this is optional.  (Note: in the special case
	// of name=dev_intr, it's assumed that arg0 is a pointer to
	// pending_dev_intrs, which is cleared after it's passed to the
	// client.)
	//
	.macro	downcall_hnd	first=NOTFIRST, name, nextname=NONE, arg0=NONE

check_for_\name&_downcall:
	// Check to see if this the right downcall.
	.ifnc	\first, NOTFIRST
	andi	r8, TREG_SYSCALL_NR_NAME, downcall_\name
	.endif
	{
	.ifc	\nextname, NONE
	 beqz	r8, downcall_return
	.else
	 beqz	r8, check_for_\nextname&_downcall
	 andi	r8, TREG_SYSCALL_NR_NAME, downcall_\nextname
	.endif
	}

\name&_downcall:
	{
	 andi	r8, TREG_SYSCALL_NR_NAME, ~downcall_\name
	 moveli	TREG_SYSCALL_NR_NAME, hw2_last(downcall_info + \
					       DOWNCALL_PENDING_DOWNCALLS_OFF)
	}
	shl16insli TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		   hw1(downcall_info + DOWNCALL_PENDING_DOWNCALLS_OFF)
	// If this was the last pending downcall, clear the downcall interrupt.
	// Either way, clear it from the set of pending downcalls.
	{
	 bnez	r8, 0f
	 shl16insli TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		    hw0(downcall_info + DOWNCALL_PENDING_DOWNCALLS_OFF)
	}
	mtspr	INTCTRL_CL_STATUS, zero
0:
	{
	 st4	TREG_SYSCALL_NR_NAME, r8
	 movei	r8, 1
	}
	{
	 shli	r8, r8, INT_INTCTRL_CL
	 mfspr	TREG_SYSCALL_NR_NAME, INTERRUPT_VECTOR_BASE_CL
	}

	// Mask the downcall interrupt.
	{
	 mtspr	INTERRUPT_MASK_SET_CL, r8
	 addli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
	 	int_\name&_dwncl << 8
	}

	// Set the PC.
	{
	 mtspr	EX_CONTEXT_HV_0, TREG_SYSCALL_NR_NAME
	.ifnc	\arg0, NONE
	 moveli	r8, hw2_last(\arg0)
	.endif
	}
	{
	 moveli	TREG_SYSCALL_NR_NAME, CLIENT_PL | SPR_EX_CONTEXT_1_1__ICS_MASK
	.ifc	\arg0, NONE
	 mfspr	r8, SYSTEM_SAVE_HV_3
	.else
	 shl16insli r8, r8, hw1(\arg0)
	.endif
	}

	// Set PL and ICS.
	{
	 mtspr	EX_CONTEXT_HV_1, TREG_SYSCALL_NR_NAME
	.ifnc	\arg0, NONE
	 shl16insli r8, r8, hw0(\arg0)
	.endif
	}

	// Pass data to receiving interrupt handler, then restore registers.

	.ifnc	\arg0, NONE
	ld4u	r8, r8
	.endif

	mfspr	TREG_SYSCALL_NR_NAME, SYSTEM_SAVE_CL_3

	.ifnc	\arg0, NONE
	mtspr	SYSTEM_SAVE_CL_3, r8
	mfspr	r8, SYSTEM_SAVE_HV_3
	.endif

	// Return to new vector.
	iret
	.endm

	//
	// Define some symbols so that the macro can use the #defines.
	//
	downcall_message_rcv    = DOWNCALL_MESSAGE_RCV
	downcall_dev_intr       = DOWNCALL_DEV_INTR
	int_message_rcv_dwncl   = INT_MESSAGE_RCV_DWNCL
	int_dev_intr_dwncl      = INT_DEV_INTR_DWNCL

	//
	// Now we process each type of downcall.
	//
	downcall_hnd	FIRST, message_rcv

downcall_return:
	// We didn't find a downcall to dispatch.  This should never 
	// happen on Gx, but for consistency with earlier chips we
	// just clear the downcall and return to the glue.
	mtspr	INTCTRL_CL_STATUS, zero
	mfspr	r8, SYSTEM_SAVE_HV_3

downcall_early_return:
	// Advance the PC before returning.
	mfspr	TREG_SYSCALL_NR_NAME, EX_CONTEXT_HV_0
	addi	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, BUNDLE_SIZE
	mtspr	EX_CONTEXT_HV_0, TREG_SYSCALL_NR_NAME
	mfspr	TREG_SYSCALL_NR_NAME, SYSTEM_SAVE_CL_3
	iret

//
// Call a TSB miss or TLB fault handler, written in C, and take appropriate
// action afterwards.
//
// On entry to this routine:
//
//   The address of the handler is contained in r3.
//
//   Parameters to the handler may be placed in r0 and r1.
//
//   If STATS is enabled, r2 points to the top of the stack, onto which
//   the cycle counter and statistics pointer have already been pushed.
//   Otherwise, its value is immaterial; either way, r2 will be destroyed
//   before the miss handler is called.
//
//   System save registers 0 - 3 are assumed to contain the original values of
//   r0 - r3 which should be restored before returning to the faulting code.
//
//   The interrupt critical section bit must be set.
//
// The handler is called with four parameters.  The first two are the values
// which were placed in r0 and r1 by this routine's caller.  The third is the
// value of EX_CONTEXT_HV_0 (i.e., the VA of the faulting instruction bundle).
// The fourth is a state pointer which must be supplied to the tsb_downcall()
// or tsb_fatal() routines if they are called by the handler.  The state
// pointer actually points to two words which are reserved at the base of the
// stack that's passed to the handler, one of which is initialized to zero;
// tsb_downcall() modifies this value when called, and we check it upon the
// handler's return to see if we need to do a downcall or just return to
// the client normally.
//
// If the miss or access violation was taken care of by the called handler,
// it's expected to return; in this case, we return to the supervisor or
// user code which incurred the original fault.  Otherwise, the handler
// should call tsb_downcall() or tsb_fatal(), as appropriate, and then return.
// (As it happens, tsb_fatal() does not return to the caller, but that's just
// the way things are currently implemented and not a guarantee.)
//
call_tsb_handler:
	get_stack r2, 16

	//
	// If stats are enabled, our caller already pushed the cycle counter
	// and stats pointer on the stack, and put the stack pointer in R2,
	// so we jump here instead.
	//
call_tsb_handler_with_stack:

	// Push the caller-saved registers on the stack.  Registers are saved
	// in the following order; note that we reserve two words at the base
	// of the stack for our state area.
	//
	// (untouched)		<- initial sp, highest address, 16-byte aligned
	// Padding to make the final stack pointer value 16-byte aligned
	// cycle counter       (only if STATS enabled)
	// statistics pointer  (only if STATS enabled)
	// sp
	// lr

	// Dummy word to take the place of IDN_AVAIL; we don't need to save
	// and restore it for this interrupt, but we need to leave space for
	// it on the stack so we have the same register layout as other
	// interrupts do.

	// r0
	// r1
	// r2
	// .
	// .
	// .
	// r29
	// EX_CONTEXT_HV_1
	// EX_CONTEXT_HV_0



	// Interrupt mask
	// (untouched) (second state word)
	// zero (first state word)       <- value of state pointer passed as
	//                                  argument
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value, lowest
	//                                  address
        //

	push_reg sp, r2



	// Push lr and then a dummy word; see comment above for the rationale.
	{
	  st	r2, lr
	  addi	r2, r2, -16
	}

	{
	 mfspr	r2, SYSTEM_SAVE_HV_0	// Original r0
	 move	sp, r2
	}
	push_reg r2
	mfspr	r2, SYSTEM_SAVE_HV_1	// Original r1
	push_reg r2
	mfspr	r2, SYSTEM_SAVE_HV_2	// Original r2
	push_reg r2
	mfspr	r2, SYSTEM_SAVE_HV_3	// Original r3
	push_reg r2
	push_reg r4
	push_reg r5
	push_reg r6
	push_reg r7
	push_reg r8
	push_reg r9
	push_reg r10
	push_reg r11
	push_reg r12
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
	mfspr	r2, EX_CONTEXT_HV_1
	push_reg r2
	mfspr	r2, EX_CONTEXT_HV_0	// Note this is our third parameter for
	push_reg r2			// the handler; don't touch r2 after
					// this












	// Save current interrupt mask on the stack.

	mfspr	r11, INTERRUPT_MASK_HV
	push_reg r11

	// Then mask the standard set of interrupts, and set up the state.

	int_mask -1, r11
	mtspr	INTERRUPT_MASK_SET_HV, r11
	{
	 addi	sp, sp, -8		// Skip the unchanged second state word
	}
	{
	 st	sp, zero		// Clear the first state word
	 move	r4, r3			// Pointer to our called routine
	 move	r3, sp			// State pointer for downcalls, etc.
	}

        {
	 addi	sp, sp, -C_ABI_SAVE_AREA_SIZE // Allocate caller save area
	 // Everything is saved now; exit from the critical section.
	 mtspr	INTERRUPT_CRITICAL_SECTION, zero
        }

	// Jump to the C routine
	jalr	r4

	// Done with handler, reenter the critical section.
	movei	r29, 1

        {
	 mtspr	INTERRUPT_CRITICAL_SECTION, r29
         addi	sp, sp, C_ABI_SAVE_AREA_SIZE // Deallocate caller save area
        }

	// Upon return, see whether the first state word is nonzero; if so,
	// go do the downcall.

	pop_reg	r0
	bnez	r0, tsb_do_downcall

tsb_handled:
	// We don't need to do a downcall.  Restore state and return.

	addi	sp, sp, 8	// Ditch the second state word.

	// Restore original interrupt mask.
	pop_reg r29
	mtspr	INTERRUPT_MASK_HV, r29




	// Restore original EX_CONTEXT values.
	pop_reg r29
	mtspr	EX_CONTEXT_HV_0, r29
	pop_reg r29
	mtspr	EX_CONTEXT_HV_1, r29

	// Restore the caller-saved registers.  Note that this code is also
	// used by the tsb_do_downcall case below.
tsb_restore_regs:
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
	patch_insert_after stats



	stats_exit 7, r0, r1, r2, r3, 0

	patch_end
	pop_reg r3
	pop_reg r2
	pop_reg r1



	// Pop r0 and then a dummy word; see stack layout comment above.
	{
	  ld	r0, sp
	  addi	sp, sp, 16
	}

	pop_reg lr
	ld	sp, sp

	iret

tsb_do_downcall:
	// At this point, r0 contains the following:
	// - Bit 0:    miss reason bit (1 if this is a write miss)
	// - Bit 1:    always 1 (to ensure that word is always nonzero)
	// - Bits 9-2: interrupt vector number
	// - Bit 10:   fault in client critical section flag
	// - Bit 11:   force client PL downcall
	// The sp is pointing at our second state word, which contains the
	// fault address.  ICS is on.

	pop_reg r2			// Get fault address/client argument

	// Restore the previous interrupt mask.
	pop_reg r29

	{
	 mtspr	INTERRUPT_MASK_HV, r29
	 shrui	r1, r0, 11		// Get virtualization bit to r1
	}






	// See if we need to do special processing.
	// If the fault in client critical section bit is
	// set, we pass the faulting PC to the client as an argument,
	// instead of in its EX_CONTEXT registers.
	{
#ifdef GUEST_PL
	 blbs	r1, tsb_downcall_guest
#endif
	 shrui	r1, r0, 10		// Get fault in CCS bit to r1
	}
	blbct	r1, 0f

	// This is a fault in a client critical section; add the original
	// faulting PC to the second argument we pass the client.
	// Note that this code is very similar to the GUEST_PL code below.
	pop_reg r29			// Get EX_CONTEXT_CL_0
	{
	 addi	sp, sp, 8		// Discard EX_CONTEXT_CL_1
	 andi	r1, r0, 1		// r1 now has miss reason
	}
	{
	 or	r29, r29, r1		// r29 now has miss reason and fault PC
	 j	1f
	}
0:

	// Set up the client's EX_CONTEXT so that it will return to the
	// original faulting code when _it_ irets.
	pop_reg r29
	mtspr	EX_CONTEXT_CL_0, r29
	pop_reg r29
	{
	 mtspr	EX_CONTEXT_CL_1, r29
	 andi	r29, r0, 1		// r29 now has miss reason
	}

1:
	{
	 mtspr	SYSTEM_SAVE_CL_2, r29	// Give miss reason to client
	 shli	r1, r0, 6		// r1 now has correct interrupt vector
					// in bits <15:8>
	}

	// Set up our EX_CONTEXT so that when _we_ iret, we'll end up in the
	// correct client interrupt vector.  Also, pass argument to client.

	{
	 mfspr	r0, INTERRUPT_VECTOR_BASE_CL
	 mm	r1, zero, 8, 15		// Get rid of everything but the vector;
					// now offset to vector code
	}
	{
	 mtspr	SYSTEM_SAVE_CL_3, r2	// Give fault address to client
	 add	r0, r0, r1		// r0 now client VA to jump to
	}
	{
	 mtspr	EX_CONTEXT_HV_0, r0
	 // Client PL and ICS set
	 movei	r1, CLIENT_PL | SPR_EX_CONTEXT_1_1__ICS_MASK
	}
	mtspr	EX_CONTEXT_HV_1, r1

	// Restore all of the client's registers and jump to the vector.

	j	tsb_restore_regs


#ifdef GUEST_PL
tsb_downcall_guest:
	// This is a fault in a guest critical section; add the original
	// faulting PC to the second argument we pass the guest.
	// Note that this code is very similar to the code above.
	blbct	r1, 0f
	pop_reg r29			// Get EX_CONTEXT_G_0
	{
	 addi	sp, sp, 8		// Discard EX_CONTEXT_G_1
	 andi	r1, r0, 1		// r1 now has miss reason
	}
	{
	 or	r29, r29, r1		// r29 now has miss reason and fault PC
	 j	1f
	}
0:

	// Set up the guest's EX_CONTEXT so that it will return to the
	// original faulting code when _it_ irets.
	pop_reg r29
	mtspr	EX_CONTEXT_G_0, r29
	pop_reg r29
	{
	 mtspr	EX_CONTEXT_G_1, r29
	 andi	r29, r0, 1		// r29 now has miss reason
	}

1:
	{
	 mtspr	SYSTEM_SAVE_G_2, r29	// Give miss reason to guest
	 shli	r1, r0, 6		// r1 now has correct interrupt vector
					// in bits <15:8>
	}

	// Set up our EX_CONTEXT so that when _we_ iret, we'll end up in the
	// correct guest interrupt vector.  Also, pass argument to guest.

	{
	 mfspr	r0, INTERRUPT_VECTOR_BASE_G
	 mm	r1, zero, 8, 15		// Get rid of everything but the vector;
					// now offset to vector code
	}
	{
	 mtspr	SYSTEM_SAVE_G_3, r2	// Give fault address to guest
	 add	r0, r0, r1		// r0 now guest VA to jump to
	}
	{
	 mtspr	EX_CONTEXT_HV_0, r0
	 // Guest PL and ICS set
	 movei	r1, GUEST_PL | SPR_EX_CONTEXT_1_1__ICS_MASK
	}
	mtspr	EX_CONTEXT_HV_1, r1

	// Restore all of the client's registers and jump to the vector.

	j	tsb_restore_regs
#endif


//
// The eventual TSB miss handler expects the fault address as its first
// argument, and a word holding various miss flags as its second argument,
// so we need to compute those before jumping to the common register save/jump
// code.  (That code may add other subsequent arguments.)
//
	.macro	call_tsb_hand handler, type, faultspr
	//
	// call_tsb_handler expects us to have saved 4 registers, but the
	// TLB miss handler only saved 2, so get the others squirreled away.
	//
	{
	 mtspr	SYSTEM_SAVE_HV_3, r3
	 moveli	r3, hw2_last(\handler)
	}
	{
	 mfspr	r1, EX_CONTEXT_HV_1
	 shl16insli r3, r3, hw1(\handler)
	}
	{
	 mtspr	SYSTEM_SAVE_HV_2, r2
	 andi	r1, r1, TSB_MISS_PL_MASK
	}

	patch_insert_after stats
	get_stack r2, 16

	.ifeq	\type - TSB_D
	stats_enter r0, r2, intr_stats, INT_DTLB_MISS, 0
	.else
	stats_enter r0, r2, intr_stats, INT_ITLB_MISS, 0
	.endif
	patch_end

	.ifeq	\type - TSB_D
	{
	 mfspr	r0, DTLB_BAD_ADDR_REASON
	 ori	r1, r1, (\type << TSB_MISS_TYPE_SHIFT)
	}
	{
	 bfins	r1, r0, TSB_MISS_RSN_SHIFT, \
	 	TSB_MISS_RSN_SHIFT + TSB_MISS_RSN_WIDTH - 1
	 mfspr	r0, \faultspr
	}
	.else
	{
	 mfspr	r0, \faultspr
	 ori	r1, r1, (\type << TSB_MISS_TYPE_SHIFT)
	}
	.endif

	patch_replace stats
	{
	 j	call_tsb_handler
	 shl16insli r3, r3, hw0(\handler)
	}
	patch_with
	{
	 j	call_tsb_handler_with_stack
	 shl16insli r3, r3, hw0(\handler)
	}
	patch_end
	.endm

//
// On entry to these next 2 TSB miss/access violation routines, the machine
// state is as follows:
//
// - Registers r0-r1 are available; their original contents are in the
//   system save registers.  All other registers still contain user
//   data and can't be used.
//
// - INTERRUPT_CRITICAL_SECTION is still on.
//
// The routines are generally expected to disable the relevant interrupt, if
// it's still asserted (so we can turn off ICS, possibly as part of doing a
// supervisor downcall, and not get the interrupt again) and then call the
// generic TSB handler, using the call_tsb_hand macro.
//
dtsb_miss:
	call_tsb_hand tsb_miss, TSB_D, DTLB_BAD_ADDR

itsb_miss:
	call_tsb_hand tsb_miss, TSB_I, EX_CONTEXT_HV_0

	//
	// Handler for fast syscalls.  On entry, TREG_SYSCALL_NR_NAME has
	// the syscall number rotated so that the HV_SYS_FAST_PL0 flag is
	// in the low bit.  No other registers or SPRs have been saved,
	// we don't have a stack, and we're still in an interrupt critical
	// section.  Since the client called a glue routine to get here,
	// it's OK to use the caller-saved registers.
	//
handle_fast_syscall:
	//
	// Check to make sure we didn't come from PL0, unless this syscall
	// has the (now somewhat misnamed) HV_SYS_FAST_ALLOW_PL0 flag set.
	// Note that our caller rotated TREG_SYSCALL_NR_NAME so that the
	// HV_SYS_FAST_PL0 flag is in the low bit.
	mfspr	r3, EX_CONTEXT_HV_1
	{
#if CLIENT_PL == 2
	 // Extract HV_SYS_FAST_PL0 bit from syscall number into r3.
	 bfins	r3, TREG_SYSCALL_NR_NAME, 0, 0
#elif CLIENT_PL == 1
	 // OR HV_SYS_FAST_PL0 bit and rest of syscall number into r3.
	 or     r3, TREG_SYSCALL_NR_NAME, r3
#else
#error Code assumes CLIENT_PL is 1 or 2
#endif
	}
	{
#if CLIENT_PL == 2
	 // Bit 1 indicates that we came from PL2, and bit 0 indicates that
	 // that coming from PL0/1 is okay.  Mask out all the other bits
	 // from EX_CONTEXT_HV_1.
	 andi	r3, r3, 0x3
#elif CLIENT_PL == 1
	 // Bit 0 indicates that we came from PL1 or that coming from PL0
	 // is okay.  Mask out all the other bits from EX_CONTEXT_HV_1.
	 andi	r3, r3, 0x1
#else
#error Code assumes CLIENT_PL is 1 or 2
#endif
	// Also, restore the syscall number to its original value.
	 rotli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		HV_SYS_FAST_PLO_SHIFT
	}
	{
	 beqz	r3, bad_fast_syscall
	 addli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, \
		-HV_SYS_fence_incoherent
	}
	// If this isn't fence_incoherent, it's an unknown syscall, fail.
	bnez	TREG_SYSCALL_NR_NAME, bad_fast_syscall

sys_fence_incoherent:
	fence_incoherent client_fence_incoherent_pas, 0

fast_syscall_done:
	//
	// Skip over swint2 instruction, and return success.
	//
	mfspr	r0, EX_CONTEXT_HV_0
	addi	r0, r0, BUNDLE_SIZE
	{
	 mtspr	EX_CONTEXT_HV_0, r0
	 moveli	r0, HV_OK
	}
	iret

bad_fast_syscall:
	//
	// Skip over swint2 instruction, and return an error.
	//
	mfspr	r0, EX_CONTEXT_HV_0
	addi	r0, r0, BUNDLE_SIZE
	{
	 mtspr	EX_CONTEXT_HV_0, r0
	 moveli	r0, HV_ENOSYS
	}
	iret


	// If we were called from PL1, check the list of approved syscalls
	// for a guest OS, and jump back to an appropriate place.
	// We clear out the GUEST bit before returning.
	// r22 holds the value of EX_CONTEXT_HV_1 on entry.
	// It's tempting to make this data-driven with a small table,
	// but there are few enough of these syscalls that we do it by hand.
handle_guest_syscall:
	andi	r22, r22, SPR_EX_CONTEXT_3_1__PL_MASK
	beqz	r22, return_from_syscall   // called from PL0, so fail
	{
	 addli	TREG_SYSCALL_NR_NAME, TREG_SYSCALL_NR_NAME, -HV_SYS_GUEST_MASK
	 addli	r22, TREG_SYSCALL_NR_NAME, \
		- (HV_SYS_install_guest_context | HV_SYS_GUEST_MASK)
	}
	{
	 beqz	r22, guest_syscall_ok
	 addli	r22, TREG_SYSCALL_NR_NAME, -HV_SYS_inquire_guest_context
	}
	{
	 beqz	r22, guest_syscall_ok
	 addli	r22, TREG_SYSCALL_NR_NAME, -HV_SYS_flush_asid
	}
	{
	 beqz	r22, guest_syscall_ok
	 addli	r22, TREG_SYSCALL_NR_NAME, -HV_SYS_flush_page
	}
	{
	 beqz	r22, guest_syscall_ok
	 addli	r22, TREG_SYSCALL_NR_NAME, -HV_SYS_flush_pages
	}
	{
	 beqz	r22, guest_syscall_ok
	 addli	r22, TREG_SYSCALL_NR_NAME, -HV_SYS_flush_all
	}
	beqz	r22, guest_syscall_ok
	j	return_from_syscall

//
// System call table.  Note that the entries must be ordered by their
// system call numbers (as defined in syscall.h), but it's OK if some numbers
// are skipped, or if some syscalls exist but aren't present in the table.
//
	.pushsection .rodata.syscall_table, "a"
	.align 8
syscall_table:
	syscall	HV_SYS_install_context,           install_context
	syscall	HV_SYS_sysconf,		          sysconf
	syscall	HV_SYS_get_rtc,	                  get_rtc
	syscall	HV_SYS_set_rtc,	                  set_rtc
	syscall	HV_SYS_flush_asid,	          flush_asid
	syscall	HV_SYS_flush_page,	          flush_page
	syscall	HV_SYS_flush_pages,	          flush_pages
	syscall	HV_SYS_restart,		          restart
	syscall	HV_SYS_halt,		          halt
	syscall	HV_SYS_power_off,	          power_off
	syscall	HV_SYS_inquire_physical,          inquire_physical
	syscall	HV_SYS_inquire_memory_controller, inquire_memory_controller
	syscall	HV_SYS_inquire_virtual,	          inquire_virtual
	syscall	HV_SYS_inquire_asid,	          inquire_asid
	syscall	HV_SYS_console_read_if_ready,     console_read_if_ready
	syscall	HV_SYS_console_write,	          console_write
	syscall	HV_SYS_init,		          init
	syscall	HV_SYS_inquire_topology,          inquire_topology
	syscall	HV_SYS_fs_findfile,	          fs_findfile
	syscall	HV_SYS_fs_fstat,	          fs_fstat
	syscall	HV_SYS_fs_pread,	          fs_pread
	syscall HV_SYS_physaddr_read64,           physaddr_read64
	syscall HV_SYS_physaddr_write64,          physaddr_write64
	syscall HV_SYS_get_command_line,          get_command_line
//	syscall HV_SYS_set_caching,               set_caching
	syscall HV_SYS_register_message_state,    register_message_state
	syscall HV_SYS_send_message,              send_message
	syscall HV_SYS_receive_message,           receive_message
	syscall HV_SYS_inquire_context,           inquire_context
	syscall HV_SYS_start_all_tiles,           start_all_tiles
	syscall HV_SYS_dev_open,                  dev_open
	syscall HV_SYS_dev_close,                 dev_close
	syscall HV_SYS_dev_pread,                 dev_pread
	syscall HV_SYS_dev_pwrite,                dev_pwrite
	syscall HV_SYS_dev_poll,                  dev_poll
	syscall HV_SYS_dev_poll_cancel,           dev_poll_cancel
	syscall HV_SYS_dev_preada,                dev_preada
	syscall HV_SYS_dev_pwritea,               dev_pwritea
	syscall HV_SYS_flush_remote,              flush_remote
	syscall HV_SYS_console_putc,              console_putc
	syscall HV_SYS_inquire_tiles,             inquire_tiles
	syscall HV_SYS_confstr,                   confstr
	syscall HV_SYS_reexec,                    reexec
	syscall HV_SYS_set_command_line,          set_command_line
	syscall HV_SYS_store_mapping,             store_mapping
	syscall HV_SYS_inquire_realpa,            inquire_realpa
	syscall	HV_SYS_flush_all,	          flush_all
	syscall HV_SYS_get_ipi_pte,	          get_ipi_pte
	syscall HV_SYS_set_pte_super_shift,       set_pte_super_shift
	syscall HV_SYS_set_speed,                 set_speed
	syscall	HV_SYS_install_virt_context,      install_virt_context
	syscall	HV_SYS_inquire_virt_context,      inquire_virt_context
	syscall	HV_SYS_install_guest_context,     install_guest_context
	syscall	HV_SYS_inquire_guest_context,     inquire_guest_context
	syscall	HV_SYS_console_set_ipi,           console_set_ipi
	syscall	HV_SYS_close_all_devices,         close_all_devices
	syscall	HV_SYS_send_nmi,                  send_nmi
	.popsection

	patch_fini stats
