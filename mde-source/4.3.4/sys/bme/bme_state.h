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
 * Private state for the BME runtime.
 */

#ifndef _SYS_BME_STATE_H
#define _SYS_BME_STATE_H

#include <stdint.h>

#include <bme/interrupts.h>
#include <bme/sys_info.h>

/** BME runtime private state. */
typedef struct _bme_state
{
  /** Pointer to an optional application-supplied per-tile structure.  This
   * MUST BE FIRST in this structure, since the assembly implementation of
   * bme_{set, get}_pertile_state() assumes that. */
  void* user_state;

  /** Pointer to our local information structure, passed to us by the
   *  hypervisor at boot time. */
  bme_local_info_t* local_info;

  /** Pointer to our global information structure, passed to us by the
   *  hypervisor at boot time, if it's currently mapped into the TLB
   *  via bme_map_global_info(). */
  bme_global_info_t* global_info;

  /** DTLB index at which the global info structure is currently mapped. */
  int global_info_index;

  /** Reference count for the global info structure. */
  int global_info_refcnt;

  /** Number of tiles in our BME instance. */
  int num_tiles;

  /** Our ordinal number within the application (ranging from 0 to num_tiles -
   *  1). */
  int ordinal;

  /** Panic prefix ("(x,y) bme_panic: "). */
  char panic_prefix[24];

  /** tprintf() prefix ("(x,y) "). */
  char tprintf_prefix[12];

  /** Base VA used for tmc_mem_flush_l2(). */
  VA flush_va;

  /** Last-used offset for tmc_mem_flush_l2(). */
  VA flush_offset;

  /** Page size used for tmc_mem_flush_l2(). */
  int flush_ps;

  /** Reserved PA used for tmc_mem_flush_l2(). */
  PA flush_pa;

  /** Pointer to interrupt handlers for this tile. */
  bme_interrupt_handler_t* int_handler[NUM_INTERRUPTS];
} _bme_state_t;


/** Get this tile's state structure.
 * @return State structure pointer.
 */
_bme_state_t* _bme_get_state(void);


/** Set this tile's state structure.
 * @param state New state structure pointer.
 */
void _bme_set_state(_bme_state_t* state);

#endif /* _SYS_BME_STATE_H */
