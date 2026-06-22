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
 * mPIPE driver link management routines.
 */

#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"

#include "mpipe.h"

#include "mpipe_rpc_dispatch.h"
#include "mpipe_info_rpc_dispatch.h"

#include <arch/ipi.h>
#include <arch/sim.h>

/** Fake link name table for the simulator. */
static const struct link_name_table sim_link_table[48] =
{
  //
  // Note that we leave the MAC address uninitialized, and thus zero.
  //
  { "gbe0",   0,  0,  0 },
  { "gbe1",   0,  1,  1 },
  { "gbe2",   0,  2,  2 },
  { "gbe3",   0,  3,  3 },
  { "gbe4",   0,  4,  4 },
  { "gbe5",   0,  5,  5 },
  { "gbe6",   0,  6,  6 },
  { "gbe7",   0,  7,  7 },
  { "gbe8",   0,  8,  8 },
  { "gbe9",   0,  9,  9 },
  { "gbe10",  0, 10, 10 },
  { "gbe11",  0, 11, 11 },
  { "gbe12",  0, 12, 12 },
  { "gbe13",  0, 13, 13 },
  { "gbe14",  0, 14, 14 },
  { "gbe15",  0, 15, 15 },
  { "xgbe0",  0, 16,  0 },
  { "xgbe1",  0, 17,  4 },
  { "xgbe2",  0, 18,  8 },
  { "xgbe3",  0, 19, 12 },
  { "loop0",  0, 20, 16 },
  { "loop1",  0, 21, 17 },
  { "loop2",  0, 22, 18 },
  { "loop3",  0, 23, 19 },
  { "gbe16",  1, 24,  0 },
  { "gbe17",  1, 25,  1 },
  { "gbe18",  1, 26,  2 },
  { "gbe19",  1, 27,  3 },
  { "gbe20",  1, 28,  4 },
  { "gbe21",  1, 29,  5 },
  { "gbe22",  1, 30,  6 },
  { "gbe23",  1, 31,  7 },
  { "gbe24",  1, 32,  8 },
  { "gbe25",  1, 33,  9 },
  { "gbe26",  1, 34, 10 },
  { "gbe27",  1, 35, 11 },
  { "gbe28",  1, 36, 12 },
  { "gbe29",  1, 37, 13 },
  { "gbe30",  1, 38, 14 },
  { "gbe31",  1, 39, 15 },
  { "xgbe4",  1, 40,  0 },
  { "xgbe5",  1, 41,  4 },
  { "xgbe6",  1, 42,  8 },
  { "xgbe7",  1, 43, 12 },
  { "loop4",  1, 44, 16 },
  { "loop5",  1, 45, 17 },
  { "loop6",  1, 46, 18 },
  { "loop7",  1, 47, 19 },
};

/** Real link name table.  The size here allows for 20 MACs, plus 4 extra
 *  for loopback, on each of 2 mPIPEs; this may need to be larger for
 *  Gx100. */
static struct link_name_table link_table[48] _SHARED;

/** Number of current valid entries in the real link name table. */
static int link_table_len _SHARED;

/** Bitmap of currently used gbe link numbers. */
static unsigned long gbe_link_numbers_map _SHARED;

/** Bitmap of currently used xgbe link numbers. */
static unsigned long xgbe_link_numbers_map _SHARED;

/** Number of currently used loopback link numbers. */
static int next_loop_link_number _SHARED;


/** Determine whether a link is gbe, xgbe, both, or neither.
 * @param ms Driver state.
 * @param p Pointer to the BIB item describing the link.
 * @param gbe_mac_num Value this points to will be set to the MAC
 *  number if the link is a 10/100/1000 Mbps link, -1 otherwise.
 * @param xgbe_mac_num Value this points to will be set to the MAC
 *  number if the link is a 10/20 Gbps link, -1 otherwise.
 * @param serdes_mac_num Value this points to will be set to the MAC
 *  number controlling this link's SERDES.  If *gbe_mac_num and
 *  *xgbe_mac_num are both -1 on return, this value is undefined.
 * @param serdes_lanes Value this points to will be set to a bitmap of
 *  lanes associated with this link, within the quad controlled by
 *  *serdes_mac_num.  If *gbe_mac_num and *xgbe_mac_num are both -1 on
 *  return, this value is undefined.
 */
static void
get_link_types(mpipe_state_t* ms, struct bi_phy_link_cfg* p,
               int* gbe_mac_num, int* xgbe_mac_num,
               int* serdes_mac_num, uint8_t* serdes_lanes)
{
  int xgbe_mac = -1;
  int gbe_mac = -1;

  if (p->lanes >= BI_PHY_LINK_CFG_LANES__VAL_LANE0_3 &&
      p->lanes <= BI_PHY_LINK_CFG_LANES__VAL_LANE12_15)
  {
    //
    // This is an xgbe port.
    //
    int quad_num = p->lanes - BI_PHY_LINK_CFG_LANES__VAL_LANE0_3;
    xgbe_mac = 4 + 5 * quad_num;

    //
    // It could also be a gbe port if it's a combo PHY.
    //
    if (p->speed_10m || p->speed_100m || p->speed_1g)
      gbe_mac = 5 * quad_num;

    *serdes_mac_num = xgbe_mac;
    *serdes_lanes = 0xF;
  }
  else if (p->lanes >= BI_PHY_LINK_CFG_LANES__VAL_LANE0 &&
           p->lanes <= BI_PHY_LINK_CFG_LANES__VAL_LANE15)
  {
    //
    // This is a gbe port.
    //
    int lane_num = p->lanes - BI_PHY_LINK_CFG_LANES__VAL_LANE0;
    gbe_mac = (lane_num / 4) * 5 + lane_num % 4;

    *serdes_mac_num = 4 + 5 * (lane_num / 4);
    *serdes_lanes = 0x1 << (lane_num % 4);
  }
  else
  {
    //
    // FIXME: eventually need to handle Interlaken here.
    //
  }

  //
  // If a MAC can't be enabled, don't return it.
  //
  static uint_reg_t avail_macs_map = 0;

  if (!avail_macs_map)
    avail_macs_map = cfg_rd(ms->shim_pos.word, 0, MPIPE_MAC_ENABLE) >>
                 MPIPE_MAC_ENABLE__AVAIL_SHIFT;

  if (!(avail_macs_map & (1L << xgbe_mac)))
    xgbe_mac = -1;
  if (!(avail_macs_map & (1L << gbe_mac)))
    gbe_mac = -1;

  *xgbe_mac_num = xgbe_mac;
  *gbe_mac_num = gbe_mac;
}


/** Add an entry to the link name table and the macs table for a link.
 * @param ms Driver state.
 * @param p Pointer to the BIB item describing the link.
 * @param mac_num Hardware MAC number.
 * @param is_gbe Nonzero if a GbE entry should be added, otherwise XGbE.
 * @param mdio_mac_num MAC number to be used for MDIO.
 * @param serdes_mac_num MAC number to be used for SERDES access.
 * @param serdes_lanes Bitmap of SERDES lanes within the quad controlled by
 *  serdes_mac_num.
 * @param xaui_refclk_125 If nonzero, the XAUI shim has a 125 MHz reference
 *   clock.
 * @return Pointer to the newly added mac table entry.
 */
