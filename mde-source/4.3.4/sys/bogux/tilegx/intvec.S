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
 * Bogux interrupt vectors.
 */

#include <hv/hypervisor.h>

#include <arch/abi.h>
#include <arch/interrupts.h>
#include <arch/spr_def.h>

#include "intvec.h"

#include "bogux.h"
#include "errno.h"
#include "debug.h"
#include "mem_layout.h"

	.altmacro	// Necessary for \xx& macro substitution feature


	.macro	push_reg reg, ptr=sp
	{
	 st	\ptr, reg
	 addi	\ptr, \ptr, -8
	}
	.endm

	.macro	pop_reg reg, ptr=sp
	{
	 ld	reg, \ptr
	 addi	\ptr, \ptr, 8
	}
	.endm


	//
	// Default interrupt handler.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector.
	// c_routine is the C routine we'll call.
	//
	// The C routine is always passed two arguments:
	// - A pointer to a null-terminated string containing "vecname".
	// - The interrupt vector number.
	// Additionally, if pass_ss3 is nonzero, we pass a third argument:
	// - The contents of system save register 3.
	//
	.macro	int_hand vecnum, vecname, c_routine, pass_ss3=0
	.pushsection .rodata, "a"
intname_\vecname:
	.asciz "\vecname"
	.popsection
	.org	(\vecnum << 8)
intvec_\vecname:
	// Temporarily save a register so we have somewhere to work.

	mtspr	SYSTEM_SAVE_BX_0, r0

	// If we're coming from an MPL less than that of the supervisor,
	// then point to the base of the interrupt stack.  Otherwise, we
	// assume that the stack pointer is already valid.  Note that we can
	// check for the whole register being equal to the supervisor's MPL
	// here, without masking off the ICS bit; if the ICS bit is set,
	// and this is coming from the supervisor PL, that would have to
	// be a double fault interrupt, which would have ended up in the
	// hypervisor instead.

	mfspr	r0, EX_CONTEXT_BX_1
	cmpeqi	r0, r0, BOGUX_PL
	{
	 blbs	r0, 0f
	 move	r0, sp
	}

	moveli	r0, hw2_last(MEM_STACK_INIT)
	shl16insli r0, r0, hw1(MEM_STACK_INIT)
	shl16insli r0, r0, hw0(MEM_STACK_INIT)

0:
	// Push the parameter-passing registers on the stack, so that we can
	// set them to vector-unique values before we jump to the common code.
	// (We could save a couple more registers here and still fit in the
	// vector, but stopping at this point seems like more of a logical
	// division.)
	//
	// Registers are pushed on the stack in this order:
	//
	// (untouched)		<- initial stack pointer value, highest address
	// sp
	// lr
	// r0
	// r1
	// r2
	// .
	// .
	// .
	// r12			<- last register saved by this routine
	// ------------------------------------------------------------------
	// r13			<- first register saved by handle_interrupt;
 	//			   sp is here when we jump to handle_interrupt
	// .
	// .
	// .
	// r29
	// EX_CONTEXT_BX_1
	// EX_CONTEXT_BX_0
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value,
        //                                  lowest address

	addi	r0, r0, -8
	push_reg sp, r0
	move	sp, r0

	push_reg lr
	mfspr   r0, SYSTEM_SAVE_BX_0    // Original r0
	push_reg r0
	push_reg r1
	push_reg r2
	push_reg r3
	push_reg r4
	push_reg r5
	push_reg r6
	push_reg r7
	push_reg r8
	push_reg r9
	push_reg r10
	push_reg r11
	push_reg r12

	// Set up function parameters.

	{
	 moveli	r10, hw2_last(\c_routine)
	 moveli	r0, hw2_last(intname_\vecname)
	}
	{
	 shl16insli r10, r10, hw1(\c_routine)
	 shl16insli r0, r0, hw1(intname_\vecname)
	}
	{
	 shl16insli r10, r10, hw0(\c_routine)
	 shl16insli r0, r0, hw0(intname_\vecname)
	}
	{
	 moveli	r1, \vecnum
	 .ifne	\pass_ss3
	 mfspr	r2, SYSTEM_SAVE_BX_3
	 .endif
	}

	// Jump to remainder of interrupt handling code.

	j	handle_interrupt

	.endm

	//
	// System call interrupt handler.
	//
	// vecnum is where we'll put this code.
	// vecname is the name of the vector.
	//
	.macro	syscall_hand vecnum, vecname
	.org	(\vecnum << 8)
