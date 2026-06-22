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
// Initial hypervisor startup.
//
// When the hypervisor is loaded, this is the first code that executes.  The
// L1 booter has loaded us into memory at the end of memory shim 0, and has
// set things up as follows:
//
// - Cache-as-RAM mode is disabled.
//
// - Data physical memory mode is still on, but caching is disabled in the
//   AAR.
//
// - Instruction physical memory mode is still on.
//
// - At least one memory shim is initialized, and the mapping registers on our
//   tile are set such that we can access memory on that memory shim.  (If
//   we'rd doing memory striping, all shims are initialized, and all mapping
//   registers are set.)
//
// - Registers are set as follows:
//
//   r0    Physical base address of the text segment.
//   r1    First address used by L1 boot code (for L1I$ flush).
//   r2    Last address used by L1 boot code (for L1I$ flush).
//   r3    Upper-left-hand corner tile coordinate.
//   r4    Lower-right-hand corner tile coordinate.
//   r5    Master tile coordinate.
//
//   On the boot tile only:
//
//   r6    Rshim coordinate (~0 if not found).
//   r7    Shim present mask.
//   r8    Board flags word.
//
// What we need to do before we can start executing hypervisor C code is
// the following:
//
// - Clean the L1 booter out of the cache.
//
// - Reenable caching (just to make the rest of the process run more quickly).
//
// - Initialize our tile-specific data area, by copying the read-only and
//   read-write initialized data there from our initial memory image, and
//   zeroing the BSS.
//
// - Add a locked TTE to the ITLB to map our code, and one to the DTLB to map
//   our data.
//
// - Disable instruction and data physical memory modes to enable the DTLB and
//   ITLB.
//
// - Point to an initial stack.
//

#include <arch/chip.h>
#include <arch/interrupts.h>
#include <arch/sim.h>
#include <arch/spr_def.h>

#include "asm_inc.h"
#include "hv.h"
#include "hw_config.h"
#include "page.h"
#include "param.h"
#include "tte.h"

	.section .text._start, "ax"

//
// Hypervisor memory layout.
//
// The Hypervisor is located at the end of memory of the shim it's on, so the
// physical addresses vary with the amount of memory present.  This example is
// based on 256M (0x1000000):
//
// Physical                   Virtual
// 0x000FF00000-0x000FFFFFFF  0xFFFFFFFE00000000-0xFFFFFFFE000FFFFF
//                            Hypervisor code, plus initial data area prototype
// 0x000FE00000-0x000FEFFFFF  0xFFFFFFFE00100000-0xFFFFFFFE001FFFFF
//                            Hypervisor data, tile 0
// 0x000FD00000-0x000FDFFFFF  0xFFFFFFFE00100000-0xFFFFFFFE001FFFFF
//                            Hypervisor data, tile 1
// .                          .
// .                          .
// .                          .
// 0x0009A00000-0x0009AFFFFF  0xFFFFFFFE00100000-0xFFFFFFFE001FFFFF
//                            Hypervisor data, tile 99
// 0x??????????-0x00099FFFFF  0xFFFFFFFE00200000-(dynamic)
//                            Hypervisor filesystem data
// 0x0000000000-0x000FF00000  N/A
//                            Client memory
//
// Note that the stack is at the start of each hypervisor data area.
// The stack pointer is initialized to 0xFFFFFFFE0010F000, and so the stack
// will cause a TLB fault if it overflows, rather than corrupting data.

	.global _start
	.type _start,@function
_start:
	// Tell the simulator that we are done booting, and it is ok to
	// turn on functional mode. This spr write is ignored on tile-sim if
	// you don't invoke tile-sim with --functional.
	movei	r10, SIM_CONTROL_ENABLE_FUNCTIONAL_BARRIER
	mtspr	SPR_SIM_CONTROL, r10

