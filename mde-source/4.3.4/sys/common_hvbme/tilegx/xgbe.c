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
 * Shared routines for the 10 Gigabit Ethernet driver.
 */

#include <stdio.h>
#include <util.h>

#include <arch/mpipe.h>
#include <arch/mpipe_constants.h>
#include <arch/mpipe_gbe.h>
#include <arch/mpipe_xaui.h>

#include "board_info.h"
#include "cfg.h"
#include "enet.h"
#include "enet_specific.h"
#include "shared_lock.h"


extern int drv_get_mac(int is_xaui, int instance, uint8_t mac[]);

static void
gbe_set_pause(enet_link_config_t* lc, int is_in, int value)
{
  if (is_in)
  {
    MPIPE_GBE_NETWORK_CONFIGURATION_t mgnc =
    {
      .word = cfg_rd(lc->shim_port, lc->mac_pa,
                     MPIPE_GBE_NETWORK_CONFIGURATION)
    };

    mgnc.rx_pause_ena = (value != 0);

    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONFIGURATION,
           mgnc.word);
  }
  else
  {
    MPIPE_GBE_MAC_INTFC_CTL_t intf_ctl =
    {
      .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_CTL)
    };

    if (value)
    {
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_TRANSMIT_PAUSE_QUANTUM,
             value);

      intf_ctl.pause_mode = MPIPE_GBE_MAC_INTFC_CTL__PAUSE_MODE_VAL_AUTO;
    }
    else
    {
      intf_ctl.pause_mode = MPIPE_GBE_MAC_INTFC_CTL__PAUSE_MODE_VAL_MANUAL;
    }
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_CTL,
           intf_ctl.word);
  }
}


void
enet_set_pause(enet_link_config_t* lc, int is_in, int value)
{
  if (lc->gbe)
  {
    gbe_set_pause(lc, is_in, value);
    return;
  }

  if (lc->loop)
    return;

  if (is_in)
  {
    MPIPE_XAUI_TRANSMIT_CONFIGURATION_t mxtc =
    {
      .word = cfg_rd(lc->shim_port, lc->mac_pa,
                     MPIPE_XAUI_TRANSMIT_CONFIGURATION)
    };

    mxtc.pause_det = (value != 0);

    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_TRANSMIT_CONFIGURATION,
           mxtc.word);
  }
  else
  {
    MPIPE_XAUI_MAC_INTFC_CTL_t intf_ctl =
    {
      .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_CTL)
    };

    if (value)
    {
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_TRANSMIT_PAUSE_QUANTA,
             value);

      intf_ctl.pause_mode = MPIPE_XAUI_MAC_INTFC_CTL__PAUSE_MODE_VAL_AUTO;
    }
    else
    {
      intf_ctl.pause_mode = MPIPE_XAUI_MAC_INTFC_CTL__PAUSE_MODE_VAL_MANUAL;
    }
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_CTL,
           intf_ctl.word);
  }
}


void
enet_set_jumbo(enet_link_config_t* lc, int accept_jumbo)
{
  if (lc->loop)
    return;

  unsigned long regaddr =
    (lc->gbe) ? MPIPE_GBE_NETWORK_CONFIGURATION :
                MPIPE_XAUI_RECEIVE_CONFIGURATION;
  uint_reg_t bit =
    (lc->gbe) ? MPIPE_GBE_NETWORK_CONFIGURATION__JUMBO_ENA_MASK :
                MPIPE_XAUI_RECEIVE_CONFIGURATION__ACCEPT_JUMBO_MASK;

  uint_reg_t regval = cfg_rd(lc->shim_port, lc->mac_pa, regaddr);

  regval = (accept_jumbo) ? regval | bit : regval & ~bit ;

  cfg_wr(lc->shim_port, lc->mac_pa, regaddr, regval);
}


