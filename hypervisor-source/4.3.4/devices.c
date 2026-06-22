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
 * Device handling routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip.h>

#include <arch/mpipe.h>
#include <arch/rsh.h>

#include <arch/sim.h>


#include "sys/libc/include/util.h"

#include "cfg.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "fault.h"
#include "filesys.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "idn.h"
#include "mshim_acc.h"
#include "tile.h"
#include "types.h"

const struct dev_info* mshims[MAX_MSHIMS];
PA mshim_sizes[MAX_MSHIMS];
PA mshim_bases[MAX_MSHIMS];
uint64_t mshim_speeds[MAX_MSHIMS];
uint8_t mshim_controller[MAX_MSHIMS];
PA mshim_ping[MAX_MSHIMS];

uint8_t mshim_dimm_info[MAX_MSHIMS][HV_MSH_MAX_DIMMS];

pos_t ipi_pos[MAX_IPI_SHIMS];
pos_t my_ipi_pos;

const struct dev_info* rshims[MAX_RSHIMS];
const struct dev_info* gpio_shims[MAX_GPIOS];
const struct dev_info* srom_info;
const struct dev_info* i2cm_info[MAX_I2CMS];

/** Canonicalize the driver table by converting all null function pointers
 *  to appropriate no-support/no-action functions.
 */
static void
canonicalize_driver_table()
{
  for (driver_t* drvp = driver_table_start; drvp != driver_table_end; drvp++)
  {
    struct drv_ops* ops = drvp->ops;

    if (!ops->probe)
      ops->probe = null_probe;
    if (!ops->init)
      ops->init = null_init;
    if (!ops->open)
      ops->open = no_open;
    if (!ops->close)
      ops->close = null_close;
    if (!ops->close_all)
      ops->close_all = null_close_all;
    if (!ops->pread)
      ops->pread = no_pread;
    if (!ops->pwrite)
      ops->pwrite = no_pwrite;
    if (!ops->poll)
      ops->poll = no_poll;
    if (!ops->poll_cancel)
      ops->poll_cancel = no_poll_cancel;
    if (!ops->preada)
      ops->preada = no_preada;
    if (!ops->pwritea)
      ops->pwritea = no_pwritea;
    if (!ops->msg)
      ops->msg = no_msg;
    if (!ops->service)
      ops->service = no_service;
#if MAX_DEVICE_CLOCKS > 0
    if (!ops->get_cur_freq)
      ops->get_cur_freq = no_get_cur_freq;
    if (!ops->get_desired_freq)
      ops->get_desired_freq = no_get_desired_freq;
    if (!ops->set_freq)
      ops->set_freq = no_set_freq;
#endif
  }
}


void
probe_ipic()
{
  pos_t rshim = { .word = __insn_mfspr(SPR_RSHIM_COORD) };

#if !defined(__DOXYGEN__)
  // IPI shims are special - they don't support the common MMIO registers.
  // Access the rshim directly to get the location of the IPI shims.
  RSH_IPI_LOC_t ipi_loc;
  if (rshim.word == ~0)
    panic("no rshims found");

// The !defined(__DOXYGEN__) above is for the benefit of this macro, since
// Doxygen chokes on macros defined inside functions.
#define FILL_IPI_POS(i) do {                    \
    ipi_pos[i].bits.x = ipi_loc.x ## i;         \
    ipi_pos[i].bits.y = ipi_loc.y ## i;         \
  } while (0)
  
  ipi_loc.word = cfg_rd(rshim.word, 0, RSH_IPI_LOC);
#if MAX_IPI_SHIMS >= 1
  FILL_IPI_POS(0);
#endif
#if MAX_IPI_SHIMS >= 2
  FILL_IPI_POS(1);
#endif
#if MAX_IPI_SHIMS >= 3
  FILL_IPI_POS(2);
#endif
#if MAX_IPI_SHIMS >= 4
  FILL_IPI_POS(3);