intvec_\vecname:
	// Point to the base of the interrupt stack.  (We assume we won't
	// be gettting any swint calls from our own PL, so we never try
	// to just re-use the current sp.)  Note that we don't save many
	// registers here; we assume that the client called the libc routine
	// (which issued the swint) using the normal C calling conventions,
	// so we can use any of the caller-saved registers.  We only need
	// to save sp, lr, and the two EX_CONTEXT registers (the latter only
	// because we could potentially take another interrupt, like a page
	// fault, while we're handling the syscall).  Registers are pushed
	// on the stack in this order:
	//
	// (untouched)		<- top of interrupt stack, highest address
	// sp
	// lr
	// EX_CONTEXT_BX_1
	// EX_CONTEXT_BX_0	<- last register saved by this routine
	// ------------------------------------------------------------------
        // r30			<- first register saved by handle_syscall;
 	//			   sp is here when we jump to handle_syscall
        // ...
        // r52
        // tp
        // valid
        // Padding to make the final stack pointer value 16-byte aligned
        // Caller state save area word 0
        // Caller state save area word 1 <- final stack pointer value,
        //                                  lowest address
        //
        // The "valid" word is 1 if r30..r52 are valid.

	// Stash sp temporarily
	move	r29, sp

	{
	 moveli	sp, hw2_last(MEM_STACK_INIT - 8)
	 moveli	r20, hw2_last(syscall_table)
	}
	{
	 shl16insli sp, sp, hw1(MEM_STACK_INIT - 8)
	 shl16insli r20, r20, hw1(syscall_table)
	}
	{
	 shl16insli sp, sp, hw0(MEM_STACK_INIT - 8)
	 shl16insli r20, r20, hw0(syscall_table)
	}

	push_reg r29		// Save original sp
	push_reg lr
        mfspr	r21, EX_CONTEXT_BX_1
	push_reg r21
        andi    r21, r21, 3     // strip off ICS, etc.
        bnez    r21, syscall_from_bogux
	mfspr	r21, EX_CONTEXT_BX_0
	// Bump the saved PC by one bundle so that when we return, we won't
	// execute the same swint instruction again.  We need to do this while
	// we're still in the critical section.
        // clone induces us to call handle_syscall_save so that we can
        // save away the callee-saved registers and restore them later.
        {
	 addi	r21, r21, 8		// FIXME need a #define for this
         addli  r22, TREG_SYSCALL_NR_NAME, -__NR_clone
        }
        push_reg r21
        bnezt   r22, handle_syscall_nosave
        j       handle_syscall_save

	.endm

	//
	// Macros to build the syscall table, used below.
	//
	.macro	syscall number routine
	.org	syscall_table + ((\number) * 8)
	.quad	\routine
	.endm

	//
	// Redispatch a downcall.
	//
	.macro	dc_dispatch vecnum, vecname
	.org	(\vecnum << 8)
intvec_\vecname:
	j	hv_downcall_dispatch
	.endm

//
// Routines which follow are called by the various macros instantiated above.
//

        .text

	//
	// Common code for most interrupts.  The C function we're eventually
	// going to is in r10, and its args are in r0...r9; the original
	// values for all of those registers are on the stack.
	//
handle_interrupt:
	// Save the rest of the registers that we didn't save in the actual	
	// vector itself

	push_reg r13
	push_reg r14
	push_reg r15
	push_reg r16
	push_reg r17
	push_reg r18
	push_reg r19
	push_reg r20
	push_reg r21
	push_reg r22
	push_reg r23
	push_reg r24
	push_reg r25
	push_reg r26
	push_reg r27
	push_reg r28
	push_reg r29
	mfspr	r29, EX_CONTEXT_BX_1
	push_reg r29

	mfspr	r29, EX_CONTEXT_BX_0

        {
         st	sp, r29
         // Allocate caller state save area.
         addi   sp, sp, -C_ABI_SAVE_AREA_SIZE
        }

  	// Jump to the C routine.  It should release INTERRUPT_CRITICAL_SECTION
	// as soon as possible.  It must return nonzero if it was called via a
	// downcall, and zero otherwise.
	jalr	r10

	// Upon return, fall through to...

