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
 * USB host driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/usb_device.h>
#include <arch/usb_host.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hw_config.h"
#include "usb_host.h"
#include "usb_host_rpc_dispatch.h"

/** A convenient macro for printing warnings in standard format. */
#define WARN(...) tprintf("hv_warning: usb_host: " __VA_ARGS__)

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t usb_host_alloc_lock _SHARED = DRV_SPINLOCK_INIT;

/** Address of the shared state object. */
usb_host_state_t* usb_host_state[MAX_USB_HOSTS] _SHARED = { 0 };


/** Return the base PTE that the client should use to access our
 * shim's MMIO registers.
 */
int
handle_gxio_usb_host_get_mmio_base(usb_host_state_t* us, int is_ehci,
                                   HV_PTE *base)
{
  PA pa = us->shim_chan +
          (is_ehci ? HV_USB_HOST_MMIO_OFFSET_EHCI
                   : HV_USB_HOST_MMIO_OFFSET_OHCI);

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
handle_gxio_usb_host_check_mmio_offset(usb_host_state_t* us, int is_ehci,
                                       unsigned long offset,
                                       unsigned long size)
{
  if (offset > HV_USB_HOST_MMIO_SIZE || offset + size > HV_USB_HOST_MMIO_SIZE)
    return GXIO_ERR_MMIO_ADDRESS;

  return 0;
}


/** Configure shim interrupts. */
int
handle_gxio_usb_host_cfg_interrupt(usb_host_state_t* us, int is_ehci,
                                   int inter_x, int inter_y,
                                   int inter_ipi, int inter_event)
{
  USB_HOST_INT_BIND_t intbind = {{
    .enable = 1,
    .mode = 1,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .int_num = inter_ipi,
    .evt_num = inter_event,
    .vec_sel = USB_HOST_INT_BIND__VEC_SEL_VAL_MAC_INTS,
  }};

  if (inter_event < 0)
    intbind.enable = 0;

  if (is_ehci)
  {
    /* EHCI interrupt. */
    intbind.bind_sel = 0;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);
  }
  else
  {
    /* OHCI HCI bus general interrupt. */
    intbind.bind_sel = 1;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);

    /* OHCI HCI bus management interrupt. */
    intbind.bind_sel = 2;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);
  }

  return 0;
}


/** Register client memory. */
int
handle_gxio_usb_host_register_client_memory(usb_host_state_t* us, int is_ehci,
                                            HV_PTE pte, unsigned int flags)
{
  return drv_map_cpa_space_to_iotlb(us->shim_pos, us->port, pte,
                                    USB_DEVICE_TLB_ENTRY_ADDR__FIRST_WORD,
                                    flags);
}


/** Get the current setting for the USB host PLL. */
static long
usb_hostdrv_get_cur_freq(const struct dev_info* info, int clock_index)
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


/** Get the desired setting for the USB host PLL. */
static long
usb_hostdrv_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // The USB shim uses its base frequency to derive certain clocks whose
  // frequencies are dictated by the USB specification.  Thus, we always
  // want to run at the same speed.
  //
  return 336000000;
}


/** Set the USB host PLL frequency. */
static int
usb_hostdrv_set_freq(const struct dev_info* info, int clock_index, long freq)
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


/** USB host driver initialization routine. */
static int
usb_hostdrv_init(const char* drvname, void** statepp, int instance,
                 int tileno, pos_t tile, const struct dev_info* info,
                 const char* args)
{
  DEVICE_TRACE("usb_host_init: name %s inst %d\n", drvname, instance);

  usb_host_state_t* us;

  if (instance >= MAX_USB_HOSTS)
    return HV_ENODEV;

  drv_spin_lock(&usb_host_alloc_lock);
  us = usb_host_state[instance];
  if (us == NULL)
  {
    us = drv_shared_state_zalloc(sizeof(*us), 0);
    if (us == NULL)
    {
      drv_spin_unlock(&usb_host_alloc_lock);
      return HV_ENOMEM;
    }
    usb_host_state[instance] = us;
    us->shim_pos = info->idn_ports[0];
    us->shim_chan = info->channel;
    us->instance = instance;
    USB_HOST_DEV_INFO_t uhdi =
    {
      .word = cfg_rd(us->shim_pos.word, us->shim_chan, USB_HOST_DEV_INFO),
    };
    us->port = uhdi.instance;

    //
    // Spin up the shim PLL, in case it's not already up.  We don't do this
    // in the booter, like we do for all of the other shims, because that
    // messes up trying to use the shim in device mode.
    //
    usb_hostdrv_set_freq(info, 0, usb_hostdrv_get_desired_freq(info, 0));
  }
  drv_spin_unlock(&usb_host_alloc_lock);

  *statepp = us;

  return 0;
}


