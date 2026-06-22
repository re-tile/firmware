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
 * SERDES configuration routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <util.h>

#include <arch/jtag_drs_def.h>
#include <arch/rsh.h>

#include "cfg.h"
#include "jtag.h"
#include "serdes.h"
#include "shared_lock.h"


/** JTAG instructions specifying which SERDES object to control. */
uint8_t pcie_serdes_inst[3] =
{
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD0_SERDES,
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD1_SERDES,
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD2_SERDES,
};

/** JTAG instructions specifying which clock/reset object to control. */
uint8_t pcie_crc_inst[3] =
{
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD0_CRC,
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD1_CRC,
  RSH_JTAG_SETUP__JTAG_INST_VAL_JTAG_PCIE_PHY_TP0_QUAD2_CRC,
};


/** Set a PCIE clock/reset control field to a value.
 * @param state SERDES state.
 * @param field Name of the field to set.
 * @param val Value to set the field to.
 */
#define SERDES_SET_PCIE_CRC(state, field, val) \
  bit_insert(state->crc_chain, \
             JTAG_DRS_PCIE0_CRC__ ## field ## _FIELD, val)


/** Set a PCIE SERDES field to a value.
 * @param state SERDES state.
 * @param lane Lane number.
 * @param field Name of the field to set.
 * @param val Value to set the field to.
 */
#define SERDES_SET_PCIE_SERDES(state, lane, field, val) \
  bit_insert(state->serdes_chain, \
             JTAG_DRS_PCIE0_SERDES__ ## field ## lane ## _FIELD, val)


/** Set all per-lane versions of a SERDES field to the same value.
 * @param state SERDES state.
 * @param fields Descriptor for the field to set.
 * @param val Value to set the field to.
 */
#define SERDES_SET_PCIE_SERDES_ALL_LANES(state, field, val) \
  do \
  { \
    SERDES_SET_PCIE_SERDES(state, 0, field, val); \
    SERDES_SET_PCIE_SERDES(state, 1, field, val); \
    SERDES_SET_PCIE_SERDES(state, 2, field, val); \
    SERDES_SET_PCIE_SERDES(state, 3, field, val); \
  } while (0)


/** Write a SERDES scan chain to the hardware, protected by appropriate
 *  locking.
 * @param state SERDES state.
 * @param is_crc If nonzero, commit the clock/reset chain, else the SERDES
 *   chain.
 */
static void
serdes_commit(serdes_state_t* state, int is_crc)
{
  hvbme_spin_lock(HVBME_SPINLOCK_SERDES);

  if (is_crc)
    rshim_jtag_send(state->rshim_port, state->rshim_channel,
                    state->crc_chain, state->crc_jtag_inst,
                    JTAG_DRS_PCIE0_CRC_TOTAL_WIDTH);
  else
    rshim_jtag_send(state->rshim_port, state->rshim_channel,
                    state->serdes_chain, state->serdes_jtag_inst,
                    JTAG_DRS_PCIE0_SERDES_TOTAL_WIDTH);

  hvbme_spin_unlock(HVBME_SPINLOCK_SERDES);
}


void
serdes_cfg_init(int serdes_type, int instance, uint32_t rshim_port,
                unsigned long rshim_channel, serdes_state_t* state)
{
  memset(state, 0, sizeof (*state));
  state->rshim_port = rshim_port;
  state->rshim_channel = rshim_channel;

  if (serdes_type == SERDES_PCIE && instance >= 0 && instance < 3)
  {
    state->serdes_jtag_inst = pcie_serdes_inst[instance];
    state->crc_jtag_inst = pcie_crc_inst[instance];

    //
    // JTAG scan chains don't get reset on soft reset, so reset them by
    // hand now.  We want the serdes chain to be all 0, and we cleared it
    // above, so just commit it.
    //
    serdes_commit(state, 0);

    //
    // We write the CRC chain twice to ensure a glitch free transition.
    //
    state->crc_chain[0] = 0x814000000000018eUL;
    state->crc_chain[1] = 0x60000074212fdef6UL;
    state->crc_chain[2] = 0x0000000000000078UL;
    serdes_commit(state, 1);

    state->crc_chain[0] = 0x8140000000000000UL;
    state->crc_chain[1] = 0x60000074212fdef6UL;
    state->crc_chain[2] = 0x0000000000000078UL;
    serdes_commit(state, 1);
  }

  if (!state->crc_jtag_inst)
    panic("unknown SERDES type %#x/instance %#x passed to serdes_cfg_init",
          serdes_type, instance);
}


void
serdes_pll_ovd(serdes_state_t* state, unsigned long atxhfclkdn_val,
               unsigned long pllrst_ovrd_val, unsigned long atxhfclkdn_ovrd_val)
{
  memset(&state->serdes_chain, 0, sizeof (state->serdes_chain));
  SERDES_SET_PCIE_SERDES_ALL_LANES(state, TX_HFCLK_DN_OVRD,
                                   atxhfclkdn_ovrd_val);
  SERDES_SET_PCIE_SERDES_ALL_LANES(state, ATXHFCLKDNB, atxhfclkdn_val);

  SERDES_SET_PCIE_SERDES_ALL_LANES(state, TX_PLL_RST_OVRD, pllrst_ovrd_val);
  SERDES_SET_PCIE_SERDES_ALL_LANES(state, ATXPLLRSTB, 1);

  SERDES_SET_PCIE_SERDES_ALL_LANES(state, TX_PLL_DIV_INIT_OVRD, 1);
  SERDES_SET_PCIE_SERDES_ALL_LANES(state, ATXPLLDIVINIT, 0);

  serdes_commit(state, 0);
}


void
serdes_force_pma_reset(serdes_state_t* state)
{
  //
  // Set all bits in non-ovd mode first.
  //
  memset(state->crc_chain, 0, sizeof (state->crc_chain));
  state->crc_chain[0] = 0x8140000000000000UL;
  state->crc_chain[1] = 0x60000074212fdef6UL;
  state->crc_chain[2] = 0x0000000000000078UL;
  SERDES_SET_PCIE_CRC(state, RESET_OVD, 0);
  SERDES_SET_PCIE_CRC(state, XRST, 1);
  SERDES_SET_PCIE_CRC(state, REG_RST, 1);
  SERDES_SET_PCIE_CRC(state, PMA_RST, 1);
  SERDES_SET_PCIE_CRC(state, XCLK_READY, 1);
  SERDES_SET_PCIE_CRC(state, BCON_RST, 1);

  serdes_commit(state, 1);

  //
  // Reset PCS/PHY.
  //
  memset(state->crc_chain, 0, sizeof (state->crc_chain));
  state->crc_chain[0] = 0x8140000000000000UL;
  state->crc_chain[1] = 0x60000074212fdef6UL;
  state->crc_chain[2] = 0x0000000000000078UL;
  SERDES_SET_PCIE_CRC(state, RESET_OVD, 1);
  SERDES_SET_PCIE_CRC(state, XRST, 1);
  SERDES_SET_PCIE_CRC(state, REG_RST, 1);
  SERDES_SET_PCIE_CRC(state, PMA_RST, 0);
  SERDES_SET_PCIE_CRC(state, XCLK_READY, 1);
  SERDES_SET_PCIE_CRC(state, BCON_RST, 1);

  serdes_commit(state, 1);

  //
  // This will basically stop clocks while we deassert reset.
  //
  serdes_pll_ovd(state, 0, 1, 1);

  memset(state->crc_chain, 0, sizeof (state->crc_chain));
  state->crc_chain[0] = 0x8140000000000000UL;
  state->crc_chain[1] = 0x60000074212fdef6UL;
  state->crc_chain[2] = 0x0000000000000078UL;
  SERDES_SET_PCIE_CRC(state, RESET_OVD, 1);
  SERDES_SET_PCIE_CRC(state, XRST, 1);
  SERDES_SET_PCIE_CRC(state, REG_RST, 1);
  SERDES_SET_PCIE_CRC(state, PMA_RST, 1);
  SERDES_SET_PCIE_CRC(state, XCLK_READY, 1);
  SERDES_SET_PCIE_CRC(state, BCON_RST, 1);

  serdes_commit(state, 1);

  serdes_pll_ovd(state, 1, 1, 1);
}
