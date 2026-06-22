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
// SROM booter.
//
//   The purpose of the SROM booter is to support more than one boot image
//   in a bootable SPI ROM.  This provides improved robustness in the face
//   of errors which might happen during an SROM update: for instance, a
//   power failure, software fault, or user error.  If the SROM contained
//   only one boot image, such an error might leave the system unbootable.
//   With more than one image, in many cases we will be able to detect
//   that an error had corrupted one of the boot images, and fall back
//   to another, non-corrupt image.
//
//   The SROM booter is responsible for examining the images present
//   in the ROM, choosing one based on the available information, and
//   then loading it.  Since the SROM booter is the component which picks
//   between the available images to provide robustness, it itself is not
//   resilient to corruption.  Because of this, the booter is intentionally
//   small and as simple as possible.  The intent is that the booter will
//   be installed once during the initial manufacturing of a system and
//   will never be updated in the field.
//
//   There are many possible policies which might be used to pick from the
//   available images.  The booter currently picks the newest noncorrupt
//   image (as determined by a generation number which is incremented
//   whenever a new image is installed).  More advanced algorithms are
//   certainly possible; for instance, on hardware which supported a
//   user-modifiable switch or jumper which could be read by the booter,
//   it might be beneficial to sense that, and use that input to possibly
//   modify the image choice.
//
//   See sromboot.h for a definition of the interface between the SROM
//   booter and the L0.5/L1 booters.
//

#include <arch/rsh_def.h>

#include "asm_inc.h"
#include "boot_error.h"
#include "param.h"
#include "sromboot.h"

#include "srom_asm.h"  // Included from build tree

//
// Our registers.
//
#define r_temp_0           r10  // Various temporary uses
#define r_temp_1           r11  // Various temporary uses
#define r_temp_2           r12  // Various temporary uses
#define r_temp_3           r13  // Various temporary uses
#define r_temp_4           r14  // Various temporary uses





#define r_secsiz           r19  // SROM sector size
#define r_pagsiz           r20  // SROM page size
#define r_sromsiz          r21  // SROM total size
#define r_nimages          r22  // Number of images in the SROM
#define r_n_val_images     r23  // Number of valid images in the SROM

#define r_image_offsets    r24  // Pointer to base of image offsets table
#define r_image_headers    r25  // Pointer to base of image header table
#define r_seg_header       r26  // Pointer to segment header buffer

#define r_cur_img_srom     r27  // SROM offset of current image
#define r_cur_img_offp     r28  // Pointer to current image in offset table
#define r_cur_img_hdrp     r29  // Pointer to current image header in header tbl

#define r_tgt_img_srom     r30  // SROM offset of target image
#define r_tgt_img_offp     r31  // Pointer to target image in offset table
#define r_tgt_img_hdrp     r32  // Pointer to target image header in header tbl

#define r_altimage         r33  // If nonzero, boot alternate image

//
// Registers used by the SROM access routines, which we're about to
// instantiate by including their source.  These can't overlap with the
// registers above.
//
#define r_srom_dev         r34
#define r_srom_shim_addr   r35
#define r_cfg_temp_0       r36
#define r_srom_temp_0      r37
#define r_srom_temp_1      r38
#define r_srom_dest_addr   r39
#define r_srom_dest_incr   r40
#define r_srom_wds         r41
#define r_srom_crc         r42
#define r_srom_wds_pass    r43
#define r_cfg_chan         r44
#define r_cfg_aar          r45
#define r_cfg_addr         r46
#define r_cfg_data         r47
#define r_srom_lr          r48
#define r_a24_lr           r49

//
// r50 is used as part of the interface from this code; see sromboot.h.
//

#include "srom_acc_asm.h"

//
// Maximum number of images we support.
//
#define MAX_IMAGES       2

	.section .text._srom_start, "ax"
	.global _srom_start
	.type _srom_start,@function
_srom_start:

#ifdef BOOT_TIMING
	movei	r0, 1
	mtspr	SPR_PASS, r0
