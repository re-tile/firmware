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
 * Debugging routines.
 */

#include <stdio.h>
#include <ctype.h>
#include <util.h>

#include <arch/spr.h>

#include "config.h"
#include "debug.h"
#include "filesys.h"
#include "hv.h"
#include "types.h"

#ifdef DEBUG
uint32_t debug_flags = DEBUG_DEFAULT;
#else
uint32_t debug_flags = 0;
#endif

static char my_tprintf_prefix[12]; ///< This tile's coordinates as a string
static char my_panic_prefix[24];   ///< What we'll print before a panic message

/** Initialize the debug output subsystem.
 * @param phase Initialization phase; 0 for the first call (very early in
 *        startup, before really anything else is initialized) and 1 for the
 *        second (much later, after the hvfs has been read and the
 *        configuration file has been parsed).
 */
void
debug_init(int phase)
{
  switch (phase)
  {
  case 0:
    zero_save_regs();
    snprintf(my_tprintf_prefix, sizeof (my_tprintf_prefix) - 1,
             "(%d,%d) ", UXY(my_pos));
    tprintf_prefix = my_tprintf_prefix;
    snprintf(my_panic_prefix, sizeof (my_panic_prefix) - 1,
             "(%d,%d) hv_panic: ", UXY(my_pos));
    panic_prefix = my_panic_prefix;
    break;

  case 1:
    if (config.debug != 0)
      debug_flags = config.debug;

    if (debug_flags)
      tprintf("Hypervisor debugging flags set to 0x%x\n", debug_flags);

    break;
  }
}


/** Zero all of the system save registers.  This is done because sometimes
 *  it's useful to use these for temporary debug code.
 */
void
zero_save_regs()
{
  __insn_mtspr(SPR_SYSTEM_SAVE_2_0, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_2_1, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_2_2, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_2_3, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_1_0, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_1_1, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_1_2, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_1_3, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_0_0, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_0_1, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_0_2, 0);
  __insn_mtspr(SPR_SYSTEM_SAVE_0_3, 0);
}

/** Dump out all of the system save registers.  This is handy if you're
 *  using them for some temporary debug code.
 */
void
dump_save_regs()
{
  printf("Save regs dump\n");
  printf("MPL 2: %lx %lx %lx %lx\n", (long) __insn_mfspr(SPR_SYSTEM_SAVE_2_0),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_2_1),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_2_2),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_2_3));
  printf("MPL 1: %lx %lx %lx %lx\n", (long) __insn_mfspr(SPR_SYSTEM_SAVE_1_0),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_1_1),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_1_2),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_1_3));
  printf("MPL 0: %lx %lx %lx %lx\n", (long) __insn_mfspr(SPR_SYSTEM_SAVE_0_0),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_0_1),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_0_2),
         (long) __insn_mfspr(SPR_SYSTEM_SAVE_0_3));
}
