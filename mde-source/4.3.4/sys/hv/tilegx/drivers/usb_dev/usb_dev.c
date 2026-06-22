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
 * USB device-mode driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/usb_device.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hw_config.h"
#include "usb_dev.h"
#include "usb_dev_rpc_dispatch.h"

/** A convenient macro for printing warnings in standard format. */
#define WARN(...) tprintf("hv_warning: usb_dev: " __VA_ARGS__)

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t usb_dev_alloc_lock _SHARED = DRV_SPINLOCK_INIT;

/** Address of the shared state object. */
usb_dev_state_t* usb_dev_state[MAX_USB_DEVS] _SHARED = { 0 };


/** Return the base PTE that the client should use to access our
 * shim's MMIO registers.
 */
int
handle_gxio_usb_dev_get_mmio_base(usb_dev_state_t* us, HV_PTE *base)
{
  PA pa = us->shim_chan + HV_USB_DEV_MMIO_OFFSET;

  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(us->shim_pos.bits.x,
                                             us->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;

  return 0;
}


/** Check whether an MMIO range is legal. */
int
handle_gxio_usb_dev_check_mmio_offset(usb_dev_state_t* us,
                                       unsigned long offset,
                                       unsigned long size)
{
  if (offset > HV_USB_DEV_MMIO_SIZE || offset + size > HV_USB_DEV_MMIO_SIZE)
    return GXIO_ERR_MMIO_ADDRESS;

  return 0;
}


/** Configure shim interrupt. */
int
handle_gxio_usb_dev_cfg_interrupt(usb_dev_state_t* us,
                                   int inter_x, int inter_y,
                                   int inter_ipi, int inter_event)
{
  USB_DEVICE_INT_BIND_t intbind = {{
    .enable = 1,
    .mode = 0,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .int_num = inter_ipi,
    .evt_num = inter_event,
    .vec_sel = USB_DEVICE_INT_BIND__VEC_SEL_VAL_MAC_INTS,
  }};

  if (inter_event < 0)
    intbind.enable = 0;

  intbind.bind_sel = 0;
  cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_INT_BIND, intbind.word);

  return 0;
}


/** Reset shim interrupt. */
int
handle_gxio_usb_dev_reset_interrupt(usb_dev_state_t* us)
{
  cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_INT_VEC1_W1TC, 1);

  return 0;
}


/** Get the current setting for the USB dev PLL. */
static long
usb_devdrv_get_cur_freq(const struct dev_info* info, int clock_index)
{
  //
  // The clock control is actually resident in the device half of the shim.
  //
  USB_DEVICE_CLOCK_CONTROL_t udcc = 
  {
    .word = cfg_rd(info->idn_ports[0].word, 0, USB_DEVICE_CLOCK_CONTROL)
  };

  return pll_to_freq(!udcc.ena, udcc.pll_m, udcc.pll_n, udcc.pll_q, REFCLK);
}


/** Get the desired setting for the USB dev PLL. */
static long
usb_devdrv_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // The USB shim uses its base frequency to derive certain clocks whose
  // frequencies are dictated by the USB specification.  Thus, we always
  // want to run at the same speed.
  //
  return 336000000;
}


/** Set the USB dev PLL frequency. */
static int
usb_devdrv_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  //
  // It's possible that the frequency has already been set, either because
  // this is port 1 and we already set the clock for port 0, or because
  // this is port 1 and the chip is strapped to put port 0 in device mode
  // (which automatically spins up the clock after reset).  If that's the
  // case, we don't want to mess with the clock.  We determine whether the
  // frequency has been set by looking at the enable bit.
  //
  if (cfg_rd(info->idn_ports[0].word, 0, USB_DEVICE_CLOCK_CONTROL) &
      USB_DEVICE_CLOCK_CONTROL__ENA_MASK)
    return 0;

  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  USB_DEVICE_CLOCK_CONTROL_t udcc = 
  {{
    .ena = 1,
    .pll_m = m,
    .pll_n = n,
    .pll_q = q,
    .pll_range = range,
   }};

  cfg_wr(info->idn_ports[0].word, 0, USB_DEVICE_CLOCK_CONTROL, udcc.word);
  __insn_mf();

  do
  {
    udcc.word = cfg_rd(info->idn_ports[0].word, 0, USB_DEVICE_CLOCK_CONTROL);
  }
  while (!udcc.clock_ready);

  return 0;
}


