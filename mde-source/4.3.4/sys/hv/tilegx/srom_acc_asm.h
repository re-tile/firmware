//
// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.
//

//
// SPI Flash ROM access routines.
//

#include <arch/spr_def.h>
#include <arch/srom_def.h>

#include "srom_asm.h"  // Included from build tree

//
// This file is not assembled directly; instead, it is #included into another
// assembly file (almost certainly one which will be directly calling these
// routines).  This allows the caller to determine which registers are used
// by these routines, based on its requirements.
//
// Before the file is #included, the includer must define the following
// symbols so that they each evaluate to a unique register which is not
// r0, r1, or r2:
//
// r_srom_dest_addr (*)
// r_srom_dest_incr (*)
// r_srom_wds (*)
// r_srom_crc (*)
// r_srom_shim_addr (*)
// r_srom_dev (*)
// r_srom_wds_pass
// r_cfg_chan
// r_cfg_aar
// r_cfg_addr
// r_cfg_data
// r_srom_lr
// r_a24_lr
// r_srom_temp_0
// r_srom_temp_1
// r_cfg_temp_0
//
// The symbols with (*) next to them are used as parameters to the
// routines below, and their use is described there.  All other registers
// are private to the routines in this file; they may be destroyed by a
// call to any of those routines.
//
// r0-r2 are also used as a parameter to some of the routines, so that
// they may be called from C; if this is desired, all of the registers above
// must be caller-saved registers.  This is why those registers may not be
// used as a definition for the routines above.
//

	//
	// Get the SROM device cookie.
	// @param r0 Tile coordinates of the rshim.
	// @return r0 SROM device cookie.
	//
	.section .text.srom_get_dev, "ax"
	.global early_srom_get_dev
	.type srom_get_dev,@function
srom_get_dev:
early_srom_get_dev:
	//
	// Set up invariants for cfg_rd/_wr.
	//
	move	r_srom_shim_addr, r0
	{
	 move	r_srom_lr, lr
	 jal	srom_invariants
	}

	//
	// Read the ID value from the device.
	//
	{
	 moveli	r_cfg_addr, SROM_BYTE
	 movei	r_cfg_data, 5
	}
	jal	srom_cfg_wr
	{
	 moveli	r_cfg_addr, SROM_INSTRUCTION
	 moveli	r_cfg_data, SROM_INSTRUCTION__INST_VAL_RDID0
	}
	jal	srom_cfg_wr
0:
	{
	 moveli	r_cfg_addr, SROM_FLAG
	 jal	srom_cfg_rd
	}
	andi	r_cfg_data, r_cfg_data, SROM_FLAG__RFIFO_EMPTY_MASK
	bnez	r_cfg_data, 0b
	{
	 moveli	r_cfg_addr, SROM_READ_DATA
	 jal	srom_cfg_rd
	}

	//
	// Look though the table for a match.
	//
	// We assume that each table entry is 24 bytes, laid out as:
	//
	// - SROM ID (8 bytes)
	// - SROM ID mask (8 bytes)
	// - Dev cookie (4 bytes)
	// - Filler (4 bytes)
	//
	// The dev cookie contains:
	//
	// - [5:0]   64 - ctz(srom size).  This enables creation of a
	//           mask for the device address bits with 2 instructions;
	//           see examples below.
	// - [15:8]  Command to read A24 register.
	// - [23:16] Command to write A24 register.
	//
	// This is all defined by struct srom_boot in srom_table.h.  Note
	// that the dev cookie format is private to routines in this file,
	// and may or may not match that used in the hypervisor.
	//
	moveli	lr, hw2_last(srom_table)
	shl16insli lr, lr, hw1(srom_table)
	shl16insli lr, lr, hw0(srom_table)

	moveli	r_srom_temp_0, hw2_last(srom_table_end)
	shl16insli r_srom_temp_0, r_srom_temp_0, hw1(srom_table_end)
	shl16insli r_srom_temp_0, r_srom_temp_0, hw0(srom_table_end)

0:
	// Get the ID.
	{
	 ld	r_srom_temp_1, lr
	 addi	lr, lr, 8
	}
	// Get the mask.
	{
	 ld	r0, lr
	 addi	lr, lr, 8
	}
	// See if our ID and the table ID are equal when masked.
	xor	r_srom_temp_1, r_cfg_data, r_srom_temp_1
	and	r_srom_temp_1, r_srom_temp_1, r0
	bnez	r_srom_temp_1, 1f

	// Yes; load the dev cookie and exit the loop.
	ld4u	r0, lr
	j	2f

