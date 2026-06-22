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
// This is essentially a copy of the L0 boot code built into the chip.  The
// only difference is that we treat a jump-to address of 0x0 as "go back and
// get more data", which is what it means with the real L0 booter; since this
// routine will not be linked at virtual address 0, we can't just jump there.
//

//
// Level-0 boot code
//
// This on-chip Level 0 boot code processes code/data blocks arriving
// at each tile on UDN queue 0.
//
// Note that every tile will be executing this same code after reset.
//
        .global hw_l0_boot
hw_l0_boot:
        //
        // The general format for the blocks is:
        // - 64-bit word count for the block
        // - 64-bit address where the block should be stored
        // - n 64-bit words
        // - address to which the L0 boot code should jump
        //
        // Multiple blocks can be processed by providing a jump address of
        // 0 until the final block is provided.
        //

        // Get the word count for this block of code/data from UDN queue 0
        { move r0, udn0 }

        // Get the address where we are going to store it from UDN queue 0
        { move r1, udn0 }

loop:
        // Store the next 64-bit word arriving in UDN queue 0
        // Decrement the remaining word count
        { st r1, udn0 ; addi r0, r0, -1 }

        // If the remaining word count is not zero, go get another word for
        // this block.  Update the address to point to the next 64-bit
        // location.
        { bnez r0, loop ; addi r1, r1,  8 }

        // Jump to the address provided through UDN queue 0, unless it's
	// zero, which means "do another block"
	{ move r2, udn0; beqz udn0, hw_l0_boot }
        { jr r2 }

        .size hw_l0_boot,.-hw_l0_boot