#endif
#if MAX_IPI_SHIMS >= 5
  FILL_IPI_POS(4);
#endif
#if MAX_IPI_SHIMS >= 6
  FILL_IPI_POS(5);
#endif
#if MAX_IPI_SHIMS >= 7
  FILL_IPI_POS(6);
#endif
#if MAX_IPI_SHIMS >= 8
  FILL_IPI_POS(7);
#endif
#undef FILL_IPI_POS
  if (ipi_pos[0].bits.x == 0xf && ipi_pos[0].bits.y == 0xf)
    panic("no IPI shim found");

  //
  // Find the closest shim to this tile and put it in my_ipi_pos.
  //
  my_ipi_pos = ipi_pos[0];
  int my_ipi_dist = manhattan(my_pos, my_ipi_pos);
  for (int i = 1; i < MAX_IPI_SHIMS; i++)
  {
    if (ipi_pos[i].bits.x == 0xf && ipi_pos[i].bits.y == 0xf)
      continue;

    int new_ipi_dist = manhattan(my_pos, ipi_pos[i]);
    if (new_ipi_dist < my_ipi_dist)
    {
      my_ipi_pos = ipi_pos[i];
      my_ipi_dist = new_ipi_dist;
    }
  }
#endif
}

  
void
probe_devices(unsigned long shim_mask, pos_t rshim)
{
  canonicalize_driver_table();


  DEVICE_TRACE("Probing devices, shim mask is %#lx\n", shim_mask);

  //
  // Shift the shim mask bits up to the top of the word, then reverse them,
  // so that the low bit is the first shim we're going to probe.
  //
  shim_mask <<= (8 * sizeof (shim_mask)) -
                (2 * (grid_lrhc.bits.x - grid_ulhc.bits.x + 1) +
                 2 * (grid_lrhc.bits.y - grid_ulhc.bits.y + 1));
  shim_mask = __insn_revbits(shim_mask);

  //
  // Probe the shims.  "pos" here is the off-grid coordinate of the shim
  // we're currently probing; it will be cropped appropriately in probe_shim().
  //
  pos_t pos = { .word = 0 };

  int x, y;

  //
  // Note that the overall probe order here is designed to probe every
  // device in the order that it's named on the chip data sheet; thus,
  // the first PCI-E interface we find is the one which the data sheet
  // calls "PCI-E 0", the next is the one called "PCI-E 1", etc.
  //

  // Probe left side. No need to do so if we're a 1-column configuration.

  if (1)
  {
    pos.bits.x = 0xF;  // FIXME: GX: want a #define in XML for this

    for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
    {
      pos.bits.y = y;
      if (shim_mask & 1)
        probe_shim(pos, 0);
      shim_mask >>= 1;
    }
  }
  else
    shim_mask >>= grid_lrhc.bits.y - grid_ulhc.bits.y + 1;

  // Probe top edge.

  pos.bits.y = 0xF; // FIXME: GX: want a #define in XML for this

#ifdef SWAP_MSHIM_0_1
  for (x = grid_lrhc.bits.x; x >= grid_ulhc.bits.x; x--)
#else
  for (x = grid_ulhc.bits.x; x <= grid_lrhc.bits.x; x++)
#endif
  {
    pos.bits.x = x;
    if (shim_mask & 1)
      probe_shim(pos, 0);
    shim_mask >>= 1;
  }

  // Probe bottom edge; note that this goes right-to-left.

  pos.bits.y = grid_lrhc.bits.y + 1;

  for (x = grid_lrhc.bits.x; x >= grid_ulhc.bits.x; x--)
  {
    pos.bits.x = x;
    if (shim_mask & 1)
      probe_shim(pos, 0);
    shim_mask >>= 1;
  }

  // Probe right side.

  pos.bits.x = grid_lrhc.bits.x + 1;

  for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
  {
    pos.bits.y = y;
    if (shim_mask & 1)
      probe_shim(pos, 0);
    shim_mask >>= 1;
  }

  // If we didn't find any memory, we're hosed; just go ahead and die right
  // now.

  for (int i = 0; i < MAX_MSHIMS; i++)
    if (mshims[i])
      return;

  panic("No memory shims found");
}


