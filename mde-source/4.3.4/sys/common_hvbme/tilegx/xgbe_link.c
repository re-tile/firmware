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
 * Link-specific plugins for the hypervisor and BME Ethernet drivers.
 */

#include <stdio.h>
#include <util.h>

#include <arch/mpipe.h>
#include <arch/mpipe_constants.h>
#include <arch/mpipe_gbe.h>
#include <arch/mpipe_xaui.h>
#include <arch/serdes.h>
#include <arch/sim.h>

#include "board_info.h"
#include "cfg.h"
#include "enet.h"
#include "enet_specific.h"
#include "i2c_acc.h"
#include "sig.h"


//
// Generic utilities.
//

extern void drv_udelay(uint32_t usec);
extern uint64_t drv_timer_start(uint32_t usec);
extern int drv_timer_done(uint64_t timer);

/** Read a SERDES register.
 * @param lc Driver link configuration.
 * @param lane Lane number within the quad.
 * @param addr Register address.
 */
static int
serdes_rd(enet_link_config_t* lc, int lane, int addr)
{
  MPIPE_XAUI_SERDES_CONFIG_t cfg =
  {{
    .reg_addr = addr,
    .lane_sel = 1 << lane,
    .read = 1,
    .send = 1,
  }};

  cfg_wr(lc->shim_port, lc->serdes_mac_pa, MPIPE_XAUI_SERDES_CONFIG, cfg.word);

  while (cfg.send)
    cfg.word = cfg_rd(lc->shim_port, lc->serdes_mac_pa,
                      MPIPE_XAUI_SERDES_CONFIG);

  return cfg.reg_data;
}


/** Write a SERDES register.
 * @param lc Driver link configuration.
 * @param lane Lane number within the quad.
 * @param addr Register address.
 * @param val Register value.
 */
static void
serdes_wr(enet_link_config_t* lc, int lane, int addr, int val)
{
  MPIPE_XAUI_SERDES_CONFIG_t cfg =
  {{
    .reg_data = val,
    .reg_addr = addr,
    .lane_sel = 1 << lane,
    .read = 0,
    .send = 1,
  }};

  cfg_wr(lc->shim_port, lc->serdes_mac_pa, MPIPE_XAUI_SERDES_CONFIG, cfg.word);

  while (cfg.send)
    cfg.word = cfg_rd(lc->shim_port, lc->serdes_mac_pa,
                      MPIPE_XAUI_SERDES_CONFIG);
}


/** Enable or disable SERDES loopback.
 * @param lc Driver link configuration.
 * @param enable If nonzero, enable loopback, else disable it.
 */
static void
loopback_config(enet_link_config_t* lc, int enable)
{
  uint32_t lanes = lc->serdes_lanes;

#ifdef LINK_DEBUG
  tprintf("%s %s MAC loopback\n", lc->device_name,
          (enable) ? "enabling" : "disabling");
#endif

  while (lanes)
  {
    int lane = __builtin_ctz(lanes);
    lanes &= lanes - 1;

    int reg = serdes_rd(lc, lane, SERDES_PRBS_CTRL);
    if (enable)
      reg |= SERDES_PRBS_CTRL__LPBK_EN_MASK;
    else
      reg &= ~SERDES_PRBS_CTRL__LPBK_EN_MASK;

    serdes_wr(lc, lane, SERDES_PRBS_CTRL, reg);
  }
}


/** Configure a XAUI quad for 12 GbE, using a 125 MHz reference clock.
 * @param lc Driver link configuration.
 */
static void
twelve_gbe_config(enet_link_config_t* lc)
{
  uint32_t lanes = lc->serdes_lanes;

#ifdef LINK_DEBUG
  tprintf("%s enabling 12 Gbps mode\n", lc->device_name);
#endif

  while (lanes)
  {
    int lane = __builtin_ctz(lanes);
    lanes &= lanes - 1;

    //
    // The PLL parameters used below are calculated to produce 3.75 Gbps
    // from a 125 MHz reference clock.
    //
    SERDES_PLL_F_SET_t setf =
    {
      .word = serdes_rd(lc, lane, SERDES_PLL_F_SET),
    };
    setf.f = 2;

    SERDES_PLL_M_N_SET_t setmn =
    {
      .word = serdes_rd(lc, lane, SERDES_PLL_M_N_SET),
    };
    setmn.m = 1;
    setmn.n = 9;

    serdes_wr(lc, lane, SERDES_PLL_F_SET, setf.word);
    serdes_wr(lc, lane, SERDES_PLL_M_N_SET, setmn.word);
    serdes_wr(lc, lane, SERDES_UPD_SET_CMD, 0xff);
  }
}


/** Disable a MAC.
 * @param lc Driver link configuration.
 */
static void
mac_disable(enet_link_config_t* lc)
{
  if (!lc->gbe)
  {
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_RECEIVE_CONTROL, 0);
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_TRANSMIT_CONTROL, 0);

    //
    // This delay after disabling receive/transmit and before disabling the
    // MAC is necessary to avoid hardware erratum 11424.
    //
    __insn_mf();
    drv_udelay(10);
  }

  MPIPE_MAC_ENABLE_t mme =
  {
     .word = cfg_rd(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE)
  };

  mme.ena &= ~(1L << lc->mac_num);
  cfg_wr(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE, mme.word);
}


/** Enable a MAC.
 * @param lc Driver link configuration.
 */
static void
mac_enable(enet_link_config_t* lc)
{
  MPIPE_MAC_ENABLE_t mme =
  {
     .word = cfg_rd(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE)
  };

  mme.ena |= (1L << lc->mac_num);
  cfg_wr(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE, mme.word);

  if (!lc->gbe)
  {
    //
    // If we have a 125 MHz refclk, we're going to be running at 12 Gbps.
    // That means we need to reprogram the SERDES PLL.  We can't do this
    // with the MAC enabled, and if we had done above before the MAC was
    // enabled for the first time, our values would have been overwritten
    // by the hardware.  Thus, we need to take the MAC down, reprogram the
    // PLL, and bring it back up.  Theoretically you could avoid this on
    // subsequent enables, as long as you weren't enabling GbE MACs on the
    // same SERDES lanes in between, but it doesn't really seem worth the
    // trouble to keep track of that.
    //
    if (lc->xaui_refclk_125)
    {
      //
      // Wait until the PLL is up.  If we disable the MAC before this
      // happens, it'll go through its initialization again when we
      // reenable it, wiping out the register changes we're about to do.
      //
      int tries = 0;
      while (!(cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_STS) &
               MPIPE_XAUI_PCS_STS__CLOCK_READY_MASK))
      {
        tries++;
        if (tries > 100000)
          panic("%s: clock did not spin up\n", lc->device_name);
      }

      //
      // Disable the MAC.
      //
      mme.ena &= ~(1L << lc->mac_num);
      cfg_wr(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE, mme.word);

      //
      // We need to wait 64 refclks here, to be sure that things are really
      // disabled; 1 us is a bit too long but easier to do.
      //
      drv_udelay(1);

      //
      // Now we configure the SERDES for 12 Gbps.
      //
      twelve_gbe_config(lc);

      //
      // Finally, reenable the MAC.  The SERDES config just sets some
      // values that take effect when the link is spun back up so we don't
      // need a delay here.
      //
      mme.ena |= (1L << lc->mac_num);
      cfg_wr(lc->shim_port, lc->shim_pa, MPIPE_MAC_ENABLE, mme.word);
    }

    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_RECEIVE_CONTROL, 1);
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_TRANSMIT_CONTROL, 1);
  }
}


/** Common code to handle the get_module_eeprom() entry point.  The caller
 *  supplies a function that knows how to read from the I2C bus that goes to
 *  the relevant SFP/SFP+ module, and this function handles the rest.
 * @param lc Pointer to the link configuration structure for the link.
 * @param type Pointer to returned module type (ENET_MODULE_xxx); may be
 *  NULL in which case no type is returned.
 * @param offset Offset within the EEPROM at which to start the transfer.
 * @param buf Buffer to hold the transferred bytes.
 * @param len Number of bytes to transfer.
 * @param rd_func Function called to do reads from the module I2C bus.
 * @return The number of bytes that were available to be transferred at the
 *  given offset, or a negative error code.  Note that this number of bytes
 *  may be more, or less, than the number of bytes that were specified in
 *  len.
 */
static int
get_module_eeprom(enet_link_config_t* lc, int* type,
                  int offset, void* buf, int len,
                  int (*rd_func)(enet_link_config_t* lc, int i2c_addr,
                                 int offset, int len, void* buf))
{
  //
  // Bytes 92 and 94 of the EEPROM are used to figure out if it supports
  // SFF-8472 or not.  We could potentially go to a lot of trouble to get
  // optimal performance in the case where the user wants the type value,
  // and has also requested that we read that region of the EEPROM (i.e.,
  // don't read those bytes twice).  But we know that the major consumer
  // of this interface never does that, so we don't bother.
  //
  if (type)
  {
    uint8_t sfp_92_94[3];
    if (rd_func(lc, 0xA0, 92, 3, sfp_92_94) != 3)
    {
      *type = ENET_MODULE_NONE;
      return ENET_EIO;
    }

    //
    // If byte 94 says no 8472 compliance, or if byte 92 says you have to
    // go through the weird address-swapping maneuver to read 0xA2, we say
    // this is a 8079-only module.  You could be really paranoid and check
    // the DOM support bit in byte 92 as well, but other drivers don't.
    //
    if (sfp_92_94[2] == 0 || (sfp_92_94[0] & 0x4))
      *type = ENET_MODULE_8079;
    else
      *type = ENET_MODULE_8472;
  }

  //
  // You could argue that if we know the module type, and we aren't an 8472
  // module, we shouldn't be trying to read the A2 bytes.  But again, since
  // we know that our main consumer does the right thing here, we don't
  // bother; it shouldn't hurt anything if a caller screws up.
  //
  int a0_len = max(min(len, 256 - offset), 0);

  if (a0_len)
  {
    if (rd_func(lc, 0xA0, offset, a0_len, buf) != a0_len)
      return ENET_EIO;
  }

  int a2_len = min(len - a0_len, offset + len - 256);

  if (a2_len > 0)
  {
    int a2_offset = offset + a0_len;

    if (rd_func(lc, 0xA2, a2_offset - 256, a2_len, buf + a0_len) != a2_len)
      return (a0_len) > 0 ? a0_len : ENET_EIO;
  }

  return len;
}



//
// 10/20 GbE plugins.
//

//
// Helper routines
//

/** Read an MDIO register from an XGbE interface's PHY.
 * @param lc Driver link configuration.
 * @param devaddr Device within the PHY.
 * @param reg Register number.
 * @return Value read from PHY.
 */
static uint32_t
xgbe_phy_rd(enet_link_config_t* lc, uint32_t devaddr, uint32_t reg)
{
  return xgbe_mdio_cl45_rd(lc->shim_port, lc->mdio_mac_pa, lc->phyaddr,
                           devaddr, reg);
}


/** Write an MDIO register on an XGbE interface's PHY.
 * @param lc Driver link configuration.
 * @param devaddr Device within the PHY.
 * @param reg Register number.
 * @param data Data to write.
 */
static void
xgbe_phy_wr(enet_link_config_t* lc, uint32_t devaddr, uint32_t reg,
            uint32_t data)
{
  xgbe_mdio_cl45_wr(lc->shim_port, lc->mdio_mac_pa,
                    lc->phyaddr, devaddr, reg, data);
}

//
// QT2025 routines
//

/** Check to see whether the firmware seems to be running on an AMCC
 *  QT2025 PHY.
 * @param lc Driver link configuration.
 * @return Nonzero if firmware is running, zero if it is not.
 */
static int
qt2025_heartbeat_ok(enet_link_config_t* lc)
{
  uint32_t heartbeat = xgbe_phy_rd(lc, 3, 0xd7ee);

  drv_udelay(1000);

  return (heartbeat != xgbe_phy_rd(lc, 3, 0xd7ee));
}


/** Read a byte from the boot EEPROM on an AMCC QT2025 PHY.
 * @param lc Driver link configuration.
 * @param addr Address within the EEPROM.
 */
static uint8_t
qt2025_read_boot_eeprom(enet_link_config_t* lc, uint32_t addr)
{
  // Wait until no commands are in progress.
  while ((xgbe_phy_rd(lc, 1, 0x8000) & 0xc) == 0x8)
    ;

  // Upper bits for 2-byte addressing.
  xgbe_phy_wr(lc, 1, 0xc026, (addr >> 8) & 0x1f);

  // Write the read command.
  xgbe_phy_wr(lc, 1,  0x8000, ((addr & 0xff) << 8) | 0x2);

  // Wait for the command to succeed or fail.
  while ((xgbe_phy_rd(lc, 1, 0x8000) & 0xc) == 0x8)
    ;

  // Return the read data.
  return xgbe_phy_rd(lc, 1, 0x8007 + (addr & 0xff));
}

/** Build the value which we use to identify a particular AMCC firmware
 *  version.  Note that the four values are not each in their own byte;
 *  the first two share a byte.  This is weird but matches what AMCC
 *  does in their own version of the firmware ID register.
 */
#define AMCC_FW_VER(a, b, c, d) \
  (((a) << 20) | ((b) << 16) | ((c) << 8) | ((d) << 0))

/** Boot an AMCC QT2025 PHY from an attached I2C EEPROM.
 * @param lc Driver link configuration.
 * @return Zero if the PHY was booted successfully, nonzero otherwise.
 */
