// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
//


#ifndef __tilegx__

// Pro stack frame is as follows (layout corresponds to the 
// sigcontext struct):
// NOTE that only r0-r29, sp, lr and pc inside sigcontext are saved properly in
// the following process to save time, since the other fields are either zero 
// all the time or not commonly used. 
//
// (SP + 280):  user stack (i.e. our frame size is 280)
// (SP + 276):  interrupt bit mask (high) for the handler
// (SP + 272):  interrupt bit mask (low) for the handler
// (SP + 268):  pad[4]
// (SP + 264):  pad[3]
// (SP + 260):  pad[2]
// (SP + 256):  pad[1]
// (SP + 252):  pad[0]
// (SP + 248):  faultnum
// (SP + 244):  ics
// (SP + 240):  pc
// (SP + 236):  lr
// (SP + 232):  sp (SP_in)
// (SP + 228):  tp
// (SP + 224):  r52
// (SP + 220):  r51
// [...]
// (SP + 20):   r1
// (SP + 16):   r0
// (SP + 12):   EX_CONTEXT_0_1
// (SP + 8):    EX_CONTEXT_0_0 (link register back to interrupted insn)
// (SP + 4):    caller state save area (caller's caller's sp)
// (SP + 0):    caller state save area (caller's lr)
//
// trampoline_body_copied function will help to get parameters of interrupt
// number and user C callback function pointer, then jump to
// trampoline_body_fixed. 
// This function need to be copied into interrupt vector memory according 
// to the input interrupt number.
	.data
  	.type trampoline_body_copied,@object
	.global trampoline_body_copied
  	.align 8
trampoline_body_copied:
  	{ 
  	addli sp, sp, -256;   // SP + 24
  	}
  	{
  	addi sp, sp, -24;     // Get our real stack pointer 
  	addi r2, sp, 4;       // SP + 28
 	sw sp, r2;            // Save r2 
  	}
  	{
	addli r3, sp, 232;    // Save location for SP_in
	sw r2, r3;            // Save r3 
	}
	{
	addi r2, r2, 4;       // SP + 32
	}
	{
	addli r4, sp, 280;    // Regenerate SP_in
	sw r2, r4;            // Save r4
	}
	{
	addi r2, sp, 36;      // SP + 36
	addi r3, r3, 4;       // SP + 236
	sw r3, r4;            // Save SP_in
	}
	.global trampoline_body_copied_mask_high_lo16
trampoline_body_copied_mask_high_lo16:  
	{
	moveli r4, lo16(0);   // Low half of interrupt mask high part
	sw r2, r5;            // Save r5
	}
	.global trampoline_body_copied_mask_high_ha16
trampoline_body_copied_mask_high_ha16: 
	{
	auli r4, r4, ha16(0); // Upper half of interrupt mask high part
	sw r3, lr;            // Save lr
	}
	.global trampoline_body_copied_mask_low_lo16
trampoline_body_copied_mask_low_lo16:  
	{
	moveli r5, lo16(0);   // Low half of interrupt mask low part
	addli sp, sp, 276;    // SP + 276
	}
	.global trampoline_body_copied_mask_low_ha16
trampoline_body_copied_mask_low_ha16: 
	{
	auli r5, r5, ha16(0); // Upper half of interrupt mask high part
	sw sp, r4;            // Save interrupt mask high part
	}
  	{
	addi sp, sp, -4;      // SP + 272
	}
	{
	addli sp, sp, -272;   // SP + 0
	sw sp, r5;            // Save interrupt mask low part
	}
	.global trampoline_body_copied_c_lo16
trampoline_body_copied_c_lo16: 
	{
	moveli r3, lo16(0); // Save (in r3) our entry point (lo16)
	mtspr INTERRUPT_MASK_SET_0_1, r4; // Mask out interrupts (high)
	}
	.global trampoline_body_copied_c_ha16
trampoline_body_copied_c_ha16: 
	{
	auli r3, r3, ha16(0); // Save (in r3) our entry point (ha16)
	mfspr r4, EX_CONTEXT_0_0; // Save (in r4) the interrupt PC
	}
	{
	addli r2, sp, 240; // SP + 240
	mtspr INTERRUPT_MASK_SET_0_0, r5; // Mask out interrupts (low)
	}
	{
	// Save (in r2) trampoline_body_fixed (lo16)
	moveli r2, lo16(trampoline_body_fixed);
	sw r2, r4; // Save interrupt PC
	}
	{
	// Save (in r2) trampoline_body_fixed (ha16)
	auli r2, r2, ha16(trampoline_body_fixed);
	mfspr r5, EX_CONTEXT_0_1; // Save (in r5) EX_CONTEXT_0_1
	}
	{
	addi r2, sp, 12; // Save in r2 where EX_CONTEXT_0_1 should be stored
	jr r2; // Jump to trampoline_body_fixed function
	}
	.global trampoline_body_copied_end