void
probe_shim(pos_t test_shim, unsigned long chan)
{
  DEVICE_TRACE("Probing (%d,%d) channel 0x%lx...\n", UXY(test_shim), chan);

  RSH_DEV_INFO_t info = { .word = cfg_rd(test_shim.word, chan, RSH_DEV_INFO) };

  int shim_type = info.type;

  //
  // See if we can find a place for this device in the device table.
  //
  struct device* devp = 0;
  int found = 0;

  for (devp = devices; devp->name; devp++)
  {
    if (shim_type == devp->shim_type)
    {
      if (!devp->probed)
      {
        found = 1;
        devp->probed = 1;
        break;
      }
    }
  }

  //
  // We use a shim type of zero in the simulator to act as a placeholder,
  // when we only implement some of the channels in a multi-channel device
  // but want the ones we _do_ implement to be in the right spots.  We
  // don't want to complain about that, but we _do_ want to complain if
  // we see any kind of unknown shim on real hardware.
  //
  if (!found && (shim_type || !sim_is_simulator()))
  {
    DEVICE_TRACE("Found unknown shim type %d at (%d,%d) channel 0x%lx\n",
                 shim_type, UXY(test_shim), chan);
    return;
  }

  DEVICE_TRACE("Found %s at (%d,%d) channel 0x%lx\n", devp->desc,
               UXY(test_shim), chan);

  //
  // Fill in this device's info structure.
  //
  // To keep the code from getting too hairy, we aren't implementing full
  // parsing for the memory config register here.  We only handle the
  // configurations we expect to see on the real chip, or in simulation: 1
  // port, 2 contiguous ports, 2 ports with 2 unconnected tiles between
  // them, or 3 contiguous ports.  We assume that ports don't wrap around a
  // corner of the chip.
  //
  struct dev_info* infop = &devp->info;
  infop->channel = chan;
  infop->name = devp->name;

  // Calculate position deltas from the initial IDN port.

  int xdelta = 0;  // X delta to next clockwise port
  int ydelta = 0;  // Y delta to next clockwise port

  if (test_shim.bits.y == 0xF)                    // Top edge
    xdelta = 1;
  else if (test_shim.bits.y > grid_lrhc.bits.y)   // Bottom edge
    xdelta = -1;
  else if (test_shim.bits.x == 0xF)               // Left edge
    ydelta = -1;
  else if (test_shim.bits.x > grid_lrhc.bits.x)   // Right edge
    ydelta = 1;
  else
    panic("Bad shim position");

  //
  // Create the list of ports.  We set idn_ports[0] so that we can use
  // that as our cfg_rd/wr port, even though it's not really an IDN port.
  //
  pos_t portpos = test_shim;
  infop->idn_ports[0] = infop->mdn_ports[0] = portpos;
  infop->num_idn_ports = infop->num_mdn_ports = 1;

  RSH_MEM_INFO_t rmmi = 
    { .word = cfg_rd(test_shim.word, 0, RSH_MEM_INFO) };
  uint32_t port_map = rmmi.req_ports;

  //
  // Work around hardware erratum 12642.
  //
  if (is_gx72 && !sim_is_simulator())
  {
    if (portpos.bits.x == 0xF && portpos.bits.y == 0x3)
      port_map = 0x9000;
    else if (portpos.bits.x == 0xF && portpos.bits.y == 0x5)
      port_map = 0x48000;
    else if (portpos.bits.x == 0x8 && portpos.bits.y == 0x3)
      port_map = 0x48000;
  }

  switch (port_map)
  {
    case 0x8000: // 1 port
      break;

    case 0xc000:  // 2 ports, 1 clockwise from port 0
      portpos.bits.x += xdelta;
      portpos.bits.y += ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    case 0x18000: // 2 ports, 1 counter-clockwise from port 0
      portpos.bits.x -= xdelta;
      portpos.bits.y -= ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    //
    // FIXME: gsim uses this config, but the real chip doesn't.  We
    // need to fix the simulator.
    //
    case 0xa000:  // 2 ports, 1 clockwise from port 0, stride 2
      portpos.bits.x += 2 * xdelta;
      portpos.bits.y += 2 * ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    case 0x9000:  // 2 ports, 1 clockwise from port 0, stride 3
      portpos.bits.x += 3 * xdelta;
      portpos.bits.y += 3 * ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    //
    // FIXME: gsim uses this config, but the real chip doesn't.  We
    // need to fix the simulator.
    //
    case 0x28000: // 2 ports, 1 counter-clockwise from port 0, stride 2
      portpos.bits.x -= 2 * xdelta;
      portpos.bits.y -= 2 * ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    case 0x48000: // 2 ports, 1 counter-clockwise from port 0, stride 3
      portpos.bits.x -= 3 * xdelta;
      portpos.bits.y -= 3 * ydelta;
      infop->mdn_ports[1] = portpos;
      infop->num_mdn_ports = 2;
      break;

    case 0xe000:  // 3 ports, 2 clockwise from port 0
      portpos.bits.x += xdelta;
      portpos.bits.y += ydelta;
      infop->mdn_ports[1] = portpos;
      portpos.bits.x += xdelta;
      portpos.bits.y += ydelta;
      infop->mdn_ports[2] = portpos;
      infop->num_mdn_ports = 3;
      break;

    case 0x38000: // 3 ports, 2 counter-clockwise from port 0
      portpos.bits.x -= xdelta;
      portpos.bits.y -= ydelta;
      infop->mdn_ports[1] = portpos;
      portpos.bits.x -= xdelta;
      portpos.bits.y -= ydelta;
      infop->mdn_ports[2] = portpos;
      infop->num_mdn_ports = 3;
      break;

    default:      // Anything else, we complain and stick with the first port
      if (is_master)
        printf("hv_warning: unexpected port config %#x for shim at (%d,%d)\n",
               port_map, UXY(test_shim));
      break;
  }

  //
  // Call the driver probe routine.  If it doesn't like the device, then
  // clear the probed bit; if we find another instance of this device
  // later we'll overwrite the info structure we set up above.
  //
  driver_t* drvp;
  for (drvp = driver_table_start; drvp != driver_table_end; drvp++)
  {
    if (drvp->shim_type == devp->shim_type)
    {
      struct drv_ops* ops = drvp->ops;
      if (ops->probe(drvp->name, devp->instance, my_pos, &devp->info) < 0)
        devp->probed = 0;

      break;
    }
  }

  // Paranoia?
  if (drvp == driver_table_end)
    drvp = NULL;

  //
  // If this is a system device, initialize it now; otherwise we'll wait
  // until we parse the config file so we know what driver to use.
  //
  if ((devp->flags & DEV_FLG_SYSDEV) && devp->probed && drvp)
  {
    //
    // This device might need a shared tile; if so, we'll use the boot
    // master.  Note that it doesn't make sense to have a device which
    // needs a dedicated tile as a system device, since we wouldn't know
    // where to run it.
    //
    int tileno = 0;
    if (drvp->stilereq > 0)
    {
      if (drvp->stilereq > 1)
        panic("too many shared tiles for driver %s", drvp->name);

      devp->info.num_stiles = 1;
      devp->info.stiles[0] = chip_master;
      tileno = -1;
    }

    if (drvp->ops->init(drvp->name, &devp->drv_state, devp->instance,
                        tileno, my_pos, &devp->info, NULL) >= 0)
      devp->drv = drvp;
  }

  //
  // If there might be shims within this shim at higher channel numbers,
  // do one level of recursion to probe them.
  //
  if (chan == 0)
  {
    RSH_MMIO_INFO_t mi = { .word = cfg_rd(test_shim.word, 0, RSH_MMIO_INFO) };
    if (mi.num_ch > 1)
      for (unsigned long i = 1; i < mi.num_ch; i++)



        probe_shim(test_shim, i << (CHIP_PA_WIDTH() - mi.ch_width));

  }
}