#if defined (__BIG_ENDIAN__)
        // Set the processor to big-endian mode.
	movei	r10, 1
	mtspr	SPR_BIG_ENDIAN_CONFIG, r10
#endif
        
	// Clean the L1 booter out of the cache.
0:
	inv	r1
	{
	 icoh	r1
	 addi	r1, r1, CHIP_INV_STRIDE()
	}
	cmpltu	r10, r1, r2
	blbs	r10, 0b
	drain

	// Configure for local caching.
	{
	 mfspr  r10, AAR
	 movei  r11, SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT
	}
	{
	 bfins  r10, r11, SPR_AAR__MEMORY_ATTRIBUTE_FIELD
	 movei  r11, SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE
	}
	{
	 bfins  r10, r11, SPR_AAR__CACHE_HOME_MAPPING_FIELD
	 mfspr	r12, TILE_COORD
	}
	bfextu	r11, r12, SPR_TILE_COORD__X_COORD_FIELD
	bfins	r10, r11, SPR_AAR__LOCATION_X_OR_PAGE_MASK_FIELD
	bfextu	r11, r12, SPR_TILE_COORD__Y_COORD_FIELD
	bfins	r10, r11, SPR_AAR__LOCATION_Y_OR_PAGE_OFFSET_FIELD
	mtspr   AAR, r10
	mf
	drain

	// Set MPLs to the HV's level early, before we drop our own PL.
	// Some of these will be adjusted again later to match the
	// client's PL.

#define _SET_MPL(name, mpl) mtspr MPL_ ## name ## _SET_ ## mpl, r10
#define SET_MPL(name, mpl) _SET_MPL(name, mpl)
#define HV_MPL(name) SET_MPL(name, HV_PL)

	movei	r10, 1
	HV_MPL(MEM_ERROR)
	HV_MPL(SINGLE_STEP_3)
	HV_MPL(SINGLE_STEP_2)
	HV_MPL(SINGLE_STEP_1)
	HV_MPL(SINGLE_STEP_0)

	HV_MPL(IDN_COMPLETE)
	HV_MPL(UDN_COMPLETE)

	HV_MPL(ITLB_MISS)
	HV_MPL(ILL)
	HV_MPL(ILL_TRANS)
	HV_MPL(GPV)

	HV_MPL(IDN_ACCESS)
	HV_MPL(UDN_ACCESS)

	HV_MPL(SWINT_3)
	HV_MPL(SWINT_2)
	HV_MPL(SWINT_1)
	HV_MPL(SWINT_0)
	HV_MPL(UNALIGN_DATA)
	HV_MPL(DTLB_MISS)
	HV_MPL(DTLB_ACCESS)

	HV_MPL(IDN_FIREWALL)
	HV_MPL(UDN_FIREWALL)

	HV_MPL(TILE_TIMER)
	HV_MPL(AUX_TILE_TIMER)

	HV_MPL(IDN_TIMER)
	HV_MPL(UDN_TIMER)
	HV_MPL(IDN_AVAIL)
	HV_MPL(UDN_AVAIL)

	HV_MPL(IPI_3)
	HV_MPL(IPI_2)
	HV_MPL(IPI_1)
	HV_MPL(IPI_0)
	HV_MPL(PERF_COUNT)
	HV_MPL(AUX_PERF_COUNT)
	HV_MPL(INTCTRL_3)
	HV_MPL(INTCTRL_2)
	HV_MPL(INTCTRL_1)
	HV_MPL(INTCTRL_0)
	HV_MPL(BOOT_ACCESS)
	HV_MPL(WORLD_ACCESS)
	HV_MPL(I_ASID)
	HV_MPL(D_ASID)
	HV_MPL(DOUBLE_FAULT)

	// Mask any interrupts that might have been unmasked by the booter.
	movei	r10, -1
	mtspr	SPR_INTERRUPT_MASK_0, r10
	mtspr	SPR_INTERRUPT_MASK_1, r10
	mtspr	SPR_INTERRUPT_MASK_2, r10
	mtspr	SPR_INTERRUPT_MASK_3, r10

	// Zero the wired tlb registers because their reset value is 4.
	mtspr	WIRED_ITLB, zero
	mtspr	WIRED_DTLB, zero

        // Convert our tile coordinate to a tile index, using the ULHC
        // and LRHC which were passed to us.

	{
	 mfspr	r12, TILE_COORD
         // Make r13 our 0,0-based LRHC; its x is thus our width-1.
         sub    r13, r4, r3
	}
	{
         // Make r10 our 0,0-based position.
         sub    r10, r12, r3
	 // Get our width-1 into r13.
	 bfextu	r13, r13, SPR_TILE_COORD__X_COORD_FIELD
	}
	{
	 // Get our y into r12.
	 bfextu	r12, r10, SPR_TILE_COORD__Y_COORD_FIELD
         // Add 1 to width-1 to get the width into r13.
         addi   r13, r13, 1
	}
	{
         // Compute y * width into r12.
         mul_lu_lu r12, r12, r13
	}
	{
	 // Get our x into r11.
	 bfextu	r11, r10, SPR_TILE_COORD__X_COORD_FIELD
	}
	{
         // Compute x + y * width into r10; this is our final tile index.
         add    r10, r11, r12
	}