/** USB host driver open routine. */
static int
usb_hostdrv_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
                 pos_t tile)
{
  DEVICE_TRACE("usb_host_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  usb_host_state_t* us = statep;
  int intf_busy = 0;

  int is_ehci = !strcmp(suffix, "/iorpc/ehci");
  int is_ohci = !strcmp(suffix, "/iorpc/ohci");

  if (!is_ehci && !is_ohci)
    return HV_ENODEV;

  //
  // We need to figure out if the port is actually usable.  It would be
  // nicer to do this in the probe or init routine, but the probe routine
  // can't necessarily read the BIB, and if the init routine fails we get
  // complaints on the console.
  //
  // If this is port zero, figure out whether it's strapped for host mode.
  // If not, we won't use it.  (Note this test also fails if we were not
  // strapped for host mode but the usb_dev driver loaded before we did.)
  //
  if (us->port == 0)
  {
    USB_DEVICE_USB_PORT0_SELECT_t udups =
    {
      .word = cfg_rd(us->shim_pos.word, 0, USB_DEVICE_USB_PORT0_SELECT),
    };
    if (!udups.host_enable)
      return HV_ENODEV;
  }

  //
  // Figure out whether this port is enabled for host mode in the BIB;
  // again, if not, we won't use it.
  //
  union
  {
    bi_inst_t inst;
    struct bi_port_inst s_inst;
  }
  u =
  {
    .s_inst.shim = 0,
    .s_inst.port = us->port,
  };

  bi_ptr_t bp;
  if (bi_getparam(BI_TYPE_USB_PORT_CFG, u.inst, &bp, 0) != BI_NULL)
  {
    struct bi_usb_port_cfg* pc = bp;
    if (!pc->allow_host)
      return HV_ENODEV;
  }
  else
    return HV_ENODEV;

  //
  // Okay, this port is usable, try to open the driver.
  //
  drv_spin_lock(&us->lock);
  if (us->busy[is_ehci])
    intf_busy = 1;
  else
    us->busy[is_ehci] = 1;
  drv_spin_unlock(&us->lock);

  if (intf_busy)
    return HV_EBUSY;

  //
  // FIXME: may well need to do some more reset/initialization magic here.
  // For instance, we might consider resetting the PHY.
  //
  cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_MAC_ULPI_STRAP_CONTROL,
         USB_HOST_MAC_ULPI_STRAP_CONTROL__SS_ULPI_PP2VBUS_I_MASK);

  //
  // Clear the TLB. 
  //
  USB_HOST_MEM_INFO_t uhmi =
  {
    .word = cfg_rd(us->shim_pos.word, us->shim_chan, USB_HOST_MEM_INFO),
  };

  for (int entry = 0; entry < uhmi.num_tlb_ent; entry++)
  {
    USB_DEVICE_TLB_TABLE_t table =
    {{
        .entry = entry,
        .asid = us->port,
    }};
    cfg_wr(us->shim_pos.word, 0,
           USB_DEVICE_TLB_ENTRY_ADDR__FIRST_WORD + table.word, 0);
    cfg_wr(us->shim_pos.word, 0,
           USB_DEVICE_TLB_ENTRY_ATTR__FIRST_WORD + table.word, 0);
  }

  //
  // Permit MMIO access.
  //
  int err = drv_permit_mmio_access(us->shim_pos,
                                   us->shim_chan +
                                   (is_ehci ? HV_USB_HOST_MMIO_OFFSET_EHCI
                                            : HV_USB_HOST_MMIO_OFFSET_OHCI),
                                   max(HV_USB_HOST_MMIO_SIZE,
                                       HV_DEFAULT_PAGE_SIZE_SMALL), 0);

  if (err != 0)
  {
    drv_spin_lock(&us->lock);
    us->busy[is_ehci] = 0;
    drv_spin_unlock(&us->lock);
    return err;
  }

  return is_ehci;
}


