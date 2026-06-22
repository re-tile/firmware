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
// Second part of the L1 booter; jumps to C code to do the main part of the
// boot process.  (Also the last part of the booter, which reads in the
// hypervisor and jumps to it; see below for why that's done here.)
//
// This program needs to be loaded in a range of addresses which don't
// overlap with the built-in boot code (0x0 to 0x1FF), doesn't overlap with
// the first part of the L1 booter, and which also don't overlap with the
// loaded code; otherwise we might end up kicking ourselves out of the L2
// during the loading process.  Suggested practice is to load this at a
// relatively high address, like 0x80000200.  Since the hypervisor is
// loaded at the very top of memory, and since the size of a memory is
// always a power of two, we'd have to have a > 2GB hypervisor before we
// saw any conflicts.
//

#include <arch/abi.h>
#include <arch/spr_def.h>

#include "asm_inc.h"
#include "page.h"
#include "param.h"
#include "tte.h"

	.section .text._start, "ax"
	.global _start
	.type _start,@function
_start:
#ifndef SET_DIAG_MUX
	// If we're not tracing anything, make the tile stop driving the diag
	// bus to save power.  Custom builds of the hypervisor can override
	// SET_DIAG_MUX for debugging hangs, etc.
	mtspr	DIAG_TRACE_CTL, zero

	// Since we're not tracing, allow the supervisor to make use of
	// the diag mux for watchpoints instead; the SPR can't be used
	// directly since it has boot_access MPL.  We use M_SEL to enable
	// the VA tracing mode, since that's the most useful.
# define SET_DIAG_MUX \
  (SPR_DIAG_MUX_CTL__TOP_SEL_VAL_M_C_S_N << SPR_DIAG_MUX_CTL__TOP_SEL_SHIFT) |\
  (1 << SPR_DIAG_MUX_CTL__M_SEL_SHIFT)
#endif

	// Set the DIAG_MUX_CTL.
	moveli	r10, SET_DIAG_MUX
	mtspr	DIAG_MUX_CTL, r10

#ifdef BOOT_TIMING
	movei	r40, 1
	mtspr	PASS, r40
#endif

	//
	// Put nonzero values onto the return address stack; this avoids
	// hardware errata 9997.
	//
	jal	1f
1:	jal	2f
2:	jal	3f
3:	jal	4f
4:

	//
	// Set up a stack.
	//
#define BOOT_STACK (BOOT_BASEADDR + L2_GUARANTEED_SIZE - C_ABI_SAVE_AREA_SIZE)

	moveli	sp, hw2_last(BOOT_STACK)
	shl16insli sp, sp, hw1(BOOT_STACK)
	shl16insli sp, sp, hw0(BOOT_STACK)

	// Jump to the continuation of the booter

jump_to_booter:
	jal	boot

	// The above shouldn't return, but just in case...
	j	.

	.size _start,.-_start

//
// This routine loads code from the UDN into memory, verifies its CRC,
// and then jumps to it.  (If the CRC does not match, this routine
// returns, and the caller should take appropriate action.)
//
// Note that we do not touch r0-r9; those may be set by our caller,
// and we want to pass those straight through to the code we've loaded.
//
// It would be better to actually do this in the C boot code, but
// unfortunately we can't be sure that C code won't try to use the stack
// at some random point after we've enabled physical memory mode, which
// would obviously not work.
//
// The following arguments are passed to load_and_go_net(), and are
// passed through to the called routine:
//
//  r0 Physical base address of the text segment.
//  r1 First address used by L1 boot code (for L2 flush).
//  r2 Last address used by L1 boot code (for L2 flush).
//  r3 Upper-left-hand corner tile coordinate.
//  r4 Lower-right-hand corner tile coordinate.
//  r5 Master tile coordinate.
//  r6 Rshim coordinate (~0 if not found).
//  r7 Shim present mask.
//  r8 Board flags word.
//

	.global load_and_go_net
	.type load_and_go_net,@function
load_and_go_net:

	// Turn off cache-is-RAM mode so that when we copy in code from the
	// UDN, it actually goes to memory.  Also, turn off caching in the
	// AAR, so that those stores are noncacheable; that prevents us from
	// evicting our running code from the cache, and also means that when
	// the slave tiles go to get that code or data it'll be in memory.
	// We're still in physical memory mode, though, and caching is still
	// enabled in I_AAR.

	mf
	{
	 mfspr	r29, AAR
	 movei	r27, SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE
	}
	{
	 mfspr	r28, CBOX_CACHEASRAM_CONFIG
	 move	r26, r29
	}
	{
	 mtspr	CBOX_CACHEASRAM_CONFIG, zero
	 bfins	r26, r27, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}
	mtspr	AAR, r26
	mf
	drain

	// Initialize the CRC.
	movei	r25, ~0

	// Copy data in from the UDN.