void
enet_set_discard(enet_link_config_t* lc, int discard)
{
  if (lc->loop)
    return;

  //
  // We take this lock primarily to keep the interrupt routine from
  // racing with us and confusing the state of lc->discarding, but
  // it's also needed in case we need to get state on a non-interrupting
  // link.
  //
  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

  //
  // For XAUI, we use the MAC's transmit discard bit to implement
  // discard-if-down.  For GbE, we use the MAC's UNI_DIR bit, since the
  // transmit discard bit does not work (hardware erratum 11079).
  //
  if (lc->gbe)
  {
    //
    // UNI_DIR enables transmission when the link is down, so we just turn
    // it on if discard is requested, and off otherwise; we don't need to
    // take the current link state into account.
    //
    MPIPE_GBE_NETWORK_CONFIGURATION_t mgnc =
    {
      .word = cfg_rd(lc->shim_port, lc->mac_pa,
                     MPIPE_GBE_NETWORK_CONFIGURATION)
    };

    mgnc.uni_dir = (discard != 0);

    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONFIGURATION,
           mgnc.word);

#ifdef LINK_DEBUG
      tprintf("%s: %sabled packet discard\n", lc->device_name,
              discard ? "en" : "dis");
#endif
  }
  else
  {
    //
    // TX_DROP is active all the time, not just when the link is down.
    // Thus, we need to set it based on the current link state and the
    // requested discard behavior.
    //
    if (!discard && lc->discarding)
    {
      //
      // If we're currently throwing away packets, but we don't want to,
      // stop.  Note that we don't need to check the link state in this case;
      // if we're discarding it must be down.
      //
      uint_reg_t tc = cfg_rd(lc->shim_port, lc->mac_pa,
                             MPIPE_XAUI_MAC_INTFC_TX_CTL);
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_TX_CTL,
             tc & ~MPIPE_XAUI_MAC_INTFC_TX_CTL__TX_DROP_MASK);
      lc->discarding = 0;
#ifdef LINK_DEBUG
      tprintf("%s: disabled packet discard\n", lc->device_name);
#endif
    }
    else if (discard && !lc->discarding)
    {
      //
      // If we want to throw away packets, and we aren't already doing so,
      // then we need to see if the link is down; if so, start discarding.
      //
      uint32_t link_state = (lc->link_does_intr) ? lc->current_state :
                                                   lc->ops->get_state(lc);

      if ((link_state & ENET_LINK_SPEED) == 0)
      {
        uint_reg_t tc = cfg_rd(lc->shim_port, lc->mac_pa,
                               MPIPE_XAUI_MAC_INTFC_TX_CTL);
        cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_TX_CTL,
               tc | MPIPE_XAUI_MAC_INTFC_TX_CTL__TX_DROP_MASK);
        lc->discarding = 1;
#ifdef LINK_DEBUG
      tprintf("%s: enabled packet discard\n", lc->device_name);
#endif
      }
    }
  }

  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);
}


/** Write the XGM's MDIO control register and wait until the operation
 *  completes.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param reg Value to write.
 * @param err String to include with a timeout error message.
 */
static void
xgbe_mdio_op(uint32_t shim_addr, uint32_t channel,
             MPIPE_XAUI_MDIO_CONTROL_t reg, char* err)
{
  // Write the MDIO control register.
  cfg_wr(shim_addr, channel, MPIPE_XAUI_MDIO_CONTROL, reg.word);

  // Wait for the MDIO operation to finish.
  for (int retries = 0; retries < 1000; retries++)
  {
    uint_reg_t rd_data = cfg_rd(shim_addr, channel,
                                MPIPE_XAUI_DIAG_INTERRUPT_STATUS);
    if (rd_data & MPIPE_XAUI_DIAG_INTERRUPT_STATUS__MDIO_COMPLETE_MASK)
      return;
  };

  panic("xgbe_mdio_op timed out (%s): %#x/%#x val %#llx", err, shim_addr,
        channel, reg.word);
}