static int
qt2025_boot_eeprom(enet_link_config_t* lc)
{
  //
  // If we're already booted, we don't want to do it again.
  //
  if (qt2025_heartbeat_ok(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s: qt2025 already booted\n", lc->device_name);
#endif
    return 0;
  }
#ifdef LINK_DEBUG
  else
    tprintf("%s: qt2025 not already booted, booting it\n", lc->device_name);
#endif

  uint32_t fw_ver = 0xffffffff;

  if (!lc->phy_auto_cfg)
  {
    fw_ver = qt2025_read_boot_eeprom(lc, 0)         |
             (qt2025_read_boot_eeprom(lc, 1) << 8)  |
             (qt2025_read_boot_eeprom(lc, 2) << 16) |
             (qt2025_read_boot_eeprom(lc, 3) << 24);

    // Reset the PHY.
    xgbe_phy_wr(lc, 1, 0x0000, 0x8000);

    // Disable transmit (we'll enable this again later).
    xgbe_phy_wr(lc, 1, 0x0009, 1);

    // Hold the PHY's microcontroller in reset.
    xgbe_phy_wr(lc, 1, 0xc300, 0x0000);

    // SFP+ setup: LAN mode, VCXO control loop enable, module application.
    xgbe_phy_wr(lc, 1, 0xc301, 0x0805);

    // Set the microcontroller clock speed.
    xgbe_phy_wr(lc, 1, 0xc302, 0x0004);

    // 7-bit I2C address of the EEPROM containing the firmware.
    xgbe_phy_wr(lc, 1, 0xc318, 0x53);

    switch (fw_ver)
    {
    case AMCC_FW_VER(2, 0, 2, 4):
    case AMCC_FW_VER(2, 0, 2, 5):
#ifdef LINK_DEBUG
      tprintf("%s: qt2025 version 2.0.2.X\n", lc->device_name);
#endif
      // Enable user control of TXOUT inversion, set for SR module.
      xgbe_phy_wr(lc, 1, 0xc319, 0x0048);

      // Set for SFP+ module.
      xgbe_phy_wr(lc, 1, 0xc31a, 0x0098);

      // Disable firmware freezepoints.
      xgbe_phy_wr(lc, 3, 0x0026, 0x0e00);

      // Enable DOM_MODE, hardware control of LEDs; 1" FR4 loss.
      xgbe_phy_wr(lc, 3, 0x0027, 0x0093);

      break;

    default:
#ifdef LINK_DEBUG
      tprintf("%s: qt2025 unknown version %#x\n", lc->device_name, fw_ver);
#endif
      // Pass parameters to the loaded firmware.  Most of these bits set up for
      // SFP+ behavior.
      xgbe_phy_wr(lc, 1, 0xc308, 0x0800);
      xgbe_phy_wr(lc, 1, 0xc319, 0x0008);
      xgbe_phy_wr(lc, 1, 0xc31a, 0x0098);

      // Disable firmware freezepoints.
      xgbe_phy_wr(lc, 3, 0x0026, 0x0e00);

      // Enable hardware control of LEDs.
      xgbe_phy_wr(lc, 3, 0x0027, 0x0001);

      break;
    }

    // Firmware configuration key.
    xgbe_phy_wr(lc, 3, 0x0028, 0xa528);

// XXX FIXME use BIB data for this

    // Top (LED 1): TX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0003);

    // Bottom (LED 2): RX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd007, 0x000b);

#ifdef QT2025_NO_SPLIT_LEDS
    // Top (LED 1): off.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0004);

    // Bottom (LED 2): off.
    xgbe_phy_wr(lc, 1, 0xd007, 0x0004);
#else
    // Top (LED 1): TX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0003);

    // Bottom (LED 2): RX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd007, 0x000b);
#endif

    // Interior (LED 3): off.
    xgbe_phy_wr(lc, 1, 0xd008, 0x0004);

    // Release reset, upload from eeprom.
    xgbe_phy_wr(lc, 1, 0xc300, 0x0002);
  }

  //
  // Wait for the microcontroller heartbeat.  Empirically this takes about 300
  // retries, which is more or less in line with what the data sheet suggests.
  //
  uint32_t hb_counter = 0;
  while (!qt2025_heartbeat_ok(lc))
  {
    hb_counter++;
    drv_udelay(1000);
    if (hb_counter > 1000)
    {
      tprintf("hv_warning: %s: PHY init failed (no heartbeat)\n",
              lc->device_name);
      return 1;
    }
  }

  //
  // Verify the firmware checksum.  According to the data sheet we shouldn't
  // need to loop on this, but it seems to help.  Empirically we need to
  // check about a dozen times before we get a valid status.
  //
  uint32_t ck_counter = 0;
  while (1)
  {
    uint16_t cksum_status = (xgbe_phy_rd(lc, 3, 0xd716) << 8) |
                            xgbe_phy_rd(lc, 3, 0xd717);

    // This magic value means "checksum OK".
    if (cksum_status == 0xf628)
      break;

    // This magic value means "checksum bad".
    if (cksum_status == 0xdead)
    {
      tprintf("hv_warning: %s: PHY init failed (bad firmware checksum)\n",
              lc->device_name);

      return 1;
    }

    // Anything else, we retry.
    if (ck_counter++ > 1000)
    {
      tprintf("hv_warning: %s: PHY init failed (checksum timeout)\n",
              lc->device_name);

      return 1;
    }

    drv_udelay(1000);
  }

  //
  // Wait until the firmware says it's ready.  This can supposedly take up
  // to 2 seconds.  Empirically it's about 7 times through this loop.
  //
  uint32_t rdy_counter = 0;
  while (1)
  {
    uint32_t ucstatus = xgbe_phy_rd(lc, 3, 0xd7fd);

    if (ucstatus != 0 && ucstatus != 0x10)
      break;

    if (rdy_counter++ > 1000)
    {
      tprintf("hv_warning: %s: PHY init failed (firmware not ready)\n",
              lc->device_name);

      return 1;
    }

    drv_udelay(100 * 1000);
  }

  //
  // Handle any remaining setup.
  //

  //
  // Float TXENABLE.
  //
  xgbe_phy_wr(lc, 1, 0xc313, xgbe_phy_rd(lc, 1, 0xc313) | 0x1000);

  switch (fw_ver)
  {
  case AMCC_FW_VER(2, 0, 2, 4):
  case AMCC_FW_VER(2, 0, 2, 5):
    //
    // Don't know what these do, but the 2.0.2.4 release notes doc says
    // they're required.
    //
    xgbe_phy_wr(lc, 1, 0xf058, 0x0020);
    xgbe_phy_wr(lc, 1, 0xf055, 0x0002);
    xgbe_phy_wr(lc, 1, 0xf055, 0x0000);

    /* FALLTHROUGH */

  case 0xffffffff:  // Unknown version (auto-configured)
  {
    //
    // Set the TXOUT polarity, if we have a BIB entry telling us what it
    // should be.  Otherwise, we just leave it alone.
    //
    uint32_t rd_data = xgbe_phy_rd(lc, 1, 0xc301);

    if (lc->sfp_txout_inv)
      rd_data |= 1 << 7;
    else
      rd_data &= ~(1 << 7);

    xgbe_phy_wr(lc, 1, 0xc301, rd_data);

    break;
  }

  default:
    break;
  }

  return 0;
}


//
// Broadcom BCM8747/8727/84727, common routines.
//

/** Is this a Broadcom 8747? */
#define IS_BCM8747(lc)  ((lc)->phytype == 0x00206037)
/** Is this a Broadcom 8727? */
#define IS_BCM8727(lc)  ((lc)->phytype == 0x00206036)
/** Is this a Broadcom 84727? */
#define IS_BCM84727(lc) ((lc)->phytype == 0x600d8718)

/** Init routine. */
static int
bcm87x7_phy_init(enet_link_config_t* lc)
{
  //
  // XXX We don't have the right sequence yet for the 84727, so don't do
  // any SROM configuration there.
  //
  if (!IS_BCM84727(lc))
  {
    //
    // Set various registers to disable the SROM interface, so that this
    // port doesn't interfere with that if another port gets loaded first.
    //
    xgbe_phy_wr(lc, 1, 0x0000, 0x8000);
    xgbe_phy_wr(lc, 1, 0xCA10, 0x018F);
    xgbe_phy_wr(lc, 1, 0xC848, 0xC0F1);
    xgbe_phy_wr(lc, 1, 0xC843, 0x000F);
    xgbe_phy_wr(lc, 1, 0xC840, 0x000C);
  }

  return 0;
}


/** Set LED state. */
static void
bcm87x7_set_led_mode(enet_link_config_t* lc)
{
  //
  // Use BIB data to configure the LEDs as appropriate.  The PHY only
  // supports two useful settings, which we check for here explicitly.
  //
  int data = xgbe_phy_rd(lc, 1, 0xc808);
  data &= 0xFF8F;

  if (lc->leds[0] == BI_ENET_LED_CFG__VAL_LINK &&
      lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT)
  {
    data |= (lc->gbe) ? 0x0040 : 0x0060;  // Set [6:4] to binary 110
  }
  else if (lc->leds[0] == BI_ENET_LED_CFG__VAL_ACT_RX &&
      lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT_TX)
  {
    data |= (lc->gbe) ? 0x0040 : 0x0020;  // Set [6:4] to binary 010
  }
  else
  {
    //
    // For any other settings, we set the LEDs to fixed values, off unless
    // the BIB says "always on".  Leaving [6:4] of c808 0 makes the LED pins
    // GPIO pins; we just have to enable them as outputs and set their
    // values.
    //
    int gpio = xgbe_phy_rd(lc, 1, 0xc80e);
    if (IS_BCM84727(lc))
      gpio &= 0xFFCC;
    else
      gpio &= 0xFFEC;
    if (lc->leds[0] != BI_ENET_LED_CFG__VAL_ON)
      gpio |= 1;
    if (lc->leds[1] != BI_ENET_LED_CFG__VAL_ON)
      gpio |= 2;

    xgbe_phy_wr(lc, 1, 0xc80e, gpio);
  }

  xgbe_phy_wr(lc, 1, 0xc808, data);
}

#if 0

/** Dump out an SFP/SFP+ module's EEPROM.  Note: this doesn't work until
 *  the microcode is loaded. */
static void
bcm87x7_dump_sfp_eeprom(enet_link_config_t* lc)
{
  //
  // We might eventually want to use code like this to sense the type of
  // module plugged in and do something useful with that data (like fail
  // link bringup if the right type of module is not installed), although
  // the fact that the user can change it at any time makes that a bit
  // tricky.
  //

  //
  // Number of bytes to read from module.
  //
  const int length = 96;

  //
  // Target location in the PHY's RAM for the read data.
  //
  const int base = 0x8200 - length;

  //
  // Set destination address in PHY's RAM and source address in EEPROM.
  //
  xgbe_phy_wr(lc, 1, 0x8004, base);
  xgbe_phy_wr(lc, 1, 0x8003, 0);

  //
  // Set read length.
  //
  int data = xgbe_phy_rd(lc, 1, 0x8002);
  data &= 0xc000;
  data |= length;
  xgbe_phy_wr(lc, 1, 0x8002, data);

  //
  // Set device I2C Address.
  //
  data = xgbe_phy_rd(lc, 1, 0x8005);
  data &= 0x1ff;
  data |= 0xa000;
  xgbe_phy_wr(lc, 1, 0x8005, data);

  //
  // Wait for device to be idle.
  //
  do
  {
    data = xgbe_phy_rd(lc, 1, 0x8000);
  }
  while ((data & 0xc) != 0);

  //
  // Kick off read.
  //
  data = xgbe_phy_rd(lc, 1, 0x8000);
  data &= 0xFFDC;
  data |= 0x0002;
  xgbe_phy_wr(lc, 1, 0x8000, data);

  //
  // Wait for read to finish.
  //
  do
  {
    data = xgbe_phy_rd(lc, 1, 0x8000);
  }
  while ((data & 0xc) == 8);

  //
  // If it worked, dump out the data.
  //
  if ((data & 0xc) == 0x4)
  {
    tprintf("%s: SFP module data dump:\n", lc->device_name);

    for (int i = 0; i < length / 8; i++)
    {
      char str[9] = { 0 };

      tprintf("  %2d:", i * 8);
      for (int j = 0; j < 8; j++)
      {
        data = xgbe_phy_rd(lc, 1, base + i * 8 + j) & 0xFF;
        printf(" %02x", data);
        if (data >= ' ' && data <= '~')
          str[j] = data;
        else
          str[j] = '.';
      }
      printf("  %s\n", str);
    }
  }
  else
    tprintf("%s: SFP module read failed, code 0x%x\n", lc->device_name, data);
}


/** Dump out the registers of a copper SFP module's PHY.  Note: this
 *  doesn't work until the microcode is loaded. */
static void
bcm87x7_dump_sfp_phy(enet_link_config_t* lc)
{
  //
  // Number of registers to read.
  //
  const int nreg = 32;

  //
  // Number of bytes to read from module on each pass.
  //
  const int length = 2;

  //
  // Target location in the PHY's RAM for the read data.
  //
  const int base = 0x8200 - length;

  //
  // Deassert TX_DISABLE, which takes the PHY out of reset, then give the
  // PHY a chance to get itself together.
  //
  xgbe_phy_wr(lc, 1, 0x0009, 0);
  drv_udelay(200 * 1000);

  //
  // Set device I2C Address.
  //
  int data = xgbe_phy_rd(lc, 1, 0x8005);
  data &= 0x1ff;
  data |= 0xac00;
  xgbe_phy_wr(lc, 1, 0x8005, data);

  tprintf("%s: SFP module PHY register dump:\n", lc->device_name);

  for (int reg = 0; reg < nreg; reg++)
  {
    //
    // Wait for device to be idle.
    //
    do
    {
      data = xgbe_phy_rd(lc, 1, 0x8000);
    }
    while ((data & 0xc) != 0);

    //
    // Set read length.
    //
    data = xgbe_phy_rd(lc, 1, 0x8002);
    data &= 0xc000;
    data |= length;
    xgbe_phy_wr(lc, 1, 0x8002, data);

    //
    // Set source address in SFP's PHY registers.
    //
    xgbe_phy_wr(lc, 1, 0x8003, reg);

    //
    // Set destination address in PHY's RAM.
    //
    xgbe_phy_wr(lc, 1, 0x8004, base);

    //
    // Kick off read.
    //
    data = xgbe_phy_rd(lc, 1, 0x8000);
    data &= 0xFFDC;
    data |= 0x0001;
    xgbe_phy_wr(lc, 1, 0x8000, data);

    //
    // Wait for read to finish.
    //
    do
    {
      data = xgbe_phy_rd(lc, 1, 0x8000);
    }
    while ((data & 0xc) == 8);

    //
    // If it worked, dump out the data.
    //
    tprintf("  %2d: ", reg);

    if ((data & 0xc) == 0x4)
    {
      for (int j = 0; j < length; j++)
      {
        data = xgbe_phy_rd(lc, 1, base + j) & 0xFF;
        printf("%02x", data);
      }
      printf("\n");
    }
    else
      printf("read failed, code 0x%x\n", data);
  }
}

#endif

/** Microcode load. */
static int
bcm87x7_load_ucode(enet_link_config_t* lc)
{
  if (IS_BCM84727(lc))
  {
    //
    // XXX We don't have the right sequence yet for the 84727, so don't do
    // this there yet.  Note that this means we're doing the LED and
    // potentially preemphasis setup more often than we need to, but that's
    // probably not that big a deal.
    //
  }
  else
  {
    //
    // The instances of the 8747 and 8727 that we've seen have different
    // firmware versions; not sure if different versions are really in use or
    // if it's a chip-specific difference.  For now, assume the latter.  It
    // might turn out that we want to accept both of these.
    //
    int exp_vers = (IS_BCM8747(lc)) ? 0x0514 : 0x0511;

    //
    // Check the device to see if the firmware is already loaded.  If not,
    // load it, then ensure it got loaded properly.
    //
    int cksum = xgbe_phy_rd(lc, 1, 0xCA1C);
    int vers = xgbe_phy_rd(lc, 7, 0x805C);

    //
    // FIXME: we probably need to handle multiple firmware versions here.
    // See check below as well.
    //
#ifdef LINK_DEBUG
    tprintf("%s: bcm87x7 firmware cksum 0x%04x, vers 0x%04x: ",
            lc->device_name, cksum, vers);
#endif
    if (cksum != 0x600D || vers != exp_vers)
    {
#ifdef LINK_DEBUG
      printf("loading firmware\n");
#endif
      //
      // Enable SPI, release microcontroller from reset, wait for it to load.
      //
      xgbe_phy_wr(lc, 1, 0xC843, 0x0000);
      xgbe_phy_wr(lc, 1, 0xC840, 0x0000);
      xgbe_phy_wr(lc, 1, 0xCA10, 0x0188);

      drv_udelay(60000);

      //
      // Disable SPI.
      //
      xgbe_phy_wr(lc, 1, 0xC843, 0x000F);
      xgbe_phy_wr(lc, 1, 0xC840, 0x000C);
    }
    else
    {
#ifdef LINK_DEBUG
      printf("firmware already loaded\n");
#endif
      return 0;
    }

    cksum = xgbe_phy_rd(lc, 1, 0xCA1C);
    vers = xgbe_phy_rd(lc, 7, 0x805C);

    if (cksum != 0x600D || vers != exp_vers)
    {
      tprintf("hv_warning: %s: PHY init failed (firmware did not load, "
              "0x%x/0x%x)\n", lc->device_name, cksum, vers);

      return ENET_EIO;
    }
#ifdef LINK_DEBUG
    else
      tprintf("%s: bcm87x7 firmware load successful\n", lc->device_name);
#endif
  }

  //
  // Set up the LEDs.  This doesn't seem to work if you do it before
  // loading the microcode, and we only want to do it once, so this
  // seems like a good spot.
  //
  bcm87x7_set_led_mode(lc);

  //
  // If the receive traces are very long, increase the PHY's XAUI
  // preemphasis settings.  This is like the LEDs, in that loading the
  // microcode seems to mess up the settings, so we do it afterward.
  //
  for (int i = 0; i < 4; i++)
  {
#ifdef LINK_DEBUG
    tprintf("%s: lane %d rx len %d tx len %d\n", lc->device_name,
            i, lc->serdes_rx_lane_length[i], lc->serdes_tx_lane_length[i]);
#endif

    //
    // Preemphasis setting in percent.
    //
    int pre_pct = 0;
    //
    // This is the minimum lane length on a TILExtreme-Duo.
    //
    if (lc->serdes_rx_lane_length[i] >= 347)
      pre_pct = 44;
    //
    // This is the minimum lane length on the modules in the rear of a
    // TILExtreme-Duo, which are further away from the IOMs.
    //
    if (lc->serdes_rx_lane_length[i] >= 510)
      pre_pct = 52;

    if (pre_pct)
    {
      xgbe_phy_wr(lc, 4, 0x8067 + 0x10 * i, min(15, (pre_pct + 3) / 4) << 12);
#ifdef LINK_DEBUG
      tprintf("%s: lane %d: enabled preemphasis, %d%%\n", lc->device_name, i,
              pre_pct);
#endif
    }
  }

  return 0;
}


static int
bcm87x7_i2c_rd(enet_link_config_t* lc, int i2c_addr, int offset, int len,
               void* buf)
{
  //
  // Target location in the PHY's RAM for the read data.
  //
  const int base = 0x8200 - len;

  //
  // Set destination address in PHY's RAM and source address in EEPROM.
  //
  xgbe_phy_wr(lc, 1, 0x8004, base);
  xgbe_phy_wr(lc, 1, 0x8003, offset);

  //
  // Set read length.
  //
  int data = xgbe_phy_rd(lc, 1, 0x8002);
  data &= 0xc000;
  data |= len;
  xgbe_phy_wr(lc, 1, 0x8002, data);

  //
  // Set device I2C Address.
  //
  data = xgbe_phy_rd(lc, 1, 0x8005);
  data &= 0x1ff;
  data |= i2c_addr << 8;
  xgbe_phy_wr(lc, 1, 0x8005, data);

  //
  // Wait for device to be idle.
  //
  do
  {
    data = xgbe_phy_rd(lc, 1, 0x8000);
  }
  while ((data & 0xc) != 0);

  //
  // Kick off read.
  //
  data = xgbe_phy_rd(lc, 1, 0x8000);
  data &= 0xFFDC;
  data |= 0x0002;
  xgbe_phy_wr(lc, 1, 0x8000, data);

  //
  // Wait for read to finish.
  //
  do
  {
    data = xgbe_phy_rd(lc, 1, 0x8000);
  }
  while ((data & 0xc) == 8);

  //
  // If it worked, copy out the data.
  //
  if ((data & 0xc) == 0x4)
  {
    for (int i = 0; i < len; i++)
      ((uint8_t*) buf)[i] = xgbe_phy_rd(lc, 1, base + i);
    return len;
  }
  else
    return -1;
}


static int
bcm87x7_get_module_eeprom(enet_link_config_t* lc, int* type,
                          int offset, void* buf, int len)
{
  return get_module_eeprom(lc, type, offset, buf, len, bcm87x7_i2c_rd);
}


//
// Common xgbe routines.
//

/** Get state routine. */
static uint32_t
xgbe_link_get_state_common(enet_link_config_t* lc)
{
  //
  // If we're not trying to bring the link up, then the SERDES is off, and
  // we can't really trust the state bits, so just say the link is down.
  //
  if (!(lc->desired_state & ENET_LINK_SPEED))
    return 0;

  //
  // Figure out what state we'll return if we turn out to be up.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };
  uint32_t up_state =
    (mxpc.double_rate) ? ENET_LINK_20G | ENET_LINK_FDX
                       : (lc->xaui_refclk_125) ? ENET_LINK_12G | ENET_LINK_FDX
                                               : ENET_LINK_10G | ENET_LINK_FDX;

  up_state |= lc->desired_state & ENET_LINK_ALLLOOP;

  //
  // For the link to be up, the PCS state has to be okay, and the local
  // and remote fault bits have to be off.
  //
  MPIPE_XAUI_PCS_STS_t mxps =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_STS)
  };

#ifdef LINK_DEBUG
  tprintf("clock_ready %d pcs_sync_sts %d pcs_unaligned %d pcs_ready %d "
          "cdr_locked %d\n", mxps.clock_ready, mxps.pcs_sync_sts,
          mxps.pcs_unaligned, mxps.pcs_ready, mxps.cdr_locked);
#endif

  if (mxps.clock_ready != 1 || mxps.pcs_sync_sts != 0xf ||
      mxps.pcs_unaligned == 1 || mxps.pcs_ready != 0xf ||
      mxps.cdr_locked != 0xf)
    return 0;

  MPIPE_XAUI_RECEIVE_CONFIGURATION_t mxrc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa,
                    MPIPE_XAUI_RECEIVE_CONFIGURATION)
  };

#ifdef LINK_DEBUG
  tprintf("loc_fault %d rem_fault %d\n", mxrc.loc_fault, mxrc.rem_fault);