static struct mac_state*
add_one_link(mpipe_state_t* ms, struct bi_phy_link_cfg* p, int mac_num,
             int is_gbe, int mdio_mac_num, int serdes_mac_num,
             uint8_t serdes_lanes, int xaui_refclk_125)
{
  int mac_tbl_idx = ms->n_macs++;
  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  mp->link_config.hv_private = ms;

  mp->link_config.gbe = is_gbe;

  memcpy(mp->link_config.mac_addr, p->mac_addr, 6);

  mp->link_config.shim_port = ms->shim_pos.word;
  mp->link_config.shim_pa = 0;

  mp->link_config.mac_num = mac_num;
  mp->link_config.mac_pa =
    ((PA) RESERVED_SVC_DOM << MPIPE_CFG_REGION_ADDR__SVC_DOM_SHIFT) |
    ((PA) MPIPE_CFG_REGION_ADDR__INTFC_MASK) |
    ((PA) mac_num << MPIPE_CFG_REGION_ADDR__MAC_SEL_SHIFT);

  mp->link_config.mdio_mac_num = mdio_mac_num;
  mp->link_config.mdio_mac_pa =
    ((PA) RESERVED_SVC_DOM << MPIPE_CFG_REGION_ADDR__SVC_DOM_SHIFT) |
    ((PA) MPIPE_CFG_REGION_ADDR__INTFC_MASK) |
    ((PA) mdio_mac_num << MPIPE_CFG_REGION_ADDR__MAC_SEL_SHIFT);

  mp->link_config.serdes_mac_pa =
    ((PA) RESERVED_SVC_DOM << MPIPE_CFG_REGION_ADDR__SVC_DOM_SHIFT) |
    ((PA) MPIPE_CFG_REGION_ADDR__INTFC_MASK) |
    ((PA) serdes_mac_num << MPIPE_CFG_REGION_ADDR__MAC_SEL_SHIFT);
  mp->link_config.serdes_lanes = serdes_lanes;

  int quad_offset = 4 * (serdes_mac_num / 5);

  if (is_gbe)
  {
    mp->link_config.serdes_rx_lane_length[0] =
      ms->serdes_rx_lane_length[quad_offset + mac_num % 5];
    mp->link_config.serdes_tx_lane_length[0] =
      ms->serdes_tx_lane_length[quad_offset + mac_num % 5];
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      mp->link_config.serdes_rx_lane_length[i] =
        ms->serdes_rx_lane_length[quad_offset + i];
      mp->link_config.serdes_tx_lane_length[i] =
        ms->serdes_tx_lane_length[quad_offset + i];
    }
  }

  mp->link_config.link_intr_sig = p->intr_sig;

  mp->link_config.possible_state =
    ((p->speed_10m) ? ENET_LINK_10M : 0) |
    ((p->speed_100m) ? ENET_LINK_100M : 0) |
    ((p->speed_1g) ? ENET_LINK_1G : 0) |
    ((p->speed_10g && !xaui_refclk_125) ? ENET_LINK_10G : 0) |
    ((p->speed_10g && xaui_refclk_125) ? ENET_LINK_12G : 0) |
    ((p->speed_20g && !xaui_refclk_125) ? ENET_LINK_20G : 0) |
    ((p->speed_25g) ? ENET_LINK_25G : 0) |
    ((p->speed_50g) ? ENET_LINK_50G : 0);

  mp->link_config.xaui_refclk_125 = xaui_refclk_125;

  //
  // Compute the link name.
  //
  unsigned long* ln_bmap = (is_gbe) ? &gbe_link_numbers_map :
                                      &xgbe_link_numbers_map;
  int l_num = p->link_name_num;
  if (l_num == BI_PHY_LINK_CFG_LINK_NAME_NUM__VAL_DEFAULT)
  {
    l_num = __builtin_ctzl(~*ln_bmap);
    *ln_bmap  |= (1L << l_num);
  }

  //
  // Get the channel numbers.
  //
  uint_reg_t chans =
    cfg_rd(ms->shim_pos.word, 0,
           MPIPE_MAC0_MAP + sizeof (uint_reg_t) * mac_num);
  mp->channels = chans;
  mp->link_config.chan = __builtin_ctzl(mp->channels);

  //
  // Get PHY related information, or SFP information if there is no PHY.
  //
  union port_inst {
    bi_inst_t instance;
    struct bi_port_inst port;
  } port_inst = {
    .port.port = mp->link_config.chan,
    .port.shim = ms->virt_instance,
  };

  bi_ptr_t bp;
  struct bi_sfp_cfg* bsc = NULL;

  mp->led_sig.type = BI_SIGNAL_TYPE__VAL_NONE;

  if (bi_getparam(BI_TYPE_SFP_CFG, port_inst.instance, &bp, NULL) != BI_NULL)
    bsc = bp;

  //
  // We copy this even in the no_phy case, enabling a system with mPIPE
  // links but no PHYs to use the gxio_mpipe_link_mdio_{rd,wr}_ex() APIs
  // successfully.
  //
  mp->link_config.xgbe_mdio = p->mdio_bus_xgbe;

  if (p->no_phy)
  {
    if (bsc)
    {
      mp->link_config.sfp_cfg = *bsc;
      mp->link_config.has_sfp_cfg = 1;
      mp->led_sig = mp->link_config.sfp_cfg.link_led_sig;
    }
  }
  else
  {
    mp->link_config.has_phy = 1;
    mp->link_config.phyaddr = p->mdio_addr;
    mp->link_config.phy_auto_cfg = p->phy_auto_cfg;

    if (mp->link_config.xgbe_mdio)
      ms->xgbe_mdio_dev_mask |= 1 << p->mdio_addr;
    else
      ms->gbe_mdio_dev_mask  |= 1 << p->mdio_addr;

    //
    // Allow use of the SFP_CFG item to add a link-up LED signal even in
    // cases where we have a PHY.  This basically supports broken PHYs that
    // can't be configured to provide a simple link up/down indication.
    //
    if (bsc)
      mp->led_sig = bsc->link_led_sig;
  }

  drv_set_signal(mp->led_sig, DRV_SIGNAL_INIT | DRV_SIGNAL_DEASSERT);

  mp->link_config.sfp_txout_inv = p->sfp_txout_inv;

  for (int i = 0; i < 6; i++)
    mp->link_config.leds[i] = p->leds[i].cfg;

  //
  // Add the link name table entry.
  //
  if (link_table_len >= (sizeof link_table) / (sizeof *link_table))
    panic("mpipe: too many links defined in BIB");

  struct link_name_table* lnt = &link_table[link_table_len++];
  snprintf(lnt->name, sizeof (lnt->name), (is_gbe) ? "gbe%d" :
                                                     "xgbe%d", l_num);
  lnt->shim = ms->instance;
  lnt->mac_tbl_idx = mac_tbl_idx;
  lnt->channel = __builtin_ctzl(chans);

  strncpy(mp->link_config.device_name, lnt->name,
          sizeof (mp->link_config.device_name));
  memcpy(lnt->mac, mp->link_config.mac_addr, 6);

  //
  // Most of the link attributes default to 0, so they were initialized
  // when we allocated our data structures, but those that don't need to be
  // set here.
  //
  mp->link_config.discard_if_down = 1;

  return mp;
}


/** Add an entry to the link name table and the macs table for a loopback
 *  channel.
 * @param ms Driver state.
 * @param chan Channel number.
 */