next_segment:
	{
	 // Get length.
	 move	r20, udn0
	 // Add length to CRC.
	 crc32_32 r25, r25, udn0
	}
	shrui	r19, r20, 32
	crc32_32 r25, r25, r19
	{
	 // Get address.
	 move	r21, udn0
	 // Add address to CRC.
	 crc32_32 r25, r25, udn0
	}
	shrui	r19, r21, 32
	crc32_32 r25, r25, r19

	// Add physical base address to the load address passed to us.
	// Invert the accumlated CRC to its final value, check against input,
	// and return failure if no match.  Note that we preserve the
	// accumulated CRC so we can continue adding to it.
	{
	 add	r21, r21, r0
	 nor	r26, r25, zero
	}
	bfextu	r26, r26, 0, 31
	cmpeq	r26, r26, udn0
	blbc	r26, bad_crc

	// Zero length means we're done; jump to the given address.
	beqz	r20, copy_done

segment_loop:
	// Copy data to memory, CRCing it as we go.
	{
	 move	r19, udn0
	 crc32_32 r25, r25, udn0
	}
	{
	 st	r21, r19
	 shrui	r19, r19, 32
	}
	{
	 addi	r20, r20, -1
	 crc32_32 r25, r25, r19
	}
	{
	 bgtzt	r20, segment_loop
	 addi	r21, r21, 8
	}

	// Done with that segment; go get another one.
	j	next_segment

copy_done:
#ifdef BOOT_TIMING
	movei	r20, 2
	mtspr	PASS, r20
#endif
	{
	 move	r10, r21
	 j	jump_to_hv
	}

bad_crc:
	//
	// Got a bad CRC, return.  Note that we need to re-enable caching,
	// and cache-as-RAM mode, before we return.  Nothing should have
	// hit in the cache, so we should be able to continue execution.
	//
	mf
	mtspr	CBOX_CACHEASRAM_CONFIG, r28
	mtspr	AAR, r29
	mf
	drain

	jrp	lr

	.size load_and_go_net,.-load_and_go_net

//
// This routine jumps to code in physical memory.
//
// See comment in load_and_go for why this is done here, instead of in C code.
//
// Arguments are passed as follows:
//
//  r0 Physical base address of text segment.
//  r1 First address used by L1 boot code (for L2 flush).
//  r2 Last address used by L1 boot code (for L2 flush).
//  r3 Upper-left-hand corner tile coordinate.
//  r4 Lower-right-hand corner tile coordinate.
//  r5 Master tile coordinate.
//  r6 Target function to call.
//
// The arguments are passed straight through to the code we jump to.
//

	.global go
	.type go,@function

go:
	// Turn off cache-is-RAM mode so that we can use memory; turn off
	// caching in the AAR, so we're consistent in how the code we're
	// jumping to is called.  We're still in physical memory mode, and
	// caching is still enabled in I_AAR.

	mf
	{
	 mfspr	r29, AAR
	 movei	r28, SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE
	}
	{
	 mtspr	CBOX_CACHEASRAM_CONFIG, zero
	 bfins	r29, r28, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}
	mtspr	AAR, r29
	mf
	drain

#ifdef BOOT_TIMING
	movei	r30, 2
	mtspr	PASS, r30
#endif
	{
	 move	r10, r6
	 j	jump_to_hv
	}

	.size go,.-go

	.align	64

//
// Define the registers which will be used by the SROM access routines,
// which we're about to instantiate here from an #include file.  Since
// we call those routines from C, and they don't obey the ABI, all of
// the registers must be caller-saved registers.  Most of these registers
// are destroyed by calls to the SROM access routines, and thus their use
// in the code below is inadvisable; see srom_acc_asm.h for details.
//
#define r_a24_lr		r14
#define r_srom_dev		r15
#define r_srom_shim_addr	r16
#define r_cfg_temp_0		r17
#define r_srom_temp_0		r18
#define r_srom_temp_1		r19
#define r_srom_dest_addr	r20
#define r_srom_dest_incr	r21
#define r_srom_wds		r22
#define r_srom_crc		r23
#define r_srom_wds_pass		r24
#define r_cfg_chan		r25
#define r_cfg_aar		r26
#define r_cfg_addr		r27
#define r_cfg_data		r28
#define r_srom_lr		r29

//
// Include the SROM access routines.
//
#include "srom_acc_asm.h"

//
// This routine is just a wrapper around srom_rd which allows it to be
// called from C.
//
	.section .text.early_srom_read, "ax"
	.global early_srom_read
	.type early_srom_read,@function

early_srom_read:
	{
	 move	r_srom_shim_addr, r0
	 move	r_srom_dev, r1
	}
	{
	 move	r_srom_dest_addr, r2
	 move	r_srom_wds, r3
	}
	{
	 movei	r_srom_dest_incr, 4
	 j	srom_rd
	}

	.size early_srom_read,.-early_srom_read

