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
// Physical memory access routines.
//

#include <arch/chip.h>

#include "asm_inc.h"
#include "hv.h"

// Temporary registers used below; need to be caller-saved registers which
// aren't used for parameter passing.

// Note that unlike the chip0/1 versions of these routines, we do not
// enter an interrupt critical section while in physical memory mode;
// instead, we disable all interrupts and then reenable them afterward.
// This is mainly because we call phys_rd64/wr64 with values supplied by
// device drivers, as part of the implementation of cfg_rd/wr.  If we're
// in an ICS, a bad value there will cause a hard-to-debug double fault,
// but if we just disable interrupts, we'll get an interrupt, either
// right away (in the case of an unmaskable condition, like
// UNALIGNED_DATA) or once we reenable interrupts (in the case of a
// maskable condition, like MEM_ERROR).

//
// We want to make sure that the read routines don't load more data than
// requested, due to the dstream prefetcher.  The risk is that we might be
// loading data from the end of page A, and prefetch data from page B into the
// cache with A's attributes.  (This is only a possibility if our small page
// size is less than 64K, since the prefetcher won't prefetch across a 64K
// boundary.)  Luckily, the prefetcher gets reset on every mtspr instruction.
// So, given that each of the read routines does several mtspr's after the
// load, all we need to do is to make sure there aren't any significant delays
// in between the execution of those instructions.  We mask interrupts anyway,
// since we don't want to be executing interrupt routines in physical memory
// mode.  The only other thing we need to do is to make sure we don't take any
// I$ misses.  Since all of the routines fit on one cacheline, we just make
// sure that they're cacheline-aligned.
//
// (Another alternative would be to just disable the dstream prefetcher during
// execution of these routines, but that would be 3 slow SPR accesses.)
//

#define TMP_MINUS1 r12
#define TMP_IMASK  r13


	.global phys_rd8
	.section .text.phys_rd8, "ax"
	.type phys_rd8,@function
	.align CHIP_L1I_LINE_SIZE()   // See prefetcher comment above
phys_rd8:			// uint8_t phys_rd8(PA physaddr,
				//                  unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r1
	nop	// Must wait for physical memory mode to become effective
	ld1u	r0, r0
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_rd8,.-phys_rd8


	.global phys_rd16
	.section .text.phys_rd16, "ax"
	.type phys_rd16,@function
	.align CHIP_L1I_LINE_SIZE()   // See prefetcher comment above
phys_rd16:			// uint16_t phys_rd16(PA physaddr,
				//                    unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r1
	nop	// Must wait for physical memory mode to become effective
	ld2u	r0, r0
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_rd16,.-phys_rd16


	.global phys_rd32
	.section .text.phys_rd32, "ax"
	.type phys_rd32,@function
	.align CHIP_L1I_LINE_SIZE()   // See prefetcher comment above
phys_rd32:			// uint32_t phys_rd32(PA physaddr,
				//                    unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
#ifdef L1BOOT
	// Note that this routine is called from cfg_rd32() in the prebooter,
	// which is running in physical memory mode, so in that case we
	// have to save/restore the AAR.
	mfspr	r2, AAR
#endif
	mtspr	AAR, r1
	nop	// Must wait for physical memory mode to become effective
	ld4u	r0, r0
#ifdef L1BOOT
	mtspr	AAR, r2
#else
	mtspr	AAR, zero
#endif
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_rd32,.-phys_rd32


	.global phys_rd64
	.section .text.phys_rd64, "ax"
	.type phys_rd64,@function
	.align CHIP_L1I_LINE_SIZE()   // See prefetcher comment above
phys_rd64:			// uint64_t phys_rd64(PA physaddr,
				//                    unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
#ifdef L1BOOT
	// Note that this routine is called from cfg_rd() in the booter,
	// which is running in physical memory mode, so in that case we
	// have to save/restore the AAR.
	mfspr	r2, AAR
#endif
	mtspr	AAR, r1
	nop	// Must wait for physical memory mode to become effective
	ld	r0, r0
#ifdef L1BOOT
	mtspr	AAR, r2
#else
	mtspr	AAR, zero
#endif
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_rd64,.-phys_rd64


	.global phys_wr8
	.section .text.phys_wr8, "ax"
	.type phys_wr8,@function
phys_wr8:			// void phys_wr8(PA physaddr, uint8_t data,
				//               unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r2
	nop	// Must wait for physical memory mode to become effective
	st1	r0, r1
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_wr8,.-phys_wr8


	.global phys_wr16
	.section .text.phys_wr16, "ax"
	.type phys_wr16,@function
