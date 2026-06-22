/**
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
 *
 * @file
 * Entry point for Bogux startup.
 */

#include <hv/hypervisor.h>

#include <arch/abi.h>
#include <arch/chip.h>
#include <arch/interrupts.h>
#include <arch/spr_def.h>

#include "bogux.h"
#include "mem_layout.h"
#include "pa_allocator.h"

/* Address of the L0 PTE we'll stuff by hand.  The tables themselves
 * are defined at the end of this file.
 */
/* PA of L0 PTE which will point to supervisor_page_table */
#define L0_SV_PTE_PA     (temporary_l0_page_table - \
                          MEM_DATA_PA_ADJUST + \
                          HV_L0_INDEX(MEM_DATA_VA) * HV_PTE_SIZE)

	.macro	clr2 rnum1, rnum2
	{
	 move	\rnum1, zero
	 move	\rnum2, zero
	}
	.endm

        .text
        .global _start
        .align 8
_start:
#ifdef BOOT_TIMING
	movei	r10, 3
	mtspr	PASS, r10
#endif

	/* Notify the hypervisor of what version of the API we want */
	{
	  movei r0, _HV_VERSION
	  movei r1, __tile_chip__
	}
	{
	  movei r2, __tile_chip_rev__
	  movei r3, BOGUX_PL
	}
	jal hv_init
        /* Get a reasonable default ASID */
        {
          move r0, zero
          jal hv_inquire_asid
        }


        /* Install the default page table.  The relocation required to
	 * statically define the table is a bit too complex, so we have
	 * to plug in the pointer from the L0 to the L1 table by hand.
	 * Interspersed throughout here we're setting r0-r3 to the parameters
	 * passed to hv_install_context(); we also intersperse the code to
	 * set the interrupt vector base.
	 */
        {
	  /* Use starting ASID of range for this page table */
          bfextu r2, r0, 0, 31
	  /* MEM_CODE_VA is our first VA and thus the interrupt vector base. */
	  moveli r10, hw2_last(MEM_CODE_VA)
	}
	{
	  /* Construct access_pte for our page table. */
          moveli r1, hw1_last(HV_PTE_PRESENT | \
		     (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE))
	  shl16insli r10, r10, hw1(MEM_CODE_VA)
	}
	{
          shl16insli r1, r1, hw0(HV_PTE_PRESENT | \
		     (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE))
	  shl16insli r10, r10, hw0(MEM_CODE_VA)
	}
	{
	  move r3, zero
	  mtspr INTERRUPT_VECTOR_BASE_BX, r10
	}

	/* Stuff the L0 PTE. */
	{
          moveli r0, hw2_last(L0_SV_PTE_PA)
          moveli r6, hw2_last(supervisor_page_table - MEM_DATA_PA_ADJUST)
	}
	{
          shl16insli r0, r0, hw1(L0_SV_PTE_PA)
          shl16insli r6, r6, hw1(supervisor_page_table - MEM_DATA_PA_ADJUST)
	}
	{
          shl16insli r0, r0, hw0(L0_SV_PTE_PA)
          shl16insli r6, r6, hw0(supervisor_page_table - MEM_DATA_PA_ADJUST)
	}
	{
	  /* Cut off the low bits of the PT address. */
	  shrui r6, r6, HV_LOG2_PAGE_TABLE_ALIGN
	  /* Start with our access pte. */
	  move r5, r1
	}
	{
	  /* Stuff the address into the page table pointer slot of the PTE. */
	  bfins r5, r6, HV_PTE_INDEX_PTFN, \
			HV_PTE_INDEX_PTFN + HV_PTE_PTFN_BITS - 1
	}
	{
	  /* Store the L0 PTE. */
	  st r0, r5
	  moveli lr, hw2_last(1f)
	}
	{
	  /* We're locally caching, so we need to flush to make sure the
	   * other tiles can see it. */
	  flush r0
	  shl16insli lr, lr, hw1(1f)
	}
	{
	  /* Bias r0 back to point to the start of the table. */
	  addli r0, r0, -(HV_L0_INDEX(MEM_DATA_VA) * HV_PTE_SIZE)
	  shl16insli lr, lr, hw0(1f)
	}
        {
          j hv_install_context
        }
1:
        /* Get our x, y, width, height in r0..r1 */
        {
          jal hv_inquire_topology
        }
	/* Extract stuff from the topology structure: r4 = y, r6 = x,
	 * r5 = width.  FIXME: consider whether we want to just make these
	 * 64-bit values. */
	{
	  bfextu r5, r1, 0, 31
	  shrui r4, r0, 32
	}
	{
	  bfextu r6, r0, 0, 31
	}
        /* Generate a per-tile PA = VA stack address.  Also load the first
	 * real virtual address we're going to jump to; it's far enough away
	 * that we can't use a j instruction. */
        {
          moveli sp, hw2_last(MEM_INITIAL_SP)
          mul_lu_lu r4, r4, r5
        }
	{
          shl16insli sp, sp, hw1(MEM_INITIAL_SP)
          add r4, r4, r6
	}
        {
          shl16insli sp, sp, hw0(MEM_INITIAL_SP)
	  moveli r10, hw2_last(sv_go)
        }
        {
          shli r4, r4, 16  /* arbitrarily use 64KB */
	  shl16insli r10, r10, hw1(sv_go)
	}
	{
          addi sp, sp, -C_ABI_SAVE_AREA_SIZE  /* Allocate space for caller
                                                 state save area. */
	  shl16insli r10, r10, hw0(sv_go)
        }
        {
          sub sp, sp, r4
          jr r10
        }

