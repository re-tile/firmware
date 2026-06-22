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
 * PCIe prebooter.
 */

/**
 * Enable debug output.  Note that this also subtly changes the behavior of
 * the prebooter, in that if it's defined, we won't exit and try to consume
 * the boot stream until the link is up.  This is only really an issue if
 * you're trying to initialize multiple links and want to be able to boot
 * from any of them; that won't necessarily work when you've enabled
 * debugging.
 */
// #define PREBOOT_DEBUG

/**
 * Enable auto-training of root complex ports.  This is generally not
 * required, since the hypervisor will do this as part of the normal system
 * boot.  It can be handy in certain system configurations where you'd like
 * the RC links to be trained at power up.
 */
// #define TRAIN_RC_PORTS

/**
 * Enable Virtual Function BAR registers. When SR-IOV is not supported, the
 * VF BAR sizes should be set to zero, to avoid wasting PCI bus resources.
 */
#define ENABLE_VF_BARS 

/**
 * Increase the BAR2 size to 256MB from the default 128MB.
 * In order for the new BAR2 size to take effect, the host must be
 * power-cycled after the prebooter is updated.
 */
// #define ENABLE_256MB_BAR2

#include <stdint.h>
#include <string.h>
#include <util.h>

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/rsh.h>
#include <arch/serdes.h>
#include <arch/sim.h>
#include <arch/trio.h>
#include <arch/trio_pcie_intfc.h>
#include <arch/trio_pcie_ep.h>
#include <arch/spr.h>
#include <arch/uart.h>

#include <hvbme/serdes.h>

#include "bits.h"
#include "cfg.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "types.h"
#include "uart.h"


//
// Tile position data.
//
pos_t grid_ulhc;  /**< Coordinates of grid's upper left hand corner. */
pos_t grid_lrhc;  /**< Coordinates of grid's lower right hand corner. */
pos_t rshimaddr;  /**< Coordinates of the rshim. */

#ifdef PREBOOT_DEBUG
void prebooter_uart_init(uint32_t refclk);
#endif

/** Delay a number of microseconds.
 * @param usec Number of microseconds to delay.
 */
static void
udelay(uint32_t usec)
{
  uint64_t end_time = get_cycle_count() +
                      (REFCLK * usec + 1000000UL - 1) / 1000000UL;

  while (get_cycle_count() < end_time)
    ;
}


/** Base physical address for a MAC's interface registers. */
#define TRIO_MAC_INTF(mac) ((TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE << \
                             TRIO_CFG_REGION_ADDR__INTFC_SHIFT) | \
                            ((mac) << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT))

/** Base physical address for a MAC's standard registers. */
#define TRIO_MAC_STD(mac) ((TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD << \
                            TRIO_CFG_REGION_ADDR__INTFC_SHIFT) | \
                           ((mac) << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT))

/** Base physical address for a MAC's protected registers. */
#define TRIO_MAC_PROT(mac) ((TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_PROTECTED << \
                            TRIO_CFG_REGION_ADDR__INTFC_SHIFT) | \
                           ((mac) << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT))


/** Customize the PCI EP configuration header to suit special requirement,
 *  e.g. non-Tilera Vendor/Device ID, BAR size changes, etc.
 * @param shimaddr Tile coordinates of shim.
 * @param mac PCIe link number on the shim.
 */