#endif

  if (mxrc.loc_fault || mxrc.rem_fault)
    return 0;

  //
  // Some PHYs (e.g., AMCC QT2025) seem to be sending us fault
  // notifications when the link is down, so that the MAC fault bits
  // checked above work.  Some (e.g., Broadcom BCM8706) don't; to
  // accommodate them, we also check the link state bit in the PCS.  It's
  // possible this could be fixed with different configuration bits in
  // the non-notifying PHYs, but if so it's not obvious.  We don't check
  // if we're in MAC loopback mode since we don't care about the PHY
  // state in that case.
  //
  // Note that the link status bit is a latching low bit, so if it's off,
  // we need to read it again to see if it's still low before concluding
  // that the link is currently down.
  //
  if ((lc->desired_state & ENET_LINK_LOOP_MAC) ||
      (!lc->has_phy) ||
      (xgbe_phy_rd(lc, 3, 1) & 0x4) ||
      (xgbe_phy_rd(lc, 3, 1) & 0x4))
    return up_state;

  return 0;
}


//
// No PHY (e.g., CX4 connection).
//

/** Probe routine. */
static int
xgbe_link_probe_nophy(enet_link_config_t* lc)
{
  if (lc->gbe || lc->has_phy)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s has no PHY (CX4 or onboard link)\n", lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_10G | ENET_LINK_12G | ENET_LINK_20G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_FDX;

  return 1;
}


/** Initialization routine. */
static int
xgbe_link_init_nophy(enet_link_config_t* lc)
{
  //
  // These are the interrupts we want to pay attention to whenever the
  // link is supposed to be up.
  //
  lc->mac_intrs =
    MPIPE_XAUI_INTERRUPT_STATUS__PCS_ALIGNMENT_CHANGED_MASK |
    MPIPE_XAUI_INTERRUPT_STATUS__LINK_STS_CHANGE_MASK;

  //
  // Use BIB data to configure the LEDs as appropriate.  The chip only
  // supports two settings, which we check for here explicitly, plus
  // fixed values, which we use in all other cases.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };

  if (lc->leds[0] == BI_ENET_LED_CFG__VAL_LINK &&
      lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT)
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_LINK_ACT;
  }
  else if (lc->leds[0] == BI_ENET_LED_CFG__VAL_ACT_TX &&
           lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT_RX)
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_TX_RX;
  }
  else
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_SW;

    int led0 = (lc->leds[0] == BI_ENET_LED_CFG__VAL_ON) ? 1 : 0;
    int led1 = (lc->leds[1] == BI_ENET_LED_CFG__VAL_ON) ? 2 : 0;

    mxpc.led_ovd_val = led1 | led0;
  }

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  return 0;
}

/** Begin configuration routine. */
static int
xgbe_link_start_config_nophy(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
    tprintf("%s link_start_config(new_config=%#x)\n", lc->device_name,
            new_config);
#endif

  if (!(new_config & ENET_LINK_SPEED))
  {
    //
    // Disable the MAC.
    //
    mac_disable(lc);

    //
    // Disable loopback.
    //
    loopback_config(lc, 0);

    // Note new link state.
    enet_new_link_state(lc, 0);
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (down) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  //
  // At this point, we know the desired state for the link is up.  If we're
  // turning off SERDES loopback, do so, and also disable the MAC; we'll
  // reenable it below.  Just disabling loopback and doing nothing else
  // seems to cause link flapping.  If we're enabling_loopback, that will
  // get done below after we enable the MAC.
  //
  if (!(new_config & ENET_LINK_LOOP_MAC) &&
      (lc->desired_state & ENET_LINK_LOOP_MAC))
  {
    mac_disable(lc);
    loopback_config(lc, 0);
  }

  //
  // Set the speed appropriately and enable the MAC, which should bring
  // up the link.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };
  mxpc.double_rate = (new_config & ENET_LINK_20G) != 0;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  mac_enable(lc);

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
    loopback_config(lc, 1);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


/** Interrupt routine. */
static int
xgbe_link_intr_nophy(enet_link_config_t* lc, int dummy)
{
  //
  // Figure out what interrupts we got; if none, return.
  //
  uint_reg_t intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                            MPIPE_XAUI_INTERRUPT_STATUS);

  if (!(intrs & lc->mac_intrs))
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s intr %#llx pcs_st %#lx in_msk %#lx rxcfg %#lx\n",
          lc->device_name, intrs,
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_PCS_STS),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_INTERRUPT_MASK),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_RECEIVE_CONFIGURATION));
#endif

  //
  // Reset the interrupts.  We have to do this before we actually look at
  // the status bits, so that if things are still changing, we will get
  // another interrupt.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_STATUS,
         intrs);

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine
  // to notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  enet_new_link_state(lc, new_state);

  return 1;
}


static const enet_link_ops_t xgbe_nophy_link_ops =
{
  .name = "xgbe_nophy",
  .probe = xgbe_link_probe_nophy,
  .init = xgbe_link_init_nophy,
  .start_config = xgbe_link_start_config_nophy,
  .get_state = xgbe_link_get_state_common,
  .intr = xgbe_link_intr_nophy,
};


//
// AMCC QT2025/QT2225.
//

/** Probe routine. */
static int
xgbe_link_probe_qt2025(enet_link_config_t* lc)
{
  if (lc->gbe || lc->phytype != 0x0043a400)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s phytype is AMCC QT2x25\n", lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_10G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_FDX;
  lc->possible_state |= ENET_LINK_LOOP_PHY;

  return 1;
}


/** Initialization routine. */
static int
xgbe_link_init_qt2025(enet_link_config_t* lc)
{
  //
  // Do initialization for the PHY; if it doesn't come up,
  // there's no point in initializing anything else.
  //
  if (qt2025_boot_eeprom(lc))
  {
    if (!lc->link_warned)
    {
      printf("%s: link failed to come up, PHY failure\n", lc->device_name);
      lc->link_warned = 1;
    }
    return ENET_EIO;
  }

  //
  // These are the interrupts we want to pay attention to whenever the
  // link is supposed to be up.
  //
  // FIXME Do we want LPI here?
  //
  lc->mac_intrs =
    MPIPE_XAUI_INTERRUPT_STATUS__PCS_ALIGNMENT_CHANGED_MASK |
    MPIPE_XAUI_INTERRUPT_STATUS__PHY_INT_MASK |
    MPIPE_XAUI_INTERRUPT_STATUS__LINK_STS_CHANGE_MASK;

  //
  // XXX FIXME use BIB data to configure the LEDs if necessary
  //

  return 0;
}

/** Begin configuration routine. */
static int
xgbe_link_start_config_qt2025(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
    tprintf("%s link_start_config(new_config=%#x)\n", lc->device_name,
            new_config);
#endif

  if (!(new_config & ENET_LINK_SPEED))
  {
    //
    // Disable the MAC.
    //
    mac_disable(lc);

    //
    // Disable loopback.
    //
    loopback_config(lc, 0);

    // Float TXENABLE.  The manual claims that you can do this by
    // writing to bit 1 of 1.9, as we do below, and also writing to
    // bit 11 of 1.0.  This doesn't seem to work.  This register
    // actually disables the TXENABLE I/O pin, and that does work.
    xgbe_phy_wr(lc, 1, 0xc313, xgbe_phy_rd(lc, 1, 0xc313) | 0x1000);

#ifdef QT2025_NO_SPLIT_LEDS
    // Top (LED 1): off.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0004);

    // Bottom (LED 2): off.
    xgbe_phy_wr(lc, 1, 0xd007, 0x0004);
#endif

    // Note new link state.
    enet_new_link_state(lc, 0);
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (down) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  //
  // At this point, we know the desired state for the link is up.  If we're
  // turning off SERDES loopback, do so, and also disable the MAC; we'll
  // reenable it below.  Just disabling loopback and doing nothing else
  // seems to cause link flapping.  If we're enabling_loopback, that will
  // get done below after we enable the MAC.
  //
  if (!(new_config & ENET_LINK_LOOP_MAC) &&
      (lc->desired_state & ENET_LINK_LOOP_MAC))
  {
    mac_disable(lc);
    loopback_config(lc, 0);
  }

  //
  // If the PHY loopback state is being changed, then change it.  Then, if
  // that's the only thing that's being changed, return.
  //
  if ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_PHY)
  {
    uint32_t pma_ctl = xgbe_phy_rd(lc, 1, 0);

    // Set PHY loopback properly.
    if (new_config & ENET_LINK_LOOP_PHY)
      pma_ctl |= 1;
    else
      pma_ctl &= ~1;

    xgbe_phy_wr(lc, 1,  0, pma_ctl);

#ifdef LINK_DEBUG
    tprintf("%s %s PHY loopback\n", lc->device_name,
            (new_config & ENET_LINK_LOOP_PHY) ? "enabling" : "disabling");
#endif

    if (!((new_config ^ lc->desired_state) & ~ENET_LINK_LOOP_PHY))
      return 0;
  }

  // Enable TXENABLE on the QT2025.
  xgbe_phy_wr(lc, 1, 0xc313, xgbe_phy_rd(lc, 1, 0xc313) & ~0x1000);

  //
  // Set the speed appropriately and enable the MAC, which should bring
  // up the link.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };
  mxpc.double_rate = (new_config & ENET_LINK_20G) != 0;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  mac_enable(lc);

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
    loopback_config(lc, 1);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


/** Interrupt routine. */
static int
xgbe_link_intr_qt2025(enet_link_config_t* lc, int dummy)
{
  uint_reg_t mac_intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                                MPIPE_XAUI_INTERRUPT_STATUS);

  if (!(mac_intrs & lc->mac_intrs))
    return 0;

#if 0 // XXX FIXME - old Pro registers def LINK_DEBUG
  tprintf("%s intr %d if_st %#lx if_c2 %#lx in_st %#lx in_ms %#lx rxcfg %#lx\n",
          lc->device_name, mac_intnum,
          cfg_rd(lc->shim_port, lc->mac_pa,
                 XAUI_MAC_INTERFACE_STATUS),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 XAUI_MAC_INTERFACE_CONTROL2),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 XAUI_MAC_INTERRUPT_STATUS),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 XAUI_MAC_INTERRUPT_MASK),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 XAUI_RECEIVE_CONFIGURATION));

  if (lc->phytype)
  {
    tprintf("%s 1.1 %#x 1.8 %#x 1.a %#x 3.1 %#x 3.8 %#x 3.20 %#x\n"
            "4.1 %#x 4.8 %#x"
            "\n",
            lc->device_name,
            xgbe_phy_rd(lc, 1, 0x1),
            xgbe_phy_rd(lc, 1, 0x8),
            xgbe_phy_rd(lc, 1, 0xa),
            xgbe_phy_rd(lc, 3, 0x1),
            xgbe_phy_rd(lc, 3, 0x8),
            xgbe_phy_rd(lc, 3, 0x20),
            xgbe_phy_rd(lc, 4, 0x1),
            xgbe_phy_rd(lc, 4, 0x8));

    tprintf("2nd 1.1 %#x 1.8 %#x 1.a %#x 3.1 %#x 3.8 %#x 3.20 %#x\n"
            "4.1 %#x 4.8 %#x"
            "\n",
            xgbe_phy_rd(lc, 1, 0x1),
            xgbe_phy_rd(lc, 1, 0x8),
            xgbe_phy_rd(lc, 1, 0xa),
            xgbe_phy_rd(lc, 3, 0x1),
            xgbe_phy_rd(lc, 3, 0x8),
            xgbe_phy_rd(lc, 3, 0x20),
            xgbe_phy_rd(lc, 4, 0x1),
            xgbe_phy_rd(lc, 4, 0x8));
  }
#endif
  //
  // Reset the interrupt.  We have to do this before we actually look at
  // the status bits, so that if things are still changing, we will get
  // another interrupt.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_STATUS,
         mac_intrs);

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine
  // to notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  enet_new_link_state(lc, new_state);

#ifdef QT2025_NO_SPLIT_LEDS
  if (new_state == ENET_LINK_10G)
  {
    // Top (LED 1): TX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0003);

    // Bottom (LED 2): RX activity and link state.
    xgbe_phy_wr(lc, 1, 0xd007, 0x000b);
  }
  else
  {
    // Top (LED 1): off.
    xgbe_phy_wr(lc, 1, 0xd006, 0x0004);

    // Bottom (LED 2): off.
    xgbe_phy_wr(lc, 1, 0xd007, 0x0004);
  }
#endif

  return 1;
}


static const enet_link_ops_t xgbe_qt2025_link_ops =
{
  .name = "xgbe_qt2025",
  .probe = xgbe_link_probe_qt2025,
  .init = xgbe_link_init_qt2025,
  .start_config = xgbe_link_start_config_qt2025,
  .get_state = xgbe_link_get_state_common,
  .intr = xgbe_link_intr_qt2025,
};


//
// Broadcom BCM8747/8727.
//

/** Probe routine. */
static int
xgbe_link_probe_bcm87x7(enet_link_config_t* lc)
{
  if (lc->gbe)
    return 0;

  if (IS_BCM8747(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s is Broadcom BCM8747, 10 Gbps mode\n", lc->device_name);
#endif
  }
  else if (IS_BCM8727(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s is Broadcom BCM8727, 10 Gbps mode\n", lc->device_name);
#endif
  }
  else if (IS_BCM84727(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s is Broadcom BCM84727, 10 Gbps mode\n", lc->device_name);
#endif
  }
  else
    return 0;

  lc->possible_state &= ENET_LINK_10G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_FDX |
                        ENET_LINK_LOOP_PHY;

  return 1;
}


/** Initialization routine. */
static int
xgbe_link_init_bcm87x7(enet_link_config_t* lc)
{
  //
  // Do common init flow first.
  //
  int rv = bcm87x7_phy_init(lc);

  if (rv)
    return rv;

  // Disable transmit (we'll enable this again later).
  xgbe_phy_wr(lc, 1, 0x0009, 1);

  //
  // These are the interrupts we want to pay attention to whenever the
  // link is supposed to be up.
  //
  // Note: we're explicitly ignoring the PHY interrupt here, for two
  // reasons.  First, it turns out not to add any value; we get accurate
  // link status without it.  Second, some very early versions of the
  // TILEmpower-Gx I/O module have an incorrect BIB which states that the
  // PHY interrupt is not inverted, even though it is.  If we enable the
  // PHY interrupt on those machines we get infinite interrupts.
  //
  // FIXME Do we want LPI here?
  //
  lc->mac_intrs = MPIPE_XAUI_INTERRUPT_STATUS__LINK_STS_CHANGE_MASK;

  //
  // We've historically paid attention to PCS alignment, but on the 84727
  // that seems to give us continuous interrupts if the link is down, so
  // we ignore it there.
  //
  if (!IS_BCM84727(lc))
    lc->mac_intrs |= MPIPE_XAUI_INTERRUPT_STATUS__PCS_ALIGNMENT_CHANGED_MASK;

  return 0;
}

/** Begin configuration routine. */
static int
xgbe_link_start_config_bcm87x7(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
    tprintf("%s link_start_config(new_config=%#x)\n", lc->device_name,
            new_config);
#endif

  if (!(new_config & ENET_LINK_SPEED))
  {
    //
    // Disable the MAC.
    //
    mac_disable(lc);

    //
    // Disable loopback.
    //
    loopback_config(lc, 0);

    // Disable the laser on the optical module.
    xgbe_phy_wr(lc, 1, 0x0009, 1);

    // Note new link state.
    enet_new_link_state(lc, 0);
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (down) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  //
  // If we're bringing the link up, ensure that the microcode is loaded.
  //
  int rv = bcm87x7_load_ucode(lc);
  if (rv)
    return rv;

  //
  // This is a good spot to dump out various SFP/SFP+ characteristics
  // if you're debugging something.
  //
  // bcm87x7_dump_sfp_eeprom(lc);
  // bcm87x7_dump_sfp_phy(lc);

  //
  // At this point, we know the desired state for the link is up.  If we're
  // turning off SERDES loopback, do so, and also disable the MAC; we'll
  // reenable it below.  Just disabling loopback and doing nothing else
  // seems to cause link flapping.  If we're enabling_loopback, that will
  // get done below after we enable the MAC.
  //
  if (!(new_config & ENET_LINK_LOOP_MAC) &&
      (lc->desired_state & ENET_LINK_LOOP_MAC))
  {
    mac_disable(lc);
    loopback_config(lc, 0);
  }

  // Enable the laser on the optical module; configure PHY for 10 Gbps
  xgbe_phy_wr(lc, 1, 0x0009, 0);
  xgbe_phy_wr(lc, 1, 0x0000, 0x2040);

  //
  // If the PHY loopback state is being changed, then change it.  Then, if
  // that's the only thing that's being changed, return.
  //
  if ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_PHY)
  {
    uint32_t pma_ctl = xgbe_phy_rd(lc, 1, 0);

    // Set PHY loopback properly.
    if (new_config & ENET_LINK_LOOP_PHY)
      pma_ctl |= 1;
    else
      pma_ctl &= ~1;

    xgbe_phy_wr(lc, 1,  0, pma_ctl);

#ifdef LINK_DEBUG
    tprintf("%s %s PHY loopback\n", lc->device_name,
            (new_config & ENET_LINK_LOOP_PHY) ? "enabling" : "disabling");
#endif

    if (!((new_config ^ lc->desired_state) & ~ENET_LINK_LOOP_PHY))
      return 0;
  }

  //
  // Set the speed appropriately and enable the MAC, which should bring
  // up the link.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };
  mxpc.double_rate = (new_config & ENET_LINK_20G) != 0;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  mac_enable(lc);

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
    loopback_config(lc, 1);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


/** Interrupt routine. */
static int
xgbe_link_intr_bcm87x7(enet_link_config_t* lc, int dummy)
{
  //
  // Figure out what interrupts we got; if none, return.
  //
  uint_reg_t intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                            MPIPE_XAUI_INTERRUPT_STATUS);

  if (!(intrs & lc->mac_intrs))
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s intr %#llx pcs_st %#lx in_msk %#lx rxcfg %#lx\n",
          lc->device_name, intrs,
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_PCS_STS),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_INTERRUPT_MASK),
          cfg_rd(lc->shim_port, lc->mac_pa,
                 MPIPE_XAUI_RECEIVE_CONFIGURATION));

  tprintf("%s 1.1 %#x 1.8 %#x 1.a %#x 3.1 %#x 3.8 %#x 3.20 %#x\n"
          "4.1 %#x 4.8 %#x"
          "\n",
          lc->device_name,
          xgbe_phy_rd(lc, 1, 0x1),
          xgbe_phy_rd(lc, 1, 0x8),
          xgbe_phy_rd(lc, 1, 0xa),
          xgbe_phy_rd(lc, 3, 0x1),
          xgbe_phy_rd(lc, 3, 0x8),
          xgbe_phy_rd(lc, 3, 0x20),
          xgbe_phy_rd(lc, 4, 0x1),
          xgbe_phy_rd(lc, 4, 0x8));

  tprintf("2nd 1.1 %#x 1.8 %#x 1.a %#x 3.1 %#x 3.8 %#x 3.20 %#x\n"
          "4.1 %#x 4.8 %#x"
          "\n",
          xgbe_phy_rd(lc, 1, 0x1),
          xgbe_phy_rd(lc, 1, 0x8),
          xgbe_phy_rd(lc, 1, 0xa),
          xgbe_phy_rd(lc, 3, 0x1),
          xgbe_phy_rd(lc, 3, 0x8),
          xgbe_phy_rd(lc, 3, 0x20),
          xgbe_phy_rd(lc, 4, 0x1),
          xgbe_phy_rd(lc, 4, 0x8));

  tprintf("%s rx_al_st %#x tx_al_st %#x la_al_st %#x\n",
          lc->device_name,
          xgbe_phy_rd(lc, 1, 0x9003),
          xgbe_phy_rd(lc, 1, 0x9004),
          xgbe_phy_rd(lc, 1, 0x9005));

  tprintf("2nd   rx_al_st %#x tx_al_st %#x la_al_st %#x\n",
          xgbe_phy_rd(lc, 1, 0x9003),
          xgbe_phy_rd(lc, 1, 0x9004),
          xgbe_phy_rd(lc, 1, 0x9005));
#endif

  //
  // Reset the interrupts.  We have to do this before we actually look at
  // the status bits, so that if things are still changing, we will get
  // another interrupt.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_STATUS,
         intrs);

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine
  // to notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  enet_new_link_state(lc, new_state);

  return 1;
}


