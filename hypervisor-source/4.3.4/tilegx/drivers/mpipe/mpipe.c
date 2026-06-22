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
 * mPIPE driver.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "drvintf.h"
#include "devices.h"
#include "hw_config.h"

#include "mpipe.h"
#include "rules.h"

#include "mpipe_rpc_dispatch.h"
#include "mpipe_info_rpc_dispatch.h"

#include <arch/cycle.h>
#include <arch/mpipe_shm.h>
#include <arch/sim.h>


/** A convenient macro for printing warnings in standard format. */
#define WARN(...) tprintf("hv_warning: mpipe: " __VA_ARGS__)

/** Tracing infrastructure for debug. */
#if 0
#define TRACE(...) tprintf("mpipe: " __VA_ARGS__)
#else
#define TRACE(...)
#endif

/** Some APIs require a client number, but I don't know how to get
    one, so for now just use a static one. */
#define CLIENTNO 0

/** The largest RPC buffer we're willing to put on the stack. */
#define MAX_STACK_BYTES 4096


/** Lock used to make sure that only one tile allocates shared state. */
static spinlock_t mpipe_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state objects. */
static mpipe_state_t* mpipe_state[MAX_MPIPES] _SHARED = { 0 };

/** Nanoseconds per second. */
static const long ns_per_sec = 1000000000;

/** All shim buffer stack allocated mask. */
static uint64_t all_shim_buffer_stack_allocated_mask _SHARED = 0;

/** Flush the micro-TLBs. */
static void
flush_micro_tlbs(mpipe_state_t* ms)
{
  MPIPE_TLB_CTL_t ctl = {{ .mtlb_flush = 1 }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_TLB_CTL, ctl.word);
}


/** Freeze/unfreeze the load balancer. */
static void
freeze_load_balancer(mpipe_state_t* ms, bool freeze)
{
  MPIPE_LBL_CTL_t ctl;

  ctl.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_LBL_CTL);
  ctl.freeze = freeze;
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_CTL, ctl.word);

  if (freeze)
    __insn_mf();
}


/** Configure the timestamp registers. */
static void
config_timestamp(mpipe_state_t* ms)
{
  //
  // The timestamp mechanism isn't yet implemented in the simulator.
  //
  if (sim_is_simulator())
    return;

  //
  // Configure the timestamp registers.
  //

  //
  // First, figure out what speed we're running at, and what speed we
  // want the timestamp to run at.
  //
  MPIPE_PCLK_CONTROL_t mcc =
  {
    .word = cfg_rd(ms->shim_pos.word, 0, MPIPE_PCLK_CONTROL)
  };

  long pclk_freq = pll_to_freq(0, mcc.pll_m, mcc.pll_n, mcc.pll_q, REFCLK);
  long ts_freq = ms->tstamp_is_cycle ? cpu_speed : ns_per_sec;

  if (ms->tstamp_is_cycle && ts_freq > 2 * pclk_freq)
  {
    printf("hv_warning: mpipe/%d: mPIPE core frequency too slow for "
           "timestamp-is-cycle option, ignored\n", ms->instance);
    ms->tstamp_is_cycle = 0;
    ts_freq = ns_per_sec;
  }

  if (ts_freq > 2 * pclk_freq)
  {
    while (ts_freq > 2 * pclk_freq)
      ts_freq >>= 1;

    printf("hv_warning: mpipe/%d: mPIPE pclk frequency too slow for "
           "1 ns timestamps, timestamp frequency will be %ld Hz\n",
           ms->instance, ts_freq);
  }

  //
  // Now figure out what the divisor/dividend need to be to make the
  // timestamp tick at the correct rate.  The timestamp ticks at
  // a frequency of (inc / thr) * pclk_freq.
  //
  long best_thr = 1;
  long best_inc = 1;
  long best_diff = LONG_MAX;
  long best_rem = LONG_MAX;

  for (long this_thr = 1 << 15; this_thr < 1 << 16; this_thr++)
  {
    long this_inc = (ts_freq * this_thr) / pclk_freq;

    if (this_inc >= 2 * this_thr)
      continue;

    long this_ts_freq = (this_inc * pclk_freq) / this_thr;
    long this_rem = (this_inc * pclk_freq) % this_thr;

    long this_diff = abs(this_ts_freq - ts_freq);

    if (this_diff < best_diff ||
        (this_diff == best_diff && this_rem < best_rem))
    {
      best_thr = this_thr;
      best_inc = this_inc;
      best_diff = this_diff;
      best_rem = this_rem;
    }

    if (best_diff == 0 && best_rem == 0)
      break;
  }

  MPIPE_TIMESTAMP_CAL_t mtc =
  {{
    .thr = best_thr,
    .inc = best_inc,
  }};

  cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_CAL, mtc.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_RES, 0);

  //
  // Finally, if requested, synchronize the current timestamp value to
  // the cycle count.  This is necessarily approximate.
  //
  if (ms->tstamp_is_cycle)
  {
    //
    // First, we'll just read the cycle count, compute the proper
    // timestamp value, and jam it in there.  This will get us within a
    // second, which is necessary for the code below to work.
    //
    MPIPE_TIMESTAMP_VAL_t mtv;
    long cyc = get_cycle_count();
    mtv.sec = cyc / ns_per_sec;
    mtv.ns = cyc % ns_per_sec;

    cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_VAL, mtv.word);

    //
    // Now we do the fine adjustment.  We read the timestamp value, and
    // take the cycle count before and after.  We then take the average
    // of the two cycle counts as an estimate of the timestamp value we
    // should have gotten back (there may be a few cycles skew in this
    // but it's about the best estimate we're going to get).  Finally we
    // use the timestamp adjustment register to adjust the timestamp to
    // what we expected it to be.
    //
    // We run this several times since there seem to be I$ effects which
    // give us lousy results on the first pass (and which therefore throw
    // off the second pass).  Experiments show that things converge on
    // the third pass, so we do 4 just to be paranoid.
    //
    for (int i = 0; i < 4; i++)
    {
      long cyc1 = get_cycle_count();
      mtv.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_VAL);
      long cyc2 = get_cycle_count();

      long ns = mtv.sec * ns_per_sec + mtv.ns;

      long delta = (cyc1 + cyc2) / 2 - ns;
      cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_NS_ADJ, delta);
    }
  }
}


/** A helper routine for validating the service domain bits in a
    client device file handle. */
static bool
is_open_svc_dom(unsigned int index, mpipe_state_t* ms)
{
  return ((index < MPIPE_MMIO_NUM_SVC_DOM) &&
          (index != RESERVED_SVC_DOM) &&
          !(ms->svc_dom_avail_mask & (1ull << index)));
}


/** Mpipe driver init routine. */
static int
mpipe_init(const char* drvname, void** statepp, int instance,
           int tileno, pos_t tile, const struct dev_info* info,
           const char* args)
{
  if (instance >= MAX_MPIPES)
    return HV_ENODEV;

  spin_lock(&mpipe_alloc_lock);

  mpipe_state_t* ms = mpipe_state[instance];
  if (ms == NULL)
  {
    ms = drv_shared_state_zalloc(sizeof(*ms), 0);
    if (ms == NULL)
    {
      spin_unlock(&mpipe_alloc_lock);
      return HV_EFAULT;
    }

    mpipe_state[instance] = ms;

    // Initialize the new object.
    spin_lock_init(&ms->lock);
    ms->svc_dom_avail_mask = (1ull << MPIPE_MMIO_NUM_SVC_DOM) - 1;
    ms->shim_pos = info->idn_ports[0];
    ms->instance = instance;

    union virt_inst {
      int instance;
      struct bi_clock_inst clock;
    } virt_inst = {
      .clock.type = BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN,
      .clock.shim = ms->instance,
    };

    bi_ptr_t bp;
    if (bi_getparam(BI_TYPE_SHIM_VIRT_INST, virt_inst.instance, &bp, NULL) !=
        BI_NULL)
    {
      struct bi_shim_virt_inst* bsvi = (struct bi_shim_virt_inst*) bp;
      ms->virt_instance = bsvi->virt_inst;
    }
    else
      ms->virt_instance = instance;

    //
    // Parse any driver arguments.
    //
    int args_len = args ? strlen(args) : 0;
    char args_copy[args_len + 1];

    if (args != NULL)
    {
      strcpy(args_copy, args);

      char* argptr = args_copy;
      char* opt;
      char* val;

      while (drv_next_opt(&argptr, &opt, &val))
      {
        if (!strcmp(opt, "timestamp-is-cycle"))
        {
          ms->tstamp_is_cycle = 1;
        }
        else
        {
          printf("hv_warning: mpipe/%d: unrecognized option %s, ignored\n",
                 ms->instance, opt);
        }
      }
    }

    // We reserve service domain 0 for use by the hypervisor.
    ms->svc_dom_avail_mask &= ~(1ull << RESERVED_SVC_DOM);

    // Initialize the hardware.
    for (int svc_dom = 0; svc_dom < MPIPE_MMIO_NUM_SVC_DOM; svc_dom++)
    {
      if (svc_dom == RESERVED_SVC_DOM)
        continue;

      // NOTE: This changes "cfg_prot_level" from 3 to 0.
      MPIPE_MMIO_INIT_CTL_t ctl = {{ .svc_dom_idx = svc_dom }};
      cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_CTL, ctl.word);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_DAT, 0);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_DAT, 0);
    }

    // ISSUE: These registers are not implemented on the simulator.
    if (!sim_is_simulator())
    {
      // Put all the stacks into "idma_asid_fault_mode".
      MPIPE_IDMA_ASID_FAULT_MODE_t miafm = {{ .flush = ~0 }};
      cfg_wr(ms->shim_pos.word, 0, MPIPE_IDMA_ASID_FAULT_MODE, miafm.word);

      // Put all the stacks into "edma_asid_fault_mode".
      MPIPE_EDMA_ASID_FAULT_MODE_t meafm = {{ .flush = ~0 }};
      cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_ASID_FAULT_MODE, meafm.word);
    }

    // Zero out TLB (including the "vld" bits) before allowing the
    // user to access the IOTLB, since the hardware comes out of reset
    // with the first entry as a valid, PA=VA mapping.
    for (int stack = 0; stack < HV_MPIPE_NUM_BUFFER_STACKS; stack++)
    {
      for (int entry = 0; entry < MPIPE_NUM_TLBS_PER_ASID; entry++)
      {
        MPIPE_TLB_TABLE_t table = {{
            .entry = entry,
            .asid = stack
          }};
        cfg_wr(ms->shim_pos.word, 0,
               MPIPE_TLB_ENTRY_ADDR__FIRST_WORD + table.word, 0);
        cfg_wr(ms->shim_pos.word, 0,
               MPIPE_TLB_ENTRY_ATTR__FIRST_WORD + table.word, 0);
      }
    }
    flush_micro_tlbs(ms);

    // Disable all the rings.
    for (int ring = 0; ring < MPIPE_NUM_NOTIF_RINGS; ring++)
    {
      MPIPE_LBL_INIT_CTL_t ctl = {{
          .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_NR_TBL,
          .idx = ring * 2,
        }};




      MPIPE_LBL_INIT_DAT_NR_TBL_0_t data0 = {{ 0 }};


      MPIPE_LBL_INIT_DAT_NR_TBL_1_t data1 = {{ .count = 0xfffe }};

      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data0.word);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data1.word);
    }

    // Remove all rings from all groups.
    for (int group = 0; group < MPIPE_NUM_NOTIF_GROUPS; group++)
    {
      MPIPE_LBL_INIT_CTL_t ctl = {{
          .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_GROUP_TBL,
          .idx = group * 4,
        }};
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);

      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
    }

    // Zero out all the buckets.
    MPIPE_LBL_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_BSTS_TBL,
        .idx = 0,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);
    for (int bucket = 0; bucket < MPIPE_NUM_BUCKETS; bucket++)
    {
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
    }

    // Forbid all erings from using any buffer stacks.
    for (int ering = 0; ering < HV_MPIPE_ALLOC_EDMA_RINGS_BITS; ering++)
    {
      MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
          .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_STACK_PROT,
          .idx = ering,
        }};
      cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, 0);
    }

    // Build our list of available links.
    init_link_data(ms);

    // Configure the timestamp registers.
    config_timestamp(ms);
  }

  // The shared tile registers for link interrupts.
  if (tileno < 0)
    init_link_intrs(ms);

  spin_unlock(&mpipe_alloc_lock);

  *statepp = ms;

  return 0;
}


