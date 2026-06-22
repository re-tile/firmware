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
 * Routines to manipulate signals.
 */

#include <stdint.h>
#include <stdio.h>

#include "board_info.h"
#include "devices.h"
#include "gpio_acc.h"
#include "i2c_acc.h"

#include "hvbme/sig.h"

//
// This code runs in both the hypervisor and the booter; we use different
// data structures to locate the I2C shims in each case.
//
#ifdef L1BOOT
#include "hv_l1boot.h"
/** I2C shim address. */
#define I2C_ADDR(bus)  rshimaddr
/** I2C shim channel number (MMIO register offset). */
#define I2C_CHAN(bus)  I2CMS_CHAN(bus)
#else
/** I2C shim address. */
#define I2C_ADDR(bus)  i2cm_info[bus]->idn_ports[0]
/** I2C shim channel number (MMIO register offset). */
#define I2C_CHAN(bus)  i2cm_info[bus]->channel
#endif


static int
set_signal_i2c_pca9555(int i2c_addr, int bus, int pin, int invert,
                       int action)
{
  int assert = (action & SIGNAL_ASSERT);

  //
  // The PCA9555 only has 16 I/O pins.
  //
  if (pin > 15)
    return (-3);

  //
  // Convert the pin number to a mask and register addresses.
  //
  int out_reg = (pin > 7) ? I2C_GPIO_OUT_1 : I2C_GPIO_OUT_0;
  int pol_reg = (pin > 7) ? I2C_GPIO_POL_1 : I2C_GPIO_POL_0;
  int cfg_reg = (pin > 7) ? I2C_GPIO_CFG_1 : I2C_GPIO_CFG_0;
  pin = 1 << (pin & 7);

  int reg_val = 0;

  //
  // Do a read-modify-write on the pin value.
  //
  if (i2c_rd(I2C_ADDR(bus), I2C_CHAN(bus), i2c_addr,
             out_reg, 1, &reg_val) != 1)
  return (-4);

  if ((assert && !invert) || (!assert && invert))
    reg_val |= pin;
  else
    reg_val &= ~pin;

  if (i2c_wr(I2C_ADDR(bus), I2C_CHAN(bus), i2c_addr,
             out_reg, 1, &reg_val) != 1)
  return (-5);

  //
  // Oddly, we do the initialization step _after_ the code which is done
  // in all cases.  This is because the initialization consists of
  // configuring the GPIO expander pin to actually be an output; when we
  // do that, we want it to be putting out the value the user asked for.
  //
  if (action & SIGNAL_INIT)
  {
    //
    // Do a read-modify-write to clear the polarity value.
    //
    if (i2c_rd(I2C_ADDR(bus), I2C_CHAN(bus),
               i2c_addr, pol_reg, 1, &reg_val) != 1)
    return (-6);

    reg_val &= ~pin;

    if (i2c_wr(I2C_ADDR(bus), I2C_CHAN(bus),
               i2c_addr, pol_reg, 1, &reg_val) != 1)
    return (-7);

    //
    // Do a read-modify-write to clear the configuration value.
    //
    if (i2c_rd(I2C_ADDR(bus), I2C_CHAN(bus),
               i2c_addr, cfg_reg, 1, &reg_val) != 1)
    return (-8);

    reg_val &= ~pin;

    if (i2c_wr(I2C_ADDR(bus), I2C_CHAN(bus),
               i2c_addr, cfg_reg, 1, &reg_val) != 1)
    return (-9);
  }

  return (0);
}