uint32_t
xgbe_mdio_cl22_rd(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                  uint32_t devaddr, uint32_t reg)
{
  // Set the MDIO control register fields for a read operation.
  MPIPE_XAUI_MDIO_CONTROL_t mcr =
  {{
    .data = 0,
    .m10 = 2,
    .dev_addr = reg,
    .port_addr = phyaddr,
    .op = 2,
    .cls22 = 1,
  }};

  // Write the MDIO control register.
  xgbe_mdio_op(shim_addr, channel, mcr, "cl22_rd");

  // Return the MDIO read data.
  mcr.word = cfg_rd(shim_addr, channel, MPIPE_XAUI_MDIO_CONTROL);
  return(mcr.data);
}


void
xgbe_mdio_cl22_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                  uint32_t devaddr, uint32_t reg, uint32_t data)
{
  // Set the MDIO control register fields for a write operation.

  MPIPE_XAUI_MDIO_CONTROL_t mcr =
  {{
    .data = data,
    .m10 = 2,
    .dev_addr = reg,
    .port_addr = phyaddr,
    .op = 1,
    .cls22 = 1,
  }};

  xgbe_mdio_op(shim_addr, channel, mcr, "cl22_wr");

  return;
}


uint32_t
xgbe_mdio_cl45_rd(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                  uint32_t devaddr, uint32_t reg)
{
  // Set the MDIO control register fields for the first phase of a read
  // operation.

  MPIPE_XAUI_MDIO_CONTROL_t mcr =
  {{
    .data = reg,
    .m10 = 2,
    .dev_addr = devaddr,
    .port_addr = phyaddr,
    .op = 0,
    .cls22 = 0,
  }};

  // Write the MDIO control register.
  xgbe_mdio_op(shim_addr, channel, mcr, "cl45_rd_1");

  // Now set up for second phase.
  mcr.op = 3;
  mcr.data = 0;

  // Write the MDIO control register.
  xgbe_mdio_op(shim_addr, channel, mcr, "cl45_rd_2");

  // Return the MDIO read data.
  mcr.word = cfg_rd(shim_addr, channel, MPIPE_XAUI_MDIO_CONTROL);
  return(mcr.data);
}


void
xgbe_mdio_cl45_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                  uint32_t devaddr, uint32_t reg, uint32_t data)
{
  // Set the MDIO control register fields for the first phase of a write
  // operation.

  MPIPE_XAUI_MDIO_CONTROL_t mcr =
  {{
    .data = reg,
    .m10 = 2,
    .dev_addr = devaddr,
    .port_addr = phyaddr,
    .op = 0,
    .cls22 = 0,
  }};

  // Write the MDIO control register.
  xgbe_mdio_op(shim_addr, channel, mcr, "cl45_wr_1");

  // Now set up for second phase.
  mcr.op = 1;
  mcr.data = data;

  // Write the MDIO control register.
  xgbe_mdio_op(shim_addr, channel, mcr, "cl45_wr_2");

  return;
}


