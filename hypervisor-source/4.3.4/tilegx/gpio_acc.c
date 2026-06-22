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
 * Routines to manipulate GPIO pins.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/gpio.h>

#include "cfg.h"
#include "devices.h"

//
// This code runs in both the hypervisor and the booter; we use different
// data structures to locate the GPIO shim in each case.
//
#ifdef L1BOOT
#include "hv_l1boot.h"
/** Declare the dest and chan variables, and set them to the GPIO shim's
 *  location and channel number (MMIO register offset). */
#define SET_DEST_CHAN() \
  uint32_t dest = gpioaddr.word; \
  unsigned long chan = 0
#else
/** Declare the dest and chan variables, and set them to the GPIO shim's
 *  location and channel number (MMIO register offset). */
#define SET_DEST_CHAN() \
  uint32_t dest = gpio_shims[bank]->idn_ports[0].word; \
  unsigned long chan = gpio_shims[bank]->channel
#endif

/** A routine to drive GPIO pins.  This is available after the GPIO
 *  shim has been probed.
 * @param bank Bank to use (GPIO shim number).
 * @param value New value for selected pins.
 * @param mask Mask specifying which pins to modify.
 */
void
gpio_raw_drive_pins(unsigned int bank, uint64_t value, uint64_t mask)
{
  SET_DEST_CHAN();

  if (value & mask)
    cfg_wr(dest, chan, GPIO_PIN_SET, value & mask);

  if (~value & mask)
    cfg_wr(dest, chan, GPIO_PIN_CLR, ~value & mask);
}


/** A routine to sense GPIO pin state.  This is available after the GPIO
 *  shim has been probed.
 * @param bank Bank to use (GPIO shim number).
 * @return State of all the bank's pins, as a bitmap.
 */
uint64_t
gpio_raw_sense_pins(unsigned int bank)
{
  SET_DEST_CHAN();

  return cfg_rd(dest, chan, GPIO_PIN_STATE);
}


/** A routine to configure GPIO pins.  This is available after the GPIO
 *  shim has been probed.
 * @param bank Bank to use (GPIO shim number).
 * @param disabled_pins Mask of pins to disable.
 * @param input_pins Mask of pins to enable for input.
 * @param output_pins Mask of pins to enable for output.
 * @param output_od_pins Mask of pins to enable for open-drain output.
 */
void
gpio_raw_set_dir(unsigned int bank, uint64_t disabled_pins, uint64_t input_pins,
                 uint64_t output_pins, uint64_t output_od_pins)
{
  SET_DEST_CHAN();

  uint64_t i_reg = cfg_rd(dest, chan, GPIO_PIN_DIR_I);
  uint64_t o_reg = cfg_rd(dest, chan, GPIO_PIN_DIR_O);

  i_reg = (i_reg | input_pins | output_od_pins) &
          ~(disabled_pins | output_pins);
  o_reg = (o_reg | output_pins | output_od_pins) &
          ~(disabled_pins | input_pins);

  cfg_wr(dest, chan, GPIO_PIN_DIR_I, i_reg);
  cfg_wr(dest, chan, GPIO_PIN_DIR_O, o_reg);
}

/** A routine to invert GPIO pin input values.  This is available after
 * the GPIO shim has been probed.
 * @param bank Bank to use (GPIO shim number).
 * @param inv_map Bitmap of inversion values for selected pins; if 1, the pin
 *  will be inverted.
 * @param mask Mask specifying which pins to modify.
 */
void
gpio_raw_invert_input(unsigned int bank, uint64_t inv_map, uint64_t mask)
{
  SET_DEST_CHAN();

  uint64_t regval = cfg_rd(dest, chan, GPIO_PIN_INPUT_INV);

  regval = (regval & ~mask) | (inv_map & mask);

  cfg_wr(dest, chan, GPIO_PIN_INPUT_INV, regval);
}


/** Configure a target tile and event for a GPIO interrupt.  Note that 
 *  the interrupt must be subsequently enabled via gpi_raw_unmask_interrupt()
 *  if it is to occur.
 * @param bank Bank to use (GPIO shim number).
 * @param x X coordinate of the interrupt destination tile.
 * @param y Y coordinate of the interrupt destination tile.
 * @param ipi IPI number of the interrupt.
 * @param event Event number of the interrupt.
 * @param pin Pin number.
 * @param invert If nonzero, configure the pin deassertion interrupt; else
 *  configure the pin assertion interrupt.
 */
