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
 * A driver for profiling the memory controllers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/atomic.h>
#include <arch/cycle.h>
#include <arch/spr.h>

#include "cfg.h"
#include "debug.h"
#include "devices.h" // for mshims[]
#include "drvintf.h"
#include "hv.h" // for UXY()
#include "hw_config.h"
#include "memprof.h"
#include "physacc.h"


//#define MEMPROF_DEBUG
//#define MEMPROF_DEBUG_LIVE

static void init_hw();

// mshim memprof counter wrap around interrupt handler routine.
static void
mshim_delayed_perf_cnt_intr(void *intrarg, void *msg, int len)
{
  memprof_ded_state_t* ds = (memprof_ded_state_t*)intrarg;

  static const uint64_t perf_wrap_mask[] = {
    MEMPROF_MSH_PERF_WRAP_INT_MASK
  };

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i])
    {
      int controller = mshim_controller[i];

      // Read the vec status.
      uint64_t vec0 = cfg_rd(mshims[i]->idn_ports[0].word,
                             mshims[i]->channel,
                             MSH_INT_VEC0_W1TC) & PERF_CNT_0_3_WRAP_MASK;
      if (vec0)
      {
        for (int k = 0;
             k < sizeof(perf_wrap_mask) / sizeof(perf_wrap_mask[0]); k++)
        {
          // Handle Perf #k wrap interrupt - bump up the corresponding
          // counter.
          if (vec0 & perf_wrap_mask[k])
            ds->perf_cnt_wrap_msh[controller].count[k]++;
        }

        // Clear the intererupts processed.
        cfg_wr(mshims[i]->idn_ports[0].word, mshims[i]->channel,
               MSH_INT_VEC0_W1TC, vec0);
      }
    }
  }
}

/** Memory profiling driver init routine. */
static int
memprof_init(const char* drvname, void** statepp, int instance, int tileno,
             pos_t tile, const struct dev_info* info, const char* args)
{
  //
  // Allocate our state
  //
  memprof_state_t* ms = drv_state_zalloc(sizeof (*ms), 0);

  if (!ms)
    return (HV_EFAULT);

  *statepp = ms;

  //
  // If we're a dedicated tile, allocate more state.
  //
  if (tileno > 0)
  {
    ms->ded_state = drv_state_zalloc(sizeof (*(ms->ded_state)), 0);
    if (!ms->ded_state)
      return (HV_EFAULT);
  }

  if (tileno < 0)
  {
    ms->ded_state = drv_state_zalloc(sizeof (*(ms->ded_state)), 0);
    if (!ms->ded_state)
      return (HV_EFAULT);

    // Set shared tile mode.
    ms->ded_state->shr_mode = 1;
    init_hw();

    // Set up interrupt handling for perf count wrap.

    int intchan = drv_alloc_intchan();
    if (intchan < 0)
    {
      printf("hv_warning: memprof_init couldn't allocate interrupt");
      return HV_EFAULT;
    }

    if (drv_register_intr(mshim_delayed_perf_cnt_intr,
                          (void *)ms->ded_state,
                          DRV_INTR_DELAYED, intchan))
    {
      printf("hv_warning: memprof_init couldn't register interrupt");
      return HV_EFAULT;
    }

    // Save the interrupt.
    ms->ded_state->perf_wrap_intr = intchan;

    //
    // Binding the interrupts.
    //
    MSH_INT_BIND_t intbind = {{
        .enable = 1,
        .mode = 0,
        .tileid = DRV_COORDS_TO_TILE_ID(tile.bits.x, tile.bits.y),
        .int_num = HV_PL,
        .evt_num = intchan,
        .vec_sel = MSH_INT_BIND__VEC_SEL_VAL_INTS,
      }};

    static const uint64_t perf_wrap_shift[] = {
      MEMPROF_MSH_PERF_WRAP_INT_SHIFT
    };

    for (int i = 0; i < MAX_MSHIMS; i++)
    {
      if (mshims[i])
      {
        for (int k = 0;
             k < sizeof(perf_wrap_shift) / sizeof(perf_wrap_shift[0]); k++)
        {
          // Binding perf #k wrap interrupt.
          intbind.bind_sel = perf_wrap_shift[k];
          cfg_wr(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                 MSH_INT_BIND, intbind.word);
        }
      }
    }
  }

  //
  // Save other useful data.
  //
  ms->my_pos = tile;
  ms->infop = info;

  //
  // Save forward Tile.
  //
  if (info->num_dtiles <= 0)
    ms->fwd_tile = info->stiles[0];
  else
    ms->fwd_tile = info->dtiles[0];

  ms->fwd = (ms->fwd_tile.word != tile.word);

  return (0);
}