/** USB dev driver initialization routine. */
static int
usb_devdrv_init(const char* drvname, void** statepp, int instance,
                int tileno, pos_t tile, const struct dev_info* info,
                const char* args)
{
  DEVICE_TRACE("usb_dev_init: name %s inst %d\n", drvname, instance);

  usb_dev_state_t* us;

  if (instance >= MAX_USB_DEVS)
    return HV_ENODEV;

  drv_spin_lock(&usb_dev_alloc_lock);
  us = usb_dev_state[instance];
  if (us == NULL)
  {
    //
    // Get our port instance, which we'll use below.
    //
    USB_DEVICE_DEV_INFO_t uhdi =
    {
      .word = cfg_rd(info->idn_ports[0].word, info->channel,
                     USB_DEVICE_DEV_INFO),
    };

    //
    // Figure out whether this port is enabled for device mode in the BIB;
    // if not, we're going to fail.  This will cause a console complaint,
    // but that's OK since the device shim isn't enabled by default.
    //
    union
    {
      bi_inst_t inst;
      struct bi_port_inst s_inst;
    }
    u =
    {
      .s_inst.shim = 0,
      .s_inst.port = uhdi.instance,
    };

    bi_ptr_t bp;
    if (bi_getparam(BI_TYPE_USB_PORT_CFG, u.inst, &bp, 0) == BI_NULL ||
        !((struct bi_usb_port_cfg*) bp)->allow_device)
    {
      drv_spin_unlock(&usb_dev_alloc_lock);
      return HV_ENODEV;
    }

    us = drv_shared_state_zalloc(sizeof(*us), 0);
    if (us == NULL)
    {
      drv_spin_unlock(&usb_dev_alloc_lock);
      return HV_ENOMEM;
    }

    usb_dev_state[instance] = us;
    us->shim_pos = info->idn_ports[0];
    us->shim_chan = info->channel;
    us->instance = instance;
    us->port = uhdi.instance;

    //
    // Remember whether standalone device mode (the boot/debug interface)
    // was on when we started; if so, we won't put the device shim into
    // reset until we're opened, and we'll attempt to reenable the
    // standalone mode when we're closed, although that doesn't seem to
    // be totally reliable.
    //
    USB_DEVICE_STANDALONE_DEVICE_CONFIG_t udsdc =
    {
      .word = cfg_rd(us->shim_pos.word, us->shim_chan,
                     USB_DEVICE_STANDALONE_DEVICE_CONFIG)
    };
    us->standalone = !udsdc.disable;

    if (!us->standalone)
    {
      //
      // We aren't in standalone mode.  This means that the host half of
      // port 0 is enabled, and the PLL isn't spun up yet.  It's possible
      // that port 1 will be used for host mode, which will mean the PLL
      // has to be spun up; we don't want our port to come up when that
      // happens.  This is because we only have a limited time after the
      // port comes up to program the device CSRs, and the client code that
      // will do that hasn't even been loaded yet.  So, we're going to
      // disable the host half of the port, but hold the device half in
      // reset; we'll take it out of reset when our open routine is called.
      // Finally, we'll spin up the shim PLL, since it'll need to be done
      // eventually anyway.
      //
      // Note that disabling the host side of port 0 will make the
      // usb_host/0 open routine fail, which is what keeps the client from
      // trying to use both sides of the port at once.
      //
      USB_DEVICE_USB_PORT0_SELECT_t udups =
      {
        .word = cfg_rd(us->shim_pos.word, 0, USB_DEVICE_USB_PORT0_SELECT),
      };
      if (udups.host_enable)
      {
        //
        // Oddly, you have to set the strap disable bit first, then change
        // the host enable bit in a separate MMIO write.
        //
        udups.strap_pin_disable = 1;
        cfg_wr(us->shim_pos.word, 0, USB_DEVICE_USB_PORT0_SELECT, udups.word);
        udups.host_enable = 0;
        cfg_wr(us->shim_pos.word, 0, USB_DEVICE_USB_PORT0_SELECT, udups.word);
      }

      //
      // Put the device half of the port into reset.
      //
      USB_DEVICE_RESET_CONTROL_t udrc =
      {
        .word = cfg_rd(us->shim_pos.word, us->shim_chan,
                       USB_DEVICE_RESET_CONTROL)
      };
      udrc.mac_reset_mode =
        USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_RESET_ASSERT;
      udrc.phy_reset_mode =
        USB_DEVICE_RESET_CONTROL__PHY_RESET_MODE_VAL_RESET_ASSERT;
      cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL,
             udrc.word);

      //
      // Spin up the PLL.
      //
      usb_devdrv_set_freq(info, 0, usb_devdrv_get_desired_freq(info, 0));
    }
  }
  drv_spin_unlock(&usb_dev_alloc_lock);

  *statepp = us;

  return 0;
}


