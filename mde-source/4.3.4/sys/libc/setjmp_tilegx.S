/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *   DAMAGE. TILERA FURTHER DISCLAIMS ANY LIABILITY WHATSOEVER FOR
 *   INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS OF ANY THIRD PARTY.
 */

#include <arch/spr_def.h>
        
/* PL to return to via iret in longjmp */
#define RETURN_PL 3

	.section .text.setjmp, "ax"
        .no_require_canonical_reg_names

#define FOR_EACH_CALLEE_SAVED_REG(f)                                    \
                                                        f(r30); f(r31); \
        f(r32); f(r33); f(r34); f(r35); f(r36); f(r37); f(r38); f(r39); \
        f(r40); f(r41); f(r42); f(r43); f(r44); f(r45); f(r46); f(r47); \
        f(r48); f(r49); f(r50); f(r51); f(r52); f(r53); f(r54); f(r55)
                
	.global setjmp
	.type setjmp,@function
setjmp:
#define SAVE(r) \
        { st r0,r ; addi r0,r0,8 }

        FOR_EACH_CALLEE_SAVED_REG(SAVE)
#undef SAVE

        mfspr r3, INTERRUPT_CRITICAL_SECTION
        st r0, r3

        /* We return from setjmp with "0". */
        { movei r0,0 ; jrp lr }
	.size setjmp,.-setjmp


	.section .text.longjmp, "ax"

	.global longjmp
	.type longjmp,@function
longjmp:
#define RESTORE(r) \
        { ld r, r0 ; addi r0, r0, 8 }

        FOR_EACH_CALLEE_SAVED_REG(RESTORE)
#undef RESTORE

        /*
         * Make longjmp(buf, 0) return "1" instead.
         * At the same time, construct our iret context; we set ICS so
         * we can validly load EX_CONTEXT for iret without being
         * interrupted halfway through.
         */

        {
         ld r2, r0   /* retrieve ICS bit from jmp_buf */
         movei r3, 1
         cmpeqi r0, r1, 0
        }
        {
         mtspr INTERRUPT_CRITICAL_SECTION, r3
         shli r2, r2, SPR_EX_CONTEXT_3_1__ICS_SHIFT
        }
        {
         mtspr EX_CONTEXT_3_0, lr
         ori r2, r2, RETURN_PL
        }
        {
         or r0, r1, r0
         mtspr EX_CONTEXT_3_1, r2
        }
        iret
        jrp lr    /* keep the backtracer happy */

	.size longjmp,.-longjmp
