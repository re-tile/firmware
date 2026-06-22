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
 * Memory shim driver.
 */

#include <stdio.h>
#include <stdlib.h>

#include <arch/msh.h>

#include "sys/libc/include/util.h"

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "mshim.h"
#include "mshim_acc.h"
#include "types.h"


/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t mshim_alloc_lock _SHARED = DRV_SPINLOCK_INIT;

/** Address of the shared state object. */
static mshim_state_t* mshim_state[MAX_MSHIMS] _SHARED = { 0 };


// Forward reference, since we call this from probe; we could just move the
// routine here, but it's at the end of the file in all of the other
// drivers.
static long mshim_get_cur_freq(const struct dev_info* info, int clock_index);

/** Memory shim driver probe routine. */
static int
mshim_probe(const char* drvname, int instance,
            pos_t tile, const struct dev_info* info)
{
  pos_t shimaddr = info->idn_ports[0];
  int chan = info->channel;

  // Index in mshims[] where next mshim we find will go
  static int next_mshim = 0;

  MSH_BASELINE_CTL_t ctlreg =
    { .word = cfg_rd(shimaddr.word, 0, MSH_BASELINE_CTL) };

  MSH_DEV_INFO_t inforeg = 
    { .word = cfg_rd(shimaddr.word, 0, MSH_DEV_INFO) };

  int controller = inforeg.instance;

  if (ctlreg.enable)
  {
    PA memsize = cfg_rd(shimaddr.word, 0, MSH_SCRATCHPAD);

    int mshim_idx = next_mshim++;

    DEVICE_TRACE("Memory shim %d (port %d) at (%d,%d) channel %d has %#llX "
                 "bytes\n", mshim_idx, controller, UXY(shimaddr), chan,
                 memsize);

    if (mshim_idx > MAX_MSHIMS || mshims[mshim_idx] != 0)
      panic("out-of-range or duplicate mshim %d at (%d,%d)", controller,
            UXY(shimaddr));

    mshims[mshim_idx] = info;
    mshim_sizes[mshim_idx] = memsize;
    mshim_bases[mshim_idx] = (PA) mshim_idx << MSH_MAX_SIZE_SHIFT;
    mshim_controller[mshim_idx] = controller;

    //
    // Note that the speed calculation here assumes that memory is 8 bytes
    // wide; if that's ever not the case this will need to change.
    //
    mshim_speeds[mshim_idx] = 8 * mshim_get_cur_freq(info, 0);

    //
    // Compute address used by memprof to ping the shim with.
    //
    // FIXME: GX: this may need to change if the striping width changes.
    //
    if (board_flags & BOARD_STRIPE_MEMORY)
      mshim_ping[mshim_idx] = (PA) mshim_idx << 13;
    else
      mshim_ping[mshim_idx] = mshim_bases[mshim_idx];

    return (0);
  }

#ifdef USE_PCIE_AS_MEMORY
  if (next_mshim == 0)
  {
    //
    // FIXME: instead of using Trio's info struct, we're going to use
    // msh0's and just change the MDN coordinates.  This is pretty cheesy,
    // and we should do better when we get to the point of productizing
    // this.
    //
    mshims[0] = info;
    for (int i = 0; i < info->num_mdn_ports; i++)
      //
      // The evil cast is because this is const in the info struct.
      // Again, we should just use Trio's info struct eventually.
      //
      *(uint32_t *)&info->mdn_ports[i].word = __insn_mfspr(SPR_CBOX_MMAP_0);
    //
    // FIXME: we're wiring in the size and offset.  The booter puts these
    // in Trio's scratchpad register; we should eventually get this data
    // from there.
    //
    mshim_sizes[0] = 0x40000000;
    mshim_bases[0] = 0x940000000;
    mshim_controller[0] = 0;
    next_mshim++;
    return 0;
  }
#endif

  DEVICE_TRACE("Memory shim at (%d,%d) channel %d disabled\n",
               UXY(shimaddr), chan);

  return (-1);
}

