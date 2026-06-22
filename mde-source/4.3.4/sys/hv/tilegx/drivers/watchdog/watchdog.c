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
 * Watchdog driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/rsh.h>
#include <arch/sim.h>

#include "sys/libc/include/util.h"

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "fault.h"
#include "hv.h"
#include "hw_config.h"
#include "lock.h"
#include "mapping.h"
#include "types.h"
#include "watchdog.h"

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t watchdog_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state objects. */
static watchdog_state_t* watchdog_state[MAX_RSHIMS] _SHARED = { 0 };


/** Configure the down-counter.
 * @param instance Timer instance number.
 * @param wdt Timer attribute.
 * @param ws Timer state object.
 * @return non-negative instance number.
 */
static int
wdt_config_rshim(int instance, struct watchdog_attr* wdt, watchdog_state_t* ws)
{
  wdt->instance = instance;

  return (instance);
}

/** Set the timer countdown value and start the timer. If the countdown value
 *  is zero, disable the timer.
 * @param instance Timer instance number.
 * @param seconds Timer countdown value in seconds.
 * @param wdt Timer attribute.
 * @return 1, negative if error.
 */
static int
wdt_set_countdown_rshim(int instance, int seconds, struct watchdog_attr* wdt)
{
  struct watchdog_state *ws = wdt->ws;
 
  if (!sim_is_simulator())
  {
    //
    // If the timeout value is zero, the watchdog timer is disabled.
    //
    if (seconds == 0)
    {
      RSH_DOWN_COUNT_CONTROL_t down_count_control =
      {{
        .ena = 0,
      }};
      cfg_wr(ws->rshim, 0, 
             (RSH_DOWN_COUNT_CONTROL__FIRST_WORD + 0x18 * instance),
             down_count_control.word);
      return (1);
    }

    //
    // Set the timer value register.
    //
    cfg_wr(ws->rshim, 0, (RSH_DOWN_COUNT_VALUE__FIRST_WORD + 0x18 * instance),
           min((uint_reg_t) seconds * REFCLK,
               RSH_DOWN_COUNT_VALUE__COUNT_RMASK));

    //
    // Enable/start the timer.
    //
    RSH_DOWN_COUNT_CONTROL_t down_count_control =
    {{
      .ena = 1,
      .div = 0,
      .mode = RSH_DOWN_COUNT_CONTROL__MODE_VAL_CORE_REFCLK,
    }};
    cfg_wr(ws->rshim, 0, (RSH_DOWN_COUNT_CONTROL__FIRST_WORD + 0x18 * instance),
           down_count_control.word);

    //
    // Configure the timer interrupt signal to reset the chip.
    //
    RSH_WATCHDOG_CONTROL_t watchdog_control =
      { .word = cfg_rd(ws->rshim, 0, RSH_WATCHDOG_CONTROL) };
    watchdog_control.reset_ena |= (1 << instance);
    cfg_wr(ws->rshim, 0, RSH_WATCHDOG_CONTROL, watchdog_control.word);
  }

  wdt->countdown = seconds;

  return (1);
}

/** Rearm the timer chip.
 * @param instance Timer instance number.
 * @param wdt Timer attribute.
 * @return 1.
 */
static int
wdt_keep_alive_rshim(int instance, struct watchdog_attr* wdt)
{
  struct watchdog_state *ws = wdt->ws;

  //
  // Set the timer value register.
  //
  cfg_wr(ws->rshim, 0, (RSH_DOWN_COUNT_VALUE__FIRST_WORD + 0x18 * instance),
         min((uint_reg_t) wdt->countdown * REFCLK,
             RSH_DOWN_COUNT_VALUE__COUNT_RMASK));

  return (1);
}

static struct wdt_ops wdt_ops_rshim = {
  .config          = wdt_config_rshim,
  .set_countdown   = wdt_set_countdown_rshim,
  .keep_alive      = wdt_keep_alive_rshim,
};

/** WATCHDOG driver initialization routine. */
static int
watchdog_init(const char* drvname, void** statepp, int instance, int tileno,
              pos_t tile, const struct dev_info* info, const char* args)
{
  watchdog_state_t* ws;

  if (instance >= MAX_RSHIMS)
    return (HV_ENODEV);

  drv_spin_lock(&watchdog_alloc_lock);
  ws = watchdog_state[instance];

  //
  // First core to call watchdog_init allocates the shared state object.
  //
  if (ws == NULL)
  {
    ws = drv_shared_state_zalloc(sizeof(*ws), 0);
    if (ws == NULL)
    {
      drv_spin_unlock(&watchdog_alloc_lock);
      return (HV_ENOMEM);
    }
    watchdog_state[instance] = ws;
    drv_spin_lock_init(&ws->lock);
    ws->rshim = rshims[0]->idn_ports[0].word;
  }
  drv_spin_unlock(&watchdog_alloc_lock);

  *statepp = ws;

  return (0);
}