1:
	// No.  Move to the next entry; if we aren't done, try again.
	addi	lr, lr, 8
	cmpltu	r_srom_temp_1, lr, r_srom_temp_0
	blbs	r_srom_temp_1, 0b

	// We didn't find the entry, so return a zero cookie.
	{
	 move	r0, zero
	 jrp	r_srom_lr
	}

2:
	// Now we need to figure out if we need to do WREN before we
	// set the A24 register.  We read it, flip the low bit, and
	// write it back.
	{
	 move	r_srom_dev, r0
	 jal	srom_a24_rd
	}
	move	r_srom_temp_0, r_cfg_data
	{
	 xori	r_cfg_data, r_cfg_data, 1
	 jal	srom_a24_wr
	}
	jal	srom_a24_rd

	// If the two values are equal, then we need the WREN; modify the
	// cookie and return.
	cmpeq	r_srom_temp_1, r_srom_temp_0, r_cfg_data
	blbc	r_srom_temp_1, 3f

	{
	 ori	r0, r0, 1 << 6
	 jrp	r_srom_lr
	}

3:
	// Otherwise, we write back the original value and return.
	{
	 move	r_cfg_data, r_srom_temp_0
	 jal	srom_a24_wr
	}

	jrp	r_srom_lr

	.size srom_enable,.-srom_enable


	//
	// Get the current value of the SROM shim's address register.
	// @param r0 Tile coordinates of the rshim.
	// @param r1 SROM device cookie.
	// @return r0 Address register contents.
	//
	.section .text.srom_get_addr, "ax"
	.global srom_get_addr
	.type srom_get_addr,@function
srom_get_addr:
	//
	// Set up invariants for cfg_rd/_wr.
	//
	{
	 move	r_srom_shim_addr, r0
	 move	r_srom_dev, r1
	}
	{
	 move	r_srom_lr, lr
	 jal	srom_invariants
	}

	//
	// Read the address register.
	//
	{
	 moveli	r_cfg_addr, SROM_ADDRESS
	 jal	srom_cfg_rd
	}

	//
	// If there're no A24 bits, just return the base address.
	//
	{
	 bnez   r_srom_dev, 0f
	 move   r0, r_cfg_data
	}
	jrp	r_srom_lr
0:
	//
	// Get the A24 bits.
	//
	jal	srom_a24_rd

	//
	// Stick the A24 bits at the top of the address, then mask off
	// any garbage.
	//
	{
	 movei	r_srom_temp_0, -1
	 bfins	r0, r_cfg_data, 24, 31
	}
	shru	r_srom_temp_0, r_srom_temp_0, r_srom_dev
	{
	 and	r0, r0, r_srom_temp_0
	 jrp	r_srom_lr
	}

	.size srom_get_addr,.-srom_get_addr


	//
	// Set the current value of the SROM shim's address register.
	// @param r0 Tile coordinates of the rshim.
	// @param r1 SROM device cookie.
	// @param r2 Value to place in address register.
	//
	.section .text.srom_set_addr, "ax"
	.global srom_set_addr
	.type srom_set_addr,@function
srom_set_addr:
	//
	// Set up invariants for cfg_rd/_wr.
	//
	move	r_srom_shim_addr, r0
	{
	 move	r_srom_lr, lr
	 jal	srom_invariants
	}

	//
	// Set the address register.
	//
	{
	 moveli	r_cfg_addr, SROM_ADDRESS
	 move	r_cfg_data, r2
	}
	{
	 jal	srom_cfg_wr
	 move   r_srom_dev, r1
	}

	//
	// If there're no A24 bits, we're done.  Otherwise, mask the
	// address and write the high bits to the device.
	//
	{
	 beqz	r_srom_dev, 0f
	 movei	r_srom_temp_0, -1
	}
	shru	r_srom_temp_0, r_srom_temp_0, r_srom_dev
	and	r_srom_temp_0, r2, r_srom_temp_0
	{
	 shrui	r_cfg_data, r_srom_temp_0, 24
	 jal	srom_a24_wr
	}