#endif

	//
	// Set up some constants.
	//
	{
	 moveli	r_image_headers, hw2_last(image_headers)
	 moveli	r_seg_header, hw2_last(seg_header)
	}
	{
	 shl16insli r_image_headers, r_image_headers, hw1(image_headers)
	 moveli	r_image_offsets, hw2_last(image_offsets)
	}
	{
	 shl16insli r_image_headers, r_image_headers, hw0(image_headers)
	 shl16insli r_seg_header, r_seg_header, hw1(seg_header)
	}
	{
	 shl16insli r_seg_header, r_seg_header, hw0(seg_header)
	 shl16insli r_image_offsets, r_image_offsets, hw1(image_offsets)
	}
	{
	 shl16insli r_image_offsets, r_image_offsets, hw0(image_offsets)
	 mfspr	r_srom_shim_addr, RSHIM_COORD
	}

	//
	// Check to see whether booting the alternate image has been requested.
	//
	{
	 moveli	r0, RSH_BREADCRUMB0
	 jal	rsh_cfg_rd
	}
	{
	 move	r_temp_0, r0
	 andi	r_altimage, r0, SROMBOOT_SOFTREBOOT_ACT_MASK
	}
	{
	 cmpeqi	r_altimage, r_altimage, SROMBOOT_SOFTREBOOT_ACT_BADCRC
	 bfextu	r1, r_temp_0, SROMBOOT_SOFTREBOOT_WD_FIELD
	}

	//
	// Check to see if we should enable the watchdog, and if so, do so.
	//
	beqzt	r1, no_wdog

	//
	// First set the count; we multiply the given number of seconds by the
	// reference clock frequency.
	//
	moveli	r2, hw1_last(REFCLK)
	{
	 shl16insli r2, r2, hw0(REFCLK)
	 moveli	r0, RSH_DOWN_COUNT_VALUE__FIRST_WORD
	}
	{
	 mul_lu_lu r1, r1, r2
	 jal	rsh_cfg_wr
	}

	//
	// Next we configure down counter 0 to enable soft reset when it
	// becomes zero.
	//
	movei	r1, (1 << 0)
	{
	 moveli	r0, RSH_WATCHDOG_CONTROL
	 jal	rsh_cfg_wr
	}

	//
	// Finally enable the downcounter.  Note that we just set the enable
	// bit; we want everything else in the register to be zero, which
	// gets us divide-by-1 of the core reference clock.
	//
	movei	r1, RSH_DOWN_COUNT_CONTROL_2__ENA_MASK
	{
	 moveli	r0, RSH_DOWN_COUNT_CONTROL__FIRST_WORD
	 jal	rsh_cfg_wr
	}

no_wdog:
	//
	// Clear any bits passed to us in the breadcrumb register so they
	// don't apply to future boots.
	//
	moveli	r1, hw1_last(~SROMBOOT_BREADCRUMB0_MASK)
	{
	 shl16insli r1, r1, hw0(~SROMBOOT_BREADCRUMB0_MASK)
	 moveli	r0, RSH_BREADCRUMB0
	}
	{
	 and	r1, r1, r_temp_0
	 jal	rsh_cfg_wr
	}

	//
	// Read in and CRC our parameters; check to make sure the magic
	// number is correct and that the number of images is in range.
	// Note that the values in the trailer are all 32-bit words,
	// so we need to split the 64-bit input words appropriately.
	//
	{
	 moveli	r_temp_0, ~0
	 moveli	r_temp_2, hw1_last(SROM_TRAILER_MAGIC)
	}
	{



	 move	r_temp_4, udn0

	 shl16insli r_temp_2, r_temp_2, hw0(SROM_TRAILER_MAGIC)
	}
	{
	 bfextu	r_temp_1, r_temp_4, 0, 31
	}
	{
	 crc32_32 r_temp_0, r_temp_0, r_temp_1
	 cmpeq	r_temp_1, r_temp_1, r_temp_2
	}

	// Check trailer, get sector size.
	{
	 blbc	r_temp_1, bad_trailer
	 shrui	r_secsiz, r_temp_4, 32
	}
	{
	 crc32_32 r_temp_0, r_temp_0, r_secsiz

	 move	r_temp_4, udn0



	}

	// Page size.
	{
	 addxi	r_pagsiz, r_temp_4, 0
	 crc32_32 r_temp_0, r_temp_0, r_temp_4
	}

	// SROM size.
	{
	 shrui	r_sromsiz, r_temp_4, 32
	}
	{
	 crc32_32 r_temp_0, r_temp_0, r_sromsiz

	 move	r_temp_4, udn0



	}

	// Number of images.
	{
	 addxi	r_nimages, r_temp_4, 0
	 crc32_32 r_temp_0, r_temp_0, r_temp_4
	}

	// Make sure nimages is reasonable.
	{
	 blez	r_nimages, bad_trailer
	 addi	r_temp_2, r_nimages, -MAX_IMAGES
	}
	{
	 bgtz	r_temp_2, bad_trailer
	 shrui	r_temp_4, r_temp_4, 32
	}

	// Get the list of offsets.
	{
	 move	r_temp_1, r_nimages
	 move	r_temp_3, r_image_offsets
	}