static const enet_link_ops_t xgbe_bcm87x7_link_ops =
{
  .name = "xgbe_bcm87x7",
  .probe = xgbe_link_probe_bcm87x7,
  .init = xgbe_link_init_bcm87x7,
  .start_config = xgbe_link_start_config_bcm87x7,
  .get_state = xgbe_link_get_state_common,
  .intr = xgbe_link_intr_bcm87x7,
  .get_module_eeprom = bcm87x7_get_module_eeprom,
};


/** Read an MDIO register from a GbE interface's PHY.
 * @param lc Driver link configuration.
 * @param reg Register number.
 * @return Value read from PHY.
 */
static uint32_t
gbe_phy_rd(enet_link_config_t* lc, uint32_t reg)
{
  return gbe_mdio_rd(lc->shim_port, lc->mdio_mac_pa, lc->phyaddr, 0, reg);
}


/** Write an MDIO register on a GbE interface's PHY.
 * @param lc Driver link configuration.
 * @param reg Register number.
 * @param data Data to write.
 */
static void
gbe_phy_wr(enet_link_config_t* lc, uint32_t reg, uint32_t data)
{
  gbe_mdio_wr(lc->shim_port, lc->mdio_mac_pa, lc->phyaddr, 0, reg, data);
}


/** Read a register from a copper SFP module's PHY via I2C.  Note that the
 *  caller is responsible for calling i2c_switch_swing/release if necessary.
 * @param bus I2C master bus number.
 * @param reg Register number.
 * @return Value read from PHY, or 0xFFFF0000 if an error was encountered.
 *  Note that this value is chosen so that code bothering to check the
 *  value for an error can see that one happened, but code that doesn't
 *  will see all zeroes in the expected 16-bit return value; this has the
 *  salutary effect of causing gbe_link_get_state_nophy to report that the
 *  link is down in the error case.
 */
static int
sfp_phy_rd(int bus, int reg)
{
  uint16_t val;

  //
  // The registers are two bytes wide, but they're addressed as if they're
  // one byte wide.  This causes problems for register 7, 15, 23, etc.,
  // since the I2C controller normally doesn't like to wrap reads across
  // an 8-byte boundary.  We work around this by using addressless mode
  // in these cases, doing a separate dummy write to set the register
  // address and then telling the controller we're reading the data from
  // address zero.
  //
  if ((reg & 7) != 7)
  {
    if (i2c_rd_bus(bus, 0xAC, reg, 2, &val) != 2)
      return 0xFFFF0000;
  }
  else
  {
    uint8_t addrbyte = reg;

    if (i2c_wr_bus(bus, 0xAC | I2C_DEV_NOADDR, 0, 1, &addrbyte) != 1)
      return 0xFFFF0000;

    if (i2c_rd_bus(bus, 0xAC | I2C_DEV_NOADDR, 0, 2, &val) != 2)
      return 0xFFFF0000;
  }

  return be16_to_cpu(val);
}


/** Write a register in a copper SFP module's PHY via I2C.  Note that the
 *  caller is responsible for calling i2c_switch_swing/release if necessary.
 * @param bus I2C master bus number.
 * @param reg Register number.
 * @param data Data to write.
 */
static void
sfp_phy_wr(int bus, int reg, uint16_t data)
{
  //
  // See comments in sfp_phy_rd() for why certain registers are handled as
  // a special case.
  //
  if ((reg & 7) != 7)
  {
    uint16_t val = cpu_to_be16(data);
    i2c_wr_bus(bus, 0xAC, reg, 2, &val);
  }
  else
  {
    uint8_t addr_reg[3];
    addr_reg[0] = reg;
    addr_reg[1] = data >> 8;
    addr_reg[2] = data & 0xFF;
    i2c_wr_bus(bus, 0xAC | I2C_DEV_NOADDR, 0, 3, &addr_reg);
  }
}

//
// Various GbE plugins.
//

//
// Helper routines, used by multiple plugins.  Some of these are called
// from plugin routines, and some are used directly as one of the elements
// of the plugin's ops vector.  To the extent they depend upon PHY
// registers, they use ones which have standard meanings (i.e., are defined
// by 802.3).
//

/** Common probe routine. */
static int
gbe_link_probe_common(enet_link_config_t* lc)
{
  // FIXME - are hdx/fdx always appropriate, or are there some PHYs which
  // can't do half-duplex?

  lc->possible_state &= ENET_LINK_1G | ENET_LINK_100M | ENET_LINK_10M;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_HDX | ENET_LINK_FDX;

  return 1;
}


/** Common initialization routine. */
static int
gbe_link_init_common(enet_link_config_t* lc)
{
  //
  // Soft-reset the PHY just in case it wasn't hardware reset properly.
  //
  gbe_phy_wr(lc, 0, 1 << 15);

  //
  // Many PHYs come out of reset trying to bring the link up.
  // We don't want this, so set the power-down bit on the PHY.
  //
  gbe_phy_wr(lc, 0, 1 << 11);

  //
  // These are the shim interrupts we want to pay attention to whenever the
  // link is supposed to be up.  We enable the MAC interrupt, but in practice
  // we don't actually see it, just the external (PHY) interrupt.
  //
  lc->mac_intrs = MPIPE_GBE_INTERRUPT_STATUS__LINK_CHANGE_MASK;

  return 0;
}


/** Return the highest speed bit set in a configuration.
 * @param config Configuration.
 * @return Highest speed set in the configuration.
 */
static inline uint32_t
gbe_top_speed(uint32_t config)
{
  uint32_t des_speed = config & (ENET_LINK_SPEED & ~ENET_LINK_ANYSPEED);
  int top_speed_shift = 31 - __builtin_clz(des_speed);
  return (1 << top_speed_shift);
}


/** Configure the MAC to match a speed and duplex setting negotiated by the
 *  PHY.
 * @param link_config Configuration to match.
 */
static void
gbe_config_mac_speed(enet_link_config_t* lc, uint32_t link_config)
{
  //
  // If the link is going down, we don't need to mess with the configuration,
  // and doing so is fairly involved, so let's not bother.
  //
  if (!(link_config & ENET_LINK_SPEED))
    return;

  //
  // Get our current configuration and figure out what the new config
  // should be.
  //
  MPIPE_GBE_NETWORK_CONFIGURATION_t net_config =
    { .word = cfg_rd(lc->shim_port, lc->mac_pa,
                     MPIPE_GBE_NETWORK_CONFIGURATION) };

  int gige_mode = (link_config & ENET_LINK_1G) != 0;
  int speed = (link_config & ENET_LINK_100M) != 0;
  int full_duplex = (link_config & ENET_LINK_FDX) != 0;

  //
  // If we aren't changing anything, return.
  //
  if (net_config.gige_mode == gige_mode &&
      net_config.speed == speed &&
      net_config.full_duplex == full_duplex)
    return;

#ifdef LINK_DEBUG
  tprintf("%s: configuring MAC, gige %d speed %d fdx %d\n",
          lc->device_name, gige_mode, speed, full_duplex);
#endif

  //
  // Okay, we're changing the state.  The MAC does not react well to having
  // these config bits (particularly gige_mode) changed while packets are
  // flowing through it, so to prevent any misbehavior, we're first going
  // to temporarily stop any egress rings on this channel by asserting PFC
  // pause on queue 15; then we're going to disable transmit and receive
  // operation in the MAC.
  //
  net_config.gige_mode = gige_mode;
  net_config.speed = speed;
  net_config.full_duplex = full_duplex;

  MPIPE_GBE_MAC_INTFC_TX_CTL_t orig_tx_ctl =
    { .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_TX_CTL) };

  //
  // Make it look like queue 15 is paused.  We set up all egress rings to
  // respond to this queue.
  //
  MPIPE_GBE_MAC_INTFC_TX_CTL_t tx_ctl = orig_tx_ctl;
  tx_ctl.tx_pause_ovd = 1;
  tx_ctl.tx_pfc_pause_val = 1 << 15;

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_TX_CTL, tx_ctl.word);
  __insn_mf();

  //
  // Egress will stop on a packet boundary.  Figure out how long we need
  // to wait to be sure the longest possible packet will have drained.
  // The extra 64 bytes here is a fudge factor to account for preamble,
  // IPG, etc.
  //
  int speed_mbps = net_config.gige_mode ? 1000 : net_config.speed ? 100 : 10;
  int pkt_bits = 8 * (64 + (lc->jumbo ? 10240 : 1536));
  int delay_usec = (pkt_bits + speed_mbps - 1) / speed_mbps;

  drv_udelay(delay_usec);

  //
  // Disable MAC transmit/receive.
  //
  MPIPE_GBE_NETWORK_CONTROL_t orig_net_ctl =
    { .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONTROL) };

  MPIPE_GBE_NETWORK_CONTROL_t net_ctl = orig_net_ctl;
  net_ctl.tx_ena = 0;
  net_ctl.rx_ena = 0;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONTROL, net_ctl.word);
  __insn_mf();

  //
  // Do the actual configuration change.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONFIGURATION,
         net_config.word);
  __insn_mf();

  //
  // If we start up egress right away, and we've just cleared gige_mode, we
  // see problems with the MAC.  The required delay to avoid this seems to
  // be a couple hundred cycles, but we're waiting 5 us just to be
  // paranoid.
  //
  drv_udelay(5);

  //
  // Reenable transmit/receive.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_NETWORK_CONTROL,
         orig_net_ctl.word);

  //
  // Reenable egress.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_MAC_INTFC_TX_CTL,
         orig_tx_ctl.word);
}


/** Calculate speeds we'll advertise based on our configuration. */
static void
gbe_speed_bits(uint32_t new_config, unsigned int* speed_bits_4,
               unsigned int* speed_bits_9)
{
  unsigned int duplex = ((new_config & ENET_LINK_FDX) ? 2 : 0) |
                        ((new_config & ENET_LINK_HDX) ? 1 : 0);

  if (new_config & ENET_LINK_1G)
    *speed_bits_9 |= duplex << 8;
  if (new_config & ENET_LINK_100M)
    *speed_bits_4 |= duplex << 7;
  if (new_config & ENET_LINK_10M)
    *speed_bits_4 |= duplex << 5;
}


#ifdef LINK_DEBUG

/** Copy a length-limited string and then strip trailing spaces from it.
 * @param dest Destination buffer.
 * @param src Source string.
 * @param n Maximum number of bytes copied from src to dest.  Note that
 *  a trailing NULL will be added after the copy, so dest must have room
 *  for n + 1 bytes.
 */
static void
cpy_strip(char* dst, const uint8_t* src, size_t n)
{
  char* d = dst;
  while (n-- && (*d++ = *src++) != '\0')
    ;
  *d-- = 0;
  while (d >= dst && (*d == '\0' || *d == ' '))
    *d-- = '\0';
}

#endif


/** Test an SFP module to see if it's a copper SFP; if so, and if
 *  necessary based on the requested configuration, configure it in SGMII
 *  mode so that we can support 10 and 100 Mbps speeds in addition to 1
 *  Gbps.
 * @param lc Link configuration.
 * @param new_config Desired configuration.
 */
static void
sfp_test_config_cu(enet_link_config_t* lc, uint32_t new_config)
{
  //
  // If no module is plugged in, there's nothing for us to do.
  //
  if (get_signal(lc->sfp_cfg.mod_abs_sig, SIGNAL_ASSERT) > 0)
    return;

  //
  // Enable the SFP.  For a fiber SFP, this enables transmit, but for a
  // copper SFP, this takes the PHY out of reset.  The 88E1111
  // documentation says that power has to be valid for at least 10 ms
  // before you remove reset, and that the reset pulse has to be at least
  // 10 ms wide in any event.  It's possible that the module was
  // hot-plugged just before we were called, so we wait here to avoid
  // violating those contraints.
  //
  drv_udelay(15 * 1000);
  set_signal(lc->sfp_cfg.tx_disable_sig, SIGNAL_DEASSERT);

  //
  // Start a timer that we'll use to force a delay between deasserting
  // TX_DISABLE to the SFP and trying to access the PHY within the SFP, if
  // it turns out to be a copper SFP.  The 88E1111 PRM claims that register
  // access is available 5 ms after hardware reset, but that seems to be
  // bogus.  Measurements show that the point at which it sometimes works,
  // sometimes doesn't is 86 ms, when the module has been powered on for a
  // while; it's even longer, somewhere between 140 and 160 ms, if the
  // module has just been hot-plugged.  We use 200 ms to provide a safety
  // margin, but might need to revisit that based on experience with a
  // wider set of systems and SFPs.
  //
  uint64_t reset_timer = drv_timer_start(200 * 1000);

  int addr = (lc->sfp_cfg.i2c.dev_addr << 1);
  int bus = lc->sfp_cfg.i2c.bus;
  int inst = lc->sfp_cfg.i2c.switch_inst;
  int chan = lc->sfp_cfg.i2c.switch_chan;

  //
  // Let's see if we have a copper SFP module, if we haven't checked
  // already.  Note that if we aren't enabling 10/100 operation we won't
  // bother checking; this avoids wasting time if the user knows they're
  // doing 1 G, and also avoids changing the behavior on older boards where
  // the BIB specifies just 1 G support.
  //
  if (lc->sfp_cu == ENET_SFP_CU_UNK &&
      (new_config & (ENET_LINK_100M | ENET_LINK_10M)))
  {
    //
    // We assume we won't find a copper SFP; we'll change this later if we
    // do.
    //
    lc->sfp_cu = ENET_SFP_CU_NO;

    //
    // We might consider tweaking this code so that we make only one call
    // to i2c_switch_{swing,release} per call to this routine.  That would
    // make things run faster, but might tie up the I2C bus so that other
    // tiles couldn't use it while we were running, so it's not clearly a
    // win.
    //
    i2c_switch_swing(bus, inst, chan);

    uint8_t sfp_eeprom[64];

    int eeprom_len = i2c_rd_bus(bus, addr, 0, sizeof (sfp_eeprom), sfp_eeprom);

    if (eeprom_len >= 64)
    {
      uint8_t csum = 0;
      for (int i = 0; i < 63; i++)
        csum += sfp_eeprom[i];

      if (csum != sfp_eeprom[63])
      {
#ifdef LINK_DEBUG
        tprintf("%s SFP has bad checksum (calculated 0x%02x, "
                "expected 0x%02x)\n",
                lc->device_name, csum, sfp_eeprom[63]);
#endif
      }
      else
      {
#ifdef LINK_DEBUG
        char sfp_mfg[17];
        cpy_strip(sfp_mfg, sfp_eeprom + 20, 16);
        char sfp_model[17];
        cpy_strip(sfp_model, sfp_eeprom + 40, 16);
        char sfp_ver[5];
        cpy_strip(sfp_ver, sfp_eeprom + 56, 4);
        tprintf("%s SFP module: %s %s, ver %s\n",
                lc->device_name, sfp_mfg, sfp_model, sfp_ver);
#endif

        if (sfp_eeprom[0] == 3 &&  // Transceiver type is SFP
            sfp_eeprom[1] == 4 &&  // Extended ID is serial ID definition
            (sfp_eeprom[6] & 8)    // 1000BASE-T is supported
           )
        {
          //
          // If this is a copper SFP, see whether the PHY inside is a Marvell
          // 88E1111.
          //
          addr = 0xAC;

          //
          // We could consider releasing and re-swinging the I2C switch
          // here, since we're delaying a fair bit.
          //
          while (!drv_timer_done(reset_timer))
            ;

          int reg2 = sfp_phy_rd(bus, 2);
          int reg3 = sfp_phy_rd(bus, 3);
          if (reg2 == 0x0141 && (reg3 & 0xFFF0) == 0x0CC0)
          {
#ifdef LINK_DEBUG
            tprintf("%s found 88E1111 PHY in SFP\n", lc->device_name);
#endif
            lc->sfp_cu = ENET_SFP_CU_YES;
          }
#ifdef LINK_DEBUG
          else
              tprintf("%s not 88E1111 (%04x/%04x), marking non-copper SFP\n",
                      lc->device_name, reg2, reg3);
#endif
        }
#ifdef LINK_DEBUG
        else
            tprintf("%s No 1000BASE-T support, marking non-copper SFP\n",
                    lc->device_name);
#endif
      }
    }
#ifdef LINK_DEBUG
    else
        tprintf("%s EEPROM read failure (%d), marking non-copper SFP\n",
                lc->device_name, eeprom_len);
#endif

    i2c_switch_release(bus, inst);
  }

  //
  // If we have a copper module, but it's not in SGMII mode, put it in that
  // mode.  Again, we don't bother to do this if we're 1G-only.
  //
  if (lc->sfp_cu == ENET_SFP_CU_YES && !lc->sfp_sgmii &&
      (new_config & (ENET_LINK_100M | ENET_LINK_10M)))
  {
    i2c_switch_swing(bus, inst, chan);

    // Change mode to 0100 (SGMII without clock), and disable bypass of
    // autonegotiation
    uint16_t data = sfp_phy_rd(bus, 27);
    data &= ~(0xF | (1 << 12));
    data |= 0x4;
    sfp_phy_wr(bus, 27, data);

    // Soft reset.
    data = sfp_phy_rd(bus, 0);
    sfp_phy_wr(bus, 0, data | (1 << 15));

    i2c_switch_release(bus, inst);

    lc->sfp_sgmii = 1;
  }

  //
  // If we're in SGMII mode, autonegotiation must be enabled, and if not,
  // it mustn't be.
  //
  MPIPE_GBE_PCS_CTL_t pcs_ctl =
  {
     .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_CTL)
  };

  if (pcs_ctl.auto_neg != lc->sfp_sgmii)
  {
    pcs_ctl.auto_neg = lc->sfp_sgmii;
    lc->no_auto_neg = !pcs_ctl.auto_neg;
    //
    // You might think you'd need a soft reset here, but you'd be wrong;
    // in fact, if you're trying to turn off autonegotiation, you *can't*
    // do a soft reset, since that automatically sets the auto_neg bit!
    //
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_CTL, pcs_ctl.word);
    if (pcs_ctl.auto_neg)
    {
      pcs_ctl.restart_neg = 1;
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_CTL, pcs_ctl.word);
    }
  }

  //
  // If we're in SGMII mode, advertise the requested link modes.
  //
  if (lc->sfp_sgmii)
  {
    // Calculate speeds we'll advertise based on our configuration.
    unsigned int speed_bits_4 = sfp_phy_rd(bus, 4) & 0x1C1F;
    unsigned int speed_bits_9 = sfp_phy_rd(bus, 9) & 0x00FF;
    gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

    i2c_switch_swing(bus, inst, chan);

    // Advertise requested speeds.
    sfp_phy_wr(bus, 4, speed_bits_4);
    sfp_phy_wr(bus, 9, speed_bits_9);

    // Enable and restart autonegotiation.
    uint16_t data = sfp_phy_rd(bus, 0);
    sfp_phy_wr(bus, 0, data | (1 << 12) | (1 << 9));

    i2c_switch_release(bus, inst);
  }
}