/** Memory profiling driver open routine. */
static int
memprof_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
             pos_t tile)
{
  DEVICE_TRACE("memprof_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  // No actual preparation to do here, so we don't forward anything to our
  // remote tile.

  if (*suffix == '\0')
    return (0);

  return (HV_ENODEV);
}


/** Memory profiling driver close routine. */
static int
memprof_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("memprof_close: devhdl %#x\n", devhdl);

  // Nothing to do on close.

  return (0);
}

/**
 * Refresh the memory controller samples, recording the change since
 * last time if requested.
 *
 * @param samples Array of MAX_MSHIMS old sample values; filled
 * with new values.
 *
 * @param deltas Array of MAX_MSHIMS samples; if non-NULL, filled
 * with (new - old).
 */
static void
refresh_samples(memprof_sample_t* samples, memprof_sample_t* deltas)
{
  // It might be better to scan all the shims in parallel, but the
  // skew of doing them sequentially should be vanishingly small when
  // compared to the sampling interval, so we'll just do the easy
  // thing.
  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i])
    {
      int controller = mshim_controller[i];
      static const int reg_nums[] = {
        MEMPROF_SAMPLE_REG_ORDER
      };

      for (int reg = 0; reg < sizeof(reg_nums) / sizeof(reg_nums[0]); reg++)
      {
        // Read the sample, calculate the delta, and overwrite the old sample.
        uint32_t sample = cfg_rd(mshims[i]->idn_ports[0].word,
                                 mshims[i]->channel, reg_nums[reg]);
        uint32_t old = samples[controller].array[reg];
        if (deltas)
        {
          //
          // If the new sample value is lower than the old one, we know that
          // the counter has wrapped, so we add the wrap offset.  Note that
          // , the wrap offset is a 64-bit value, so we just promote
          // everything to 64-bit while computing the delta.  The delta itself
          // is always small enough to fit in 32 bits, so it's okay that it
          // gets truncated on assigment.
          //
          deltas[controller].array[reg] = (sample >= old) ?
            (sample - old) :
            ((uint64_t) sample + MEMPROF_WRAP_OFFSET - (uint64_t) old);
        }
        samples[controller].array[reg] = sample;
      }
    }
  }
}

/**
 * Refresh the memory controller samples, recording the change since
 * last time if requested.
 *
 * @param ds memprof's ded_state.
 *
 * @param deltas Array of MAX_MSHIMS samples; if non-NULL, filled
 * with (new - old).
 */

