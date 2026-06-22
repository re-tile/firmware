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
 * Panic the system.
 */

#include <stdio.h>
#include <util.h>

#include <arch/sim.h>

#ifdef __BME__
#include "../bme/bme_state.h"
#endif

/** This string is printed before each panic message.  The pointer may be
 *  modified by the user to point to a different string, but must point
 *  to a valid string (i.e., it cannot be NULL).
 */
char* panic_prefix = "panic: ";

/** Implements actual panic string formatting; not to be used directly.  Call
 *  panic() or panic_start() instead.
 */
static void
vpanic_start(const char* fmt, va_list ap)
{
  // If running in the simulator, dump a backtrace.
  __insn_mtspr(SPR_SIM_CONTROL, SIM_DUMP_SPR_ARG(SIM_DUMP_BACKTRACE));

#ifdef __BME__
  putstr(_bme_get_state()->panic_prefix);
#else
  putstr(panic_prefix);
#endif
  vprintf(fmt, ap);
  putchar('\n');

  // Notify the simulator that there was a panic.
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PANIC);
}


void
panic(const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vpanic_start(fmt, ap);
  va_end(ap);

  panic_end();
}


void
panic_start(const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vpanic_start(fmt, ap);
  va_end(ap);
}


void
panic_end()
{
#ifdef __BME__
  puts("System halted.");
#endif
  exit(1);
}


void
abort()
{
  panic("Executed abort().");
}
