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

/* This file contains the "glue" entry points that users of the hypervisor
 * call to invoke hypervisor services. This file is not linked against,
 * because its contents are supplied at runtime by the current hypervisor.
 * Instead, the function pointers in hypervisor.h are used to jump to
 * entry points at predefined offsets in this code.
 */

#include <arch/abi.h>
#include <arch/spr_def.h>


#include "glue_asm.h"   // Included from build tree


#include <hv/hypervisor.h>

#include "hv.h"
#include "param.h"
#include "syscall.h"

/* Each hypervisor glue entry point is aligned modulo this many bytes. */
#define ALIGN 32

        .align ALIGN
.Lglue_start:
hv_dispatch_reserved:
        ill
        .byte 'H','V','G','L'
        .word .Linitial_page_table - .Lglue_start
        .word 0, 0
        .word 0, 0

#define GENERIC_SWINT2(name) \
        .align ALIGN ; \
hv_##name: \
        moveli TREG_SYSCALL_NR_NAME, HV_SYS_##name ; \
        swint2 ; \
        jrp lr ; \
        fnop

#define ILLEGAL_SWINT2(name) \
        .align ALIGN ; \
hv_##name: \
	 moveli	r0, HV_ENOSYS ; \
	 jrp	lr ; \

GENERIC_SWINT2(init)
GENERIC_SWINT2(install_context)

        .align ALIGN
hv_sysconf:
        /* Set up values for .Lsysconf_handler in a PIC way. */
        {
          move r3, lr
          lnk lr
        }
0:	{
          addli r1, lr, hw0_last(.Lsysconf_table - 0b)
          cmpltui r2, r0, (.Lsysconf_table_end - .Lsysconf_table) >> 4
        }
        {
          mnz r2, r2, r0  /* Replace out-of-range with _HV_SYSCONF_RESERVED */
        }
	{
          /* This 32-byte code block is full so jump to more code
           * to handle the rest.
           */
          shli r2, r2, 4
          j .Lsysconf_handler
        }

GENERIC_SWINT2(get_rtc)
GENERIC_SWINT2(set_rtc)
GENERIC_SWINT2(flush_asid)
GENERIC_SWINT2(flush_page)
GENERIC_SWINT2(flush_pages)
GENERIC_SWINT2(restart)
GENERIC_SWINT2(halt)
GENERIC_SWINT2(power_off)
GENERIC_SWINT2(inquire_physical)
GENERIC_SWINT2(inquire_memory_controller)
GENERIC_SWINT2(inquire_virtual)
GENERIC_SWINT2(inquire_asid)

        .align ALIGN
hv_nanosleep:
        /* Each time through the loop we consume three cycles and
         * therefore four nanoseconds, assuming a 750 MHz clock rate.
         *
         * TODO: reading a slow SPR would be the lowest-power way
         * to stall for a finite length of time, but the exact delay
         * for each SPR is not yet finalized.
         */
        {
          crc32_32 r1, r0, r0
          addi r0, r0, -4
        }
        {
          add r1, r1, r1 /* force a stall */
          bgtzt r0, hv_nanosleep
        }
        jrp lr
        fnop

GENERIC_SWINT2(console_read_if_ready)
GENERIC_SWINT2(console_write)

	.align	ALIGN
hv_downcall_dispatch:
	mtspr SYSTEM_SAVE_CL_3, TREG_SYSCALL_NR_NAME
	moveli TREG_SYSCALL_NR_NAME, HV_SYS_downcall_dispatch
	swint2
	iret	// We shouldn't get here, but for consistency with !Gx

GENERIC_SWINT2(inquire_topology)
GENERIC_SWINT2(fs_findfile)
GENERIC_SWINT2(fs_fstat)
GENERIC_SWINT2(fs_pread)
GENERIC_SWINT2(physaddr_read64)
GENERIC_SWINT2(physaddr_write64)
GENERIC_SWINT2(get_command_line)
GENERIC_SWINT2(set_caching)
GENERIC_SWINT2(bzero_page)
GENERIC_SWINT2(register_message_state)
GENERIC_SWINT2(send_message)










        .align ALIGN
hv_receive_message:
	j .Lreceive_message
	fnop
	fnop
	fnop


