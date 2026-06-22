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
 * Rshim JTAG access routines.
 */

#include <stdint.h>
#include <util.h>

#include <arch/jtag_drs_def.h>
#include <arch/rsh.h>

#include "cfg.h"
#include "serdes.h"


void
rshim_jtag_send(uint32_t port, unsigned long channel, const unsigned long* data,
                uint32_t jtag_inst, int num_bits)
{
  int curr_wd_idx = 0;
  int curr_wr_size;

  // Set up the JTAG controller.
  RSH_JTAG_SETUP_t rjs =
  {{
    .jtag_ena = 1,
    .jtag_rate = RSH_JTAG_SETUP__JTAG_RATE_VAL_DIV4,
    .jtag_inst = jtag_inst,
  }};
  cfg_wr(port, channel, RSH_JTAG_SETUP, rjs.word);

  // send data words
  while (num_bits)
  {
    curr_wr_size = min(num_bits, 8 * sizeof(*data));

    // setup the data
    cfg_wr(port, channel, RSH_JTAG_DATA, data[curr_wd_idx++]);

    num_bits -= curr_wr_size;

    // trigger the shift
    RSH_JTAG_CONTROL_t rjc =
    {{
      .jtag_shift_cnt = curr_wr_size,
      .jtag_cmd = num_bits ? RSH_JTAG_CONTROL__JTAG_CMD_VAL_START_SHIFT
                           : RSH_JTAG_CONTROL__JTAG_CMD_VAL_END_SHIFT,
    }};
    cfg_wr(port, channel, RSH_JTAG_CONTROL, rjc.word);

    // wait for shift to complete before sending the next one
    while (cfg_rd(port, channel, RSH_JTAG_CONTROL))
      ;
  }
}


void
bit_insert(unsigned long* dest, int dest_lsb, int dest_msb, unsigned long src)
{
  const int word_width = sizeof (unsigned long) * 8;

  int field_width = dest_msb - dest_lsb + 1;
  int field_word =  dest_lsb / word_width;
  int field_shift = dest_lsb % word_width;

  unsigned long field_mask = (1UL << field_width) - 1;

  unsigned long field_mask_0 = field_mask << field_shift;
  unsigned long field_val_0 = src << field_shift;
  dest[field_word] = (dest[field_word] & ~field_mask_0) |
                     (field_val_0 & field_mask_0);

  //
  // If the value spans two output words, handle the top half.
  //
  if (field_word != (dest_msb / word_width))
  {
    unsigned long field_mask_1 = field_mask >> (word_width - field_shift);
    unsigned long field_val_1 = src >> (word_width - field_shift);
    dest[field_word + 1] = (dest[field_word + 1] & ~field_mask_1) |
                           (field_val_1 & field_mask_1);
  }
}