#if 0 // FIXME: GX: debug stub not yet ported
	// Enable the debug stub here.
	// This overwrites the contents of r16 - r29.
	jal	hvgdb_init
#endif

	// Set the hypervisor PL to the desired value.
	{
	 movei	r12, HV_PL
	 lnk	r13
	}
1:	{
	 addi	r13, r13, (2f - 1b)
	 mtspr	EX_CONTEXT_3_1, r12
	}
	mtspr	EX_CONTEXT_3_0, r13

	iret
2:

	// Figure out the physical address where our tile-specific data goes.

	// The tile number is 0 based.	This is used to generate the
	// data page offset from the text page, with the first tile's
	// data page being one page before the text page, so we need a
	// one based tile number.
	addi	r10, r10, 1
	shli	r10, r10, HV_DATA_PAGE_SHIFT	// r10 is our data offset.

	// Get a bunch of symbols we're going to need below.

	{
	 moveli	r11, hw2_last(_sdata_phys)	// Start of physical data area.
	 moveli	r12, hw2_last(_edata_phys)	// End of physical data area.
	}
	{
         shl16insli r11, r11, hw1(_sdata_phys)
         shl16insli r12, r12, hw1(_edata_phys)
	}
	{
         shl16insli r11, r11, hw0(_sdata_phys)
         shl16insli r12, r12, hw0(_edata_phys)
	}
	{
	 moveli	r13, hw2_last(_sbss)		// Start of BSS area.
	 moveli	r14, hw2_last(_ebss)		// End of BSS area.
	}
	{
         shl16insli r13, r13, hw1(_sbss)
         shl16insli r14, r14, hw1(_ebss)
	}
	{
         shl16insli r13, r13, hw0(_sbss)
         shl16insli r14, r14, hw0(_ebss)
	}
	{
	 moveli	r15, hw2_last(_sdata)		// Start of data area.
	 moveli	r16, hw2_last(_stext)		// Start of text area.
	}
	{
         shl16insli r15, r15, hw1(_sdata)
         shl16insli r16, r16, hw1(_stext)
	}
	{
         shl16insli r15, r15, hw0(_sdata)
         shl16insli r16, r16, hw0(_stext)
	}
	{
	 moveli	r17, hw2_last(_stext_phys)	// Start of physical text area
	 moveli	r18, hw2_last(_sstack)		// Start of stack area
	}
	{
         shl16insli r17, r17, hw1(_stext_phys)
         shl16insli r18, r18, hw1(_sstack)
	}
	{
         shl16insli r17, r17, hw0(_stext_phys)
         shl16insli r18, r18, hw0(_sstack)
	}


	// Add the physical text segment base address to the addresses that
	// need it.
	{
	 add	r11, r11, r0			// _sdata_phys
	 add	r12, r12, r0			// _edata_phys
	}
	{
	 add	r17, r17, r0			// _stext_phys
	 sub	r10, r0, r10			// data page
	}

	// Register usage:
	//  r0	 Physical address of our text segment.
	//  r10	 Physical address of our data segment.
	//  r11	 _sdata_phys (adjusted)
	//  r12	 _edata_phys (adjusted)
	//  r17	 _stext_phys (adjusted)
	//  r13	 _sbss
	//  r14	 _ebss
	//  r15	 _sdata
	//  r16	 _stext
	//  r18	 _sstack

	// Copy read-only and read-write initialized data to our data area.

	{
	 // _sdata_phys
	 move	r20, r11		// Source pointer - _sdata_phys
	 // Our data segment
	 sub	r21, r15, r18		// Offset of data in data page
	}
	add	r21, r21, r10		// Destination pointer