trampoline_body_copied_end:
	.size trampoline_body_copied, .-trampoline_body_copied

// trampoline_body_fixed function will help to save other registers as 
// on stack, as well as restore them back when user C callback function
// returns.
// This function is parameter-independent, so the same to all interrupts.
	.section .text.trampoline_body_fixed, "ax"	
	.type trampoline_body_fixed,@function                                        
	.align 64
trampoline_body_fixed:

	#define STORE_BUNDLE(REG, NEXT_OFFSET) \
	{ addli r2, sp, NEXT_OFFSET; sw r2, REG; }	

	STORE_BUNDLE(r5, 8)  // Save EX_CONTEXT_0_1      
	STORE_BUNDLE(r4, 16) // Save EX_CONTEXT_0_0
	{
	mtspr INTERRUPT_CRITICAL_SECTION, zero; // Exit from critical section
	}
	STORE_BUNDLE(r0, 20)
	STORE_BUNDLE(r1, 40)        
	STORE_BUNDLE(r6, 44)        
	STORE_BUNDLE(r7, 48)        
	STORE_BUNDLE(r8, 52)        
	STORE_BUNDLE(r9, 56)        
	STORE_BUNDLE(r10, 60)                
	STORE_BUNDLE(r11, 64)        
	STORE_BUNDLE(r12, 68)        
	STORE_BUNDLE(r13, 72)        
	STORE_BUNDLE(r14, 76)             
	STORE_BUNDLE(r15, 80)        
	STORE_BUNDLE(r16, 84)        
	STORE_BUNDLE(r17, 88)        
	STORE_BUNDLE(r18, 92)                
	STORE_BUNDLE(r19, 96)        
	STORE_BUNDLE(r20, 100)        
	STORE_BUNDLE(r21, 104)        
	STORE_BUNDLE(r22, 108)                
	STORE_BUNDLE(r23, 112)        
	STORE_BUNDLE(r24, 116)        
	STORE_BUNDLE(r25, 120)        
	STORE_BUNDLE(r26, 124)        
	STORE_BUNDLE(r27, 128)        
	STORE_BUNDLE(r28, 132)       
	STORE_BUNDLE(r29, 272)        
// Pass arguments to C callback function, including the interrupt number via r0
// and the sigcontext pointer via r1.
	{
	addi r1, sp, 16; // SP + 16
	}
	{
	addli r2, sp, 276; // SP + 276
	}
	{
	addli r6, sp, 272; // SP + 272
	lw r4, r2; // Save (in r4) interrupt_mask_high
	}
	{
	pcnt r2, r4; // Save (in r2) how many bits set in interrupt_mask_high
	lw r5, r6; // Save (in r5) interrupt_mask_low
	}
	{
	pcnt r6, r5; // Save (in r3) how many bits set in interrupt_mask_low
	bgz r2, branch_mask_high_calc; // Branch to calculate on mask high
 	}
	{
	ctz r0, r5; // Count number of zero bits from LSB of mask low
	j branch_enter_c_func;
	}
branch_mask_high_calc:
	{
	ctz r0, r4; // Count number of zero bits from LSB of mask high
	}
	{
	addi r0, r0, 32; // Generate intr_num
	}
branch_enter_c_func:
	{            
	jalr r3; // Go to user C callback entry point
	}        