/** mshim delayed interrupt handler routine.  */
static void
mshim_delayed_intr(void* intarg, void* msg, int len)
{
  mshim_state_t* ms = intarg;

  //
  // Read the interrupt status.
  //
  uint_reg_t miv = cfg_rd(ms->shim_pos.word, ms->shim_chan, MSH_INT_VEC0_W1TC);
  MSH_ECC_ERROR_INFO_t meei =
    { .word = cfg_rd(ms->shim_pos.word, ms->shim_chan, MSH_ECC_ERROR_INFO) };

  if (miv & MSH_INT_VEC0__ECC_1BIT_MASK)
  {
    //
    // It would be nice to record the PA here to see if we're getting
    // repeated errors in the same spot on on the same DIMM, but the
    // hardware doesn't provide it.  FIXME: we ought to be sending the rank
    // info, at least, to the client.  (Bit data would be nice but the
    // Linux EDAC framework may not support it.)
    //
    __insn_fetchadd4(&ms->single_bit_err_cnt, 1);
#ifdef MSH_DEBUG
    tprintf("msh%d: detected correctable memory error on rank %d",
            ms->instance, meei.onebit_rank);
    if (meei.onebit_h)
      printf(", bit %d", meei.pos_h + 72);
    if (meei.onebit_l)
      printf(", bit %d", meei.pos_l);
    printf("\n");
#endif
  }

  if (miv & MSH_INT_VEC0__ECC_2BIT_MASK)
  {
    //
    // For now, we just panic on uncorrectable memory errors.  In the
    // future, we could take more graceful exits, e.g. asking for
    // instruction from the BMC.
    //
    // FIXME: we should translate the logical rank to the DIMM label.
    //
    panic("msh%d: detected uncorrectable memory error on rank %d (%s%s)",
          ms->instance, meei.twobit_rank, meei.twobit_h ? "H" : "",
          meei.twobit_l ? "L": "");
  }

  //
  // Clear the interrupts we processed.
  //
  miv &= MSH_INT_VEC0__ECC_1BIT_MASK | MSH_INT_VEC0__ECC_2BIT_MASK;
  cfg_wr(ms->shim_pos.word, ms->shim_chan, MSH_INT_VEC0_W1TC, miv);
}


/** mshim driver init routine. */
static int
mshim_init(const char* drvname, void** statepp, int instance, int tileno,
           pos_t tile, const struct dev_info* info, const char* args)
{
  mshim_state_t* ms;

  if (instance >= MAX_MSHIMS)
    return HV_ENODEV;

  drv_spin_lock(&mshim_alloc_lock);
  ms = mshim_state[instance];
  if (ms == NULL)
  {
    ms = drv_shared_state_zalloc(sizeof(*ms), 0);
    if (ms == NULL)
    {
      drv_spin_unlock(&mshim_alloc_lock);
      return HV_ENOMEM;
    }
    mshim_state[instance] = ms;
    ms->instance = instance;
    ms->shim_pos = info->idn_ports[0];
    ms->shim_chan = info->channel;

    //
    // If ECC memory is used, set up the interrupt handling registers and
    // register the error handler.
    //
    MSH_CONTROL_t mc = { .word = cfg_rd(ms->shim_pos.word, ms->shim_chan,
                                        MSH_CONTROL) };
    if (mc.ecc_cor)
    {
      ms->ecc = 1;

      int intchan = drv_alloc_intchan();
      if (intchan < 0)
      {
        printf("hv_warning: mshim_init couldn't allocate interrupt "
               "for mshim/%d\n", instance);
        drv_spin_unlock(&mshim_alloc_lock);
        return (HV_EFAULT);
      }

      if (drv_register_intr(mshim_delayed_intr, (void*) ms,
                            DRV_INTR_DELAYED, intchan))
      {
        printf("hv_warning: mshim_init couldn't register interrupt "
               "for mshim/%d\n", instance);
        drv_spin_unlock(&mshim_alloc_lock);
        return (HV_EFAULT);
      }

      //
      // Set up the mshim bindings to direct ECC error interrupts.
      //
      MSH_INT_BIND_t intbind = {{
        .enable = 1,
        .mode = 0,
        .tileid = DRV_COORDS_TO_TILE_ID(my_pos.bits.x, my_pos.bits.y),
        .int_num = HV_PL,
        .evt_num = intchan,
        .vec_sel = MSH_INT_BIND__VEC_SEL_VAL_INTS,
      }};

      intbind.bind_sel = MSH_INT_VEC0__ECC_1BIT_SHIFT;
      cfg_wr(ms->shim_pos.word, ms->shim_chan, MSH_INT_BIND, intbind.word);

      intbind.bind_sel = MSH_INT_VEC0__ECC_2BIT_SHIFT;
      cfg_wr(ms->shim_pos.word, ms->shim_chan, MSH_INT_BIND, intbind.word);
    }
  }
  drv_spin_unlock(&mshim_alloc_lock);

  *statepp = ms;

  return 0;
}


