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

#include <limits.h>
#include <string.h>

#include <arch/diag.h>
#include <arch/interrupts.h>
#include <arch/jtag_drs_def.h>
#include <arch/rsh.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include <hvbme/jtag.h>

#include "board_info.h"
#include "boot_error.h"
#include "client_obj.h"
#include "debug.h"
#include "devices.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "lock.h"
#include "mapping.h"
#include "srom_acc.h"
#include "temp_sens.h"
#include "util.h"


//
// The first part of this file contains code shared between the hypervisor
// and L1 booter, related to speed and voltage settings; the remainder
// of the file is used only by the hypervisor.
//

//
// Load line adjustment.
//

/** Load line factor, from BIB. */
static int load_line_factor;
/** Load line offset, from BIB. */
static int load_line_offset;

/** Set load line factor and offset if needed, then tell the caller if they
 *  need to be used.
 * @return Nonzero if there is a load line adjustment; zero if not.
 */
static int
check_load_line()
{
  static int load_line_checked = 0;
  static int load_line = 0;

  if (!load_line_checked)
  {
    load_line_checked = 1;

    bi_ptr_t bi;
    if (bi_getparam(BI_TYPE_CPU_VOLT_CHAR, -1, &bi, NULL) != BI_NULL)
    {
      struct bi_cpu_volt_char* cvc = (struct bi_cpu_volt_char*) bi;

      if (cvc->load_line)
      {
        load_line = 1;

        if (cvc->load_line_factor == 0 && cvc->load_line_offset == 0)
        {
          //
          // Values for EVRD11.1 regulator on a -12 Gx36; here to support a
          // very few early Duo systems which don't include proper values in
          // the BIB.
          //
          load_line_factor = 117900;
          load_line_offset = -5;
        }
        else
        {
          load_line_factor = cvc->load_line_factor;
          load_line_offset = cvc->load_line_offset;
        }
      }
    }
  }

  return load_line;
}


/** Convert a target voltage (in uv) at the chip to the voltage that we
 *  need to set in the VID pins to achieve the target.
 * @param uv Pointer to the target voltage, which will be overwritten with
 *  the VID voltage if necessary.
 * @return Nonzero if a load line adjustment was performed, else zero.
 */
int
loadline_chip_to_vid(unsigned int* uv)
{
  if (!check_load_line())
    return 0;

  *uv = (((1000000 + load_line_factor) * (uint64_t) *uv) / 1000000) +
        load_line_offset * 6250;

  return 1;
}


/** Convert a voltage (in uv) at the VID pins to the voltage that we will
 *  achieve in practice at the chip.
 * @param uv Pointer to the VID voltage, which will be overwritten with
 *  the target voltage if necessary.
 * @return Nonzero if a load line adjustment was performed, else zero.
 */
int
loadline_vid_to_chip(unsigned int* uv)
{
  if (!check_load_line())
    return 0;

  *uv = (((uint64_t) *uv - load_line_offset * 6250) * 1000000) /
        (1000000 + load_line_factor);

  return 1;
}


/** Convert frequency to voltage by means of a frequency-to-voltage
 *  lookup table.  Frequencies lower than the reference clock, or higher
 *  than the highest lookup table entry will be clipped to those extremes.
 *  Voltage are calculated by linear interpolation between table entries
 *  when necessary; as a special case, frequencies between the reference
 *  clock and the lowest f2v entry are allowed, at the voltage found in the
 *  latter.
 * @param freq Requested frequency in Hertz.
 * @param ftable List of frequency-to-voltage table entries, ordered in
 *  increasing order of frequency.  Trailing entries in this table which
 *  are all 0s or all 1s will be ignored, but there must be at least one
 *  valid entry.
 * @param ftable_entries Number of entries in ftable.
 * @return Voltage in uV corresponding to the (possibly clipped) frequency.
 */
unsigned int
freq_to_volt(long freq, struct f2v_entry* ftable, int ftable_entries)
{
  //
  // Reduce the table size if there are trailing invalid entries.
  //
  while (ftable_entries > 1 && ftable[ftable_entries - 1].vid == 0)
    ftable_entries--;

  //
  // Clip the frequency to be within the valid limits.
  //
  freq = min(freq, SCALED_FREQ_TO_FREQ(ftable[ftable_entries - 1].freq));
  freq = max(freq, REFCLK);
    
  //
  // We add an initial virtual point to the map, consisting of the
  // reference clock frequency and the voltage from the first real
  // map point.  This allows the use of frequencies below the lowest
  // tested one, at the same voltage.
  //
  uint32_t prev_f = REFCLK;
  uint32_t prev_v = VID_TO_UV(ftable[0].vid);

  //
  // Go through the table, looking for an exact match for our speed,
  // or two points which enclose the speed, then calculate the right
  // voltage.
  //
  for (int i = 0; i < ftable_entries; i++)
  {
    uint32_t this_f = SCALED_FREQ_TO_FREQ(ftable[i].freq);
    uint32_t this_v = VID_TO_UV(ftable[i].vid);

    if (freq <= this_f && freq >= prev_f)
      return (int) (prev_v + ((uint64_t) freq - prev_f) *
                              ((uint64_t) this_v - prev_v) /
                              ((uint64_t) this_f - prev_f));

    prev_f = this_f;
    prev_v = this_v;
  }

  //
  // We didn't find a voltage, so panic.  (Since we clipped the
  // frequency to fit, this must mean that the table is out of order.)
  //
#ifdef L1BOOT
  boot_error(BOOT_ERR_BAD_F2V_TABLE);
  return 0;
#else
  panic("requested speed of %ld MHz out of legal range "
        "(%d MHz to %d MHz)\n", freq / (1000 * 1000),
        refclk_speed / (1000 * 1000), prev_f / (1000 * 1000));
#endif
}


/** Convert voltage to frequency by means of a frequency-to-voltage
 *  lookup table.  Voltages lower than the lowest lookup table entry, or
 *  higher than the highest lookup table entry, will be clipped to those
 *  extremes.  Frequencies are calculated by linear interpolation between
 *  table entries.  The highest frequency achievable with the given
 *  voltage is returned.
 * @param voltage_uv Pointer to the input voltage in uV.
 * @param ftable List of frequency-to-voltage table entries, ordered in
 *  increasing order of frequency.  Trailing entries in this table which
 *  are all 0s or all 1s will be ignored, but there must be at least one
 *  valid entry.
 * @param ftable_entries Number of entries in ftable.
 * @return Frequency in Hertz corresponding to the (possibly clipped) voltage.
 */