0:
	jrp	r_srom_lr

	.size srom_set_addr,.-srom_set_addr

	//
	// Read some 4-byte words from the SROM.  The offset used within the
	// SROM is the current value of the SROM shim's address register,
	// which is incremented upon return by the number of words read;
	// this permits sequential access via a series of calls.
	//
	// Note that we work in 4-byte words even though the shim, and the
	// processor, handle 8-byte words; this is so that we can handle
	// reading things which are built from 4-byte words (like the BIB).
	//
	// @param r_srom_shim_addr Tile coordinates of the rshim.
	// @param r_srom_dest_addr Address in memory at which to store the
	//   words read.  Upon return this will have been incremented by
	//   the number of words stored times r_srom_dest_incr.
	// @param r_srom_dest_incr Address increment.  This is added to
	//   the destination address after each word is stored.  The purpose
	//   of this parameter is to make it possible to call this routine
	//   when you just want to CRC data from the SROM but don't want to
	//   have to allocate a big buffer for it.  Note that all of the data
	//   is still stored to memory, just to one word, so r_srom_dest_addr
	//   must be valid.  Upon return this register will be unchanged.
	// @param r_srom_wds Number of 4-byte words to read.  Upon return this
	//   register will be destroyed.
	// @param r_srom_crc This register is updated with a running CRC-32
	//   computed over the words read.
	// @param r_srom_dev SROM device cookie.  Upon return this register
	//   will be unchanged.
	//
	.section .text.srom_rd, "ax"
	.type srom_rd,@function
srom_rd:
	//
	// Set up invariants for cfg_rd/_wr; invert the CRC; save off the
	// current SROM address.
	//
	{
	 move	r_srom_lr, lr
	 jal	srom_invariants
	}
	nor	r_srom_crc, r_srom_crc, zero

	//
	// Save off current SROM address.
	//
	{
	 moveli	r_cfg_addr, SROM_ADDRESS
	 jal	srom_cfg_rd
	}
	{
	 move	r_srom_temp_0, r_cfg_data
	}

srom_rd_pass:
	//
	// The shim can only handle a megabyte per request, so we break
	// things into chunks.
	//
	max_chunk_wds = ((1 << 20) - 4) / 4
	moveli	r_srom_wds_pass, hw1_last(max_chunk_wds)
	shl16insli r_srom_wds_pass, r_srom_wds_pass, hw0(max_chunk_wds)
	cmpleu	r_srom_temp_1, r_srom_wds_pass, r_srom_wds
	cmoveqz	r_srom_wds_pass, r_srom_temp_1, r_srom_wds

	//
	// Calculate the new value for the shim address register that we'll
	// set at the end of this pass; set up the SROM shim for the length
	// of the read and request execution of the read instruction.  Also
	// update the total number of words moved (it would make more sense
	// to do that later, but we decrement the per-pass word count in
	// the loop below, so we have to do it now).
	//
	{
	 shl2add r_srom_temp_0, r_srom_wds_pass, r_srom_temp_0
	 moveli	r_cfg_addr, SROM_BYTE
	}
	{
	 shl2add r_cfg_data, r_srom_wds_pass, zero
	 jal	srom_cfg_wr
	}
	{
	 moveli	r_cfg_addr, SROM_INSTRUCTION
	 moveli	r_cfg_data, SROM_INSTRUCTION__INST_VAL_READ
	}
	{
	 jal	srom_cfg_wr
	 sub	r_srom_wds, r_srom_wds, r_srom_wds_pass
	}

	//
	// Loop until the SROM says it has some data.
	//
0:
	{
	 moveli	r_cfg_addr, SROM_FLAG
	 jal	srom_cfg_rd
	}
	andi	r_cfg_data, r_cfg_data, SROM_FLAG__RFIFO_EMPTY_MASK
	bnez	r_cfg_data, 0b

	//
	// Read an 8-byte word from the SROM, store it to memory, and CRC it.
	//
	{
	 moveli	r_cfg_addr, SROM_READ_DATA
	 jal	srom_cfg_rd
	}

	//
	// If we only have one 4-byte word left, we just want to use the
	// top half of the 8-byte data register.
	//
	addi	r_srom_temp_1, r_srom_wds_pass, -1
	beqz	r_srom_temp_1, 1f

	//
	// Handle the bottom half of the data.
	//
	{
	 st4	r_srom_dest_addr, r_cfg_data
	 add	r_srom_dest_addr, r_srom_dest_addr, r_srom_dest_incr
	 addi	r_srom_wds_pass, r_srom_wds_pass, -1
	}

	{
	 crc32_32 r_srom_crc, r_srom_crc, r_cfg_data
	}

	//
	// Handle the top half of the data.
	//
