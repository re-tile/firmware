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
 * Core PLL driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/rsh.h>

#include "sys/libc/include/util.h"

#include "board_info.h"
#include "cfg.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hw_config.h"
#include "types.h"


/** Get the current setting for the core PLL. */
static long
corepll_get_cur_freq(const struct dev_info* info, int clock_index)
{
  RSH_CLOCK_CONTROL_t rcc = 
  {
    .word = cfg_rd(rshims[0]->idn_ports[0].word, 0, RSH_CLOCK_CONTROL)
  };

  return pll_to_freq(!rcc.ena, rcc.pll_m, rcc.pll_n, rcc.pll_q, REFCLK);
}


/** Get the desired setting for the core PLL. */
static long
corepll_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // If it's set in the .hvc, use that value.
  //
  if (config.cpu_speed)
    return config.cpu_speed;

  //
  // See if there's a board default in the BIB, and if so, use it.
  //
  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_NOM_TILE_FREQ, 0, &bp, NULL) != BI_NULL)
  {
    struct bi_nom_tile_freq* ntf = bp;
    return ntf->clock;
  }

  //
  // If we haven't found a value, we want to run as fast as we can subject
  // to the limits imposed by the chip and the available voltage.
  //
  return DRV_DESIRED_FREQ_MAX_RAISEV;
}


/** Set the core PLL frequency. */
static int
corepll_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  RSH_CLOCK_CONTROL_t rcc = 
  {{
    .ena = 1,
    .pll_m = m,
    .pll_n = n,
    .pll_q = q,
    .pll_range = range,
  }};

  cfg_wr(rshims[0]->idn_ports[0].word, 0, RSH_CLOCK_CONTROL, rcc.word);
  __insn_mf();

  do
  {
    rcc.word = cfg_rd(rshims[0]->idn_ports[0].word, 0, RSH_CLOCK_CONTROL);
  }
  while (!rcc.clock_ready);

  return 0;
}


/** Core PLL driver operations vector. */
static struct drv_ops corepll_ops = {
  .get_cur_freq     = corepll_get_cur_freq,
  .get_desired_freq = corepll_get_desired_freq,
  .set_freq         = corepll_set_freq,
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_corepll = {
  .shim_type  = DEV_PSEUDO_COREPLL,
  .name       = "corepll",
  .desc       = "Tile Processor core PLL",
  .ops        = &corepll_ops,
  .flags      = DRV_FLG_AUTOMATIC,
};