long
volt_to_freq(unsigned int voltage_uv, struct f2v_entry* ftable,
             int ftable_entries)
{
  //
  // Reduce the table size if there are trailing invalid entries.
  //
  while (ftable_entries > 1 && ftable[ftable_entries - 1].vid == 0)
    ftable_entries--;

  //
  // Clip the voltage to be within the valid limits.
  //
  voltage_uv = max(voltage_uv, VID_TO_UV(ftable[0].vid));
  voltage_uv = min(voltage_uv, VID_TO_UV(ftable[ftable_entries - 1].vid));
    
  //
  // If there's only one point in the voltage map, we just return that
  // frequency.
  //
  if (ftable_entries == 1)
    return SCALED_FREQ_TO_FREQ(ftable[0].freq);

  //
  // Go through the table, looking for an exact match for our speed,
  // or two points which enclose the speed, then calculate the right
  // voltage.  We go from the top down since we want to return the
  // largest frequency in the case where we have multiple points at
  // the same voltage.
  //
  uint32_t prev_f = SCALED_FREQ_TO_FREQ(ftable[ftable_entries - 1].freq);
  uint32_t prev_v = VID_TO_UV(ftable[ftable_entries - 1].vid);

  for (int i = ftable_entries - 2; i >= 0; i--)
  {
    uint32_t this_f = SCALED_FREQ_TO_FREQ(ftable[i].freq);
    uint32_t this_v = VID_TO_UV(ftable[i].vid);

    if (voltage_uv <= prev_v && voltage_uv >= this_v)
    {
      //
      // It's conceivable that the table just has a valid range of
      // frequencies, all of which are attainable at the same voltage;
      // check for that case so we don't divide by zero.
      //
      if (this_v == prev_v)
        return prev_f;
      else
        return (int) (this_f + ((uint64_t) voltage_uv - this_v) *
                                ((uint64_t) prev_f - this_f) /
                                ((uint64_t) prev_v - this_v));
    }

    prev_f = this_f;
    prev_v = this_v;
  }

  //
  // We didn't find a frequency, so panic.  (Since we clipped the
  // voltage to fit, this must mean that the table is out of order.)
  //
#ifdef L1BOOT
  boot_error(BOOT_ERR_BAD_F2V_TABLE);
  return 0;
#else
  panic("requested voltage of %d uv out of legal range "
        "(%d uv to %d uv)\n", voltage_uv, VID_TO_UV(ftable[0].vid),
        VID_TO_UV(ftable[ftable_entries - 1].vid));
#endif
}


/** Compute a PLL range value.
 * @param n N value; the reference clock divisor is N + 1.
 * @param refclk Reference clock.
 * @return Value for the range component of the PLL.
 */
static int
pll_range_val(int n, long refclk)
{
  /**
   * This is a mapping for the RANGE input to the PLL; the index of the
   * array is the RANGE value, and the value of the array at that slot is
   * the maximum divided reference clock frequency we can use for that
   * range.
   */
  static const int pll_ranges[] =
  {
      0,
     14 * 1000 * 1000,
     16 * 1000 * 1000,
     26 * 1000 * 1000,
     42 * 1000 * 1000,
     65 * 1000 * 1000,
    104 * 1000 * 1000,
    166 * 1000 * 1000,
    300 * 1000 * 1000,
  };

  uint32_t div_refclk = refclk / (n + 1);
  for (int i = 0; i < sizeof (pll_ranges) / sizeof (pll_ranges[0]); i++)
    if (div_refclk <= pll_ranges[i])
      return i - 1;

#ifdef L1BOOT
  boot_error(BOOT_ERR_BAD_F2PLL);
  return 0;
#else
  panic("couldn't compute PLL range for refclk %ld n %d", refclk, n);
#endif
}


/** Convert frequency to phase-locked-loop parameters.
 * @param freq Input frequency in Hertz.
 * @param ret_m Pointer to returned M value.
 * @param ret_n Pointer to returned N value.
 * @param ret_q Pointer to returned Q value.
 * @param ret_range Pointer to returned range value.
 * @param refclk Reference clock.
 * @param round_up If nonzero, round up to the next legal frequency if
 *  needed; if zero, round down.
 * @return Achieved frequency in hertz.
 */
long
freq_to_pll(long freq, unsigned int* ret_m, unsigned int *ret_n,
            unsigned int *ret_q, unsigned int* ret_range, long refclk,
            int round_up)
{
  unsigned long best_freq = (round_up) ? ULONG_MAX : 0;
  unsigned int best_m = 0;
  unsigned int best_n = 0;
  unsigned int best_q = 0;

  //
  // Essentially we're going to try all of the possible values for the
  // m, n, and q parameters, and pick the one which gets us closest to
  // the speed we want without going over.  We have some constraints that
  // we must obey, which cut down the search space.  We order the search
  // so that if there are multiple sets of parameters which get us to the
  // desired frequency, we pick the one with the highest Fvco value.
  //
  const long PLL_DIVREF_MIN =  14000000L;
  const long PLL_DIVREF_MAX = 200000000L;

  const long PLL_FVCO_MIN =  2133000000L;
  const long PLL_FVCO_MAX =  4266000000L;

  const int PLL_N_MAX = 31;
  const int PLL_M_MAX = 255;
  const int PLL_Q_MAX = 6;

  //
  // The divided reference clock must be within a certain range, so we
  // only try N values which satisfy that constraint.
  //
  int n_low = max(refclk / PLL_DIVREF_MAX - 1, 0);
  int n_high = min(refclk / PLL_DIVREF_MIN - 1, PLL_N_MAX);

  for (int n = n_low; n <= n_high; n++)
  {
    //
    // Fvco, which is equal to the reference clock times 2 times (m + 1)
    // divided by (n + 1), must be within a certain range.  We invert
    // the test so that we can calculate the m values which satisfy that
    // constraint, given the current n value.
    //
    int m_low = (((n + 1) * PLL_FVCO_MIN / 2 + refclk - 1) / refclk) - 1;
    int m_high = min((((n + 1) * PLL_FVCO_MAX / 2) / refclk) - 1, PLL_M_MAX);

    for (int m = m_high; m >= m_low; m--)
    {
      //
      // Now that we have m and n, we try all of the q values.
      //
      if (round_up)
      {
        for (int q = PLL_Q_MAX; q >= 0; q--)
        {
          unsigned long this_freq = pll_to_freq(0, m, n, q, refclk);

          //
          // We only consider speeds higher than the requested speed.
          //
          if (this_freq >= freq)
          {
            //
            // If it's better than the last best speed, update the best speed.
            //
            if (this_freq < best_freq)
            {
              //
              // If it's exactly what we want, we can just return right now.
              //
              if (this_freq == freq)
              {
                *ret_m = m;
                *ret_n = n;
                *ret_q = q;
                *ret_range = pll_range_val(n, refclk);

                return this_freq;
              }

              best_freq = this_freq;
              best_m = m;
              best_n = n;
              best_q = q;
            }
            //
            // Since lower values of q increase the speed, once we've found
            // one that's more than the desired speed, all others in this
            // loop will be even further away from the desired speed, so
            // there's no reason to try them.
            //
            break;
          }
        }
      }
      else
      {
        for (int q = 0; q <= PLL_Q_MAX; q++)
        {
          unsigned long this_freq = pll_to_freq(0, m, n, q, refclk);

          //
          // We only consider speeds lower than the requested speed.
          //
          if (this_freq <= freq)
          {
            //
            // If it's better than the last best speed, update the best speed.
            //
            if (this_freq > best_freq)
            {
              //
              // If it's exactly what we want, we can just return right now.
              //
              if (this_freq == freq)
              {
                *ret_m = m;
                *ret_n = n;
                *ret_q = q;
                *ret_range = pll_range_val(n, refclk);

                return this_freq;
              }

              best_freq = this_freq;
              best_m = m;
              best_n = n;
              best_q = q;
            }
            //
            // Since higher values of q decrease the speed, once we've found
            // one that's less than the desired speed, all others in this loop
            // will be even further away from the desired speed, so there's no
            // reason to try them.
            //
            break;
          }
        }
      }
    }
  }

  if (best_freq > 0)
  {
    *ret_m = best_m;
    *ret_n = best_n;
    *ret_q = best_q;
    *ret_range = pll_range_val(best_n, refclk);

    return best_freq;
  }

#ifdef L1BOOT
  boot_error(BOOT_ERR_BAD_F2PLL);
#else
  panic("couldn't compute PLL values for freq %ld", freq);
#endif

  //
  // Note that we never actually return here, this is just to avoid
  // compiler complaints.  We can't mark boot_error() __noreturn__
  // since it behaves differently for different error codes.
  //
  return 0;
}


