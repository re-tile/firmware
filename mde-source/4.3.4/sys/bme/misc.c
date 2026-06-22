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
 * Miscelleanous utility routines.
 */

// FIXME: this needs to be ported to Gx.
#ifndef __tilegx__

#include <string.h>
#include <arch/cycle.h>

#include <bme/sys_info.h>
#include <bme/tte.h>
#include <bme/tlb.h>

#include <board_info.h>
#include <shared_lock.h>

#include "misc.h"

/// VA used for mapping memory for the shared JTAG spin lock.
/// Can be overridden by application.
VA bme_shared_spinlock_page_va = BME_HVBME_SPINLOCK_PAGE_VA;

/** Start a driver timer.
 * @param usec Microseconds until the timer will complete.
 * @return Value which can be passed to drv_timer_done() to test for
 *         timer completion.
 */
uint64_t
drv_timer_start(uint32_t usec)
{
  uint64_t cur_time = get_cycle_count();

  // FIXME: decide if we really want to map this every single time, or
  // if we should have an init function that gets called to set a static
  // variable
  bme_global_info_t* global_info = bme_map_global_info();

  return (cur_time + ((uint64_t) global_info->cpu_speed * usec) / 1000000ULL);
}


/** Test a driver timer for completion.
 * @param timer Timer value returned from drv_timer_start().
 * @return Nonzero if the time specified when the timer was started has
 *         passed; zero otherwise.
 */
int
drv_timer_done(uint64_t timer)
{
  return (get_cycle_count() >= timer);
}


/** Delay for a short period of time.
 * @param usec Microseconds of delay requested.  The actual delay will be
 *        no less than this value.
 */
void
drv_udelay(uint32_t usec)
{
  uint64_t timer = drv_timer_start(usec);

  while (!drv_timer_done(timer))
    ;
}

/** Get a MAC address.
 * @param is_xaui Nonzero if this is a XAUI interface.
 * @param instance Instance number of the device.
 * @param mac Buffer in which to return the 6-byte MAC address.
 * @return Nonzero if a MAC address was returned, zero otherwise.
 */
int
drv_get_mac(int is_xaui, int instance, uint8_t mac[])
{
  uint32_t* resbuf;

  bme_global_info_t* global_info = bme_map_global_info();

  if (bi_find(global_info->bib_buf, global_info->bib_len, BI_TYPE_MAC,
              ((is_xaui) ? BI_INST_XAUI : BI_INST_GBE) + instance,
              &resbuf, 0) != BI_NULL)
  {
    memcpy(mac, resbuf, 6);
    return (1);
  }
  return (0);
}

/** Get maximum interface speed.
 * @param is_xaui Nonzero if this is a XAUI interface.
 * @param instance Instance number of the device.
 * @return Maximum interface speed in megabits per second, or -1 if no maximum
 *        speed is defined.
 */
int
drv_get_intf_max_speed(int is_xaui, int instance)
{
  uint32_t* resbuf;

  bme_global_info_t* global_info = bme_map_global_info();

  if (bi_find(global_info->bib_buf, global_info->bib_len,
              BI_TYPE_INTF_MAX_SPEED,
              ((is_xaui) ? BI_INST_XAUI : BI_INST_GBE) + instance,
              &resbuf, 0) != BI_NULL)
    return (*resbuf);

  return (-1);
}

#include <tmc/spin.h>

/** Map the shared lock page in the DTLB.
 */
static int
map_shared_lock_page(VA page_va, PA page_pa, Lotar lotar)
{
  bme_global_info_t* global_info = bme_map_global_info();
  Lotar my_lotar =
    HV_XY_TO_LOTAR(global_info->tile_table[bme_tile_index()].pos.bits.x,
                   global_info->tile_table[bme_tile_index()].pos.bits.y);

  int page_size = TTE_SHIFT_TO_PS(__insn_ctz(HV_BME_SHARED_PAGE_SIZE));
#ifdef __tilegx__
  tte_t tte = {
    {{ .ps = page_size,
       .g = 1,
       .asid = 0,
       .v = 1,
       .w = 1,
       .mpl = 2,
       .pin = 0,
       .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
       .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
       .location_x_or_page_mask = HV_LOTAR_X(lotar),
       .location_y_or_page_offset = HV_LOTAR_Y(lotar)
       .dtlbv = 1,
       .itlbv = 0,
    }},
    .w1.word = page_va,
    .w2.word = page_pa,
  };
#else
  int cacheable = lotar == my_lotar;
  tte_t tte = {
    {{ .ps = page_size,
       .g = 1,
       .asid = 0,
       .vpn = VPFN(page_va)
    }},
    {{ .pfn_high = PFN_HI(page_pa) }},
    {{ .v = 1,
       .w = 1,
       .c = cacheable,
       .lo = 1,
       .be = 1,
       .re = 1,
       .mpl = 2,
       .pfn_low = PFN_LO(page_pa)
    }},
    {{ .lotar_y = HV_LOTAR_Y(lotar),
       .lotar_x = HV_LOTAR_X(lotar)
    }}
  };
#endif

  return bme_install_dtte(&tte, BME_TTE_INDEX_WIRED);
}

/** Grab the shared hv/bme lock indicated by the lock number.
 */
void
hvbme_spin_lock(int lock_number)
{
  bme_global_info_t* global_info = bme_map_global_info();

  // map the memory for the lock to a va
  int dtlb_slot = map_shared_lock_page(bme_shared_spinlock_page_va,
                                       global_info->shared_lock_pa,
                                       global_info->shared_lock_lotar);

  tmc_spin_mutex_t* lock = (tmc_spin_mutex_t*)bme_shared_spinlock_page_va
    + lock_number;

  // grab the lock itself
  tmc_spin_mutex_lock(lock);

  // unmap the memory
  bme_remove_dtte(dtlb_slot);
}

/** Release the shared hv/bme lock indicated by the lock number.
 */
void
hvbme_spin_unlock(int lock_number)
{
  bme_global_info_t* global_info = bme_map_global_info();

  int dtlb_slot = map_shared_lock_page(bme_shared_spinlock_page_va,
                                       global_info->shared_lock_pa,
                                       global_info->shared_lock_lotar);

  tmc_spin_mutex_t* lock = (tmc_spin_mutex_t*)bme_shared_spinlock_page_va
    + lock_number;

  // grab the lock itself
  tmc_spin_mutex_unlock(lock);

  // unmap the memory
  bme_remove_dtte(dtlb_slot);
}

/** Find a descriptor in the system board information block.
 * @param type Type of descriptor to look for, or -1 to match any.
 * @param instance Instance number to look for, or -1 to match any.
 * @param resbuf Pointer to where a pointer to the found data is placed.
 * @param offset If NULL, the search will start at the beginning of the
 *        block.  Otherwise, points to an offset at which the search will
 *        start; if the item requested is found, the offset will be updated
 *        so that a subsequent search will start after the last-found item.
 *        This offset is opaque to the user; the only defined value is zero,
 *        which means "start searching at the beginning of the block".
 * @return The descriptor for the item, if found, or BI_NULL otherwise.
 */
uint32_t
bi_getparam(int type, int instance, uint32_t** resbuf, int* offset)
{
  bme_global_info_t* global_info = bme_map_global_info();

  //
  // Note that, unlike with the hypervisor version of this routine, we don't
  // do an automatic load of the info block.
  //
  if (!global_info->bib_buf)
    return (BI_NULL);

  //
  // Do the requested search against the system block.
  //
  return (bi_find(global_info->bib_buf, global_info->bib_len, type, instance,
                  resbuf, offset));
}
#endif
