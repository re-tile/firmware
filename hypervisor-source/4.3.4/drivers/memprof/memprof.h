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
 * Definitions for the memory profiling driver.
 */

#ifndef _SYS_HV_DRV_MEMPROF_H
#define _SYS_HV_DRV_MEMPROF_H

#include <arch/msh.h>

#include "drvintf.h"

#include <hv/drv_memprof_intf.h>

//
// The memory shim statistics are almost same between Pro and Gx.
//

/** Width of shim counters. */
#define MEMPROF_SAMPLE_BITS 32
/** Value at which our counters wrap. */
#define MEMPROF_WRAP_OFFSET (1ULL << MEMPROF_SAMPLE_BITS)

#define MEMPROF_DEBUG_PERIOD (30*1000000) /**< Printout interval if debug. */

/** We have to sample the memory controller statistics fast enough to
    avoid any overflow.  Since we have 32 bits of counter, we actually have
    scads of time here; it works out to sampling every 10 seconds or so.
    However, we run the memory latency pings at the same frequency as the
    register sampling, and we want to do that a lot more frequently than
    once every 10 seconds. */
#define MEMPROF_SAMPLE_USEC ((1 << (16 - 2)) * 10 / 1000)

/** Data collected each time we sample the profiling counters. */
typedef union
{
  struct
  {
    int64_t msh_r_cnt;          ///< Total read count
    int64_t msh_rh_cnt;         ///< Read hit count
    int64_t msh_w_cnt;          ///< Total write count
    int64_t msh_wh_cnt;         ///< Write hit count
  };
  uint64_t array[4];            ///< Values as array for looped access
}
memprof_sample_t;

/** The counters to keep track how many perf 0-3 cnt wrap interrupts
    from one MSH. */
typedef struct
{
  uint64_t count[4];            ///< MSH Perf 0-3 cnt wrap counts
}
memprof_perf_cnt_wrap_t;

/** The registers accessed when taking a sample, in the same order as
    in memprof_sample_t.  Since we have a bunch of registers which can
    count different quantities, we just pick four of them here;
    the MEMPROF_MODEx_VALUE defines below determine what's being counted
    and need to be updated if memprof_sample_t changes. */
#define MEMPROF_SAMPLE_REG_ORDER \
  MSH_PERF_CNT_0,                \
  MSH_PERF_CNT_1,                \
  MSH_PERF_CNT_2,                \
  MSH_PERF_CNT_3,                \

/** The registers accessed when handling the msh perf cnt wrap
    interrupt, in same order as in memprof_sample_t. */

#define MEMPROF_MSH_PERF_WRAP_INT_MASK \
  MSH_INT_VEC0__PERF0_WRAP_MASK,       \
  MSH_INT_VEC0__PERF1_WRAP_MASK,       \
  MSH_INT_VEC0__PERF2_WRAP_MASK,       \
  MSH_INT_VEC0__PERF3_WRAP_MASK

/** The registers accessed when binding the msh perf cnt wrap
    interrupt, in same order as in memprof_sample_t. */

#define MEMPROF_MSH_PERF_WRAP_INT_SHIFT  \
  MSH_INT_VEC0__PERF0_WRAP_SHIFT,        \
  MSH_INT_VEC0__PERF1_WRAP_SHIFT,        \
  MSH_INT_VEC0__PERF2_WRAP_SHIFT,        \
  MSH_INT_VEC0__PERF3_WRAP_SHIFT

/** Event for counter 0. */
#define MEMPROF_MODE0_VALUE  17    // Total reads
/** Event for counter 1. */
#define MEMPROF_MODE1_VALUE  18    // Read hits
/** Event for counter 2. */
#define MEMPROF_MODE2_VALUE  19    // Total writes
/** Event for counter 3. */
#define MEMPROF_MODE3_VALUE  20    // Write hits

/** Mask of perf cnt 0-3 wrap interrupts. */
#define PERF_CNT_0_3_WRAP_MASK                  \
  (MSH_INT_VEC0__PERF0_WRAP_MASK |              \
   MSH_INT_VEC0__PERF1_WRAP_MASK |              \
   MSH_INT_VEC0__PERF2_WRAP_MASK |              \
   MSH_INT_VEC0__PERF3_WRAP_MASK)

/** A state object only allocated on the dedicated tile. */
typedef struct
{
  int is_running;                /**< True if we should be sampling. */
  int shr_mode;                  /**< True if in shared tile mode. */
  int perf_wrap_intr;            /**< interrupt of msh perf wrap. */            
  uint64_t next_sample_timer;    /**< Timer indicating next sample time. */
  uint64_t last_sample_cycle;    /**< Cycle at which we last updated samples. */
  uint64_t total_cycles;         /**< Total cycles profiled. */

  /** VA at which each memory shim can be pinged. */
  VA shim_ping_vas[MAX_MSHIMS];

  /** Most recent sample from each controller. */
  memprof_sample_t last_sample[MAX_MSHIMS];

  /** MSH perf cnt wrap counts. */
  memprof_perf_cnt_wrap_t perf_cnt_wrap_msh[MAX_MSHIMS];

  /** Accumulated statistics for each controller. */
  struct memprof_stats stats[MAX_MSHIMS];
}
memprof_ded_state_t;

/** A state object kept by every tile in the system. */
typedef struct
{
  pos_t my_pos;                   /**< This tile's coordinates */
  pos_t fwd_tile;                 /**< Forward requests here */
  uint32_t fwd:1;                 /**< Forward requests to fwd_tile? */
  const struct dev_info* infop;   /**< Device information */

  memprof_ded_state_t* ded_state; /**< Non-NULL on dedicated tile. */
}
memprof_state_t;

/** Measure the latency of a load to the given VA. */
uint32_t measure_load_latency(VA va);

/** Measure the latency of a load to the given PA. */
uint32_t measure_load_latency_phys(VA pa, unsigned long aar);

#endif /* _SYS_HV_DRV_MEMPROF_H */