static void
add_one_loop(mpipe_state_t* ms, int chan)
{
  int mac_tbl_idx = ms->n_macs++;
  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  mp->link_config.hv_private = ms;

  mp->link_config.loop = 1;
  mp->channels = (1L << chan);

  //
  // Compute the link name.
  //
  int l_num = next_loop_link_number++;

  //
  // Add the link name table entry.
  //
  if (link_table_len >= (sizeof link_table) / (sizeof *link_table))
    panic("mpipe: too many links defined in BIB");

  struct link_name_table* lnt = &link_table[link_table_len++];
  snprintf(lnt->name, sizeof (lnt->name), "loop%d", l_num);
  lnt->shim = ms->instance;
  lnt->mac_tbl_idx = mac_tbl_idx;
  lnt->channel = chan;

  strncpy(mp->link_config.device_name, lnt->name,
          sizeof (mp->link_config.device_name));

  //
  // Set up our link ops.
  //
  enet_probe_init_link(&mp->link_config, 1);
}


/** Construct the list of available links, and set up data structures which
 *  will be used for link management.
 * @param ms Driver state.
 */
void
init_link_data(mpipe_state_t* ms)
{
  if (sim_is_simulator())
  {
    ms->n_macs = sizeof (sim_link_table) / sizeof (sim_link_table[0]);

    //
    // Marking all links as loopback is a good way to ensure we won't
    // try to access any MAC registers which aren't implemented on the
    // simulator.
    //
    ms->macs = drv_shared_state_zalloc(ms->n_macs * sizeof (*ms->macs), 0);
    for (int i = 0; i < ms->n_macs; i++)
    {
      ms->macs[i].link_config.loop = 1;
      enet_probe_init_link(&ms->macs[i].link_config, 1);
    }

    memcpy(link_table, sim_link_table, sizeof (sim_link_table));

    link_table_len = ms->n_macs;

    return;
  }

  //
  // Make a pass through the BIB to collect any SERDES lane information.
  //
  bi_ptr_t bp;
  uint32_t desc;
  int bibpos = 0;

  memset(ms->serdes_rx_lane_length, 0, sizeof ms->serdes_rx_lane_length);
  memset(ms->serdes_tx_lane_length, 0, sizeof ms->serdes_tx_lane_length);

  union virt_inst {
    bi_inst_t instance;
    struct bi_clock_inst clock;
  } virt_inst = {
    .clock.type = BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN,
    .clock.shim = ms->virt_instance,
  };

  while ((desc = bi_getparam(BI_TYPE_SERDES_LANE_CHAR, virt_inst.instance,
                             &bp, &bibpos)) != BI_NULL)
  {
    struct bi_serdes_lane_char* p = bp;

    for (int i = 0; i < BI_WDS(desc); i++)
    {
      ms->serdes_rx_lane_length[i] += p->entries[i].rx_len;
      ms->serdes_tx_lane_length[i] += p->entries[i].tx_len;
    }
  }

  //
  // Make a pass through the BIB to see how many MAC table entries we'll
  // need, make a list of which MACs are in use so we can figure out which
  // ones to use for MDIO, and save a list of reset signals so that we can
  // reset the PHYs before we start doing anything with them.  Note that we
  // also look at the items for other mPIPEs, in order to make a list of
  // explicitly requested link numbers, so we know which ones are left over
  // when we do the assignment below.
  //
  int n_macs = 0;
  uint32_t gbe_macs_map = 0;
  uint32_t xgbe_macs_map = 0;
  sigdesc_t reset_sigs[32];
  int n_reset_sigs = 0;

  bibpos = 0;

  while ((desc = bi_getparam(BI_TYPE_PHY_LINK_CFG, -1, &bp, &bibpos)) !=
         BI_NULL)
  {
    struct bi_phy_link_cfg* p = bp;
    int gbe_mac_num;
    int xgbe_mac_num;
    int serdes_mac_num;
    uint8_t serdes_lanes;

    get_link_types(ms, p, &gbe_mac_num, &xgbe_mac_num, &serdes_mac_num,
                   &serdes_lanes);

    if (BI_INST(desc) == ms->virt_instance)
    {
      if (gbe_mac_num >= 0)
      {
        gbe_macs_map |= 1L << gbe_mac_num;
        n_macs++;
      }

      if (xgbe_mac_num >= 0)
      {
        xgbe_macs_map |= 1L << xgbe_mac_num;
        n_macs++;
      }

      if (p->reset_sig.type != BI_SIGNAL_TYPE__VAL_NONE &&
          n_reset_sigs < sizeof (reset_sigs) / sizeof (reset_sigs[0]))
        reset_sigs[n_reset_sigs++] = p->reset_sig;
    }

    if (p->link_name_num != BI_PHY_LINK_CFG_LINK_NAME_NUM__VAL_DEFAULT)
    {
      //
      // We could check for overlap here, but we don't.
      //
      if (gbe_mac_num >= 0)
        gbe_link_numbers_map |= (1UL << p->link_name_num);
      if (xgbe_mac_num >= 0)
        xgbe_link_numbers_map |= (1UL << p->link_name_num);
    }
  }

  //
  // See how many loopback channels we have and add them on.
  //
  uint_reg_t lchan = cfg_rd(ms->shim_pos.word, 0, MPIPE_LOOPBACK_MAP);
  n_macs += __builtin_popcount(lchan);

  //
  // Based on what we found, figure out which MACs will be used for MDIO.
  // We try to use a MAC that we're going to use anyway, but if we can't,
  // we use the lowest-numbered MAC of each type.
  //
  int gbe_mdio_mac = (gbe_macs_map) ? __builtin_ctzl(gbe_macs_map) : 0;
  int xgbe_mdio_mac = (xgbe_macs_map) ? __builtin_ctzl(xgbe_macs_map) : 4;

  //
  // Allocate the actual table.
  //
  ms->macs = drv_shared_state_zalloc(n_macs * sizeof (*ms->macs), 0);

  //
  // Figure out if we have a 125 MHz XAUI reference clock.
  //
  int xaui_refclk_125 = 0;

  if (bi_getparam(BI_TYPE_XAUI_REFCLK, ms->virt_instance, &bp, 0) != BI_NULL)
  {
    struct bi_xaui_refclk* p = bp;

    switch (p->speed)
    {
    case 125000000:
      xaui_refclk_125 = 1;
      break;

    case 156250000:
      break;

    default:
      printf("hv_warning: mpipe/%d: illegal XAUI reference clock (%d Hz), "
             "assuming 156.25 MHz\n", ms->instance, p->speed);
      break;
    }
  }

  //
  // Reset the PHYs before we try to probe them.  In the presence of shared
  // resets, we could streamline this a little by sorting the list of reset
  // signals and removing duplicates, but that's probably not critical.
  // The reset duration of 100 ms is just a guess, but it seems unlikely
  // that any chip would really need more than that.
  //
  for (int i = 0; i < n_reset_sigs; i++)
    drv_set_signal(reset_sigs[i], DRV_SIGNAL_ASSERT | DRV_SIGNAL_INIT);

  drv_udelay(100 * 1000);

  for (int i = 0; i < n_reset_sigs; i++)
    drv_set_signal(reset_sigs[i], DRV_SIGNAL_DEASSERT);

  //
  // Make a second pass through the BIB to actually create our MAC table
  // entries, fill in the link name table, and make a list of channel
  // speeds which will be used below to configure pause frame parameters.
  //
  bibpos = 0;
  n_macs = 0;
  long chan_speeds[MPIPE_NUM_NON_LB_CHANNELS] = { 0 };

  while (bi_getparam(BI_TYPE_PHY_LINK_CFG, ms->virt_instance, &bp, &bibpos) !=
         BI_NULL)
  {
    struct bi_phy_link_cfg* p = bp;
    struct mac_state* mp = 0;
    int gbe_mac_num;
    int xgbe_mac_num;
    int serdes_mac_num;
    uint8_t serdes_lanes;
    int chan = 0;

    get_link_types(ms, p, &gbe_mac_num, &xgbe_mac_num, &serdes_mac_num,
                   &serdes_lanes);

    //
    // Set up various bits of state in the link_config_t, then probe and
    // initialize the PHY.
    //
    if (xgbe_mac_num >= 0)
    {
      mp = add_one_link(ms, p, xgbe_mac_num, 0,
                        (p->mdio_bus_xgbe) ? xgbe_mdio_mac : gbe_mdio_mac,
                        serdes_mac_num, serdes_lanes, xaui_refclk_125);

      enet_probe_init_link(&mp->link_config, 1);

      chan = __builtin_ctzl(mp->channels);
    }

    if (gbe_mac_num >= 0)
    {
      mp = add_one_link(ms, p, gbe_mac_num, 1,
                        (p->mdio_bus_xgbe) ? xgbe_mdio_mac : gbe_mdio_mac,
                        serdes_mac_num, serdes_lanes, 0);

      enet_probe_init_link(&mp->link_config, 1);

      //
      // Note that we always use the same set of channels for both sides of
      // a combo PHY so it's OK to possibly do this twice.
      //
      chan = __builtin_ctzl(mp->channels);
    }

    //
    // Compute the top speed for this entry.
    //
    int mbps = 0;
    if (p->speed_50g)
      mbps = 50000;
    else if (p->speed_25g)
      mbps = 25000;
    else if (p->speed_20g)
      mbps = 20000;
    else if (p->speed_10g)
      mbps = 10000;
    else if (p->speed_1g)
      mbps = 1000;
    else if (p->speed_100m)
      mbps = 100;
    else if (p->speed_10m)
      mbps = 10;

    //
    // Update the channel speed array for this entry's channel.
    //
    chan_speeds[chan] = max(chan_speeds[chan], mbps);
  }

  //
  // Add loopback entries.
  //
  while (lchan)
  {
    add_one_loop(ms, __builtin_ctzl(lchan));
    lchan &= lchan - 1;
  }

  //
  // Now go through the channel speed array, and convert it into the number
  // of blocks in the iPkt buffer that each channel would be allocated if
  // we did it in proportion to each channel's speed.  Note that we stop
  // worrying about this early if we have no links with defined speeds,
  // since we won't be doing any pause frame activity in that case.
  //
  long tot_mbps = 0;
  for (int i = 0; i < MPIPE_NUM_NON_LB_CHANNELS; i++)
    tot_mbps += chan_speeds[i];

  if (tot_mbps)
  {
    MPIPE_IPKT_THRESH_t mit =
      { .word = cfg_rd(ms->shim_pos.word, 0, MPIPE_IPKT_THRESH) };

    for (int i = 0; i < MPIPE_NUM_NON_LB_CHANNELS; i++)
      chan_speeds[i] = (mit.num_blocks * chan_speeds[i]) / tot_mbps;

    //
    // Finally, set up the auto-pause thresholds for each queue so that they
    // can each consume 80% of their space before we send a pause frame.
    // Later we'll point each MAC at the queue with the same number as its
    // channel.
    //
    // Note that 80% is really just a rough guess; this might need to be
    // adjusted in the future.  Ideally we'd just provide a gxio API to set
    // all this stuff, but it's really hard to modify these registers once
    // any traffic is flowing.  If just tweaking the percentage is necessary
    // we could make it an argument passed to the mPIPE driver from the .hvc.
    //
    for (int i = 0; i < MPIPE_NUM_NON_LB_CHANNELS / 4; i++)
    {
      uint_reg_t reg_val = 0;

      for (int j = 0; j < 4; j++)
      {
        reg_val |= ((80 * chan_speeds[4 * i + j]) / 100) <<
                   (j * MPIPE_PR_PAUSE_THR__PR1_THRESH_SHIFT);
      }

      cfg_wr(ms->shim_pos.word, 0, MPIPE_PR_PAUSE_THR__FIRST_WORD + 8 * i,
             reg_val);
    }
  }
}