/** Convert frequency to phase-locked-loop parameters, for a PLL in
 *  feedback mode.  This is used for the mshim's deskew PLL.
 * @param freq Input frequency in Hertz.
 * @param ret_m Pointer to returned M value.
 * @param ret_n Pointer to returned N value.
 * @param ret_q Pointer to returned Q value.
 * @param ret_range Pointer to returned range value.
 * @param refclk Reference clock.
 */
void
freq_to_pll_fb(long freq, unsigned int* ret_m, unsigned int *ret_n,
               unsigned int *ret_q, unsigned int* ret_range, long refclk)
{
  unsigned long best_freq = 0;
  unsigned int best_m = 0;
  unsigned int best_n = 0;
  unsigned int best_q = 0;

  //
  // Essentially we're going to try all of the possible values for the
  // m, n, and q parameters, and pick the one which gets us closest to
  // the speed we want without going over.  We have some constraints that
  // we must obey, which cut down the search space.  We order the search
  // so that if there are multiple sets of parameters which get us to the
  // desired frequency, we pick the one with the highest Fvco value.
  //
  const long PLL_DIVREF_MIN =  14000000L;
  const long PLL_DIVREF_MAX = 200000000L;

  const long PLL_FVCO_MIN =  2133000000L;
  const long PLL_FVCO_MAX =  4266000000L;

  const int PLL_N_MAX = 31;
  const int PLL_M_MAX = 255;
  const int PLL_Q_MAX = 6;

  //
  // The divided reference clock must be within a certain range, so we
  // only try N values which satisfy that constraint.
  //
  int n_low = max(refclk / PLL_DIVREF_MAX - 1, 0);
  int n_high = min(refclk / PLL_DIVREF_MIN - 1, PLL_N_MAX);

  for (int n = n_low; n <= n_high; n++)
  {
    //
    // We can't do the trick of limiting the m values here, as we do in
    // the non-feedback case, since Fvco depends upon q.  The feedback
    // case is uncommon so we just try all possible m values.
    //
    int m_low = 0;
    int m_high = PLL_M_MAX;

    for (int m = m_high; m >= m_low; m--)
    {
      //
      // Now that we have m and n, we try all of the q values.
      //
      for (int q = 0; q <= PLL_Q_MAX; q++)
      {
        unsigned long fvco = (refclk * (1 << q) * (m + 1)) / (n + 1);

        if (fvco < PLL_FVCO_MIN || fvco > PLL_FVCO_MAX)
          continue;

        unsigned long this_freq = pll_to_freq_fb(0, m, n, q, refclk);

        //
        // We only consider speeds no greater than the requested speed.
        //
        if (this_freq <= freq)
        {
          //
          // If it's better than the last best speed, update the best speed.
          //
          if (this_freq > best_freq)
          {
            //
            // If it's exactly what we want, we can just return right now.
            //
            if (this_freq == freq)
            {
              *ret_m = m;
              *ret_n = n;
              *ret_q = q;
              *ret_range = pll_range_val(n, refclk);

              return;
            }

            best_freq = this_freq;
            best_m = m;
            best_n = n;
            best_q = q;
          }
          //
          // Since higher values of q decrease the speed, once we've found
          // one that's less than the desired speed, all others in this loop
          // will be even further away from the desired speed, so there's no
          // reason to try them.
          //
          break;
        }
      }
    }
  }

  if (best_freq > 0)
  {
    *ret_m = best_m;
    *ret_n = best_n;
    *ret_q = best_q;
    *ret_range = pll_range_val(best_n, refclk);

    return;
  }

#ifdef L1BOOT
  boot_error(BOOT_ERR_BAD_F2PLL);
#else
  panic("couldn't compute PLL values for freq %ld", freq);
#endif
}


/** Convert phase-locked-loop parameters to frequency.
 * @param bypass Bypass value.
 * @param m M value.
 * @param n N value.
 * @param q Q value.
 * @param refclk Reference clock.
 * @return Frequency in Hertz.
 */
long
pll_to_freq(unsigned int bypass, unsigned int m, unsigned int n,
            unsigned int q, long refclk)
{
  return bypass ? refclk : (((refclk) * 2 * (m + 1)) / (n + 1)) >> q;
}


/** Convert phase-locked-loop parameters to frequency, for a PLL in
 *  feedback mode.  This is used for the mshim's deskew PLL.
 * @param bypass Bypass value.
 * @param m M value.
 * @param n N value.
 * @param q Q value.
 * @param refclk Reference clock.
 * @return Frequency in Hertz.
 */
long
pll_to_freq_fb(unsigned int bypass, unsigned int m, unsigned int n,
               unsigned int q, long refclk)
{
  return bypass ? refclk : (refclk * (m + 1)) / (n + 1);
}