// The interrupt return code does the following within its        
// critical section:         
//         
// 1.  Unmask interrupts   
// 2.  Restore sp, r0-r29, lr         
// 3.  Restore EX_CONTEXT_* registers         
// 4.  iret
	{        
	addi r2, sp, 8; // Get SP + 8 into r2 (save loc for EX_CONTEXT_0_0)
	movei r4, 1; // Get a 1 for the critical section SPR
	} 
  	{
  	mtspr INTERRUPT_CRITICAL_SECTION, r4; // Enter critical section
  	}
	{        
	addli r2, sp, 276; // Get address of interrupt mask high part
	lw r7, r2; // Load PC of interrupted insn
	}        
	{       
	addli r2, sp, 272; // Get address of interrupt mask low part
	lw r4, r2; // Load interrupt mask high part
	}
	{
	lw r5, r2; // Load interrupt mask low part
	}
	{
	mtspr INTERRUPT_MASK_RESET_0_1, r4; // Write back interrupts (high)
	}        
	{
	mtspr INTERRUPT_MASK_RESET_0_0, r5; // Write back interrupts (low)
	}        
	{       
	addi r2, sp, 12; // Get address of EX_CONTEXT_0_1
	mtspr EX_CONTEXT_0_0, r7; // Write the iret PC
	}

	#define RESTORE_BUNDLE(REG, NEXT_OFFSET) \
	{ addli r2, sp, NEXT_OFFSET; lw REG, r2; } 

	RESTORE_BUNDLE(r4, 36) // EX_CONTEXT_0_1 in r4 temporarily
	RESTORE_BUNDLE(r5, 40)      
	RESTORE_BUNDLE(r6, 44)   
	RESTORE_BUNDLE(r7, 48)     
	RESTORE_BUNDLE(r8, 52)    
	RESTORE_BUNDLE(r9, 56)    
	RESTORE_BUNDLE(r10, 60)   
	RESTORE_BUNDLE(r11, 64)    
	RESTORE_BUNDLE(r12, 68)    
	RESTORE_BUNDLE(r13, 72)    
	RESTORE_BUNDLE(r14, 76)    
	RESTORE_BUNDLE(r15, 80)    
	RESTORE_BUNDLE(r16, 84)    
	RESTORE_BUNDLE(r17, 88)    
	RESTORE_BUNDLE(r18, 92)   
	RESTORE_BUNDLE(r19, 96)   
	RESTORE_BUNDLE(r20, 100)   
	RESTORE_BUNDLE(r21, 104)   
	RESTORE_BUNDLE(r22, 108)   
	RESTORE_BUNDLE(r23, 112)   
	RESTORE_BUNDLE(r24, 116)     
	RESTORE_BUNDLE(r25, 120)    
	RESTORE_BUNDLE(r26, 124)   
	RESTORE_BUNDLE(r27, 128)    
	RESTORE_BUNDLE(r28, 132)    
	RESTORE_BUNDLE(r29, 16)     
	RESTORE_BUNDLE(r0,  20)     
	RESTORE_BUNDLE(r1,  236)
	RESTORE_BUNDLE(lr,  32)     
	{
	mtspr EX_CONTEXT_0_1, r4; // Restore EX_CONTEXT_0_1
	}
	RESTORE_BUNDLE(r4, 28)     
	RESTORE_BUNDLE(r3, 24)         
	{        
	addli sp, sp, 232; // SP + 232      
	lw r2, r2; // Restore r2
	}      
	{       
	lw sp, sp; // Load SP_in
	}     
	{     
	iret;   
	}       
	.size trampoline_body_fixed, .-trampoline_body_fixed
  
#else // __tilegx__

// Gx stack frame is as follows (layout corresponds to the 
// sigcontext struct):
// NOTE that only r0-r29, sp, lr and pc inside sigcontext are saved properly in
// the following process to save time, since the other fields are either zero
// all the time or not commonly used.
//
// (SP + 560):  user stack (i.e. our frame size is 560)   
// (SP + 552):  interrupt bit mask for the handler
// (SP + 544):  pad[4]
// (SP + 536):  pad[3]
// (SP + 528):  pad[2]
// (SP + 520):  pad[1]
// (SP + 512):  pad[0]
// (SP + 504):  faultnum
// (SP + 496):  ics
// (SP + 488):  pc
// (SP + 480):  lr
// (SP + 472):  sp (SP_in)
// (SP + 464):  tp
// (SP + 456):  r52
// (SP + 448):  r51
// [...]
// (SP + 48):   r1
// (SP + 40):   r0
// (SP + 32):   CMPEXCH_VALUE
// (SP + 24):   EX_CONTEXT_0_1
// (SP + 16):   EX_CONTEXT_0_0 (link register back to interrupted insn)
// (SP + 8):    caller state save area (caller's caller's sp)
// (SP + 0):    caller state save area (caller's lr)
	.data
	.type trampoline_body_copied,@object
	.global trampoline_body_copied
	.align 8
trampoline_body_copied:
	{
	addli sp, sp, -504; // SP + 56
	}
	{
	addi sp, sp, -56;   // Get our real stack pointer
	addi r2, sp, 8;     // SP + 64
	st sp, r2;          // Save r2
	}
	{
	addli r3, sp, 472;  // Save location for SP_in
	st_add r2, r3, 8;   // Save r3
	}
	{
	addli r4, sp, 560;  // Regenerate SP_in
	st_add r2, r4, 8;   // Save r4 
	}
	{
	addli r3, sp, 480;  // Save location for lr
	st r3, r4;          // Save SP_in
	} 
	{
	st_add r2, r5, 8;   // Save r5 
	}
	.global trampoline_body_copied_mask_hw2