/** Common begin configuration routine.  This handles a number of tasks
 *  which are common to most or all PHY-specific start_config routines:
 *  taking the link down, enabling/disabling interrupts, powering on
 *  the PHY, and enabling/disabling MAC loopback.
 * @param lc Link configuration.
 * @param new_config Desired configuration.
 * @return Nonzero if the caller should just return zero; this happens for
 *   link down requests, which can be completely handled by this routine.
 *   Zero if the caller should continue execution; in this case, the
 *   link is not being taken down.
 */
static int
gbe_link_start_config_common(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
  tprintf("%s link_start_config_common(new_config=%#x)\n", lc->device_name,
          new_config);
#endif

  if ((new_config & ENET_LINK_SPEED) == 0)
  {
    //
    // Disable any SFP signal interrupts (our caller will disable the
    // mPIPE MAC interrupts).
    //
    if (lc->link_does_intr && lc->has_sfp_cfg)
    {
      disable_signal_intr(lc->sfp_cfg.mod_abs_sig, SIGNAL_ASSERT |
                          SIGNAL_DEASSERT);
      disable_signal_intr(lc->sfp_cfg.rx_los_sig, SIGNAL_ASSERT |
                          SIGNAL_DEASSERT);
    }

    //
    // Take the link down.
    //

    // Disable the MAC.
    mac_disable(lc);

    // Disable loopback.
    loopback_config(lc, 0);

    // Set the power-down bit on the PHY, or turn off the SFP.
    if (lc->has_phy)
      gbe_phy_wr(lc, 0, 1 << 11);
    else if (lc->has_sfp_cfg)
    {
      set_signal(lc->sfp_cfg.tx_disable_sig, SIGNAL_ASSERT);

      //
      // Remember that we turned off the SFP.  Note that if we were paying
      // attention to the MOD_ABS interrupt while the desired link state was
      // down, and clearing sfp_cu when the module was removed, we could just
      // clear sfp_sgmii here, which would save us time bringing the link
      // back up.  But without that, the module could be swapped and we
      // wouldn't notice, so we have to set it to unknkown (and thus re-read
      // the EEPROM every time we bring up the link).
      //
      lc->sfp_cu = ENET_SFP_CU_UNK;
      lc->sfp_sgmii = 0;
    }

    // Note new link state.
    enet_new_link_state(lc, 0);

    // Tell our caller it doesn't need to do anything else.
    return 1;
  }

  //
  // If the link was already being brought up, and the MAC loopback state is
  // being changed, then change it and return.  Otherwise, we'll set up
  // loopback later if it's enabled.
  //
  if ((lc->desired_state & ENET_LINK_SPEED) &&
      ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_MAC))
  {
    loopback_config(lc, new_config & ENET_LINK_LOOP_MAC);
    return 1;
  }

  //
  // Enable the MAC.
  //
  mac_enable(lc);

  if (lc->link_does_intr)
  {
    //
    // Clear and then enable the one interrupt we care about within the
    // MAC (LINK_CHANGE).
    //
    (void) cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_STATUS);
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_ENABLE,
           MPIPE_GBE_INTERRUPT_STATUS__LINK_CHANGE_MASK);

    //
    // Enable any SFP interrupts.
    //
    if (lc->has_sfp_cfg)
    {
      enable_signal_intr(lc->sfp_cfg.mod_abs_sig, SIGNAL_ASSERT |
                         SIGNAL_DEASSERT);
      enable_signal_intr(lc->sfp_cfg.rx_los_sig, SIGNAL_ASSERT |
                         SIGNAL_DEASSERT);
    }
  }

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.  Note that
  // we return early in this case since we don't need to configure the PHY.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
  {
    loopback_config(lc, 1);
    return 1;
  }

  // Clear the power-down bit on the PHY, or enable and potentially
  // configure the SFP.
  if (lc->has_phy)
    gbe_phy_wr(lc, 0, 0);
  else if (lc->has_sfp_cfg)
    sfp_test_config_cu(lc, new_config);

  return 0;
}


/** Common get state routine which senses the PCS state. */
static int
gbe_link_pcs_down(enet_link_config_t* lc)
{
  MPIPE_GBE_PCS_STS_t mgps =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_STS)
  };

  if ((!mgps.auto_neg_comp && !lc->no_auto_neg) || mgps.rem_fault ||
      !mgps.link_sts)
  {
#ifdef LINK_DEBUG
      tprintf("%s 1 mgps.auto_neg_comp %d mgps.rem_fault %d "
              "mgps.link_sts %d\n", lc->device_name,
              mgps.auto_neg_comp, mgps.rem_fault, mgps.link_sts);
#endif
    //
    // Some of the error bits latch, so try again.
    //
    mgps.word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_PCS_STS);
#ifdef LINK_DEBUG
      tprintf("%s 2 mgps.auto_neg_comp %d mgps.rem_fault %d "
              "mgps.link_sts %d\n", lc->device_name,
              mgps.auto_neg_comp, mgps.rem_fault, mgps.link_sts);
#endif
    if ((!mgps.auto_neg_comp && !lc->no_auto_neg) || mgps.rem_fault ||
        !mgps.link_sts)
      return 1;
  }

  return 0;
}


/** Get state routine for devices which autonegotiate. */
static uint32_t
gbe_link_get_state_auto(enet_link_config_t* lc)
{
  //
  // If we're not trying to bring the link up, we may have never
  // initialized the PHY, so it's not clear we can trust the results;
  // just say it's down.
  //
  if (!(lc->desired_state & ENET_LINK_SPEED))
    return 0;

  //
  // At this point, we know we want the link to be up, so if we've enabled
  // MAC loopback, the link is by definition up.  The speed has no real
  // meaning in this case, so just return the highest one that they asked
  // for.
  //
  if (lc->desired_state & ENET_LINK_LOOP_MAC)
    return gbe_top_speed(lc->desired_state) | ENET_LINK_LOOP_MAC |
      ENET_LINK_FDX;

  //
  // If the link is down, say so.  Note that the PHY bit we're looking at
  // latches in the "down" position, so we may need to read it twice.
  //
  if (gbe_link_pcs_down(lc) || (!(gbe_phy_rd(lc, 1) & 0x4) &&
                                !(gbe_phy_rd(lc, 1) & 0x4)))
    return 0;

  //
  // It's up; determine the speed.
  //
  uint32_t reg0 = gbe_phy_rd(lc, 0);

  //
  // If the auto-negotiation bit is off, then just figure out what the
  // configured speed is.  This happens in loopback mode.
  //
  if (!(reg0 & 0x1000))
  {
    uint32_t duplex = (reg0 & 0x100) ? ENET_LINK_FDX : ENET_LINK_HDX;

    if (reg0 & 0x40)
      return duplex | ENET_LINK_1G;
    else if (reg0 & 0x2000)
      return duplex | ENET_LINK_100M;
    else
      return duplex | ENET_LINK_10M;
  }

  //
  // Otherwise, see what we negotiated.
  //
  uint32_t advSpeed = gbe_phy_rd(lc, 9);
  uint32_t partnerSpeed = gbe_phy_rd(lc, 10);

  if ((advSpeed & 0x0200) && (partnerSpeed & 0x0800))
    return ENET_LINK_FDX | ENET_LINK_1G;
  else if ((advSpeed & 0x0100) && (partnerSpeed & 0x0400))
    return ENET_LINK_HDX | ENET_LINK_1G;
  else
  {
    advSpeed = gbe_phy_rd(lc, 4);
    partnerSpeed = gbe_phy_rd(lc, 5);
    if (advSpeed & partnerSpeed & 0x0100)
      return ENET_LINK_FDX | ENET_LINK_100M;
    else if (advSpeed & partnerSpeed & 0x0080)
      return ENET_LINK_HDX | ENET_LINK_100M;
    else if (advSpeed & partnerSpeed & 0x0040)
      return ENET_LINK_FDX | ENET_LINK_10M;
    else if (advSpeed & partnerSpeed & 0x0020)
      return ENET_LINK_HDX | ENET_LINK_10M;
    else
      return 0;
  }
}


/** Common interrupt routine. */
static int
gbe_link_intr_common(enet_link_config_t* lc, int phy_intregnum)
{
  //
  // Get the PHY status bits, then reset the shim interrupt, then
  // get the PHY status bits again in case new interrupts showed up
  // in the meantime.  This isn't strictly necessary; we don't actually
  // act on the PHY status bits right now, so this would just lead
  // to an extra interrupt, which wouldn't do anything.  Easier to
  // just do it right now, though, in case we want to act differently
  // on different interrupts in the future.
  //

  uint32_t phy_intr = gbe_phy_rd(lc, phy_intregnum);

  uint_reg_t mac_intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                                MPIPE_GBE_INTERRUPT_STATUS);

  if (!((mac_intrs & lc->mac_intrs) | phy_intr))
    return 0;

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_STATUS,
         mac_intrs);

  phy_intr |= gbe_phy_rd(lc, phy_intregnum);

#ifdef LINK_DEBUG
  tprintf("%s mac_intr 0x%llx phy_intr %#x\n",
          lc->device_name, mac_intrs, phy_intr);
  tprintf("%s r00 0x%04x r01 0x%04x r02 0x%04x r03 0x%04x\n"
          "      r04 0x%04x r05 0x%04x r06 0x%04x r07 0x%04x\n",
          lc->device_name,
          gbe_phy_rd(lc, 0), gbe_phy_rd(lc, 1),
          gbe_phy_rd(lc, 2), gbe_phy_rd(lc, 3),
          gbe_phy_rd(lc, 4), gbe_phy_rd(lc, 5),
          gbe_phy_rd(lc, 6), gbe_phy_rd(lc, 7));
  tprintf("%s r08 0x%04x r09 0x%04x r10 0x%04x r11 0x%04x\n"
          "      r12 0x%04x r13 0x%04x r14 0x%04x r15 0x%04x\n",
          lc->device_name,
          gbe_phy_rd(lc,  8), gbe_phy_rd(lc,  9),
          gbe_phy_rd(lc, 10), gbe_phy_rd(lc, 11),
          gbe_phy_rd(lc, 12), gbe_phy_rd(lc, 13),
          gbe_phy_rd(lc, 14), gbe_phy_rd(lc, 15));
  tprintf("%s r16 0x%04x r17 0x%04x r18 0x%04x r19 0x%04x\n"
          "      r20 0x%04x r21 0x%04x r22 0x%04x r23 0x%04x\n",
          lc->device_name,
          gbe_phy_rd(lc, 16), gbe_phy_rd(lc, 17),
          gbe_phy_rd(lc, 18), gbe_phy_rd(lc, 19),
          gbe_phy_rd(lc, 20), gbe_phy_rd(lc, 21),
          gbe_phy_rd(lc, 22), gbe_phy_rd(lc, 23));
  tprintf("%s r24 0x%04x r25 0x%04x r26 0x%04x r27 0x%04x\n"
          "      r28 0x%04x r29 0x%04x r30 0x%04x r31 0x%04x\n",
          lc->device_name,
          gbe_phy_rd(lc, 24), gbe_phy_rd(lc, 25),
          gbe_phy_rd(lc, 26), gbe_phy_rd(lc, 27),
          gbe_phy_rd(lc, 28), gbe_phy_rd(lc, 29),
          gbe_phy_rd(lc, 30), gbe_phy_rd(lc, 31));
#endif

  //
  // Get the current state, configure the MAC to match the speed we're now
  // using, and call the state change routine.  If it hasn't actually
  // changed, it's the job of the state change routine to notice that and
  // not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  gbe_config_mac_speed(lc, new_state);
  enet_new_link_state(lc, new_state);

  return 1;
}


/** Interrupt routine for Marvell PHYs. */
static int
gbe_link_intr_mrvl(enet_link_config_t* lc, int dummy)
{
  //
  // On Marvell parts, register 19 is the interrupt status register.
  //
  return gbe_link_intr_common(lc, 19);
}


/** Interrupt routine for Broadcom PHYs. */
static int
gbe_link_intr_brcm(enet_link_config_t* lc, int dummy)
{
  //
  // On Broadcom parts, register 26 is the interrupt status register.
  //
  return gbe_link_intr_common(lc, 26);
}


/** Interrupt routine for Vitesse PHYs. */
static int
gbe_link_intr_vsc(enet_link_config_t* lc, int dummy)
{
  //
  // On Vitesse parts, register 26 is the interrupt status register.
  //
  return gbe_link_intr_common(lc, 26);
}


/** Interrupt routine for Realtek PHYs. */
static int
gbe_link_intr_rtl(enet_link_config_t* lc, int dummy)
{
  //
  // On Realtek parts, register 19 is the interrupt status register.
  //
  return gbe_link_intr_common(lc, 19);
}


//
// Marvell 88E1121 and 88E1118.
//