/** Safely set the processor core VID pins to a new value.
 * @param rshimaddr Address of the rshim.
 * @param vid Desired value for the VID pins.
 */
void
set_vid(pos_t rshimaddr, unsigned int vid)
{
  int start_vid = cfg_rd(rshimaddr.word, 0, RSH_VID_CORE);

  if (vid == start_vid)
    return;

  int dir = vid < start_vid ? -1 : 1;

  //
  // The VR11 spec requires that we make any dynamic voltage changes in a
  // stepped fashion; i.e., we must visit each VID value between the one
  // we're on and the one we want to get to, we can't just jump to the new
  // one.  We must wait 1.5 us between each 6.25 mV step, and then once we
  // get to the end, the power controller isn't guaranteed to catch up with
  // us for another 5 us (if we stepped less than 50 uV) or 15 us (if we
  // stepped more).
  //
  int next_vid = start_vid;
  do
  {
    next_vid += dir;
    cfg_wr(rshimaddr.word, 0, RSH_VID_CORE, next_vid);
    drv_udelay(2);
  } while (next_vid != vid);

  if (abs(start_vid - (int) vid) >= (50000 / 6250))
    drv_udelay(15);
  else
    drv_udelay(5);
}


/** Define a frequency-to-voltage table entry. */
#define F2V_ELEM(f, v)  \
      { .freq = FREQ_TO_SCALED_FREQ((f)), .vid = UV_TO_VID((v)) }

/** Default values to use for Gx36 fuses if the fuses aren't programmed. */
static const union fuses default_fuses_gx36 =
{
  .data =
  {
    .f2v =
    {
      // Core
      [FUSE_F2V_CORE]           = F2V_ELEM( 900000000,  900000),
      [FUSE_F2V_CORE + 1]       = F2V_ELEM(1000000000,  950000),
      [FUSE_F2V_CORE + 2]       = F2V_ELEM(1200000000, 1050000),

      // MSH
      [FUSE_F2V_MSH]            = F2V_ELEM(1334000000,  900000),
      [FUSE_F2V_MSH + 1]        = F2V_ELEM(1600000000,  950000),
      [FUSE_F2V_MSH + 2]        = F2V_ELEM(1866000000, 1050000),

      // Crypto
      [FUSE_F2V_CRYPTO]         = F2V_ELEM( 750000000,  850000),
      [FUSE_F2V_CRYPTO + 1]     = F2V_ELEM(1000000000, 1000000),

      // Compression
      [FUSE_F2V_COMPRESS]       = F2V_ELEM( 650000000,  900000),
      [FUSE_F2V_COMPRESS + 1]   = F2V_ELEM( 750000000, 1000000),

      // mPIPE core
      [FUSE_F2V_MPIPE_CORE]     = F2V_ELEM( 900000000,  900000),
      [FUSE_F2V_MPIPE_CORE + 1] = F2V_ELEM(1000000000, 1000000),

      // mPIPE classifer
      [FUSE_F2V_MPIPE_CLS]      = F2V_ELEM(1600000000,  900000),
      [FUSE_F2V_MPIPE_CLS + 1]  = F2V_ELEM(1800000000, 1000000),

      // Trio
      [FUSE_F2V_TRIO]           = F2V_ELEM( 720000000,  900000),
      [FUSE_F2V_TRIO + 1]       = F2V_ELEM( 800000000,  950000),

      // USB
      [FUSE_F2V_USB]            = F2V_ELEM( 336000000,  900000),
      [FUSE_F2V_USB + 1]        = F2V_ELEM( 336000000,  950000),
    },
    .duty_cycle_adjust = 0,
    .duty_cycle_adjust_valid = 0,
  }
};


/** Default values to use for Gx72 fuses if the fuses aren't programmed. */
static const union fuses default_fuses_gx72 =
{
  .data =
  {
    .f2v =
    {
      // Core
      [FUSE_F2V_CORE]           = F2V_ELEM( 850000000,  900000),
      [FUSE_F2V_CORE + 1]       = F2V_ELEM(1000000000,  975000),

      // MSH
      [FUSE_F2V_MSH]            = F2V_ELEM(1334000000,  900000),
      [FUSE_F2V_MSH + 1]        = F2V_ELEM(1600000000,  950000),
      [FUSE_F2V_MSH + 2]        = F2V_ELEM(1866000000, 1050000),

      // Crypto
      [FUSE_F2V_CRYPTO]         = F2V_ELEM( 750000000,  850000),
      [FUSE_F2V_CRYPTO + 1]     = F2V_ELEM(1000000000, 1000000),

      // Compression
      [FUSE_F2V_COMPRESS]       = F2V_ELEM( 650000000,  900000),
      [FUSE_F2V_COMPRESS + 1]   = F2V_ELEM( 750000000, 1000000),

      // mPIPE core
      [FUSE_F2V_MPIPE_CORE]     = F2V_ELEM( 900000000,  900000),
      [FUSE_F2V_MPIPE_CORE + 1] = F2V_ELEM(1000000000, 1000000),

      // mPIPE classifer
      [FUSE_F2V_MPIPE_CLS]      = F2V_ELEM(1600000000,  900000),
      [FUSE_F2V_MPIPE_CLS + 1]  = F2V_ELEM(1800000000, 1000000),

      // Trio
      [FUSE_F2V_TRIO]           = F2V_ELEM( 720000000,  900000),
      [FUSE_F2V_TRIO + 1]       = F2V_ELEM( 800000000,  950000),

      // USB
      [FUSE_F2V_USB]            = F2V_ELEM( 336000000,  900000),
      [FUSE_F2V_USB + 1]        = F2V_ELEM( 336000000,  950000),
    },
    .duty_cycle_adjust = 0,
    .duty_cycle_adjust_valid = 0,
  }
};


/** Get the fuses from the hardware, or, if unavailable, use default
 *  values.
 * @param rshimaddr Address of the rshim.
 * @param fuses Pointer to the returned fuse data.
 * @return 0 if the fuses came from the rshim, 1 if default values were
 *  used.
 */
int
get_fuses(pos_t rshimaddr, union fuses* fuses)
{
  int rv = 0;

  int fuse_first_word = is_gx72 ? FUSE_FIRST_WORD_GX72 : FUSE_FIRST_WORD_GX36;

  for (int i = 0; i < FUSE_NUM_WORDS; i++)
  {
    RSH_EFUSE_CTL_t rec;

    rec.index = fuse_first_word + i;
    cfg_wr(rshimaddr.word, 0, RSH_EFUSE_CTL, rec.word);

    do
    {
      rec.word = cfg_rd(rshimaddr.word, 0, RSH_EFUSE_CTL);
    }
    while (rec.read_pend);

    fuses->words[i] = cfg_rd(rshimaddr.word, 0, RSH_EFUSE_DATA);
  }

  if (fuses->words[0] == 0 || fuses->words[0] == ~(uint64_t) 0)
  {
    const union fuses* default_fuses =
      is_gx72 ? &default_fuses_gx72 : &default_fuses_gx36;
    memcpy(fuses, default_fuses, sizeof (*fuses));
    rv = 1;
  }

  return rv;
}