trampoline_body_copied_mask_hw2: 
	{
	moveli r5, hw2(0);  // Save (in r5) interrupt mask (hw2)
	st r3, lr;          // Save lr 
	}
	.global trampoline_body_copied_mask_hw1
trampoline_body_copied_mask_hw1: 
	{
	shl16insli r5, r5, hw1(0); // Save (in r5) interrupt mask (hw1)
	st_add r2, r6, 8;   // Save r6
	}
	.global trampoline_body_copied_mask_hw0
trampoline_body_copied_mask_hw0: 
	{
	shl16insli r5, r5, hw0(0); // Save (in r5) interrupt mask (hw0)
	st r2, r7;          // Save r7
	}
	.global trampoline_body_copied_c_hw2
trampoline_body_copied_c_hw2:
	{
	moveli r3, hw2_last(0); // Save (in r3) our entry point (hw2)
	mtspr INTERRUPT_MASK_SET_0, r5; // Mask out interrupts
	}
	.global trampoline_body_copied_c_hw1
trampoline_body_copied_c_hw1:
	{
	shl16insli r3, r3, hw1(0); // Save (in r3) our entry point (hw1)
	mfspr r4, EX_CONTEXT_0_0; // Save (in r4) the interrupt PC
	}
	.global trampoline_body_copied_c_hw0
trampoline_body_copied_c_hw0:
	{
	shl16insli r3, r3, hw0(0); // Save (in r3) our entry point (hw0)
	mfspr r6, EX_CONTEXT_0_1; // Save (in r6) EX_CONTEXT_0_1
	}
#ifdef __LP64__
	{
	// Save (in r2) trampoline_body_fixed (hw2)
	moveli r2, hw2_last(trampoline_body_fixed); 
	}
	{
	// Save (in r2) trampoline_body_fixed (hw1)
	shl16insli r2, r2, hw1(trampoline_body_fixed);
	}
#else
	{
	// Save (in r2) trampoline_body_fixed (hw1)
	moveli r2, hw1_last(trampoline_body_fixed);
	}
#endif
	{
	// Save (in r2) trampoline_body_fixed (hw0)
	shl16insli r2, r2, hw0(trampoline_body_fixed);
	mfspr r7, CMPEXCH_VALUE; // Save (in r7) the CMPEXCH_VALUE
	}
	{
	addi r2, sp, 16; // Save in r2 where EX_CONTEXT_0_0 should be stored
	jr r2; // Jump to trampoline_body_fixed
	}
	.global trampoline_body_copied_end
trampoline_body_copied_end:
	.size trampoline_body_copied, .-trampoline_body_copied

	.section .text.trampoline_body_fixed, "ax"	
	.type trampoline_body_fixed,@function  
	.align 64
trampoline_body_fixed:

	#define STORE_BUNDLE(REG, NEXT_OFFSET) \
	{ st_add r2, REG, NEXT_OFFSET; }
  
	STORE_BUNDLE(r4, 8)  // Save EX_CONTEXT_0_0 into SP + 16
	STORE_BUNDLE(r6, 8)  // Save EX_CONTEXT_0_1 into SP + 24
	STORE_BUNDLE(r7, 8)  // Save CMPEXCH_VALUE into SP + 32
	{
	mtspr INTERRUPT_CRITICAL_SECTION, zero; // Exit from critical section
	}
	STORE_BUNDLE(r0, 8)         
	STORE_BUNDLE(r1, 56)
	STORE_BUNDLE(r8, 8)
	STORE_BUNDLE(r9, 8)
	STORE_BUNDLE(r10, 8)
	STORE_BUNDLE(r11, 8)
	STORE_BUNDLE(r12, 8)
	STORE_BUNDLE(r13, 8)
	STORE_BUNDLE(r14, 8)
	STORE_BUNDLE(r15, 8)
	STORE_BUNDLE(r16, 8)
	STORE_BUNDLE(r17, 8)
	STORE_BUNDLE(r18, 8)
	STORE_BUNDLE(r19, 8)
	STORE_BUNDLE(r20, 8)
	STORE_BUNDLE(r21, 8)
	STORE_BUNDLE(r22, 8)
	STORE_BUNDLE(r23, 8)
	STORE_BUNDLE(r24, 8)
	STORE_BUNDLE(r25, 8)
	STORE_BUNDLE(r26, 8)
	STORE_BUNDLE(r27, 8)
	STORE_BUNDLE(r28, 8)
	STORE_BUNDLE(r29, 0)
	{
	addli r2, sp, 552; // SP + 552
	}
	{
	addli r2, sp, 488; // SP + 488
	st r2, r5; // Save interrupt bit mask for handler 
	}
	{       
	st r2, r4; // Save interrupt PC
	}