return_from_interrupt:
	// Enable critical section bit

	movei	r29, 1
	mtspr	INTERRUPT_CRITICAL_SECTION, r29

        // Deallocate caller state save area.
        addi    sp, sp, C_ABI_SAVE_AREA_SIZE

	// If we're returning from a downcall, unmask the downcall interrupt.
#if INT_HAND_NO_DOWNCALL != 0
#error INT_HAND_NO_DOWNCALL must be zero
#endif
	{
	 beqz	r0, 0f
	 shli	r29, r29, INT_INTCTRL_BX
	}
	mtspr INTERRUPT_MASK_RESET_1, r29
0:
	pop_reg r29
	mtspr	EX_CONTEXT_BX_0, r29
	pop_reg r29
	mtspr	EX_CONTEXT_BX_1, r29
	pop_reg r29
	pop_reg r28
	pop_reg r27
	pop_reg r26
	pop_reg r25
	pop_reg r24
	pop_reg r23
	pop_reg r22
	pop_reg r21
	pop_reg r20
	pop_reg r19
	pop_reg r18
	pop_reg r17
	pop_reg r16
	pop_reg r15
	pop_reg r14
	pop_reg r13
	pop_reg r12
	pop_reg r11
	pop_reg r10
	pop_reg r9
	pop_reg r8
	pop_reg r7
	pop_reg r6
	pop_reg r5
	pop_reg r4
	pop_reg r3
	pop_reg r2
	pop_reg r1
	pop_reg r0
	pop_reg lr
	ld	sp, sp

	iret

// Save all the callee-saved registers (and tp) before doing the syscall.
handle_syscall_save:
        push_reg r30
        push_reg r31
        push_reg r32
        push_reg r33
        push_reg r34
        push_reg r35
        push_reg r36
        push_reg r37
        push_reg r38
        push_reg r39
        push_reg r40
        push_reg r41
        push_reg r42
        push_reg r43
        push_reg r44
        push_reg r45
        push_reg r46
        push_reg r47
        push_reg r48
        push_reg r49
        push_reg r50
        push_reg r51
        push_reg r52
        push_reg tp
        movei   r21, 1
        st      sp, r21
        j       handle_syscall

handle_syscall_nosave:

        // Don't save the callee-saved registers, and set "valid" to zero
        addli   sp, sp, -(PT_REGS_COUNT * 8)
        st      sp, zero

handle_syscall:

        {
         moveli	r21, NR_syscalls
         // Allocate caller state save area.
         addi    sp, sp, -(C_ABI_SAVE_AREA_SIZE + 8)
        }

	// Now that our sp is valid, we can enable further interrupts, and
	// then get the address of the function we're going to call.  We
	// ensure that the syscall number is within the legal range, and
	// that the function pointer we retrieve from the table is non-NULL.
	{
	 mtspr	INTERRUPT_CRITICAL_SECTION, zero
	 cmpltu	r21, TREG_SYSCALL_NR_NAME, r21
	}
	{
	 blbc	r21, nosys
	 shl3add r20, TREG_SYSCALL_NR_NAME, r20
	}
	ld	r20, r20
	beqz	r20, nosys

	// Jump to syscall handler.
	jalr	r20

	// Set r1 to errno if we are returning an error, otherwise zero.
	{
	 moveli r29, 1024
	 sub    r1, zero, r0
	}
	cmpltu  r29, r1, r29
	mnz     r1, r29, r1

resume_user:
	// Enter interrupt critical section and get back the saved EX_CONTEXT
	// registers.

	movei	r20, 1
	mtspr	INTERRUPT_CRITICAL_SECTION, r20

        // Deallocate caller state save area.
        addi    sp, sp, (C_ABI_SAVE_AREA_SIZE + 8)

        // Get the "valid" word.  If non-zero, restore the callee-saveds;
        // otherwise, skip over them.
	pop_reg	r21
        bnez    r21, 2f
        {
          addli sp, sp, (PT_REGS_COUNT * 8)
          j     3f
        }

        // Restore the callee-saved registers