/** Probe routine. */
static int
gbe_link_probe_88e1121_18(enet_link_config_t* lc)
{
  if (!lc->gbe || ((lc->phytype & ~0xf) != 0x1410cb0 &&
                   (lc->phytype & ~0xf) != 0x1410e10))
    return 0;

#ifdef LINK_DEBUG
  int is_88e1121 = (lc->phytype & ~0xf) == 0x1410cb0;
  tprintf("%s is Marvell %s\n", lc->device_name,
          is_88e1121 ? "88E1121" : "88E1118");
#endif

  gbe_link_probe_common(lc);

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_88e1121_18(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  // Clear and then enable PHY interrupts (speed/duplex/link status
  // changed, auto-negotiation complete, and page received).
  (void) gbe_phy_rd(lc, 19);
  gbe_phy_wr(lc, 18, 0x7C00);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 9, speed_bits_9);

  // Restart negotiation.
  gbe_phy_wr(lc, 0, 0x9340);

  // Enable LEDs.
  gbe_phy_wr(lc, 22, 3);
  uint16_t phy_data = gbe_phy_rd(lc, 16);

#if 0
  uint32_t *led_ptr;
  uint32_t led_desc;

  led_desc = bi_getparam(BI_TYPE_ENET_PHY_LED_CFG, lc->instance, &led_ptr,
                         NULL);
  switch ((led_desc == BI_NULL) ? -1 : led_ptr[0])
  {
  case BI_ENET_PHY_3LED_ACTIVITY_SPEED:
    // 3 LEDs: LED 0 = 1000Mbps, LED 1 = 100Mbps, LED 2 = activity.
    phy_data &= 0xf000;
    phy_data |= 0x0477;

    break;

  default:
  case BI_ENET_PHY_2LED_ACTIVITY_STATUS:
    // The default is 2 LEDs: LED 0 = link, LED 1 = activity.
    phy_data &= 0xff00;
    phy_data |= 0x0040;
  }
#endif

  gbe_phy_wr(lc, 16, phy_data);
  gbe_phy_wr(lc, 22, 0);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_88e1121_18_link_ops =
{
  .name = "gbe_mrvl_88e1121_88e1121",
  .probe = gbe_link_probe_88e1121_18,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_88e1121_18,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_mrvl,
};


//
// Marvell 88E1111, strapped for 1 Gbps fiber only.
//

/** Probe routine. */
static int
gbe_link_probe_88e1111_1g(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x1410cc0)
    return 0;

  // (r27 & 0x8000) == 0x8000: Copper/Fiber Autoselect Disabled
  // (r27 & 0x000F) == 3: RGMII->Fiber mode
  if ((gbe_phy_rd(lc, 27) & 0x800F) != 0x8003)
    return 0;

  //
  // This is a hack.  The SFP+ mezzanine card and the AMC card use the same
  // PHY, but it's wired up in very different ways.  One important
  // difference is that the SFP+ card wants to look at the signal detect
  // input to avoid link flapping when there's no cable connected, but the
  // AMC card, which uses the same PHY, doesn't connect signal detect, so
  // if we enable it there the link won't come up.  We don't have a BIB
  // entry describing how the SFP is wired to the PHY, only one describing
  // how it's wired to various GPIO pins.  Until we fix that, we're going
  // to just check to see if we have anything at all defined for the SFP's
  // RXLOS signal; if we do, we'll assume this is not the AMC card, and
  // we'll take responsibility for the link.
  //
#if 0
  uint32_t *sfp_ptr;
  uint32_t sfp_desc;

  sfp_desc = bi_getparam(BI_TYPE_SFP, lc->instance, &sfp_ptr, NULL);
  if (sfp_desc == BI_NULL ||
      BI_WDS(sfp_desc) < 3 ||
      (sfp_ptr[2] & 0xFF) == BI_SIGNAL_NONE)
    return 0;
#endif

#ifdef LINK_DEBUG
  tprintf("%s is Marvell 88E1111, strapped for 1 Gbps fiber\n",
          lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_1G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_LOOP_PHY |
                        ENET_LINK_HDX | ENET_LINK_FDX;

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_88e1111_1g(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  uint32_t speed_bits_9 = 0x0200;          // 1G full
  uint32_t speed_bits_4 = 0x1e0;           // 1000base-X FDX, HDX,
                                           //  Sym/Asym Pause

  // Clear and then enable PHY interrupts (speed/duplex/link status
  // changed, auto-negotiation complete, and page received).
  (void) gbe_phy_rd(lc, 19);
  gbe_phy_wr(lc, 18, 0x7C00);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  gbe_phy_wr(lc, 0x14, 0x0ce2);

  // Pay attention to the signal detect input.
  uint32_t data = gbe_phy_rd(lc, 26);
  gbe_phy_wr(lc, 26, data | 0x80);

  //
  // Reset and configure.
  //
  if (new_config & ENET_LINK_LOOP_PHY)
  {
    //
    // We're doing PHY loopback, so reset and disable autonegotiation, then
    // set the loopback bit.
    //
    gbe_phy_wr(lc, 0, 0x8140);
    gbe_phy_wr(lc, 0, 0x4140);
#ifdef LINK_DEBUG
    tprintf("%s enabling PHY loopback\n", lc->device_name);
#endif
  }
  else
  {
    //
    // No loopback, just reset and enable autonegotiation.
    //
    gbe_phy_wr(lc, 0, 0x9140);
  }

#ifdef LINK_DEBUG
  tprintf("%s link_start_config_88e1111_1g returns 0\n", lc->device_name);
#endif
  return 0;
}


/** Get state routine. */
static uint32_t
gbe_link_get_state_88e1111_1g(enet_link_config_t* lc)
{
  //
  // If we're not trying to bring the link up, we may have never
  // initialized the PHY, so it's not clear we can trust the results;
  // just say it's down.
  //
  if (!(lc->desired_state & ENET_LINK_SPEED))
    return 0;

  //
  // At this point, we know we want the link to be up, so if we've enabled
  // MAC loopback, the link is by definition up.
  //
  if (lc->desired_state & ENET_LINK_LOOP_MAC)
    return ENET_LINK_1G | ENET_LINK_LOOP_MAC | ENET_LINK_FDX;

  if (!gbe_link_pcs_down(lc) &&
      (gbe_phy_rd(lc, 17) & 0x400))
    return ENET_LINK_1G | ENET_LINK_FDX;

  return 0;
}


static const enet_link_ops_t gbe_88e1111_1g_link_ops =
{
  .name = "gbe_mrvl_88e1111_1g",
  .probe = gbe_link_probe_88e1111_1g,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_88e1111_1g,
  .get_state = gbe_link_get_state_88e1111_1g,
  .intr = gbe_link_intr_mrvl,
};


//
// Marvell 88E1111, as used on the AMC card.  Very similar to the 1 Gbps
// fiber version above, but doesn't try to use autonegotiation, since the
// switch on the other end doesn't do it.
//

/** Probe routine. */
static int
gbe_link_probe_88e1111_amc(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x1410cc0)
    return 0;

  // (r27 & 0x8000) == 0x8000: Copper/Fiber Autoselect Disabled
  // (r27 & 0x000F) == 3: RGMII->Fiber mode
  if ((gbe_phy_rd(lc, 27) & 0x800F) != 0x8003)
    return 0;

  //
  // See the comments in gbe_link_probe_88e1111_1g for why we do this
  // particular check here.
  //
#if 0
  uint32_t *sfp_ptr;
  uint32_t sfp_desc;

  sfp_desc = bi_getparam(BI_TYPE_SFP, lc->instance, &sfp_ptr, NULL);
  if (sfp_desc != BI_NULL &&
      BI_WDS(sfp_desc) >= 3 &&
      (sfp_ptr[2] & 0xFF) != BI_SIGNAL_NONE)
    return 0;
#endif

#ifdef LINK_DEBUG
  tprintf("%s is Marvell 88E1111, strapped for 1 Gbps fiber, AMC config\n",
          lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_1G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_HDX | ENET_LINK_FDX;

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_88e1111_amc(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  uint32_t speed_bits_9 = 0x0200;          // 1G full
  uint32_t speed_bits_4 = 0;

  // Clear and then enable PHY interrupts (speed/duplex/link status
  // changed, auto-negotiation complete, and page received).
  (void) gbe_phy_rd(lc, 19);
  gbe_phy_wr(lc, 18, 0x7C00);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  gbe_phy_wr(lc, 0x14, 0x0ce2);

  // Reset.
  gbe_phy_wr(lc, 0, 0x8140);

  // Poll for reset to complete.
  for (int x = 0; x < 100; x++)
    if (!(gbe_phy_rd(lc, 0) & 0x8000))
      break;

  // If it didn't come out of reset then it's probably not coming up.
  if (gbe_phy_rd(lc, 0) & 0x8000)
    return ENET_EIO;

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_88e1111_amc_link_ops =
{
  .name = "gbe_mrvl_88e1111_amc",
  .probe = gbe_link_probe_88e1111_amc,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_88e1111_amc,
  .get_state = gbe_link_get_state_88e1111_1g,
  .intr = gbe_link_intr_mrvl,
};


//
// Marvell 88E1111, regular autonegotiation.
//

/** Probe routine. */
static int
gbe_link_probe_88e1111(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x1410cc0)
    return 0;

  // (r27 & 0x8000) == 0x8000: Copper/Fiber Autoselect Disabled
  // (r27 & 0x000F) == 3: RGMII->Fiber mode
  if ((gbe_phy_rd(lc, 27) & 0x800F) == 0x8003)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Marvell 88E1111\n", lc->device_name);
#endif

  gbe_link_probe_common(lc);

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_88e1111(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  // Clear and then enable PHY interrupts (speed/duplex/link status
  // changed, auto-negotiation complete, and page received).
  (void) gbe_phy_rd(lc, 19);
  gbe_phy_wr(lc, 18, 0x7C00);

  // Caution: These bits require a soft reset to take effect.
  gbe_phy_wr(lc, 0x14, 0x0ce2);
  gbe_phy_wr(lc, 0, 0x8140);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  // Restart negotiation.
  gbe_phy_wr(lc, 0, 0x9340);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_88e1111_link_ops =
{
  .name = "gbe_mrvl_88e1111",
  .probe = gbe_link_probe_88e1111,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_88e1111,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_mrvl,
};


//
// Broadcom BCM5481.
//

/** Probe routine. */
static int
gbe_link_probe_bcm5481(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x143bca0)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Broadcom BCM5481\n", lc->device_name);
#endif

  gbe_link_probe_common(lc);

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_bcm5481(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  //
  // Clear and then enable PHY interrupts (link status/speed change,
  // duplex mode change, local/remote receive status change).  Note
  // that these are interrupt mask bits, not enable bits.
  //
  (void) gbe_phy_rd(lc, 26);
  gbe_phy_wr(lc, 27, 0xFFC1);

  // Caution: These bits are reset by a soft reset.
  //          The auto-neg does NOT issue a reset.
  gbe_phy_wr(lc, 0x18, 0xf187);
  gbe_phy_wr(lc, 0x1c, 0x8e00);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  // Restart negotiation.
  gbe_phy_wr(lc, 0, 0x1340);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_bcm5481_link_ops =
{
  .name = "gbe_brcm_bcm5481",
  .probe = gbe_link_probe_bcm5481,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_bcm5481,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_brcm,
};


//
// Broadcom BCM5482s.
//

/** Probe routine. */
static int
gbe_link_probe_bcm5482s(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x143bcb0)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Broadcom BCM5482s\n", lc->device_name);
#endif

  gbe_link_probe_common(lc);

  lc->possible_state |= ENET_LINK_LOOP_PHY;

  return 1;
}


/** Translate a BIB LED code to a BCM5482s or BCM54280 register setting. */
static int
bcm54xx_led2code(int led, int default_code)
{
  switch (led)
  {
  case BI_ENET_LED_CFG__VAL_ON:
    return 0xf;

  case BI_ENET_LED_CFG__VAL_LINK:
    return 0x5;

  case BI_ENET_LED_CFG__VAL_ACT:
    return 0x3;

  case BI_ENET_LED_CFG__VAL_ACT_TX:
    return 0x2;

  case BI_ENET_LED_CFG__VAL_ACT_RX:
    return 0x8;

  case BI_ENET_LED_CFG__VAL_FDX:
    return 0x4;

  case BI_ENET_LED_CFG__VAL_INTR:
    return 0x6;

  case BI_ENET_LED_CFG__VAL_DEFAULT:
    return default_code;

  case BI_ENET_LED_CFG__VAL_OFF:
  default:
    return 0xe;
  }
}


/** Begin configuration routine. */
static int
gbe_link_start_config_bcm5482s(enet_link_config_t* lc, uint32_t new_config)
{
  uint16_t data;

  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  //
  // Clear and then enable PHY interrupts (link status/speed change,
  // duplex mode change, local/remote receive status change).  Note
  // that these are interrupt mask bits, not enable bits.
  //
  (void) gbe_phy_rd(lc, 26);
  gbe_phy_wr(lc, 27, 0xFFC1);

  //
  // Set link LED mode.
  //
  gbe_phy_wr(lc, 0x1c, 0x0800);
  data = gbe_phy_rd(lc, 0x1c);
  data |= 1;
  gbe_phy_wr(lc, 0x1c, 0x8000 | data);
  //
  // Set LED3 and LED1.
  //
  data = 0xb400;
  data |= bcm54xx_led2code(lc->leds[2], 1) << 4; // led3
  data |= bcm54xx_led2code(lc->leds[0], 0);      // led1
  gbe_phy_wr(lc, 0x1c, data);
  //
  // Set LED2 and LED4.
  //
  data = 0xb800;
  data |= bcm54xx_led2code(lc->leds[1], 6) << 4; // led2
  data |= bcm54xx_led2code(lc->leds[3], 3);      // led4
  gbe_phy_wr(lc, 0x1c, data);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  //
  // Finish configuration.
  //
  if (new_config & ENET_LINK_LOOP_PHY)
  {
    //
    // We're doing PHY loopback.  We can't autonegotiate in this case, so
    // we pick the highest speed allowed, and configure it into the PHY
    // with autonegotiation diabled and the loopback bit set.
    //
    uint32_t reg_0;

    if (new_config & ENET_LINK_1G)
      reg_0 = 0x4140;
    else if (new_config & ENET_LINK_100M)
      reg_0 = 0x6100;
    else   // Must be ENET_LINK_10M
      reg_0 = 0x4100;

    gbe_phy_wr(lc, 0, reg_0);
#ifdef LINK_DEBUG
    tprintf("%s enabling PHY loopback\n", lc->device_name);
#endif
  }
  else
  {
    //
    // No loopback, just restart negotiation.
    //
    gbe_phy_wr(lc, 0, 0x1340);
  }

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_bcm5482s_link_ops =
{
  .name = "gbe_brcm_bcm5482s",
  .probe = gbe_link_probe_bcm5482s,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_bcm5482s,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_brcm,
};


//
// Broadcom BCM54280.
//

/** Probe routine. */
static int
gbe_link_probe_bcm54280(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0x7) != 0x600d8440)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Broadcom BCM54280\n", lc->device_name);
#endif

  gbe_link_probe_common(lc);

  lc->possible_state |= ENET_LINK_LOOP_PHY | ENET_LINK_LOOP_EXT;

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_bcm54280(enet_link_config_t* lc, uint32_t new_config)
{
  uint16_t data;

  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  //
  // Clear and then enable PHY interrupts (link status/speed change,
  // duplex mode change, local/remote receive status change).  Note
  // that these are interrupt mask bits, not enable bits.
  //
  (void) gbe_phy_rd(lc, 26);
  gbe_phy_wr(lc, 27, 0xFFC1);

  //
  // Set link LED mode.
  //
  gbe_phy_wr(lc, 0x1c, 0x0800);
  data = gbe_phy_rd(lc, 0x1c);
  data |= 1;
  gbe_phy_wr(lc, 0x1c, 0x8000 | data);
  //
  // Set LED0 and LED1.
  //
  data = 0xb400;
  data |= bcm54xx_led2code(lc->leds[1], 1) << 4; // led1
  data |= bcm54xx_led2code(lc->leds[0], 0);      // led0
  gbe_phy_wr(lc, 0x1c, data);
  //
  // Set LED2 and LED3.
  //
  data = 0xb800;
  data |= bcm54xx_led2code(lc->leds[3], 6) << 4; // led3
  data |= bcm54xx_led2code(lc->leds[2], 3);      // led2
  gbe_phy_wr(lc, 0x1c, data);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  //
  // Finish configuration.
  //
  if (new_config & ENET_LINK_LOOP_PHY)
  {
    //
    // We're doing PHY loopback.  We can't autonegotiate in this case, so
    // we pick the highest speed allowed, and configure it into the PHY
    // with autonegotiation diabled and the loopback bit set.
    //
    uint32_t reg_0;

    if (new_config & ENET_LINK_1G)
      reg_0 = 0x4140;
    else if (new_config & ENET_LINK_100M)
      reg_0 = 0x6100;
    else   // Must be ENET_LINK_10M
      reg_0 = 0x4100;

    gbe_phy_wr(lc, 0, reg_0);
#ifdef LINK_DEBUG
    tprintf("%s enabling PHY loopback\n", lc->device_name);
#endif
  }
  else if (new_config & ENET_LINK_LOOP_EXT)
  {
    //
    // XXX The docs claim you need to do a soft reset to get out of the
    // external loopback mode; taking the link down seems to work too.
    // You could argue we ought to be detecting the case where the user
    // just turns off the loopback bit, and doing a reset/reconfig.
    //
    if (new_config & ENET_LINK_1G)
    {
      // Enable 1000BASE-T master mode.
      gbe_phy_wr(lc, 9, 0x1800);
      // Force 1000BASE-T full duplex.
      gbe_phy_wr(lc, 0, 0x0140);
    }
    else if (new_config & ENET_LINK_100M)
    {
      // Force 100BASE-T full duplex.
      gbe_phy_wr(lc, 0, 0x2100);
    }
    else   // Must be ENET_LINK_10M
    {
      // Force 10BASE-T full duplex.
      gbe_phy_wr(lc, 0, 0x0100);
    }
    // Enable loopback mode with loopback plug.  Set up to point to the
    // shadow register, then read it, then write it.
    gbe_phy_wr(lc, 0x18, 7);
    data = gbe_phy_rd(lc, 0x18);
    data |= 1 << 15;
    gbe_phy_wr(lc, 0x18, data);
#ifdef LINK_DEBUG
    tprintf("%s enabling external loopback\n", lc->device_name);
#endif
  }
  else
  {
    //
    // No loopback, just restart negotiation.
    //
    gbe_phy_wr(lc, 0, 0x1340);
  }

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_bcm54280_link_ops =
{
  .name = "gbe_brcm_bcm54280",
  .probe = gbe_link_probe_bcm54280,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_bcm54280,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_brcm,
};


//
// Vitesse VSC8211, regular autonegotiation.
//

/** Probe routine. */
static int
gbe_link_probe_vsc8211(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0xfc4b0)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s Vitesse VSC8211\n", lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_1G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_LOOP_PHY |
                        ENET_LINK_HDX | ENET_LINK_FDX;

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_vsc8211(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  if ((lc->phytype & 0xF) == 1)
  {
    //
    // Magic initialization steps advised for rev C of the VSC8211;
    // see page 161 of the data sheet.  This is supposed to be done
    // after power-up or reset.  Since we just powered up in
    // gbe_link_start_config_common(), and that's the only place
    // we power up, this seems like the right place.
    //
    gbe_phy_wr(lc, 31, 0x2a30);
    gbe_phy_wr(lc, 8,  0x0212);
    gbe_phy_wr(lc, 31, 0x52b5);
    gbe_phy_wr(lc, 2,  0x000f);
    gbe_phy_wr(lc, 1,  0x472a);
    gbe_phy_wr(lc, 0,  0x8fa4);
    gbe_phy_wr(lc, 31, 0x2a30);
    gbe_phy_wr(lc, 8,  0x0012);
    gbe_phy_wr(lc, 31, 0x0000);
  }

#if 0
  //
  // This code dumps out the EEPROM attached to the PHY.
  //
  if (new_config == 0x1)
  {
    const int nbytes = 128;
    // set extended mode
    gbe_phy_wr(lc, 31, 1);

    uint32_t prev_val =
      gbe_phy_rd(lc, 21) & 0x3FFF;

    for (int i = 0; i < nbytes; i++)
    {
      if ((i & 15) == 0)
        tprintf("%s eeprom %04x:", lc->device_name, i);

      gbe_phy_wr(lc, 21,
                  prev_val |
                  (1 << 13) |  // do op
                  (1 << 12) |  // op is read
                  i & 0x7FF    // at address i
                  );

      while ((gbe_phy_rd(lc, 21) & (1 << 11)) == 0)
        ;

      uint32_t val =
        gbe_phy_rd(lc, 22) >> 8;

      printf(" %02x", val);

      if (((i+1) & 15) == 0)
        printf("\n");
    }

    // clear extended mode
    gbe_phy_wr(lc, 31, 0);
  }
#endif

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  //
  // Clear and then enable PHY interrupts (interrupt enable,
  // link state change, autonegotiation done).
  //
  (void) gbe_phy_rd(lc, 26);
  gbe_phy_wr(lc, 25, 0xA400);

  //
  // This PHY supports both a direct copper UTP connection, as well as a
  // connection to an SFP module for fiber usage.  Only one can be used
  // at a time.  We currently set up to auto-detect the media, with
  // a preference for direct copper, although our current boards don't
  // actually implement copper.
  //
  gbe_phy_wr(lc, 23, 0x24); // PHY Control Register#1
  gbe_phy_wr(lc, 0, 0x8140);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  //
  // Reset and configure.
  //
  if (new_config & ENET_LINK_LOOP_PHY)
  {
    //
    // We're doing PHY loopback, so reset and disable autonegotiation, then
    // set the loopback bit.
    //
    gbe_phy_wr(lc, 0, 0x8140);
    gbe_phy_wr(lc, 0, 0x4140);
#ifdef LINK_DEBUG
    tprintf("%s enabling PHY loopback\n", lc->device_name);
#endif
  }
  else
  {
    //
    // No loopback, just reset and restart autonegotiation.
    //
    gbe_phy_wr(lc, 0, 0x9340);
  }

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_vsc8211_link_ops =
{
  .name = "gbe_vsc_vsc8211",
  .probe = gbe_link_probe_vsc8211,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_vsc8211,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_vsc,
};

//
// Broadcom BCM8747/8727, 1 GbE mode.
//

/** Probe routine. */
static int
gbe_link_probe_bcm87x7(enet_link_config_t* lc)
{
  //
  // On some other PHYs, we don't compare the low 4 bits of the phytype
  // since it's supposedly a mask ID; the data sheet for this part doesn't
  // say that, so we look at all of the bits.
  //
  if (!lc->gbe)
    return 0;

  if (IS_BCM8747(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s is Broadcom BCM8747, 1 Gbps mode\n", lc->device_name);
#endif
  }
  else if (IS_BCM8727(lc))
  {
#ifdef LINK_DEBUG
    tprintf("%s is Broadcom BCM8727, 1 Gbps mode\n", lc->device_name);
#endif
  }
  else
    return 0;

  lc->no_auto_neg = 1;
  lc->possible_state &= ENET_LINK_1G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_HDX | ENET_LINK_FDX |
                        ENET_LINK_LOOP_PHY;

  return 1;
}


/** Initialization routine. */
static int
gbe_link_init_bcm87x7(enet_link_config_t* lc)
{
  //
  // Do common init flow first.
  //
  int rv = bcm87x7_phy_init(lc);

  if (rv)
    return rv;

  //
  // Many PHYs come out of reset trying to bring the link up.
  // We don't want this, so disable the laser on the optical module.
  //
  xgbe_phy_wr(lc, 1, 0x0009, 1);

  //
  // These are the shim interrupts we want to pay attention to whenever the
  // link is supposed to be up.  We enable the MAC interrupt, but in practice
  // we don't actually see it, just the external (PHY) interrupt.
  //
  lc->mac_intrs = MPIPE_GBE_INTERRUPT_STATUS__LINK_CHANGE_MASK;

  return 0;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_bcm87x7(enet_link_config_t* lc, uint32_t new_config)
{
  //
  // First ensure that the microcode is loaded.
  //
  int rv = bcm87x7_load_ucode(lc);
  if (rv)
    return rv;

  //
  // We're doing more or less the same stuff here that
  // gbe_link_start_config_common does; unfortunately it uses clause 22
  // accesses to the PHY and we have to use clause 45 accesses, plus some
  // of the register addresses are slightly different.
  //
  if ((new_config & ENET_LINK_SPEED) == 0)
  {
    //
    // Take the link down.
    //

    // Disable the MAC.
    mac_disable(lc);

    // Disable the laser on the optical module.
    xgbe_phy_wr(lc, 1, 0x0009, 1);

    // Note new link state.
    enet_new_link_state(lc, 0);

    // We're done.
    return 0;
  }

  //
  // If the link was already being brought up, and the MAC loopback state is
  // being changed, then change it and return.  Otherwise, we'll set up
  // loopback later if it's enabled.
  //
  if ((lc->desired_state & ENET_LINK_SPEED) &&
      ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_MAC))
  {
    loopback_config(lc, new_config & ENET_LINK_LOOP_MAC);
    return 0;
  }

  //
  // Configure the PHY to set the EDC mode based on the module type.
  // (FIXME: not actually sure this even works, or is necessary, for 1 GbE,
  // but we'll leave it in for now.)
  //
  int rd_data = xgbe_phy_rd(lc, 1, 0xC82B);
  rd_data |= 1 << 10;
  xgbe_phy_wr(lc, 0xC82B, 1, rd_data);

  //
  // Enable the laser on the optical module; set the chip to forced 1 Gbps
  // mode, no autonegotiation.
  //
  xgbe_phy_wr(lc, 1, 0x0009, 0);
  xgbe_phy_wr(lc, 1, 0x0000, 0x0040);

  //
  // If the PHY loopback state is being changed, then change it.  Then, if
  // that's the only thing that's being changed, return.
  //
  if ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_PHY)
  {
    uint32_t pma_ctl = xgbe_phy_rd(lc, 1, 0);

    // Set PHY loopback properly.
    if (new_config & ENET_LINK_LOOP_PHY)
      pma_ctl |= 1;
    else
      pma_ctl &= ~1;

    xgbe_phy_wr(lc, 1,  0, pma_ctl);

#ifdef LINK_DEBUG
    tprintf("%s %s PHY loopback\n", lc->device_name,
            (new_config & ENET_LINK_LOOP_PHY) ? "enabling" : "disabling");
#endif

    if (!((new_config ^ lc->desired_state) & ~ENET_LINK_LOOP_PHY))
      return 0;
  }

  //
  // Enable the MAC.
  //
  mac_enable(lc);

  if (lc->link_does_intr)
  {
    //
    // Clear and then enable the one interrupt we care about within the
    // MAC (LINK_CHANGE).
    //
    (void) cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_STATUS);
    cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_ENABLE,
           MPIPE_GBE_INTERRUPT_STATUS__LINK_CHANGE_MASK);
  }

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.  Note that
  // we return early in this case since we don't need to configure the PHY.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
  {
    loopback_config(lc, 1);
    return 0;
  }

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


/** Get state routine. */
static uint32_t
gbe_link_get_state_bcm87x7(enet_link_config_t* lc)
{
  //
  // If we're not trying to bring the link up, we may have never
  // initialized the PHY, so it's not clear we can trust the results;
  // just say it's down.
  //
  if (!(lc->desired_state & ENET_LINK_SPEED))
    return 0;

  //
  // At this point, we know we want the link to be up, so if we've enabled
  // MAC loopback, the link is by definition up.
  //
  if (lc->desired_state & ENET_LINK_LOOP_MAC)
    return ENET_LINK_1G | ENET_LINK_LOOP_MAC | ENET_LINK_FDX;

  //
  // Check state.  The mPIPE side of the mPIPE-PHY link has to be up, and
  // the outgoing link has to be up (device 7).  We don't check the host
  // side of the link (device 4), since it seems to lag the other
  // indicators and there's no interrupt when it clicks on; thus, if we
  // make it part of this check the link never appears to come up even
  // though it eventually does.
  //
  if (!gbe_link_pcs_down(lc) &&
      ((xgbe_phy_rd(lc, 7, 0x8304) & 0x2) ||
       (xgbe_phy_rd(lc, 7, 0x8304) & 0x2)))
    return ENET_LINK_1G | ENET_LINK_FDX;

  return 0;
}

/** Interrupt routine. */
static int
gbe_link_intr_bcm87x7(enet_link_config_t* lc, int dummy)
{
  //
  // Figure out what interrupts we got; if none, return.
  //
  uint_reg_t intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                            MPIPE_GBE_INTERRUPT_STATUS);

  if (!(intrs & lc->mac_intrs))
    return 0;

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_STATUS,
         intrs);

#ifdef LINK_DEBUG
  tprintf("%s intr %#llx\n", lc->device_name, intrs);
#endif

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine to
  // notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  gbe_config_mac_speed(lc, new_state);
  enet_new_link_state(lc, new_state);

  return 1;
}


static const enet_link_ops_t gbe_bcm87x7_link_ops =
{
  .name = "gbe_brcm_bcm87x7",
  .probe = gbe_link_probe_bcm87x7,
  .init = gbe_link_init_bcm87x7,
  .start_config = gbe_link_start_config_bcm87x7,
  .get_state = gbe_link_get_state_bcm87x7,
  .intr = gbe_link_intr_bcm87x7,
  .get_module_eeprom = bcm87x7_get_module_eeprom,
};


//
// Vitesse VSC8488.
//

/** Init routine. */
static int
vsc8488_phy_init(enet_link_config_t* lc)
{
  int val;

  // Reset
  xgbe_phy_wr(lc, 1, 0x0000, 0x8000);

  // PLL run-away workaround for 8484/8487/8488
  val = xgbe_phy_rd(lc, 1, 0x8003);
  val |= 0x1;
  xgbe_phy_wr(lc, 1, 0x8003, val);
  val &= ~0x1;
  xgbe_phy_wr(lc, 1, 0x8003, val);

  // Set GPIO functions

  // GPIO 4 - SDA_SFP[0]
  val = xgbe_phy_rd(lc, 0x1e, 0x0108);
  val = (val & ~0x7) | 0x4;
  xgbe_phy_wr(lc, 0x1e, 0x0108, val);

  // GPIO 5 - SCL_SFP[0]
  val = xgbe_phy_rd(lc, 0x1e, 0x010a);
  val = (val & ~0x7) | 0x4;
  xgbe_phy_wr(lc, 0x1e, 0x010a, val);

  // GPIO 6 - SFP0_TXDIS
  val = xgbe_phy_rd(lc, 0x1e, 0x0124);
  val = (val & ~0x7) | 0x3;
  xgbe_phy_wr(lc, 0x1e, 0x0124, val);

  xgbe_phy_wr(lc, 0x1e, 0x0125, 0x49);

  // GPIO 7 - SFP1_TXDIS
  val = xgbe_phy_rd(lc, 0x1e, 0x0126);
  val = (val & ~0x7) | 0x3;
  xgbe_phy_wr(lc, 0x1e, 0x0126, val);

  xgbe_phy_wr(lc, 0x1e, 0x0127, 0x4b);

  // GPIO 10 - SDA_SFP[1]
  val = xgbe_phy_rd(lc, 0x1e, 0x012c);
  val = (val & ~0x7) | 0x4;
  xgbe_phy_wr(lc, 0x1e, 0x012c, val);

  // GPIO 11 - SCL_SFP[1]
  val = xgbe_phy_rd(lc, 0x1e, 0x012e);
  val = (val & ~0x7) | 0x4;
  xgbe_phy_wr(lc, 0x1e, 0x012e, val);

  // Regular LAN mode with a single Ref clock: XREFCLK @ 156.25Mhz
  xgbe_phy_wr(lc, 2, 0x0007, 0);         // Disable WAN MODE
  xgbe_phy_wr(lc, 0x1e, 0x7f10, 0x0100); // Global XREFCLK from CMU clkgen
  xgbe_phy_wr(lc, 0x1e, 0x7f11, 0X00d2); // Global Refclk CMU 14Mhz BW control
  xgbe_phy_wr(lc, 1, 0x8017, 0x0020);    // PMA TX Rate control. CMU Varclk sel
  xgbe_phy_wr(lc, 1, 0x8019, 0x0008);    // PMA RX Rate control
  xgbe_phy_wr(lc, 1, 0xB0C0, 0x002A);    // DC Offset Alarm Mode
  xgbe_phy_wr(lc, 1, 0x8014, 0x0300);    // TX output driver control current

  // Don't invert LOPC signal
  val = xgbe_phy_rd(lc, 1, 0xa201);
  val |= 1 << 2;
  xgbe_phy_wr(lc, 1, 0xa201, val);

  // Force LOF/SEF on LOPC
  val = xgbe_phy_rd(lc, 2, 0xec30);
  val |= 1 << 8;
  xgbe_phy_wr(lc, 2, 0xec30, val);

  // Force AIS-L and RDI-L on LOPC/LOS/LOF
  val = xgbe_phy_rd(lc, 2, 0xedff);
  val |= 0x7F;
  xgbe_phy_wr(lc, 2, 0xedff, val);

  return 0;
}


/** Probe routine. */
static int
xgbe_link_probe_vsc8488(enet_link_config_t* lc)
{
  if (lc->gbe || lc->phytype != 0x00070400)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Vitesse VSC8488, 10 Gbps mode\n", lc->device_name);
#endif

  lc->possible_state &= ENET_LINK_10G;
  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_FDX | ENET_LINK_LOOP_PHY;

  return 1;
}


/** Initialization routine. */
static int
xgbe_link_init_vsc8488(enet_link_config_t* lc)
{
  //
  // Do common init flow first.
  //
  int rv = vsc8488_phy_init(lc);

  if (rv)
    return rv;

  // Disable transmit (we'll enable this again later).
  xgbe_phy_wr(lc, 1, 0x0009, 1);

  //
  // These are the interrupts we want to pay attention to whenever the
  // link is supposed to be up.
  //
  // Note: we're explicitly ignoring the PHY interrupt here, for two
  // reasons.  First, it turns out not to add any value; we get accurate
  // link status without it.  Second, some very early versions of the
  // TILEmpower-Gx I/O module have an incorrect BIB which states that the
  // PHY interrupt is not inverted, even though it is.  If we enable the
  // PHY interrupt on those machines we get infinite interrupts.
  //
  // FIXME Do we want LPI here?
  //
  lc->mac_intrs =
        MPIPE_XAUI_INTERRUPT_STATUS__PCS_ALIGNMENT_CHANGED_MASK |
        MPIPE_XAUI_INTERRUPT_STATUS__LINK_STS_CHANGE_MASK;

  //
  // Use BIB data to configure the LEDs as appropriate.  The chip only
  // supports two settings, which we check for here explicitly, plus
  // fixed values, which we use in all other cases.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };

  if (lc->leds[0] == BI_ENET_LED_CFG__VAL_LINK &&
      lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT)
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_LINK_ACT;
  }
  else if (lc->leds[0] == BI_ENET_LED_CFG__VAL_ACT_TX &&
           lc->leds[1] == BI_ENET_LED_CFG__VAL_ACT_RX)
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_TX_RX;
  }
  else
  {
    mxpc.led_mode = MPIPE_XAUI_PCS_CTL__LED_MODE_VAL_SW;

    int led0 = (lc->leds[0] == BI_ENET_LED_CFG__VAL_ON) ? 1 : 0;
    int led1 = (lc->leds[1] == BI_ENET_LED_CFG__VAL_ON) ? 2 : 0;

    mxpc.led_ovd_val = led1 | led0;
  }

  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  return 0;
}


/** Begin configuration routine. */
static int
xgbe_link_start_config_vsc8488(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
  tprintf("%s link_start_config(new_config=%#x)\n", lc->device_name,
          new_config);
#endif

  if ((new_config & ENET_LINK_10G) == 0)
  {
    //
    // Disable the MAC.
    //
    mac_disable(lc);

    //
    // Disable loopback.
    //
    loopback_config(lc, 0);

    // Disable the laser on the optical module.
    xgbe_phy_wr(lc, 1, 0x0009, 1);

    // Note new link state.
    enet_new_link_state(lc, 0);
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (down) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  //
  // At this point, we know the desired state for the link is up.  If we're
  // turning off SERDES loopback, do so, and also disable the MAC; we'll
  // reenable it below.  Just disabling loopback and doing nothing else
  // seems to cause link flapping.  If we're enabling_loopback, that will
  // get done below after we enable the MAC.
  //
  if (!(new_config & ENET_LINK_LOOP_MAC) && (lc->desired_state &
                                             ENET_LINK_LOOP_MAC))
  {
    mac_disable(lc);
    loopback_config(lc, 0);
  }

  // Enable the laser on the optical module; configure PHY for 10 Gbps
  xgbe_phy_wr(lc, 1, 0x0009, 0);
  xgbe_phy_wr(lc, 1, 0x0000, 0x2040);

  //
  // If the PHY loopback state is being changed, then change it.  Then, if
  // that's the only thing that's being changed, return.
  //
  if ((new_config ^ lc->desired_state) & ENET_LINK_LOOP_PHY)
  {
    uint32_t pma_ctl = xgbe_phy_rd(lc, 1, 0);

    // Set PHY loopback properly.
    if (new_config & ENET_LINK_LOOP_PHY)
      pma_ctl |= 1;
    else
      pma_ctl &= ~1;

    xgbe_phy_wr(lc, 1,  0, pma_ctl);

#ifdef LINK_DEBUG
    tprintf("%s %s PHY loopback\n", lc->device_name,
            (new_config & ENET_LINK_LOOP_PHY) ?  "enabling" : "disabling");
#endif

    if (!((new_config ^ lc->desired_state) & ~ENET_LINK_LOOP_PHY))
      return 0;
  }

  //
  // Set the speed appropriately and enable the MAC, which should bring
  // up the link.
  //
  MPIPE_XAUI_PCS_CTL_t mxpc =
  {
    .word = cfg_rd(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL)
  };
  mxpc.double_rate = (new_config & ENET_LINK_20G) != 0;
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_PCS_CTL, mxpc.word);

  mac_enable(lc);

  //
  // Enable SERDES loopback if requested.  We have to do this after the MAC
  // is enabled, since enabling the MAC modifies the SERDES state.
  //
  if (new_config & ENET_LINK_LOOP_MAC)
    loopback_config(lc, 1);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif

  return 0;
}


/** Interrupt routine. */
static int
xgbe_link_intr_vsc8488(enet_link_config_t* lc, int dummy)
{
  //
  // Figure out what interrupts we got; if none, return.
  //
  uint_reg_t intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                            MPIPE_XAUI_INTERRUPT_STATUS);

  if (!(intrs & lc->mac_intrs))
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s intrs: 0x%llx lopc 0x%x\n",
          lc->device_name, intrs, xgbe_phy_rd(lc, 1, 0xa200));
#endif

  //
  // Reset the interrupts.  We have to do this before we actually look at
  // the status bits, so that if things are still changing, we will get
  // another interrupt.
  //
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_XAUI_INTERRUPT_STATUS, intrs);

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine
  // to notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  enet_new_link_state(lc, new_state);

  return 1;
}


