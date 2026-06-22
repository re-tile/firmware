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
 * .init and .fini start header.
 */

.section .init
.align 8
.global _init
.hidden _init
.type   _init,%function
_init:
        { st sp, lr;  addi r12, sp, -8 } /* save lr to caller's save area */
        { st r12, sp; addi sp, sp, -16 }  /* save sp and allocate callee's save area */


.section .fini
.align 8
.global _fini
.hidden _fini
.type   _fini,%function
_fini:
        { st sp, lr;  addi r12, sp, -8 } /* save lr to caller's save area */
        { st r12, sp; addi sp, sp, -16 }  /* save sp and allocate callee's save area */