get_offset_loop:
	{
	 crc32_32 r_temp_0, r_temp_0, r_temp_4
	}
	{
	 st4	r_temp_3, r_temp_4
	 addi	r_temp_1, r_temp_1, -1
	 addi	r_temp_3, r_temp_3, 4
	}
	blez	r_temp_1, read_crc_get_offset_done


	{
	 move	r_temp_4, udn0
	 crc32_32 r_temp_0, r_temp_0, udn0
	}




	{
	 st4	r_temp_3, r_temp_4
	 addi	r_temp_1, r_temp_1, -1
	 addi	r_temp_3, r_temp_3, 4
	}
	{
	 blez	r_temp_1, get_offset_done
	 shrui	r_temp_4, r_temp_4, 32
	}
	j	get_offset_loop

read_crc_get_offset_done:

	bfextu	r_temp_4, udn0, 0, 31





get_offset_done:
	// Finally, check the CRC.
	nor	r_temp_0, r_temp_0, zero
	bfextu	r_temp_0, r_temp_0, 0, 31
	cmpeq	r_temp_0, r_temp_0, r_temp_4
	{
	 blbc	r_temp_0, bad_trailer
	 move	r0, r_srom_shim_addr
	}

	//
	// Get the SROM device cookie.
	//
	jal	srom_get_dev
	{
	 move	r_srom_dev, r0
	 move	r1, r0
	}

	//
	// Load all of the image headers, check them for validity.  If they're
	// invalid we clear the corresponding entry in the offset table.
	//
	{
	 move	r_temp_2, r_nimages
	 move	r_n_val_images, r_nimages
	}
	{
	 move	r_cur_img_offp, r_image_offsets
	 move	r_cur_img_hdrp, r_image_headers
	}
load_hdr_loop:
	// get offset

	{
	 ld4u	r_cur_img_srom, r_cur_img_offp
	 move	r0, r_srom_shim_addr
	}
	{
	 move	r2, r_cur_img_srom
	 jal	srom_set_addr
	}

	// Read header up to the CRC.
	{
	 move	r_srom_dest_addr, r_cur_img_hdrp
	 moveli	r_srom_wds, (SIH_HEADER_CRC_OFF / 4)
	}
	{
	 movei	r_srom_crc, 0
	 movei	r_srom_dest_incr, 4
	}
	jal	srom_rd

	// Save the calculated CRC to compare to the real one below.
	{
	 move	r_temp_0, r_srom_crc
	 addi	r_temp_1, r_cur_img_hdrp, SIH_HEADER_CRC_OFF
	}

	// Read the CRC.
	{
	 moveli	r_srom_wds, 1
	 jal	srom_rd
	}

	// Load the CRC.
	ld4u	r_temp_1, r_temp_1

	// If it's not valid, mark the image as bad.
	cmpeq	r_temp_1, r_temp_0, r_temp_1
	blbc	r_temp_1, bad_header

	// Load the magic number; if it's not valid, mark the image as bad.
	{
	 addi	r_temp_0, r_cur_img_hdrp, SIH_MAGIC_OFF
	 moveli	r_temp_1, hw1_last(SROM_IMAGE_MAGIC)
	}
	{
	 ld4u	r_temp_0, r_temp_0
	 shl16insli	r_temp_1, r_temp_1, hw0(SROM_IMAGE_MAGIC)
	}

	cmpeq	r_temp_1, r_temp_0, r_temp_1
	blbs	r_temp_1, next_header