static void
refresh_samples_shared(memprof_ded_state_t* ds, memprof_sample_t* deltas)
{
  // It might be better to scan all the shims in parallel, but the
  // skew of doing them sequentially should be vanishingly small when
  // compared to the sampling interval, so we'll just do the easy
  // thing.

  memprof_sample_t* samples = ds->last_sample;
  volatile memprof_perf_cnt_wrap_t* perf_cnt_wrap = ds->perf_cnt_wrap_msh;

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i])
    {
      int controller = mshim_controller[i];
      static const int reg_nums[] = {
        MEMPROF_SAMPLE_REG_ORDER
      };
      uint64_t  new_sample[sizeof(reg_nums) / sizeof(reg_nums[0])];

      while(1)
      {
        // Get the current perf cnt wrap interrupt status.
        uint64_t vec0 = cfg_rd(mshims[i]->idn_ports[0].word,
                               mshims[i]->channel,
                               MSH_INT_VEC0) & PERF_CNT_0_3_WRAP_MASK;

        if (vec0)
        {
          // Perf wrap interrupts are pending, now directly call perf cnt wrap
          // interrupt handler to "handle" it - bump up the perf cnt wrap
          // counters. Note: IPI interrupt is masked off at this moment.

#ifdef MEMPROF_DEBUG
          printf("memprof: 0 wrap interrupt %x\n", (int)vec0);
#endif
          mshim_delayed_perf_cnt_intr(ds, NULL, 0);
        }

        for (int reg = 0; reg < sizeof(reg_nums) / sizeof(reg_nums[0]); reg++)
        {
          // Read the sample, calculate the delta, and overwrite the old sample.
          uint64_t sample = cfg_rd(mshims[i]->idn_ports[0].word,
                                   mshims[i]->channel, reg_nums[reg]);
          uint64_t old = samples[controller].array[reg];

          // Save the new sample locally in case there is perf cnt wrap.
          new_sample[reg] = sample +
            (perf_cnt_wrap[controller].count[reg] * MEMPROF_WRAP_OFFSET);

          if (deltas)
          {
            //
            // To get the deltas based on current sample and perf cnt wrap.
            // Note: all SW counts are 64 bits and never wrap. At this stage,
            // all counts modified are temporary.
            //
            new_sample[reg] = sample +
              (perf_cnt_wrap[controller].count[reg] * MEMPROF_WRAP_OFFSET);
            deltas[controller].array[reg] = new_sample[reg] - old;
          }
        }

        //
        // We had read all 4 samples for this msh. Now, go back to check
        // if any perf cnt wrap interrupt is pending. If yes, that means
        // perf cnt wrapped during our processing, we need start over.
        // Luckily, this will be a very rare case.

        // Get the current perf cnt wrap interrupt status again.
        //
        vec0 = cfg_rd(mshims[i]->idn_ports[0].word,
                      mshims[i]->channel,
                      MSH_INT_VEC0) & PERF_CNT_0_3_WRAP_MASK;

        if (!vec0)
        {
          // Now, no perf cnt wrap pending, and the IPI interrupt is disabled,
          // So there is no wrap during our processing and it is safe to update
          // the new samples now.

          for (int reg = 0; reg < sizeof(reg_nums) / sizeof(reg_nums[0]); reg++)
          {
#ifdef MEMPROF_DEBUG
            printf("msh[%d], %x %16llx %16llx %16llx %16llx\n", i, reg,
                   (unsigned long long)samples[controller].array[reg],
                   (unsigned long long)new_sample[reg],
                   (unsigned long long)deltas[controller].array[reg],
                   (unsigned long long)perf_cnt_wrap[controller].count[reg]);
#endif
            samples[controller].array[reg] =  new_sample[reg];
          }
          // Go to next msh.
          break;
        }
#ifdef MEMPROF_DEBUG
        printf("memprof: 1 wrap interrupt %x\n", (int)vec0);
#endif
      }
    }
  }
}


/**
 * Measure and update memory read latency to all active memory shims.
 * Count cycles for an uncached load and sum the results to avoid optimization.
 *
 * @param ds The dedicated tile state object.
 */
static void
update_latencies(memprof_ded_state_t* ds)
{
  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i])                              // Exists
    {
      int controller = mshim_controller[i];
      uint32_t cycles;

      if (ds->shr_mode)
      {
        // physical memory mode, uncachable access.
        SPR_AAR_t aar = {{
            .physical_memory_mode = 1,
            .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE,
          }};
        cycles = measure_load_latency_phys(mshim_ping[i], aar.word);
      }
      else
        cycles = measure_load_latency(ds->shim_ping_vas[i]);

      ds->stats[controller].lrd_count += 1;
      ds->stats[controller].lrd_cycles += cycles;
    }
  }
}


/**
 * Update the statics, given an array of sample deltas.
 *
 * @param ds The dedicated tile state object.
 * @param deltas Array of MAX_MSHIMS deltas from refresh_samples().
 */
static void
update_stats(memprof_ded_state_t* ds, memprof_sample_t* deltas)
{
  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    ds->stats[i].read_hit_count += deltas[i].msh_rh_cnt;
    ds->stats[i].read_miss_count += deltas[i].msh_r_cnt - deltas[i].msh_rh_cnt;
    ds->stats[i].write_hit_count += deltas[i].msh_wh_cnt;
    ds->stats[i].write_miss_count += deltas[i].msh_w_cnt - deltas[i].msh_wh_cnt;
  }
}