2:      pop_reg tp
        pop_reg r52
        pop_reg r51
        pop_reg r50
        pop_reg r49
        pop_reg r48
        pop_reg r47
        pop_reg r46
        pop_reg r45
        pop_reg r44
        pop_reg r43
        pop_reg r42
        pop_reg r41
        pop_reg r40
        pop_reg r39
        pop_reg r38
        pop_reg r37
        pop_reg r36
        pop_reg r35
        pop_reg r34
        pop_reg r33
        pop_reg r32
        pop_reg r31
        pop_reg r30

3:	pop_reg	r21
	mtspr	EX_CONTEXT_BX_0, r21
	pop_reg	r21
	mtspr	EX_CONTEXT_BX_1, r21

	// Get sp and lr back from the stack.

	pop_reg	lr
	ld	sp, sp

	// Return from interrupt.
	iret

nosys:
        {
          movei r0, -ENOSYS
          j     resume_user
        }

syscall_from_bogux:
        moveli  r0, hw2_last(bogux_syscall)
        shl16insli r0, r0, hw1(bogux_syscall)
        {
          shl16insli r0, r0, hw0(bogux_syscall)
          jal   panic
        }

        .section ".rodata", "a"
bogux_syscall:
        .ascii  "got syscall, but not from user level"

// FIXME: not yet working on GX
dev_intr_dwncl = bad_intr

//
// Done with macro definitions, here are our vectors
//
	.section ".intrpt1", "ax"

	int_hand     INT_MEM_ERROR, MEM_ERROR, bad_intr
	int_hand     INT_IDN_COMPLETE, IDN_COMPLETE, bad_intr
	int_hand     INT_UDN_COMPLETE, UDN_COMPLETE, bad_intr
	int_hand     INT_ITLB_MISS, ITLB_MISS, page_fault, 1
	int_hand     INT_ILL, ILL, bad_intr
	int_hand     INT_GPV, GPV, bad_intr
	int_hand     INT_IDN_ACCESS, IDN_ACCESS, bad_intr
	int_hand     INT_UDN_ACCESS, UDN_ACCESS, bad_intr
	int_hand     INT_SWINT_3, SWINT_3, bad_intr
	int_hand     INT_SWINT_2, SWINT_2, bad_intr
	syscall_hand INT_SWINT_1, SWINT_1
	int_hand     INT_SWINT_0, SWINT_0, bad_intr
	int_hand     INT_UNALIGN_DATA, UNALIGN_DATA, bad_intr
	int_hand     INT_DTLB_MISS, DTLB_MISS, page_fault, 1
	int_hand     INT_DTLB_ACCESS, DTLB_ACCESS, access_fault, 1
	int_hand     INT_IDN_FIREWALL, IDN_FIREWALL, bad_intr
	int_hand     INT_UDN_FIREWALL, UDN_FIREWALL, bad_intr
	int_hand     INT_TILE_TIMER, TILE_TIMER, bad_intr
	int_hand     INT_IDN_TIMER, IDN_TIMER, bad_intr
	int_hand     INT_UDN_TIMER, UDN_TIMER, bad_intr
	int_hand     INT_IDN_AVAIL, IDN_AVAIL, bad_intr
	int_hand     INT_UDN_AVAIL, UDN_AVAIL, bad_intr
	int_hand     INT_PERF_COUNT, PERF_COUNT, bad_intr
	int_hand     INT_AUX_PERF_COUNT, AUX_PERF_COUNT, bad_intr
	int_hand     INT_INTCTRL_3, INTCTRL_3, bad_intr
#if BOGUX_PL == 2
	dc_dispatch  INT_INTCTRL_2, INTCTRL_2
	int_hand     INT_INTCTRL_1, INTCTRL_1, bad_intr
#elif BOGUX_PL == 1
	int_hand     INT_INTCTRL_2, INTCTRL_2, bad_intr
	dc_dispatch  INT_INTCTRL_1, INTCTRL_1
#else
#error BOGUX_PL must be 1 or 2
#endif
	int_hand     INT_INTCTRL_0, INTCTRL_0, bad_intr
	int_hand     INT_MESSAGE_RCV_DWNCL, MESSAGE_RCV_DWNCL, hv_message_intr
	int_hand     INT_DEV_INTR_DWNCL, DEV_INTR_DWNCL, dev_intr_dwncl, 1
	int_hand     INT_I_ASID, I_ASID, bad_intr
	int_hand     INT_D_ASID, D_ASID, bad_intr