1:
	shrui	r_cfg_data, r_cfg_data, 32

	{
	 st4	r_srom_dest_addr, r_cfg_data
	 add	r_srom_dest_addr, r_srom_dest_addr, r_srom_dest_incr
	 addi	r_srom_wds_pass, r_srom_wds_pass, -1
	}

	{
	 crc32_32 r_srom_crc, r_srom_crc, r_cfg_data
	}


	//
	// Loop back if we have more on this pass.
	//
	bgtz	r_srom_wds_pass, 0b

	//
	// We're done with this pass.  Write the incremented SROM address back
	// to the shim.
	//
	moveli	r_cfg_addr, SROM_ADDRESS
	{
	 move	r_cfg_data, r_srom_temp_0
	 jal	srom_cfg_wr
	}

	//
	// If the SROM address incremented into bit 24, then we need to
	// bump the chip's A24 register by one.
	//
	shrui	r_srom_temp_1, r_srom_temp_0, 24
	beqzt	r_srom_temp_1, 0f

	//
	// If this chip doesn't have an A24 register, nothing to do either.
	// This happens when the very last byte we read is the last byte of
	// a 16 MB part.
	//
	beqz	r_srom_dev, 0f

	//
	// Get the current A24 value.
	//
	jal	srom_a24_rd

	//
	// Compute the new value; mask it so we won't write any bogus
	// upper bits into the A24 register; and finally write it.
	//
	add	r_cfg_data, r_srom_temp_1, r_cfg_data
	{
	 shli	r_cfg_data, r_cfg_data, 24
	 movei	r_srom_temp_1, -1
	}
	shru	r_srom_temp_1, r_srom_temp_1, r_srom_dev
	and	r_cfg_data, r_cfg_data, r_srom_temp_1
	{
	 shrui	r_cfg_data, r_cfg_data, 24
	 jal	srom_a24_wr
	}
0:
	//
	// Do another pass if needed.
	//
	bgtz	r_srom_wds, srom_rd_pass

	//
	// We're done with the read.  Invert the CRC and return.
	//
	nor	r_srom_crc, r_srom_crc, zero
	{
	 jrp	r_srom_lr
	 bfextu	r_srom_crc, r_srom_crc, 0, 31
	}

	.size srom_rd,.-srom_rd


	//
	// Routines after this point are support routines for the functions
	// above and are not intended to be called directly by anything
	// outside this file.
	//

	//
	// Set up a couple of registers used by the cfg_rd/_wr routines.
	//
	.section .text.srom_invariants, "ax"
	.type srom_invariants,@function
srom_invariants:
	{
	 movei	r_cfg_aar, SPR_AAR__PHYSICAL_MEMORY_MODE_MASK
	 movei	r_cfg_temp_0, SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO
	}

	{
	 moveli	r_cfg_chan, hw2_last(SROM_CHAN)
	 bfins	r_cfg_aar, r_cfg_temp_0, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}

	{
	 shl16insli r_cfg_chan, r_cfg_chan, hw1(SROM_CHAN)
	 bfextu	r_cfg_temp_0, r_srom_shim_addr, SPR_RSHIM_COORD__Y_COORD_FIELD
	}

	{
	 shl16insli r_cfg_chan, r_cfg_chan, hw0(SROM_CHAN)
	 bfins	r_cfg_aar, r_cfg_temp_0, \
		SPR_AAR__LOCATION_Y_OR_PAGE_OFFSET_FIELD
	}

	{
	 bfextu	r_cfg_temp_0, r_srom_shim_addr, SPR_RSHIM_COORD__X_COORD_FIELD
	}

	{
	 bfins	r_cfg_aar, r_cfg_temp_0, \
		SPR_AAR__LOCATION_X_OR_PAGE_MASK_FIELD
	 jrp	lr
	}

	.size srom_invariants,.-srom_invariants


	//
	// Do a config write.  SROM invariants must have already been set up.
	// @param r_cfg_addr Address of the register to write.
	// @param r_cfg_data Data to write to that register.
	//
	.section .text.srom_cfg_wr, "ax"
	.type srom_cfg_wr,@function
srom_cfg_wr:
	mfspr	r_cfg_temp_0, AAR
	mtspr	AAR, r_cfg_aar
	// Note that there must be a 1-cycle delay between setting AAR and
	// the store.
	bfins	r_cfg_chan, r_cfg_addr, 0, 15
	st	r_cfg_chan, r_cfg_data
	mtspr	AAR, r_cfg_temp_0
	jrp	lr

	.size srom_cfg_wr,.-srom_cfg_wr


	//
	// Do a config read.  SROM invariants must have already been set up.
	// @param r_cfg_addr Address of the register to read.
	// @return r_cfg_data Data read from that register.
	//
	.section .text.srom_cfg_rd, "ax"
	.type srom_cfg_rd,@function