bad_header:
	{
	 st4	r_cur_img_offp, zero
	 addi	r_n_val_images, r_n_val_images, -1
	}

next_header:
	// Done with this image, back up to the top of the loop for another.
	{
	 addi	r_temp_2, r_temp_2, -1
	 addi	r_cur_img_offp, r_cur_img_offp, 4
	}
	{
	 addli	r_cur_img_hdrp, r_cur_img_hdrp, SIH_SIZE
	 bgtzt	r_temp_2, load_hdr_loop
	}

	//
	// Choose an image to boot from the list of available images.
	// If we later find that image is bogus, we clear out its offset and
	// come back here to pick another one.
	//
choose_image:
	{
	 move	r_temp_2, r_nimages
	 move	r_cur_img_offp, r_image_offsets
	}
	{
	 move	r_cur_img_hdrp, r_image_headers
	 move	r_tgt_img_srom, zero
	}
	{
	 move	r_tgt_img_offp, zero
	 move	r_tgt_img_hdrp, zero
	}
	// temp_3 is the highest generation number we've seen.
	movei	r_temp_3, -1

choose_image_loop:
	//
	// Get the offset; if zero, skip this one.  Otherwise, load the
	// generation; if it's newer than the current one, set the target
	// pointers to this image.
	//
	ld4u	r_cur_img_srom, r_cur_img_offp
	{
	 beqz	r_cur_img_srom, 0f
	 addi	r_temp_0, r_cur_img_hdrp, SIH_GENERATION_OFF
	}

	ld4u	r_temp_0, r_temp_0
	sub	r_temp_1, r_temp_0, r_temp_3
	blez	r_temp_1, 0f

	{
	 move	r_temp_3, r_temp_0
	 move	r_tgt_img_srom, r_cur_img_srom
	}
	{
	 move	r_tgt_img_offp, r_cur_img_offp
	 move	r_tgt_img_hdrp, r_cur_img_hdrp
	}
0:
	//
	// Point to the next candidate image and go around again.
	//
	{
	 addi	r_temp_2, r_temp_2, -1
	 addi	r_cur_img_offp, r_cur_img_offp, 4
	}
	{
	 addli	r_cur_img_hdrp, r_cur_img_hdrp, SIH_SIZE
	 bgtzt	r_temp_2, choose_image_loop
	}

	//
	// If we didn't get a valid image, punt.
	//
	bnez	r_tgt_img_srom, found_image
	PUNT(BOOT_ERR_SROM_NO_VALID_IMAGE)

found_image:
	//
	// If we only have one image, we're just going to load it without
	// checksumming it first.  We do this for two reasons:
	//
	// - If the image is good, checksumming it first doubles the load
	//   time.  This is reasonable if doing so could prevent us from
	//   loading something that's corrupt, but since in this case we
	//   don't have any other choices, it's not quite so compelling.
	//
	// - If the image is bad, it's entirely possible that the loaded
	//   code will do a better job reporting the error than we will,
	//   since we'll just hang in a way which you could discover
	//   with a debugger.  All of our images have embedded checksums
	//   which are verified by the boot process, and if the failure
	//   is relatively late, the detector will have brought up the
	//   serial port or other console and will be able to provide a
	//   coherent error message.
	//
	cmpeqi	r_temp_0, r_n_val_images, 1
	{
	 blbs	r_temp_0, load_image
	 move	r0, r_srom_shim_addr
	}
	//
	// We have more than one, so we'll first checksum the L0 boot part of
	// the image without loading it.
	//
	{
	 addi	r2, r_tgt_img_hdrp, SIH_OFFSET_OFF
	 movei	r_srom_crc, 0
	}
	{
	 ld4u	r2, r2
	 addi	r_srom_wds, r_tgt_img_hdrp, SIH_L0_BOOT_WORDS_OFF
	}
	{
	 add	r2, r2, r_tgt_img_srom
	 jal	srom_set_addr
	}
	{
	 ld4u	r_srom_wds, r_srom_wds
	 move	r_srom_dest_addr, r_seg_header
	 move	r_srom_dest_incr, zero
	}
	shli	r_srom_wds, r_srom_wds, 1
	{
	 jal	srom_rd
	 addi	r_temp_0, r_tgt_img_hdrp, SIH_L0_BOOT_CRC_OFF
	}
	{
	 ld4u	r_temp_0, r_temp_0
	}

	//
	// If the image turned out to have a bad checksum, we mark it as
	// invalid and go through the whole image choice loop again.
	//
	cmpeq	r_temp_0, r_temp_0, r_srom_crc
	blbs	r_temp_0, good_image

	st4	r_tgt_img_offp, zero
	{
	 j	choose_image
	 addi	r_n_val_images, r_n_val_images, -1
	}