/** Initialize the memory mapping registers for devices which have them,
 *  and also initialize the memory shims appropriately.
 */
void
init_device_memory_regs()
{
  //
  // On GX, we just need to program the home map into the shim.
  //

  // We use the rshim versions of the register definitions, but we
  // expect the values to be the same for all shims.
  //
  // FIXME: actually, we're currently using mPIPE versions for some
  // registers due to bug 7340; would be marginally cleaner to use the
  // RSH_ versions once that's fixed.
  //

  for (struct device* devp = devices; devp->name; devp++)
  {
    //
    // If we didn't find this device, or it's marked for no MDN
    // configuration, don't do any.
    //
    if (!devp->probed || (devp->flags & DEV_FLG_NO_MDN_CFG))
      continue;

    RSH_MEM_INFO_t rmmi =
    {
      .word = cfg_rd(devp->info.idn_ports[0].word, devp->info.channel,
                     RSH_MEM_INFO)
    };

    if (rmmi.num_hfh_tbl > 0)
    {
      //
      // Compute the home map that we'll use.  If this device is
      // exclusively associated with a particular client, we use that
      // client's home map.  If it's not, we use this tile's home map.
      //
      // FIXME: when we support shared drivers, we'll have to make this
      // more elaborate; essentially we'll have to force all sharing clients
      // to use the same home map.
      //
      uint32_t home_map[CHIP_CBOX_HOME_MAP_SIZE()];
      int cidx = (devp->client_owner >= 0) ? devp->client_owner : my_client;

      mask_to_home_map(&config.clients[cidx].home_map_tiles, home_map);

      //
      // Write the home map into the shim.  Note that we're not setting the
      // table field in the init_ctl register, but that means "program all
      // tables the same way", which is what we want.  Note also that we
      // always use channel 0; on some shims (like USB), the HFH table
      // registers are in channel zero but only the nonzero channels have
      // a nonzero num_hfh_tbl value.
      //
      for (int i = 0; i < CHIP_CBOX_HOME_MAP_SIZE(); i++)
      {
        cfg_wr(devp->info.idn_ports[0].word, 0, MPIPE_HFH_INIT_CTL, i);
        cfg_wr(devp->info.idn_ports[0].word, 0, MPIPE_HFH_INIT_DAT,
               home_map[i]);
      }
    }
  }
}


