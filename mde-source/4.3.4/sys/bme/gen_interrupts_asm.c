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

/**
 * @file
 * Generate the header file for the replaceable interrupt vector assembly code.
 */

#include <bme_state.h>
#include <bme/interrupts.h>

#include "../hv/asmhdr.h"

/** Structure to allow us to come up with a macro for the size of a pointer
 * to an interrupt handler.
 */
typedef struct
{
  /** Pointer to interrupt handler function  */
  bme_interrupt_handler_t* int_handler_ptr;
} bme_interrupt_handler_ptr_t;

ASMHDR_BEGIN()

ASMHDR_TYPE(bme_interrupt_handler_ptr_t, BIH)
ASMHDR_MEMBER(int_handler_ptr,)
ASMHDR_ENDTYPE()

ASMHDR_TYPE(_bme_state_t, BS)
ASMHDR_MEMBER(int_handler,)
ASMHDR_ENDTYPE()

ASMHDR_END()