/** USB dev driver open routine. */
static int
usb_devdrv_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
                pos_t tile)
{
  DEVICE_TRACE("usb_dev_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  usb_dev_state_t* us = statep;
  int intf_busy = 0;

  if (strcmp(suffix, "/iorpc"))
    return HV_ENODEV;

  //
  // Make sure we aren't already opened.
  //
  drv_spin_lock(&us->lock);
  intf_busy = us->busy;
  us->busy = 1;
  drv_spin_unlock(&us->lock);

  if (intf_busy)
    return HV_EBUSY;
  //
  // Permit MMIO access.
  //
  int err = drv_permit_mmio_access(us->shim_pos,
                                   us->shim_chan + HV_USB_DEV_MMIO_OFFSET,
                                   ROUND_UP(HV_USB_DEV_MMIO_SIZE,
                                            HV_DEFAULT_PAGE_SIZE_SMALL),
                                   0);

  if (err != 0)
  {
    drv_spin_lock(&us->lock);
    us->busy = 0;
    drv_spin_unlock(&us->lock);
    return err;
  }

  //
  // If we weren't in reset, then either we were in standalone mode, or
  // perhaps we've already been opened once.  Either way we need to reset.
  //
  USB_DEVICE_RESET_CONTROL_t udrc =
  {
    .word = cfg_rd(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL)
  };

  if (udrc.mac_reset_mode !=
      USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_RESET_ASSERT)
  {
    udrc.mac_reset_mode =
      USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_RESET_ASSERT;
    udrc.phy_reset_mode =
      USB_DEVICE_RESET_CONTROL__PHY_RESET_MODE_VAL_RESET_ASSERT;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL,
           udrc.word);

    //
    // If we were in standalone mode, then we need to disable it.
    //
    if (us->standalone)
    {
      USB_DEVICE_STANDALONE_DEVICE_CONFIG_t udsdc =
      {
        .word = cfg_rd(us->shim_pos.word, us->shim_chan,
                       USB_DEVICE_STANDALONE_DEVICE_CONFIG)
      };

      udsdc.disable = 1;
      cfg_wr(us->shim_pos.word, us->shim_chan,
             USB_DEVICE_STANDALONE_DEVICE_CONFIG, udsdc.word);
    }

    //
    // This delay may not need to be this large...
    //
    drv_udelay(1000);
  }

  //
  // Either way, we're now in reset; we need to come out.
  //
  udrc.mac_reset_mode =
    USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_AUTO;
  udrc.phy_reset_mode =
    USB_DEVICE_RESET_CONTROL__PHY_RESET_MODE_VAL_AUTO;
  cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL,
         udrc.word);

  return 0;
}