static const enet_link_ops_t xgbe_vsc8488_link_ops =
{
  .name = "xgbe_vsc8488",
  .probe = xgbe_link_probe_vsc8488,
  .init = xgbe_link_init_vsc8488,
  .start_config = xgbe_link_start_config_vsc8488,
  .get_state = xgbe_link_get_state_common,
  .intr = xgbe_link_intr_vsc8488,
};


//
// Realtek RTL8211.
//

/** Probe routine. */
static int
gbe_link_probe_rtl8211(enet_link_config_t* lc)
{
  if (!lc->gbe || (lc->phytype & ~0xf) != 0x1cc910)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s is Realtek RTL8211\n", lc->device_name);
#endif

  gbe_link_probe_common(lc);

  return 1;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_rtl8211(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
  {
#ifdef LINK_DEBUG
    tprintf("%s link_start_config (common) returns 0\n", lc->device_name);
#endif
    return 0;
  }

  // Calculate speeds we'll advertise based on our configuration.
  unsigned int speed_bits_4 = 0;
  unsigned int speed_bits_9 = 0;
  gbe_speed_bits(new_config, &speed_bits_4, &speed_bits_9);

  // Clear and then enable PHY interrupts (speed/duplex/link status
  // changed, auto-negotiation complete, and page received).
  (void) gbe_phy_rd(lc, 19);
  gbe_phy_wr(lc, 18, 0x7C00);

  // Caution: These bits require a soft reset to take effect.
  gbe_phy_wr(lc, 0, 0x8140);

  // Advertise requested speeds.
  gbe_phy_wr(lc, 0x4, speed_bits_4 | 0x0001);
  gbe_phy_wr(lc, 0x9, speed_bits_9);

  // Restart negotiation.
  gbe_phy_wr(lc, 0, 0x9340);

#ifdef LINK_DEBUG
  tprintf("%s link_start_config returns 0\n", lc->device_name);
#endif
  return 0;
}


static const enet_link_ops_t gbe_rtl8211_link_ops =
{
  .name = "gbe_rtl8211",
  .probe = gbe_link_probe_rtl8211,
  .init = gbe_link_init_common,
  .start_config = gbe_link_start_config_rtl8211,
  .get_state = gbe_link_get_state_auto,
  .intr = gbe_link_intr_rtl,
};


//
// No PHY (direct SFP connection).
//

/** Probe routine. */
static int
gbe_link_probe_nophy(enet_link_config_t* lc)
{
  if (!lc->gbe || lc->has_phy)
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s has no PHY (direct SFP or onboard link)\n", lc->device_name);
#endif

  //
  // For copper SFP's, we might later change the autonegotiation settings,
  // but for fiber autoneg needs to be off, so just set it that way up
  // front.
  //
  lc->no_auto_neg = 1;

  //
  // If we have an SFP, we might be able to do 10 M or 100 M or half-duplex
  // if a copper SFP is plugged in.  (We only enable the last if the BIB
  // specified 10 M or 100 M support to avoid changing the behavior of
  // older systems.)  If it's a direct-connect link we have no chance for
  // the first two and don't care about the third.
  //
  if (lc->has_sfp_cfg)
  {
    lc->possible_state &= ENET_LINK_1G | ENET_LINK_100M | ENET_LINK_10M;
    if (lc->possible_state & (ENET_LINK_100M | ENET_LINK_10M))
      lc->possible_state |= ENET_LINK_HDX;
  }
  else
    lc->possible_state &= ENET_LINK_1G;

  lc->possible_state |= ENET_LINK_LOOP_MAC | ENET_LINK_FDX;

  return 1;
}


/** Initialization routine. */
static int
gbe_link_init_nophy(enet_link_config_t* lc)
{
  //
  // These are the interrupts we want to pay attention to whenever the
  // link is supposed to be up.
  //
  lc->mac_intrs = MPIPE_GBE_INTERRUPT_STATUS__LINK_CHANGE_MASK;

  //
  // Disable the SFP.
  //
  if (lc->has_sfp_cfg)
    set_signal(lc->sfp_cfg.tx_disable_sig, SIGNAL_ASSERT | SIGNAL_INIT);

  return 0;
}


/** Begin configuration routine. */
static int
gbe_link_start_config_nophy(enet_link_config_t* lc, uint32_t new_config)
{
  if (gbe_link_start_config_common(lc, new_config))
    return 0;

#ifdef LINK_DEBUG
  tprintf("%s link_start_config_nophy returns 0\n", lc->device_name);
#endif

  return 0;
}


/** Get state routine. */
static uint32_t
gbe_link_get_state_nophy(enet_link_config_t* lc)
{
  //
  // If we're not trying to bring the link up, just say it's down.
  //
  if (!(lc->desired_state & ENET_LINK_SPEED))
    return 0;

  //
  // At this point, we know we want the link to be up, so if we've enabled
  // MAC loopback, the link is by definition up.
  //
  if (lc->desired_state & ENET_LINK_LOOP_MAC)
    return ENET_LINK_1G | ENET_LINK_LOOP_MAC | ENET_LINK_FDX;

  //
  // You could argue that we should check LOS here, but so far it hasn't
  // seemed to be necessary.
  //
  if (gbe_link_pcs_down(lc))
    return 0;

  //
  // If we're in SGMII mode, we need to actually check the PHY status.
  //
  if (lc->sfp_sgmii)
  {
    //
    // This code is essentially doing what gbe_link_get_state_auto() does,
    // but it's changed to use sfp_phy_{rd,wr} and to swing the I2C switch.
    // We might consider making gbe_phy_{rd,wr} able to use I2C, which
    // would allow us to just use that routine here.
    //

    int bus = lc->sfp_cfg.i2c.bus;
    int inst = lc->sfp_cfg.i2c.switch_inst;
    int chan = lc->sfp_cfg.i2c.switch_chan;

    i2c_switch_swing(bus, inst, chan);

    int rv = 0;

    //
    // See if the link is up.  Note that the PHY bit we're looking at
    // latches in the "down" position, so we may need to read it twice.
    //
    if ((sfp_phy_rd(bus, 1) & 0x4) || (sfp_phy_rd(bus, 1) & 0x4))
    {
      //
      // It's up; determine the speed.
      //
      uint32_t reg0 = sfp_phy_rd(bus, 0);

      //
      // If the auto-negotiation bit is off, then just figure out what the
      // configured speed is.  This happens in PHY loopback mode.
      //
      if (!(reg0 & 0x1000))
      {
        uint32_t duplex = (reg0 & 0x100) ? ENET_LINK_FDX : ENET_LINK_HDX;

        if (reg0 & 0x40)
          rv = duplex | ENET_LINK_1G;
        else if (reg0 & 0x2000)
          rv = duplex | ENET_LINK_100M;
        else
          rv = duplex | ENET_LINK_10M;
      }
      else
      {
        //
        // Otherwise, see what we negotiated.
        //
        uint32_t advSpeed = sfp_phy_rd(bus, 9);
        uint32_t partnerSpeed = sfp_phy_rd(bus, 10);

        if ((advSpeed & 0x0200) && (partnerSpeed & 0x0800))
          rv = ENET_LINK_FDX | ENET_LINK_1G;
        else if ((advSpeed & 0x0100) && (partnerSpeed & 0x0400))
          rv = ENET_LINK_HDX | ENET_LINK_1G;
        else
        {
          advSpeed = sfp_phy_rd(bus, 4);
          partnerSpeed = sfp_phy_rd(bus, 5);
          if (advSpeed & partnerSpeed & 0x0100)
            rv = ENET_LINK_FDX | ENET_LINK_100M;
          else if (advSpeed & partnerSpeed & 0x0080)
            rv = ENET_LINK_HDX | ENET_LINK_100M;
          else if (advSpeed & partnerSpeed & 0x0040)
            rv = ENET_LINK_FDX | ENET_LINK_10M;
          else if (advSpeed & partnerSpeed & 0x0020)
            rv = ENET_LINK_HDX | ENET_LINK_10M;
        }
      }
    }

    i2c_switch_release(bus, inst);

    return rv;
  }
  else
    // If we aren't in SGMII mode, we must be up, and must be 1G/FDX.
    return ENET_LINK_1G | ENET_LINK_FDX;
}


/** Interrupt routine. */
static int
gbe_link_intr_nophy(enet_link_config_t* lc, int dummy)
{
  //
  // Figure out what interrupts we got, then clear them.
  //
  uint_reg_t intrs = cfg_rd(lc->shim_port, lc->mac_pa,
                            MPIPE_GBE_INTERRUPT_STATUS);
  cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_STATUS,
         intrs);

  if (lc->has_sfp_cfg)
  {
    //
    // Clear SFP signal interrupt, if any.  Note that we print whether or
    // not the interrupts happened if debugging is enabled, but we don't
    // actually depend upon that information; we check the current signal
    // values later and use those instead.
    //
    int mod_abs_intr = get_clear_signal_intr(lc->sfp_cfg.mod_abs_sig,
                                             SIGNAL_ASSERT |
                                             SIGNAL_DEASSERT);
    int rx_los_intr = get_clear_signal_intr(lc->sfp_cfg.rx_los_sig,
                                            SIGNAL_ASSERT |
                                            SIGNAL_DEASSERT);

    if (!((intrs & lc->mac_intrs) | mod_abs_intr | rx_los_intr))
      return 0;

#ifdef LINK_DEBUG
    tprintf("%s mod_abs_intr %d rx_los_intr %d\n", lc->device_name,
            mod_abs_intr, rx_los_intr);
#endif

    //
    // If the module was plugged in, we might get some more interrupts due
    // to contact bounce; also it might take a little bit for the EEPROM to
    // be ready.  Delay a little, then re-read and re-clear the interrupts.
    //
    if (mod_abs_intr & SIGNAL_DEASSERT)
    {
#ifdef LINK_DEBUG
    tprintf("%s debouncing MOD_ABS\n", lc->device_name);
#endif
      drv_udelay(200);
      mod_abs_intr |= get_clear_signal_intr(lc->sfp_cfg.mod_abs_sig,
                                            SIGNAL_ASSERT | SIGNAL_DEASSERT);
      rx_los_intr |= get_clear_signal_intr(lc->sfp_cfg.rx_los_sig,
                                           SIGNAL_ASSERT | SIGNAL_DEASSERT);
    }

    //
    // If the module was removed at any point, it might have been swapped,
    // and at the very least it lost power, so it needs to be reprobed
    // and reconfigured.
    //
    if (mod_abs_intr & SIGNAL_ASSERT)
    {
#ifdef LINK_DEBUG
    tprintf("%s SFP module removed\n", lc->device_name);
#endif
      lc->sfp_cu = ENET_SFP_CU_UNK;
      lc->sfp_sgmii = 0;
    }

    //
    // When we aren't getting any actual signal input, the MAC will
    // sometimes think the link has come up and then gone right back down.
    // This causes unnecessary interrupts (perhaps many per second).  To
    // prevent this, if we can see the MOD_ABS and RX_LOS signals, we
    // monitor them, and turn off the MAC interrupts while they're in a
    // state which would prevent the link from coming up (no module or no
    // signal).
    //
    // Note that if we're in MAC loopback mode we ignore MOD_ABS/RX_LOS;
    // we don't really care if the module is present in that case.
    //
    int mod_abs = (get_signal(lc->sfp_cfg.mod_abs_sig, SIGNAL_ASSERT) > 0);
    int rx_los = (get_signal(lc->sfp_cfg.rx_los_sig, SIGNAL_ASSERT) > 0);

#ifdef LINK_DEBUG
    tprintf("%s intr %#llx mod_abs %d rx_los %d\n", lc->device_name, intrs,
            mod_abs, rx_los);
#endif

    //
    // If the module is present, and we haven't probed it so far, do so.
    //
    if (!mod_abs && lc->sfp_cu == ENET_SFP_CU_UNK)
    {
#ifdef LINK_DEBUG
    tprintf("%s probing newly hotplugged SFP\n", lc->device_name);
#endif
      sfp_test_config_cu(lc, lc->desired_state);
    }

    if ((mod_abs || rx_los) && !(lc->desired_state & ENET_LINK_LOOP_MAC))
    {
      //
      // Mask the interrupts we had enabled.
      //
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_DISABLE,
             lc->mac_intrs);

      //
      // If the link _can't_ be up, we don't want to even check the link
      // state; it's possible that we could get really unlucky, and it
      // could look up when we checked, only to go down right after.  If
      // that happens, we won't see the state change, since we just turned
      // off the MAC interrupts, so the link would appear to be up even
      // though it isn't.  Instead, we just say it's down; we'll come
      // back here when the signal bits change and re-check things.
      //
      enet_new_link_state(lc, 0);

      return 1;
    }
    else
    {
      //
      // Unmask the MAC interrupts we care about.  (It would be nice to
      // keep track of whether we had interrupts disabled, and only do this
      // if they were, but it's not really critical.)
      //
      cfg_wr(lc->shim_port, lc->mac_pa, MPIPE_GBE_INTERRUPT_ENABLE,
             lc->mac_intrs);

      //
      // Fall through to the code which gets the current state.
      //
    }
  }
  else
  {
    if (!(intrs & lc->mac_intrs))
      return 0;

#ifdef LINK_DEBUG
    tprintf("%s intr %#llx\n", lc->device_name, intrs);
#endif
  }

  //
  // Get the current state, and call the state change routine.  If it
  // hasn't actually changed, it's the job of the state change routine to
  // notice that and not do anything.
  //
  uint32_t new_state = lc->ops->get_state(lc);
  gbe_config_mac_speed(lc, new_state);
  enet_new_link_state(lc, new_state);

  return 1;
}


