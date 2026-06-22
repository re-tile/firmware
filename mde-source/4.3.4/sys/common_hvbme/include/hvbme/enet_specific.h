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
 * Routines to manipulate specific Ethernet I/O shims.  Most applications
 * should use the more general routines in <enet.h> instead.
 */

#ifndef _SYS_COMMON_ENET_SPECIFIC_H
#define _SYS_COMMON_ENET_SPECIFIC_H

#include <stdint.h>

/** Read an MDIO register on a XGbE interface, clause 22 signaling.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Which device to talk to.  Ignored for this routine, but
 *        present for uniformity across MDIO access functions.
 * @param reg Register number.
 * @return Value read from PHY.
 */
uint32_t xgbe_mdio_cl22_rd(uint32_t shim_addr, uint32_t channel,
                           uint32_t phyaddr, uint32_t devaddr, uint32_t reg);

/** Write an MDIO register on a XGbE interface, clause 22 signaling.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Which device to talk to.  Ignored for this routine, but
 *        present for uniformity across MDIO access functions.
 * @param reg Register number.
 * @param data Data to write.
 */
void xgbe_mdio_cl22_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                       uint32_t devaddr, uint32_t reg, uint32_t data);

/** Read an MDIO register on a XGbE interface, clause 45 signaling.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Device within the PHY
 * @param reg Register number.
 * @return Value read from PHY.
 */
uint32_t xgbe_mdio_cl45_rd(uint32_t shim_addr, uint32_t channel,
                           uint32_t phyaddr, uint32_t devaddr, uint32_t reg);

/** Write an MDIO register on a XGbE interface, clause 45 signaling.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Device within the PHY
 * @param reg Register number.
 * @param data Data to write.
 */
void xgbe_mdio_cl45_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                       uint32_t devaddr, uint32_t reg, uint32_t data);

/** Read an MDIO register on a GbE interface.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Which device to talk to.  Ignored for this routine, but
 *        present for uniformity across MDIO access functions.
 * @param reg Register number.
 * @return Value read from PHY.
 */
uint32_t gbe_mdio_rd(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                     uint32_t devaddr, uint32_t reg);

/** Write an MDIO register on a GbE interface.
 * @param shim_addr Shim to send request to.
 * @param channel Channel on shim to send request to.
 * @param phyaddr Which PHY to talk to.
 * @param devaddr Which device to talk to.  Ignored for this routine, but
 *        present for uniformity across MDIO access functions.
 * @param reg Register number.
 * @param data Data to write.
 */
void gbe_mdio_wr(uint32_t shim_addr, uint32_t channel, uint32_t phyaddr,
                 uint32_t devaddr, uint32_t reg, uint32_t data);

#endif // _SYS_COMMON_ENET_SPECIFIC_H