/** Get the min/max voltage values from the BIB.  Then clip them to the
 *  min/max values that can be represented by our 6-bit VID code subset.
 * @param min_v Pointer to returned minimum voltage in uV.
 * @param max_v Pointer to returned maximum voltage in uV.
 * @return 1 if the values came from the BIB (even if they were clipped), 0
 *  if we used the defaults.
 */
int
get_voltage_range(unsigned int* min_v, unsigned int* max_v)
{
  int rv = 0;

  // Minimum representable voltage (uV)
  const unsigned int ds_min_v =  818750;
  // Maximum representable voltage (uV)
  const unsigned int ds_max_v = 1212500;

  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_CPU_VOLT_RANGE, 0, &bp, NULL) != BI_NULL)
  {
    struct bi_cpu_volt_range* cvr = bp;
    //
    // We force the minimum value to be within the representable limits,
    // then we make sure the maximum is at least that much and also within
    // the limits.
    //
    *min_v = min(max(cvr->vmin, ds_min_v), ds_max_v);
    *max_v = max(min(cvr->vmax, ds_max_v), *min_v);
    rv = 1;
  }
  else
  {
    *min_v = ds_min_v;
    *max_v = ds_max_v;
  }

  return rv;
}


//
// This is the start of the hypervisor-only code.
//
#ifndef L1BOOT

/** Set up our interrupts. */
void
set_intrs()
{
  // Enable the interrupst we want to handle.
  __insn_mtspr(INTERRUPT_MASK_RESET_HV, HV_INTS_HANDLED);

  // Disable the interrupts we want to handle at lower PLs; these will
  // be enabled when we jump to the client.
  __insn_mtspr(INTERRUPT_MASK_SET_HV, HV_INTS_HANDLED_SV);

  // Disable the interrupts we never want to handle.
  __insn_mtspr(INTERRUPT_MASK_SET_HV, HV_INTS_UNHANDLED);
}


/** Enable various machine error interrupts. */
void
set_error_enable()
{
  //
  // Clear memory errors that may have occured during boot due to
  // predictions that were made during VA-is-PA that were later
  // not valid.
  //
  __insn_mtspr(SPR_MEM_ERROR_CBOX_STATUS,
               SPR_MEM_ERROR_CBOX_STATUS__VALID_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_CORRECTED_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_FATAL_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_TAG_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_STATE_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_RDN_WRITE_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_RDN_READ_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__L2_MAF_TIMEOUT_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__SHARE_INVALIDATION_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_DATA_CORRECTED_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_DATA_FATAL_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_TAG_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_STATE_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_RDN_WRITE_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_RDN_READ_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_L2_MAF_TIMEOUT_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INT_SHARE_INVALIDATION_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__OVERFLOW_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INFO_WAY_MASK |
               SPR_MEM_ERROR_CBOX_STATUS__INFO_MASK);
  __insn_mtspr(SPR_MEM_ERROR_CBOX_ADDR, 0);

  //
  // Also clear memory errors that may have occured in the L1D$ during boot.
  //
  __insn_mtspr(SPR_MEM_ERROR_MBOX_STATUS,
               SPR_MEM_ERROR_MBOX_STATUS__VALID_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__L1_D_DATA_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__L1_D_TAG_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__DTLB_MULTI_MATCH_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_DTLB_ENTRY_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_ATOMIC_ATTRIBUTE_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_OPCODE_ATTRIBUTE_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_L1_D_DATA_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_L1_D_TAG_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_DTLB_MULTI_MATCH_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_ILLEGAL_DTLB_ENTRY_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_ILLEGAL_ATOMIC_ATTRIBUTE_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__INT_ILLEGAL_OPCODE_ATTRIBUTE_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__OVERFLOW_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__WAY0_ERROR_MASK |
               SPR_MEM_ERROR_MBOX_STATUS__WAY1_ERROR_MASK);
  __insn_mtspr(SPR_MEM_ERROR_MBOX_ADDR, 0);

  //
  // The MAF timeout can actually fire in normal operation, so we don't
  // generally enable it.
  //
  SPR_CBOX_MSR_t scm = { .word =  __insn_mfspr(SPR_CBOX_MSR) };
#ifdef MAF_TIMEOUT_DEBUG
  // Set timeout to 2 Mcycles
  scm.maf_timeout = 3;
#else
  // Disable entirely
  scm.maf_timeout_disable = 1;
#endif
  //
  // Set the MMIO timeout to 1 G cycles (894 ms at 1.2 GHz).  We want this
  // to be larger than TRIO's PIO completion timeout, so we don't get a
  // MEM_ERROR if a PCIe device goes out to lunch.
  //
  scm.mmio_timeout = 2;
  __insn_mtspr(SPR_CBOX_MSR, scm.word);

  //
  // Enable most of the MEM_ERROR conditions, but don't trap on various
  // pointless but ignorable operations on noncacheable or MMIO space.  We
  // do trap on atomics to those spaces, since they'll produce unexpected
  // results.
  //
  // If we are booting up starting with small page sizes, we disable
  // the illegal ITLB and DTLB checking.  See the HV_CTX_PG_* flags
  // and validate_tte() in tsb.c.
  //
  // Note that we need to do this after we've cleared the pending error
  // bits above.
  //
  unsigned long disabled_mem_error =
    SPR_MEM_ERROR_ENABLE__ILLEGAL_OPCODE_ATTRIBUTE_MASK;
#if HV_DEFAULT_PAGE_SIZE_SMALL < 65536
  disabled_mem_error |= SPR_MEM_ERROR_ENABLE__ILLEGAL_ITLB_ENTRY_MASK |
    SPR_MEM_ERROR_ENABLE__ILLEGAL_DTLB_ENTRY_MASK;
#endif

  __insn_mtspr(SPR_MEM_ERROR_ENABLE, ~disabled_mem_error);

  //
  // Configure our response to diag broadcasts.  We set things up so
  // that diag network 0 quiesces the SBox, and network 1 quiesces the
  // CBox.
  //
  SPR_QUIESCE_CTL_t qc =
  {{
    .bcst_in_ii_qr_enable = 1,
    .bcst_out_ii_qr_enable = 1,
    .clr_ii_qr = 1,
    .bcst_in_ct_qr_enable = 2,
    .bcst_out_ct_qr_enable = 2,
    .clr_ct_qr = 1,
  }};
  __insn_mtspr(SPR_QUIESCE_CTL, qc.word);

#ifdef WATCHDOG_MEM_ERROR
  //
  // Halt tiles, and quiesce the caches, if we enter the MEM_ERROR
  // interrupt vector.  This may be useful when debugging certain types of
  // problems (e.g., bad cache flushing or TLB flushing leading to
  // coherency mismatches).  It's not reasonable for normal operation,
  // since user code can cause MEM_ERROR via bad MMIO requests, and we
  // can also get correctable cache errors, ECC errors, etc.
  //

  //
  // Configure the diag mux to give us SBox mode 2 data.
  //
  SPR_DIAG_MUX_CTL_t dm =
  {{
    .top_sel = SPR_DIAG_MUX_CTL__TOP_SEL_VAL_M_C_S_N,
    .s_sel = 2,
  }};
  __insn_mtspr(SPR_DIAG_MUX_CTL, dm.word);

  //
  // Configure the watch registers to trigger when the decode PC portion of
  // the mode 2 data matches the VA for the MEM_ERROR interrupt handler.
  //
  extern char intvec_MEM_ERROR;
  __insn_mtspr(SPR_WATCH_VAL, ((uintptr_t) &intvec_MEM_ERROR >> 3) <<
                              DIAG_SBOX_MODE2_PC_DC_SHIFT);
  __insn_mtspr(SPR_WATCH_MASK, ~DIAG_SBOX_MODE2_PC_DC_MASK);

  //
  // Configure perf counter zero to count on the watch event.
  //
  SPR_PERF_COUNT_CTL_t pcc =
  {{
    .count_0_box = SPR_AUX_PERF_COUNT_CTL__COUNT_0_BOX_VAL_SPCL,
    .count_0_sel = DIAG_SPCL_EVENT_WATCH,
  }};
  __insn_mtspr(SPR_PERF_COUNT_CTL, pcc.word);

  //
  // Configure quiesce control to quiesce this tile upon local quiesce, and
  // to trigger SBox/CBox quiesce on perf event 0.  We already configured
  // quiesce to produce outgoing broadcast signals above.
  //
  qc.word = __insn_mfspr(SPR_QUIESCE_CTL);
  qc.local_ii_qr_enable = 1;
  qc.local_ct_qr_enable = 1;
  qc.perf_event_ct_qr_enable_0 = 1;
  qc.perf_event_ii_qr_enable_0 = 1;
  __insn_mtspr(SPR_QUIESCE_CTL, qc.word);
#endif
}