/** Initialize all of our device drivers.
 */
void
init_drivers()
{
  struct device* devp;
  char arg[DEV_MAX_ARGLEN + 1];


  //
  // First, we make a pass through to see if this is a dedicated tile for
  // some device; if so, we're only going to initialize that one device.
  //
  for (devp = devices; devp->name; devp++)
  {
    //
    // System devices have already been initialized.
    //
    if (devp->flags & DEV_FLG_SYSDEV)
      continue;

    //
    // If we don't have a driver pointer, this device wasn't in the config
    // file.
    //
    if (!devp->drv)
      continue;

    //
    // Figure out if this is a dedicated tile; if not, go to the next device.
    //
    int tileno = 0;

    for (int i = 0; i < devp->info.num_dtiles; i++)
      if (my_pos.word == devp->info.dtiles[i].word)
      {
        tileno = i + 1;
        break;
      }

    if (!tileno)
      continue;

    //
    // Get the arguments, if any.
    //
    if (devp->arg.len > 0)
    {
      fs_pread(devp->arg.ino, arg, devp->arg.len, devp->arg.off);
      arg[devp->arg.len] = '\0';
    }
    else
      arg[0] = '\0';

    //
    // Run the init routine.  If it fails, remove this device from the config;
    // if it succeeds, we're not going to initialize anything else, so return.
    //
    int rv = devp->drv->ops->init(devp->drv->name, &devp->drv_state,
                                  devp->instance, tileno, my_pos,
                                  &devp->info, arg);
    if (rv < 0)
    {
      tprintf("hv_warning: %s init routine returned %d, removed from "
              "configuration\n", devp->name, rv);
      devp->drv = NULL;
    }
    else
      return;
  }

  //
  // On all non-dedicated tiles:
  // Initialize the device interrupt and IPI path.
  //
  init_client_intrs();

  //
  // Now we initialize everything else.
  //
  for (devp = devices; devp->name; devp++)
  {
    //
    // System devices have already been initialized.
    //
    if (devp->flags & DEV_FLG_SYSDEV)
      continue;

    //
    // If we don't have a driver pointer, this device wasn't in the config
    // file.
    //
    if (!devp->drv)
      continue;

    //
    // Figure out if this is a shared tile, to set tileno arg for init.
    // It can't be a dedicated tile, since we would have set it up in the
    // previous loop.
    //
    int tileno = 0;

    if (!tileno)
    {
      for (int i = 0; i < devp->info.num_stiles; i++)
        if (my_pos.word == devp->info.stiles[i].word)
        {
          tileno = -(i + 1);
          break;
        }
    }

    //
    // Get the arguments, if any.
    //
    if (devp->arg.len > 0)
    {
      fs_pread(devp->arg.ino, arg, devp->arg.len, devp->arg.off);
      arg[devp->arg.len] = '\0';
    }
    else
      arg[0] = '\0';

    //
    // Run the init routine; if it fails, remove this device from the config.
    // Ideally we'd rather not run init for devices which could never be
    // used from this tile, but then certain driver resources (e.g.,
    // interrupt numbers for downcalls) don't get allocated the same way on
    // all tiles, which turns out to be a bad idea.
    //
    int rv = devp->drv->ops->init(devp->drv->name, &devp->drv_state,
                                  devp->instance, tileno, my_pos,
                                  &devp->info, arg);
    if (rv < 0)
    {
      tprintf("hv_warning: %s init routine returned %d, removed from "
              "configuration\n", devp->name, rv);
      devp->drv = NULL;
    }
  }
}