int
set_signal(sigdesc_t sig_desc, int action)
{
  if (__insn_pcnt(action & (SIGNAL_ASSERT | SIGNAL_DEASSERT)) != 1)
    return (-1);

  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    uint64_t mask = (action & SIGNAL_ASSERT) ? ~0UL : 0UL;
    if (sig_desc.u.gpio.inverted)
      mask = ~mask;

    if (action & SIGNAL_INIT)
    {
      if (sig_desc.u.gpio.open_drain)
        gpio_raw_set_dir(sig_desc.u.gpio.bank, 0, 0,
                         0, 1UL << sig_desc.u.gpio.pin);
      else
        gpio_raw_set_dir(sig_desc.u.gpio.bank, 0, 0,
                         1UL << sig_desc.u.gpio.pin, 0);
    }

    gpio_raw_drive_pins(sig_desc.u.gpio.bank, mask,
                        1UL << sig_desc.u.gpio.pin);

    break;
  }

  case BI_SIGNAL_TYPE__VAL_I2C:
  {
    int i2c_addr = sig_desc.u.i2c.addr.dev_addr << 1;
    int bus = sig_desc.u.i2c.addr.bus;
    int pin = sig_desc.u.i2c.pin;
    int invert = sig_desc.u.i2c.inverted;

    if (sig_desc.u.i2c.addr.switch_inst != BI_I2C_ADDR_SWITCH_INST__VAL_NONE)
    {
#ifdef L1BOOT
      i2c_switch_swing_boot(rshimaddr, I2CMS_CHAN(sig_desc.u.i2c.addr.bus),
                            sig_desc.u.i2c.addr.bus,
                            sig_desc.u.i2c.addr.switch_inst,
                            sig_desc.u.i2c.addr.switch_chan);
#else
      i2c_switch_swing(bus, sig_desc.u.i2c.addr.switch_inst,
                       sig_desc.u.i2c.addr.switch_chan);
#endif

      int rv = set_signal_i2c_pca9555(i2c_addr, bus, pin, invert, action);

#ifdef L1BOOT
      i2c_switch_release_boot(rshimaddr, I2CMS_CHAN(sig_desc.u.i2c.addr.bus),
                              bus, sig_desc.u.i2c.addr.switch_inst);
#else
      i2c_switch_release(bus, sig_desc.u.i2c.addr.switch_inst);
#endif

      return (rv);
    }
    else
      return (set_signal_i2c_pca9555(i2c_addr, bus, pin, invert, action));
  }

  case BI_SIGNAL_TYPE__VAL_RESET:
  case BI_SIGNAL_TYPE__VAL_NONE:
  case BI_SIGNAL_TYPE__VAL_FIXED:
  default:
    return (-2);
  }

  return (0);
}

//
// All of these routines are post-4.0.0, so there is no TILEPro support.
//

static int
get_signal_i2c_pca9555(int i2c_addr, int bus, int pin, int invert,
                       int action)
{
  //
  // FIXME: we ought to implement this eventually.  (Currently the only
  // user is the PHY-less SFP link plugin, and I2C signals are pretty
  // useless there since they don't interrupt, so it's OK for now.)
  //
  return 0;
}


int
get_signal(sigdesc_t sig_desc, int action)
{
  if (__insn_pcnt(action & (SIGNAL_ASSERT | SIGNAL_DEASSERT)) != 1)
    return (-1);

  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    if (action & SIGNAL_INIT)
    {
      gpio_raw_set_dir(sig_desc.u.gpio.bank, 0, 1UL << sig_desc.u.gpio.pin,
                       0, 0);

      if (sig_desc.u.i2c.inverted)
        gpio_raw_invert_input(sig_desc.u.gpio.bank, ~0UL,
                              1UL << sig_desc.u.gpio.pin);
    }

    return (gpio_raw_sense_pins(sig_desc.u.gpio.bank) >>
            sig_desc.u.gpio.pin) & 1;

    break;
  }

  case BI_SIGNAL_TYPE__VAL_I2C:
  {
    int i2c_addr = sig_desc.u.i2c.addr.dev_addr << 1;
    int bus = sig_desc.u.i2c.addr.bus;
    int pin = sig_desc.u.i2c.pin;
    int invert = sig_desc.u.i2c.inverted;

    if (sig_desc.u.i2c.addr.switch_inst != BI_I2C_ADDR_SWITCH_INST__VAL_NONE)
    {
#ifdef L1BOOT
      i2c_switch_swing_boot(rshimaddr, I2CMS_CHAN(sig_desc.u.i2c.addr.bus),
                            sig_desc.u.i2c.addr.bus,
                            sig_desc.u.i2c.addr.switch_inst,
                            sig_desc.u.i2c.addr.switch_chan);
#else
      i2c_switch_swing(bus, sig_desc.u.i2c.addr.switch_inst,
                       sig_desc.u.i2c.addr.switch_chan);
#endif

      int rv = get_signal_i2c_pca9555(i2c_addr, bus, pin, invert, action);

#ifdef L1BOOT
      i2c_switch_release_boot(rshimaddr, I2CMS_CHAN(sig_desc.u.i2c.addr.bus),
                              bus, sig_desc.u.i2c.addr.switch_inst);
#else
      i2c_switch_release(bus, sig_desc.u.i2c.addr.switch_inst);
#endif

      return (rv);
    }
    else
      return (get_signal_i2c_pca9555(i2c_addr, bus, pin, invert, action));
  }

  default:
    return (-2);
  }
}