int
enet_config_link(enet_link_config_t* lc, uint32_t desired_state)
{
  int retval = 0;
  int locked = 0;

  //
  // If they've asked for any speed, replace the speed bits with the
  // set of valid speeds for this link.
  //
  if (desired_state & ENET_LINK_ANYSPEED)
    desired_state = (desired_state & ~ENET_LINK_SPEED) |
                    (lc->possible_state & ENET_LINK_SPEED);

  //
  // Make sure we have appropriate duplex flags.
  //
  if ((desired_state & (ENET_LINK_HDX | ENET_LINK_FDX)) == 0)
    desired_state |= (ENET_LINK_HDX | ENET_LINK_FDX) & lc->possible_state;

  //
  // Make sure they're not asking for anything this link can't do; this
  // saves individual plugins from having to do this check.
  //
  if (desired_state & ~lc->possible_state)
    return ENET_BAD_CONFIG;

  //
  // You can't enable multiple loopback types.
  //
  if (__builtin_popcount(desired_state & ENET_LINK_ALLLOOP) > 1)
    return ENET_BAD_CONFIG;

  //
  // If we're taking the link down, reset the warned and spun flags, so
  // we'll do those operations once on the next link up request.
  //
  if ((desired_state & ENET_LINK_SPEED) == 0)
  {
    lc->link_warned = 0;
    lc->link_spun = 0;
  }

  //
  // If we're already trying to accomplish what we've been asked to do,
  // we don't need to restart the config process.
  //
  if (desired_state != lc->desired_state)
  {
    //
    // Before we make the configuration change, enable or disable
    // MAC interrupts as appropriate.
    //
    if (lc->link_does_intr)
    {
      //
      // Note that the interrupt masking on the XAUI and GBE PHYs is
      // not at all the same; see the hardware docs for details.
      //
      if (lc->gbe)
      {
        if ((desired_state & ENET_LINK_SPEED) == 0)
        {
          // Mask the MAC interrupts we had enabled.
          cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_DISABLE,
                 lc->mac_intrs);
        }
        else
        {
          // Clear any previously set MAC interrupts.
          cfg_wr(lc->shim_port, lc->mac_pa,
                 MPIPE_GBE_INTERRUPT_STATUS, lc->mac_intrs);

          // Unmask the MAC interrupts we care about.
          cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_ENABLE,
                 lc->mac_intrs);
        }
      }
      else
      {
        if ((desired_state & ENET_LINK_SPEED) == 0)
        {
          // Mask the MAC interrupts we had enabled.
          uint_reg_t mac_intr_mask = cfg_rd(lc->shim_port, lc->mac_pa,
                                            MPIPE_XAUI_INTERRUPT_MASK);
          mac_intr_mask |= lc->mac_intrs;
          cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_MASK,
                 mac_intr_mask);
        }
        else
        {
          // Clear any previously set MAC interrupts.
          cfg_wr(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_INTERRUPT_STATUS, lc->mac_intrs);

          // Unmask the MAC interrupts we care about.
          uint_reg_t mac_intr_mask = cfg_rd(lc->shim_port, lc->mac_pa,
                                            MPIPE_XAUI_INTERRUPT_MASK);
          mac_intr_mask &= ~lc->mac_intrs;
          cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_MASK,
                 mac_intr_mask);
        }
      }
    }

    hvbme_spin_lock(HVBME_SPINLOCK_MDIO);
    locked = 1;

    retval = lc->ops->start_config(lc, desired_state);
    if (retval)
    {
      hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);
      return retval;
    }

    lc->desired_state = desired_state;
  }

  if (locked)
    hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

  return retval;
}


static int
xgbe_config_mac(enet_link_config_t* lc)
{
  //
  // Set the first MAC address; this is only currently used for sending
  // pause frames.
  //
  cfg_wr(lc->shim_port, lc->mac_pa,
         MPIPE_XAUI_EXACT_MATCH_TOP_0,
         (lc->mac_addr[0] << 8) |
         lc->mac_addr[1]);

  cfg_wr(lc->shim_port, lc->mac_pa,
         MPIPE_XAUI_EXACT_MATCH_BOTTOM_0,
         (lc->mac_addr[2] << 24) |
         (lc->mac_addr[3] << 16) |
         (lc->mac_addr[4] << 8) |
         lc->mac_addr[5]);

  //
  // Configure pause frame generation.
  //
  enet_set_pause(lc, 0, lc->pause_out);
  enet_set_pause(lc, 1, lc->pause_in);

  MPIPE_XAUI_RECEIVE_CONFIGURATION_t rx_config =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa,
                    MPIPE_XAUI_RECEIVE_CONFIGURATION)
  };

  rx_config.copy_all = 1;                // Receive all frames
  rx_config.fcs_remove = 1;              // Remove FCS from frames
  rx_config.accept_1536 = 1;             // Accept VLAN frames
  rx_config.discard_pause = 1;           // Discard pause frames

  MPIPE_XAUI_TRANSMIT_CONFIGURATION_t tx_config =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa,
                   MPIPE_XAUI_TRANSMIT_CONFIGURATION)
  };

  MPIPE_XAUI_MAC_INTFC_CTL_t intf_ctl =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa,
                   MPIPE_XAUI_MAC_INTFC_CTL)
  };

  intf_ctl.tx_prq_ena = 1 << lc->chan;
  intf_ctl.prq_ovd = 1;
  intf_ctl.prq_ovd_val = lc->chan;

  if (lc->jumbo)
    rx_config.accept_jumbo = 1;   // Accept jumbo frames

