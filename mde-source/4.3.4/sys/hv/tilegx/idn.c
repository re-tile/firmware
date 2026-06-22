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
 * I/O dynamic network routines.
 */

#include <arch/interrupts.h>
#include <arch/spr.h>

#include "idn.h"

#ifndef L1BOOT

#include "hw_config.h"
#include "drvintf.h"

#endif



int idn0_busy = 0;


/** Initialize IDN 0 and 1. */
void
init_idn()
{
  //
  // We shouldn't ever deadlock, since we theoretically use the IDN in a
  // deadlock-free manner.  That doesn't mean that the deadlock interrupt
  // can't fire; if we're prevented from handling the IDN catchall interrupt
  // for a long time while we have incoming messages, it could trigger, and
  // since it's higher priority than the catchall interrupt, we'll execute its
  // handler instead.  Our deadlock handler handler thus just does the same
  // thing as the catchall handler.  We probably don't even need to set the
  // timeout, but we make it the maximum value just to set it to something.
  //
  __insn_mtspr(SPR_IDN_DEADLOCK_TIMEOUT, 0xFFFF);

  //
  // Make sure the UDN can't starve out the IDN.  (The inverse is OK, since
  // we know the hypervisor will always drain the IDN.)  We really only
  // need to reserve 1 word for the IDN to ensure correctness, but
  // reserving 16 should increase performance for common IDN messages under
  // heavy UDN loads.
  //
  __insn_mtspr(SPR_IDN_DEMUX_BUF_THRESH, 0);
  __insn_mtspr(SPR_UDN_DEMUX_BUF_THRESH, 250 - 16);

  //
  // Interrupt on IDN 0 for all message-passing, and on IDN 1 for I/O
  // interrupts (no I/O at present, but used for fast IPIs).
  //
  __insn_mtspr(SPR_IDN_AVAIL_EN, 3);
}


#ifndef L1BOOT

/** Enable interrupts on IDN1. */
void
enable_idn1_intr()
{
  //
  // Turn on the avail interrupts and deadlock interrupts.
  //
  __insn_mtspr(SPR_IDN_AVAIL_EN, 3);
  unmask_intr(INT_IDN_AVAIL);
  unmask_intr(INT_IDN_TIMER);
}


/** Disable interrupts on IDN1. */
void
disable_idn1_intr()
{
  //
  // Turn off the avail interrupt for IDN 1; we leave it on for IDN 0
  // since it's used for messaging.
  //
  __insn_mtspr(SPR_IDN_AVAIL_EN, 1);
}


/** Initialize IDN for a dedicated driver tile. */
void
init_idn_dedicated()
{
  disable_idn1_intr();
}


/** Initialize the IDN catch-all.  */
void
init_idn_ca()
{
  unmask_intr(INT_IDN_AVAIL);
  unmask_intr(INT_IDN_TIMER);
}

#endif // !L1BOOT