/** Set various miscellaneous tile registers. */
void
setup_misc()
{
  //
  // Start off the pseudo-random number generator with a nonzero (and
  // slightly unpredictable) value.
  //
  __insn_mtspr(SPR_PSEUDO_RANDOM_NUMBER_MODIFY, __insn_mfspr(SPR_CYCLE));

  //
  // Set various SBox configuration bits.
  //
  SPR_SBOX_CONFIG_t sc = { .word =  __insn_mfspr(SPR_SBOX_CONFIG) };

  //
  // Configure the pseudo-random number generator in changes-every-cycle
  // mode.
  //
  sc.prn_mode = 1;

  //
  // Enable the Dstream prefetcher.
  //
  sc.dpf_enable = 1;

  //
  // Set the Istream prefetch to prefetch 2 lines ahead.
  //
  sc.pf_limit = 2;

  //
  // Enable I$ parity checking.
  //
  sc.l1i_tag_parity_enable = 1;
  sc.l1i_data_parity_enable = 1;

  //
  // Commit changes to the SBox config.
  //
  __insn_mtspr(SPR_SBOX_CONFIG, sc.word);

  //
  // Configure the Dstream prefetcher to prefetch with a stride of 1.
  //
  SPR_DSTREAM_PF_t dp = { .word = __insn_mfspr(SPR_DSTREAM_PF) };
  dp.stride = 1;
  __insn_mtspr(SPR_DSTREAM_PF, dp.word);

  //
  // Umask performance counting for all PLs in SPR_PERF_COUNT_PLS.
  // Note register SPR_PERF_COUNT_PLS has not been implemented in simulator.
  //
  if (!sim_is_simulator())
    __insn_mtspr(SPR_PERF_COUNT_PLS, 0x0);
}


/** Set up the networks, primarily their protection. */
void
setup_networks()
{

  Lotar client_lotar;

  (void) r2c_lotar(my_lotar, &client_lotar);
  SPR_UDN_DIRECTION_PROTECT_t firewall = { .word = 0 };

  if (on_north_edge(client_lotar))
    firewall.n_protect = 1;

  if (on_east_edge(client_lotar))
    firewall.e_protect = 1;

  if (on_south_edge(client_lotar))
    firewall.s_protect = 1;

  if (on_west_edge(client_lotar))
    firewall.w_protect = 1;

  __insn_mtspr(SPR_UDN_DIRECTION_PROTECT, firewall.word);
  //
  // FIXME: GX: when the hypervisor stops using the IDN, this needs to
  // set up the IDN protections like the UDN.
  //
  __insn_mtspr(SPR_IDN_DIRECTION_PROTECT, 0);

}


/** Lock protecting the following shared static data, all used by
 *  set_speed(). */
static spinlock_t set_speed_lock _SHARED;

/** Nonzero if the following data has been initialized. */
static int fuses_read _SHARED;

/** Fuse block, containing the frequency->voltage information. */
static union fuses fuses _SHARED;

/** Minimum voltage supported by the board. */
static unsigned int min_v _SHARED;

/** Maximum voltage supported by the board. */
static unsigned int max_v _SHARED;

/** Number of clocks. */
static int n_clocks _SHARED;

/** The clock table. */
static struct clock
{
  /** Device table pointer. */
  struct device* devp;
  /** Fuse table pointer. */
  struct f2v_entry* fuses;
  /** Current frequency. */
  long cur_freq;
  /** Desired frequency (what the user asked for). */
  long des_freq;
  /** Target frequency (what we're actually going to set it to). */
  long targ_freq;
  /** Clock index for this clock. */
  int clk_index;
}
*clocks _SHARED;

/** The core PLL device. */
static struct clock* corepll_clkp _SHARED;

/** Maximum core PLL frequency, given our maximum voltage. */
static unsigned long max_core_freq _SHARED;


/** Set the processor speed.
 * @param freq New frequency, in hertz.
 * @param flags
 * @return Achieved new frequency, in hertz.
 */