/** USB host driver close routine. */
static int
usb_hostdrv_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("usb_host_close: devhdl %#x\n", devhdl);

  usb_host_state_t* us = statep;
  unsigned int is_ehci = DRV_HDL2BITS(devhdl);

  //
  // Clear any interrupts that may have been requested.
  //
  USB_HOST_INT_BIND_t intbind =
  {{
    .enable = 0,
    .vec_sel = USB_HOST_INT_BIND__VEC_SEL_VAL_MAC_INTS,
  }};

  if (is_ehci)
  {
    // EHCI interrupt.
    intbind.bind_sel = 0;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);

    // Mask interrupts in the HCI in case the device is reopened.
    cfg_wr32(us->shim_pos.word, us->shim_chan, USB_HOST_USBINTR_REG, 0);
  }
  else
  {
    // OHCI HCI bus general interrupt.
    intbind.bind_sel = 1;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);

    // OHCI HCI bus management interrupt.
    intbind.bind_sel = 2;
    cfg_wr(us->shim_pos.word, us->shim_chan, USB_HOST_INT_BIND, intbind.word);

    // Mask interrupts in the HCI in case the device is reopened.
    cfg_wr32(us->shim_pos.word, us->shim_chan,
           USB_HOST_OHCD_HC_INTERRUPT_DISABLE_REG, 0xc000007f);
  }

  //
  // Remove access to the registers.
  //
  if (drv_deny_mmio_access(us->shim_pos,
                           us->shim_chan +
                           (is_ehci ? HV_USB_HOST_MMIO_OFFSET_EHCI
                                    : HV_USB_HOST_MMIO_OFFSET_OHCI),
                           max(HV_USB_HOST_MMIO_SIZE,
                               HV_DEFAULT_PAGE_SIZE_SMALL), 0))
        WARN("unexpected deny_mmio_access() failure\n");

  //
  // Make the device available for another user.
  //
  drv_spin_lock(&us->lock);
  us->busy[(is_ehci != 0)] = 0;
  drv_spin_unlock(&us->lock);

  return 0;
}


/** USB host driver close_all routine. */
static int
usb_hostdrv_close_all(int dev_idx, void* statep)
{
  int devhdl;
  usb_host_state_t* us = statep;

  DEVICE_TRACE("usb_close_all: dev_idx %d\n", dev_idx);

  if (us->busy[0])
  {
    devhdl = MK_HDL(dev_idx, 0);
    usb_hostdrv_close(devhdl, statep, my_pos);
  }

  if (us->busy[1])
  {
    devhdl = MK_HDL(dev_idx, 1);
    usb_hostdrv_close(devhdl, statep, my_pos);
  }

  return (0);
}


/** USB host driver read routine. */
static int
usb_hostdrv_pread(int devhdl, void* statep, uint32_t flags, char* va,
                  uint32_t len, uint64_t offset, pos_t tile)
{
  usb_host_state_t* us = statep;
  unsigned int is_ehci = DRV_HDL2BITS(devhdl);

  DEVICE_TRACE("usb_host_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];

  if (len > sizeof(buf))
    return HV_EINVAL;

  drv_spin_lock(&us->lock);

  if (!us->busy[is_ehci])
  {
    drv_spin_unlock(&us->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  int result = dispatch_gxio_usb_host_read(offset, buf, len, us, is_ehci);

  drv_spin_unlock(&us->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    result = HV_EFAULT;

  return result;
}


/** USB host driver write routine. */
static int
usb_hostdrv_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  usb_host_state_t* us = statep;
  unsigned int is_ehci = DRV_HDL2BITS(devhdl);

  DEVICE_TRACE("usb_host_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];

  if (len > sizeof(buf))
    return HV_EINVAL;

  if (drv_copy_from_client(buf, va, len, flags))
    return HV_EFAULT;

  drv_spin_lock(&us->lock);

  if (!us->busy[is_ehci])
  {
    drv_spin_unlock(&us->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  int result = dispatch_gxio_usb_host_write(offset, buf, len, us, is_ehci);

  drv_spin_unlock(&us->lock);

  return result;
}


/** USB host driver operations vector. */
static struct drv_ops usb_host_ops = {
  .init             = usb_hostdrv_init,
  .open             = usb_hostdrv_open,
  .close            = usb_hostdrv_close,
  .close_all        = usb_hostdrv_close_all,
  .pread            = usb_hostdrv_pread,
  .pwrite           = usb_hostdrv_pwrite,
  .get_cur_freq     = usb_hostdrv_get_cur_freq,
  .get_desired_freq = usb_hostdrv_get_desired_freq,
  .set_freq         = usb_hostdrv_set_freq,
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_usb_host = {
  .shim_type  = USB_HOST_DEV_INFO__TYPE_VAL_USBH,
  .name       = "usb_host",
  .desc       = "USB Host interface",
  .ops        = &usb_host_ops,
};