/** USB dev driver close routine. */
static int
usb_devdrv_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("usb_dev_close: devhdl %#x\n", devhdl);

  usb_dev_state_t* us = statep;

  //
  // Clear any interrupts that may have been requested.
  //
  USB_DEVICE_INT_BIND_t intbind =
  {{
    .enable = 0,
    .vec_sel = USB_DEVICE_INT_BIND__VEC_SEL_VAL_MAC_INTS,
  }};

  intbind.bind_sel = 0;
  cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_INT_BIND, intbind.word);

  //
  // Remove access to the registers.
  //
  if (drv_deny_mmio_access(us->shim_pos,
                           us->shim_chan + HV_USB_DEV_MMIO_OFFSET,
                           ROUND_UP(HV_USB_DEV_MMIO_SIZE,
                                    HV_DEFAULT_PAGE_SIZE_SMALL),
                           0))
        WARN("unexpected deny_mmio_access() failure\n");

  //
  // If we booted in standalone mode, reenable the boot/debug engine.
  // This requires that we reset the MAC and PHY interface (but not the
  // external PHY chip).
  //
  if (us->standalone)
  {
    //
    // Assert reset.
    //
    USB_DEVICE_RESET_CONTROL_t udrc =
    {
      .word = cfg_rd(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL)
    };

    udrc.mac_reset_mode =
      USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_RESET_ASSERT;
    udrc.phy_reset_mode =
      USB_DEVICE_RESET_CONTROL__PHY_RESET_MODE_VAL_RESET_ASSERT;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL,
           udrc.word);

    //
    // This delay may not need to be this large...
    //
    drv_udelay(1000);

    //
    // Reenable standalone mode.
    //
    USB_DEVICE_STANDALONE_DEVICE_CONFIG_t udsdc =
    {
      .word = cfg_rd(us->shim_pos.word, us->shim_chan,
                     USB_DEVICE_STANDALONE_DEVICE_CONFIG)
    };

    udsdc.disable = 0;
    cfg_wr(us->shim_pos.word, us->shim_chan,
           USB_DEVICE_STANDALONE_DEVICE_CONFIG, udsdc.word);

    //
    // This delay may not need to be this large...
    //
    drv_udelay(1000);

    // 
    // Deassert reset.
    //
    udrc.mac_reset_mode =
      USB_DEVICE_RESET_CONTROL__MAC_RESET_MODE_VAL_AUTO;
    udrc.phy_reset_mode =
      USB_DEVICE_RESET_CONTROL__PHY_RESET_MODE_VAL_AUTO;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_DEVICE_RESET_CONTROL,
           udrc.word);
  }

  //
  // Make the device available for another user.
  //
  drv_spin_lock(&us->lock);
  us->busy = 0;
  drv_spin_unlock(&us->lock);

  return 0;
}


/** USB dev driver close_all routine. */
static int
usb_devdrv_close_all(int dev_idx, void* statep)
{
  int devhdl = MK_HDL(dev_idx, 0);
  usb_dev_state_t* us = statep;

  DEVICE_TRACE("usb_dev_close_all: dev_idx %d\n", dev_idx);

  if (us->busy)
    usb_devdrv_close(devhdl, statep, my_pos);

  return (0);
}


/** USB dev driver read routine. */
static int
usb_devdrv_pread(int devhdl, void* statep, uint32_t flags, char* va,
                 uint32_t len, uint64_t offset, pos_t tile)
{
  usb_dev_state_t* us = statep;

  DEVICE_TRACE("usb_dev_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];

  if (len > sizeof(buf))
    return HV_EINVAL;

  drv_spin_lock(&us->lock);

  int result = dispatch_gxio_usb_dev_read(offset, buf, len, us);

  drv_spin_unlock(&us->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    result = HV_EFAULT;

  return result;
}


/** USB dev driver write routine. */
static int
usb_devdrv_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
                  uint32_t len, uint64_t offset, pos_t tile)
{
  usb_dev_state_t* us = statep;

  DEVICE_TRACE("usb_dev_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];

  if (len > sizeof(buf))
    return HV_EINVAL;

  if (drv_copy_from_client(buf, va, len, flags))
    return HV_EFAULT;

  drv_spin_lock(&us->lock);

  int result = dispatch_gxio_usb_dev_write(offset, buf, len, us);

  drv_spin_unlock(&us->lock);

  return result;
}


/** USB dev driver operations vector. */
static struct drv_ops usb_dev_ops = {
  .init             = usb_devdrv_init,
  .open             = usb_devdrv_open,
  .close            = usb_devdrv_close,
  .close_all        = usb_devdrv_close_all,
  .pread            = usb_devdrv_pread,
  .pwrite           = usb_devdrv_pwrite,
  .get_cur_freq     = usb_devdrv_get_cur_freq,
  .get_desired_freq = usb_devdrv_get_desired_freq,
  .set_freq         = usb_devdrv_set_freq,
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_usb_dev = {
  .shim_type  = USB_DEVICE_DEV_INFO__TYPE_VAL_USBS,
  .name       = "usb_dev",
  .desc       = "USB Device interface",
  .ops        = &usb_dev_ops,
};
