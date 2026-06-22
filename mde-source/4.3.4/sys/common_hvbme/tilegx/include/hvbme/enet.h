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
 * Routines to configure and otherwise manage Ethernet I/O shims.
 */

#ifndef _SYS_COMMON_ENET_H
#define _SYS_COMMON_ENET_H

#include <stdint.h>

#ifdef __HV__
#include "hvbme/enet_plugin.h"
#else
#include "enet_plugin.h"
#endif

/** Link configuration data structure.  If statically initializing the
 *  structure, we strongly recommend use of C99-style named initializers,
 *  since members will be added or reordered in the future.  If dynamically
 *  initializing it, be sure to set all bits to zero first.
 */
typedef struct _enet_link_config
{
  /** Device name. */
  char device_name[7];

  /** mPIPE instance number. */
  int instance;

  /** Link's MAC number. */
  int mac_num;

  /** MAC's channel number (used by mPIPE to deliver packets, etc.). */
  int chan;

  /** Are we a gigabit device (vs. 10 gigabit)? */
  uint32_t gbe:1;
  /** Are we a loopback device? */
  uint32_t loop:1;
  /** Should we accept pause frames? */
  uint32_t pause_in:1;
  /** Should we discard transmit packets when the link is down? */
  uint32_t discard_if_down:1;
  /** Are we currently discarding transmit packets? */
  uint32_t discarding:1;
  /** TX pause quanta, 0 if no pause frames */
  uint16_t pause_out;
  /** Should we accept jumbo frames? */
  uint32_t jumbo:1;

  /** Have we complained about this link since it was last taken down? */
  uint32_t link_warned:1;
  /** Have we spun waiting for this link to come up since it was last taken
   *  down? */
  uint32_t link_spun:1;
  /** Does this link interrupt on state change? */
  uint32_t link_does_intr:1;
  /** Should this link disable autonegotiation? */
  uint32_t no_auto_neg:1;
  /** Signal descriptor for this link's interrupt */
  struct bi_signal link_intr_sig;

  /** Desired link state (ENET_LINK_xxx bitmask) */
  uint32_t desired_state;
  /** Possible link states (ENET_LINK_xxx bitmask) */
  uint32_t possible_state;
  /** Current link state (ENET_LINK_xxx bitmask, only one speed bit set);
   *  only valid if link_does_intr is true */
  uint32_t current_state;

  /** Is the SFP transmit line inverted? */
  uint8_t sfp_txout_inv:1;

  /** Is the shim's XAUI reference clock 125 MHz? */
  uint8_t xaui_refclk_125:1;

  /** Is there a PHY at all?  If not, the variables from here down to (and
   *  including) phytype are undefined. */
  uint8_t has_phy:1;
  /** Is this PHY connected to the XGbE MDIO bus (vs. the GbE MDIO bus)? */
  uint8_t xgbe_mdio:1;
  /** Does this PHY automatically configure itself? */
  uint8_t phy_auto_cfg:1;
  /** MDIO address of the PHY */
  uint8_t phyaddr;
  /** PHY identifier */
  uint32_t phytype;

  /** SFP copper status; see ENET_SFP_CU_xxx below. */
  uint8_t sfp_cu:2;
  /** 1 iff SFP is in SGMII mode; implies sfp_cu == ENET_SFP_CU_YES. */
  uint8_t sfp_sgmii:1;
  /** Is sfp_cfg valid?  Must be 0 if has_phy is 1. */
  uint8_t has_sfp_cfg:1;
  /** SFP configuration; only valid if has_sfp_cfg is 1. */
  struct bi_sfp_cfg sfp_cfg;

  /** MAC address. */
  uint8_t mac_addr[6];

  /** LED settings, from the BIB (BI_ENET_LED_CFG__VAL_xxx values). */
  uint8_t leds[6];

  /** Ops vector for link plugin */
  const enet_link_ops_t* ops;

  /** MAC interrupt bits to be routed to this link's plugin */
  uint_reg_t mac_intrs;

  /** Grid coordinates of the I/O shim */
  uint32_t shim_port;

  /** PA offset for base mPIPE registers. */
  unsigned long shim_pa;

  /** PA offset for MAC registers. */
  unsigned long mac_pa;

  /** PA offset for MAC registers on the MAC which runs our MDIO port. */
  unsigned long mdio_mac_pa;
  
  /** Number for the MAC which runs our MDIO port. */
  int mdio_mac_num;

  /** PA offset for MAC registers on the MAC which controls our SERDES
   *  registers. */
  unsigned long serdes_mac_pa;

  /** Bitmap of the SERDES lanes used by this port, within the quad
   *  controlled by serdes_mac_pa. */
  uint8_t serdes_lanes;

  /** SERDES lane receive length, in notional mm.  All 4 values are
   *  supplied for a 10 or 20 Gbps port; for a 1 Gbps or slower port, only
   *  the first entry in the array is valid. */
  int16_t serdes_rx_lane_length[4];
  
  /** SERDES lane transmit length, in notional mm.  All 4 values are
   *  supplied for a 10 or 20 Gbps port; for a 1 Gbps or slower port, only
   *  the first entry in the array is valid. */
  int16_t serdes_tx_lane_length[4];
  
  /** Private hypervisor data. */
  void* hv_private;
} enet_link_config_t;

