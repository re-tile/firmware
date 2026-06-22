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
// First part of hypervisor booter; gets rid of the built-in boot code at
// address 0, zeroes the L2$, then loads the rest of the booter.  The
// only reason this is done in two steps is so that the whole booter can be
// larger than what would fit in way 0 of the L2$.
//
// This program needs to be loaded in a range of addresses which don't
// overlap with the built-in boot code (0x0 to 0x1FF), and which also don't
// overlap with the loaded code; otherwise we might end up kicking ourselves
// out of the L2 during the loading process.  Suggested practice is to load
// this at a relatively high address, like 0x80000200, so that the loaded
// code (which is probably the hypervisor) can start at address 0.
//

#include <arch/chip.h>
#include <arch/sim.h>
#include <arch/spr_def.h>

#include "asm_inc.h"
#include "boot_error.h"
#include "param.h"
#include "sromboot.h"

#define BOOT_BUNDLES	64	// Number of bundles in the L0 boot ROM

	.section .text._lh_start, "ax"
	.global _lh_start
	.type _lh_start,@function
_lh_start:
	//
	// When we're invoked by the SROM booter, we're entered at our
	// second bundle, with some specific values in our registers
	// (see below).  The first bundle thus jumps around the code to
	// handle the SROM case.  Since we have a free instruction slot,
	// we use it for some information on the boot stream.  The moveli
	// to zero doesn't do anything when excuted, but a tool examining
	// the generated code can extract the argument and use it for
	// identification purposes.  Right now, we just put the chip type
	// in the low bits, but we may add to this in the future.
	//
	{
	 j	lh_start_no_srom
	 moveli	zero, __tile_chip__
	}

lh_start_srom:
	//
	// Code to handle the case where we're run from the SROM booter.
	// In this case, another instance of the L0.5 booter has already
	// run, so we don't really have much to do.
	//

	//
	// Save the current cycle count.  We pass this to the L1 booter so it
	// has an idea of how long it takes to boot a tile; this is used as
	// input to the timeout calculation for a dynamic boot.  Since we're
	// going to load the L1 booter via SROM, this is going to greatly
	// overestimate the time needed to boot succeeding tiles via the UDN,
	// but that's OK.
	//
	mfspr	r0, CYCLE

	//
	// We're passed the address of the SROM booter's "load the next
	// chunk of code from the SROM and jump to it" routine in r52,
	// so we just call back to it.
	//
	{
	 jr	r52
         // Pass SROM boot flags to L1 booter; force SROMBOOT_SROMBOOT on
         // in case we were called by a V1 SROM booter.
	 ori	r1, r50, SROMBOOT_SROMBOOT
	}

lh_start_no_srom:
	//
	// Code to handle the case where we're not run from the SROM booter.
	//

#ifdef BOOT_TIMING
	movei	r40, 1
	mtspr	PASS, r40
#endif
	// Clear our registers.

	clr2	 r0, r27
	clr2	 r1, r28
	clr2     r2, r29
	clr2	 r3, r30
	clr2	 r4, r31
	clr2	 r5, r32
	clr2	 r6, r33
	clr2	 r7, r34
	clr2	 r8, r35
	clr2	 r9, r36
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
	clr2	sp,  lr

	// Save the current cycle count.  We pass this to the L1 booter so it
	// has an idea of how long it takes to boot a tile; this is used as
	// input to the timeout calculation for a dynamic boot.
	mfspr	r0, CYCLE

	// Zero out the first 64 bundles in the L2$ (these are aliased by
	// the L0 boot code that was copied into I$), and then flush the
	// corresponding lines in the I$.  Note that for this purpose, and
	// for the whole-cache clearing we do later, we use addresses which
	// are consistent with our loaded code; when we're done, we want
	// every tag in the L2 to not match anything we're likely to be
	// writing to memory.

	moveli r10, hw2_last(BOOT_BASEADDR)
	{
	 shl16insli r10, r10, hw1(BOOT_BASEADDR)
	 moveli	r11, (BOOT_BUNDLES * BUNDLE_SIZE) - 8
	}
	shl16insli r10, r10, hw0(BOOT_BASEADDR)
	add	r10, r10, r11

	mf
	drain

bundle_clear:
	{
	 st	r10, zero
	 addi	r11, r11, -8
	}
	// FIXME: GX: this inv currently causes a crash.  That will go away
	// when the new init values for the AAR/I_AAR are implemented (6589),
	// although it might turn out that we just don't need the inv anyway.
	//inv	r10
	icoh	r10
	{
	 bnez	r11, bundle_clear
	 addi	r10, r10, -8
	}

	mf
	drain

#if 0
        // POST for Caches
        jal     post_cache