static void
pcie_customize_mac(pos_t shimaddr, int mac)
{
#ifdef ENABLE_256MB_BAR2
  //
  // Set the BAR2 size to 256MB.
  // When setting ENA to 1, the BAR2_MASK must be written after the ENA
  // is written.
  //
  cfg_wr32(shimaddr.word, 0, TRIO_MAC_PROT(mac) + TRIO_PCIE_EP_BASE_ADDR2, 1);
  cfg_wr32(shimaddr.word, 0, TRIO_MAC_PROT(mac) + TRIO_PCIE_EP_BASE_ADDR2,
           0xfffffff);
#endif

#ifndef ENABLE_VF_BARS
  //
  // Set the VF BAR sizes to zero.
  //
  cfg_wr32(shimaddr.word, 0, TRIO_MAC_PROT(mac) + TRIO_PCIE_EP_VF_BAR0, 0);
  cfg_wr32(shimaddr.word, 0, TRIO_MAC_PROT(mac) + TRIO_PCIE_EP_VF_BAR2, 0);

  //
  // This sets both InitialVFs and TotalVFs to zero.
  //
  cfg_wr32(shimaddr.word, 0, TRIO_MAC_STD(mac) + TRIO_PCIE_EP_INITIALVFS, 0);
#endif

  //
  // Users can use make argument "-DPCIE_ID=0x<dev_id><vend_id>"
  // to enable this option.
  //
#ifdef PCIE_ID
  cfg_wr32(shimaddr.word, 0,
           TRIO_MAC_STD(mac) + TRIO_PCIE_EP_DEVICE_ID_VEN_ID, PCIE_ID);
#endif

  //
  // Use make argument "-DPCIE_SUBSYSTEM=0x<subsys_id><subsys_vend_id>"
  // to enable this option.
  //
#ifdef PCIE_SUBSYSTEM
  cfg_wr32(shimaddr.word, 0,
           TRIO_MAC_STD(mac) + TRIO_PCIE_EP_SUBSYS_ID_SUBSYS_VEN_ID,
           PCIE_SUBSYSTEM);
#endif
}


/** Probe a PCIE link to see whether it is strapped as an endpoint, and
 *  what its configured width is.
 * @param shimaddr Tile coordinates of shim.
 * @param mac PCIe link number on the shim.
 * @return 0 if the shim is not strapped as an endpoint; 1 if the shim is
 *  strapped as a x4 endpoint; 2 if the shim is strapped as a x8 endpoint.
 */
static int
pcie_get_ep_status(pos_t shimaddr, int mac)
{
#ifdef BUG_15841_WORKAROUND
  //
  // The first prototype build of the Hancock card has PCIE port 2 strapped
  // improperly.  Ignore the port config register and just declare that
  // we're going to bring the link up.
  //
  if (mac == 2)
    return 1;
#endif

  TRIO_PCIE_INTFC_PORT_CONFIG_t port_config;
  unsigned long int port_config_reg_offset =
    TRIO_MAC_INTF(mac) + TRIO_PCIE_INTFC_PORT_CONFIG;

  port_config.word = cfg_rd(shimaddr.word, 0, port_config_reg_offset);

#ifdef TRAIN_RC_PORTS
  if (port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT &&
      port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT_G1 &&
      port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC &&
      port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC_G1)
    return 0;
#else
  if (port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT &&
      port_config.strap_state !=
      TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT_G1)
    return 0;
#endif

  //
  // Ports 1 and 2 are always x4.
  //
  if (mac != 0)
    return 1;

  //
  // There is no register which tells us directly whether port 0 is
  // strapped as x8.  To figure that out, we use the SERDES configuration
  // registers.  Hardware won't let us access registers associated with
  // lanes that aren't strapped, so we just try to read register 0 for
  // lane 8; if it works, we're strapped as x8, and if it times out, we
  // must be x4.
  //
  TRIO_PCIE_INTFC_SERDES_CONFIG_t serdes_config =
  {{
    .send = 1,
    .read = 1,
    .lane_sel = 1 << 7,
  }};
  unsigned long int serdes_config_reg_offset =
    TRIO_MAC_INTF(mac) + TRIO_PCIE_INTFC_SERDES_CONFIG;

  cfg_wr(shimaddr.word, 0, serdes_config_reg_offset, serdes_config.word);

  //
  // Give the read time to happen.
  //
  __insn_mf();

  serdes_config.word = cfg_rd(shimaddr.word, 0, serdes_config_reg_offset);
  return (serdes_config.send == 0) ? 2 : 1;
}


/** Configure a PCIE link.
 * @param shimaddr Tile coordinates of shim.
 * @param mac PCIe link number on the shim.
 */