GENERIC_SWINT2(inquire_context)
GENERIC_SWINT2(start_all_tiles)
GENERIC_SWINT2(dev_open)
GENERIC_SWINT2(dev_close)
GENERIC_SWINT2(dev_pread)
GENERIC_SWINT2(dev_pwrite)
GENERIC_SWINT2(dev_poll)
GENERIC_SWINT2(dev_poll_cancel)
GENERIC_SWINT2(dev_preada)
GENERIC_SWINT2(dev_pwritea)
GENERIC_SWINT2(flush_remote)
GENERIC_SWINT2(console_putc)
GENERIC_SWINT2(inquire_tiles)
GENERIC_SWINT2(confstr)
GENERIC_SWINT2(reexec)
GENERIC_SWINT2(set_command_line)
ILLEGAL_SWINT2(clear_intr)
ILLEGAL_SWINT2(enable_intr)
ILLEGAL_SWINT2(disable_intr)
ILLEGAL_SWINT2(raise_intr)
ILLEGAL_SWINT2(trigger_ipi)
GENERIC_SWINT2(store_mapping)
GENERIC_SWINT2(inquire_realpa)
GENERIC_SWINT2(flush_all)
GENERIC_SWINT2(get_ipi_pte)
GENERIC_SWINT2(set_pte_super_shift)
GENERIC_SWINT2(set_speed)
GENERIC_SWINT2(install_virt_context)
GENERIC_SWINT2(inquire_virt_context)
GENERIC_SWINT2(install_guest_context)
GENERIC_SWINT2(inquire_guest_context)
GENERIC_SWINT2(console_set_ipi)
GENERIC_SWINT2(close_all_devices)
GENERIC_SWINT2(send_nmi)

/*
 * Add a bunch of dummy handlers in case our client uses syscall numbers
 * which are too large.
 */
bad_syscalls:
	.rept 256 - HV_MAX_SYSCALL
	.align ALIGN
	 moveli	r0, HV_ENOSYS
	 jrp	lr
	.endr

/* The sysconf handler is too big to fit into its slot, so we put
 * the rest of it here.
 *
 * On entry:
 *    r0 holds the sysconf request number (HV_SYSCONF_*)
 *    r1 holds the address of .Lsysconf_table
 *    r2 holds the sysconf number * 16, with out-of-range values zeroed
 *    r3 holds the original link register
 */
        .align 64
.Lsysconf_handler:
        { add r2, r2, r1 ; move lr, r3 }
        { moveli TREG_SYSCALL_NR_NAME, HV_SYS_sysconf ; jr r2 }

/* .Lsysconf_table is an array of 16 byte code blocks, one for each
 * HV_SYSCONF_ value. The dispatcher transfers control to the code block
 * corresponding to the sysconf request.  The common case is just to
 * return a constant, which is expressed using the SIMPLE macro.
 */
.Lsysconf_table:

/* A sysconf entry that just returns a 32-bit constant value. */
#define SIMPLE(n) \
  moveli r0, hw1_last(n) ; { shl16insli r0, r0, hw0(n) ; jrp lr }

/* A sysconf entry that requires a hypervisor call. */
#define HVCALL()  swint2; jrp lr

        /* _HV_SYSCONF_RESERVED */
        SIMPLE(-1)   /* illegal sysconf requests always return -1 */
	
        /* HV_SYSCONF_GLUE_SIZE */
        SIMPLE(.Lglue_end - .Lglue_start)
	
        /* HV_SYSCONF_PAGE_SIZE_SMALL */
        HVCALL()
	
        /* HV_SYSCONF_PAGE_SIZE_LARGE */
        HVCALL()

        /* HV_SYSCONF_CPU_SPEED */
        HVCALL()
	
        /* HV_SYSCONF_CPU_TEMP */
        HVCALL()
	
        /* HV_SYSCONF_BOARD_TEMP */
        HVCALL()
	
        /* HV_SYSCONF_VALID_PAGE_SIZES */
        SIMPLE(HV_CTX_PG_SM_16K | HV_CTX_PG_SM_64K)
	
        /* HV_SYSCONF_PAGE_SIZE_JUMBO */
        HVCALL()

.Lsysconf_table_end:

#undef SIMPLE
#undef HVCALL
#undef REL


// FIXME: GX: the fast message receive stuff has been ported but not tested;
// also, we may want to make the pointers, etc. 64-bit.
/*
 * Implement the fast message receiver.  We're passed the contents of the
 * HV_MsgState struct, by value, so on entry:
 *    r0 holds the VA of the start of the message buffer area, which contains
 *       the head offset, the tail offset, and the messages.
 *    r1 holds the mask with which to wrap the read and write pointers.
 *    r2 holds the pointer to where the received message goes.
 *    r3 holds the length of the received message buffer.
 */
        .align 64