0:
	{
	 ld	r30, r20
	 addi	r20, r20, 8
	}
	{
	 st	r21, r30
	 addi	r21, r21, 8
	 cmpltu	r31, r20, r12
	}
	blbst	r31, 0b

	// Zero out our BSS.

	sub	r20, r13, r18		// BSS offset within data page
	{
	 add	r20, r20, r10		// BSS address
	 sub	r21, r14, r13		// BSS length
	}

0:
	{
	 st	r20, zero
	 addi	r20, r20, 8
	 addi	r21, r21, -8
	}
	{
	 bgtzt	r21, 0b
	}

	//
	// Add a locked TTE to the ITLB to map our code.
	//
#define ITTE_ATTR \
	(1 << SPR_ITLB_CURRENT_ATTR__V_SHIFT) | \
	(HV_PL << SPR_ITLB_CURRENT_ATTR__MPL_SHIFT) | \
	(TTE_SHIFT_TO_PS(HV_CODE_PAGE_SHIFT) << \
	 SPR_ITLB_CURRENT_ATTR__PS_SHIFT) | \
	(1 << SPR_ITLB_CURRENT_ATTR__G_SHIFT) | \
	(SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT << \
	 SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_SHIFT) | \
	(SPR_ITLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE << \
	 SPR_ITLB_CURRENT_ATTR__CACHE_HOME_MAPPING_SHIFT)

	{
	 // We'll put the new TTE in the first unwired entry.
	 mfspr	r40, WIRED_ITLB
	 // Start building the attribute word.
	 moveli	r41, hw3_last(ITTE_ATTR)
	}
	{
	 mtspr	ITLB_INDEX, r40
	 // Bump the number of wired entries by 1.
	 addi	r40, r40, 1
	}
	{
	 mtspr	WIRED_ITLB, r40
	 // Continue building the attribute word.
	 shl16insli r41, r41, hw2(ITTE_ATTR)
	}
	{
	 movei	r40, 1
	 // Continue building the attribute word.
	 shl16insli r41, r41, hw1(ITTE_ATTR)
	}
	{
	 // Get our LOTAR to stuff into the TLB entry.
	 mfspr   r42, TILE_COORD
	 // Finish building the attribute word.
	 shl16insli r41, r41, hw0(ITTE_ATTR)
	}

	// Get the Y coordinate from our position.
	bfextu	r43, r42, SPR_TILE_COORD__Y_COORD_FIELD
	// Add to the attribute word.
	bfins	r41, r43, SPR_ITLB_CURRENT_ATTR__LOCATION_Y_OR_PAGE_OFFSET_FIELD

	{
	 // Get the X coordinate from our position.
	 bfextu	r43, r42, SPR_TILE_COORD__X_COORD_FIELD
	 // Set up the PA for the entry.
	 mtspr	ITLB_CURRENT_PA, r17		// pfn is _stext_phys.
	}
	{
	 // Add the X coordinate to the attribute word.
	 bfins	r41, r43, SPR_ITLB_CURRENT_ATTR__LOCATION_X_OR_PAGE_MASK_FIELD
	 // Set up the VA for the entry.
	 mtspr	ITLB_CURRENT_VA, r16		// vpn is _stext.
	}

	// Store the attribute word, inserting the new ITTE.
	mtspr	ITLB_CURRENT_ATTR, r41

	//
	// Now we need to add a temporary TTE to the ITLB to allow us to enable
	// it.  Right now, the PC is the physical address of the code we're
	// executing; we need it to eventually be the virtual address of that
	// code.  It's impossible for us to both enable the ITLB and change the
	// PC from physical to virtual all at once.  Thus, we add an ITLB entry
	// which maps the physical addresses we're currently using as a virtual
	// address to those same physical addresses.  Once that's done, we can
	// enable the ITLB safely.  Then, once it's on, we can jump to the
	// virtual address we want to use, and finally flush that extra
	// identity mapping out of the ITLB.
	//

	//
	// This new entry is only going to be around for a very short time,
	// so we'll stuff it in the first non-wired entry, and not bother
	// to wire it down.  We won't add any other entries, so when we go
	// to remove it, ITLB_INDEX will still be pointing at it; all we
	// have to do is write a zero into the attribute register.
	//
	mfspr	r40, WIRED_ITLB
	mtspr	ITLB_INDEX, r40

	// Set up the PA for the entry.
	mtspr	ITLB_CURRENT_PA, r0
	// Set up the VA for the entry.
	mtspr	ITLB_CURRENT_VA, r0
	// Store the attribute word, inserting the new ITTE.
	mtspr	ITLB_CURRENT_ATTR, r41

	//
	// Add a locked TTE to the DTLB to map our data.
	//