/** mpipe driver open routine - a new context number for each open. */
static int
mpipe_open(int devhdl, void* statep, const char* suffix,
           uint32_t flags, pos_t tile)
{
  mpipe_state_t* ms = statep;
  int err;

  DEVICE_TRACE("mpipe_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  if (!strcmp(suffix, "/iorpc"))
  {
    spin_lock(&ms->lock);

    if (ms->svc_dom_avail_mask == 0)
    {
      spin_unlock(&ms->lock);
      return GXIO_ERR_NO_SVC_DOM;
    }

    int svc_dom = ffs(ms->svc_dom_avail_mask) - 1;

    // Optimistically commit the service domain allocation.
    ms->svc_dom_avail_mask &= ~(1ull << svc_dom);

    spin_unlock(&ms->lock);

    // The permit/deny calls below can deadlock if the lock is held!

    // Permit CONFIG_MMIO access at the address corresponding to svc_dom.
    PA base = (PA)svc_dom << MPIPE_MMIO_ADDR__SVC_DOM_SHIFT;
    err = drv_permit_mmio_access(ms->shim_pos,
                                 base + HV_MPIPE_CONFIG_MMIO_OFFSET,
                                 HV_MPIPE_CONFIG_MMIO_SIZE, CLIENTNO);
    if (err != 0)
    {
      WARN("Unexpected permit_mmio_access() failure(1)\n");
    oops1:
      // Uncommit the service domain allocation.
      spin_lock(&ms->lock);
      ms->svc_dom_avail_mask |= (1ull << svc_dom);
      spin_unlock(&ms->lock);
      return err;
    }

    // Permit FAST_MMIO access at the address corresponding to svc_dom.
    err = drv_permit_mmio_access(ms->shim_pos,
                                 base + HV_MPIPE_FAST_MMIO_OFFSET,
                                 HV_MPIPE_FAST_MMIO_SIZE, CLIENTNO);
    if (err != 0)
    {
      WARN("Unexpected permit_mmio_access() failure(2)\n");
    oops2:
      // Forbid the CONFIG_MMIO access which was permitted above.
      drv_deny_mmio_access(ms->shim_pos, base + HV_MPIPE_CONFIG_MMIO_OFFSET,
                           HV_MPIPE_CONFIG_MMIO_SIZE, CLIENTNO);
      goto oops1;
    }

    spin_lock(&ms->lock);
    err = mpipe_open_aux(ms, svc_dom);
    spin_unlock(&ms->lock);

    if (err != 0)
    {
      WARN("Unexpected classifier load failure\n");
      goto oops2;
    }

    // Use the service domain as part of the opaque fd number.
    return svc_dom;
  }
  else if (!strcmp(suffix, "/iorpc_info"))
  {
    //
    // This is a magic device used only for the link enumerate and instance
    // IORPC calls.  It's not associated with a hardware service domain.
    //
    return INFO_SVC_DOM;
  }

  return HV_ENODEV;
}


// Forward declaration.
static void
mpipe_close_cleanup(mpipe_state_t* ms, int svc_dom);


/** mpipe driver close routine. */
static int
mpipe_close(int devhdl, void* statep, pos_t tile)
{
  mpipe_state_t* ms = statep;
  unsigned int svc_dom = DRV_HDL2BITS(devhdl);

  DEVICE_TRACE("mpipe_close: devhdl %#x\n", devhdl);

  // If it's the magic link info device, just return success.
  if (svc_dom == INFO_SVC_DOM)
    return 0;

  spin_lock(&ms->lock);

  if (!is_open_svc_dom(svc_dom, ms))
  {
    spin_unlock(&ms->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  mpipe_close_cleanup(ms, svc_dom);

  spin_unlock(&ms->lock);

  // We're releasing the svc_dom, so don't allow any more MMIO.
  // ISSUE: These calls cannot be done while holding the lock.
  PA base = (PA)svc_dom << MPIPE_MMIO_ADDR__SVC_DOM_SHIFT;
  if (drv_deny_mmio_access(ms->shim_pos, base + HV_MPIPE_CONFIG_MMIO_OFFSET,
                           HV_MPIPE_CONFIG_MMIO_SIZE, CLIENTNO) ||
      drv_deny_mmio_access(ms->shim_pos, base + HV_MPIPE_FAST_MMIO_OFFSET,
                           HV_MPIPE_FAST_MMIO_SIZE, CLIENTNO))
    WARN("Unexpected deny_mmio_access() failure at close\n");

  spin_lock(&ms->lock);

  // Allow some future call to reuse the service domain.
  ms->svc_dom_avail_mask |= (1ull << svc_dom);

  // Clean up the mpipe stat config
  if (ms->stat_config_on && ms->stat_config_svc_dom == svc_dom)
  {
    ms->stat_config_on = false;
    memset(ms->stat_config, 0, sizeof(ms->stat_config));
  }
  spin_unlock(&ms->lock);

  return 0;
}


/** mpipe driver close_all routine. */
static int
mpipe_close_all(int dev_idx, void* statep)
{
  DEVICE_TRACE("mpipe_close_all: dev_idx %d\n", dev_idx);

  for (int svc_dom = 0; svc_dom < MPIPE_MMIO_NUM_SVC_DOM; svc_dom++)
  {
    int devhdl = MK_HDL(dev_idx, svc_dom);

    mpipe_close(devhdl, statep, my_pos);
  }

  return (0);
}


/** mpipe driver read routine. */
static int
mpipe_pread(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[MAX_STACK_BYTES];
  mpipe_state_t* ms = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("mpipe_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(buf))
    return HV_EINVAL;

  spin_lock(&ms->lock);

  if (!is_open_svc_dom(index, ms) && index != INFO_SVC_DOM)
  {
    result = GXIO_ERR_INVAL_SVC_DOM;
    goto end;
  }

  if (index == INFO_SVC_DOM)
    result = dispatch_gxio_mpipe_info_read(offset, buf, len, ms, index);
  else
    result = dispatch_gxio_mpipe_read(offset, buf, len, ms, index);

  if (drv_copy_to_client(va, buf, len, flags))
  {
    result = HV_EFAULT;
    goto end;
  }

 end:
  spin_unlock(&ms->lock);
  return result;
}


/** mpipe driver write routine. */
static int
mpipe_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
             uint32_t len, uint64_t offset, pos_t tile)
{
  mpipe_state_t* ms = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("mpipe_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(ms->rpc_buf))
    return HV_EINVAL;

  spin_lock(&ms->lock);

  if (!is_open_svc_dom(index, ms) && index != INFO_SVC_DOM)
  {
    result = GXIO_ERR_INVAL_SVC_DOM;
    goto end;
  }

  if (drv_copy_from_client(ms->rpc_buf, va, len, flags))
  {
    result = HV_EFAULT;
    goto end;
  }

  if (index == INFO_SVC_DOM)
    result = dispatch_gxio_mpipe_info_write(offset, ms->rpc_buf, len, ms,
                                            index);
  else
    result = dispatch_gxio_mpipe_write(offset, ms->rpc_buf, len, ms, index);

 end:
  spin_unlock(&ms->lock);
  return result;
}


/** Get the current setting for an mPIPE PLL. */
static long
mpipe_get_cur_freq(const struct dev_info* info, int clock_index)
{
  int regaddr = clock_index ? MPIPE_KCLK_CONTROL : MPIPE_PCLK_CONTROL;

  MPIPE_PCLK_CONTROL_t mcc = {
    .word = cfg_rd(info->idn_ports[0].word, info->channel, regaddr)
  };

  return pll_to_freq(!mcc.ena, mcc.pll_m, mcc.pll_n, mcc.pll_q, REFCLK);
}


/** Get the desired setting for an mPIPE PLL. */
static long
mpipe_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // If it's set in the .hvc, use that value.
  //
  if (info->speeds[clock_index])
    return info->speeds[clock_index];

  //
  // See if there's a board default in the BIB, and if so, use it.
  //
  MPIPE_DEV_INFO_t mdi = {
    .word = cfg_rd(info->idn_ports[0].word, info->channel, MPIPE_DEV_INFO)
  };

  union
  {
    bi_inst_t inst;
    struct bi_clock_inst bci;
  } ci = {
    .bci.type = clock_index ? BI_CLOCK_INST_TYPE__VAL_MPIPE_CLASSIFIER :
    BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN,
    .bci.shim = mdi.instance,
  };

  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_SHIM_CLOCK, ci.inst, &bp, NULL) != BI_NULL)
  {
    struct bi_shim_clock* sc = bp;
    return sc->freq;
  }

  //
  // If we haven't found a value, we want to run as fast as we can subject
  // to the voltage required by other shims.
  //
  return DRV_DESIRED_FREQ_MAX;
}


/** Set an mPIPE PLL frequency. */
static int
mpipe_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  //
  // FIXME: must temporarily disable the classifier before changing
  // its clock speed
  //
  int regaddr = clock_index ? MPIPE_KCLK_CONTROL : MPIPE_PCLK_CONTROL;

  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  MPIPE_PCLK_CONTROL_t mcc = {{
      .ena = 1,
      .pll_m = m,
      .pll_n = n,
      .pll_q = q,
      .pll_range = range,
    }};

  cfg_wr(info->idn_ports[0].word, info->channel, regaddr, mcc.word);
  __insn_mf();

  do
  {
    mcc.word = cfg_rd(info->idn_ports[0].word, info->channel, regaddr);
  }
  while (!mcc.clock_ready);

  return 0;
}