#if 0
  //
  // FIXME this stuff may well be wrong now.  Plus we don't have any way to
  // turn it on.
  //
  if (lc->tag_brcm)
  {
    rx_config.pass_preamble = 1;  // Return preamble as part of packet
    rx_config.accept_nsp = 1;     // Accept any preamble
    intf_ctl.dis_pre = 1;         // Disable preamble generation on transmit
    rx_config.preamble_crc = 2;   // Include first 8 preamble bytes in CRC
    tx_config.preamble_crc = 2;   // Include first 8 preamble bytes in CRC
  }

  if (lc->tag_brcm || lc->tag_mrvl)
  {
    tx_config.decrease_ipg = 1;   // Send average 8-byte IPG, instead of 12
  }
#endif

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_RECEIVE_CONFIGURATION,
         rx_config.word);
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_TRANSMIT_CONFIGURATION,
         tx_config.word);
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_CTL,
         intf_ctl.word);

  return 1;
}


/** Write the GEM's MDIO control register and wait until the operation
 *  completes.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param reg Value to write.
 * @param err String to include with a timeout error message.
 */
static void
gbe_mdio_op(uint32_t shim_addr, uint32_t channel,
            MPIPE_GBE_PHY_MAINTENANCE_t reg, char* err)
{
  // Write the MDIO control register.
  cfg_wr(shim_addr, channel, MPIPE_GBE_PHY_MAINTENANCE, reg.word);

  // Wait for the MDIO operation to finish.
  for (int retries = 0; retries < 1000; retries++)
  {
    uint32_t rd_data = cfg_rd(shim_addr, channel, MPIPE_GBE_NETWORK_STATUS);
    if (rd_data & MPIPE_GBE_NETWORK_STATUS__PHY_MANAGEMENT_IDLE_MASK)
      return;
  }

  panic("gbe_mdio_op timed out (%s): %#x/%#x val %#llx", err, shim_addr,
        channel, reg.word);
}


uint32_t
gbe_mdio_rd(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
            uint32_t devaddr, uint32_t reg)
{
  // Set the MDIO control register fields for a read operation.
  MPIPE_GBE_PHY_MAINTENANCE_t gpmr =
  {{
    .mb2 = 2,
    .data = 0,
    .reg_addr = reg,
    .phy_addr = phyaddr,
    .op = 2,
    .cls_22 = 1,
  }};

  // Write the GEM PHY maintenance register.
  gbe_mdio_op(shim_addr, channel, gpmr, "rd");

  // Return the MDIO read data.
  gpmr.word = cfg_rd(shim_addr, channel, MPIPE_GBE_PHY_MAINTENANCE);
  return gpmr.data;
}


void
gbe_mdio_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
            uint32_t devaddr, uint32_t reg, uint32_t data)
{
  MPIPE_GBE_PHY_MAINTENANCE_t gpmr =
  {{
    .mb2 = 2,
    .data = data,
    .reg_addr = reg,
    .phy_addr = phyaddr,
    .op = 1,
    .cls_22 = 1,
  }};

  // Write the GEM PHY maintenance register.
  gbe_mdio_op(shim_addr, channel, gpmr, "wr");
}


uint32_t
enet_inquire_link(enet_link_config_t* lc)
{
  if (lc->link_does_intr)
    return lc->current_state;

  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);
  uint32_t retval = lc->ops->get_state(lc);
  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);
  return retval;
}


