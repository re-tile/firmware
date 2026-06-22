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
 * Miscellaneous memory shim routines.
 */

#ifndef _SYS_HV_TILEGX_MSHIM_ACC_H
#define _SYS_HV_TILEGX_MSHIM_ACC_H

#include <arch/chip.h>

/** Log2 of minimum bytes contained by one shim */
#define MSH_MIN_SIZE_SHIFT      29
/** Log2 of maximum bytes contained by one shim */
#define MSH_MAX_SIZE_SHIFT      (CHIP_PA_WIDTH() - CHIP_LOG_NUM_MSHIMS())


/** Set one of the processor's CBOX_MMAP SPRs.
 * @param index Index of the map register to set.
 * @param value Value to set it to.
 */
void set_cbox_mmap_spr(int index, uint32_t value);

/** Determine which mshim port should be used by a tile or shim.
 * @param pos Position of the tile or shim MDN port.
 * @param mshim_idx Index of the mshim within the mshims array.
 * @return Index within the mdn_ports[], or -1 if invalid.
 */
int mshim_portidx_from_pos(pos_t pos, int mshim_idx);

#endif /* _SYS_HV_TILEGX_MSHIM_ACC_H */