long
set_speed(unsigned long freq, unsigned long flags)
{
  //
  // We aren't currently handling the HV_SET_SPEED_ROUNDUP flag in all
  // cases.  Doing so will require a change to the driver interface, so
  // we're holding off on that until we do the shim speed-setting work.  In
  // the short term, it doesn't matter, because the Linux cpufreq framework
  // always calls us with the HV_SET_SPEED_DRYRUN flag first to convert the
  // requested frequency to the actual frequency, and in that case we do
  // implement the ROUNDUP flag.
  //

  //
  // The simulator doesn't implement real speed-setting support, or the
  // fuses, so we just do nothing and return the current speed.
  //
  if (sim_is_simulator())
    return sim_query_cpu_speed();

  //
  // The PLL registers on the FPGA aren't implemented, so we hang when
  // we wait for them to spin up; do nothing and return a plausible speed.
  //
  if (board_flags & BOARD_FPGA)
    return 20 * 1000 * 1000;

  //
  // Take the lock; this protects the shared data, and also prevents
  // multiple tiles from trying to change the speed at the same time.
  //
  spin_lock(&set_speed_lock);

  //
  // See if we need to initialize the fuse data and clock table.
  //
  if (!fuses_read)
  {
    fuses_read = 1;

    //
    // Get the fuses.
    //
    if (get_fuses(rshims[0]->idn_ports[0], &fuses))
      SPEED_TRACE("fuses invalid, using default values\n");

    //
    // Get the min/max voltage values.
    //
    if (config.dvs)
    {
      if (get_voltage_range(&min_v, &max_v))
        SPEED_TRACE("voltage min %d uv, max %d uv from bib\n", min_v, max_v);
      else
        SPEED_TRACE("voltage min %d uv, max %d uv from VRM limits\n", min_v,
                    max_v);
    }
    else
    {
      //
      // If we aren't changing the voltage, just lock the min/max to our
      // current setting.
      //
      min_v = max_v = VID_TO_UV(cfg_rd(rshims[0]->idn_ports[0].word, 0,
                                       RSH_VID_CORE));
    }

    //
    // Note: we must make both calls, so we need | below, not ||.
    //
    if (loadline_vid_to_chip(&min_v) | loadline_vid_to_chip(&max_v))
      SPEED_TRACE("doing load line adjustment, min/max now %d/%d uv\n",
                  min_v, max_v);

    //
    // Count the number of clocks we have on enabled devices, so that we
    // know how big to make our clock table.
    //
    for (struct device* devp = devices; devp->name; devp++)
      if (devp->drv)
        n_clocks += devp->n_clocks;

    //
    // Now allocate our clock table, fill in the static values, and adjust
    // the min voltage.
    //
    clocks = shared_alloc(n_clocks * sizeof (*clocks), 0);

    struct clock* clkp = clocks;
    for (struct device* devp = devices; devp->name; devp++)
    {
      if (devp->drv)
      {
        for (int i = 0; i < devp->n_clocks; i++)
        {

          clkp->devp = devp;
          clkp->clk_index = i;
          clkp->fuses = &fuses.data.f2v[devp->f2v_index[i]];
          clkp->cur_freq = clkp->devp->drv->ops->get_cur_freq(&clkp->devp->info,
                                                              clkp->clk_index);
          //
          // Right now, we aren't changing any shim frequencies, so we
          // force the desired frequency to be the current frequency.
          //
          clkp->des_freq = clkp->cur_freq;

          if (devp->shim_type == DEV_PSEUDO_COREPLL)
          {
            //
            // For the core PLL, we remember where it is for later, and
            // also compute its maximum frequency based on our maximum
            // voltage.
            //
            corepll_clkp = clkp;
            max_core_freq = volt_to_freq(max_v, clkp->fuses,
                                         FUSE_NUM_F2V_ENTRIES);
          }
          else
          {
            //
            // For other shims, we figure out how much voltage they need,
            // and raise the minimum if needed to support that.
            //

            if (clkp->des_freq >= 0)
            {
              unsigned int desired_v = freq_to_volt(clkp->des_freq, clkp->fuses,
                                                    FUSE_NUM_F2V_ENTRIES);
              min_v = max(min_v, desired_v);

              SPEED_TRACE("%s clk %d: current %ld Hz, desired voltage "
                          "%d, new min voltage %d uv\n", clkp->devp->name,
                          clkp->clk_index, clkp->cur_freq, desired_v,
                          min_v);
            }
          }

          clkp++;
        }
      }
    }
  }

  //
  // Clip the input frequency to its maximum.
  //
  freq = min(freq, max_core_freq);

  //
  // If they just want to see what the closest valid frequency to this
  // one is, tell them.
  //
  if (flags & HV_SET_SPEED_DRYRUN)
  {
    unsigned int dummy;

    freq = freq_to_pll(freq, &dummy, &dummy, &dummy, &dummy, REFCLK,
                       flags & HV_SET_SPEED_ROUNDUP);

    spin_unlock(&set_speed_lock);

    return freq;
  }

  //
  // Now determine our target voltage.
  //
  unsigned int target_v = freq_to_volt(freq, corepll_clkp->fuses,
                                       FUSE_NUM_F2V_ENTRIES);

  target_v = max(target_v, min_v);

  SPEED_TRACE("new target voltage %d uv\n", target_v);

  //
  // Note that the loadline calculation adjusts the voltage assuming that
  // the processor is fully loaded, which is probably not the case when
  // using DVS.  However, we really can't tell, and if we're wrong the
  // results are rather catastrophic, so we have no choice but to jack it
  // up.  This is a good reason _not_ to use a power supply which doesn't
  // supply constant voltage if you care about power usage.
  //
  if (loadline_chip_to_vid(&target_v))
    SPEED_TRACE("doing load line adjustment, target voltage now %d\n",
                target_v);

  //
  // If we're lowering the frequency, we do that and then set the voltage;
  // if we're raising it, we raise the voltage first.
  //
  if (freq < cpu_speed)
  {
    SPEED_TRACE("%s clk %d: lowering to %ld Hz\n", corepll_clkp->devp->name,
                corepll_clkp->clk_index, freq);
    corepll_clkp->devp->drv->ops->set_freq(&corepll_clkp->devp->info,
                                           corepll_clkp->clk_index, freq);

    //
    // Note that we must set cpu_speed here before we call set_vid, since
    // set_vid uses delays which depend upon the CPU speed.
    //
    cpu_speed =
      corepll_clkp->devp->drv->ops->get_cur_freq(&corepll_clkp->devp->info, 0);

    if (config.dvs)
    {
      SPEED_TRACE("setting voltage to %d, VID 0x%x\n", target_v,
                  UV_TO_VID(target_v));
      set_vid(rshims[0]->idn_ports[0], UV_TO_VID(target_v));
    }
  }
  else
  {
    if (config.dvs)
    {
      SPEED_TRACE("setting voltage to %d, VID 0x%x\n", target_v,
                  UV_TO_VID(target_v));
      set_vid(rshims[0]->idn_ports[0], UV_TO_VID(target_v));
    }

    SPEED_TRACE("%s clk %d: raising to %ld Hz\n", corepll_clkp->devp->name,
                corepll_clkp->clk_index, freq);
    corepll_clkp->devp->drv->ops->set_freq(&corepll_clkp->devp->info,
                                           corepll_clkp->clk_index, freq);

    cpu_speed =
      corepll_clkp->devp->drv->ops->get_cur_freq(&corepll_clkp->devp->info, 0);
  }

  spin_unlock(&set_speed_lock);

  //
  // Return the current CPU speed.
  //
  return cpu_speed;
}