static void
pcie_init_link(pos_t shimaddr, int mac)
{
#ifdef PREBOOT_DEBUG
  boot_printf("Configuring TRIO at (%d,%d) MAC %d\n",
              shimaddr.bits.x, shimaddr.bits.y, mac);
#endif

  unsigned int mac_intf_pa = TRIO_MAC_INTF(mac);
  
  //
  // We need to go through the loop at least once to do
  // pcie_customize_mac(), so start out in DETECT_QUIET instead of reading
  // status from the hardware.  In the future, for chips that don't need
  // extra link bringup operations, we could do customization here, before
  // the loop.  However, getting here takes < 12ms on typical platforms, so
  // we may well be in DETECT_QUIET anyway.
  //
  TRIO_PCIE_INTFC_PORT_STATUS_t port_status =
  {{
    .ltssm_state = TRIO_PCIE_INTFC_PORT_STATUS__LTSSM_STATE_VAL_DETECT_QUIET,
  }};

  while (port_status.ltssm_state == 
         TRIO_PCIE_INTFC_PORT_STATUS__LTSSM_STATE_VAL_DETECT_QUIET ||
         port_status.ltssm_state == 
         TRIO_PCIE_INTFC_PORT_STATUS__LTSSM_STATE_VAL_DETECT_ACT)
  {
    //
    // Reset the link to clear out state (otherwise the port won't retrain
    // to Gen2 automatically).
    //
    TRIO_PCIE_INTFC_RESET_CTL_t reset_ctl =
    {{
      .auto_mode = 0,
      .reset_pmc = 1,
      .reset_mac = 1,
      .reset_phy = 1,
      .reset_sticky = 1,
      .reset_non_sticky = 1,
    }};

    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_RESET_CTL,
           reset_ctl.word);

    //
    // Disable training, force endpoint mode.
    //
    TRIO_PCIE_INTFC_PORT_CONFIG_t port_config =
      { .word = cfg_rd(shimaddr.word, 0,
                       mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG) };
    port_config.train_mode =
      TRIO_PCIE_INTFC_PORT_CONFIG__TRAIN_MODE_VAL_TRAIN_DIS;
    port_config.ovd_dev_type = 1;
    port_config.ovd_dev_type_val =
      TRIO_PCIE_INTFC_PORT_CONFIG__OVD_DEV_TYPE_VAL_VAL_EP;
    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG,
           port_config.word);

    //
    // Re-enable auto-reset, but don't let it reset any of the shim
    // registers.
    //
    reset_ctl = (TRIO_PCIE_INTFC_RESET_CTL_t)
    {{
      .auto_mode = 1,
      .reset_pmc = 1,
      .reset_mac = 1,
    }};

    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_RESET_CTL,
           reset_ctl.word);

    //
    // If the port might have been up or coming up, taking it down and
    // bringing it up immediately may confuse the other side.  The BTK and
    // hypervisor endpoint code wait 200 ms right here for that reason.
    // However, the prebooter needs to get the link up much quicker than
    // that, and we expect that in most cases the link should be down here,
    // since we just got reset.  Thus, we omit the delay.  Should we run
    // into a system where (a) the link seems to be coming up sometimes
    // without our doing anything and (b) our taking it down confuses the
    // host, we might need to re-think this.
    //

    //
    // We're done resetting the shim, but the link is still down, so this
    // is the time to set any internal state we want the host to see.
    //
    pcie_customize_mac(shimaddr, mac);

    //
    // Enable training.
    //
    port_config.word = cfg_rd(shimaddr.word, 0,
                              mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG);
    port_config.train_mode =
      TRIO_PCIE_INTFC_PORT_CONFIG__TRAIN_MODE_VAL_TRAIN_ENA;
    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG,
           port_config.word);

    //
    // We need to wait for at least 30 us here to let the PLL spin up, but
    // if we wait longer than 1 ms, it'll be too late to synchronize the
    // clock dividers.
    //
    // Note that the BTK link bringup flow can't guarantee that it won't be
    // unduly delayed at this point, so what it does is keep the MAC in reset
    // until after it's done the synchronization.  We don't need to do that,
    // and more importantly, if we had, we wouldn't have been able to do
    // pcie_customize_mac() above.  Calling it after the link had been
    // enabled for training would mean that we'd be racing with the host to
    // make sure we set those registers before it saw them; we'd probably
    // win, but it seems better to do it this way.
    //
    udelay(50);

    //
    // Make sure lanes are synchronized.  This needs to happen before we get
    // to Detect.Active but after PMA reset has completed.
    // 
    SERDES_PLL_F_SET_t spfs =
    {{
      .f = 4,           // Original value that we don't want to change.
      .div_mode0 = 0,   // Setting to 0 and back to 2 synchronizes the dividers.
    }};

    TRIO_PCIE_INTFC_SERDES_CONFIG_t tpisc =
    {{
      .send = 1,
      .read = 0,
      .lane_sel = 0xff, // Write all lanes (it's OK if this is a 4-lane MAC).
      .reg_addr = SERDES_PLL_F_SET,
      .reg_data = spfs.word,
    }};

    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG,
           tpisc.word);

    //
    // This probably isn't necessary in practice, but the shim interface
    // claims that you should make sure the SERDES write completed before
    // you start another one.
    //
    TRIO_PCIE_INTFC_SERDES_CONFIG_t check_tpisc;
    do
    {
      check_tpisc.word = cfg_rd(shimaddr.word, 0,
                                mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG);
    }
    while (check_tpisc.send);

    spfs.div_mode0 = 2;
    tpisc.reg_data = spfs.word;

    cfg_wr(shimaddr.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG,
           tpisc.word);

    udelay(30 * 1000);

    port_status.word = cfg_rd(shimaddr.word, 0,
                              mac_intf_pa | TRIO_PCIE_INTFC_PORT_STATUS);
  }

