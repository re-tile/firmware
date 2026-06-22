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
 * Routines for hardware configuration.
 */

#ifndef _SYS_HV_TILEGX_HW_CONFIG_H
#define _SYS_HV_TILEGX_HW_CONFIG_H

/** The Gx reference clock is always 125 MHz. */
#define REFCLK 125000000L

#include "drvchan.h"

#ifndef __ASSEMBLER__

#include <arch/interrupts.h>

#include "fuses.h"
#include "hv.h"
#include "tile.h"

//
// Scaled frequency conversion macros.  In order to save space in the
// fuses, we use a 10-bit representation for the frequency in frequency-to-
// voltage table entries.  The stored value is in units of 2 MHz, and is
// offset so that a stored value of 0 is equivalent to running at 200 MHz.
//

/** Convert a scaled frequency (as found in the frequency-to-voltage table
 *  in the chip's fuses) to an actual frequency.
 * @param scaled_freq 10-bit scaled frequency.
 * @return Actual frequency in hertz.
 */
#define SCALED_FREQ_TO_FREQ(scaled_freq) \
  (((scaled_freq) + 100) * 2L * 1000 * 1000)

/** Convert an actual frequency to a scaled frequency (as found in the
 *  frequency-to-voltage table in the chip's fuses).
 * @param freq Actual frequency in hertz.
 * @return 10-bit scaled frequency.
 */
#define FREQ_TO_SCALED_FREQ(freq) \
  ((freq) / (2 * 1000 * 1000) - 100)

//
// VID conversion macros.  The core voltage is controlled by a 6-bit ID
// value which goes to the power controller; a value of 0 means 1.2125 V,
// a value of 63 means .818750 V, and it steps evenly between those two
// values (so, by .00625 V/step).
//

/** Convert VID bits (the 6-bit subset of the 8-bit VR11 code which is
 *  supported by the processor) to an actual voltage.
 * @param vid_bits 6-bit VID code.
 * @return Voltage in uV.
 */
#define VID_TO_UV(vid_bits) \
  (818750 + (63 - (vid_bits)) * 6250)

/** Convert voltage to VID bits.
 * @param voltage_uv Voltage in uV.
 * @return 6-bit VID subset code.
 */
#define UV_TO_VID(voltage_uv) \
  (63 - ((voltage_uv) - 818750 + 6250 - 1) / 6250)

int loadline_chip_to_vid(unsigned int* uv);
int loadline_vid_to_chip(unsigned int* uv);

unsigned int freq_to_volt(long freq, struct f2v_entry* ftable,
                          int ftable_entries);
long volt_to_freq(unsigned int voltage_uv, struct f2v_entry* ftable,
                  int ftable_entries);
long freq_to_pll(long freq, unsigned int* ret_m, unsigned int *ret_n,
                 unsigned int *ret_q, unsigned int* ret_range, long refclk,
                 int round_up);
long pll_to_freq(unsigned int bypass, unsigned int m, unsigned int n,
                 unsigned int q, long refclk);
void freq_to_pll_fb(long freq, unsigned int* ret_m, unsigned int *ret_n,
                    unsigned int *ret_q, unsigned int* ret_range,
                    long refclk);
long pll_to_freq_fb(unsigned int bypass, unsigned int m, unsigned int n,
                    unsigned int q, long refclk);
void set_vid(pos_t rshimaddr, unsigned int new_vid);
int get_fuses(pos_t rshimaddr, union fuses* fuses);
int get_voltage_range(unsigned int* min_v, unsigned int* max_v);

#ifdef L1BOOT

void set_cclk_duty(int steps);

#else /* L1BOOT */

/** Unmask an interrupt.
 * @param intnum Interrupt number to unmask.
 */
static inline void
unmask_intr(int intnum)
{
  __insn_mtspr(INTERRUPT_MASK_RESET_HV, INT_MASK(intnum));
}

/** Mask an interrupt.
 * @param intnum Interrupt number to Mask.
 */
static inline void
mask_intr(int intnum)
{
  __insn_mtspr(INTERRUPT_MASK_SET_HV, INT_MASK(intnum));
}

void set_intrs(void);
void set_error_enable(void);
void setup_networks(void);
void setup_board(void);
void setup_misc(void);
void douse_fail_led(void);
long set_speed(unsigned long freq, unsigned long flags);

#endif /* L1BOOT */

#endif /* !__ASSEMBLER__ */