//
// Definitions for some flag bits.
//

/** We don't yet know if this is a copper SFP.  Note that currently, when
 *  we say "copper SFP" we really mean "copper SFP that includes a
 *  Marvell 88E1111 PHY", since that's all our code knows how to deal with. */
#define ENET_SFP_CU_UNK  0
/** We know this is a copper SFP. */
#define ENET_SFP_CU_YES  1
/** We know this is not a copper SFP. */
#define ENET_SFP_CU_NO   2

//
// Link attributes.  These must match the GXIO versions.
//
//

/** Link can run, should run, or is running at 10 Mbps. */
#define ENET_LINK_10M         0x0000000000000001UL

/** Link can run, should run, or is running at 100 Mbps. */
#define ENET_LINK_100M        0x0000000000000002UL

/** Link can run, should run, or is running at 1 Gbps. */
#define ENET_LINK_1G          0x0000000000000004UL

/** Link can run, should run, or is running at 10 Gbps. */
#define ENET_LINK_10G         0x0000000000000008UL

/** Link can run, should run, or is running at 20 Gbps. */
#define ENET_LINK_20G         0x0000000000000010UL

/** Link can run, should run, or is running at 25 Gbps. */
#define ENET_LINK_25G         0x0000000000000020UL

/** Link can run, should run, or is running at 50 Gbps. */
#define ENET_LINK_50G         0x0000000000000040UL

/** Link can run, should run, or is running at 12 Gbps. */
#define ENET_LINK_12G         0x0000000000000080UL

/** Link should run at the highest speed supported by the link and by the
 *  device connected to the link; in effect, this is the same as specifying
 *  the link's possible state ANDed with ENET_LINK_SPEED.  This value may be
 *  given to enet_config_link(), but will never be found in the link_config
 *  structure, and will never be returned by enet_inquire_link(). */
#define ENET_LINK_ANYSPEED    0x0000000000000800UL

/** All legal link speeds. */
#define ENET_LINK_SPEED       0x0000000000000FFFUL

/** Link can run, should run, or is running in MAC loopback mode.  This
 *  loops transmitted packets back to the receiver, inside the Tile
 *  Processor. */
#define ENET_LINK_LOOP_MAC    0x0000000000001000UL

/** Link can run, should run, or is running in PHY loopback mode.  This
 *  loops transmitted packets back to the receiver, inside the external
 *  PHY chip. */
#define ENET_LINK_LOOP_PHY    0x0000000000002000UL

/** Link can run, should run, or is running in external loopback mode.
 *  This requires that an external loopback plug be installed on the
 *  Ethernet port.  Note that only some links require that this be
 *  explicitly configured; other links can do external loopack with the
 *  plug and no special configuration. */
#define ENET_LINK_LOOP_EXT    0x0000000000004000UL

/** All legal loopback types. */
#define ENET_LINK_ALLLOOP     0xF000

/** Link can run, should run, or is running in full-duplex mode. */
#define ENET_LINK_FDX         0x0000000000010000UL

/** Link can run, should run, or is running in half-duplex mode. */
#define ENET_LINK_HDX         0x0000000000020000UL

//
// Error codes.  These must match the GXIO versions.
//
//
/** Operation successfully completed. */
#define ENET_NO_ERROR           0

/** The requested function was not implemented. */
#define ENET_NOT_IMPLEMENTED -1113  /* GXIO_ERR_UNSUPPORTED_OP */

/** The requested configuration is invalid. */
#define ENET_BAD_CONFIG      -1151  /* GXIO_MPIPE_ERR_BAD_CONFIG */

/** I/O error. */
#define ENET_EIO             -1118  /* GXIO_ERR_IO */


/** Read an MDIO register on an Ethernet interface, using clause 22
 *  signaling.
 * @param lc driver link configuration.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param reg Register number.
 * @param valuep Pointer to returned value.
 * @return 0 if read successful, an enet error code otherwise.
 */
int enet_mdio_cl22_rd(enet_link_config_t* lc, int phy,
		      int reg, uint32_t* valuep);

/** Write an MDIO register on an Ethernet interface, using clause 22
 *  signaling.
 * @param lc driver link configuration.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param reg Register number.
 * @param value Value to write.
 * @return 0 if write successful, an enet error code otherwise.
 */
int enet_mdio_cl22_wr(enet_link_config_t* lc, int phy,
		      int reg, uint32_t value);

/** Read an MDIO register on an Ethernet interface, using clause 45
 *  signaling.
 * @param lc driver link configuration.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param dev Device within the PHY.
 * @param reg Register number.
 * @param valuep Pointer to returned value.
 * @return 0 if read successful, an enet error code otherwise.
 */
int enet_mdio_cl45_rd(enet_link_config_t* lc, int phy, int dev,
		      int reg, uint32_t* valuep);