static int
gbe_config_mac(enet_link_config_t* lc)
{
  //
  // Set the first MAC address; this is only currently used for sending
  // pause frames.
  //
  cfg_wr(lc->shim_port, lc->mac_pa,
         MPIPE_GBE_SPECIFIC_ADDRESS_1_TOP_47_32,
         (lc->mac_addr[0] << 8) |
         lc->mac_addr[1]);

  cfg_wr(lc->shim_port, lc->mac_pa,
         MPIPE_GBE_SPECIFIC_ADDRESS_1_BOTTOM_31_0,
         (lc->mac_addr[2] << 24) |
         (lc->mac_addr[3] << 16) |
         (lc->mac_addr[4] << 8) |
         lc->mac_addr[5]);

  //
  // GEM network config.  Note that the gige_mode, speed, and full-duplex
  // bits will get set later by the link management code, since they depend
  // upon the speed negotiated by the PHY.  However, we start them out at
  // 1 G/FDX in case an app starts sending packets before the link comes
  // up; if we're going to throw away output packets, we might as well do
  // it as quickly as possible.
  //
  MPIPE_GBE_NETWORK_CONFIGURATION_t config =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa,
                    MPIPE_GBE_NETWORK_CONFIGURATION)
  };

  config.sgmii_mode = 1;          // We only do SGMII
  config.copy_all = 1;            // Copy all frames
  config.rcv_1536 = 1;            // Accept 1536-byte frames
  config.fcs_remove = 1;          // Remove FCS
  config.dis_pause_cpy = 1;       // Don't copy pause frames
  config.jumbo_ena = lc->jumbo;   // Accept jumbo frames
  config.pcs_sel = 1;             // PCS must be enabled
  config.gige_mode = 1;           // Run at 1000 Mbps
  config.speed = 0;               // Run at 1000 Mbps
  config.full_duplex = 1;         // Run in full-duplex mode

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONFIGURATION,
         config.word);

  //
  // Configure autonegotation.  Seems like we ought to be able to change
  // our minds about this later, but we currently can't link up to a
  // BCM8747 in forced 1 GbE mode unless we set this properly up front.
  //
  MPIPE_GBE_PCS_CTL_t pcs_ctl =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_CTL)
  };
  pcs_ctl.auto_neg = !lc->no_auto_neg;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_CTL, pcs_ctl.word);

  //
  // Configure pause frame generation.
  //
  enet_set_pause(lc, 0, lc->pause_out);
  enet_set_pause(lc, 1, lc->pause_in);

  // GEM network control.
  MPIPE_GBE_NETWORK_CONTROL_t ctrl =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONTROL)
  };

  ctrl.tx_ena = 1;
  ctrl.rx_ena = 1;

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONTROL, ctrl.word);

  //
  // Set priority queue for pause frames.
  //
  MPIPE_GBE_MAC_INTFC_CTL_t intf_ctl =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_CTL)
  };

  intf_ctl.tx_prq_ena = 1 << lc->chan;
  intf_ctl.prq_ovd = 1;
  intf_ctl.prq_ovd_val = lc->chan;

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_CTL,
         intf_ctl.word);

  return 1;
}


int
enet_config_mac(enet_link_config_t* lc)
{
  //
  // No MAC to configure for loopback channels.
  //
  if (lc->loop)
    return 1;

  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);
  int retval = (lc->gbe) ? gbe_config_mac(lc) : xgbe_config_mac(lc);
  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);
  return retval;
}