/** Do per-board configuration. */
void
setup_board()
{
#ifdef DEBUG
  if (debug_flags & DEBUG_DUMP_BI)
    bi_dump();
#endif

  //
  // Get the CPU speed.  We need to first do a dummy call to set_speed() to
  // make sure corepll_clkp is valid.
  //
  if (sim_is_simulator())
    cpu_speed = sim_query_cpu_speed();
  else if (board_flags & BOARD_FPGA)
    cpu_speed = set_speed(0, HV_SET_SPEED_ROUNDUP);
  else
  {
    set_speed(0, HV_SET_SPEED_DRYRUN | HV_SET_SPEED_ROUNDUP);
    cpu_speed =
      corepll_clkp->devp->drv->ops->get_cur_freq(&corepll_clkp->devp->info, 0);
  }

  //
  // Initialize the temperature sensor.
  //
  init_temp_sens();

  //
  // Reset any devices described by miscellaneous reset entries in the BIB.
  //
  bi_ptr_t bp;
  int offset = 0; 
  uint32_t desc;

  while ((desc = bi_getparam(BI_TYPE_MISC_RESET, -1, &bp, &offset)) != BI_NULL)
  {
    struct bi_misc_reset* mr = bp;

    for (int i = 0; i < BI_WDS(desc) - 1; i++)
      drv_set_signal(mr->resets[i], DRV_SIGNAL_INIT | DRV_SIGNAL_DEASSERT);

    drv_udelay(mr->assert_time);

    for (int i = 0; i < BI_WDS(desc) - 1; i++)
      drv_set_signal(mr->resets[i], DRV_SIGNAL_ASSERT);

    drv_udelay(mr->assert_time);

    for (int i = 0; i < BI_WDS(desc) - 1; i++)
      drv_set_signal(mr->resets[i], DRV_SIGNAL_DEASSERT);
  }

  //
  // If this is a prototype Hancock board, work around the fact that the
  // power-on settings for the clock generator are incorrect.
  //
  do
  {
    offset = 0;
    if ((desc = bi_getparam(BI_TYPE_BOARD_PART_NUM, 0, &bp, &offset)) ==
        BI_NULL)
      break;

    char* board_part = bp;
    if (strncmp(board_part, "402-00087-01", BI_BYTES(desc)))
      break;

    offset = 0;
    if ((desc = bi_getparam(BI_TYPE_BOARD_REV, 0, &bp, &offset)) == BI_NULL)
      break;

    char* board_rev = bp;
    if (strncmp(board_rev, "087-1-C-0-ES", BI_BYTES(desc)))
      break;

    tprintf("Reconfiguring clock generator\n");

    const int bus = 2;
    const int dev = 0xD0 | I2C_DEV_16BIT;

    uint8_t data = 0x35;
    int len = i2c_wr_bus(bus, dev, 0x05, 1, &data);

    if (len == 1)
    {
      data = 0xF1;
      len = i2c_wr_bus(bus, dev, 0x0A, 1, &data);
    }

    if (len == 1)
    {
      data = 0x08;
      len = i2c_wr_bus(bus, dev, 0xA0, 1, &data);
    }

    if (len != 1)
      tprintf("hv_warning: could not reconfigure clock generator\n");
  }
  while (0);
}


/**
 *  Reset the chip.
 * @param flags Soft reset flags for the SROM booter, or 0 if none.
 */
void
reset_chip(uint32_t flags)
{
  if (!rshims[0])
    panic("attempted to reset a chip with no rshim.");

  //
  // We reset the SROM to make sure any bank select registers are zero.
  //
  int srom_dev = srom_get_dev(rshims[0]->idn_ports[0], SROM_CHAN);
  srom_reset(rshims[0]->idn_ports[0], SROM_CHAN, srom_dev);

  do_soft_reset(rshims[0]->idn_ports[0], flags);
}


/** Turn off the FAIL LED, if we have one. */
void
douse_fail_led()
{
  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_FAIL_LED, 0, &bp, NULL) == BI_NULL)
    return;

  struct bi_fail_led* fl = bp;
  drv_set_signal(fl->signal, DRV_SIGNAL_INIT | DRV_SIGNAL_DEASSERT);
}

#endif // !L1BOOT


#ifdef L1BOOT

/** Set core clock duty cycle characteristics.  Must be called before we
 *  take the core PLL out of bypass mode.
 * @param steps Number of steps (around 5 ps each) to move the falling edge
 *  of the clock.  If positive, move the edge later; if negative, move it
 *  earlier; if zero, restore the default behavior.  Note that any motion
 *  is relative to the default, not the current setting, so calling this
 *  routine more than once gets you whatever you asked for last, not the
 *  sum of the adjustments.
 */
void
set_cclk_duty(int steps)
{
  //
  // Power-on value for this particular register is all 0's, and we don't
  // support tweaking anything other than what we're about to set, so we
  // don't bother to read it out or to supply default values for other fields.
  //
  unsigned long cclk_pll_state[(JTAG_DRS_CCLK_PLL_TOTAL_WIDTH + 63) / 64];
  memset(cclk_pll_state, 0, sizeof (cclk_pll_state));

  if (steps)
  {
    int val = min(11, abs(steps) - 1);
    int lo = (steps < 0);

    bit_insert(cclk_pll_state, JTAG_DRS_CCLK_PLL__DUTY_CTL_VAL_FIELD, val);
    bit_insert(cclk_pll_state, JTAG_DRS_CCLK_PLL__DUTY_CTL_LO_FIELD, lo);
    bit_insert(cclk_pll_state, JTAG_DRS_CCLK_PLL__DUTY_CTL_ENA_FIELD, 1);
  }

  rshim_jtag_send(__insn_mfspr(SPR_RSHIM_COORD), 0, cclk_pll_state,
                  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_CRC_CCLK,
                  JTAG_DRS_CCLK_PLL_TOTAL_WIDTH);
}

#endif
