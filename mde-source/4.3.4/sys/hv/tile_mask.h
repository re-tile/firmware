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
 * Tile mask operations.
 */

#ifndef _SYS_HV_TILE_MASK_H
#define _SYS_HV_TILE_MASK_H

#include "param.h"
#include "types.h"
#include "bits.h"

/** Number of words in a tile mask. */
#define TILE_MASK_WORDS ((HV_TILES + NBPW - 1) / NBPW)

/** Bitmask of tiles.  The bits in this type of mask are relative to the
 *  largest chip supported by the hypervisor; if tile X is in the mask, then
 *  bit POS2IDX(X) is on in it.
 */
typedef struct
{
  unsigned long mask[TILE_MASK_WORDS];  /**< Mask bits */
}
tile_mask;


/** Bitmask of tiles.  The bits in this type of mask are relative to the
 *  rectangle for a particular client; if tile <x, y> is in the mask, then bit
 *  x + (y * width) is on in it.
 */
typedef struct
{
  unsigned long mask[TILE_MASK_WORDS];  /**< Mask bits */
}
client_tile_mask;

void init_tile_mask(tile_mask* tm, pos_t ulhc, pos_t lrhc);
void clear_tile_mask(tile_mask* tm);
void add_tile_mask(tile_mask* tm, pos_t tile);
void del_tile_mask(tile_mask* tm, pos_t tile);
void bis_tile_mask(tile_mask* res, tile_mask* in);
void bic_tile_mask(tile_mask* res, tile_mask* in);
void and_tile_mask(tile_mask* res, tile_mask* in);
int in_tile_mask(tile_mask* tm, pos_t tile);
int ffs_tile_mask(tile_mask* tm, pos_t* tile);
int pcnt_tile_mask(tile_mask* tm);
int tile_mask_is_empty(tile_mask* tm);
int tile_mask_overlap(tile_mask* tm1, tile_mask* tm2);
void dump_tile_mask(tile_mask* tm);
int manhattan(pos_t a, pos_t b);

//
// Right now these are the only manipulation routines we actually need for
// client tile masks.  Feel free to add client versions of other routines
// as needed.
//
/** Set bits in a client tile mask.
 * @param res Client tile mask to be modified.
 * @param in Mask of bits to set in res.
 */
static inline void
bis_client_tile_mask(client_tile_mask* res, client_tile_mask* in)
{
  bis_tile_mask((tile_mask*) res, (tile_mask*) in);
}

/** Clear bits in a client tile mask.
 * @param res Client tile mask to be modified.
 * @param in Mask of bits to "and" with res.
 */
static inline void
and_client_tile_mask(client_tile_mask* res, client_tile_mask* in)
{
  and_tile_mask((tile_mask*) res, (tile_mask*) in);
}

#endif /* _SYS_HV_TILE_MASK_H */