/** Jump to user code; we're passed the PC and the SP. */
        .global jump_user
jump_user:
	movei r30, 1
	mtspr INTERRUPT_CRITICAL_SECTION, r30
	//
	// If we got here via a hypervisor message asking us to exec a
	// process, the downcall interrupt is disabled, and since we're
	// never going to return from hv_message_intr this is our only
	// chance to enable it.  So, we do.
	//
        shli r30, r30, INT_INTCTRL_BX
        mtspr  INTERRUPT_MASK_RESET_BX, r30

        mtspr EX_CONTEXT_BX_0, r0
        mtspr EX_CONTEXT_BX_1, zero
        move sp, r1

#ifdef BOOT_TIMING
	movei	r10, 4
	mtspr	PASS, r10
#endif
	// Clear all of the registers, to avoid leaking any data to the
	// user, and to provide a reproducible initial state.

	move r0,  zero
	clr2 r1,  r2
	clr2 r3,  r29
	clr2 r4,  r30
	clr2 r5,  r31
	clr2 r6,  r32
	clr2 r7,  r33
	clr2 r8,  r34
	clr2 r9,  r35
	clr2 r10, r36
	clr2 r11, r37
	clr2 r12, r38
	clr2 r13, r39
	clr2 r14, r40
	clr2 r15, r41
	clr2 r16, r42
	clr2 r17, r43
	clr2 r18, r44
	clr2 r19, r45
	clr2 r20, r46
	clr2 r21, r47
	clr2 r22, r48
	clr2 r23, r49
	clr2 r24, r50
	clr2 r25, r51
	clr2 r26, r52
	clr2 r27, tp
	clr2 r28, lr

        iret

/** Call a hypervisor routine; used by the hvtest device.  The first argument
 *  is the address of the routine to jump to, and the second is a pointer to
 *  the parameters.
 */
        .global do_hv_call
do_hv_call:
	move	r10, r0
	move	r11, r1

	.macro load reg, ptr
	{
	  ld \reg, \ptr
	  addi \ptr, \ptr, 8
	}
	.endm

	load r0, r11
	load r1, r11
	load r2, r11
	load r3, r11
	load r4, r11
	load r5, r11
	load r6, r11
	load r7, r11
	load r8, r11
	load r9, r11

	jr r10

.data
        // This large alignment value forces this whole data section
        // in start.o to be aligned to 64KB, which is big, but at least
        // means that MEM_L2_PAGE_TABLE_VA will be basically constant
        // for any page size.
        .align 65536

//
// Following are the initial supervisor page tables, which are shared between
// all tiles during startup, and thrown away soon afterward.  To prevent
// spurious --grind-coherence errors, we pre-set the dirty and accessed bits
// on every PTE, so the hypervisor will never try and write them.
//

//
// This table provides mappings for the SVMISC VA region, which is used for
// various temporary tasks, like bzero'ing out physical memory.
//
.global l2_page_table
l2_page_table:
        .skip HV_L2_SIZE

//
// This is the L0 page table.  We don't set any entries here, since the
// relocations turn out to be too hard for the assembler to handle.  We
// set the valid entries (pointing to the following L1 tables) at boot
// time via code above.
//
         .align HV_PAGE_TABLE_ALIGN
temporary_l0_page_table:
        .rept HV_L0_ENTRIES
        .quad   0
        .endr

//
// Macros to define page table entries in the next two tables.
//
#define PTE(pt, va, cpa, bits) \
  .org pt + HV_L1_INDEX(va) * HV_PTE_SIZE; \
  .quad (bits) | HV_PTE_PRESENT | \
        (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE) | \
        (HV_CPA_TO_PFN(cpa) << HV_PTE_INDEX_PFN)
#define LARGE_PAGE(pt, va, cpa, bits) \
  PTE(pt, va, cpa, HV_PTE_PAGE | HV_PTE_GLOBAL | (bits))
#define L2_TABLE(pt, va, cpa) \
  PTE(pt, va, cpa, 0)

//
// The supervisor L1 page table implements the mappings for our code and data.
//
         .align HV_PAGE_TABLE_ALIGN
.global supervisor_page_table
supervisor_page_table:
        LARGE_PAGE(supervisor_page_table, MEM_DATA_VA, MEM_DATA_PA, \
		   HV_PTE_DIRTY | HV_PTE_ACCESSED | \
                   HV_PTE_READABLE | HV_PTE_WRITABLE)
        L2_TABLE(supervisor_page_table, MEM_SVMISC_VA, \
		 MEM_L2_PAGE_TABLE_VA - MEM_DATA_PA_ADJUST)
        LARGE_PAGE(supervisor_page_table, MEM_CODE_VA, MEM_CODE_PA, \
		   HV_PTE_ACCESSED | HV_PTE_EXECUTABLE)
        .org supervisor_page_table + HV_L1_SIZE

# Makefile.in checks to make sure this symbol is zero during the build.
# If not, the mem_layout.h header needs to be modified to match.
mem_l2_page_table_check = l2_page_table - MEM_L2_PAGE_TABLE_VA

//
// This is used for the first zero page on the first controller.
//
        .align HV_PAGE_SIZE_SMALL * PA_COLORS
.global zero_pages_data
zero_pages_data:
        .skip HV_PAGE_SIZE_SMALL * PA_COLORS