// Pass arguments to C callback function, including the interrupt number via r0
// and the sigcontext pointer via r1.
	{
	addli r1, sp, 40;
	ctz r0, r5; // Count number of zero bits from LSB of interruupt bit mask
	} 
	{
	jalr r3; // Go to our entry point
	}	
// The interrupt return code does the following within its         
// critical section:         
//         
//  1.  Unmask interrupts        
//  2.  Restore sp, r1-r29, lr         
//  3.  Restore EX_CONTEXT_* registers
//  4.  Restore CMPEXCH_VALUE register         
//  5.  iret 
	{        
	addi r2, sp, 16;   // Get SP + 16 into r2 (saved EX_CONTEXT_0_0)
	movei r4, 1;       // Get a 1 for the critical section SPR 
	}        
	{	
	addli r2, sp, 552; // Get SP + 552 into r2 (saved intr mask)
	ld r7, r2;         // Load PC of interrupted insn
	}	
	{	
	ld r0, r2;         // Load interrupt mask into r0
	}        
	{        
	mtspr INTERRUPT_CRITICAL_SECTION, r4; // Enter critical section
	}        
	{        
	mtspr INTERRUPT_MASK_RESET_0, r0; // Write back interrupts
	}       
	{	
	addli r2, sp, 24; // SP + 24
	mtspr EX_CONTEXT_0_0, r7; // Write the iret PC
	}

	#define RESTORE_BUNDLE(REG, NEXT_OFFSET) \
	{ ld_add REG, r2, NEXT_OFFSET; }

  	RESTORE_BUNDLE(r3, 8) // EX_CONTEXT_0_1 in r3 temporarily 
  	RESTORE_BUNDLE(r4, 8) // CMPEXCH_VALUE in r4 temporarily
  	{	
  	mtspr EX_CONTEXT_0_1, r3; // Restore EX_CONTEXT_0_1
  	}	
	{        
	mtspr CMPEXCH_VALUE, r4;  // Restore CMPEXCH_VALUE
	}        
	RESTORE_BUNDLE(r0, 8)
	RESTORE_BUNDLE(r1, 16)
	RESTORE_BUNDLE(r3, 8)
	RESTORE_BUNDLE(r4, 8)
	RESTORE_BUNDLE(r5, 8)
	RESTORE_BUNDLE(r6, 8)
	RESTORE_BUNDLE(r7, 8)
	RESTORE_BUNDLE(r8, 8)
	RESTORE_BUNDLE(r9, 8)
	RESTORE_BUNDLE(r10, 8)
	RESTORE_BUNDLE(r11, 8)
	RESTORE_BUNDLE(r12, 8)
	RESTORE_BUNDLE(r13, 8)
	RESTORE_BUNDLE(r14, 8)
	RESTORE_BUNDLE(r15, 8)
	RESTORE_BUNDLE(r16, 8)
	RESTORE_BUNDLE(r17, 8)
	RESTORE_BUNDLE(r18, 8)
	RESTORE_BUNDLE(r19, 8)
	RESTORE_BUNDLE(r20, 8)
	RESTORE_BUNDLE(r21, 8)
	RESTORE_BUNDLE(r22, 8)
	RESTORE_BUNDLE(r23, 8)
	RESTORE_BUNDLE(r24, 8)
	RESTORE_BUNDLE(r25, 8)
	RESTORE_BUNDLE(r26, 8)
	RESTORE_BUNDLE(r27, 8)
	RESTORE_BUNDLE(r28, 8)
	{
	addli r2, sp, 480;  // SP + 480
	ld r29, r2;         // Restore r29
	}
	{	
	addi r2, sp, 56;   // r2 points to SP + 56
	ld lr, r2;         // Restore lr
	}            
	{        
	addli sp, sp, 472; // sp points to SP + 472
	ld r2, r2;         // Restore r2 
	}        
	{        
	ld sp, sp;         // Load SP_in 
	}
	{        
	iret;        
	}        
	.size trampoline_body_fixed, .-trampoline_body_fixed
   
#endif // __tilegx__