int
target_signal_intr(sigdesc_t sig_desc, int action, pos_t tile, int event)
{
  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    //
    // Call the get routine and ignore the results just to init the input path.
    //
    (void) get_signal(sig_desc, SIGNAL_ASSERT | SIGNAL_INIT);

    if (action & SIGNAL_ASSERT)
    {
      gpio_raw_cfg_interrupt(sig_desc.u.gpio.bank, tile.bits.x,
                             tile.bits.y, HV_PL, event,
                             sig_desc.u.gpio.pin, 0);
      gpio_raw_get_clear_interrupt(sig_desc.u.gpio.bank,
                                   sig_desc.u.gpio.pin, 0);
    }

    if (action & SIGNAL_DEASSERT)
    {
      gpio_raw_cfg_interrupt(sig_desc.u.gpio.bank, tile.bits.x,
                             tile.bits.y, HV_PL, event,
                             sig_desc.u.gpio.pin, 1);
      gpio_raw_get_clear_interrupt(sig_desc.u.gpio.bank,
                                   sig_desc.u.gpio.pin, 1);
    }

    break;
  }

  default:
    return (-2);
  }

  return 0;
}


int
enable_signal_intr(sigdesc_t sig_desc, int action)
{
  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    if (action & SIGNAL_ASSERT)
      gpio_raw_unmask_interrupt(sig_desc.u.gpio.bank, sig_desc.u.gpio.pin, 0);

    if (action & SIGNAL_DEASSERT)
      gpio_raw_unmask_interrupt(sig_desc.u.gpio.bank, sig_desc.u.gpio.pin, 1);

    break;
  }

  default:
    return (-2);
  }

  return 0;
}


int
disable_signal_intr(sigdesc_t sig_desc, int action)
{
  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    if (action & SIGNAL_ASSERT)
      gpio_raw_mask_interrupt(sig_desc.u.gpio.bank, sig_desc.u.gpio.pin, 0);

    if (action & SIGNAL_DEASSERT)
      gpio_raw_mask_interrupt(sig_desc.u.gpio.bank, sig_desc.u.gpio.pin, 1);

    break;
  }

  default:
    return (-2);
  }

  return 0;
}


int
get_clear_signal_intr(sigdesc_t sig_desc, int action)
{
  switch (sig_desc.type)
  {
  case BI_SIGNAL_TYPE__VAL_GPIO:
  {
    int rv = 0;

    if (action & SIGNAL_ASSERT)
    {
      if (gpio_raw_get_clear_interrupt(sig_desc.u.gpio.bank,
                                       sig_desc.u.gpio.pin, 0))
        rv |= SIGNAL_ASSERT;
    }

    if (action & SIGNAL_DEASSERT)
    {
      if (gpio_raw_get_clear_interrupt(sig_desc.u.gpio.bank,
                                       sig_desc.u.gpio.pin, 1))
        rv |= SIGNAL_DEASSERT;
    }

    return rv;
  }

  default:
    return (-2);
  }

  return 0;
}

