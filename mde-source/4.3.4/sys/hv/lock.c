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
 * Locking routines.
 */

#include <arch/spr.h>
#include <string.h>

#include "hvbme/shared_lock.h"

#include "bme.h"
#include "debug.h"
#include "hv.h"
#include "lock.h"
#include "mapping.h"
#include "msg.h"
#include "tlb.h"
#include "page.h"
#include "types.h"


/** Map the shared lock page in the DTLB.
 */
static void
map_shared_lock_page(PA page_pa, Lotar lotar)
{
  const int ps = TTE_SHIFT_TO_PS(__builtin_ctzl(HV_BME_SHARED_PAGE_SIZE));

  assert(page_pa);

  install_wired_mapping(HV_BME_SHARED_PAGE_VA, page_pa, ps,
                        HV_PTE_MODE_CACHE_TILE_L3, lotar);
}


/** Allocate and initialize some physical memory for a lock on resources
 * that must be shared between the HV and the BME.  Right now the only
 * resource we share is the JTAG.
 * @param page_pa Pointer to a physical address, which upon return of this
 *   routine will be set to the address of the allocated physical page.
 * @param page_lotar Pointer to a LOTAR, which upon return of this
 *   routine will be set to the home tile of the allocated physical page.
 */
void
init_shared_lock(PA* page_pa, Lotar* page_lotar)
{
  PA pa = get_phys(HV_BME_SHARED_PAGE_SIZE, HV_BME_SHARED_PAGE_SIZE);

  map_shared_lock_page(pa, my_lotar);
  memset((void*)HV_BME_SHARED_PAGE_VA, 0, HV_BME_SHARED_PAGE_SIZE);
  remove_wired_tte();

  // Return the pa and the lotar.
  *page_pa = pa;
  *page_lotar = my_lotar;
}


/** Initial iteration count for exponential backoff for spinlocks. */
#define BACKOFF_START 4

/** Grab a spinlock.
 * @param mutex The lock word.
 */
void
spin_lock(spinlock_t* mutex)
{
#ifdef DEBUG_SPINLOCK
  uint32_t locked_val = (1U << 31) | my_pos.word;
  uint_reg_t save_cmpexch = __insn_mfspr(SPR_CMPEXCH_VALUE);
  __insn_mtspr(SPR_CMPEXCH_VALUE, 0);

#endif
  for (int backoff = 0; ; backoff++)
  {
#ifdef DEBUG_SPINLOCK
    uint32_t prev_val = __insn_cmpexch4(&mutex->lock, locked_val);

    if (prev_val == 0)
#else
    if (__insn_exch4(&mutex->lock, 1) == 0)
#endif
      break;

#ifdef DEBUG_SPINLOCK
    if (prev_val == locked_val)
      panic("recursive spin_lock(%p) called from %p\n", mutex,
            __builtin_return_address(0));
#endif

    int iter = (backoff > 6) ? (BACKOFF_START << 6) :
                               (BACKOFF_START << backoff);
#pragma unroll 0
    for (; iter > 0; iter--)
      __insn_mfspr(SPR_PASS);

    handle_msg_intr();
  }
#ifdef DEBUG_SPINLOCK
  __insn_mtspr(SPR_CMPEXCH_VALUE, save_cmpexch);
#endif
}


/** Try to acquire a spinlock, without waiting.
 * @param mutex The lock word.
 * @return Zero if the lock was acquired, nonzero otherwise.
 */
int
spin_trylock(spinlock_t* mutex)
{
#ifdef DEBUG_SPINLOCK
  uint32_t locked_val = (1U << 31) | my_pos.word;
  uint_reg_t save_cmpexch = __insn_mfspr(SPR_CMPEXCH_VALUE);
  __insn_mtspr(SPR_CMPEXCH_VALUE, 0);
  int rv = __insn_cmpexch4(mutex, locked_val);
  __insn_mtspr(SPR_CMPEXCH_VALUE, save_cmpexch);
  return rv;
#else
  return (__insn_exch4(mutex, 1));
#endif
}


/** Release a spinlock.
 * @param mutex The lock word.
 */
void
spin_unlock(spinlock_t* mutex)
{
#ifdef DEBUG_SPINLOCK
  uint32_t locked_val = (1U << 31) | my_pos.word;
  if (mutex->lock != locked_val)
      panic("spin_unlock(%p) called from %p, but lock contains 0x%x\n",
            mutex, __builtin_return_address(0), mutex->lock);
#endif
  __insn_mf();
  mutex->lock = 0;
}


/** Grab the shared hypervisor/BME lock indicated by the lock number.
 */
void
hvbme_spin_lock(int lock_number)
{
  map_shared_lock_page(shared_lock_page_pa, shared_lock_page_lotar);

  spinlock_t* mutex = (spinlock_t*)HV_BME_SHARED_PAGE_VA + lock_number;

  spin_lock(mutex);

  remove_wired_tte_va(HV_BME_SHARED_PAGE_VA);
}


/** Release the shared hypervisor/BME lock indicated by the lock number.
 */
void
hvbme_spin_unlock(int lock_number)
{
  map_shared_lock_page(shared_lock_page_pa, shared_lock_page_lotar);

  spinlock_t* mutex = (spinlock_t*)HV_BME_SHARED_PAGE_VA + lock_number;

  spin_unlock(mutex);

  remove_wired_tte_va(HV_BME_SHARED_PAGE_VA);
}