/** mpipe driver operations vector */
static struct drv_ops mpipe_ops = {
  .init             = mpipe_init,
  .open             = mpipe_open,
  .close            = mpipe_close,
  .close_all        = mpipe_close_all,
  .pread            = mpipe_pread,
  .pwrite           = mpipe_pwrite,
  .get_cur_freq     = mpipe_get_cur_freq,
  .get_desired_freq = mpipe_get_desired_freq,
  .set_freq         = mpipe_set_freq,
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_mpipe = {
  .shim_type  = MPIPE_DEV_INFO__TYPE_VAL_MPIPE,
  .name       = "mpipe",
  .desc       = "mPIPE Driver",
  .ops        = &mpipe_ops,
  .stilereq   = 1,
};


///////////////////////////////////////////////////////////////////
//                        Global Methods                         //
///////////////////////////////////////////////////////////////////


/** Return the base PTE that the client should use to access our
    shim's MMIO registers. */
int
handle_gxio_mpipe_get_mmio_base(mpipe_state_t* ms, int svc_dom, HV_PTE *base)
{
  PA pa = (PA)svc_dom << MPIPE_MMIO_ADDR__SVC_DOM_SHIFT;
  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(ms->shim_pos.bits.x,
                                             ms->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;
  return 0;
}


static int contained_by(unsigned long bound_offset,
                        unsigned long bound_size,
                        unsigned long input_offset,
                        unsigned long input_size)
{
  if (input_offset < bound_offset ||
      input_offset + input_size > bound_offset + bound_size ||
      input_offset + input_size < input_offset)
    return 0;

  return 1;
}


/** Check whether an MMIO range is legal. */
int
handle_gxio_mpipe_check_mmio_offset(mpipe_state_t* ms, int svc_dom,
                                    unsigned long offset, unsigned long size)
{
  if (contained_by(HV_MPIPE_CONFIG_MMIO_OFFSET, HV_MPIPE_CONFIG_MMIO_SIZE,
                   offset, size) ||
      contained_by(HV_MPIPE_FAST_MMIO_OFFSET, HV_MPIPE_FAST_MMIO_SIZE,
                   offset, size))
    return 0;

  TRACE("check_mmio_offset() failed\n");
  return GXIO_ERR_MMIO_ADDRESS;
}



///////////////////////////////////////////////////////////////////
//                      Resource Allocation                      //
///////////////////////////////////////////////////////////////////




/** A generic function for allocating a range of resources out of a bitmask.
 *
 * @param count Number of resources being allocated.
 * @param res_per_bit Number of resources per bit in bitmask.
 * @param in_use_mask Currently allocated resource bits.
 * @param bitmask_bits Number of valid bits that could be in bitmask.
 * @param first_res First resource number, if explicitly requested by client.
 * @param flags If HV_MPIPE_ALLOC_FIXED is set, start at first_res.
 * @param new_bits_out Filled with the newly allocated bits.
 *
 * @return The first allocated resource number, or -1 on failure.
 */
static int
alloc_mpipe_resources(unsigned int count, unsigned int res_per_bit,
                      uint64_t in_use_mask, unsigned int bitmask_bits,
                      unsigned int first_res, unsigned int flags,
                      uint64_t* new_bits_out)
{
  uint64_t new_bits;

  if (flags & HV_MPIPE_ALLOC_FIXED)
  {
    // The client requested a particular range of resources; convert
    // that to mask bits and see if they're available.
    unsigned int first_bit = first_res / res_per_bit;
    unsigned int last_bit = (first_res + (count - 1)) / res_per_bit;
    new_bits = BIT_RANGE(first_bit, last_bit);

    if ((last_bit >= bitmask_bits) ||
        (in_use_mask & new_bits))
    {
      TRACE("Fixed resource allocation failed\n");
      return -1;
    }
  }
  else
  {
    // Any range of resources will do; see how many bits need to be
    // allocated and scan for that many contiguous bits.
    unsigned int num_bits = (count + res_per_bit - 1) / res_per_bit;
    if (num_bits > bitmask_bits)
      return -1;

    unsigned int first_bit = 0;
    new_bits = ((1ULL << num_bits) - 1) << first_bit;

    while (first_bit <= bitmask_bits - num_bits)
    {
      if ((in_use_mask & new_bits) == 0)
        break;

      first_bit++;
      new_bits <<= 1;
    }
    if (first_bit > bitmask_bits - num_bits)
    {
      TRACE("Resource allocation failed\n");
      return -1;
    }

    first_res = first_bit * res_per_bit;
  }

  // Apply the new bits to the bitmask and return the first allocated
  // resource number.
  TRACE("Resource allocation succeeded: first_res = %d, new_bits = %#llx\n",
        first_res, new_bits);
  *new_bits_out = new_bits;
  return first_res;
}


/** Update the "protections" for this svc_dom. */
static void
update_protections(mpipe_state_t* ms, int svc_dom)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];

  MPIPE_MMIO_INIT_CTL_t ctl = {{ .svc_dom_idx = svc_dom }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_CTL, ctl.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_DAT,
         svc_dom_resources->data0.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_MMIO_INIT_DAT,
         svc_dom_resources->data1.word);
}


/** Allow all allocated edma rings to use all allocated buffer stacks. */
static void
update_edma_protections(mpipe_state_t* ms, int svc_dom)
{
  // ISSUE: Make a "MPIPE_NUM_EDMA_RINGS" constant?
  for (int ering = 0; ering < HV_MPIPE_ALLOC_EDMA_RINGS_BITS; ering++)
  {
    if (!good_ering(ms, svc_dom, ering))
      continue;

    uint_reg_t stacks = 0;

    for (int stack = 0; stack < MPIPE_NUM_BUFFER_STACKS; stack++)
    {
      // ISSUE: Do we need to wait until the stacks are "initialized"?
      if (good_stack(ms, svc_dom, stack))
        stacks |= (1 << stack);
    }

    MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_STACK_PROT,
        .idx = ering,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);

    MPIPE_EDMA_RG_INIT_DAT_STACK_PROT_t data = {{
        .asid_ena = stacks,
        .stack_ena = stacks,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, data.word);
  }
}



/** Allocate a contiguous region of 'count' buffer stacks for the
 * indicated service domain.  Any contiguous region is acceptable,
 * unless a fixed mapping is indicated in the flags word.
 */