/** MAC interrupt routine.
 * @param intarg Interrupt argument.
 * @param msg Interrupt message (unused on Gx).
 * @param len Interrupt message length (unused on Gx).
 */
static void
mpipe_link_intr(void* intarg, void* msg, int len)
{
  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

  for (struct mac_state* mp = intarg; mp; mp = mp->intr_next)
    mp->link_config.ops->intr(&mp->link_config, 0);

  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);
}


/** Register interrupt handlers for our link interrupts.  This routine must
 *  only be called on one tile, expected to be the driver's the shared tile.
 *  It must be called after init_link_data() has been called (not necessarily
 *  on the same tile).
 * @param ms Driver state.
 */
void
init_link_intrs(mpipe_state_t* ms)
{
  if (sim_is_simulator())
    return;

  //
  // We chain MACs that share the same interrupt channel together, and
  // remember the channel number for later sharers.  Note that int_macs[x]
  // contains a pointer to the _last_ MAC in the chain.
  //
  struct mac_state* int_macs[LINK_INTR_PER_MPIPE] = { 0 };
  int int_channels[LINK_INTR_PER_MPIPE] = { 0 };
  int next_int = 0;

  for (int i = 0; i < ms->n_macs; i++)
  {
    struct mac_state* mp = &ms->macs[i];

    if (mp->link_config.loop)
      continue;

    int intchan;

    if (!int_macs[next_int])
    {
      intchan = drv_alloc_intchan();
      if (intchan < 0)
      {
        printf("hv_warning: can't allocate interrupt channel for %s, will not "
               "be notified of link state changes\n",
               mp->link_config.device_name);
        //
        // FIXME: we've already called probe_init_link, so it's kind of too
        // late to run the link in non-interrupt mode; maybe we should panic
        // here instead?  Or should we fix that so we can change to non-intr
        // mode later?
        //
        continue;
      }

      if (drv_register_intr(mpipe_link_intr, mp, DRV_INTR_DELAYED, intchan))
        panic("can't register mpipe link interrupt for %s",
              mp->link_config.device_name);

      int_channels[next_int] = intchan;
    }
    else
    {
      intchan = int_channels[next_int];
      int_macs[next_int]->intr_next = mp;
    }

    MPIPE_INT_BIND_t bind =
    {{
      .enable = 1,
      .mode = 0,
      .tileid = DRV_COORDS_TO_TILE_ID(my_pos.bits.x, my_pos.bits.y),
      .int_num = HV_PL,
      .evt_num = intchan,
      .vec_sel = MPIPE_INT_BIND__VEC_SEL_VAL_MAC,
      .bind_sel = mp->link_config.mac_num,
    }};

    cfg_wr(ms->shim_pos.word, 0, MPIPE_INT_BIND, bind.word);

    //
    // If this is a PHY-less SFP connection, hook up the link interrupt
    // handler to the MOD_ABS and RX_LOS signals.
    //
    if (mp->link_config.has_sfp_cfg)
    {
      target_signal_intr(mp->link_config.sfp_cfg.mod_abs_sig,
                         SIGNAL_ASSERT | SIGNAL_DEASSERT, my_pos, intchan);
      target_signal_intr(mp->link_config.sfp_cfg.rx_los_sig,
                         SIGNAL_ASSERT | SIGNAL_DEASSERT, my_pos, intchan);
    }

    int_macs[next_int] = mp;
    next_int++;
    if (next_int >= LINK_INTR_PER_MPIPE)
      next_int = 0;
  }
}


