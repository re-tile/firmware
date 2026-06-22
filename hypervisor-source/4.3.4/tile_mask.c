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
 * Tile mask routines.
 */

#include <stdio.h>

#include <arch/chip.h>

#include "sys/libc/include/util.h"

#include "hv.h"
#include "tile_mask.h"


/** Initialize a tile bitmask to contain a rectangle of set bits.
 * @param tm Mask to initialize.
 * @param ulhc Upper-left hand corner of rectangle.
 * @param lrhc Lower-right hand corner of rectangle.
 */
void
init_tile_mask(tile_mask* tm, pos_t ulhc, pos_t lrhc)
{
  clear_tile_mask(tm);

  for (int x = ulhc.bits.x; x <= lrhc.bits.x; x++)
    for (int y = ulhc.bits.y; y <= lrhc.bits.y; y++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };
      add_tile_mask(tm, tile);
    }
}


/** Clear a tile bitmask.
 * @param tm Mask to clear.
 */
void
clear_tile_mask(tile_mask* tm)
{
  for (int i = 0; i < sizeof (tm->mask) / sizeof (tm->mask[0]); i++)
    tm->mask[i] = 0;
}


/** Add a tile to a tile bitmask.
 * @param tm Mask to add to.
 * @param tile Tile to add.
 */
void
add_tile_mask(tile_mask* tm, pos_t tile)
{
  unsigned long tileno = POS2IDX(tile);
  unsigned long tileidx = tileno / NBPW;
  unsigned long tilemask = 1UL << (tileno % NBPW);
  tm->mask[tileidx] |= tilemask;
}


/** Delete a tile from a tile bitmask.
 * @param tm Mask to delete from.
 * @param tile Tile to delete.
 */
void
del_tile_mask(tile_mask* tm, pos_t tile)
{
  unsigned long tileno = POS2IDX(tile);
  unsigned long tileidx = tileno / NBPW;
  unsigned long tilemask = 1UL << (tileno % NBPW);
  tm->mask[tileidx] &= ~tilemask;
}


/** Test whether a tile is in a tile bitmask.
 * @param tm Mask to test.
 * @param tile Tile to test.
 * @return Nonzero if tile is in mask, zero otherwise.
 */
int
in_tile_mask(tile_mask* tm, pos_t tile)
{
  unsigned long tileno = POS2IDX(tile);
  if (tileno >= HV_TILES)
    return (0);
  unsigned long tileidx = tileno / NBPW;
  unsigned long tilemask = 1UL << (tileno % NBPW);
  return ((tm->mask[tileidx] & tilemask) != 0);
}


/** Find the first tile set in the mask.
 * @param tm Mask to search.
 * @param tile Returned tile, if one exists in the mask.
 * @return Nonzero if a tile was returned, zero otherwise.
 */
int
ffs_tile_mask(tile_mask* tm, pos_t* tile)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    if (tm->mask[i])
    {
      *tile = IDX2POS(__builtin_ctzl(tm->mask[i]) + i * NBPW);
      return (1);
    }

  return (0);
}


/** Count the number of tiles in the mask.
 * @param tm Mask to count.
 * @return The number of tiles in the mask.
 */
int
pcnt_tile_mask(tile_mask* tm)
{
  int retval = 0;
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    retval += __builtin_popcountl(tm->mask[i]);

  return (retval);
}


/** Test whether a tile mask is empty.
 * @param tm Mask to test.
 * @return Nonzero if tile mask has no bits set, zero otherwise.
 */
int tile_mask_is_empty(tile_mask* tm)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    if (tm->mask[i])
      return (0);

  return (1);
}


/** Set bits in a tile mask.
 * @param res Tile mask to be modified.
 * @param in Mask of bits to set in res.
 */
void
bis_tile_mask(tile_mask* res, tile_mask* in)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    res->mask[i] |= in->mask[i];
}


/** Clear bits in a tile mask.
 * @param res Tile mask to be modified.
 * @param in Mask of bits to clear in res.
 */
void
bic_tile_mask(tile_mask* res, tile_mask* in)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    res->mask[i] &= ~in->mask[i];
}


/** Clear bits in a tile mask.
 * @param res Tile mask to be modified.
 * @param in Mask of bits to "and" with res.
 */
void
and_tile_mask(tile_mask* res, tile_mask* in)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    res->mask[i] &= in->mask[i];
}


/** Test whether two tile masks have any bits in common.
 * @param tm1 First mask to test.
 * @param tm2 Second mask to test.
 * @return Nonzero if tile masks have any common bits set, zero otherwise.
 */
int
tile_mask_overlap(tile_mask* tm1, tile_mask* tm2)
{
  for (int i = 0; i < TILE_MASK_WORDS; i++)
    if (tm1->mask[i] & tm2->mask[i])
      return (1);

  return (0);
}


/** Dump out a tile mask.
 * @param tm Mask to dump.
 */
void
dump_tile_mask(tile_mask* tm)
{
  tprintf("   ");
  for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    printf("%3d", x - chip_ulhc.bits.x);
  printf("\n");

  for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
  {
    tprintf("%3d", y - chip_ulhc.bits.y);
    for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };
      if (in_tile_mask(tm, tile))
         printf("  *");
      else
         printf("   ");
    }
    printf("\n");
  }
}


/** Calculate the Manhattan distance between two coordinates. */
int
manhattan(pos_t a, pos_t b)
{
  int ax = a.bits.x;
  int ay = a.bits.y;
  int bx = b.bits.x;
  int by = b.bits.y;

  ax = (ax == 0xF) ? -1 : ax;
  ay = (ay == 0xF) ? -1 : ay;
  bx = (bx == 0xF) ? -1 : bx;
  by = (by == 0xF) ? -1 : by;

  int dx = ax - bx;
  int dy = ay - by;
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;

  return dx + dy;
}