#if defined(MEMPROF_DEBUG) || defined (MEMPROF_DEBUG_LIVE)
static void
print_stats(memprof_ded_state_t* ds)
{
  printf("Memprof over %'llu cycles:\n", ds->total_cycles);

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i])
    {
      int controller = mshim_controller[i];
      printf("Mshim %d stats:\n"
             "  read(hit; miss)=(%'llu; %'llu)\n"
             "  write(hit; miss)=(%'llu; %'llu)\n",
             controller,
             ds->stats[controller].read_hit_count,
             ds->stats[controller].read_miss_count,
             ds->stats[controller].write_hit_count,
             ds->stats[controller].write_miss_count);
      printf("Mshim %d latency: %'llu reads %'llu cycles\n",
             controller,
             ds->stats[controller].lrd_count,
             ds->stats[controller].lrd_cycles);
    }
  }
}
#endif


static void
start_running(memprof_ded_state_t* ds)
{
  if (!ds->is_running)
  {
    ds->next_sample_timer = drv_timer_start(MEMPROF_SAMPLE_USEC);
    ds->is_running = 1;
    ds->last_sample_cycle = get_cycle_count();

    // Zero out our samples so that we don't display garbage for
    // nonexistent memory controllers.
    memset(ds->last_sample, 0, sizeof (ds->last_sample));

    // Get a clean start by making sure our counts are up-to-date.
    refresh_samples(ds->last_sample, NULL);
  }
}


static void
update_stats_and_cycle(memprof_ded_state_t* ds)
{
  memprof_sample_t deltas[MAX_MSHIMS];

  // Update samples
  if (ds->shr_mode)
    refresh_samples_shared(ds, deltas);
  else
    refresh_samples(ds->last_sample, deltas);

  // Update the cycle count.
  uint64_t new_start = get_cycle_count();
  ds->total_cycles += new_start - ds->last_sample_cycle;
  ds->last_sample_cycle = new_start;

  // Update aggregated stats.
  update_stats(ds, deltas);
  update_latencies(ds);
}

static void
stop_running(memprof_ded_state_t* ds)
{
  if (ds->is_running)
  {
    update_stats_and_cycle(ds);
    ds->is_running = 0;

#ifdef MEMPROF_DEBUG
    print_stats(ds);
#endif
  }
}


/** Clear all statistics to zero.  Should work regardless of whether
    we're currently running. */
static void
clear_stats(memprof_ded_state_t* ds)
{
  memset(ds->stats, 0, sizeof(ds->stats));
  ds->total_cycles = 0;
}


/** Initialize the hardware as required. */
static void
init_hw()
{
  //
  // We have a bunch of counters which can count different events,
  // so we need to configure them to count what we're going to use.
  //
  MSH_PERF_CTL1_t ctl = {{
    .mode0 = MEMPROF_MODE0_VALUE,
    .mode1 = MEMPROF_MODE1_VALUE,
    .mode2 = MEMPROF_MODE2_VALUE,
    .mode3 = MEMPROF_MODE3_VALUE,
  }};

  for (int i = 0; i < MAX_MSHIMS; i++)
    if (mshims[i])
      cfg_wr(mshims[i]->idn_ports[0].word, mshims[i]->channel,
             MSH_PERF_CTL1, ctl.word);

}


/** Create VA space mappings for each mshim. */
static void
init_mappings(memprof_ded_state_t* ds)
{
  for (int i = 0; i < MAX_MSHIMS; i++)
    if (mshims[i])
      assert(drv_map_dtlb_page(mshim_ping[i], 4096, DRV_MAP_MODE_UNCACHED, 0,
                               &ds->shim_ping_vas[i]) == 0);
}