static int
gbe_i2c_rd_nophy(enet_link_config_t* lc, int i2c_addr, int offset, int len,
                 void* buf)
{
  return i2c_rd_bus(lc->sfp_cfg.i2c.bus, i2c_addr, offset, len, buf);
}


static int
gbe_get_module_eeprom_nophy(enet_link_config_t* lc, int* type,
                            int offset, void* buf, int len)
{
  int bus = lc->sfp_cfg.i2c.bus;
  int inst = lc->sfp_cfg.i2c.switch_inst;
  int chan = lc->sfp_cfg.i2c.switch_chan;

  i2c_switch_swing(bus, inst, chan);

  int rv = get_module_eeprom(lc, type, offset, buf, len, gbe_i2c_rd_nophy);

  i2c_switch_release(bus, inst);
  
  return rv;
}


static const enet_link_ops_t gbe_nophy_link_ops =
{
  .name = "gbe_nophy",
  .probe = gbe_link_probe_nophy,
  .init = gbe_link_init_nophy,
  .start_config = gbe_link_start_config_nophy,
  .get_state = gbe_link_get_state_nophy,
  .intr = gbe_link_intr_nophy,
  .get_module_eeprom = gbe_get_module_eeprom_nophy,
};


//
// Plugin for loopback channels.
//

/** Probe routine. */
static int
loop_link_probe(enet_link_config_t* lc)
{
  if (!lc->loop)
    return 0;

  lc->possible_state =
    ENET_LINK_1G | ENET_LINK_10G | ENET_LINK_12G | ENET_LINK_50G |
    ENET_LINK_FDX | ENET_LINK_LOOP_MAC;

  return 1;
}


/** Initialization routine. */
static int
loop_link_init(enet_link_config_t* lc)
{
  return 0;
}


/** Begin configuration routine. */
static int
loop_link_start_config(enet_link_config_t* lc, uint32_t new_config)
{
  return 0;
}


/** Get state routine. */
static uint32_t
loop_link_get_state(enet_link_config_t* lc)
{
  if (lc->desired_state & ENET_LINK_SPEED)
  {
    uint32_t speed = gbe_top_speed(lc->desired_state);
    speed |= lc->desired_state & ~ENET_LINK_SPEED;
    speed |= ENET_LINK_FDX | ENET_LINK_LOOP_MAC;
    return speed;
  }

  return lc->desired_state;
}


/** Interrupt routine. */
static int
loop_link_intr(enet_link_config_t* lc, int dummy)
{
  return 0;
}


const enet_link_ops_t enet_loop_link_ops =
{
  .name = "loop",
  .probe = loop_link_probe,
  .init = loop_link_init,
  .start_config = loop_link_start_config,
  .get_state = loop_link_get_state,
  .intr = loop_link_intr,
};


//
// Null plugin, used if we find nothing else.
//

/** Probe routine. */
static int
null_link_probe(enet_link_config_t* lc)
{
  lc->possible_state = 0;

  return 1;
}


/** Initialization routine. */
static int
null_link_init(enet_link_config_t* lc)
{
  if (lc->phyaddr >= 32)
    printf("%s: no PHY found, link will be unusable\n", lc->device_name);
  else
    printf("%s: unknown PHY type %#x at MDIO addr %d, link will be unusable\n",
           lc->device_name, lc->phytype, lc->phyaddr);

  return ENET_ENOTSUP;
}


/** Begin configuration routine. */
static int
null_link_start_config(enet_link_config_t* lc, uint32_t new_config)
{
#ifdef LINK_DEBUG
    tprintf("%s: null link, can't start config\n", lc->device_name);
#endif
  return ENET_EIO;
}


/** Get state routine. */
static uint32_t
null_link_get_state(enet_link_config_t* lc)
{
  return 0;
}


/** Interrupt routine. */
static int
null_link_intr(enet_link_config_t* lc, int dummy)
{
  return 0;
}


const enet_link_ops_t enet_null_link_ops =
{
  .name = "null",
  .probe = null_link_probe,
  .init = null_link_init,
  .start_config = null_link_start_config,
  .get_state = null_link_get_state,
  .intr = null_link_intr,
};


//
// Link plugin table.  Eventually we may want to make this dynamically
// assembled at load time; for now we don't, since that would require
// us to specify a special linker script for BME applications.
//
const enet_link_ops_t*
enet_link_plugins[] =
{
  &enet_loop_link_ops,
  &xgbe_qt2025_link_ops,
  &xgbe_bcm87x7_link_ops,
  &xgbe_vsc8488_link_ops,
  &xgbe_nophy_link_ops,
  &gbe_88e1121_18_link_ops,
  &gbe_88e1111_1g_link_ops,
  &gbe_88e1111_amc_link_ops,
  &gbe_88e1111_link_ops,
  &gbe_bcm5481_link_ops,
  &gbe_bcm5482s_link_ops,
  &gbe_bcm54280_link_ops,
  &gbe_vsc8211_link_ops,
  &gbe_bcm87x7_link_ops,
  &gbe_rtl8211_link_ops,
  &gbe_nophy_link_ops,
  NULL,
};
