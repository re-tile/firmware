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

/*
 * C runtime startup for the BME.
 */

#include <features.h>

#include <arch/abi.h>

/*
 * The BME does not provide the usual ELF-format stack environment to its
 * applications.  The BME runtime is passed a pointer to a configuration block
 * on the stack, and is responsible for doing things like parsing arguments.
 * We do most of that work in the C _bme_start() function, and just do what's
 * necessary here to get that function running.
 */
	.section .text._start, "ax"
	.global _start
	.type   _start,%function
        .align 8
_start:
        {        
         // Allocate stack frame callee space for _bme_start.
         addi	sp, sp, -8
        }
        {
         // Compute location where _bme_start's caller is supposed to
         // store its frame pointer.
         addi	r12, sp, 8

         // Zero out callee space for return address. Unnecessary but free.
         // This is just paranoia to help backtracing not go awry.
         st	sp, zero
        }
        {        
         // Zero out our frame pointer as far as _bme_start is concerned. */
         st	r12, zero
        }
        {
	 // Call _bme_start().  Note that we use "jal" only to set the link
	 // register value properly for backtracing, even though the call in
	 // fact never returns.
	 jal	_bme_start
        }
0:
        {
         // We should never reach here.
         nap
        }
	{
	 j	0b
         // Tell backtracer to give up (_start has no caller).
         info	INFO_OP_CANNOT_BACKTRACE
	}
	.size _start, .-_start