/** Return the base PTE that the client should use to access our shim's MMIO
 *  registers.  This is always invalid for an mmio_info device.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param base Pointer to returned base PTE.
 * @return Zero.
 */
int
handle_gxio_mpipe_info_get_mmio_base(mpipe_state_t* ms, int svc_dom,
                                     HV_PTE *base)
{
  //
  // The link IORPC device doesn't support any MMIO access, but we have to
  // return something to keep the kernel iorpc driver happy.  Since our
  // check_mmio_offset routine always returns an error, we'll never end
  // up mapping anything here.
  //
  HV_PTE pte = { 0 };
  *base = pte;
  return 0;
}


/** Check whether an MMIO range is legal; this is never true on an
 *  mmio_info device.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param offset Start of the range.
 * @param size Size of the range.
 * @return An error.
 */
int
handle_gxio_mpipe_info_check_mmio_offset(mpipe_state_t* ms, int svc_dom,
                                         unsigned long offset,
                                         unsigned long size)
{
  return GXIO_ERR_MMIO_ADDRESS;
}


/** Translate a link name to a mpipe instance number.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param name Link name.
 * @return The instance number or a negative error code.
 */
int
handle_gxio_mpipe_info_instance_aux(mpipe_state_t* ms, int svc_dom,
                                    _gxio_mpipe_link_name_t name)
{
  for (int i = 0; i < link_table_len; i++)
    if (!strcmp(name.name, link_table[i].name))
      return link_table[i].shim;

  return GXIO_ERR_NO_DEVICE;
}


/** Enumerate a link name and MAC address.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param idx Index into the list of names; 0 is the first name.
 * @param name Pointer to the returned name.
 * @param mac Pointer to the returned MAC address.
 * @returns Zero if successful, or a negative error code if index is out of
 *  range.
 */
int
handle_gxio_mpipe_info_enumerate_aux(mpipe_state_t* ms, int svc_dom,
                                     unsigned int idx,
                                     _gxio_mpipe_link_name_t* name,
                                     _gxio_mpipe_link_mac_t* mac)
{
  if (idx >= link_table_len)
    return GXIO_ERR_NO_DEVICE;

  strcpy(name->name, link_table[idx].name);
  memcpy(mac->mac, link_table[idx].mac, 6);
  return 0;
}


/** Open a link.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param name Link name.
 * @param flags Link flags.
 * @return The channel number (in bits 15:8) and MAC index (in bits 7:0) if
 *  successful, or a negative error code.
 */
int
handle_gxio_mpipe_link_open_aux(mpipe_state_t* ms, int svc_dom,
                                _gxio_mpipe_link_name_t name, uint flags)
{
  //
  // Find the requested link in the table.
  //
  int link_idx = 0;
  for (; link_idx < link_table_len; link_idx++)
    if (!strcmp(name.name, link_table[link_idx].name))
      break;

  if (link_idx >= link_table_len || link_table[link_idx].shim != ms->instance)
    return GXIO_ERR_NO_DEVICE;

  int mac_tbl_idx = link_table[link_idx].mac_tbl_idx;
  struct mac_state* mp = &ms->macs[mac_tbl_idx];
  int chan = link_table[link_idx].channel;

  //
  // Make sure we don't already have this link open.  This is to prevent
  // errors in the open count bookkeeping.
  //
  mpipe_resources_t* rp = &ms->svc_dom_resources[svc_dom];
  if ((rp->data_macs | rp->stats_macs | rp->control_macs) &
      (1L << mac_tbl_idx))
    return GXIO_ERR_BUSY;

  //
  // Validate flags and provide defaults.
  //
  int flag_grp;
  int flag_cnt;

  flag_grp = GXIO_MPIPE_LINK_DATA | GXIO_MPIPE_LINK_NO_DATA |
    GXIO_MPIPE_LINK_EXCL_DATA;
  flag_cnt = __builtin_popcount(flags & flag_grp);
  if (flag_cnt > 1)
    return GXIO_ERR_INVAL;
  if (flag_cnt == 0)
    flags |= GXIO_MPIPE_LINK_DATA;

  flag_grp = GXIO_MPIPE_LINK_STATS | GXIO_MPIPE_LINK_NO_STATS |
    GXIO_MPIPE_LINK_EXCL_STATS;
  flag_cnt = __builtin_popcount(flags & flag_grp);
  if (flag_cnt > 1)
    return GXIO_ERR_INVAL;
  if (flag_cnt == 0)
    flags |= GXIO_MPIPE_LINK_STATS;

  flag_grp = GXIO_MPIPE_LINK_CTL | GXIO_MPIPE_LINK_NO_CTL |
    GXIO_MPIPE_LINK_EXCL_CTL;
  flag_cnt = __builtin_popcount(flags & flag_grp);
  if (flag_cnt > 1)
    return GXIO_ERR_INVAL;
  if (flag_cnt == 0)
    flags |= GXIO_MPIPE_LINK_CTL;

  flag_grp = GXIO_MPIPE_LINK_AUTO_UP | GXIO_MPIPE_LINK_AUTO_UPDOWN |
    GXIO_MPIPE_LINK_AUTO_DOWN | GXIO_MPIPE_LINK_AUTO_NONE;
  flag_cnt = __builtin_popcount(flags & flag_grp);
  if (flag_cnt > 1)
    return GXIO_ERR_INVAL;
  if (flag_cnt == 0)
    flags |= GXIO_MPIPE_LINK_AUTO_UPDOWN;

  //
  // Ensure that the permissions requested are consistent with other users.
  //
  if (mp->num_data_opens < 0 ||
      ((flags & GXIO_MPIPE_LINK_EXCL_DATA) && mp->num_data_opens))
    return GXIO_ERR_BUSY;
  if (mp->num_stats_opens < 0 ||
      ((flags & GXIO_MPIPE_LINK_EXCL_DATA) && mp->num_stats_opens))
    return GXIO_ERR_BUSY;
  if (mp->num_control_opens < 0 ||
      ((flags & GXIO_MPIPE_LINK_EXCL_DATA) && mp->num_control_opens))
    return GXIO_ERR_BUSY;

  // Delta to data open count, applied below
  int ndo_delta = (flags & GXIO_MPIPE_LINK_EXCL_DATA) ? -1 :
                    (flags & GXIO_MPIPE_LINK_DATA) ? 1 : 0;
  // Delta to stats open count, applied below
  int nso_delta = (flags & GXIO_MPIPE_LINK_EXCL_STATS) ? -1 :
                    (flags & GXIO_MPIPE_LINK_STATS) ? 1 : 0;
  // Delta to control open count, applied below
  int nco_delta = (flags & GXIO_MPIPE_LINK_EXCL_CTL) ? -1 :
                    (flags & GXIO_MPIPE_LINK_CTL) ? 1 : 0;

  //
  // Make sure this link doesn't share any channels with another open
  // link, unless it's just open for stats, or we're opening for stats.
  //
  if (ndo_delta || nco_delta)
    for (int i = 0; i < ms->n_macs; i++)
      if (i != mac_tbl_idx &&
          (ms->macs[i].num_data_opens ||
           ms->macs[i].num_control_opens) &&
          (ms->macs[i].channels & ms->macs[mac_tbl_idx].channels))
        return GXIO_ERR_BUSY;

  //
  // If necessary, configure the MAC.
  //
  if (!mp->mac_configured)
  {
    enet_config_mac(&mp->link_config);
    mp->mac_configured = 1;
  }

  //
  // Now that we know we're opening, commit the changes to the permission
  // counts and channel/MAC bitmaps.
  //
  mp->num_data_opens += ndo_delta;
  mp->num_stats_opens += nso_delta;
  mp->num_control_opens += nco_delta;

  if (ndo_delta)
  {
    rp->data_macs |= 1L << mac_tbl_idx;
    rp->channels |= 1L << chan;
  }
  if (nso_delta)
    rp->stats_macs |= 1L << mac_tbl_idx;
  if (nco_delta)
    rp->control_macs |= 1L << mac_tbl_idx;

  //
  // If requested, try to bring the link up.
  //
  if (flags & (GXIO_MPIPE_LINK_AUTO_UP | GXIO_MPIPE_LINK_AUTO_UPDOWN))
  {
    enet_config_link(&mp->link_config,
                     mp->link_config.desired_state | GXIO_MPIPE_LINK_ANYSPEED);
  }

  //
  // Remember if we need to bring the link down on close.
  //
  if (flags & (GXIO_MPIPE_LINK_AUTO_DOWN | GXIO_MPIPE_LINK_AUTO_UPDOWN))
    rp->auto_down_macs |= 1L << mac_tbl_idx;

  return (chan << 8) | mac_tbl_idx;
}