int
handle_gxio_mpipe_alloc_buffer_stacks_aux(mpipe_state_t* ms, int svc_dom,
                                          unsigned int count, unsigned int first,
                                          unsigned int flags)
{
  uint64_t new_bits = 0;
  int result;

  // We need to access global data or other mpipe's data, so need to
  // acquire global lock mpipe_alloc_lock. In order to do it without
  // deadlock, first drop own's lock: ms->lock. Then acquire
  // mpipe_alloc_lock. After that get the own's lock again.

  spin_unlock(&ms->lock);

  spin_lock(&mpipe_alloc_lock);

  spin_lock(&ms->lock);

  if (!is_open_svc_dom(svc_dom, ms))
  {
    spin_unlock(&mpipe_alloc_lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  if (flags & HV_MPIPE_ALLOC_MULTI_MPIPES_FIXED)
  {
    mpipe_resources_t* svc_dom_resources;

    new_bits = BIT_RANGE(first/HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT,
                         (first + (count - 1))/HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT);

    svc_dom_resources = &ms->svc_dom_resources[svc_dom];

    // Make sure new_bits is in the all_shim_buffer_stack_allocated_mask
    if ((all_shim_buffer_stack_allocated_mask & new_bits) != new_bits)
    {
      spin_unlock(&mpipe_alloc_lock);
      return  GXIO_MPIPE_ERR_NO_BUFFER_STACK;
    }

    spin_unlock(&mpipe_alloc_lock);

    svc_dom_resources->data1.buffer_stack_mask |= new_bits;
    ms->resources.data1.buffer_stack_mask |= new_bits;

    update_protections(ms, svc_dom);

    // ISSUE: Wait until buffer stack is initialized?
    update_edma_protections(ms, svc_dom);

    return first;
  }
  else if (flags & HV_MPIPE_ALLOC_MULTI_MPIPES)
  {
    mpipe_state_t* other_ms = mpipe_state[ms->instance ? 0 : 1];

    if (other_ms)
    {
      uint64_t temp;

      // Obtain other mpipe's lock.
      spin_lock(&other_ms->lock);

      temp = ms->resources.data1.buffer_stack_mask |
        other_ms->resources.data1.buffer_stack_mask |
        all_shim_buffer_stack_allocated_mask;

      result = alloc_mpipe_resources(count,
                                     HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT,
                                     temp,
                                     HV_MPIPE_ALLOC_BUFFER_STACKS_BITS,
                                     first, flags, &new_bits);
      if (result >= 0)
      {
        mpipe_resources_t* svc_dom_resources;

        svc_dom_resources = &ms->svc_dom_resources[svc_dom];
        svc_dom_resources->data1.buffer_stack_mask |= new_bits;
        ms->resources.data1.buffer_stack_mask |= new_bits;

        update_protections(ms, svc_dom);

        // ISSUE: Wait until buffer stack is initialized?
        update_edma_protections(ms, svc_dom);

        // For other mpipe, we have no domain info. reserve the stack on the
        // all_shim_buffer_stack_allocated_mask. And the subsequent HV calls
        // of other shims will allocate them.
        all_shim_buffer_stack_allocated_mask |= new_bits;
      }

      spin_unlock(&other_ms->lock);

      spin_unlock(&mpipe_alloc_lock);

      if (result < 0)
        return GXIO_MPIPE_ERR_NO_BUFFER_STACK;

      return result;
    }
  }

  result = alloc_mpipe_resources(count,
                                 HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT,
                                 ms->resources.data1.buffer_stack_mask |
                                 all_shim_buffer_stack_allocated_mask,
                                 HV_MPIPE_ALLOC_BUFFER_STACKS_BITS,
                                 first, flags, &new_bits);

  spin_unlock(&mpipe_alloc_lock);

  if (result < 0)
    return GXIO_MPIPE_ERR_NO_BUFFER_STACK;

  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  svc_dom_resources->data1.buffer_stack_mask |= new_bits;
  ms->resources.data1.buffer_stack_mask |= new_bits;

  update_protections(ms, svc_dom);

  // ISSUE: Wait until buffer stack is initialized?
  update_edma_protections(ms, svc_dom);

  return result;
}


int
handle_gxio_mpipe_init_buffer_stack_aux(mpipe_state_t* ms, int svc_dom,
                                        PA backing_pa, size_t backing_size,
                                        struct iorpc_mem_attr backing_attr,
                                        unsigned int stack,
                                        unsigned int buffer_size_enum)
{
  if (!good_stack(ms, svc_dom, stack))
    return GXIO_MPIPE_ERR_BAD_BUFFER_STACK;

  if (buffer_size_enum > MPIPE_BSM_INIT_DAT_1__SIZE_VAL_BSZ_16384)
    return GXIO_MPIPE_ERR_INVAL_BUFFER_SIZE;

  MPIPE_BSM_INIT_CTL_t ctl = {{
      .reg = 0,
      .stack_idx = stack,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl.word);

  MPIPE_BSM_INIT_DAT_0_t data0 = {{
      .lim = backing_size / CHIP_L2_LINE_SIZE(),
      .tos_idx = 0,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT, data0.word);

#if 0
  MPIPE_BSM_INIT_CTL_t ctl1 = {{
      .reg = 1,
      .stack_idx = stack,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl1.word);
#endif




  MPIPE_BSM_INIT_DAT_1_t data1 = {{

      .base = backing_pa >> 16,
      .hfh = backing_attr.hfh,
      .nt_hint = backing_attr.nt_hint,
      .pin = backing_attr.io_pin,
      .tile_id = DRV_COORDS_TO_TILE_ID(backing_attr.lotar_x,
                                       backing_attr.lotar_y),
      .size = buffer_size_enum,
      .enable = 1,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT, data1.word);

  ms->initialized_stacks |= (1L << stack);

  return 0;
}


int
handle_gxio_mpipe_register_page_aux(mpipe_state_t* ms, int svc_dom,
                                    PA page_pa, size_t page_size,
                                    struct iorpc_mem_attr page_attr,
                                    unsigned int stack,
                                    uint64_t vpn)
{
  // Verify page size.
  int log2_page_size = __builtin_ctzl(page_size);
  if (log2_page_size < 12 || log2_page_size > CHIP_PA_WIDTH())
    return GXIO_ERR_IOTLB_ENTRY;

  if (!good_stack(ms, svc_dom, stack))
    return GXIO_MPIPE_ERR_BAD_BUFFER_STACK;

  // Verify there is an IOTLB entry available.
  if (ms->iotlb_entries_used[stack] == MPIPE_NUM_TLBS_PER_ASID)
    return GXIO_ERR_IOTLB_ENTRY;

  // Get next available entry for this stack.
  int entry = ms->iotlb_entries_used[stack]++;

  // NOTE: We implicitly modify "is_attr" below.
  // ISSUE: We could instead use "MPIPE_TLB_TABLE__FIRST_WORD" below,
  // and explicitly modify "is_attr" between the two uses.

  MPIPE_TLB_TABLE_t table = {{
      .is_attr = 0,
      .entry = entry,
      .asid = stack,
    }};

  MPIPE_TLB_ENTRY_ADDR_t addr = {{
      .pfn = page_pa >> 12,
      .vpn = vpn,
    }};
  cfg_wr(ms->shim_pos.word, 0,
         MPIPE_TLB_ENTRY_ADDR__FIRST_WORD + table.word, addr.word);

  MPIPE_TLB_ENTRY_ATTR_t attr = {{
      .vld = 1,
      .ps = log2_page_size - 12,
      .home_mapping = !page_attr.hfh,
      .pin = page_attr.io_pin,
      .nt_hint = page_attr.nt_hint,
      .loc_y_or_offset = page_attr.lotar_y,
      .loc_x_or_mask = page_attr.lotar_x,
      //.lru = UNUSED
    }};
  cfg_wr(ms->shim_pos.word, 0,
         MPIPE_TLB_ENTRY_ATTR__FIRST_WORD + table.word, attr.word);

  flush_micro_tlbs(ms);

  return 0;
}



/** Allocate a contiguous region of 'count' NotifRings for the
 * indicated service domain.
 */
int
handle_gxio_mpipe_alloc_notif_rings(mpipe_state_t* ms, int svc_dom,
                                    unsigned int count, unsigned int first,
                                    unsigned int flags)
{
  uint64_t new_bits = 0;
  int result;

  result = alloc_mpipe_resources(count,
                                 HV_MPIPE_ALLOC_NOTIF_RINGS_RES_PER_BIT,
                                 ms->resources.data0.notif_ring_mask,
                                 HV_MPIPE_ALLOC_NOTIF_RINGS_BITS,
                                 first, flags, &new_bits);
  if (result < 0)
    return GXIO_MPIPE_ERR_NO_NOTIF_RING;

  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  svc_dom_resources->data0.notif_ring_mask |= new_bits;
  ms->resources.data0.notif_ring_mask |= new_bits;

  update_protections(ms, svc_dom);

  return result;
}


int
handle_gxio_mpipe_init_notif_ring_aux(mpipe_state_t* ms, int svc_dom,
                                      PA mem_pa, size_t mem_size,
                                      struct iorpc_mem_attr mem_attr,
                                      unsigned int ring)
{
  if (!good_ring(ms, svc_dom, ring))
    return GXIO_MPIPE_ERR_BAD_NOTIF_RING;

  // ISSUE: Allow passing in "excessive" memory?
  int per = sizeof(MPIPE_PDESC_t);
  // Aka "gxio_mpipe_notif_ring_entries_t".
  int entries_enum;
  if (mem_size == 128 * per)
    entries_enum = MPIPE_LBL_INIT_DAT_NR_TBL_0__SIZE_VAL_SIZE128;
  else if (mem_size == 512 * per)
    entries_enum = MPIPE_LBL_INIT_DAT_NR_TBL_0__SIZE_VAL_SIZE512;
  else if (mem_size == 2048 * per)
    entries_enum = MPIPE_LBL_INIT_DAT_NR_TBL_0__SIZE_VAL_SIZE2K;
  else if (mem_size == 65536 * per)
    entries_enum = MPIPE_LBL_INIT_DAT_NR_TBL_0__SIZE_VAL_SIZE64K;
  else
    return GXIO_ERR_INVAL_MEMORY_SIZE;

  freeze_load_balancer(ms, 1);

  MPIPE_LBL_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_NR_TBL,
      .idx = ring * 2,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);




  MPIPE_LBL_INIT_DAT_NR_TBL_0_t data0 = {{

      .tail = 1,
      .base_pa = mem_pa >> 12,
      .hfh = mem_attr.hfh,
      .nt_hint = mem_attr.nt_hint,
      .pin = mem_attr.io_pin,
      .tileid = DRV_COORDS_TO_TILE_ID(mem_attr.lotar_x, mem_attr.lotar_y),
      .size = entries_enum,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data0.word);

  MPIPE_LBL_INIT_DAT_NR_TBL_1_t data1 = {{
      .count = 0,
      .size = entries_enum,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data1.word);

  freeze_load_balancer(ms, 0);

  return 0;
}


int
handle_gxio_mpipe_request_notif_ring_interrupt(mpipe_state_t* ms,
                                               int svc_dom,
                                               int inter_x, int inter_y,
                                               int inter_ipi,
                                               int inter_event,
                                               uint ring)
{
  MPIPE_INT_BIND_t data = {{
      .enable = 1,
      .mode = 0,
      .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
      .int_num = inter_ipi,
      .evt_num = inter_event,
      .vec_sel = 3 + ring / 64,
      .bind_sel = ring % 64,
    }};

  cfg_wr(ms->shim_pos.word, 0, MPIPE_INT_BIND, data.word);

  return 0;
}


int
handle_gxio_mpipe_enable_notif_ring_interrupt(mpipe_state_t* ms,
                                              int svc_dom,
                                              uint ring)
{
  unsigned long val = 1ull << (ring % 64);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_INT_VEC3_W1TC + (ring / 64) * 8, val);
  return 0;
}


/** Allocate a contiguous region of 'count' NotifGroups for the
 * indicated service domain.
 */
int
handle_gxio_mpipe_alloc_notif_groups(mpipe_state_t* ms, int svc_dom,
                                     unsigned int count, unsigned int first,
                                     unsigned int flags)
{
  uint64_t new_bits = 0;
  int result;

  result = alloc_mpipe_resources(count,
                                 HV_MPIPE_ALLOC_NOTIF_GROUPS_RES_PER_BIT,
                                 ms->resources.notif_group_mask,
                                 HV_MPIPE_ALLOC_NOTIF_GROUPS_BITS,
                                 first, flags, &new_bits);
  if (result < 0)
    return GXIO_MPIPE_ERR_NO_NOTIF_GROUP;

  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  svc_dom_resources->notif_group_mask |= new_bits;
  ms->resources.notif_group_mask |= new_bits;

  return result;
}


int
handle_gxio_mpipe_init_notif_group(mpipe_state_t* ms, int svc_dom,
                                   unsigned int group,
                                   gxio_mpipe_notif_group_bits_t bits)
{
  if (!good_group(ms, svc_dom, group))
    return GXIO_MPIPE_ERR_BAD_NOTIF_GROUP;

  // Verify each "ring" is legal and has been allocated.
  // NOTE: This does NOT verify that each "ring" has actually been
  // initialized, but we "wipe" them at startup and after cleanup.
  for (unsigned int ring = 0; ring < MPIPE_NUM_NOTIF_RINGS; ring++)
  {
    // NOTE: There are 64 bits in each element of "bits.ring_mask".
    if ((bits.ring_mask[ring / 64] & (1ull << (ring % 64))) != 0 &&
        !good_ring(ms, svc_dom, ring))
      return GXIO_MPIPE_ERR_BAD_NOTIF_RING;
  }

  freeze_load_balancer(ms, 1);

  MPIPE_LBL_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_GROUP_TBL,
      .idx = group * 4,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);

  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, bits.ring_mask[0]);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, bits.ring_mask[1]);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, bits.ring_mask[2]);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, bits.ring_mask[3]);

  freeze_load_balancer(ms, 0);

  return 0;
}



/** Allocate a contiguous region of 'count' buckets for the
 * indicated service domain.
 */
int
handle_gxio_mpipe_alloc_buckets(mpipe_state_t* ms, int svc_dom,
                                unsigned int count, unsigned int first,
                                unsigned int flags)
{
  uint64_t new_bits = 0;
  int result;

  // ISSUE: Split the high level API into "lo_buckets" and "hi_buckets"?
  // HACK: We use lo buckets if requested, or for "large" allocations.
  if ((flags & HV_MPIPE_ALLOC_FIXED) ?
      (first < HV_MPIPE_NUM_LO_BUCKETS) :
      (count >= 16))
  {
    result = alloc_mpipe_resources(count,
                                   HV_MPIPE_ALLOC_LO_BUCKETS_RES_PER_BIT,
                                   ms->resources.data0.bucket_release_mask_lo,
                                   HV_MPIPE_ALLOC_LO_BUCKETS_BITS,
                                   first, flags, &new_bits);
    if (result < 0)
      return GXIO_MPIPE_ERR_NO_BUCKET;

    mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
    svc_dom_resources->data0.bucket_release_mask_lo |= new_bits;
    ms->resources.data0.bucket_release_mask_lo |= new_bits;
  }
  else
  {
    result = alloc_mpipe_resources(count,
                                   HV_MPIPE_ALLOC_HI_BUCKETS_RES_PER_BIT,
                                   ms->resources.data0.bucket_release_mask_hi,
                                   HV_MPIPE_ALLOC_HI_BUCKETS_BITS,
                                   first - HV_MPIPE_NUM_LO_BUCKETS,
                                   flags, &new_bits);
    if (result < 0)
      return GXIO_MPIPE_ERR_NO_BUCKET;

    result += HV_MPIPE_NUM_LO_BUCKETS;

    mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
    svc_dom_resources->data0.bucket_release_mask_hi |= new_bits;
    ms->resources.data0.bucket_release_mask_hi |= new_bits;
  }

  update_protections(ms, svc_dom);

  return result;
}


int
handle_gxio_mpipe_init_bucket(mpipe_state_t* ms, int svc_dom,
                              unsigned int bucket,
                              gxio_mpipe_bucket_info_t info)
{
  if (!good_bucket(ms, svc_dom, bucket))
    return GXIO_MPIPE_ERR_BAD_BUCKET;

  // Verify "ring" is legal and has been allocated.
  // NOTE: This does NOT verify that each "ring" has actually been
  // initialized, but we "wipe" them at startup and after cleanup.
  if (!good_ring(ms, svc_dom, info.notifring))
    return GXIO_MPIPE_ERR_BAD_NOTIF_RING;

  // Verify "group" is legal and has been allocated.
  if (!good_group(ms, svc_dom, info.group))
    return GXIO_MPIPE_ERR_BAD_NOTIF_GROUP;

  freeze_load_balancer(ms, 1);

  MPIPE_LBL_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_BSTS_TBL,
      .idx = bucket,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);

  cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, info.word);

  freeze_load_balancer(ms, 0);

  return 0;
}


