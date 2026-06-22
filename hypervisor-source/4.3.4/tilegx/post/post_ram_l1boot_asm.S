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

#include <arch/chip.h>
#include <arch/spr_def.h>

// Test memory
// Params:
// r0: Physical start address.
// r1: Physical end address (inclusive).
// r2: Mask to XOR with write pattern.
// r3: Mshim location.
// Return:
// r0:
//  ~0 for test passed. Routine returns on first failing address.
//  Failing address if test failed.
// r1: Expected data.
// r2: Actual data.

#define r_start_addr	r0	// Also read addr, and return value.
#define r_end_addr	r1
#define r_mask		r2
#define r_mshim_loc	r3

#define r_saved_mmap	r10
#define r_saved_aar	r11
#define r_saved_car	r12

#define r_pattern_data	r14	// Pre-mask data pattern.
#define r_read_data	r15
#define r_write_addr	r16
#define r_write_data	r17	// Post-mask write and expected data.

#define r_temp_0	r18
#define r_temp_1	r19

	.section .text.post_ram_l1boot_one_pass, "ax"
	.global post_ram_l1boot_one_pass
	.type post_ram_l1boot_one_pass,@function

// Make the high address bits 11b to be sure that we do not use any addresses
// allocated by the HV in the L2 cache.  That will still allow us to start at
// address 0 in the RAM since those two bits are used to index the CBOX MMAP
// SPRs.

// So that these stores and loads go out to the mshim without touching the
// cache, turn off cache-as-ram mode, and mark all data non-cacheable.

// We are already in Physical Memory Mode (which uses the AAR register
// instead of the DTLB).

post_ram_l1boot_one_pass:
	{
	 movei	r_temp_0, 3
	 mfspr	r_saved_mmap, CBOX_MMAP_3
	}
	{
	 mfspr	r_saved_aar, AAR
	 bfins	r_start_addr, r_temp_0, CHIP_PA_WIDTH() - 2, CHIP_PA_WIDTH() - 1
	}
	{
	 mfspr	r_saved_car, CBOX_CACHEASRAM_CONFIG
	 bfins	r_end_addr, r_temp_0, CHIP_PA_WIDTH() - 2, CHIP_PA_WIDTH() - 1
	}
	{
	 move	r_temp_0, r_saved_aar
	 mf
	}
	{
	 mtspr	CBOX_MMAP_3, r_mshim_loc
	 movei	r_temp_1, SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE
	}
	{
	 // Starting value for write pattern (before mask xor) is word addr.
	 shrui	r_pattern_data, r_start_addr, 2
	 bfins	r_temp_0, r_temp_1, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}
	{
	// Turn off cacheable mode.
	 mtspr	 AAR, r_temp_0
	 move	r_write_addr, r_start_addr
	}
	// Turn off cache as ram mode
	mtspr  CBOX_CACHEASRAM_CONFIG, zero
	mf
	drain

	// Test the write pattern for zero, which won't work with CRC.
	bnez	r_pattern_data, ram_l1boot_write_loop
	// Pattern is zero.  Add 1 to fix it
	addi	r_pattern_data, r_pattern_data, 1

ram_l1boot_write_loop:
	{
	 xor	r_write_data, r_pattern_data, r_mask
	 crc32_32 r_temp_1, r_pattern_data, zero
	}
	crc32_32 r_pattern_data, r_temp_1, zero
	bfins	r_pattern_data, r_temp_1, 32, 63
	{
	 st	r_write_addr, r_write_data
	 addi	r_write_addr, r_write_addr, 8
	 // Note that end_addr is inclusive, so compare before increment.
	 cmpeq	r_temp_1, r_write_addr, r_end_addr
	}
	blbct	r_temp_1, ram_l1boot_write_loop
// end write loop

	// Starting value for data pattern (before xor with mask) is word addr
	{
	 shrui	r_pattern_data, r_start_addr, 2
	 mf
	}
	// Test the write pattern for zero, which won't work with CRC.
	bnez	r_pattern_data, ram_l1boot_read_loop
	// Pattern is zero.  Add 1 to fix it
	addi	r_pattern_data, r_pattern_data, 1

ram_l1boot_read_loop:
	{
	 xor	r_write_data, r_pattern_data, r_mask
	 ld	r_read_data, r_start_addr
	}
	{
	 cmpne	r_temp_0, r_read_data, r_write_data
	 // Note that end_addr is inclusive, so compare before increment.
	 cmpeq	r_temp_1, r_start_addr, r_end_addr
	}
	{
	 // If test fails, start_addr will still be in r0 when we return.
	 blbs	r_temp_0, ram_l1boot_fail
	 crc32_32 r_temp_0, r_pattern_data, zero
	}
	crc32_32 r_pattern_data, r_temp_0, zero
	bfins	r_pattern_data, r_temp_0, 32, 63
	{
	 blbct	r_temp_1, ram_l1boot_read_loop
	 addi	r_start_addr, r_start_addr, 8
	}
// end read loop

	// If reached here, test passed.
	// Put passing return value in r0.
	// If test failed, we'll branch over this line, and r0 will contain
	// the failing address.
	moveli	r0, -1
	j	ram_l1boot_done

ram_l1boot_fail:
	// Get rid of the 11b we put in the high address bits.
	bfins	r0, zero, CHIP_PA_WIDTH() - 2, CHIP_PA_WIDTH() - 1
	{
	 move	r1, r_write_data
	 move	r2, r_read_data
	}

ram_l1boot_done:
	mf
	mtspr	CBOX_CACHEASRAM_CONFIG, r_saved_car
	mtspr	CBOX_MMAP_3, r_saved_mmap
	mtspr	AAR, r_saved_aar
	drain

	jrp	lr

	.size post_ram_l1boot_one_pass,.-post_ram_l1boot_one_pass