/** If this is a dedicated device driver tile, execute the service routine.
 */
void
call_driver_service()
{
  struct device* devp;

  for (devp = devices; devp->name; devp++)
  {
    //
    // If we don't have a driver pointer (which means this device wasn't in
    // the config file), or it has no dedicated tiles, skip it.
    //
    if (!devp->drv || devp->drv->dtilereq <= 0)
      continue;

    //
    // Figure out if we're a dedicated tile for this device, and if so, run
    // the service routine.  If it returns, panic.
    //
    for (int i = 0; i < devp->info.num_dtiles; i++)
    {
      if (my_pos.word == devp->info.dtiles[i].word)
      {
        //
        // If we were called by slave_idle(), the IODN is marked busy because
        // we were expecting a start client message, but we won't ever get one,
        // so let's make it available.
        //
        IDN0_CLEAR_BUSY();

        is_dedicated = 1;
        init_idn_dedicated();
        devp->drv->ops->service(devp->drv_state);
        panic("%s driver service routine returned", devp->name);
      }
    }
  }
}


/** Send dedicated tiles a start_client message, which will end up starting
 *  the driver's service routine there.
 */
void
start_dedicated_tiles()
{
  struct device* devp;

  //
  // We need a default client number with which to start the dedicated tiles,
  // if their device is not exclusively owned by one particular client.
  // For now, we just use the primary client.
  //
  int default_client_num = 0;
  for (int i = 0; i < config.nclients; i++)
  {
    if (config.clients[i].flags & CLIENT_PRIMARY)
    {
      default_client_num = i;
      break;
    }
  }

  for (devp = devices; devp->name; devp++)
  {
    //
    // If we don't have a driver pointer (which means this device wasn't in
    // the config file), or it has no dedicated tiles, skip it.
    //
    if (!devp->drv || devp->drv->dtilereq <= 0)
      continue;

    //
    // Start the dedicated tiles, but skip our own tile.
    //
    for (int i = 0; i < devp->info.num_dtiles; i++)
      if (my_pos.word != devp->info.dtiles[i].word)
        start_slave_client(devp->info.dtiles[i],
                           (devp->client_owner >= 0) ? devp->client_owner : 
                                                       default_client_num);
  }
}