/** Write an MDIO register on an Ethernet interface, using clause 45
 *  signaling.
 * @param lc driver link configuration.
 * @param phy Optional PHY address, -1 for link default PHY.
 * @param dev Device within the PHY.
 * @param reg Register number.
 * @param value Value to write.
 * @return 0 if write successful, an enet error code otherwise.
 */
int enet_mdio_cl45_wr(enet_link_config_t* lc, int phy, int dev,
		      int reg, uint32_t value);

/** Configure the MAC.
 * @param lc driver link configuration.
 * @return 1 if link successfully brought up, 0 otherwise.
 */
int enet_config_mac(enet_link_config_t* lc);

/** Set input or output pause processing.
 * @param lc driver link configuration.
 * @param is_in Nonzero if we're setting input pause handling, zero if we're
 *        setting output pause handling.
 * @param value For input, this is nonzero to obey input pause frames, and
 *        zero to discard them.  For output, this is zero to not send pause
 *        frames, and nonzero to send them; in the latter case, value is used
 *        as the transmit pause quanta.
 */
void enet_set_pause(enet_link_config_t* lc, int is_in, int value);

/** Set input jumbo frame processing.
 * @param lc driver link configuration.
 * @param accept_jumbo Nonzero if we want to accept jumbo frames, zero if not.
 */
void enet_set_jumbo(enet_link_config_t* lc, int accept_jumbo);

/** Set link discard mode.
 * @param lc driver link configuration.
 * @param discard Nonzero if we want to discard egressed frames when the
 *  link is down, zero if we want to stall egress when the link is down.
 */
void enet_set_discard(enet_link_config_t* lc, int discard);

/** Bring an Ethernet link up or down.
 * @param lc Driver link configuration.
 * @param link_config The requested link config.
 * @return 0 if link successfully brought up, else a negative error.
 */
int enet_config_link(enet_link_config_t* lc, uint32_t link_config);

/** Query an Ethernet link's current state.
 * @param lc driver link configuration.
 * @return Current link state: zero or more ENET_LINK_XXX bits, ORed
 *   together.  If the return value & ENET_LINK_SPEED is zero, the link is
 *   down.
 */
uint32_t enet_inquire_link(enet_link_config_t* lc);

/** Probe the hardware and determine the link type, register the correct
 *  link handling plugin in the link configuration structure, and do any
 *  hardware initialization necessary to track link state.  When this
 *  routine returns, the link will be down.  As a side-effect, enables usage
 *  of the appropriate MDIO port. This routine must be called on a
 *  configuration before enet_do_link_config() or enet_inquire_link().
 * @param lc Driver link configuration.
 * @param do_intr Nonzero if the link's interrupt plugin can be called on
 *   interrupt; typically nonzero in the hypervisor, and zero in BME.
 */
void enet_probe_init_link(enet_link_config_t* lc, int do_intr);

/** Function called by link plugin routines when they detect a link state
 *  change.
 * @param lc Driver link configuration.
 * @param new_state The new current link state.
 */
void enet_new_link_state(enet_link_config_t* lc, uint32_t new_state);

//
// These need to match the corresponding hypervisor error codes.
//
#define ENET_EINVAL      -801  /**< Invalid argument */
#define ENET_ENOTSUP     -808  /**< Service not supported */
#define ENET_EBUSY       -809  /**< Device busy */

/** Link state change callback function.  Unlike other routines defined in
 *  this file, this is not part of the enet framework; it is supplied by
 *  the client of the framework.  If it exists, this routine is called
 *  whenever the framework changes the current link state, either as a
 *  result of a client request (like asking to take the link down), or
 *  because the hardware notified the framework of a change.  Since the
 *  framework is not reentrant, this routine must not invoke any framework
 *  routines.
 * @param lc Driver link configuration.
 * @param old_state The previous current link state.
 * @param new_state The new current link state.
 */
void enet_new_link_state_hook(enet_link_config_t* lc,
                              uint32_t old_state, uint32_t new_state);

/** Retrieve data from the EEPROM of a plug-in SFP module, if such a
 *  module is associated with the given link.  As with the corresponding
 *  GXIO operation, the first 256 bytes of the EEPROM address space are the
 *  SFF-8079 EEPROM values and the next 256 bytes are the SFF-8472 dynamic
 *  optical monitoring values.
 * @param lc Driver link configuration.
 * @param type Pointer to returned module type (ENET_MODULE_xxx); may be
 *  NULL in which case no type is returned.
 * @param offset Offset within the EEPROM at which to start the transfer.
 * @param buf Buffer to hold the transferred bytes.
 * @param len Number of bytes to transfer.
 * @return The number of bytes that were available to be transferred at the
 *  given offset, or a negative error code.  Note that this number of bytes
 *  may be more, or less, than the number of bytes that were specified in
 *  len. */
int enet_get_module_eeprom(enet_link_config_t* lc, int* type,
                           int offset, void* buf, int len);

//
// These need to match the corresponding GXIO values.
//
#define ENET_MODULE_NONE 0 /**< No pluggable module. */
#define ENET_MODULE_8079 1 /**< Module conforms to SFF-8079, not SFF-8472. */
#define ENET_MODULE_8472 2 /**< Module conforms to SFF-8472. */

#endif // _SYS_COMMON_ENET_H