/** Allocate a contiguous region of 'count' eDMA rings for the
 * indicated service domain.
 */
int
handle_gxio_mpipe_alloc_edma_rings(mpipe_state_t* ms, int svc_dom,
                                   unsigned int count, unsigned int first,
                                   unsigned int flags)
{
  uint64_t new_bits = 0;
  int result;

  result = alloc_mpipe_resources(count,
                                 HV_MPIPE_ALLOC_EDMA_RINGS_RES_PER_BIT,
                                 ms->resources.data1.edma_post_mask,
                                 HV_MPIPE_ALLOC_EDMA_RINGS_BITS,
                                 first, flags, &new_bits);
  if (result < 0)
    return GXIO_MPIPE_ERR_NO_EDMA_RING;

  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  svc_dom_resources->data1.edma_post_mask |= new_bits;
  ms->resources.data1.edma_post_mask |= new_bits;

  update_protections(ms, svc_dom);

  update_edma_protections(ms, svc_dom);

  return result;
}


int
handle_gxio_mpipe_init_edma_ring_aux(mpipe_state_t* ms, int svc_dom,
                                     PA mem_pa, size_t mem_size,
                                     struct iorpc_mem_attr mem_attr,
                                     unsigned int ering, unsigned int channel)
{
  if (!(ms->svc_dom_resources[svc_dom].channels & (1L << channel)))
    return GXIO_MPIPE_ERR_BAD_CHANNEL;

  if (!good_ering(ms, svc_dom, ering))
    return GXIO_MPIPE_ERR_BAD_EDMA_RING;

  // ISSUE: Allow passing in "excessive" memory?
  int per = sizeof(MPIPE_EDMA_DESC_t);
  // Aka "gxio_mpipe_edma_ring_entries_t".
  int entries_enum;
  if (mem_size == 512 * per)
    entries_enum = 0;
  else if (mem_size == 2048 * per)
    entries_enum = 1;
  else if (mem_size == 8192 * per)
    entries_enum = 2;
  else if (mem_size == 65536 * per)
    entries_enum = 3;
  else
    return GXIO_ERR_INVAL_MEMORY_SIZE;

  {
    MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_MAP,
        .idx = ering,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);

    MPIPE_EDMA_RG_INIT_DAT_MAP_t data = {{
        .channel = channel,
        // We use pause on queue 15 internally to stop traffic for MACs being
        // reconfigured.
        .priority_queues = 1 << 15,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, data.word);
  }
#if 0
  // FIXME: See "gxio_mpipe_equeue_put_at()".
  {
    MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_HEAD,
        .idx = ering,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);

    MPIPE_EDMA_DM_INIT_DAT_HEAD_t data = {{
        .head = 0,
        .gnum = 1,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, data.word);
  }
#endif
  {
    MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP,
        .idx = ering,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);




    MPIPE_EDMA_DM_INIT_DAT_SETUP_t data = {{

        .base_pa = mem_pa >> 10,
        .hfh = mem_attr.hfh,
        .tileid = DRV_COORDS_TO_TILE_ID(mem_attr.lotar_x, mem_attr.lotar_y),
        .ring_size = entries_enum,
        .freeze = 0,
        .hunt = 1,
        .flush = 0,
        .stall = 0,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, data.word);
  }

  return 0;
}