//
// Define the hypervisor's interrupt masks.  We have three classes of
// interrupts:
//
// - Those we always want to handle.  These are unmasked at startup,
//   and stay unmasked, except perhaps while we're handling an instance of
//   that event and haven't reset whatever state triggers the interrupt.
//   Interrupts which we don't ever expect to fire (like BOOT_ACCESS) are
//   here too, so if they _do_ fire we find out about it with a panic.
//   Note that some of these interrupts may also be handled by lower PLs.
//
//   There are some interrupts in this class that, honestly, we'd rather
//   not deal with, but they're not maskable in the hardware.  Unmaskable
//   interrupts are marked with an asterisk in the list below.  They're in
//   the "Unhandled by HV" column if we expect the supervisor to deal with
//   them; we still have an HV handler for those interrupts in case the
//   supervisor doesn't take care of it.
//
// - Those that we want to handle, but don't want to have asserted while
//   we're in the hypervisor.  A good example is the INTCTRL_3 interrupt;
//   we have to handle it, but it's a lot easier if we don't have to worry
//   about it happening while we're doing something else in the hypervisor,
//   so we only handle it when it happens at or below PL2. These are explicitly
//   masked upon entrance to the hypervisor and explicitly unmasked upon exit.
//
// - Those we never want to handle.  These are things which we expect the
//   supervisor or user program to take care of, like UDN interrupts, or
//   things which we don't worry about yet (like the performance counters).
//   These are masked at startup and stay that way.
//
// FIXME: GX: these masks are set up assuming the hypervisor is using the
// IDN, which will eventually not be the case.  Also, we may eventually
// handle some of the single step traps.
//
// Handled by HV             Handled by HV,          Unhandled by HV
//                           taken in SV/US
//
// INT_MEM_ERROR
//                                                   INT_SINGLE_STEP_3
//                                                   INT_SINGLE_STEP_2
//                                                   INT_SINGLE_STEP_1
//                                                   INT_SINGLE_STEP_0
//                                                   INT_IDN_COMPLETE
//                                                   INT_UDN_COMPLETE
//
// INT_ITLB_MISS*
// INT_ILL* (also at PL1)
// INT_ILL_TRANS* (also at PL1)
// INT_GPV* (also at PL1)
// INT_IDN_ACCESS*
//                                                   INT_UDN_ACCESS*
// INT_SWINT_3*
// INT_SWINT_2*
//                                                   INT_SWINT_1*
//                                                   INT_SWINT_0*
// INT_UNALIGN_DATA* (also at PL1)
// INT_DTLB_MISS*
// INT_DTLB_ACCESS*
// INT_IDN_FIREWALL
//                                                   INT_UDN_FIREWALL
//                                                   INT_TILE_TIMER
// INT_IDN_TIMER
//                                                   INT_UDN_TIMER
// INT_IDN_AVAIL
//                                                   INT_UDN_AVAIL
//                           INT_IPI_HV
//                                                   INT_IPI_CL
//                                                   INT_IPI_1
//                                                   INT_IPI_0
//                                                   INT_PERF_COUNT
//                                                   INT_AUX_PERF_COUNT
//                           INT_INTCTRL_HV
//                                                   INT_INTCTRL_CL
//                                                   INT_INTCTRL_1
//                                                   INT_INTCTRL_0
// INT_BOOT_ACCESS*
// INT_WORLD_ACCESS*
// INT_I_ASID*
// INT_D_ASID*
// INT_DOUBLE_FAULT*
//








/** Configuration-dependent interrupts handled by hypervisor */
#define HV_INTS_HANDLED_EXTRA    0
/** Configuration-dependent interrupts handled by hypervisor, taken in
 *  supervisor */
#define HV_INTS_HANDLED_SV_EXTRA INT_MASK(INT_IPI_HV)


/** Interrupts the hypervisor handles */
#define HV_INTS_HANDLED ( \
        INT_MASK(INT_MEM_ERROR) | \
        INT_MASK(INT_ITLB_MISS) | \
        INT_MASK(INT_ILL) | \
        INT_MASK(INT_ILL_TRANS) | \
        INT_MASK(INT_GPV) | \
        INT_MASK(INT_IDN_ACCESS) | \
        INT_MASK(INT_SWINT_3) | \
        INT_MASK(INT_SWINT_2) | \
        INT_MASK(INT_UNALIGN_DATA) | \
        INT_MASK(INT_DTLB_MISS) | \
        INT_MASK(INT_DTLB_ACCESS) | \
        INT_MASK(INT_IDN_FIREWALL) | \
        INT_MASK(INT_IDN_TIMER) | \
        INT_MASK(INT_IDN_AVAIL) | \
        INT_MASK(INT_BOOT_ACCESS) | \
        INT_MASK(INT_WORLD_ACCESS) | \
        INT_MASK(INT_I_ASID) | \
        INT_MASK(INT_D_ASID) | \
        INT_MASK(INT_DOUBLE_FAULT) | \
        HV_INTS_HANDLED_EXTRA | \
        0)

/** Interrupts the hypervisor handles, but which only happen while running the
 *  supervisor */
#define HV_INTS_HANDLED_SV ( \
        INT_MASK(INT_INTCTRL_HV) | \
        HV_INTS_HANDLED_SV_EXTRA | \
        0)

/** Interrupts the hypervisor ignores */
#define HV_INTS_UNHANDLED ( \
        INT_MASK(INT_SINGLE_STEP_3) | \
        INT_MASK(INT_SINGLE_STEP_2) | \
        INT_MASK(INT_SINGLE_STEP_1) | \
        INT_MASK(INT_SINGLE_STEP_0) | \
        INT_MASK(INT_IDN_COMPLETE) | \
        INT_MASK(INT_UDN_COMPLETE) | \
        INT_MASK(INT_UDN_ACCESS) | \
        INT_MASK(INT_SWINT_1) | \
        INT_MASK(INT_SWINT_0) | \
        INT_MASK(INT_UDN_FIREWALL) | \
        INT_MASK(INT_TILE_TIMER) | \
        INT_MASK(INT_UDN_TIMER) | \
        INT_MASK(INT_UDN_AVAIL) | \
        INT_MASK(INT_IPI_CL) | \
        INT_MASK(INT_IPI_1) | \
        INT_MASK(INT_IPI_0) | \
        INT_MASK(INT_PERF_COUNT) | \
        INT_MASK(INT_AUX_PERF_COUNT) | \
        INT_MASK(INT_INTCTRL_CL) | \
        INT_MASK(INT_INTCTRL_1) | \
        INT_MASK(INT_INTCTRL_0) | \
        0)

/** IPI events the hypervisor handles, but which we only want enabled while
 *  running the supervisor */

#define HV_EVENTS_HANDLED_SV  ~((ONE64 << DRV_CHAN_MESSAGE) | \
                                (ONE64 << DRV_CHAN_REPLY))

#endif /* _SYS_HV_TILEGX_HW_CONFIG_H */