#define DTTE_ATTR \
	(1 << SPR_ITLB_CURRENT_ATTR__V_SHIFT) | \
	(1 << SPR_ITLB_CURRENT_ATTR__W_SHIFT) | \
	(HV_PL << SPR_ITLB_CURRENT_ATTR__MPL_SHIFT) | \
	(TTE_SHIFT_TO_PS(HV_DATA_PAGE_SHIFT) << \
	 SPR_ITLB_CURRENT_ATTR__PS_SHIFT) | \
	(1 << SPR_ITLB_CURRENT_ATTR__G_SHIFT) | \
	(SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT << \
	 SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_SHIFT) | \
	(SPR_ITLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE << \
	 SPR_ITLB_CURRENT_ATTR__CACHE_HOME_MAPPING_SHIFT)


	{
	 // We'll put the new TTE in the first unwired entry.
	 mfspr	r40, WIRED_DTLB
	 // Start building the attribute word.
	 moveli	r41, hw3_last(DTTE_ATTR)
	}
	{
	 mtspr	DTLB_INDEX, r40
	 // Bump the number of wired entries by 1.
	 addi	r40, r40, 1
	}
	{
	 mtspr	WIRED_DTLB, r40
	 // Continue building the attribute word.
	 shl16insli r41, r41, hw2(DTTE_ATTR)
	}
	{
	 movei	r40, 1
	 // Continue building the attribute word.
	 shl16insli r41, r41, hw1(DTTE_ATTR)
	}
	{
	 // Get our LOTAR to stuff into the TLB entry.
	 mfspr   r42, TILE_COORD
	 // Finish building the attribute word.
	 shl16insli r41, r41, hw0(DTTE_ATTR)
	}

	// Get the Y coordinate from our position.
	bfextu	r43, r42, SPR_TILE_COORD__Y_COORD_FIELD
	// Add to the attribute word.
	bfins	r41, r43, SPR_DTLB_CURRENT_ATTR__LOCATION_Y_OR_PAGE_OFFSET_FIELD

	{
	 // Get the X coordinate from our position.
	 bfextu	r43, r42, SPR_TILE_COORD__X_COORD_FIELD
	 // Set up the PA for the entry.
	 mtspr	DTLB_CURRENT_PA, r10		// pfn is our data page.
	}
	{
	 // Add the X coordinate to the attribute word.
	 bfins	r41, r43, SPR_DTLB_CURRENT_ATTR__LOCATION_X_OR_PAGE_MASK_FIELD
	 // Set up the VA for the entry.
	 mtspr	DTLB_CURRENT_VA, r18		// vpn is _sstack.
	}

	// Store the attribute word, inserting the new DTTE.
	mtspr	DTLB_CURRENT_ATTR, r41

	// Disable data physical memory mode to enable the DTLB.  Also clear
	// the ASID, although since our hardwired pages are global it doesn't
	// much matter what they are.  Do the same for the ITLB.  Finally,
	// jump to subsequent code in our final VA range.

        {
	 mtspr	D_ASID, zero
	 moveli	r50, hw2_last(flush_temp_tte)
        }
	{
         mtspr   AAR, zero
         shl16insli r50, r50, hw1(flush_temp_tte)
	}
	{
	 mtspr	I_ASID, zero
         shl16insli r50, r50, hw0(flush_temp_tte)
	}
	mtspr	I_AAR, zero
	mf
	drain

	jr	r50

	// We're now executing from our final VA range.	 Set up the interrupt
	// base register so our code handles interrupts properly; flush the
	// temporary identity ITTE we added above, since we don't need it
	// anymore; and set up our stack and parameters.  Note that we need to
	// move some of the parameters down one register since we don't pass
	// the boot code start and end values to the main program.