void
gpio_raw_cfg_interrupt(unsigned int bank, int x, int y, int ipi, int event,
                       int pin, int invert)
{
  SET_DEST_CHAN();

  GPIO_INT_BIND_t intbind =
  {{
    .enable = 0,
    .mode = 0,
    .tileid = DRV_COORDS_TO_TILE_ID(x, y),
    .int_num = ipi,
    .evt_num = event,
    .bind_sel = pin,
    .vec_sel = (invert) ? GPIO_INT_BIND__VEC_SEL_VAL_PIN_DEASSERTION_INTS
                        : GPIO_INT_BIND__VEC_SEL_VAL_PIN_ASSERTION_INTS,
  }};

  cfg_wr(dest, chan, GPIO_INT_BIND, intbind.word);
}


/** Get and clear a GPIO interrupt.
 * @param bank Bank to use (GPIO shim number).
 * @param pin Pin number.
 * @param invert If nonzero, get and clear the pin deassertion interrupt; else
 *  get and clear the pin assertion interrupt.
 * @return Nonzero if the specified interrupt had occurred, 0 otherwise.
 */
int
gpio_raw_get_clear_interrupt(unsigned int bank, int pin, int invert)
{
  SET_DEST_CHAN();

  int regaddr = (invert) ? GPIO_INT_VEC1_W1TC : GPIO_INT_VEC0_W1TC;

  uint64_t int_status = cfg_rd(dest, chan, regaddr);

  if ((int_status >> pin) & 1)
  {
    cfg_wr(dest, chan, regaddr, 1ULL << pin);

    return 1;
  }

  return 0;
}


/** Mask or unmask a GPIO interrupt.
 * @param bank Bank to use (GPIO shim number).
 * @param pin Pin number.
 * @param invert If nonzero, mask the pin deassertion interrupt; else
 *  mask the pin assertion interrupt.
 * @param enable If 1, enable (unmask) the interrupt; if 0, mask it.
 */
static void
gpio_raw_mask_unmask_interrupt(unsigned int bank, int pin, int invert,
                               int enable)
{
  SET_DEST_CHAN();

  //
  // The GPIO shim doesn't actually have a mask register, so we just turn
  // off the interrupt in order to mask it, and turn it back on to unmask.
  // We don't want the caller to have to know where the interrupt is
  // targeted, so we read the current binding, change the enable bit, and
  // write it back.
  //
  GPIO_INT_BIND_t intbind =
  {{
    .nw = 1,
    .bind_sel = pin,
    .vec_sel = (invert) ? GPIO_INT_BIND__VEC_SEL_VAL_PIN_DEASSERTION_INTS
                        : GPIO_INT_BIND__VEC_SEL_VAL_PIN_ASSERTION_INTS,
  }};

  cfg_wr(dest, chan, GPIO_INT_BIND, intbind.word);

  intbind.word = cfg_rd(dest, chan, GPIO_INT_BIND);

  intbind.enable = enable;

  cfg_wr(dest, chan, GPIO_INT_BIND, intbind.word);
}


/** Mask a GPIO interrupt.
 * @param bank Bank to use (GPIO shim number).
 * @param pin Pin number.
 * @param invert If nonzero, mask the pin deassertion interrupt; else
 *  mask the pin assertion interrupt.
 */
void
gpio_raw_mask_interrupt(unsigned int bank, int pin, int invert)
{
  gpio_raw_mask_unmask_interrupt(bank, pin, invert, 0);
}


/** Unmask a GPIO interrupt.
 * @param bank Bank to use (GPIO shim number).
 * @param pin Pin number.
 * @param invert If nonzero, mask the pin deassertion interrupt; else
 *  mask the pin assertion interrupt.
 */
void
gpio_raw_unmask_interrupt(unsigned int bank, int pin, int invert)
{
  gpio_raw_mask_unmask_interrupt(bank, pin, invert, 1);
}