good_image:
	//
	// If we're booting an alternate image, skip this one by marking
	// it invalid and going through the image choice loop again.  Note
	// that we clear the alternate image flag, since it's had its effect.
	//
	beqzt	r_altimage, load_image
	{
	 st4	r_tgt_img_offp, zero
	 move	r_altimage, zero
	}
	{
	 j	choose_image
	 addi	r_n_val_images, r_n_val_images, -1
	}

load_image:













































































































































	//
	// Load in the first part of the image (probably the L0.5 booter)
	// and jump to it.
	//
	{
	 move	r0, r_srom_shim_addr
	 addi	r2, r_tgt_img_hdrp, SIH_OFFSET_OFF
	}
	{
	 ld4u	r2, r2
	 movei	r_srom_dest_incr, 4
	}
	{
	 add	r2, r2, r_tgt_img_srom
	 jal	srom_set_addr
	}

l0_next_segment:
	move	r_srom_dest_addr, r_seg_header
	{
	 movei	r_srom_wds, 4
	 jal	srom_rd
	}

	{
	 // Get length.
	 ld	r_temp_1, r_seg_header
	 // Get address.
	 addi	r_srom_dest_addr, r_seg_header, 8
	}
	{
	 ld	r_srom_dest_addr, r_srom_dest_addr
	 // Compute 4-byte words to read.  We save r_temp_1 for the
	 // icoh loop below.
	 shli	r_srom_wds, r_temp_1, 1
	}











	// Read in the segment.
	{
	 jal	srom_rd
	 // Save for icoh loop below.
	 move	r_temp_0, r_srom_dest_addr
	}









	//
	// icoh the segment we just read, since it probably overlaps the
	// level 0.5 booter which ran before we did.
	//
	mf
	drain
0:
	{
	 icoh	r_temp_0
	 addi	r_temp_1, r_temp_1, -1
	}
	{
	 bgez	r_temp_1, 0b
	 addi	r_temp_0, r_temp_0, 8
	}
	drain

	//
	// Get jump-to address, and see if it's zero, which is the magic "do
	// another segment" address; if so, we have another segment, so go
	// load it.
	//
	{
	 move	r_srom_dest_addr, r_seg_header
	 movei	r_srom_wds, 2
	}
	jal	srom_rd

	ld	r_temp_0, r_seg_header
	beqz	r_temp_0, l0_next_segment

l0_copy_done:
	//
	// Save a few of our registers so that we can get them back in
	// srom_load() if we need to.
	//

	moveli	r_temp_3, hw2_last(save_base)
	shl16insli r_temp_3, r_temp_3, hw1(save_base)
	shl16insli r_temp_3, r_temp_3, hw0(save_base)
	{
	 st	r_temp_3, r_nimages
	 addi	r_temp_3, r_temp_3, 8
	}
	{
         st	r_temp_3, r_n_val_images
	 addi	r_temp_3, r_temp_3, 8
	}
	{
         st	r_temp_3, r_image_offsets
	 addi	r_temp_3, r_temp_3, 8
	}
	{
         st	r_temp_3, r_image_headers
	 addi	r_temp_3, r_temp_3, 8
	}
	{
         st	r_temp_3, r_tgt_img_offp
	 addi	r_temp_3, r_temp_3, 8
	}























	//
	// Set up arguments for the loaded code and jump to it.
	//

	{
	 movei	r50, SROMBOOT_SROMBOOT
	 cmpltsi r_temp_3, r_n_val_images, 2
	}
	{
	 blbs	r_temp_3, 0f
	 move	r51, zero
	}
	ori	r50, r50, SROMBOOT_BADCRC_REBOOT