int
handle_gxio_mpipe_config_edma_ring_blks(mpipe_state_t* ms, int svc_dom,
                                        unsigned int ering,
                                        unsigned int max_blks,
                                        unsigned int min_snf_blks,
                                        unsigned int db)
{
  if (!good_ering(ms, svc_dom, ering))
    return GXIO_MPIPE_ERR_BAD_EDMA_RING;

  if (max_blks <= min_snf_blks)
    return GXIO_ERR_INVAL;

  // Prevent issues in "mpipe_close_cleanup_ering()".
  if (max_blks < 13)
    max_blks = 13;

  MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
      .idx = ering,
      .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_THRESH,
    }};
  MPIPE_EDMA_RG_INIT_DAT_THRESH_t dat;

  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
  dat.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT);

  if (!sim_is_simulator())
  {
    MPIPE_EDMA_CTL_t dyn;
    dyn.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);

    // How many "new" blocks will be consumed (may be negative).
    int consumed = max_blks - dat.max_blks;

    // Do not overcommit.
    if (dyn.ud_blocks < consumed)
      return GXIO_ERR_INVAL;

    dyn.ud_blocks -= consumed;
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_CTL, dyn.word);
  }

  dat.max_blks = max_blks;
  dat.min_snf_blks = min_snf_blks;
  dat.db = db;

  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, dat.word);

  return 0;
}


int
handle_gxio_mpipe_register_client_memory(mpipe_state_t* ms, int svc_dom,
                                         unsigned int iotlb,
                                         HV_PTE pte, unsigned int flags)
{
  unsigned int stack = iotlb;

  if (!good_stack(ms, svc_dom, stack))
    return GXIO_MPIPE_ERR_BAD_BUFFER_STACK;

  // Verify all IOTLB entries are available.
  if (ms->iotlb_entries_used[stack] != 0)
    return GXIO_ERR_IOTLB_ENTRY;

  int err = drv_map_cpa_space_to_iotlb(ms->shim_pos, iotlb, pte,
                                       MPIPE_TLB_ENTRY_ADDR__FIRST_WORD,
                                       flags);
  if (err != 0)
    return err;

  ms->iotlb_entries_used[stack] = MPIPE_NUM_TLBS_PER_ASID;

  flush_micro_tlbs(ms);

  return 0;
}


int
handle_gxio_mpipe_get_sqn(mpipe_state_t* ms, int svc_dom, int index)
{
  if (index < 0 || index >= MPIPE_NUM_BUCKETS)
    return GXIO_ERR_INVAL;

  MPIPE_SQN_CTR_CTL_t mscc = {{
      // Do not clear on read.
      .ctr_mode = 1,
      .struct_sel = MPIPE_SQN_CTR_CTL__STRUCT_SEL_VAL_SQN,
      .idx = index,
    }};

  cfg_wr(ms->shim_pos.word, 0, MPIPE_SQN_CTR_CTL, mscc.word);

  return cfg_rd(ms->shim_pos.word, 0, MPIPE_SQN_CTR_DAT);
}


/** Update our shadow copy of the mPIPE global counters from the hardware. */
static void
update_stats(mpipe_state_t* ms)
{
  //
  // What we want to do is to give the user the illusion that we have
  // 64-bit counters for everything.  To do this, we keep a shadow copy
  // of the counters in memory, and every time someone asks for them,
  // we read (and clear) the hardware values and add to our shadow copy,
  // then return the shadow copy.
  //
  // FIXME: In order for this to work over long periods of time, during
  // which the counters are not being read, and during which which the
  // counters may reach their limits, we need to do this accumulation
  // operation periodically (once a day or so is fine give the counter
  // sizes and maximum increment rates).  We are not yet doing this, but
  // need to.
  //
  uint64_t count;

  ms->stats.ingress_packets += cfg_rd(ms->shim_pos.word, 0,
                                      MPIPE_INGRESS_PKT_COUNT_RC);

  ms->stats.egress_packets += cfg_rd(ms->shim_pos.word, 0,
                                     MPIPE_EGRESS_PKT_COUNT_RC);

  ms->stats.ingress_bytes += cfg_rd(ms->shim_pos.word, 0,
                                    MPIPE_INGRESS_BYTE_COUNT_RC);

  ms->stats.egress_bytes += cfg_rd(ms->shim_pos.word, 0,
                                   MPIPE_EGRESS_BYTE_COUNT_RC);

  //
  // We use the IDMA/IPKT/LB configurable stats counters to get more
  // detailed drop info.
  //

  count = cfg_rd(ms->shim_pos.word, 0, MPIPE_IDMA_STAT_CTR);

  if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_IDMA])
    ms->config_stats.idma_count += count;
  else
    ms->stats.ingress_drops_no_buf += count;

  count = cfg_rd(ms->shim_pos.word, 0, MPIPE_IPKT_STAT_CTR);

  if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_IPKT])
    ms->config_stats.ipkt_count += count;
  else
    ms->stats.ingress_drops_ipkt += count;

  count = cfg_rd(ms->shim_pos.word, 0, MPIPE_LBL_STAT_CTR);

  if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_LBL])
    ms->config_stats.ingress_lb_count += count;
  else
    ms->stats.ingress_drops_cls_lb += count;

  // Bug 11017, mpipe MPIPE_INGRESS_DROP_COUNT_RC counts LBL packet drop twice.
  // calculate the overall drop packets: ms->stats.ingress_drops  from 3 drop
  // counters. If a counter is reconfigured to reasons other than default, its
  // count will not contribute to overall counter during configured period.

  ms->stats.ingress_drops =
    ms->stats.ingress_drops_no_buf +
    ms->stats.ingress_drops_ipkt +
    ms->stats.ingress_drops_cls_lb;
}


int
handle_gxio_mpipe_config_stats(mpipe_state_t* ms, int svc_dom,
                               uint32_t command, uint64_t val)
{


  static const struct
  {
    uint16_t  c;
    uint16_t  v;
    uint16_t  reg;
    uint16_t  sel;
  }
  command_to_mmio[] =
    {
      {
        /* lbl */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_DROP
      },
      {
        /* lbl_drop */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL_ONLY,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_DROP_LBL_ONLY
      },
      {
        /* lbl_bkt */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL_BKT,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_DROP_BKT
      },
      {
        /* lbl_nr */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL_NR,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_DROP_NR
      },
      {
        /* lbl_pkts */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL_PKTS,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_PKTS
      },
      {
        /* lbl_pick */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_LBL_PICK,
        .reg = MPIPE_LBL_CTL,
        .sel = MPIPE_LBL_CTL__CTR_SEL_VAL_STICKY_PICK
      },

      {
        /* ipkt */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IPKT,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IPKT_EVT_CTR_SEL_VAL_DROP_COMB
      },
      {
        /* ipkt_trunc */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_TRUNCATED,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IPKT_EVT_CTR_SEL_VAL_TRUNC
      },
      {
        /* ipkt_drop */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_DROP,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IPKT_EVT_CTR_SEL_VAL_DROP
      },
      {
        /* ipkt_pkts */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_PKTS,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IPKT_EVT_CTR_SEL_VAL_PKTS
      },
      {
        /* idma */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BE
      },
      {
          /* idma_bsm */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_STALL,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BSM_STALL

      },
      {
        /* idma_tlb */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_TLB_STALL,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_TLB_STALL
      },
      {
        /* idma_pkts */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_PKTS,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_PKTS
      },
      {
        /* idma_bufs */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BUFS,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BUFS
      },
      {
        /* idma_retries */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_RETRIES,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_RETRIES
      },
      {
        /* idma_sdn_pkts */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_SDN_PKTS,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_SDN_PKTS
      },
      {
        /* idma_sdn_af */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_SDN_AF,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_SDN_AF
      },
      {
        /* idma_trk */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_TRK_AF,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_TRK_AF
      },
      {
        /* idma_ntf */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_NTF_AF,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_NTF_AF
      },
      {
        /* idma_bsm_spill */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_SPILL,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BSM_SPILL
      },
      {
        /* idma_bsm_fill */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_FILL,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BSM_FILL
      },
      {
        /* idma_bsm_edma */
        .c = GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        .v = GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_EDMA,
        .reg = MPIPE_IDMA_CTL,
        .sel = MPIPE_IDMA_CTL__IDMA_EVT_CTR_SEL_VAL_BSM_EDMA
      }
    };

  if (ms->stat_config_on && ms->stat_config_svc_dom != svc_dom)
  {
    // Not allow re-config if one is in the process.
    return GXIO_ERR_PERM;
  }

  // Save the counters before reprogramming the counters.
  update_stats(ms);

  for (int i = 0;
       i < sizeof(command_to_mmio) / sizeof(command_to_mmio[0]);
       i++)
  {
    if ((command == command_to_mmio[i].c) &&
        (val == command_to_mmio[i].v))
    {
      // Find the right config. entry in array command_to_mmio.
      if (command_to_mmio[i].reg == MPIPE_LBL_CTL)
      {
        MPIPE_LBL_CTL_t data;

        data.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_LBL_CTL);

        if (data.ctr_sel != command_to_mmio[i].sel)
        {
          // Reconfig the counter.
          data.ctr_sel = command_to_mmio[i].sel;
          cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_CTL, data.word);
        }
        // Reset the save counter.
        ms->config_stats.ingress_lb_count = 0;
        // Clear the counter by a read.
        cfg_rd(ms->shim_pos.word, 0, MPIPE_LBL_STAT_CTR);
      }
      else if (command_to_mmio[i].reg == MPIPE_IDMA_CTL)
      {
        MPIPE_IDMA_CTL_t data;

        data.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_IDMA_CTL);

        if (command == GXIO_MPIPE_STAT_CONFIG_COMM_IPKT)
        {
          if (data.ipkt_evt_ctr_sel != command_to_mmio[i].sel)
          {
            // Reconfig. the counter.
            data.ipkt_evt_ctr_sel = command_to_mmio[i].sel;
            cfg_wr(ms->shim_pos.word, 0, MPIPE_IDMA_CTL, data.word);
          }
          // Reset the save counter.
          ms->config_stats.ipkt_count = 0;
          // Clear the counter by a read.
          cfg_rd(ms->shim_pos.word, 0, MPIPE_IPKT_STAT_CTR);
        }
        else if (command == GXIO_MPIPE_STAT_CONFIG_COMM_IDMA)
        {
          if (data.idma_evt_ctr_sel != command_to_mmio[i].sel)
          {
            // Reconfig. the counter.
            data.idma_evt_ctr_sel = command_to_mmio[i].sel;
            cfg_wr(ms->shim_pos.word, 0, MPIPE_IDMA_CTL, data.word);
          }
          // Reset the save counter.
          ms->config_stats.idma_count = 0;
          // Clear the counter by a read.
          cfg_rd(ms->shim_pos.word, 0, MPIPE_IDMA_STAT_CTR);
        }
        else
        {
          return GXIO_ERR_INVAL;
        }
      }
      else
      {
        return GXIO_ERR_INVAL;
      }

      // Update the new config.
      ms->stat_config[command] = val;

      // Update the ms->stat_config_svc_dom.
      ms->stat_config_svc_dom = svc_dom;

      // Update the stat_config_on.
      ms->stat_config_on = false;
      for (int k = 0; k < GXIO_MPIPE_STAT_CONFIG_COMM_MAX; k++)
      {
        if (ms->stat_config[k])
          ms->stat_config_on = true;
      }

      return 0;
    }
  }
  return GXIO_ERR_INVAL;
}

