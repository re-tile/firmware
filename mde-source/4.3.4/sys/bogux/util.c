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
 * Implements various miscellaneous utility stuff.
 * @file
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include <hv/hypervisor.h>

#include <arch/spr.h>

#include "bogux.h"

void
exit(int retval)
{
  (void) retval;
  hv_halt();
  /* Convince compiler this does not return. */
  while (1)
    ;
}

void
__assert(const char* msg_str)
{
  /* To save code size our input arguments are packed together as
   * one string:
   *
   * "failed_expr\0file\0line"
   */
  const char *failed_expr = msg_str;
  const char *file = failed_expr + strlen(failed_expr) + 1;
  const char *line = file + strlen(file) + 1;

  panic("assertion \"%s\" failed: file \"%s\", line %s",
        failed_expr, file, line);
}

/** Handler for a bad (unimplemented) interrupt.
 * @param int_name Interrupt name.
 * @param int_number Interrupt number.
 */
int
bad_intr(char* int_name, int int_number)
{
  panic("got unimplemented %s interrupt (#%d): addr %#lX, ics/pl %#lx",
        int_name, int_number,
        (long) __insn_mfspr(EX_CONTEXT_BX_0),
        (long) __insn_mfspr(EX_CONTEXT_BX_1));

  return INT_HAND_NO_DOWNCALL;
}