void
enet_probe_init_link(enet_link_config_t* lc, int do_intr)
{
  uint32_t (*mdio_rd)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

  //
  // Loopback channels are a special case; we have no MAC or MDIO so we
  // don't want to do much of anything here.
  //
  if (lc->loop)
  {
    const enet_link_ops_t* ops = &enet_loop_link_ops;
    if (ops->probe(lc) && ops->init(lc) == 0)
      lc->ops = ops;
    else
      lc->ops = &enet_null_link_ops;
    return;
  }

  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

  //
  // Initialize whatever MAC we're using for MDIO.  Initially, all ports
  // are enabled for management, and that's bad, so if that's the case
  // we clear all of the ports before we add the one we'll be using.
  //
  MPIPE_MAC_MANAGE_t mmm =
  {
     .word = cfg_rd(lc->shim_port, lc->shim_pa, MPIPE_MAC_MANAGE)
  };

  if (__builtin_popcount(mmm.ena) > 2)
    mmm.ena = 0;

  mmm.ena |= (1L << lc->mdio_mac_num);

  cfg_wr(lc->shim_port, lc->shim_pa, MPIPE_MAC_MANAGE, mmm.word);

  if (lc->xgbe_mdio)
  {
    //
    // Enable the interrupt on MDIO frame complete; we don't actually want
    // to get an interrupt, and we won't (since it's still masked in the
    // mask register), but this is required for the PHY frame complete bit
    // in the status register to work.
    //
    cfg_wr(lc->shim_port, lc->mdio_mac_pa, MPIPE_XAUI_DIAG_INTERRUPT_ENABLE,
           MPIPE_XAUI_DIAG_INTERRUPT_STATUS__MDIO_COMPLETE_MASK);

    //
    // Set the MDIO clock divisor.
    //
    MPIPE_XAUI_TRANSMIT_CONFIGURATION_t tx_config =
    {
      .word = cfg_rd(lc->shim_port, lc->mdio_mac_pa,
                     MPIPE_XAUI_TRANSMIT_CONFIGURATION)
    };

    tx_config.cfg_speed = 4;

    cfg_wr(lc->shim_port, lc->mdio_mac_pa, MPIPE_XAUI_TRANSMIT_CONFIGURATION,
           tx_config.word);

    mdio_rd = xgbe_mdio_cl45_rd;
  }
  else
  {
    MPIPE_GBE_NETWORK_CONFIGURATION_t config =
    {
       .word = cfg_rd(lc->shim_port, lc->mdio_mac_pa,
                      MPIPE_GBE_NETWORK_CONFIGURATION)
    };

    config.div = 4;

    cfg_wr(lc->shim_port, lc->mdio_mac_pa, MPIPE_GBE_NETWORK_CONFIGURATION,
           config.word);

    MPIPE_GBE_NETWORK_CONTROL_t ctrl =
    {
       .word = cfg_rd(lc->shim_port, lc->mdio_mac_pa,
                      MPIPE_GBE_NETWORK_CONTROL)
    };

    ctrl.mdio_ena = 1;

    cfg_wr(lc->shim_port, lc->mdio_mac_pa, MPIPE_GBE_NETWORK_CONTROL,
           ctrl.word);

    mdio_rd = gbe_mdio_rd;
  }

  //
  // Go get the PHY type, if we have one.
  //
  if (lc->has_phy)
  {
    uint32_t phy_hi = mdio_rd(lc->shim_port,
                              lc->mdio_mac_pa, lc->phyaddr, 1, 2);
    uint32_t phy_lo = mdio_rd(lc->shim_port,
                              lc->mdio_mac_pa, lc->phyaddr, 1, 3);
    lc->phytype = (phy_hi << 16) | phy_lo;

#ifdef LINK_DEBUG
    tprintf("%s: mdio addr %d PHY type is %#x\n", lc->device_name,
            lc->phyaddr, lc->phytype);
#endif
  }
  else
  {
    lc->phytype = 0;
#ifdef LINK_DEBUG
    tprintf("%s: no PHY\n", lc->device_name);
#endif
  }

  //
  // Search the link plugin table to find the correct plugin for this
  // link.
  //
  const enet_link_ops_t** ops;
  for (ops = enet_link_plugins; *ops; ops++)
  {
    if ((*ops)->probe(lc))
    {
      //
      // If we can support interrupts, then turn them on.
      //
      if (do_intr)
      {
#ifdef LINK_DEBUG
        tprintf("%s: enabling interrupts\n", lc->device_name);
#endif
        lc->link_does_intr = 1;
      }

      //
      // Initialize the plugin.
      //
      if ((*ops)->init(lc) == 0)
        lc->ops = *ops;

      break;
    }
  }

  if (lc->ops == NULL)
    lc->ops = &enet_null_link_ops;

  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

  //
  // Properly set the discard-if-down state.  Note that this path takes the
  // MDIO spinlock, so we have to do it when we don't hold that lock.
  //
  enet_set_discard(lc, lc->discard_if_down);
}