int
handle_gxio_mpipe_get_stats(mpipe_state_t* ms, int svc_dom,
                            gxio_mpipe_stats_t* result)
{
  update_stats(ms);

  memcpy(result, &ms->stats, sizeof (*result));

  if (ms->stat_config_on && svc_dom == ms->stat_config_svc_dom)
  {
    if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_LBL])
    {
      result->ingress_drops_cls_lb = ms->config_stats.ingress_lb_count;
    }

    if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_IPKT])
    {
      result->ingress_drops_ipkt = ms->config_stats.ipkt_count;
    }

    if (ms->stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_IDMA])
    {
      result->ingress_drops_no_buf = ms->config_stats.idma_count;
    }
  }
  return 0;
}


int
handle_gxio_mpipe_get_counter(mpipe_state_t* ms, int svc_dom,
                              unsigned int off, uint64_t* result)
{
  //
  // FIXME: see comment in update_stats() regarding counter rollover.  We
  // need to periodically (say, once a day) read & update all of our shadow
  // counters.
  //

  if (off >= sizeof (ms->counters) / sizeof (ms->counters[0]))
    return GXIO_ERR_INVAL;

  MPIPE_SQN_CTR_CTL_t mscc = {{
      // Clear on read.
      .ctr_mode = 0,
      .struct_sel = MPIPE_SQN_CTR_CTL__STRUCT_SEL_VAL_CTR,
      .idx = off,
    }};

  cfg_wr(ms->shim_pos.word, 0, MPIPE_SQN_CTR_CTL, mscc.word);

  ms->counters[off] += cfg_rd(ms->shim_pos.word, 0, MPIPE_SQN_CTR_DAT);
  *result = ms->counters[off];

  return 0;
}


int
handle_gxio_mpipe_set_timestamp_aux(mpipe_state_t* ms, int svc_dom,
                                    uint64_t sec, uint64_t nsec,
                                    uint64_t cycles)
{
  if (sim_is_simulator())
    return GXIO_ERR_UNSUPPORTED_OP;

  if (ms->tstamp_is_cycle)
  {
    WARN("Can't modify timestamp when timestamp-is-cycle option is enabled\n");
    return GXIO_ERR_BUSY;
  }

  uint64_t cur_cycles = get_cycle_count();

  // Calculate how many nanoseconds the hypervisor call takes.
  uint64_t delta_ns = (cur_cycles - cycles) * ns_per_sec / cpu_speed;

  nsec += delta_ns;
  if (nsec >= ns_per_sec)
  {
    nsec -= ns_per_sec;
    sec += 1;
  }
  MPIPE_TIMESTAMP_VAL_t val = {{
      .sec = sec,
      .ns = nsec,
    }};

  cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_VAL, val.word);
  return 0;
}


int
handle_gxio_mpipe_get_timestamp_aux(mpipe_state_t* ms, int svc_dom,
                                    uint64_t *sec, uint64_t *nsec,
                                    uint64_t *cycles)
{
  if (sim_is_simulator())
    return GXIO_ERR_UNSUPPORTED_OP;

  if (ms->tstamp_is_cycle)
  {
    WARN("Can't modify timestamp when timestamp-is-cycle option is enabled\n");
    return GXIO_ERR_BUSY;
  }

  MPIPE_TIMESTAMP_VAL_t val;

  val.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_VAL);
  *cycles = get_cycle_count();
  *sec = val.sec;
  *nsec = val.ns;

  return 0;
}


int
handle_gxio_mpipe_adjust_timestamp_aux(mpipe_state_t* ms, int svc_dom,
                                       int64_t nsec)
{
  if (ms->tstamp_is_cycle)
  {
    WARN("Can't modify timestamp when timestamp-is-cycle option is enabled\n");
    return GXIO_ERR_BUSY;
  }

  /* Bug 12168 workaround. */
  while (nsec > 73741824)
  {
    cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_NS_ADJ, 73741824);
    nsec -= 73741824;
  }

  cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_NS_ADJ, nsec);
  return 0;
}

// We use MPIPE_TIMESTAMP_RES_ADJ to adjust mpipe frequency.  By setting
// the field "cnt" which indicates the number of cycles to apply the tmp_thr
// threshold, this function achieves 2 ppb adjustment granularity.

int handle_gxio_mpipe_adjust_timestamp_freq(mpipe_state_t* ms, int svc_dom,
                                            int32_t ppb)
{
  int neg_adj = 0;

  if (ppb < 0)
  {
    neg_adj = 1;
    ppb = -ppb;
  }

  MPIPE_TIMESTAMP_CAL_t cal;
  cal.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_CAL);

  // Convert ppb to tmp_thr.
  uint32_t cnt = (1 << MPIPE_TIMESTAMP_RES_ADJ__CNT_WIDTH) - 1;
  uint64_t adj = cal.thr;
  adj *= ppb;
  uint32_t diff = adj / cnt;

  // Allow at most 2 times faster/slower adjustment.
  int32_t tmp_thr = neg_adj ? cal.thr + diff : cal.thr - diff;
  if (tmp_thr < cal.thr >> 1)
    tmp_thr = cal.thr >> 1;
  else if (tmp_thr > (cal.thr << 1) - 1)
    tmp_thr = (cal.thr << 1) - 1;

  MPIPE_TIMESTAMP_RES_ADJ_t res_adj;
  res_adj.tmp_thr = tmp_thr;
  res_adj.cnt = cnt;

  cfg_wr(ms->shim_pos.word, 0, MPIPE_TIMESTAMP_RES_ADJ, res_adj.word);
  return 0;
}

//
// Cleanup an EDMA ring.
//
static void
mpipe_close_cleanup_ering(mpipe_state_t* ms, int ering)
{
  MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP,
      .idx = ering,
    }};




  MPIPE_EDMA_DM_INIT_DAT_SETUP_t data;

  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
  data.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT);

  data.freeze = 1;
  data.stall = 1;
  data.hunt = 0; // ISSUE: Not required.

  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, data.word);
  __insn_mf();

  {
    MPIPE_EDMA_CTL_t fence;
    fence.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
    fence.fence = 1;
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_CTL, fence.word);
    __insn_mf();
    while (fence.fence)
      fence.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
  }

  data.flush = 1;

  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, data.word);
  __insn_mf();

  {
    MPIPE_EDMA_CTL_t fence;
    fence.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
    fence.fence = 1;
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_CTL, fence.word);
    __insn_mf();
    while (fence.fence)
      fence.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
    while (fence.flush_pnd)
      fence.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
  }

  {
    MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_STACK_PROT,
        .idx = ering,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, 0);
  }

  {
    MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_HEAD,
        .idx = ering,
      }};

    MPIPE_EDMA_DM_INIT_DAT_HEAD_t data = {{
        .head = 0,
        .gnum = 0,
      }};

    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, data.word);
  }

  {
    MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
        .idx = ering,
        .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE0,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, 0x1);
  }
  {
    MPIPE_EDMA_DM_INIT_CTL_t ctl = {{
        .idx = ering,
        .struct_sel = MPIPE_EDMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE1,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_DM_INIT_DAT, 0x0);
  }

  // Reset the thresh info.
  {
    MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
        .idx = ering,
        .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_THRESH,
      }};
    MPIPE_EDMA_RG_INIT_DAT_THRESH_t dat;

    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
    dat.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT);

    unsigned int old_max_blks = dat.max_blks;

    // Reset standard defaults.
    dat.max_blks = 13;
    dat.min_snf_blks = 12;
    dat.db = 1;

    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, dat.word);

    if (!sim_is_simulator())
    {
      MPIPE_EDMA_CTL_t dyn;
      dyn.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_CTL);
      dyn.ud_blocks += (old_max_blks - dat.max_blks);
      cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_CTL, dyn.word);
    }
  }
}


