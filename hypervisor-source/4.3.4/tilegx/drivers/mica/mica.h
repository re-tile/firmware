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
 * general mica driver.
 */
#ifndef _DRIVERS_MICA_MICA_H_
#define _DRIVERS_MICA_MICA_H_


#include <hv/drv_mica_intf.h>
#include <hv/iorpc.h>

#include "lock.h"

/** Mica shim reset information. */
typedef struct
{
  /** The shim associated with this driver can be reset.
   */
  int is_resettable;

  /** Lock to prevent multiple simultaneous resets. 
   */
  spinlock_t lock;

  /** Counter used for synchronization of tiles when the shims are being
   * reset.  Remote tiles are known to be in the hypervisor when they respond
   * to a message with this updated counter.
   */
  volatile int shim_reset_counter;

  /** Mask to indicate which engines on the shim were already disabled when the
   * request arrived to reset the shim.  Disabled engines can't be accessed
   * for save/restore.  The bit position corresponds to the engine number,
   * 1 indicates that the engine was disabled.
   */
  unsigned int disabled_engine_mask;

  /** Mask to indicate which contexts had an operation pending when
   * the request arrived to reset the shim. The bit position corresponds to
   * the context number, 1 indicates that an operation was pending.
   */
  unsigned long contexts_pending_mask;

  /** Mask to indicate which contexts had an operation in progress when
   * the request arrived to reset the shim. The bit position corresponds to
   * the context number, 1 indicates that an operation was in progress.
   */
  unsigned long contexts_in_progress_mask;

  /** Pointer to memory in which to store shim register values during shim
   * reset.
   */
  uint64_t* reg_save_buf;

} mica_reset_t;


/** Global mica state. */
typedef struct
{
  /** Must hold this lock to modify shared data. */
  spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;

  /** Number of iotlb entries supported per context, read from hardware. */
  int num_iotlb_entries;

  /** Number of iotlb entries used per context. */
  unsigned int iotlb_entries_used[HV_MICA_NUM_CONTEXTS];

  /** Bitfield for reserved contexts. LSB == context 0. */
  unsigned long reserved_contexts_bitmask;

  /** Reference count on access to the EIP154 control registers. */
  int ctl_reg_access_ref_cnt;

  /** Only allow one user at a time of each shim's TRNG.  Non-zero if
   * the shim's TRNG is in use. */
  int trng_in_use;

  /** Information needed for shim reset. */
  mica_reset_t reset;
}
mica_state_t;

#endif /* ! _DRIVERS_MICA_MICA_H_ */