srom_cfg_rd:
	mfspr	r_cfg_temp_0, AAR
	mtspr	AAR, r_cfg_aar
	// Note that there must be a 1-cycle delay between setting AAR and
	// the load.
	bfins	r_cfg_chan, r_cfg_addr, 0, 15
	ld	r_cfg_data, r_cfg_chan
	mtspr	AAR, r_cfg_temp_0
	jrp	lr

	.size srom_cfg_rd,.-srom_cfg_rd


	//
	// Read the A24 bits from the device.  SROM invariants must have
	// already been set up.
	// @param  r_srom_dev SROM device cookie.
	// @return r_cfg_data A24 bits.
	//
	.section .text.srom_a24_rd, "ax"
	.type srom_a24_rd,@function
srom_a24_rd:
	//
	// Make the RDSR command match the device's A24 read command.
	//
	{
	 moveli	r_cfg_addr, SROM_CODE_RDSR
	 move	r_a24_lr, lr
	}
	{
	 bfextu r_cfg_data, r_srom_dev, 8, 15
	 jal	srom_cfg_wr
	}

	//
	// Issue the A24 read command.
	//
	{
	 moveli	r_cfg_addr, SROM_INSTRUCTION
	 bfextu r_cfg_data, r_srom_dev, 8, 15
	}
	jal	srom_cfg_wr

	//
	// Wait until we get a byte in the read FIFO.
	//
0:
	{
	 moveli	r_cfg_addr, SROM_FLAG
	 jal	srom_cfg_rd
	}
	andi	r_cfg_data, r_cfg_data, SROM_FLAG__RFIFO_EMPTY_MASK
	bnez	r_cfg_data, 0b

	//
	// Set the RDSR command back to a real RDSR command.
	//
	{
	 moveli	r_cfg_addr, SROM_CODE_RDSR
	 movei	r_cfg_data, SROM_INSTRUCTION__INST_VAL_RDSR
	}
	jal	srom_cfg_wr

	//
	// Get the byte from the device.
	//
	{
	 moveli	r_cfg_addr, SROM_READ_DATA
	 jal	srom_cfg_rd
	}

	jrp	r_a24_lr

	.size srom_a24_rd,.-srom_a24_rd


	//
	// Write the A24 bits to the device.  SROM invariants must have
	// already been set up.
	// @param  r_srom_dev SROM device cookie.
	// @param  r_cfg_data A24 bits.
	//
	.section .text.srom_a24_wr, "ax"
	.type srom_a24_wr,@function
srom_a24_wr:
	//
	// Write the A24 bits into the write FIFO.  (It would be a bit
	// clearer to do this later, but we need to use the register it's
	// in for other stuff.)
	//
	{
	 moveli	r_cfg_addr, SROM_WRITE_DATA
	 move	r_a24_lr, lr
	}
	jal	srom_cfg_wr

	//
	// If needed, issue the WREN command.
	//
	shrui	r_cfg_data, r_srom_dev, 6
	blbc	r_cfg_data, no_wren

	{
	 moveli	r_cfg_addr, SROM_INSTRUCTION
	 moveli	r_cfg_data, SROM_INSTRUCTION__INST_VAL_WREN
	}
	jal	srom_cfg_wr

	//
	// Wait until the instruction completes.
	//
0:
	{
	 moveli	r_cfg_addr, SROM_FLAG
	 jal	srom_cfg_rd
	}
	andi	r_cfg_data, r_cfg_data, SROM_FLAG__BUSY_MASK
	bnez	r_cfg_data, 0b

no_wren:
	//
	// Make the WRSR command match the device's A24 write command.
	//
	{
	 moveli	r_cfg_addr, SROM_CODE_WRSR
	 bfextu r_cfg_data, r_srom_dev, 16, 23
	}
	jal	srom_cfg_wr

	//
	// Issue the A24 write command.
	//
	{
	 moveli	r_cfg_addr, SROM_INSTRUCTION
	 bfextu	r_cfg_data, r_srom_dev, 16, 23
	}
	jal	srom_cfg_wr

	//
	// Wait until the instruction completes.
	//
0:
	{
	 moveli	r_cfg_addr, SROM_FLAG
	 jal	srom_cfg_rd
	}
	andi	r_cfg_data, r_cfg_data, SROM_FLAG__BUSY_MASK
	bnez	r_cfg_data, 0b

	//
	// Set the WRSR command back to a real WRSR command.
	//
	{
	 moveli	r_cfg_addr, SROM_CODE_WRSR
	 movei	r_cfg_data, SROM_INSTRUCTION__INST_VAL_WRSR
	}
	jal	srom_cfg_wr

	//
	// Return.
	//
	jrp	r_a24_lr

	.size srom_a24_wr,.-srom_a24_wr
