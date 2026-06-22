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
 * Tile and chip configuration.
 */

#ifndef _SYS_HV_TILE_H
#define _SYS_HV_TILE_H

#include "misc.h"
#include "tile_mask.h"
#include "param.h"
#include "tsb.h"
#include "types.h"

/** State conveyed to a slave tile from the boot master. */
struct slave_tile_state
{
  // Filesystem related data
  PA fs_pa;                       /**< Physical address of hypervisor
                                       filesystem. */
  int fs_len;                     /**< Length, in bytes, of hypervisor
                                       filesystem. */
  // Device related data
  unsigned long shim_mask;        /**< Shims to probe. */

  // Board related data
  uint32_t refclk_speed;          /**< Reference clock in hertz. */
  uint32_t board_flags;           /**< Board characteristics. */

  // Tile related data
  uint32_t cpu_speed;             /**< Tile clock in hertz. */

  PA hvgdb_pa;                    /**< Physical address of the debugger
                                       data area. */

  pos_t grid_ulhc;                /**< Switch fabric upper left-hand corner. */
  pos_t grid_lrhc;                /**< Switch fabric lower right-hand corner. */

  pos_t chip_logical_ulhc;        /**< Chip's logical upper left-hand
                                       corner. */
  pos_t chip_logical_lrhc;        /**< Chip's logical lower right-hand
                                       corner. */

  pos_t rshim;                    /**< Coordinates of the rshim. */

  pos_t chip_console;             /**< Coordinates of the console tile. */

  PA shared_lock_pa;              /**< Physical address of locks shared
                                       between the HV and client. */
  Lotar shared_lock_lotar;        /**< Lotar for memory for locks shared
                                       between the HV and client. */

  PA shared_mapping_table;        /**< PA of the shared mapping table. */
};

void init_memory_regs(void);
void init_slaves(struct slave_tile_state* sts, tile_mask* mask);
void start_slave_client(pos_t slave_pos, int clientnum);
void start_all_slave_clients(void);

void slave_init(struct slave_tile_state* sts);
void wait_for_start_client(void);
void slave_idle(void);

extern int start_client_flag;

#endif /* _SYS_HV_TILE_H */