0:
	moveli	r52, hw2_last(srom_load)
	{
	 shl16insli r52, r52, hw1(srom_load)
	 addi	r_temp_0, r_temp_0, 8
	}
	{
	 shl16insli r52, r52, hw0(srom_load)
	 jr	r_temp_0
	}

	.size _srom_start,.-_srom_start

//
// This routine loads code from the SROM into memory, verifies its CRC, and
// then jumps to it.
//
// Note that we do not touch r0-r9; those may be set by our caller,
// and we want to pass those straight through to the code we've loaded.
//
	.global srom_load
	.type srom_load,@function
srom_load:







	{
	 moveli	r_seg_header, hw2_last(seg_header)
	 movei	r_srom_dest_incr, 4
	}
	{
	 mfspr	r_srom_shim_addr, RSHIM_COORD
	 shl16insli r_seg_header, r_seg_header, hw1(seg_header)
	}
	{
	 move	r0, r_srom_shim_addr
	 jal	srom_get_dev
	}
	move	r_srom_dev, r0
	{
	 movei	r_srom_crc, 0
	 shl16insli r_seg_header, r_seg_header, hw0(seg_header)
	}

next_segment:
	move	r_srom_dest_addr, r_seg_header
	{
	 movei	r_srom_wds, 4
	 jal	srom_rd
	}

	{
	 // Get length.
	 ld	r_temp_3, r_seg_header
	 // Get address.
	 addi	r_temp_2, r_seg_header, 8
	}
	{
	 ld	r_temp_2, r_temp_2
	 // Save CRC.
	 move	r_temp_0, r_srom_crc
	 // Get CRC.
	 move	r_srom_dest_addr, r_seg_header
	}
	{
	 movei	r_srom_wds, 2
	 jal	srom_rd
	}

	//
	// Check CRC.
	//
	ld	r_temp_1, r_seg_header
	cmpeq	r_temp_1, r_temp_0, r_temp_1
	blbc	r_temp_1, bad_crc

	//
	// If the length is zero, the address is our destination PC.
	// Otherwise, load the segment and go around again.
	//
	beqz	r_temp_3, copy_done
	{
	 move	r_srom_dest_addr, r_temp_2
	 shli	r_srom_wds, r_temp_3, 1
	}
	{
	 move	r_srom_crc, r_temp_0
	 jal	srom_rd
	}

	//
	// icoh the words we just loaded since we may be replacing the code
	// which called us.
	//
	mf
	drain
0:
	{
	 icoh	r_temp_2
	 addi	r_temp_3, r_temp_3, -1
	}
	{
	 bgez	r_temp_3, 0b
	 addi	r_temp_2, r_temp_2, 8
	}
	drain

	j	next_segment

copy_done:

	jr	r_temp_2













bad_trailer:
	//
	// Got a bad trailer, die.  There's nothing we can do here.
	//
	PUNT(BOOT_ERR_SROM_BAD_TRAILER)

bad_crc:
	//
	// We got a bad CRC.  First restore our state from the previous
	// attempt at finding a bootable image.
	//
	moveli	r_temp_0, hw2_last(save_base)
	shl16insli r_temp_0, r_temp_0, hw1(save_base)
	{
	 shl16insli r_temp_0, r_temp_0, hw0(save_base)
	 move	r1, r_srom_dev
	}
	{
	 ld	r_nimages, r_temp_0
	 addi	r_temp_0, r_temp_0, 8
	}
	{
         ld	r_n_val_images, r_temp_0
	 addi	r_temp_0, r_temp_0, 8
	}
	{
         ld	r_image_offsets, r_temp_0
	 addi	r_temp_0, r_temp_0, 8
	}
	{
         ld	r_image_headers, r_temp_0
	 addi	r_temp_0, r_temp_0, 8
	}
	{
         ld	r_tgt_img_offp, r_temp_0
	 addi	r_temp_0, r_temp_0, 8
	 cmpltsi r_temp_1, r_n_val_images, 2
	}

	//
	// If there was only 1 image before, we die.
	//
	blbc	r_temp_1, 0f
	PUNT(BOOT_ERR_SROM_BAD_CRC)
