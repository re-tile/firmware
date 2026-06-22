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
 * Output/Input of characters to the second UART of rshim.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/rsh.h>
#include <arch/uart.h>

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "lock.h"

#include "uart.h"
#include "uart_rpc_dispatch.h"


#ifdef HV_UART_DEBUG

/** UART trace */
#define UART_TRACE(fmt, ...) \
 do \
 { \
   tprintf("hv_uart:" fmt, ## __VA_ARGS__); \
 } while (0)

#else

/** Null UART trace */
#define UART_TRACE(fmt, ...)

#endif


/** UART Device state structure pointer. */
static uart_state_t* uart_state[TILEGX_UART_NR] _SHARED = { 0 };

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t uart_alloc_lock _SHARED = DRV_SPINLOCK_INIT;


/**
 * UART driver init routine.
 */
static int
gxuart_init(const char* drvname, void** statepp, int instance, int tileno,
            pos_t tile, const struct dev_info* info, const char* args)
{
  uart_state_t* us;

  DEVICE_TRACE("%s: devname %s instance %#x tile %#x\n",
               __func__, drvname, instance, tile.word);

  drv_spin_lock(&uart_alloc_lock);
  if (instance >= TILEGX_UART_NR)
  {
    drv_spin_unlock(&uart_alloc_lock);
    return HV_ENODEV;
  }

  // Check whether the port already used as hypervisor console.
  if (((instance == 0) && !(board_flags & BOARD_CONSOLE_UART1)) ||
      ((instance == 1) && (board_flags & BOARD_CONSOLE_UART1)))
  {
    UART_TRACE("%s not available\n", info->name);
    drv_spin_unlock(&uart_alloc_lock);
    return HV_EINVAL;
  }

  us = uart_state[instance];
  if (us == NULL)
  {
    // Allocate our state.
    us = drv_shared_state_zalloc(sizeof (*us), 0);
    if (us == NULL)
    {
      drv_spin_unlock(&uart_alloc_lock);
      return HV_ENOMEM;
    }

    uart_state[instance] = us;
    drv_spin_lock_init(&us->lock);
    us->shim_pos = info->idn_ports[0];
    us->instance = instance;
    us->chan = info->channel;

    // Reset and enable UART interface.
    UART_BASELINE_CTL_t ctl;
    ctl.enable = 0;
    cfg_wr(us->shim_pos.word, us->chan, UART_BASELINE_CTL, ctl.word);
    ctl.enable = 1;
    cfg_wr(us->shim_pos.word, us->chan, UART_BASELINE_CTL, ctl.word);

    // Set Ingress in interrupt Mode.
    UART_MODE_t mode;
    mode.word = UART_MODE__UART_MODE_MASK | UART_MODE__BYPASS_MASK;
    cfg_wr(us->shim_pos.word, us->chan, UART_MODE, mode.word);
  }

  drv_spin_unlock(&uart_alloc_lock);
  *statepp = us;
  return HV_OK;
}


/**
 * UART driver open routine.
 */
static int
gxuart_open(int devhdl, void* statep, const char* suffix,
            uint32_t flags, pos_t tile)
{
  int ret = TILEGX_UART_DEV_HANDLE;
  uart_state_t* us = statep;

  DEVICE_TRACE("%s: devhdl %#x suffix \"%s\" flags %#x " "tile %#x\n",
               __func__, devhdl, suffix, flags, tile.word);

  if (!strcmp(suffix, "/iorpc"))
  {
    drv_spin_lock(&us->lock);

    // Permit MMIO access by the first caller tile.
    if (!us->mmio_mapped)
    {
      PA offset;

      offset = (us->instance) ? UART1_MMIO_BASE : UART0_MMIO_BASE;
      us->mmio_mapped = 1;

      drv_spin_unlock(&us->lock);
      ret = drv_permit_mmio_access(us->shim_pos, offset, HV_UART_MMIO_SIZE, 0);
      drv_spin_lock(&us->lock);
      if (ret)
      {
        us->mmio_mapped = 0;
        UART_TRACE("drv_permit_mmio_access() fail, err=%d\n", ret);
      }
      else
        ret = TILEGX_UART_DEV_HANDLE;
    }

    drv_spin_unlock(&us->lock);
    return ret;
  }
  return HV_ENODEV;
}


/**
 * UART driver read routine.
 */
static int
gxuart_pread(int devhdl, void* statep, uint32_t flags, char* va,
             uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[1024];
  int ret = 0;
  uart_state_t* us = statep;

  DEVICE_TRACE("%s: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", __func__, devhdl, flags,
               va, len, offset, tile.word);
  if (len > sizeof (buf))
    return HV_EINVAL;

  drv_spin_lock(&us->lock);
  ret = dispatch_gxio_uart_read(offset, buf, len, us);
  drv_spin_unlock(&us->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    ret = HV_EFAULT;

  return ret;
}


/**
 * UART driver write routine.
 */
static int
gxuart_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
              uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[1024];
  int ret = 0;
  uart_state_t* us = statep;

  DEVICE_TRACE("%s: devhdl %#x flags %#x va %p len %d offset %#llx "
               "tile %#x\n", __func__, devhdl, flags,
               va, len, offset, tile.word);
  if (len > sizeof (buf))
    return HV_EINVAL;

  if (drv_copy_from_client(buf, va, len, flags))
    return HV_EFAULT;

  drv_spin_lock(&us->lock);
  ret = dispatch_gxio_uart_write(offset, buf, len, us);
  drv_spin_unlock(&us->lock);

  return ret;
}


/**
 * UART driver close routine.
 */
static int
gxuart_close(int devhdl, void* statep, pos_t tile)
{
  int ret = HV_OK;
  uart_state_t* us = statep;

  DEVICE_TRACE("%s: devhdl %#x\n", __func__, devhdl);

  drv_spin_lock(&us->lock);
  if (us->mmio_mapped)
  {
    PA offset;

    offset = (us->instance) ? UART1_MMIO_BASE : UART0_MMIO_BASE;
    us->mmio_mapped = 0;

    drv_spin_unlock(&us->lock);
    ret = drv_deny_mmio_access(us->shim_pos, offset, HV_UART_MMIO_SIZE, 0);
    drv_spin_lock(&us->lock);
    if (ret)
      UART_TRACE("drv_deny_mmio_access() fail\n");
  }

  // Client SHOULD have required hv to stop sending UART interrupt,
  // Disable sending UART interrupt in case client not require this before.
  RSH_INT_BIND_t rib =
  {{
    .dev_sel = (us->instance) ?
      RSH_INT_BIND__DEV_SEL_VAL_UART1 : RSH_INT_BIND__DEV_SEL_VAL_UART0,
    .enable = 0,
  }};
  cfg_wr(us->shim_pos.word, 0, RSH_INT_BIND, rib.word);

  drv_spin_unlock(&us->lock);
  return ret;
}


/**
 * UART driver close_all routine.
 */
static int
gxuart_close_all(int dev_idx, void* statep)
{
  int devhdl = MK_HDL(dev_idx, TILEGX_UART_DEV_HANDLE);

  DEVICE_TRACE("%s: dev_idx %d\n", __func__, dev_idx);

  return gxuart_close(devhdl, statep, my_pos);
}

 
/** TILE-GX UART driver operations vector. */
static struct drv_ops gxuart_ops = {
  .init        = gxuart_init,
  .open        = gxuart_open,
  .close       = gxuart_close,
  .close_all   = gxuart_close_all,
  .pread       = gxuart_pread,
  .pwrite      = gxuart_pwrite,
};

/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_uart = {
  .shim_type  = UART_DEV_INFO__TYPE_VAL_UART,
  .name      = "uart",
  .desc      = "Asynchronous Serial Port Controller",
  .ops        = &gxuart_ops,
};


/** Configure shim interrupts. */
int
handle_gxio_uart_cfg_interrupt(uart_state_t* us, int inter_x, int inter_y,
			       int inter_ipi, int inter_event)
{
  int rsh_int_bind_dev_sel;

  DEVICE_TRACE("%s: instance %#x inter_x %#x inter_y %#x"
               " inter_ipi %#x inter_event %#x\n",
               __func__, us->instance, inter_x, inter_y,
               inter_ipi, inter_event);

  rsh_int_bind_dev_sel = (us->instance) ?
    RSH_INT_BIND__DEV_SEL_VAL_UART1 : RSH_INT_BIND__DEV_SEL_VAL_UART0;

  // Bind the IPI interrupt.
  RSH_INT_BIND_t rib =
  {{
    .dev_sel = rsh_int_bind_dev_sel,
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 0,
    .enable = 1,
  }};

  if (inter_event < 0)
    rib.enable = 0;

  // The register that we need to write for this is
  // in the base RShim itself, not the UART shim.
  cfg_wr(us->shim_pos.word, 0, RSH_INT_BIND, rib.word);

  return 0;
}


/** Return the base PTE that the client should use to access our
 *  shim's MMIO registers.
 */
int
handle_gxio_uart_get_mmio_base(uart_state_t* us, HV_PTE *base)
{
  PA pa = us->instance ? UART1_MMIO_BASE: UART0_MMIO_BASE;
  HV_PTE pte = {0};

  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(us->shim_pos.bits.x,
                                             us->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);
  *base = pte;

  return 0;
}


/** Check whether an MMIO range is legal. */
int
handle_gxio_uart_check_mmio_offset(uart_state_t* us, unsigned long offset,
                                   unsigned long size)
{
  if (offset > HV_UART_MMIO_SIZE || offset + size > HV_UART_MMIO_SIZE)
    return GXIO_ERR_MMIO_ADDRESS;
  return 0;
}

