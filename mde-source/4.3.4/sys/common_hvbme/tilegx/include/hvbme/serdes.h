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
 * SERDES routines.
 */

#ifndef _SYS_COMMON_HVBME_SERDES_H
#define _SYS_COMMON_HVBME_SERDES_H

#include <stdint.h>

#include <arch/jtag_drs_def.h>


#define SERDES_PCIE 1       /**< PCIE SERDES. */
#define SERDES_ENET 2       /**< Ethernet SERDES. */


/** SERDES state.  This structure is opaque; its contents are not intended
 *  to be modified by the user.  It must be initialized with
 *  serdes_cfg_init(), and then passed to any of the serdes_xxx() functions
 *  to modify the SERDES configuration.
 */
typedef struct
{
#ifndef __DOXYGEN__
  /** rshim tile coordinates. */
  uint32_t rshim_port;
  /** rshim channel number. */
  unsigned long rshim_channel;
  /** Clock/reset JTAG instruction. */
  uint32_t crc_jtag_inst;
  /** SERDES JTAG instruction. */
  uint32_t serdes_jtag_inst;
  /** Contents of CRC scan chain. */
  unsigned long crc_chain[(JTAG_DRS_PCIE0_CRC_TOTAL_WIDTH + 63) / 64];
  /** Contents of SERDES scan chain. */
  unsigned long serdes_chain[(JTAG_DRS_PCIE0_SERDES_TOTAL_WIDTH + 63) / 64];
#else
  /** No user-serviceable parts inside. */
  char opaque[136];
#endif
} serdes_state_t;


/** Initialize SERDES configuration state structure.  This routine must be
 *  called before any of the routines below.
 * @param serdes_type SERDES type (SERDES_PCIE or SERDES_XAUI).
 * @param instance Instance number for the PCIE or XAUI.
 * @param rshim_port Tile coordinates of the rshim.
 * @param rshim_channel Channel number of the rshim.
 * @param state State structure to be initialized.
 */
void serdes_cfg_init(int serdes_type, int instance, uint32_t rshim_port,
                     unsigned long rshim_channel, serdes_state_t* state);


/** Override various PLL state bits in the PCIe SERDES.
 *
 * @param state SERDES state structure.
 */
void serdes_pll_ovd(serdes_state_t* state, unsigned long atxhfclkdn_val,
                    unsigned long pllrst_ovrd_val,
                    unsigned long atxhfclkdn_ovrd_val);


/** Reset the PCIe SERDES.
 *
 * @param state SERDES state structure.
 */
void serdes_force_pma_reset(serdes_state_t* state);

#endif /* _SYS_COMMON_HVBME_SERDES_H */