/** WATCHDOG driver open routine. */
static int
watchdog_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
              pos_t tile)
{
  watchdog_state_t* ws = statep;

  DEVICE_TRACE("watchdog_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  long instance;
  char* endptr;

  drv_spin_lock(&ws->lock);
  if (suffix[0] == '/' &&
      !str2l(&suffix[1], &endptr, 10, &instance) &&
      *endptr == '\0' &&
      instance < MAX_NUM_WATCHDOGS)
  {
    struct watchdog_attr* wdt = &ws->watchdog_attr_table[instance];
    wdt->ws = ws;
    wdt->ops = &wdt_ops_rshim;

    //
    // Do one-time timer configurations, such as source clock frequency 
    // setting, etc.
    //
    int ret = wdt->ops->config(instance, wdt, ws);
    drv_spin_unlock(&ws->lock);

    return (ret);
  }
  drv_spin_unlock(&ws->lock);

  return (HV_ENODEV);
}


/** WATCHDOG driver close routine. */
static int
watchdog_close(int devhdl, void* statep, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  watchdog_state_t* ws = statep;
  int ret;

  DEVICE_TRACE("watchdog_close: devhdl %#x\n", devhdl);

  struct watchdog_attr* wdt = &ws->watchdog_attr_table[instance];

  drv_spin_lock(&ws->lock);
  //
  // Disable the watchdog.
  //
  ret = wdt->ops->set_countdown(instance, 0, wdt);
  if (ret < 0) 
  {
    drv_spin_unlock(&ws->lock);
    return (ret);
  }

  drv_spin_unlock(&ws->lock);

  return (HV_OK);
}


/** WATCHDOG driver close_all routine. */
static int
watchdog_close_all(int dev_idx, void* statep)
{
  DEVICE_TRACE("watchdog_close_all: dev_idx %d\n", dev_idx);

  for (int instance = 0; instance < MAX_RSHIMS; instance++)
  {
    int devhdl = MK_HDL(dev_idx, instance);

    watchdog_close(devhdl, statep, my_pos);
  }

  return (0);
}


/** WATCHDOG driver read routine.  */
static int
watchdog_pread(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  watchdog_state_t* ws = statep;

  DEVICE_TRACE("watchdog_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // If the memory buffer is bad, we need to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  struct watchdog_attr* wdt = &ws->watchdog_attr_table[instance];

  drv_spin_lock(&ws->lock);
  if (offset == WATCHDOG_GET_COUNTDOWN_OFF)
  {
    if (len != sizeof (wdt->countdown))
    {
      drv_spin_unlock(&ws->lock);
      return (HV_EINVAL);
    }

    if (drv_copy_to_client(va, (char*) &wdt->countdown, len, flags))
    {
       drv_spin_unlock(&ws->lock);
       return (HV_EFAULT);
    }

    drv_spin_unlock(&ws->lock);

    return (len);
  }
  else
  {
    drv_spin_unlock(&ws->lock);

    return (HV_ENOTSUP);
  }
}


/** WATCHDOG driver write routine. */
static int
watchdog_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
                uint32_t len, uint64_t offset, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  watchdog_state_t* ws = statep;
  int ret;

  DEVICE_TRACE("watchdog_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // If the memory buffer is bad, we need to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  struct watchdog_attr* wdt = &ws->watchdog_attr_table[instance];

  drv_spin_lock(&ws->lock);
  if (offset == WATCHDOG_ENABLE_OFF)
  {
    int countdown;
    if (len != sizeof(countdown))
    {
      drv_spin_unlock(&ws->lock);
      return (HV_EINVAL);
    }

    if (drv_copy_from_client((char*) &countdown, va, len, flags))
    {
      drv_spin_unlock(&ws->lock);
      return (HV_EFAULT); 
    }

    ret = wdt->ops->set_countdown(instance, countdown, wdt);
    if (ret < 0)
    {
      drv_spin_unlock(&ws->lock);
      return (ret);
    }

    drv_spin_unlock(&ws->lock);

    return (len);
  }
  else if (offset == WATCHDOG_DISABLE_OFF)
  {
    ret = wdt->ops->set_countdown(instance, 0, wdt);
    if (ret < 0)
    {
      drv_spin_unlock(&ws->lock);
      return (ret);
    }

    drv_spin_unlock(&ws->lock);

    return (len);
  }
  else if (offset == WATCHDOG_PAT_OFF)
  {
    ret = wdt->ops->keep_alive(instance, wdt);
    if (ret < 0)
    {
      drv_spin_unlock(&ws->lock);
      return (ret);
    }

    drv_spin_unlock(&ws->lock);

    return (len);
  }
  else
  {
    drv_spin_unlock(&ws->lock);
    return (HV_ENOTSUP);
  }
}


/** WATCHDOG driver operations vector */
static struct drv_ops watchdog_ops = {
  .init        = watchdog_init,
  .open        = watchdog_open,
  .close       = watchdog_close,
  .close_all   = watchdog_close_all,
  .pread       = watchdog_pread,
  .pwrite      = watchdog_pwrite,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_watchdog = {
  .shim_type  = DEV_PSEUDO_WATCHDOG,
  .name       = "watchdog",
  .desc       = "RSHIM WATCHDOG",
  .ops        = &watchdog_ops,
};