void
enet_new_link_state(enet_link_config_t* lc, uint32_t new_state)
{
  uint32_t old_state = lc->current_state;
  lc->current_state = new_state;

  //
  // enet_new_link_state_hook handles some state change activity which is
  // only done in the hypervisor.  We use a weak symbol so that we don't
  // try to call the routine under the BME, where it doesn't exist.  Note
  // that we need to update lc->current_state before we call the hook,
  // since the hook might kick off activity, like an interrupt routine on
  // another tile, that queries the state.
  //
#pragma weak enet_new_link_state_hook
  if (&enet_new_link_state_hook)
    enet_new_link_state_hook(lc, old_state, new_state);

  //
  // Handle discard for xgbe.
  //
  if (!lc->gbe)
  {
    if (lc->current_state & ENET_LINK_SPEED)
    {
      if (lc->discarding)
      {
        uint_reg_t tc = cfg_rd(lc->shim_port, lc->mac_pa,
                               MPIPE_XAUI_MAC_INTFC_TX_CTL);
        cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_TX_CTL,
               tc & ~MPIPE_XAUI_MAC_INTFC_TX_CTL__TX_DROP_MASK);
        lc->discarding = 0;
#ifdef LINK_DEBUG
        tprintf("%s: disabled packet discard\n", lc->device_name);
#endif
      }
    }
    else
    {
      if (lc->discard_if_down && !lc->discarding)
      {
        uint_reg_t tc = cfg_rd(lc->shim_port, lc->mac_pa,
                               MPIPE_XAUI_MAC_INTFC_TX_CTL);
        cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_MAC_INTFC_TX_CTL,
               tc | MPIPE_XAUI_MAC_INTFC_TX_CTL__TX_DROP_MASK);
        lc->discarding = 1;
#ifdef LINK_DEBUG
        tprintf("%s: enabled packet discard\n", lc->device_name);
#endif
      }
    }
  }
}


int
enet_mdio_cl22_rd(enet_link_config_t* lc, int phy, int reg, uint32_t* valuep)
{
  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

  if (lc->xgbe_mdio)
    *valuep = xgbe_mdio_cl22_rd(lc->shim_port, lc->mdio_mac_pa, phy, 0, reg);
  else
    *valuep = gbe_mdio_rd(lc->shim_port, lc->mdio_mac_pa, phy, 0, reg);

  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

  return 0;
}


int
enet_mdio_cl22_wr(enet_link_config_t* lc, int phy, int reg, uint32_t value)
{
  hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

  if (lc->xgbe_mdio)
    xgbe_mdio_cl22_wr(lc->shim_port, lc->mdio_mac_pa, phy, 0, reg, value);
  else
    gbe_mdio_wr(lc->shim_port, lc->mdio_mac_pa, phy, 0, reg, value);

  hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

  return 0;
}


int
enet_mdio_cl45_rd(enet_link_config_t* lc, int phy, int dev, int reg,
		  uint32_t* valuep)
{
  if (lc->xgbe_mdio)
  {
    hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

    *valuep = xgbe_mdio_cl45_rd(lc->shim_port, lc->mdio_mac_pa, phy, dev, reg);

    hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

    return 0;
  }
  else
  {
    // Clause 45 not supported on GbE
    return ENET_NOT_IMPLEMENTED;
  }
}


int
enet_mdio_cl45_wr(enet_link_config_t* lc, int phy, int dev, int reg,
		  uint32_t value)
{
  if (lc->xgbe_mdio)
  {
    hvbme_spin_lock(HVBME_SPINLOCK_MDIO);

    xgbe_mdio_cl45_wr(lc->shim_port, lc->mdio_mac_pa, phy, dev, reg, value);

    hvbme_spin_unlock(HVBME_SPINLOCK_MDIO);

    return 0;
  }
  else
  {
    // Clause 45 not supported on GbE
    return ENET_NOT_IMPLEMENTED;
  }
}


int
enet_get_module_eeprom(enet_link_config_t* lc, int* type, int offset,
                       void* buf, int len)
{
  if (lc->ops->get_module_eeprom)
    return lc->ops->get_module_eeprom(lc, type, offset, buf, len);

  if (type)
    *type = ENET_MODULE_NONE;
  return 0;
}