/** Memory profiling driver service routine. */
static int __attribute__((__noreturn__))
memprof_service(void* statep)
{
  memprof_state_t* ms = statep;
  memprof_ded_state_t* ds = ms->ded_state;

  // Extra dedicated tile initialization.
  init_hw();
  init_mappings(ds);

  // Begin by pretending that somebody just issued a start.
  clear_stats(ds);
  start_running(ds);

#ifdef MEMPROF_DEBUG_LIVE
  uint64_t debug_timer = drv_timer_start(MEMPROF_DEBUG_PERIOD);
#endif

  while (1)
  {
    while (ds->is_running)
    {
      if (drv_timer_done(ds->next_sample_timer))
      {
        update_stats_and_cycle(ds);
        ds->next_sample_timer = drv_timer_start(MEMPROF_SAMPLE_USEC);
      }
      else
        drv_yield();

#ifdef MEMPROF_DEBUG_LIVE
      if (drv_timer_done(debug_timer))
      {
        print_stats(ds);
        debug_timer = drv_timer_start(MEMPROF_DEBUG_PERIOD);
      }
#endif // MEMPROF_DEBUG_LIVE
    }

    // At this point, is_running should be false; wait for it to change.
    drv_nap_until_change(&(ds->is_running), sizeof(ds->is_running),
                         ds->is_running);
  }
}

/** Memory profiling driver read routine.
 * Allow the supervisor to read the gathered statistics.  This is a
 * placeholder for now; eventually the read method will provide an
 * interface for reading the stats object.
 */
static int
memprof_pread(int devhdl, void* statep, uint32_t flags, char* va,
              uint32_t len, uint64_t offset, pos_t tile)
{
  memprof_state_t* ms = statep;
  memprof_ded_state_t* ds = ms->ded_state;

  DEVICE_TRACE("memprof_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ms->fwd)
    return (drv_pread_remote(devhdl, flags, va, len, offset, ms->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // We don't do anything special when the memory buffer is bad, but we need
  // to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  if (MEMPROF_RESULT_OFF <= offset)
  {
    struct memprof_result results;

    // Update counts only at offset == MEMPROF_RESULT_OFF to keep
    // consistent.  Also, don't update counts if we're not running.
    // Or in the shared tile mode.
    offset -= MEMPROF_RESULT_OFF;
    if (offset == 0 && (ds->is_running || ds->shr_mode))
      update_stats_and_cycle(ds);

    // Fill in the result structure
    memset(&results, 0, sizeof(results));
    for (int i = 0; i < MAX_MSHIMS; i++)
    {
      if (mshims[i])
      {
        int controller = mshim_controller[i];
        results.stats[controller] = ds->stats[controller];
        results.stats[controller].is_valid = 1;
      }
    }
    results.cycles = ds->total_cycles;

    if (len > sizeof (results) - offset)
      len = sizeof (results) - offset;
    drv_copy_to_client(va, offset + (char*) &results, len, flags);
    return len;
  }

  return (HV_EINVAL);
}


/** Memory profiling driver write routine. */
static int
memprof_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  memprof_state_t* ms = statep;
  memprof_ded_state_t* ds = ms->ded_state;

  DEVICE_TRACE("memprof_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // Forward on to the remote tile if needed.
  //
  if (ms->fwd)
    return (drv_pwrite_remote(devhdl, flags, va, len, offset, ms->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // We don't do anything special when the memory buffer is bad, but we need
  // to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  int err = 0;
  switch(offset)
  {
  case MEMPROF_START_OFF:
    start_running(ds);
    break;

  case MEMPROF_STOP_OFF:
    stop_running(ds);
    break;

  case MEMPROF_CLEAR_OFF:
    clear_stats(ds);
    break;

  default:
    err = HV_EINVAL;
  }

  return err;
}


/** Memory profiling driver operations vector */
static struct drv_ops memprof_ops = {
  .init        = memprof_init,
  .open        = memprof_open,
  .close       = memprof_close,
  .pread       = memprof_pread,
  .pwrite      = memprof_pwrite,
  .service     = memprof_service,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_memprof_dedicated = {
  .shim_type  = 0,
  .name       = "memprof",
  .desc       = "Memory Profiling",
  .ops        = &memprof_ops,
  .dtilereq   = 1,
};

//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_memprof_shared = {
  .shim_type  = 0,
  .name       = "memprof_shared",
  .desc       = "Memory Profiling without using dedicated tile",
  .ops        = &memprof_ops,
  .stilereq   = 1
};