//
// This routine loads code from the SROM into memory, verifies its CRC, and
// then jumps to it.  (If the CRC does not match, this routine returns, and the
// caller should take appopriate action.)
//
// Note that we do not modify r0-r8; those may be set by our caller, and we
// want to pass those straight through to the code we've loaded.  (We do read
// r0 and r1, which we assume are the low and high 32 bits, respectively, of
// the base physical address of our text segment.)  We also must avoid using
// r14-r29, as they're used by the SROM access routines (see above).
//

	.global load_and_go_srom
	.type load_and_go_srom,@function
load_and_go_srom:
	//
	// Save our lr on the stack in case we need to return.  Set the address
	// of the SROM shim.  Set up a pointer to our temporary segment header
	// buffer, and the constant values we'll pass to srom_rd.  Note that
	// we recreate the temporary segment header pointer each time through
	// the loop, since we're short on registers.
	//
	{
	 st	sp, lr
	 moveli	r_srom_dest_addr, hw2_last(seg_header)
	}
	{
	 mfspr	r_srom_shim_addr, RSHIM_COORD
	 movei	r_srom_dest_incr, 4
	}
	{
	 shl16insli r_srom_dest_addr, r_srom_dest_addr, hw1(seg_header)
	 movei	r_srom_crc, 0
	}
	{
	 move	r_srom_dev, r9
	 shl16insli r_srom_dest_addr, r_srom_dest_addr, hw0(seg_header)
	}

next_segment_srom:
	//
	// Get the next segment from the SROM.
	//
	{
	 movei	r_srom_wds, 4
	 jal	srom_rd
	}

	addi	r_srom_dest_addr, r_srom_dest_addr, -16
	{
	 // Get length.
	 ld	r13, r_srom_dest_addr
	 addi	r12, r_srom_dest_addr, 8
	}

	{
	 // Get address.
	 ld	r12, r12
	 // Save CRC.
	 move	r10, r_srom_crc
	}

        // Add the HV text base address to the address
        add	r12, r12, r0

	{
	 // Read in the CRC.
	 movei	r_srom_wds, 2
	 jal	srom_rd
	}

	// Check the CRC.

	addi	r_srom_dest_addr, r_srom_dest_addr, -8
	ld	r11, r_srom_dest_addr
	cmpeq	r11, r10, r11
	blbc	r11, bad_crc_srom

	// Turn off cache-is-RAM mode so that when we copy in code from the
	// SROM, it actually goes to memory.  Also, turn off caching in the
	// AAR, so that those stores are noncacheable; that prevents us from
	// evicting our running code from the cache, and also means that when
	// the slave tiles go to get that code or data it'll be in memory.
	// We're still in physical memory mode, though, and caching is still
	// enabled in I_AAR.

	mf
	{
	 mfspr	r9, AAR
	 movei	r11, SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE
	}
	{
	 bfins	r9, r11, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	 move	r11, r9
	}
	mtspr	AAR, r9
	mfspr	r9, CBOX_CACHEASRAM_CONFIG
	mtspr	CBOX_CACHEASRAM_CONFIG, zero
	mf
	drain

	// If the length is zero, the address is our destination PC.

	beqz	r13, copy_done_srom

	// No, not zero, so load the segment and go around again.

	{
	 move	r_srom_dest_addr, r12
	 shli	r_srom_wds, r13, 1
	}
	{
	 move	r_srom_crc, r10
	 jal	srom_rd
	}

	// Turn cache-is-RAM mode and caching back on.

	mf
	{
	 mtspr	CBOX_CACHEASRAM_CONFIG, r9
	 moveli	r_srom_dest_addr, hw2_last(seg_header)
	}
	{
	 mtspr	AAR, r11
	 shl16insli r_srom_dest_addr, r_srom_dest_addr, hw1(seg_header)
	}
	mf
	drain

	{
	 j	next_segment_srom
	 shl16insli r_srom_dest_addr, r_srom_dest_addr, hw0(seg_header)
	}

copy_done_srom:
	{
	 move	r10, r12
	 j	jump_to_hv
	}

bad_crc_srom:
	//
	// Got a bad CRC, return.  Note that unlike in load_and_go_net,
	// we don't have to disable cache-as-ram mode, since we only enable
	// that while we're reading the actual data in each segment.
	//
	ld	lr, sp
	jrp	lr

	.size load_and_go_srom,.-load_and_go_srom

// 
// This routine jumps to the hypervisor's entry point.
// 
// On input, r0 is the base physical address for the hypervisor text page, and
// r10 is the PC to jump to, which must be within that hypervisor text page (we
// will ignore its virtual page number and just use the offset within the
// page).  We do not modify r0-r9 as they need to be passed on to the code
// we're calling.
// 

	.type jump_to_hv,@function
jump_to_hv:
	bfextu	r10, r10, 0, HV_CODE_PAGE_SHIFT - 1
	add	r10, r0, r10
	jr	r10

	.size jump_to_hv,.-jump_to_hv


	.data
	.align	4
	//
	// Temporary data area in which we read in the segment header.
	// (We only need 2 words since we reset the address before reading
	// the CRC.)
	//
seg_header:
	.skip	2 * 8