/** Close a link.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param mac_tbl_idx MAC index.
 */
int
handle_gxio_mpipe_link_close_aux(mpipe_state_t* ms, int svc_dom,
                                 int mac_tbl_idx)
{
  mpipe_resources_t* rp = &ms->svc_dom_resources[svc_dom];

  //
  // Make sure this is a legal MAC.
  //
  if (mac_tbl_idx < 0 || mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  //
  // If we don't have the MAC open, we return an error.
  //
  if (((rp->data_macs | rp->stats_macs | rp->control_macs) &
       (1L << mac_tbl_idx)) == 0)
    return GXIO_ERR_NO_DEVICE;

  //
  // FIXME: Some hardware resource freeing may need to happen here.  We
  // also need to modify this service domain's set of classifier rules
  // to remove the channel being closed.
  //

  //
  // Decrement open counts appropriately.
  //
  if (rp->data_macs & (1L << mac_tbl_idx))
  {
    mp->num_data_opens += (mp->num_data_opens < 0) ? 1 : -1;
    rp->data_macs &= ~(1L << mac_tbl_idx);
    rp->channels &= ~(1L << __builtin_ctzl(mp->channels));
  }

  if (rp->stats_macs & (1L << mac_tbl_idx))
  {
    mp->num_stats_opens += (mp->num_stats_opens < 0) ? 1 : -1;
    rp->stats_macs &= ~(1L << mac_tbl_idx);
  }

  if (rp->control_macs & (1L << mac_tbl_idx))
  {
    mp->num_control_opens += (mp->num_control_opens < 0) ? 1 : -1;
    rp->control_macs &= ~(1L << mac_tbl_idx);
  }

  //
  // If this user marked this MAC for link-down-on-close, and there are no
  // more users, take the link down.
  //
  if (rp->auto_down_macs & (1L << mac_tbl_idx))
  {
    rp->auto_down_macs &= ~(1L << mac_tbl_idx);
    if (!(mp->num_data_opens | mp->num_stats_opens | mp->num_control_opens))
    {
      enet_config_link(&mp->link_config,
                       mp->link_config.desired_state &
                       ~GXIO_MPIPE_LINK_SPEED_MASK);
    }
  }

  return 0;
}


/** Get a link attribute.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param off Offset, containing the MAC table index and the attribute code.
 * @param data Pointer to returned attribute value.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_get_attr_aux(mpipe_state_t* ms, int svc_dom,
                                    unsigned int off, int64_t *data)
{
  uint32_t mac_tbl_idx = off >> 24;
  uint32_t attr = off & 0xFFFFFF;

  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  if (!(ms->svc_dom_resources[svc_dom].stats_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  //
  // Get the requested attribute.
  //
  switch (attr)
  {
  case GXIO_MPIPE_LINK_RECEIVE_JUMBO:
    *data = mp->link_config.jumbo;
    return 0;

  case GXIO_MPIPE_LINK_SEND_PAUSE:
    *data = mp->link_config.pause_out;
    return 0;

  case GXIO_MPIPE_LINK_RECEIVE_PAUSE:
    *data = mp->link_config.pause_in;
    return 0;

  case GXIO_MPIPE_LINK_MAC:
    *data = ((uint64_t) mp->link_config.mac_addr[0] << 40) |
            ((uint64_t) mp->link_config.mac_addr[1] << 32) |
            ((uint64_t) mp->link_config.mac_addr[2] << 24) |
            ((uint64_t) mp->link_config.mac_addr[3] << 16) |
            ((uint64_t) mp->link_config.mac_addr[4] <<  8) |
            ((uint64_t) mp->link_config.mac_addr[5] <<  0);
    return 0;

  case GXIO_MPIPE_LINK_DISCARD_IF_DOWN:
    *data = mp->link_config.discard_if_down;
    return 0;

  case GXIO_MPIPE_LINK_POSSIBLE_STATE:
    *data = mp->link_config.possible_state;
    return 0;

  case GXIO_MPIPE_LINK_CURRENT_STATE:
    *data = enet_inquire_link(&mp->link_config);
    return 0;

  case GXIO_MPIPE_LINK_DESIRED_STATE:
    *data = mp->link_config.desired_state;
    return 0;

  case GXIO_MPIPE_LINK_MODULE_TYPE:
  {
    int type;
    enet_get_module_eeprom(&mp->link_config, &type, 0, NULL, 0);
    *data = type;
    return 0;
  }

  default:
    return GXIO_ERR_INVAL;
  }

  return 0;
}


/** Set a link attribute.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param mac_tbl_idx MAC index.
 * @param attr Attribute code.
 * @param val Value to set attribute to.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_set_attr_aux(mpipe_state_t* ms, int svc_dom, int
                                    mac_tbl_idx, uint32_t attr,
                                    int64_t val)
{
  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx < 0 || mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  if (!(ms->svc_dom_resources[svc_dom].control_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  //
  // Set the requested attribute.
  //
  switch (attr)
  {
  case GXIO_MPIPE_LINK_RECEIVE_JUMBO:
    mp->link_config.jumbo = (val != 0);
    enet_set_jumbo(&mp->link_config, mp->link_config.jumbo);
    return 0;

  case GXIO_MPIPE_LINK_SEND_PAUSE:
    mp->link_config.pause_out = val & 0xFFFF;
    enet_set_pause(&mp->link_config, 0, mp->link_config.pause_out);
    return 0;

  case GXIO_MPIPE_LINK_RECEIVE_PAUSE:
    mp->link_config.pause_in = (val != 0);
    enet_set_pause(&mp->link_config, 1, mp->link_config.pause_in);
    return 0;

  case GXIO_MPIPE_LINK_DISCARD_IF_DOWN:
    mp->link_config.discard_if_down = (val != 0);
    enet_set_discard(&mp->link_config, mp->link_config.discard_if_down);
    return 0;

  case GXIO_MPIPE_LINK_DESIRED_STATE:
  {
    int err = enet_config_link(&mp->link_config, val);
    if (err < 0)
      return (err);

    return 0;
  }

  default:
    return GXIO_ERR_INVAL;
  }

  return 0;
}


/** Get a long link attribute.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param off Offset, containing the MAC table index, attribute code, and
 *  offset within the attribute.
 * @param data Pointer to returned attribute value.
 * @param data_size Length of the buffer pointed to by data.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_get_lattr_aux(mpipe_state_t* ms, int svc_dom,
                                     unsigned int off,
                                     void* data, size_t data_size)
{
  uint32_t mac_tbl_idx = off >> 24;
  uint32_t offset = (off >> 8) & 0xFFFF;
  uint32_t attr = off & 0xFF;

  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  if (!(ms->svc_dom_resources[svc_dom].stats_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  //
  // Get the requested attribute.
  //
  switch (attr)
  {
  case GXIO_MPIPE_LINK_MODULE_EEPROM:
    return enet_get_module_eeprom(&mp->link_config, NULL, offset,
                                  data, data_size);

  default:
    return GXIO_ERR_INVAL;
  }
}


/** Read an MDIO register.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param mac_tbl_idx MAC index.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param dev MDIO device number.
 * @param addr MDIO register address.
 * @return The 16-bit MDIO data on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_mdio_rd_aux(mpipe_state_t* ms, int svc_dom,
                                   int mac_tbl_idx, int phy, int dev,
				   int addr)
{
  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx < 0 || mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];
  enet_link_config_t* lc = &mp->link_config;

  if (!(ms->svc_dom_resources[svc_dom].stats_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  if (phy >= 0 && phy != lc->phyaddr) {
    uint32_t in_use = lc->xgbe_mdio ? ms->xgbe_mdio_dev_mask
				    : ms->gbe_mdio_dev_mask;
    if ((1 << phy) & in_use)
      return GXIO_ERR_PERM;
  } else {
    phy = lc->phyaddr;
  }

  //
  // No MDIO regs on loopback devices.
  //
  if (lc->loop)
      return GXIO_ERR_NO_DEVICE;

  //
  // Do the read.
  //
  uint32_t val;
  int err;

  if (dev >= 0)
    err = enet_mdio_cl45_rd(lc, phy, dev, addr, &val);
  else
    err = enet_mdio_cl22_rd(lc, phy, addr, &val);

  if (err)
    return GXIO_ERR_IO;

  return val;
}


/** Write an MDIO register.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param mac_tbl_idx MAC index.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param dev MDIO device number.
 * @param addr MDIO register address.
 * @param val Value to write to the register.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_mdio_wr_aux(mpipe_state_t* ms, int svc_dom,
                                   int mac_tbl_idx, int phy, int dev,
				   int addr, uint16_t val)
{
  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx < 0 || mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];
  enet_link_config_t* lc = &mp->link_config;

  if (!(ms->svc_dom_resources[svc_dom].control_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  if (phy >= 0 && phy != lc->phyaddr) {
    uint32_t in_use = lc->xgbe_mdio ? ms->xgbe_mdio_dev_mask
				    : ms->gbe_mdio_dev_mask;
    if ((1 << phy) & in_use)
      return GXIO_ERR_PERM;
  } else {
    phy = lc->phyaddr;
  }

  //
  // No MDIO regs on loopback devices.
  //
  if (lc->loop)
      return GXIO_ERR_NO_DEVICE;

  //
  // Do the write.
  //
  int err;

  if (dev >= 0)
    err = enet_mdio_cl45_wr(lc, phy, dev, addr, val);
  else
    err = enet_mdio_cl22_wr(lc, phy, addr, val);

  if (err)
    return GXIO_ERR_IO;

  return 0;
}


/** Read a MAC register.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param off Offset, containing the MAC table index and the attribute code.
 * @param data Pointer to returned attribute value.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_mac_rd_aux(mpipe_state_t* ms, int svc_dom,
                                  unsigned int off, int64_t *data)
{
  uint32_t mac_tbl_idx = off >> 24;
  uint32_t addr = off & 0xFFFFFF;

  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  if (!(ms->svc_dom_resources[svc_dom].stats_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  //
  // No MDIO regs on loopback devices.
  //
  if (mp->link_config.loop)
      return GXIO_ERR_NO_DEVICE;

  //
  // Make sure offset is in the MAC region and is properly aligned.
  //
  if (addr > (MPIPE_CFG_REGION_ADDR__REG_MASK & -8) || (addr & 7))
    return GXIO_ERR_INVAL;

  //
  // Do the read.
  //
  *data = cfg_rd(ms->shim_pos.word, mp->link_config.mac_pa, addr);

  return 0;
}


/** Write a MAC register.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param mac_tbl_idx MAC index.
 * @param addr Address of the register within the MAC.
 * @param val Value to set the MAC register to.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_link_mac_wr_aux(mpipe_state_t* ms, int svc_dom,
                                  int mac_tbl_idx, int addr, uint32_t val)
{
  //
  // Make sure this is a legal MAC, and that we have the right permissions.
  //
  if (mac_tbl_idx < 0 || mac_tbl_idx >= ms->n_macs)
    return GXIO_ERR_NO_DEVICE;

  struct mac_state* mp = &ms->macs[mac_tbl_idx];

  if (!(ms->svc_dom_resources[svc_dom].control_macs & (1L << mac_tbl_idx)))
    return GXIO_ERR_PERM;

  //
  // No MDIO regs on loopback devices.
  //
  if (mp->link_config.loop)
      return GXIO_ERR_NO_DEVICE;

  //
  // Make sure offset is in the MAC region and is properly aligned.
  //
  if (addr > (MPIPE_CFG_REGION_ADDR__REG_MASK & -8) || (addr & 7))
    return GXIO_ERR_INVAL;

  //
  // Disallow use of the MDIO registers, since we have a dedicated MDIO
  // interface already, and trying to touch those registers from random
  // MACs will almost certainly not produce the expected results.
  //
  if (addr == ((mp->link_config.gbe) ? MPIPE_GBE_PHY_MAINTENANCE
                                    : MPIPE_XAUI_MDIO_CONTROL))
    return GXIO_ERR_INVAL;

  //
  // Do the write.
  //
  cfg_wr(ms->shim_pos.word, mp->link_config.mac_pa, addr, val);

  return 0;
}


/** Configure link status interrupts for a pollable FD.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param inter_x X coordinate of target tile.
 * @param inter_y Y coordinate of target tile.
 * @param inter_ipi Target IPI register set, typically the PL.
 * @param inter_event Target IPI event number.
 * @param linkmask Bitmap of links on which we desire status; if we want to
 *  know about a link, the bit corresponding to its MAC table index is set.
 * @return A non-negative interrupt cookie on success, or a negative error
 *  code.
 */
int
handle_gxio_mpipe_link_cfg_pollfd(mpipe_state_t* ms, int svc_dom,
                                  int inter_x, int inter_y,
                                  int inter_ipi, int inter_event,
                                  uint64_t linkmask)
{
  mpipe_resources_t* rp = &ms->svc_dom_resources[svc_dom];

  //
  // We use no MAC bits set as a flag to mean "free interrupt table
  // entry", so we can't allow that.  Plus, it's pointless.
  //
  linkmask &= rp->stats_macs;
  if (!linkmask)
    return GXIO_ERR_INVAL;

  //
  // Find a free entry in this domain's pollable interrupt table.
  //
  int cookie = -1;

  for (int i = 0; i < MPIPE_POLLFD_LINK_INTR_PER_SD; i++)
    if (!rp->pollfd_link_intrs[i].intr_macs)
    {
      cookie = i;
      break;
    }

  if (cookie < 0)
    return GXIO_ERR_BUSY;

  //
  // Construct and save the PA for the IPI that we'll send on link status
  // change, as well as the bitmap of MACs to pay attention to.  Mark the
  // interrupt as unarmed so we don't send interrupts before they're asked
  // for.
  //









  IPI_REMOTE_TRIGGER_ADDR_t addr = {{
      .tile_y = inter_y,
      .tile_x = inter_x,
      .ipi = inter_ipi,
      .event = inter_event,
  }};


  rp->pollfd_link_intrs[cookie].armed = 0;
  rp->pollfd_link_intrs[cookie].intr_macs = linkmask;
  rp->pollfd_link_intrs[cookie].ipi_addr = addr.word;

  return cookie;
}


/** Arm link status interrupts.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param cookie Interrupt cookie returned from
 *  handle_gxio_mpipe_link_cfg_pollfd.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_arm_pollfd(mpipe_state_t* ms, int svc_dom, int cookie)
{
  mpipe_resources_t* rp = &ms->svc_dom_resources[svc_dom];

  //
  // Make sure the cookie is in-range and the table entry isn't free.
  //
  if (cookie < 0 || cookie >= MPIPE_POLLFD_LINK_INTR_PER_SD ||
      !rp->pollfd_link_intrs[cookie].intr_macs)
    return GXIO_ERR_INVAL;

  //
  // Arm the interrupt.
  //
  rp->pollfd_link_intrs[cookie].armed = 1;

  return 0;
}


/** Deconfigure link status interrupts.
 * @param ms Driver state.
 * @param svc_dom Service domain.
 * @param cookie Interrupt cookie returned from
 *  handle_gxio_mpipe_link_cfg_pollfd.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_mpipe_close_pollfd(mpipe_state_t* ms, int svc_dom, int cookie)
{
  mpipe_resources_t* rp = &ms->svc_dom_resources[svc_dom];

  //
  // Make sure the cookie is in-range.
  //
  if (cookie < 0 || cookie >= MPIPE_POLLFD_LINK_INTR_PER_SD)
    return GXIO_ERR_INVAL;

  //
  // Free the interrupt table entry, thus disabling the interrupt.  We
  // don't, strictly speaking, need to clear the armed flag, but we check
  // it first in enet_new_link_state_hook so this slightly improves
  // performance.
  //
  rp->pollfd_link_intrs[cookie].intr_macs = 0;
  rp->pollfd_link_intrs[cookie].armed = 0;

  return 0;
}


/** Callback which gets invoked by the link framework when the link state
 *  changes.
 * @param lc Link configuration.
 * @param old_state Old link state.
 * @param new_state New link state.
 */
void
enet_new_link_state_hook(enet_link_config_t* lc, uint32_t old_state,
                         uint32_t new_state)
{
  //
  // We need the mpipe_state pointer as well as the mac_state pointer, but
  // we only have one pointer passed to us from the enet subsystem.  To
  // get both of them, we use the passed pointer for mpipe_state, and we
  // work outward from the link_config struct to the enclosing mac_state
  // structure.  This means that this routine can't be called with any
  // random link_config pointer, but that's OK.
  //
  mpipe_state_t* ms = lc->hv_private;
  struct mac_state* mp =
    (struct mac_state*) ((uintptr_t) lc -
                         offsetof(struct mac_state, link_config));

  uint32_t state_change = old_state ^ new_state;
  int link_speed = new_state & ENET_LINK_SPEED;

  if (state_change & ENET_LINK_SPEED)
  {
    //
    // Link went up or down.
    //
    if (link_speed)
    {
      printf("%s: link up", lc->device_name);
      switch (link_speed)
      {
      case ENET_LINK_20G:
        printf(", 20 Gbps\n");
        break;
      case ENET_LINK_10G:
        printf(", 10 Gbps\n");
        break;
      case ENET_LINK_12G:
        printf(", 12 Gbps\n");
        break;
      case ENET_LINK_1G:
        printf(", 1 Gbps\n");
        break;
      case ENET_LINK_100M:
        printf(", 100 Mbps\n");
        break;
      case ENET_LINK_10M:
        printf(", 10 Mbps\n");
        break;
      default:
        printf("\n");
        break;
      }
    }
    else
      printf("%s: link down\n", lc->device_name);

    //
    // Change the link LED.  If we don't have one, this descriptor is of
    // type NONE so this won't do anything.
    //
    drv_set_signal(mp->led_sig,
                   (link_speed) ? DRV_SIGNAL_ASSERT : DRV_SIGNAL_DEASSERT);
  }

  if (state_change)
  {
    //
    // Send any requested interrupts.  We figure out our MAC index number
    // via pointer arithmetic on our mac_state pointer and the start of the
    // table.
    //
    uint32_t mac_mask = 1 << (mp - ms->macs);
    for (int i = 0; i < MPIPE_MMIO_NUM_SVC_DOM; i++)
    {
      for (int j = 0; j < MPIPE_POLLFD_LINK_INTR_PER_SD; j++)
      {
        pollfd_link_intr_t* pp = &ms->svc_dom_resources[i].pollfd_link_intrs[j];
        if (pp->armed && (pp->intr_macs & mac_mask))
        {
          pp->armed = 0;
          cfg_wr(my_ipi_pos.word, 0, pp->ipi_addr, 0);
        }
      }
    }
  }
}


//
// There are a number of symbols used by the enet_xxx() routines (which
// are shared between BME and the hypervisor) which must match similar
// symbols used by GXIO or by the hypervisor itself.  This file includes
// both sets of header files, so this is a good place to make sure they
// haven't diverged.
//
#if (ENET_LINK_10M        != GXIO_MPIPE_LINK_10M)         || \
    (ENET_LINK_100M       != GXIO_MPIPE_LINK_100M)        || \
    (ENET_LINK_1G         != GXIO_MPIPE_LINK_1G)          || \
    (ENET_LINK_10G        != GXIO_MPIPE_LINK_10G)         || \
    (ENET_LINK_12G        != GXIO_MPIPE_LINK_12G)         || \
    (ENET_LINK_20G        != GXIO_MPIPE_LINK_20G)         || \
    (ENET_LINK_25G        != GXIO_MPIPE_LINK_25G)         || \
    (ENET_LINK_50G        != GXIO_MPIPE_LINK_50G)         || \
    (ENET_LINK_ANYSPEED   != GXIO_MPIPE_LINK_ANYSPEED)    || \
    (ENET_LINK_SPEED      != GXIO_MPIPE_LINK_SPEED_MASK)  || \
    (ENET_LINK_LOOP_MAC   != GXIO_MPIPE_LINK_LOOP_MAC)    || \
    (ENET_LINK_LOOP_PHY   != GXIO_MPIPE_LINK_LOOP_PHY)    || \
    (ENET_LINK_LOOP_EXT   != GXIO_MPIPE_LINK_LOOP_EXT)    || \
    (ENET_LINK_HDX        != GXIO_MPIPE_LINK_HDX)         || \
    (ENET_LINK_FDX        != GXIO_MPIPE_LINK_FDX)         || \
    (ENET_MODULE_NONE     != GXIO_MPIPE_LINK_MODULE_NONE) || \
    (ENET_MODULE_8079     != GXIO_MPIPE_LINK_MODULE_8079) || \
    (ENET_MODULE_8472     != GXIO_MPIPE_LINK_MODULE_8472) || \
    (ENET_EINVAL          != HV_EINVAL)                   || \
    (ENET_ENOTSUP         != HV_ENOTSUP)                  || \
    (ENET_EBUSY           != HV_EBUSY)

#error Mismatch between ENET_xxx and GXIO_MPIPE_xxx or HV_xxx symbols
#endif