.Lreceive_message:
	.if (CLIENT_MSG_AREA_HEAD_SIZE - 4) | (CLIENT_MSG_AREA_TAIL_SIZE - 4)
	.error "client_msg_area.head/tail sizes changed, modify code below"
	.endif
	.if CLIENT_MSG_AREA_HEAD_OFF
	addi r0, CLIENT_MSG_AREA_HEAD_OFF
	.endif
        {
	  ld4u r10, r0              // Load head offset
                                    // Point to tail offset
          addi r11, r0, CLIENT_MSG_AREA_TAIL_OFF - CLIENT_MSG_AREA_HEAD_OFF
                                    // Point to messages
          addi r12, r0, CLIENT_MSG_AREA_MSGS_OFF - CLIENT_MSG_AREA_HEAD_OFF
        }
	{
	  ld4u r11, r11             // Load tail offset
	  add r12, r12, r10         // Calculate message location
	}
	{
	  cmpeq r13, r10, r11       // Are head/tail offsets equal?
                                    // Calculate new head offset
	  addi r10, r10, CLIENT_MSG_SIZE
	}
	{
	  blbs r13, .Lno_msg        // If no message, exit
	  and r10, r10, r1          // Wrap new head offsets
	}
	{
	  ld4u r14, r12	            // Load the message source/length word
	  addi r12, r12, 4          // Point to start of actual message
        }
	.if CLIENT_MSG_LEN_OFF | (CLIENT_MSG_LEN_SIZE - 2)
	.error "client_msg.len position/size changed, modify code below"
	.endif
	.if (CLIENT_MSG_SOURCE_OFF - 2) | (CLIENT_MSG_SOURCE_SIZE - 2)
	.error "client_msg.source position/size changed, modify code below"
	.endif
	{
	  mm r14, zero, 0, 15       // Get length
          shli r1, r14, 16          // Put source in high half of r1; this
	                            // will eventually end up in r0
	}
	{
	  or r15, r14, r2           // Or length & dest addr together
	  cmpleu r16, r14, r3       // See if we have enough space
	}
        {
	  blbc r16, .Ltoobig_msg    // Not enough space, return error
	  andi r15, r15, 3          // See if addr & length both aligned
        }
	{
	  st4 r0, r10               // Now we can store the new head offsets
          mm r1, r14, 32, 63         // Merge length to r1 for return; will be
	                            // moved to r0 below
	}
	{
	  bnez r15, .Lunaligned_msg // If unaligned, go to the slow path
	  shrui r18, r14, 2         // Get length in words in case it's aligned
	}

.Laligned_msg:
	{
	  ld4u r17, r12             // Load word
	  addi r12, r12, 4          // Bump load address
	}
	{
	  st4 r2, r17               // Store word
	  addi r2, r2, 4            // Bump store address
	  addi r18, r18, -1         // Decrement word count
	}
	bgtzt r18, .Laligned_msg    // Go again if more words left
	{
	  move r0, r1
	  jrp lr
	}

.Lunaligned_msg:
	{
	  ld1u r17, r12             // Load a byte
	  addi r12, r12, 1          // Bump load address
	}
	{
	  st1 r2, r17               // Store a byte
	  addi r2, r2, 1            // Bump load address
	  addi r14, r14, -1         // Decrement byte count
	}
	bnezt r14, .Lunaligned_msg  // Go again if more bytes left
	{
	  move r0, r1
	  jrp lr
	}

.Ltoobig_msg:
	{
	  moveli r0, HV_E2BIG       // Return error code
	  jrp lr
	}

.Lno_msg:
	//
	// Note that in the fast case, the purpose of this syscall is just
	// to enable a downcall for the next message which arrives; we don't
	// expect a return value.
	//
        moveli TREG_SYSCALL_NR_NAME, HV_SYS_receive_message
        swint2
	{
	  move r0, zero             // Return zero length
	  jrp lr
	}


/* This symbol indicates the end of this entire block of code and data.
 * Nothing should come after this.
 */
.Lglue_end:


/* Reserve space for the magic hypervisor to set up the initial page table.
 * Page tables are required to be aligned to HV_PAGE_TABLE_ALIGN.  We put
 * this after .Lglue_end since there is no point in memcpying it.
 */

//
// L0 page table.  We stuff only one entry, and point it to the L1
// table below.
//

        .align HV_PAGE_TABLE_ALIGN
.Linitial_page_table:

	.set ptfn, HV_CPA_TO_PTFN(HV_GLUE_START_CPA + .Ll1_page_table - \
	 			  .Lglue_start)

        .quad  (  (  1 << HV_PTE_INDEX_PRESENT) \
                | (ptfn << HV_PTE_INDEX_PTFN) \
                | (HV_PTE_MODE_CACHE_NO_L3 \
                       << HV_PTE_INDEX_MODE) \
                | (  1 << HV_PTE_INDEX_READABLE) \
                | (  1 << HV_PTE_INDEX_WRITABLE) \
                | (  1 << HV_PTE_INDEX_EXECUTABLE))

        .rept HV_L0_ENTRIES - 1
        .quad	0
        .endr

//
// L1 page table, just filled with large pages.
//
        .align HV_PAGE_TABLE_ALIGN
.Ll1_page_table:

	.set ptfn, 0
.macro define_pte
        .quad  (  (   1 << HV_PTE_INDEX_PRESENT) \
                | (   1 << HV_PTE_INDEX_PAGE) \
                | (ptfn << HV_PTE_INDEX_PTFN) \
                | (HV_PTE_MODE_CACHE_NO_L3 \
                        << HV_PTE_INDEX_MODE) \
                | (   1 << HV_PTE_INDEX_READABLE) \
                | (   1 << HV_PTE_INDEX_WRITABLE) \
                | (   1 << HV_PTE_INDEX_EXECUTABLE))

        .set ptfn, ptfn + HV_CPA_TO_PTFN(HV_DEFAULT_PAGE_SIZE_LARGE)
.endm
	
        .rept 1 << (32 - HV_LOG2_DEFAULT_PAGE_SIZE_LARGE)
        define_pte
        .endr