0:
	//
	// There were two or more images, so invalidate the one we used
	// and go search again.
	//
	{
	 st4	r_tgt_img_offp, zero
	 move	r_altimage, zero
	}
	{
	 j	choose_image
	 addi	r_n_val_images, r_n_val_images, -1
	}

	.size srom_load,.-srom_load

	//
	// Do a config read from the rshim.
	// @param r0 Register address.
	// @param r_srom_shim_addr rshim address.
	// @return r0 Data read from the target register.
	// @destroys r0, r1, r2
	//
	.section .text.rsh_cfg_rd, "ax"
	.type rsh_cfg_rd,@function
rsh_cfg_rd:
	{
	 movei	r1, SPR_AAR__PHYSICAL_MEMORY_MODE_MASK
	 bfextu	r2, r_srom_shim_addr, SPR_RSHIM_COORD__Y_COORD_FIELD
	}
	{
	 bfins	r1, r2, \
		SPR_AAR__LOCATION_Y_OR_PAGE_OFFSET_FIELD
	 movei	r2, SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO
	}
	{
	 bfins	r1, r2, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}
	{
	 bfextu	r2, r_srom_shim_addr, SPR_RSHIM_COORD__X_COORD_FIELD
	}
	{
	 bfins	r1, r2, \
		SPR_AAR__LOCATION_X_OR_PAGE_MASK_FIELD
	 mfspr	r2, AAR
	}
	mtspr	AAR, r1
	nop     // must delay while AAR change takes effect
	ld	r0, r0
	mtspr	AAR, r2
	jrp	lr

	.size rsh_cfg_rd,.-rsh_cfg_rd

	//
	// Do a config write to the rshim.
	// @param r0 Register address.
	// @param r1 Register value.
	// @param r_srom_shim_addr rshim address.
	// @destroys r2, r3
	//
	.section .text.rsh_cfg_wr, "ax"
	.type rsh_cfg_wr,@function
rsh_cfg_wr:
	{
	 movei	r2, SPR_AAR__PHYSICAL_MEMORY_MODE_MASK
	 bfextu	r3, r_srom_shim_addr, SPR_RSHIM_COORD__Y_COORD_FIELD
	}
	{
	 bfins	r2, r3, \
		SPR_AAR__LOCATION_Y_OR_PAGE_OFFSET_FIELD
	 movei	r3, SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO
	}
	{
	 bfins	r2, r3, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	}
	{
	 bfextu	r3, r_srom_shim_addr, SPR_RSHIM_COORD__X_COORD_FIELD
	}
	{
	 bfins	r2, r3, \
		SPR_AAR__LOCATION_X_OR_PAGE_MASK_FIELD
	 mfspr	r3, AAR
	}
	mtspr	AAR, r2
	nop     // must delay while AAR change takes effect
	st	r0, r1
	mtspr	AAR, r3
	jrp	lr

	.size rsh_cfg_wr,.-rsh_cfg_wr
































	PUNT_ROUTINE

	.data
	.align	4

image_offsets:
	//
	// Image offsets, from our trailer.  We change an offset to zero if
	// we decide that the image header or the L0-bootable code in the
	// image is bad, so this is also the "valid images" table.
	//
	.skip	4 * MAX_IMAGES

image_headers:
	//
	// Image headers.
	//
	.skip	SIH_SIZE * MAX_IMAGES

seg_header:
	//
	// Segment header, used while reading in boot code.  (The 3 words are
	// the length, address, and CRC of the segment.)
	//
	.skip	3 * 8

save_base:
	//
	// Register save area for things we might need if srom_load finds
	// a CRC failure.
	//
	.skip	5 * 8
