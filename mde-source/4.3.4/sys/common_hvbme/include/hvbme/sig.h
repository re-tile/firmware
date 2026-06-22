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

#ifndef _SYS_COMMON_HVBME_SIG_H
#define _SYS_COMMON_HVBME_SIG_H

#include <stdint.h>

#include "board_info.h"

/**
 * @file
 * Routines to manipulate signals, shared between the hypervisor and the BME.
 * (The BME doesn't actually implement this yet, though.)
 */

/** Type of a signal descriptor. */
typedef struct bi_signal sigdesc_t;

/** Set a signal.  Signals are defined in a board's information block, and
 *  define connections to miscellaneous low-speed I/O devices (LEDs, reset
 *  signals for chips, etc.)  This routine allows a driver to manipulate
 *  such signals without having to worry about exactly how they're
 *  connected to the chip.
 *
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together; exactly one of
 *  SIGNAL_ASSERT and SIGNAL_DEASSERT must be specified.
 * @return Zero upon success, nonzero on failure.
 */
int set_signal(sigdesc_t sig_desc, int action);

/** Get a signal.
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together; exactly one of
 *  SIGNAL_ASSERT and SIGNAL_DEASSERT must be specified.
 * @return 1 if the specified signal is consistent with the passed
 *   ASSERT or DEASSERT flag; zero if not; or a negative error code.
 */
int get_signal(sigdesc_t sig_desc, int action);

//
// This routine isn't callable from common HV/BME plugin code, only from 
// drivers.
//
#if defined(__HV__) || defined(__BME__)

/** Set a target for signal interrupts.  Note that enable_signal_intr()
 *  must be called after this routine for any interrupts to be delivered.
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together.  Note that SIGNAL_INIT is
 *  implied by this call and need not be specified.
 * @param tile Tile to which interrupt will be sent.
 * @param event IPI event number to be used for the interrupt.
 * @return Nonzero if the specified interrupts could not be targeted, zero
 *  otherwise.  Retargeting an interrupt which is already targeted is not an
 *  error.
 */
int target_signal_intr(sigdesc_t sig_desc, int action, pos_t tile, int event);

#endif // __HV__ || __BME__

/** Enable signal interrupts.  target_signal_intr() must have already
 *  been called for the specified signal.
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together.
 * @return Nonzero if the specified interrupts could not be enabled, zero
 *  otherwise.  Enabling an interrupt which is already enabled is not an
 *  error.
 */
int enable_signal_intr(sigdesc_t sig_desc, int action);

/** Disable signal interrupts.  target_signal_intr() must have already
 *  been called for the specified signal.
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together.
 * @return Nonzero if the specified interrupts could not be disabled, zero
 *  otherwise.  Disabling an interrupt which is already disabled is not an
 *  error.
 */
int disable_signal_intr(sigdesc_t sig_desc, int action);

/** Get and clear signal interrupts.  Return information about whether the
 *  specified interrupts have occurred on the specified signal, and clear
 *  those interrupts so that future transitions of the signal cause another
 *  call to any registered interrupt routine.  target_signal_intr() must
 *  have already been called for the specified signal.
 * @param sig_desc Signal descriptor.
 * @param action SIGNAL_xxx flags, ORed together.
 * @return SIGNAL_ASSERT or SIGNAL_DEASSERT, or both ORed together, if the
 *  respective interrupts were specified in action, and have occurred.
 */
int get_clear_signal_intr(sigdesc_t sig_desc, int action);

/** Initialize anything needed to read or write the signal.  This flag must
 *  be specified on the first call to drv_set_signal() or drv_get_signal()
 *  with any particular descriptor; it's best not to specify it on later
 *  calls, although that should not cause incorrect behavior, it just wastes
 *  time.  Not necessary for any of the interrupt routines, since
 *  target_signal_intr() always does the initialization. */
#define SIGNAL_INIT       0x1

/** Assert the specified signal, or check for its assertion. */
#define SIGNAL_ASSERT     0x2

/** Deassert the specified signal, or check for its deassertion. */
#define SIGNAL_DEASSERT   0x4

#endif // _SYS_COMMON_HVBME_SIG_H