static void
mpipe_close_cleanup_stack(mpipe_state_t* ms, int stack)
{
  // Clear "lim" and "tos_idx".
  {
    MPIPE_BSM_INIT_CTL_t ctl = {{
        .reg = 0,
        .stack_idx = stack,
      }};

    cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT, 0);
  }

  // Clear "enable".
  {
    MPIPE_BSM_INIT_CTL_t ctl = {{
        .reg = 1,
        .stack_idx = stack,
      }};




    MPIPE_BSM_INIT_DAT_1_t data;

    cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl.word);
    data.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT);

    data.enable = 0;

    cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT, data.word);

    __insn_mf();
  }

  if (ms->initialized_stacks & (1L << stack))
  {
    // Explicitly pop buffers which may be stuck in hardware.
    // There should never be more than 64 such buffers.
    while (1)
    {
      MPIPE_BSM_REGION_ADDR_t offset = {{
          .stack = stack,
          .region = MPIPE_MMIO_ADDR__REGION_VAL_BSM,
          .svc_dom = RESERVED_SVC_DOM,
        }};
      MPIPE_BSM_REGION_VAL_t val = {{ 0 }};
      val.word = cfg_rd(ms->shim_pos.word, 0, offset.word);
      if (val.c == MPIPE_EDMA_DESC_WORD1__C_VAL_INVALID)
        break;
      if (val.c == MPIPE_EDMA_DESC_WORD1__C_VAL_NOT_RDY)
        continue;
    }

    ms->initialized_stacks &= ~(1L << stack);
  }

  // NOTE: Writing to "ADDR" clears the "vld" bit from "ATTR",
  // and writing zeros to both ADDR and ATTR is even better.
  // ISSUE: Could use "ms->iotlb_entries_used[stack]" as upper bound.
  for (int entry = 0; entry < MPIPE_NUM_TLBS_PER_ASID; entry++)
  {
    MPIPE_TLB_TABLE_t table = {{
        .entry = entry,
        .asid = stack,
      }};
    cfg_wr(ms->shim_pos.word, 0,
           MPIPE_TLB_ENTRY_ADDR__FIRST_WORD + table.word, 0);
    cfg_wr(ms->shim_pos.word, 0,
           MPIPE_TLB_ENTRY_ATTR__FIRST_WORD + table.word, 0);
  }

  flush_micro_tlbs(ms);

  ms->iotlb_entries_used[stack] = 0;
}


// Clean up various resources.
//
static void
mpipe_close_cleanup(mpipe_state_t* ms, int svc_dom)
{
  // FIXME: Must clear all interrupts!!!

  // Stop sending packets to the app.
  if (mpipe_close_aux(ms, svc_dom) != 0)
    WARN("Unexpected mpipe_close_aux() failure at close\n");

  // Wait until the classifier is done with the app.
  {
    MPIPE_CLS_CTL_t ctl;
    ctl.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_CLS_CTL);
    ctl.fence = 1;
    cfg_wr(ms->shim_pos.word, 0, MPIPE_CLS_CTL, ctl.word);
    __insn_mf();
    while (ctl.fence)
      ctl.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_CLS_CTL);
  }

  // Drain the load balancer.
  for (int ring = 0; ring < MPIPE_NUM_NOTIF_RINGS; ring++)
  {
    if (!good_ring(ms, svc_dom, ring))
      continue;

    MPIPE_LBL_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_INFL_CNT,
        .idx = ring,
      }};

    MPIPE_LBL_INIT_DAT_INFL_CNT_t data;

    while (1)
    {
      cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);
      data.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT);
      if (data.count == 0)
        break;
    }
  }

  freeze_load_balancer(ms, 1);

  // Clean up the rings.
  for (int ring = 0; ring < MPIPE_NUM_NOTIF_RINGS; ring++)
  {
    if (!good_ring(ms, svc_dom, ring))
      continue;

    MPIPE_LBL_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_NR_TBL,
        .idx = ring * 2,
      }};




    MPIPE_LBL_INIT_DAT_NR_TBL_0_t data0 = {{ 0 }};


    MPIPE_LBL_INIT_DAT_NR_TBL_1_t data1 = {{ .count = 0xfffe }};

    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data0.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, data1.word);
  }

  // Clean up the groups.
  for (int group = 0; group < MPIPE_NUM_NOTIF_GROUPS; group++)
  {
    if (!good_group(ms, svc_dom, group))
      continue;

    MPIPE_LBL_INIT_CTL_t ctl = {{
        .struct_sel = MPIPE_LBL_INIT_CTL__STRUCT_SEL_VAL_GROUP_TBL,
        .idx = group * 4,
      }};
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_CTL, ctl.word);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
    cfg_wr(ms->shim_pos.word, 0, MPIPE_LBL_INIT_DAT, 0);
  }

  freeze_load_balancer(ms, 0);

  // Clean up edma rings.
  for (int ering = 0; ering < HV_MPIPE_NUM_EDMA_RINGS; ering++)
  {
    if (good_ering(ms, svc_dom, ering))
      mpipe_close_cleanup_ering(ms, ering);
  }

  // Clean up the stacks.
  for (int stack = 0; stack < HV_MPIPE_NUM_BUFFER_STACKS; stack++)
  {
    if (good_stack(ms, svc_dom, stack))
      mpipe_close_cleanup_stack(ms, stack);
  }

  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];

  // Release the Notif Rings.
  ms->resources.data0.notif_ring_mask &=
    ~svc_dom_resources->data0.notif_ring_mask;
  svc_dom_resources->data0.notif_ring_mask = 0;

  // Release the Notif Groups.
  ms->resources.notif_group_mask &=
    ~svc_dom_resources->notif_group_mask;
  svc_dom_resources->notif_group_mask = 0;

  // Release the buckets.
  ms->resources.data0.bucket_release_mask_lo &=
    ~svc_dom_resources->data0.bucket_release_mask_lo;
  svc_dom_resources->data0.bucket_release_mask_lo = 0;
  ms->resources.data0.bucket_release_mask_hi &=
    ~svc_dom_resources->data0.bucket_release_mask_hi;
  svc_dom_resources->data0.bucket_release_mask_hi = 0;

  // Release the buffer stacks.
  ms->resources.data1.buffer_stack_mask &=
    ~svc_dom_resources->data1.buffer_stack_mask;

  // Obtain mpipe_alloc_lock in order to modify
  // global variable: all_shim_buffer_stack_allocated_mask.
  spin_unlock(&ms->lock);

  spin_lock(&mpipe_alloc_lock);

  spin_lock(&ms->lock);

  if (!is_open_svc_dom(svc_dom, ms))
  {
    spin_unlock(&mpipe_alloc_lock);
    return;
  }

  all_shim_buffer_stack_allocated_mask &=
    ~svc_dom_resources->data1.buffer_stack_mask;

  spin_unlock(&mpipe_alloc_lock);

  svc_dom_resources->data1.buffer_stack_mask = 0;

  // Release the EDMA Rings.
  ms->resources.data1.edma_post_mask &=
    ~svc_dom_resources->data1.edma_post_mask;
  svc_dom_resources->data1.edma_post_mask = 0;

  update_protections(ms, svc_dom);

  // Forget about any link status interrupts.
  for (int i = 0; i < MPIPE_POLLFD_LINK_INTR_PER_SD; i++)
    handle_gxio_mpipe_close_pollfd(ms, svc_dom, i);

  // Close any links opened by this user.
  uint32_t macs = svc_dom_resources->data_macs |
    svc_dom_resources->stats_macs | svc_dom_resources->control_macs;
  while (macs)
  {
    handle_gxio_mpipe_link_close_aux(ms, svc_dom, __builtin_ctzl(macs));
    macs &= macs - 1;
  }
}

/** Set the priority level to an edma ring. */
int
handle_gxio_mpipe_edma_ring_set_priority(
  mpipe_state_t* ms, int svc_dom,
  uint ering, uint ering_priority)
{
  if (!good_ering(ms, svc_dom, ering))
    return GXIO_MPIPE_ERR_BAD_EDMA_RING;

  MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_MAP,
      .idx = ering,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);

  // Read MPIPE_EDMA_RG_INIT_DAT into data.
  MPIPE_EDMA_RG_INIT_DAT_MAP_t data = {
    .word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT) };

  // Set the priority level and no touch to other fields.
  data.priority_lvl = ering_priority;

  // Write back.
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT, data.word);

  return 0;
}

/** Get the priority level of an edma ring. */
int
handle_gxio_mpipe_edma_ring_get_priority(
  mpipe_state_t* ms, int svc_dom, uint ering)
{
  if (!good_ering(ms, svc_dom, ering))
    return GXIO_MPIPE_ERR_BAD_EDMA_RING;

  MPIPE_EDMA_RG_INIT_CTL_t ctl = {{
      .struct_sel = MPIPE_EDMA_RG_INIT_CTL__STRUCT_SEL_VAL_MAP,
      .idx = ering,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_CTL, ctl.word);

  // Read MPIPE_EDMA_RG_INIT_DAT into data.
  MPIPE_EDMA_RG_INIT_DAT_MAP_t data = {
    .word = cfg_rd(ms->shim_pos.word, 0, MPIPE_EDMA_RG_INIT_DAT) };

  return data.priority_lvl;
}

/** Get the number of buffers currently in a buffer stack. */
int
handle_gxio_mpipe_get_buffer_count(
  mpipe_state_t* ms, int svc_dom, uint32_t stack)
{
  // Allow other domain reads the buffer count as long as the
  // stack is allocated.
  if (!good_aux(stack, ms->resources.data1.buffer_stack_mask,
                HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT,
                HV_MPIPE_ALLOC_BUFFER_STACKS_BITS))
  {
    return GXIO_MPIPE_ERR_BAD_BUFFER_STACK;
  }

  MPIPE_BSM_INIT_CTL_t ctl = {{ .reg = 0, .stack_idx = stack }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_BSM_INIT_CTL, ctl.word);

  MPIPE_BSM_INIT_DAT_0_t data0 =
    { .word = cfg_rd(ms->shim_pos.word, 0, MPIPE_BSM_INIT_DAT) };

  // Return the buffer count currently in the stack.
  return (data0.tos_idx * BSM_BUFFER_COUNT_PER_TOS_IDX);
}