// Special-case double-fault
	.org	(INT_DOUBLE_FAULT << 8)
intvec_DOUBLE_FAULT:
        move    r0, zero
        shl16insli r0, r0, hw0(0xDEAD)
        mtspr   FAIL, r0
        mtspr   DONE, r0

//
// System call table.  Note that the entries must be ordered by their
// system call numbers (as defined in syscall.h), but it's OK if some numbers
// are skipped, or if some syscalls exist but aren't present in the table.
//
	.section .rodata, "a"
	.align 8
syscall_table:
        // 20
        syscall __NR_fcntl,              SYSCALL_FUNC(fcntl)
        syscall __NR_ioctl,              SYSCALL_FUNC(ioctl)
        // 30
        syscall __NR_mkdirat,            SYSCALL_FUNC(errno_rofs)
        syscall __NR_unlinkat,           SYSCALL_FUNC(errno_rofs)
        syscall __NR_symlinkat,          SYSCALL_FUNC(errno_rofs)
        syscall __NR_linkat,             SYSCALL_FUNC(errno_rofs)
        syscall __NR_renameat,           SYSCALL_FUNC(errno_rofs)
        // 40
        syscall __NR_faccessat,          SYSCALL_FUNC(faccessat)
        // 50
        syscall __NR_openat,             SYSCALL_FUNC(openat)
        syscall __NR_close,              SYSCALL_FUNC(close)
        // 60
        syscall __NR_lseek,              SYSCALL_FUNC(lseek)
        syscall __NR_read,               SYSCALL_FUNC(read)
        syscall __NR_write,              SYSCALL_FUNC(write)
        syscall __NR_pread64,            SYSCALL_FUNC(pread64)
        syscall __NR_pwrite64,           SYSCALL_FUNC(pwrite64)
        // 70
        syscall __NR_readlinkat,         SYSCALL_FUNC(readlinkat)
        syscall __NR_newfstatat,         SYSCALL_FUNC(fstatat)
        // 80
        syscall __NR_fstat,              SYSCALL_FUNC(fstat)
        // 90        
        syscall __NR_exit,               SYSCALL_FUNC(exit)
        syscall __NR_exit_group,         SYSCALL_FUNC(exit)
        // 120
        syscall __NR_sched_setaffinity,  SYSCALL_FUNC(sched_setaffinity)
        syscall __NR_sched_getaffinity,  SYSCALL_FUNC(sched_getaffinity)
        syscall __NR_sched_yield,        SYSCALL_FUNC(dummy_syscall)
        syscall __NR_kill,               SYSCALL_FUNC(kill)
        // 140
        syscall __NR_setgid,             SYSCALL_FUNC(dummy_syscall)
        syscall __NR_setuid,             SYSCALL_FUNC(dummy_syscall)
        // 150                
        syscall __NR_times,              SYSCALL_FUNC(times)
        // 160
        syscall __NR_uname,              SYSCALL_FUNC(uname)
        syscall __NR_umask,              SYSCALL_FUNC(dummy_syscall)
        syscall __NR_getcpu,             SYSCALL_FUNC(getcpu)
        syscall __NR_gettimeofday,       SYSCALL_FUNC(gettimeofday)
        // 170
        syscall __NR_getpid,             SYSCALL_FUNC(getpid)
        syscall __NR_getuid,             SYSCALL_FUNC(dummy_syscall)
        syscall __NR_getgid,             SYSCALL_FUNC(dummy_syscall)
        syscall __NR_gettid,             SYSCALL_FUNC(getpid)
        // 210
        syscall __NR_brk,                SYSCALL_FUNC(brk)
        syscall __NR_munmap,             SYSCALL_FUNC(munmap)
        // 220
        syscall __NR_clone,              SYSCALL_FUNC(clone)
        syscall __NR_execve,             SYSCALL_FUNC(execve)
        syscall __NR_mmap2,              SYSCALL_FUNC(mmap)
        syscall __NR_mprotect,           SYSCALL_FUNC(mprotect)
        // 230
        syscall __NR_mbind,              SYSCALL_FUNC(mbind)
        syscall __NR_get_mempolicy,      SYSCALL_FUNC(get_mempolicy)
        syscall __NR_set_mempolicy,      SYSCALL_FUNC(set_mempolicy)
end_syscall_table:
        syscall NR_syscalls,             0