#ifdef PREBOOT_DEBUG
  port_status.word = cfg_rd(shimaddr.word, 0,
                            mac_intf_pa | TRIO_PCIE_INTFC_PORT_STATUS);

  boot_printf("MAC %d link configured, state 0x%x, "
              "%ld us after reset, down_cnt %d\n", mac, port_status.ltssm_state,
              (1000000UL * cfg_rd(rshimaddr.word, 0, RSH_UPTIME)) / REFCLK,
              port_status.dl_down_cnt);

  udelay(1000 * 1000);

  port_status.word = cfg_rd(shimaddr.word, 0,
                            mac_intf_pa | TRIO_PCIE_INTFC_PORT_STATUS);

  boot_printf("MAC %d link state after 1s is 0x%x, down_cnt %d\n", mac,
              port_status.ltssm_state, port_status.dl_down_cnt);

  int old_status = port_status.ltssm_state;

  while (1)
  {
    port_status.word = cfg_rd(shimaddr.word, 0,
                              mac_intf_pa | TRIO_PCIE_INTFC_PORT_STATUS);

    if (old_status != port_status.ltssm_state || port_status.dl_down_cnt)
    {
      old_status = port_status.ltssm_state;
      boot_printf("MAC %d state change, new state is 0x%x, down_cnt %d\n", mac, 
                  old_status, port_status.dl_down_cnt);
    }

    if (port_status.dl_up)
    {
      boot_printf("MAC %d state change, link is now up\n", mac);
      break;
    }
    udelay(10 * 1000);
  }
#endif
}


/** Test a shim to see what type it is; then update the state structure
 *  appropriately.
 * @param test_shim Shim to test.
 * @param state Pointer to a state structure which will be filled in with
 *   information on the shim found, if it's a type we track.
 */
static void
boot_probe_shim(pos_t test_shim, struct boot_probe_shim_state* state)
{
  RSH_DEV_INFO_t info =
    { .word = cfg_rd(test_shim.word, 0, RSH_DEV_INFO) };

  switch (info.type)
  {
  case RSH_DEV_INFO__TYPE_VAL_TRIO:
    state->pcies[state->num_pcies++] = test_shim;
    break;

  default:
    break;
  }
}


/** Probe our shims, looking for the TRIO shims, then bring up the links
 *  on any shims which are strapped as endpoints.
 */
