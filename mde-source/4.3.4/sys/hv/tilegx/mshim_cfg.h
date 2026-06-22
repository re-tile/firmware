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
 * Memory shim configuration.
 */

#ifndef _SYS_HV_TILEGX_MSHIM_CFG_H
#define _SYS_HV_TILEGX_MSHIM_CFG_H

#include "types.h"

/** Preconfigure a memory shim.  This probes the DIMMs, if any, attached
 *  to the shim so that we can compute the desired memory speed.
 * @param shimaddr Address of the memory shim.
 * @param rshimaddr Address of the rshim shim.
 * @param board_flags Board flags word (BOARD_xxx).
 * @param speed If nonzero, user-requested memory speed in MT/s.
 * @return 0 if there is known to be no memory on the specified shim;
 *  otherwise, the desired shim frequency in T/s.  Note that even if this
 *  routine returns a nonzero value, mshim_config_shim() may later report
 *  that there is no memory on the shim.
 */
long mshim_preconfig_shim(pos_t shimaddr, pos_t rshimaddr,
                          uint32_t board_flags, int speed);

/** Configure DDR voltage.  Must not be called until after
 *  mshim_preconfig_shim() has been called for all shims; must be called
 *  before mshim_config_shim() is called for any shim.
 * @param rshimaddr Address of the rshim shim.
 * @param board_flags Board flags word (BOARD_xxx).
 */
void mshim_config_ddr_voltage(pos_t rshimaddr, uint32_t board_flags);

/** Configure a memory shim.  Must not be called for a shim until after
 *  mshim_preconfig_shim() has been called for that shim.
 * @param shimaddr Address of the memory shim.
 * @param rshimaddr Address of the rshim shim.
 * @param board_flags Board flags word (BOARD_xxx).
 * @param speed Memory speed in T/s; must be no larger than the value
 *  previously returned from mshim_preconfig_shim().
 * @return 0 if there is no memory on the specified shim; -1 if there
 *  should be memory on the shim, but POST detected an error while
 *  testing it; otherwise, the size in bytes of the detected memory.
 */
int64_t mshim_config_shim(pos_t shimaddr, pos_t rshimaddr,
                          uint32_t board_flags, long speed);

/** Do an 8-byte diagnostic read to a memory shim.
 * @param shimaddr Address of the memory shim.
 * @param pa Physical address to read from.
 * @return Value read from shim.
 */
uint64_t mshim_diag_read(pos_t shimaddr, PA pa);

/** Do an 8-byte diagnostic write to a memory shim.
 * @param shimaddr Address of the memory shim.
 * @param pa Physical address to write to.
 * @param data Data to write.
 */
void mshim_diag_write(pos_t shimaddr, PA pa, uint64_t data);

/** State of an mshim zero operation.  The contents of this structure are
 *  private to the mshim zero routines. */
struct mshim_zero_state
{
  /** First byte of the current zero operation. */
  uint64_t start;
  /** Number of bytes in the current zero operation. */
  uint64_t cur_bytes;
  /** Number of bytes left to zero, beginning at start. */
  uint64_t rem_bytes;
};

/** Start a memory zero operation.
 * @param shimaddr Address of the memory shim.
 * @param bytes Number of bytes of memory on the shim.
 * @param state State structure; this is allocated by the caller, but its
 *   contents are private to the mshim_zero routines.
 */
void mshim_zero_start(pos_t shimaddr, int64_t bytes,
                      struct mshim_zero_state* state);

/** Check to see if a zero operation has completed.
 * @param shimaddr Address of the memory shim.
 * @param state State structure; must have been initialized by
 *   mshim_start_zero().
 * @return Nonzero if the zeroing is complete.
 */
int mshim_zero_done(pos_t shimaddr, struct mshim_zero_state* state);

#endif /* _SYS_HV_TILEGX_MSHIM_CFG_H */