/** mshim driver open routine. */
static int
mshim_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
           pos_t tile)
{
  mshim_state_t* ms = (mshim_state_t*) statep;

  if (*suffix == '\0' && mshims[ms->instance])
    return ms->instance;
  else
    return (HV_ENODEV);
}

/** mshim driver read routine. */
static int
mshim_pread(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  int instance = DRV_HDL2BITS(devhdl);
  mshim_state_t* ms = (mshim_state_t*) statep;

  if (offset == MSHIM_MEM_INFO_OFF)
  {
    struct mshim_mem_info mem_info =
    {
      .mem_ecc = ms->ecc,
      .mem_size = mshim_sizes[instance],
      .mem_type = DDR3,
    };

    if (len != sizeof (mem_info))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &mem_info, len, flags))
       return (HV_EFAULT);

    return (len);
  }
  else if (offset == MSHIM_MEM_ERROR_OFF)
  {
    struct mshim_mem_error mem_error =
    {
      .sbe_count = ms->single_bit_err_cnt,
    };

    if (len != sizeof (mem_error))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &mem_error, len, flags))
       return (HV_EFAULT);

    return (len);
  }

  return (HV_EINVAL);
}


/** Get the current setting for the mshim PLL. */
static long
mshim_get_cur_freq(const struct dev_info* info, int clock_index)
{
  MSH_DDR3_CLKGEN_PLL_CONTROL_t clk = 
  {
    .word = cfg_rd(info->idn_ports[0].word, info->channel,
                   MSH_DDR3_CLKGEN_PLL_CONTROL )
  };

  return pll_to_freq(clk.bypass, clk.divf, clk.divr, clk.divq, REFCLK);
}


/** Get the desired setting for the mshim PLL. */
static long
mshim_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // The mshim frequency was set at boot time, so we want to keep what
  // we've already got.
  //
  return mshim_get_cur_freq(info, clock_index);
}


/** Set the mshim PLL frequency. */
static int
mshim_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  //
  // Actually changing the mshim frequency while we're running requires
  // that we do a bunch of work to put the DIMMs into sleep mode, and
  // that may not even work.  So, for now, we just ignore any attempt
  // to make us run faster, and panic on any attempt to slow us down
  // (we could just ignore that, but if the voltage is lowered we could
  // start getting memory errors, so better to complain up front).
  //
  long cur_freq = mshim_get_cur_freq(info, clock_index);

  if (freq < cur_freq)
    panic("can't reduce mshim frequency (cur %ld, req %ld)", cur_freq, freq);

  return 0;
}


/** Memory shim driver operations vector */
static struct drv_ops mshim_ops = {
  .probe            = mshim_probe,
  .init             = mshim_init,
  .open             = mshim_open,
  .pread            = mshim_pread,
  .get_cur_freq     = mshim_get_cur_freq,
  .get_desired_freq = mshim_get_desired_freq,
  .set_freq         = mshim_set_freq,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_mshim = {
  .shim_type  = MSH_DEV_INFO__TYPE_VAL_DDR3,
  .name       = "mshim",
  .desc       = "Memory",
  .ops        = &mshim_ops,
  .stilereq   = 1,
  .flags      = DRV_FLG_AUTOMATIC,
};