static void
config_shims()
{
  struct boot_probe_shim_state probe_state =
    { .num_mshims = 0, .num_pcies = 0, .num_rshims = 0 };
  pos_t pos = { .word = 0 };

  RSH_FABRIC_CONN_t fabric_conn =
    { .word = cfg_rd(rshimaddr.word, 0, RSH_FABRIC_CONN) };

  int x, y;

  // Probe left side.

  pos.bits.x = 0xF;

  for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
  {
    pos.bits.y = y;
    if (fabric_conn.west & (1 << y))
      boot_probe_shim(pos, &probe_state);
  }

  // Probe top edge.

  pos.bits.y = 0xF;

  for (x = grid_ulhc.bits.x; x <= grid_lrhc.bits.x; x++)
  {
    pos.bits.x = x;
    if (fabric_conn.north & (1 << x))
      boot_probe_shim(pos, &probe_state);
  }

  // Probe bottom edge; note that this goes right-to-left.

  pos.bits.y = grid_lrhc.bits.y + 1;

  for (x = grid_lrhc.bits.x; x >= grid_ulhc.bits.x; x--)
  {
    pos.bits.x = x;
    if (fabric_conn.south & (1 << x))
      boot_probe_shim(pos, &probe_state);
  }

  // Probe right side.

  pos.bits.x = grid_lrhc.bits.x + 1;

  for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
  {
    pos.bits.y = y;
    if (fabric_conn.east & (1 << y))
      boot_probe_shim(pos, &probe_state);
  }

  //
  // Bring up the links on any ports strapped as endpoints.
  //
  for (int intf = 0; intf < probe_state.num_pcies; intf++)
  {
    for (int mac = 0; mac < 3; mac++)
    {
      int stat = pcie_get_ep_status(probe_state.pcies[intf], mac);

      if (stat == 2)
      {
        pcie_init_link(probe_state.pcies[intf], mac);

        // x8 link, so skip the next port
        mac++;
      }
      else if (stat == 1)
      {
        pcie_init_link(probe_state.pcies[intf], mac);
      }
    }
  }
}


/** Main boot routine. */
void
boot()
{
  //
  // Record where the rshim is; we'll need this below.  Initialize the UART
  // in case we need to do any output.
  //
  rshimaddr.word = __insn_mfspr(SPR_RSHIM_COORD);

#ifdef PREBOOT_DEBUG
  prebooter_uart_init(REFCLK);
#endif

  //
  // Get tile grid size.
  //
  RSH_FABRIC_DIM_t grid_size =
    { .word = cfg_rd(rshimaddr.word, 0, RSH_FABRIC_DIM) };
  grid_ulhc.bits.x = 0;
  grid_ulhc.bits.y = 0;
  grid_lrhc.bits.x = grid_size.dim_x - 1;
  grid_lrhc.bits.y = grid_size.dim_y - 1;

  //
  // Probe the shims, configuring any PCIe shims.
  //
  config_shims();

  // 
  // The L0.5 booter that we're going to load next expects that the target
  // way for cache-as-RAM writes is way 0, since that's what you get after
  // hardware reset.  However, the copy that ran before us it left it
  // pointing at way 7, so reset this now.
  //
  __insn_mtspr(SPR_CBOX_CACHEASRAM_CONFIG,
               SPR_CBOX_CACHEASRAM_CONFIG__ENABLE_MASK);
  
  extern void hw_l0_boot(void);
  hw_l0_boot();
}

#ifdef PREBOOT_DEBUG

static int uart_initialized = 0;

/** Initialize the UART for console input and output.
 */
void
prebooter_uart_init(uint32_t refclk)
{
  cfg_wr(rshimaddr.word, UART_CHANNEL, UART_DIVISOR, (refclk / 16) /
         UART_SPEED);
  // Set for 8 bits, no parity, 1 stop bit
  cfg_wr(rshimaddr.word, UART_CHANNEL, UART_TYPE, 0);

  uart_initialized = 1;
}


/** Write a character to the UART console.
 * @param c Character to write.
 */
void
boot_putchar(char c)
{
  uint32_t status;

  if (!uart_initialized)
    return;

  if (c == '\n')
    boot_putchar('\r');

  do
    status = cfg_rd(rshimaddr.word, UART_CHANNEL, UART_FLAG);
  while (status & (UART_FLAG__WFIFO_FULL_MASK |
                   UART_FLAG__TFIFO_FULL_MASK));

  // Write character.
  cfg_wr(rshimaddr.word, UART_CHANNEL, UART_TRANSMIT_DATA, (uint32_t) c);
}

#endif