#endif

	// Zero out the entire L2$ (well, everything after our data ends).
	// This protects against parity errors and other random behavior
	// caused by reading a line which wasn't fully written.

        // Also zero out the L1D$ as we go, to remove anything left from POST.
        // Do this by doing a load to the same address after each store.

	{
	 // r10 is the address we're currently zeroing
	 moveli	r10, hw2_last(_sbss)
	 // r11 is the last address we'll zero in this way
	 moveli	r11, hw2_last(BOOT_BASEADDR + L2_WAY_SIZE - 8)
	}
	{
	 shl16insli r10, r10, hw1(_sbss)
	 shl16insli r11, r11, hw1(BOOT_BASEADDR + L2_WAY_SIZE - 8)
	}
	{
	 shl16insli r10, r10, hw0(_sbss)
	 shl16insli r11, r11, hw0(BOOT_BASEADDR + L2_WAY_SIZE - 8)
	}
	{
	 // r12 is the way number
	 move	r12, zero
	 // r13 is the way size
	 moveli	r13, hw1_last(L2_WAY_SIZE)
	}
	{
	 // r14 is the value for the cache-as-RAM config SPR
	 mfspr r14, CBOX_CACHEASRAM_CONFIG
	 shl16insli r13, r13, hw0(L2_WAY_SIZE)
	}

way_clear:
	{
	 st	r10, zero
	 cmpltu	r3, r10, r11
	}
        ld	r5, r10  // Load after store to get data in L1 D
	{
	 blbst	r3, way_clear
	 addi	r10, r10, 8
	}

	// Do we have another way to clear?
	cmpltui	r5, r12, CHIP_L2_ASSOC() - 1
	{
	 blbc	r5, clear_done
	 addi	r12, r12, 1
	}

	// Point to next way.
        {
	 bfins	r14, r12, SPR_CBOX_CACHEASRAM_CONFIG__ENABLE_WAY_FIELD
	 mf
        }
	mtspr	CBOX_CACHEASRAM_CONFIG, r14

	// Set up last address for next way and start clearing.
	{
	 add	r11, r11, r13
	 j	way_clear
	}

clear_done:
	//
	// Load the rest of the booter from the static network.
	//

	// Initialize the CRC.
	movei	r25, ~0

next_segment:
	{
	 // Get length.
	 move	r20, udn0
	 // Add bottom half of length to CRC.
	 crc32_32 r25, r25, udn0
	}
	// Add top half of length to CRC.
	shrui	r29, r20, 32
	crc32_32 r25, r25, r29
	{
	 // Get address.
	 move	r21, udn0
	 // Add bottom half of address to CRC.
	 crc32_32 r25, r25, udn0
	}
	// Add top half of address to CRC.
	shrui	r29, r21, 32
	crc32_32 r25, r25, r29

	// Invert the accumlated CRC to its final value, check against input,
	// and fail if no match.  Note that we preserve the accumulated CRC so
	// we can continue adding to it.

	nor	r26, r25, zero
	bfextu	r26, r26, 0, 31
	cmpeq	r26, r26, udn0
	blbc	r26, bad_crc

	// Zero length means we're done.
	beqz	r20, copy_done

segment_loop:
	// Copy data to memory, CRCing it as we go.
	{
	 st	r21, udn0
	 move	r29, udn0
	}
	{
	 crc32_32 r25, r25, r29
	 shrui	r28, r29, 32
	}
	{
	 addi	r20, r20, -1
	 crc32_32 r25, r25, r28
	}
	{
	 bgtzt	r20, segment_loop
	 addi	r21, r21, 8
	}

	// Done with that segment; go get another one.
	j	next_segment

copy_done:
	//
	// We just wrote a bunch of code to the I$, but previously we
	// fetched a bunch of stuff into the I$ to make sure it was clean,
	// so we need to invalidate things before trying to execute the
	// new code.
	//

	moveli r10, hw2_last(BOOT_BASEADDR)
	{
	 shl16insli r10, r10, hw1(BOOT_BASEADDR)
	 moveli	r11, hw1_last(CHIP_L1I_CACHE_SIZE())
	}
	{
	 shl16insli r10, r10, hw0(BOOT_BASEADDR)
	 shl16insli r11, r11, hw0(CHIP_L1I_CACHE_SIZE())
	}

	mf
	drain

icache_inv:
	{
	 icoh	r10
	 addi	r11, r11, -8
	}
	{
	 bnez	r11, icache_inv
	 addi	r10, r10, 8
	}

	mf
	drain

	//
	// Finally, jump to the loaded code.
	//
#ifdef BOOT_TIMING
	movei	r20, 2
	mtspr	PASS, r20
#endif
	{
	 jr	r21
	 move	r2, zero  // Tell L1 boot that we weren't booted from SROM
	}


bad_crc:
	//
	// Eventually we'll want to fail here in a way which causes the SROM
	// booter to regain control.
	//
        PUNT(BOOT_ERR_L1_IMAGE_BAD_CRC)

	PUNT_ROUTINE