phys_wr16:			// void phys_wr16(PA physaddr, uint16_t data,
				//                unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r2
	nop	// Must wait for physical memory mode to become effective
	st2	r0, r1
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_wr16,.-phys_wr16


	.global phys_wr32
	.section .text.phys_wr32, "ax"
	.type phys_wr32,@function
phys_wr32:			// void phys_wr32(PA physaddr, uint32_t data,
				//                unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
#ifdef L1BOOT
	// Note that this routine is called from cfg_wr32() in the prebooter,
	// which is running in physical memory mode, so in that case we
	// have to save/restore the AAR.
	mfspr	r3, AAR
#endif
	mtspr	AAR, r2
	nop	// Must wait for physical memory mode to become effective
	st4	r0, r1
#ifdef L1BOOT
	mtspr	AAR, r3
#else
	mtspr	AAR, zero
#endif
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_wr32,.-phys_wr32


	.global phys_wr64
	.section .text.phys_wr64, "ax"
	.type phys_wr64,@function
phys_wr64:			// void phys_wr64(PA physaddr, uint64_t data,
				//                unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
#ifdef L1BOOT
	// Note that this routine is called from cfg_wr() in the booter,
	// which is running in physical memory mode, so in that case we
	// have to save/restore the AAR.
	mfspr	r3, AAR
#endif
	mtspr	AAR, r2
	nop	// Must wait for physical memory mode to become effective
	st	r0, r1
#ifdef L1BOOT
	mtspr	AAR, r3
#else
	mtspr	AAR, zero
#endif
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_wr64,.-phys_wr64


	.global phys_double_wr64
	.section .text.phys_double_wr64, "ax"
	.type phys_double_wr64,@function
	.align CHIP_L1I_LINE_SIZE()
phys_double_wr64:		// void phys_double_wr64(PA physaddr,
                                //                       uint64_t data0,
                                //                       uint64_t data1,
				//                       unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
#ifdef L1BOOT
	// Note that this routine is called from cfg_wr() in the booter,
	// which is running in physical memory mode, so in that case we
	// have to save/restore the AAR.
	mfspr	r4, AAR
#endif
	mtspr	AAR, r3
	nop	// Must wait for physical memory mode to become effective
	//
	// The whole point of this routine is to do the following two stores
	// with a very short latency in between; to ensure this, they must
	// be on the same I$ line.  The .align at the start of the routine,
	// and the number of bundles between here and there, guarantees this.
	//
	st	r0, r1
	nop
	st	r0, r2
#ifdef L1BOOT
	mtspr	AAR, r4
#else
	mtspr	AAR, zero
#endif
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_double_wr64,.-phys_double_wr64


	.global phys_finv_wr64
	.section .text.phys_finv_wr64, "ax"
	.type phys_finv_wr64,@function
phys_finv_wr64:		// void phys_finv_wr64(PA physaddr, uint64_t data,
			//                     unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r2
	nop	// Must wait for physical memory mode to become effective
	finv	r0
	st	r0, r1
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_finv_wr64,.-phys_finv_wr64


	.global phys_finv
	.section .text.phys_finv, "ax"
	.type phys_finv,@function
phys_finv:		// void phys_finv(PA physaddr, unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r1
	nop	// Must wait for physical memory mode to become effective
	finv	r0
	mtspr	AAR, zero
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_finv,.-phys_finv


	.global phys_cmpexch64
	.section .text.phys_cmpexch64, "ax"
	.type phys_cmpexch64,@function
phys_cmpexch64:			// uint64_t phys_cmpexch64(PA physaddr,
                                //                         uint64_t compare,
                                //                         uint64_t write,
                                //                         unsigned long aar);
	{
	 movei	TMP_MINUS1, -1
	 mfspr	TMP_IMASK, INTERRUPT_MASK_HV
	}
	mtspr	INTERRUPT_MASK_SET_HV, TMP_MINUS1
	mtspr	AAR, r3
	//
	// FIXME: GX: We don't currently save and restore CMPEXCH_VALUE on
	// context switch, since it seems unlikely that we'll be on average
	// doing even one CMPEXCH per trip into the hypervisor.  If that
	// changes in the future -- say we start doing a lot more shared
	// memory operations -- then saving/restoring might make sense, and
	// routines like this would no longer need to do so.
	//
	mfspr	r4, CMPEXCH_VALUE
	mtspr	CMPEXCH_VALUE, r1
	cmpexch	r0, r0, r2
	mtspr	AAR, zero
	mtspr	CMPEXCH_VALUE, r4
	mtspr	INTERRUPT_MASK_HV, TMP_IMASK
	jrp	lr
	.size phys_cmpexch64,.-phys_cmpexch64