//
// Driver syscalls.
//

/** Find a device in the device table; factored out just because it's
 *  repeated in all the routines below. */
#define CHECK_DEV(idx, entry) \
  int idx = HDL2IDX(devhdl); \
  if (idx >= devices_end - devices || !devices[idx].drv || \
      !in_tile_mask(&devices[idx].tiles, my_pos)) \
  { \
    DEVICE_TRACE(#entry "() returns HV_ENODEV\n"); \
    return (HV_ENODEV); \
  }


int
syscall_dev_open(const char* name, uint32_t flags)
{
  DEVICE_TRACE("open(name=%p, flags=%#x)\n", name, flags);

  char namebuf[DRV_NAME_MAX];

  ON_FAULT_RETURN_EFAULT(name, sizeof (namebuf) - 1);
  strncpy(namebuf, name, sizeof (namebuf) - 1);
  FAULT_END();

  namebuf[sizeof (namebuf) - 1] = '\0';
  DEVICE_TRACE("opening %s\n", name);

  for (int i = 0; devices[i].name; i++)
  {
    int len = strlen(devices[i].name);
    if (strncmp(namebuf, devices[i].name, len) == 0 &&
        (namebuf[len] == '/' || namebuf[len] == '\0') &&
        devices[i].drv &&
        in_tile_mask(&devices[i].tiles, my_pos))
    {
      int drvbits = devices[i].drv->ops->open(MK_HDL(i, 0),
                                              devices[i].drv_state,
                                              namebuf + len,
                                              flags & HV_DEV_ALLFLAGS, my_pos);
      int retval = (drvbits < 0) ? drvbits : MK_HDL(i, drvbits);
      DEVICE_TRACE("open() returns %d %#x\n", retval, retval);
      return (retval);
    }
  }

  DEVICE_TRACE("open() returns HV_ENODEV\n");
  return (HV_ENODEV);
}

int
syscall_dev_close(int devhdl)
{
  DEVICE_TRACE("close(devhdl=%#x)\n", devhdl);

  CHECK_DEV(i, close);

  int retval = devices[i].drv->ops->close(devhdl, devices[i].drv_state, my_pos);
  DEVICE_TRACE("close() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_pread(int devhdl, uint32_t flags, char* va, uint32_t len,
                  uint64_t offset)

{
  DEVICE_TRACE("pread(devhdl=%#x, flags=%#x, va=%p, len=%d, offset=%#llx)\n",
               devhdl, flags, va, len, offset);

  CHECK_DEV(i, pread);

  int retval = devices[i].drv->ops->pread(devhdl, devices[i].drv_state,
                                          flags & HV_DEV_ALLFLAGS, va, len,
                                          offset, my_pos);
  DEVICE_TRACE("pread() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_pwrite(int devhdl, uint32_t flags, char* va, uint32_t len,
                   uint64_t offset)
{
  DEVICE_TRACE("pwrite(devhdl=%#x, flags=%#x, va=%p, len=%d, offset=%#llx)\n",
               devhdl, flags, va, len, offset);

  CHECK_DEV(i, pwrite);

  int retval = devices[i].drv->ops->pwrite(devhdl, devices[i].drv_state,
                                           flags & HV_DEV_ALLFLAGS, va, len,
                                           offset, my_pos);
  DEVICE_TRACE("pwrite() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_poll(int devhdl, uint32_t flags, uint32_t intarg)
{
  DEVICE_TRACE("poll(devhdl=%#x, flags=%#x, intarg=%#x)\n",
               devhdl, flags, intarg);

  CHECK_DEV(i, poll);

  int retval = devices[i].drv->ops->poll(devhdl, devices[i].drv_state,
                                         flags & HV_DEV_ALLFLAGS, intarg,
                                         my_pos);
  DEVICE_TRACE("poll() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_poll_cancel(int devhdl)
{
  DEVICE_TRACE("poll_cancel(devhdl=%#x)\n", devhdl);

  CHECK_DEV(i, poll_cancel);

  int retval = devices[i].drv->ops->poll_cancel(devhdl, devices[i].drv_state,
                                                my_pos);
  DEVICE_TRACE("poll_cancel() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_preada(int devhdl, uint32_t flags, uint32_t sgl_len,
                   HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg)
{
  DEVICE_TRACE("preada(devhdl=%#x, flags=%#x, sgl_len=%d, sgl=%p, "
               "offset=%#llx, intarg=%#x)\n", devhdl, flags, sgl_len, sgl,
               offset, intarg);

  CHECK_DEV(i, preada);

  if (sgl_len > HV_SGL_MAXLEN)
  {
    DEVICE_TRACE("sgl_len too long, preada() returns HV_EINVAL\n");
    return (HV_EINVAL);
  }

  HV_SGL real_sgl[sgl_len];

  for (int i = 0; i < sgl_len; i++)
  {
    if (c2r_pa(sgl[i].pa, sgl[i].len, &real_sgl[i].pa))
    {
      DEVICE_TRACE("preada() returns HV_EFAULT\n");
      return (HV_EFAULT);
    }
    real_sgl[i].len = sgl[i].len;
    real_sgl[i].pte = sgl[i].pte;
  }

  int retval = devices[i].drv->ops->preada(devhdl, devices[i].drv_state,
                                           flags & HV_DEV_ALLFLAGS, sgl_len,
                                           real_sgl, offset, intarg, my_pos);
  DEVICE_TRACE("preada() returns %d\n", retval);
  return (retval);
}


int
syscall_dev_pwritea(int devhdl, uint32_t flags, uint32_t sgl_len,
                    HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg)
{
  DEVICE_TRACE("pwritea(devhdl=%#x, flags=%#x, sgl_len=%d, sgl=%p, "
               "offset=%#llx, intarg=%#x)\n", devhdl, flags, sgl_len, sgl,
               offset, intarg);

  CHECK_DEV(i, pwritea);

  if (sgl_len > HV_SGL_MAXLEN)
  {
    DEVICE_TRACE("sgl_len too long, pwritea() returns HV_EINVAL\n");
    return (HV_EINVAL);
  }

  HV_SGL real_sgl[sgl_len];

  for (int i = 0; i < sgl_len; i++)
  {
    if (c2r_pa(sgl[i].pa, sgl[i].len, &real_sgl[i].pa))
    {
      DEVICE_TRACE("pwritea() returns HV_EFAULT\n");
      return (HV_EFAULT);
    }
    real_sgl[i].len = sgl[i].len;
    real_sgl[i].pte = sgl[i].pte;
  }

  int retval = devices[i].drv->ops->pwritea(devhdl, devices[i].drv_state,
                                            flags & HV_DEV_ALLFLAGS, sgl_len,
                                            real_sgl, offset, intarg, my_pos);
  DEVICE_TRACE("pwritea() returns %d\n", retval);
  return (retval);
}

int
syscall_close_all_devices()
{
  SYSCALL_TRACE("close_all_devices\n");
  
  for (int i = 0; i < (devices_end - devices); i++)
  {
    driver_t* drv = devices[i].drv;

    if (devices[i].client_owner == -1)
      continue;

    if (!drv || !in_tile_mask(&devices[i].tiles, my_pos))
      continue;

    drv->ops->close_all(i, devices[i].drv_state);
  }

  return 0;
}