flush_temp_tte:
	{
	 mtspr	INTERRUPT_VECTOR_BASE_HV, r16
	 moveli	sp, hw2_last(_estack - C_ABI_SAVE_AREA_SIZE)
	}
	{
	 mtspr	ITLB_CURRENT_ATTR, zero
         shl16insli sp, sp, hw1(_estack - C_ABI_SAVE_AREA_SIZE)
	}
	{
         shl16insli sp, sp, hw0(_estack - C_ABI_SAVE_AREA_SIZE)
	}
	{
	 move	r1, r10		// Data page physaddr
	 move	r2, r3
	}
	{
	 move	r3, r4
	 move	r4, r5
	}
	{
	 move	r5, r6
	 move	r6, r7
	}

	// Call the main program; if it returns, fall through to _exit.
	{
	 jal	hv
	 move	r7, r8
	}

	// Exit with appropriate return code.  We can fall through here from
	// the call above because of the fact that a function's first parameter
	// is stored in the same register as its return value.

	// The "mtspr SIM_CONTROL, r20" shuts down the simulator.



	// The "j 1b" makes the hardware loop forever.

	.global	_exit
_exit:
	{
	 movei	r20, SIM_CONTROL_SHUTDOWN
	 beqz	r0, 0f
	}
	mtspr	FAIL, r0
0:


	{
	 mtspr	SIM_CONTROL, r20
	 movei	r20, 1
	}

	// We need to enable INTCTRL_HV in case we get an IDN message
	// and need to send a response packet.	We defer responses
	// until INTCTRL_2 occurs.
	shli	r20, r20, INT_INTCTRL_HV
	mtspr	INTERRUPT_MASK_RESET_HV, r20







	movei	r20, 1
	//
	// We have nothing else to do now, so we just loop.





	// We drain the UDN in case we paniced while booting from PCIe; this
	// should reduce the chance of the host hanging or panicing due to a
	// PCIe timeout.  (Also, if there isn't anything there, we'll hang in a
	// somewhat lower-power way than just looping.)
	//
1:
	mtspr	DONE, r20

	{
	 move	zero, udn0
	 j	1b
	}





	.size _start,.-_start
