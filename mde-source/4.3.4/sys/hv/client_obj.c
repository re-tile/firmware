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
 * Routines to translate between client objects and real hardware objects.
 * Also, routines to implement the related client syscalls.
 */

#include <util.h>
#include <string.h>

#include <arch/chip.h>

#include <hv/hypervisor.h>
#include "sys/libc/include/util.h"

#include "client_obj.h"
#include "debug.h"
#include "devices.h"
#include "fault.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "mshim_acc.h"
#include "param.h"
#include "types.h"

//
// The following lotar_{x,y}_{offset,limit} variables allow for a
// general virtualizing scheme for client tiles, so that e.g. you
// could offset a client's tiles to have a ulhc of (1,5) and size of
// (4,4), and it would appear to have a 4x4 mesh.  However, at this
// time we are only using the mechanism to convert from (1,1)-based
// hardware coordinates to (0,0)-based client coordinates; we are
// leaving the infrastructure intact to guard against future changes.
//

/** Value to add to the client's X LOTAR value to get the real value. */
static uint32_t lotar_x_offset;

/** Largest permissible client X LOTAR value (1 less than client's X width). */
static uint32_t lotar_x_limit;

/** Value to add to the client's LOTAR Y value to get the real value. */
static uint32_t lotar_y_offset;

/** Largest permissible client Y LOTAR value (1 less than client's Y width). */
static uint32_t lotar_y_limit;

/** Mask of valid tiles within client rectangle */
static tile_mask msg_tiles;

/** Mask of valid tiles within client rectangle (client-based format) */
static client_tile_mask msg_tiles_client;

/** Mask of tiles used for hash-for-home caching (client-based format) */
static client_tile_mask home_map_tiles_client;

/** Mask of tiles to which this client can direct flush requests.  Should
 *  always be equal to msg_tiles_client | home_map_tiles_client; not
 *  instantiated on chips without a home map since we can then just use
 *  msg_tiles_client. */
static client_tile_mask flushable_tiles_client;

/** Mask of valid client tiles, plus dedicated tiles that client
    LOTARs may target. */
static tile_mask lotar_tiles;

/** Value to add to a client's ASID to get the real value. */
static Asid asid_offset;

/** Largest permissible client ASID. */
static Asid asid_limit;


//
// Memory shims are associated with several different identifiers, used
// for different purposes.  All of these are unsigned integers less
// than MAX_MSHIMS.
//
// - The "mshim index" is the index within the mshims[] table of a
//   particular memory shim's info structure.  It is equal to the high
//   2 bits of the physical address used to access memory on that shim.
//   Not all entries in the mshims[] table may be valid, so the set of
//   mshim indices is not compact.
//
// - The "CPA range" is the high 2 bits of a client physical address.  The
//   set of CPA ranges starts at 0, and unlike the set of mshim indices, is
//   compact.  On Gx, or if we are not doing memory striping on T64/Pro,
//   every CPA range is associated with exactly one distinct mshim index.
//   However, if we decided not to allocate client memory on all shims,
//   either due to the memory command in the hvconfig file, or because a
//   thorough POST performed during hypervisor initialization failed, then
//   there may be mshim indices which do not have a corresponding CPA range,
//   and the associated values may not be equal.  (For instance, if there are
//   4 mshims but we do not have any client memory on the first memory shim,
//   CPA ranges 0, 1, and 2 might be associated with mshim indices 1, 2, and
//   3.)
//
//   If we are doing memory striping on T64/Pro, all memory is associated
//   with CPA range 0.
//
// - The "controller" is the actual physical index of the memory shim on the
//   chip; i.e., controller N is connected to the wires labeled on the data
//   sheet as belonging to msh_N.  This may not match the mshim index if
//   we disabled shims during L1 boot because they had no memory or they
//   failed POST.  The controller is used in certain hypervisor system
//   calls which retrieve information about physical memory characteristics.
//

/** Value to add to the low bits of a client's CPA to get the real value.
 *  This includes the low offset and the high bits of the PA.  This is
 *  indexed by a CPA range. */
static PA c_cpa_offset[MAX_MSHIMS];

/** Largest permissible value for the low bits of the client CPA; 1 less
 *  than the client's allocated memory in that range.  This is indexed by
 *  a CPA range. */
static PA c_cpa_limit[MAX_MSHIMS];

/** Nonzero if the indexed range has valid _offset and _limit fields.
 *  This is indexed by a CPA range. */
static uint8_t c_cpa_valid[MAX_MSHIMS];

/** What shift value of the CPA gives us the CPA range bits? */
static int cpa_range_shift = CPA_RANGE_SHIFT;

/** Log 2 of largest legal page size.  Normally PG_SHIFT_MAX, but if we are
 *  making client memory contiguous, it will be limited to the size of an
 *  individual controller. */
int pg_shift_max = PG_SHIFT_MAX;

/** Value to subtract from the low bits of a real PA to get the low bits of
 *  the corresponding CPA.  This is indexed by an mshim index. */
static PA r_cpa_low_offset[MAX_MSHIMS];

/** High bits of the CPA.  This is indexed by an mshim index. */
static PA r_cpa_high_offset[MAX_MSHIMS];

/** Largest permissible value for the low bits of the client CPA; 1 less
 *  than the client's allocated memory on that shim.  This is indexed by
 *  an mshim index. */
static PA r_cpa_limit[MAX_MSHIMS];

/** Nonzero if the indexed shim has valid _offset and _limit fields.
 *  This is indexed by an mshim index. */
static uint8_t r_cpa_valid[MAX_MSHIMS];

/** An array of PAs to be used when 'pinging' every memory controller
 *  in order to guarantee that victims are visible at the controller.
 *  This array is packed to contain only valid controllers which are
 *  accessible to the client; the final entry is marked by the value -1. */
PA client_fence_incoherent_pas[MAX_MSHIMS + 1] = { -1 };

/** An array of PAs to be used when 'pinging' every memory controller
 *  in order to guarantee that victims are visible at the controller.
 *  This array is packed to contain only valid controllers; the final
 *  entry is marked by the value -1. */
PA hv_fence_incoherent_pas[MAX_MSHIMS + 1] = { -1 };


/** Translate a chip-based tile mask to a client-based one.
 * @param r_tiles Input chip-based tile mask.
 * @param c_tiles Output client-based tile mask.
 */
static void
r2c_mask(tile_mask* r_tiles, client_tile_mask* c_tiles)
{
  memset(c_tiles, 0, sizeof (*c_tiles));

  for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
      if (in_tile_mask(r_tiles, (pos_t){ .bits.x = x, .bits.y = y }))
      {
        uint32_t bitnum = (x - lotar_x_offset) +
                          (y - lotar_y_offset) * (lotar_x_limit + 1);
        c_tiles->mask[bitnum / NBPW] |= (1UL << (bitnum % NBPW));
      }
}


/** Configure the client's virtual geometry.
 * @param x X coordinate of client's upper-left-hand tile.
 * @param y Y coordinate of client's upper-left-hand tile.
 * @param w Width of client's tile rectangle.
 * @param h Height of client's tile rectangle.
 * @param tiles Mask of available tiles within client.
 */
void
configure_client_geometry(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          tile_mask* tiles)
{
  lotar_x_offset = chip_logical_ulhc.bits.x;
  lotar_y_offset = chip_logical_ulhc.bits.y;
  lotar_x_limit = chip_logical_lrhc.bits.x - chip_logical_ulhc.bits.x;
  lotar_y_limit = chip_logical_lrhc.bits.y - chip_logical_ulhc.bits.y;
  msg_tiles = *tiles;

  r2c_mask(&msg_tiles, &msg_tiles_client);
  flushable_tiles_client = msg_tiles_client;
  bis_client_tile_mask(&flushable_tiles_client, &home_map_tiles_client);

  // The lotar tiles are initially the same as the client tiles;
  // drivers may add more later via allow_client_pte_lotar().
  lotar_tiles = msg_tiles;
}


/** Retrieve the client's geometry.
 * @param x Pointer to X coordinate of client's upper-left-hand tile.
 * @param y Pointer to Y coordinate of client's upper-left-hand tile.
 * @param w Pointer to width of client's tile rectangle.
 * @param h Pointer to height of client's tile rectangle.
 * @param c_tiles Mask of available tiles within client.
 */
void
get_client_geometry(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h,
                    client_tile_mask* c_tiles)
{
  *x = lotar_x_offset;
  *y = lotar_y_offset;
  *w = lotar_x_limit + 1;
  *h = lotar_y_limit + 1;
  *c_tiles = msg_tiles_client;
}


/** Retrieve the client's geometry, and information on its set of
 *  cache-flushable tiles.
 * @param x Pointer to X coordinate of client's upper-left-hand tile.
 * @param y Pointer to Y coordinate of client's upper-left-hand tile.
 * @param w Pointer to width of client's tile rectangle.
 * @param h Pointer to height of client's tile rectangle.
 * @param flushable_tiles Mask of tiles to which the client can legally
 *  direct a flush request.
 */
void
get_client_flushinfo(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h,
                     client_tile_mask* flushable_tiles)
{
  *x = lotar_x_offset;
  *y = lotar_y_offset;
  *w = lotar_x_limit + 1;
  *h = lotar_y_limit + 1;
  *flushable_tiles = flushable_tiles_client;
}


/** Translate a LOTAR value in the client's virtual geometry to the real
 *  value.
 * @param client_lotar LOTAR value in the client's geometry.
 * @param real_lotar Pointer to returned real LOTAR value.
 * @return Nonzero if the client has specified an illegal value, 0 otherwise.
 */
int
c2r_lotar(Lotar client_lotar, Lotar* real_lotar)
{
  uint32_t c_x, c_y;

  c_x = HV_LOTAR_X(client_lotar);
  c_y = HV_LOTAR_Y(client_lotar);

  if (c_x > lotar_x_limit || c_y > lotar_y_limit)
    return (1);

  uint32_t r_x = c_x + lotar_x_offset;
  uint32_t r_y = c_y + lotar_y_offset;

  if (!in_tile_mask(&msg_tiles, (pos_t){ .bits.x = r_x, .bits.y = r_y }))
    return (1);

  *real_lotar = HV_XY_TO_LOTAR(r_x, r_y);
  return (0);
}


/** Translate a real LOTAR value to one in the client's virtual geometry.
 * @param real_lotar Real LOTAR value.
 * @param client_lotar Pointer to LOTAR value in the client's geometry.
 * @return Nonzero if the specified real LOTAR is unavailable to the client.
 */
int
r2c_lotar(Lotar real_lotar, Lotar* client_lotar)
{
  uint32_t c_x, c_y;

  uint32_t r_x = HV_LOTAR_X(real_lotar);
  uint32_t r_y = HV_LOTAR_Y(real_lotar);

  c_x = r_x - lotar_x_offset;
  c_y = r_y - lotar_y_offset;

  if (c_x > lotar_x_limit || c_y > lotar_y_limit)
    return (1);

  *client_lotar = HV_XY_TO_LOTAR(c_x, c_y);

  //
  // Note that some callers of this routine don't care about whether or not
  // the target tile is valid in the current client, so we make sure to do
  // this check after we've set the returned client LOTAR field.
  //
  if (!in_tile_mask(&msg_tiles, (pos_t){ .bits.x = r_x, .bits.y = r_y }))
    return (1);

  return (0);
}


/** Allow client PTEs to set LOTAR values to a particular non-client
 *  tile.
 * @param position The location that may be OLOC'ed.
 * @param lotar Filled with lotar that client can use to access position.
 * @return Zero on success, nonzero on failure.
 */
int
allow_client_pte_lotar(pos_t position, Lotar* lotar)
{
  add_tile_mask(&lotar_tiles, position);

  // This arithmetic may underflow; that's okay because we'll treat
  // PTE Lotar bitfields as signed.
  uint32_t c_x = position.bits.x - lotar_x_offset;
  uint32_t c_y = position.bits.y - lotar_y_offset;
  *lotar = HV_XY_TO_LOTAR(c_x, c_y);

  return (0);
}


/** Allow client PTEs to set LOTAR values to particular non-client
 *  tiles, which are specified in a bit mask.
 * @param tiles The locations that may be OLOC'ed.
 * @return Zero on success, nonzero on failure.
 */
void
allow_client_pte_lotar_tile_mask(tile_mask* tiles)
{
  bis_tile_mask(&lotar_tiles, tiles);
}


/** Translate a LOTAR value in the client's virtual geometry to the
 *  real value and allow the LOTAR to include 'extra' tiles as
 *  requested by drivers.
 * @param client_lotar LOTAR value in the client's geometry.
 * @param real_lotar Pointer to returned real LOTAR value.
 * @return Nonzero if the client has specified an illegal value, 0 otherwise.
 */
int
c2r_pte_lotar(Lotar client_lotar, Lotar* real_lotar)
{
  // PTE LOTARs are treated as signed numbers so that we can
  // conveniently specify locations outside of the client rectangle.
  int32_t c_x, c_y;
  c_x = HV_LOTAR_X(client_lotar);
  c_y = HV_LOTAR_Y(client_lotar);

  const int sign_extend_shift = (32 - HV_LOTAR_WIDTH);
  c_x = ((c_x << sign_extend_shift) >> sign_extend_shift);
  c_y = ((c_y << sign_extend_shift) >> sign_extend_shift);

  uint32_t r_x = c_x + lotar_x_offset;
  uint32_t r_y = c_y + lotar_y_offset;
  if (r_x > chip_logical_lrhc.bits.x || r_y > chip_logical_lrhc.bits.y ||
      r_x < chip_logical_ulhc.bits.x || r_y < chip_logical_ulhc.bits.y)
    return (1);

  if (!in_tile_mask(&lotar_tiles, (pos_t){ .bits.x = r_x, .bits.y = r_y }))
    return (1);

  *real_lotar = HV_XY_TO_LOTAR(r_x, r_y);
  return (0);
}


/** Is the given LOTAR on the north edge of the client's virtual geometry?
 * @param client_lotar LOTAR value (in client virtual space).
 * @return Nonzero if the LOTAR is on the north edge of the client's space.
 */
int
on_north_edge(Lotar client_lotar)
{
  return (HV_LOTAR_Y(client_lotar) == 0);
}


/** Is the given LOTAR on the south edge of the client's virtual geometry?
 * @param client_lotar LOTAR value (in client virtual space).
 * @return Nonzero if the LOTAR is on the south edge of the client's space.
 */
int
on_south_edge(Lotar client_lotar)
{
  return (HV_LOTAR_Y(client_lotar) == lotar_y_limit);
}


/** Is the given LOTAR on the east edge of the client's virtual geometry?
 * @param client_lotar LOTAR value (in client virtual space).
 * @return Nonzero if the LOTAR is on the east edge of the client's space.
 */
int
on_east_edge(Lotar client_lotar)
{
  return (HV_LOTAR_X(client_lotar) == lotar_x_limit);
}


/** Is the given LOTAR on the west edge of the client's virtual geometry?
 * @param client_lotar LOTAR value (in client virtual space).
 * @return Nonzero if the LOTAR is on the west edge of the client's space.
 */
int
on_west_edge(Lotar client_lotar)
{
  return (HV_LOTAR_X(client_lotar) == 0);
}


/** Configure the client's legal ASIDs.
  * @param base Real ASID which corresponds to client ASID 0.
  * @param size Number of ASIDs client is permitted to use.
  */
void
configure_client_asids(Asid base, Asid size)
{
  asid_offset = base;
  asid_limit = size - 1;
}


/** Translate a clent ASID to a real ASID.
 * @param client_asid Client ASID.
 * @param real_asid Pointer to returned real ASID.
 * @return Nonzero if the client has specified an illegal value, 0 otherwise.
 */
int
c2r_asid(Asid client_asid, Asid* real_asid)
{
  if (client_asid > asid_limit)
    return (1);

  *real_asid = client_asid + asid_offset;
  return (0);
}


/** Translate a real ASID to a CPA.
 * @param real_asid Real ASID.
 * @param client_asid Pointer to returned client ASID.
 * @return Nonzero if the specified real ASID is unavailable to this client.
 */
int
r2c_asid(Asid real_asid, Asid* client_asid)
{
  Asid asid = real_asid - asid_offset;

  if (asid > asid_limit)
    return (1);

  *client_asid = asid;
  return (0);
}



/** Compute a home map from a set of tiles.
 * @param home_map_tiles Bitmap of tiles to include in the map.
 * @param home_map Output home map array.
 */
void
mask_to_home_map(tile_mask* home_map_tiles, uint32_t* home_map)
{

  //
  // This algorithm divides the hash-for-home lines as evenly as possible
  // over the set of available tiles.  We assume that there are more home
  // map entries than tiles, so we never need more than two tiles to take
  // care of one home map entry.
  //
  // The hardware allows us to split up a map entry into 128 parts; some
  // of those get assigned to one tile, and the rest to another, depending
  // upon the frac element of the map entry.  What we're doing here is
  // effectively dividing the total set of parts (128 parts times 128
  // map entries) evenly among the available tiles.
  //

  // Parts we can assign in each map entry.
  const int parts_per_map_entry = SPR_CBOX_HOME_MAP_DATA__FRAC_MASK + 1;

  // Total number of parts to be assigned to a CPU.
  int total_parts = CHIP_CBOX_HOME_MAP_SIZE() * parts_per_map_entry;

  // Number of tiles we'll be using.
  int num_tiles = pcnt_tile_mask(home_map_tiles);

  // List of tiles.
  int tiles[num_tiles];

  // Parts left for each tile in the tile array.
  int parts[num_tiles];

  // Fill in tiles[] and parts[].

  tile_mask remaining_tiles = *home_map_tiles;
  int parts_left = total_parts;

  for (int i = 0; i < num_tiles; i++)
  {
    pos_t tile;

    ffs_tile_mask(&remaining_tiles, &tile);
    del_tile_mask(&remaining_tiles, tile);
    tiles[i] = (tile.bits.x << 4) | tile.bits.y;

    // Note that we do this divide every time so that we get the most
    // even distribution of parts to tiles.

    parts[i] = parts_left / (num_tiles - i);
    parts_left -= parts[i];
  }

  // Index of the tile we're using.
  int cur_tile_index = num_tiles;

  // Number of parts we can still assign to tile at cur_tile_index.
  int parts_left_on_cur_tile = 0;

  // The entry we're currently filling in.
  SPR_CBOX_HOME_MAP_DATA_t this_ent = { .word = 0 };

  //
  // Calculate a limit on the number of parts we'll use from one tile
  // before we go to another.  This makes us spread out the tiles more over
  // the map entries, which means that it's more likely that similar
  // addresses map to different tiles.  We pick the smallest value which
  // more-or-less evenly divides the number of parts we use per tile but
  // which is still at least the number of parts per map entry; if it were
  // smaller than the latter, we might have to use three tiles per map
  // entry in some cases, which we can't do.  As a special case, if the
  // number of tiles evenly divides the number of map entries, then we
  // use half the parts per map entry, because everything will come out
  // even in that case and it gives us a better distribution.
  //
  int parts_per_pass;

  if (CHIP_CBOX_HOME_MAP_SIZE() % num_tiles == 0)
  {
    parts_per_pass = parts_per_map_entry / 2;
  }
  else
  {
    parts_per_pass = total_parts / num_tiles;
    parts_per_pass = parts_per_pass /
                     (parts_per_pass / parts_per_map_entry);
  }

  // Are we filling the A half of the entry first, or not?  We flip this
  // when we wrap around the list of CPUs; it provides better
  // randomization, especially when the number of tiles is a power of 2.
  int fill_a_first = 0;

  // Index of the map entry we're filling in.
  int cur_map_entry = 0;

  // State of the current entry.
  enum _map_entry_state
  {
    MAP_EMPTY,
    MAP_HALF_FULL_A,
    MAP_HALF_FULL_B,
    MAP_FULL
  }
  state = MAP_EMPTY;

  //
  // Keep going until we use all parts.
  //
  while (cur_map_entry < CHIP_CBOX_HOME_MAP_SIZE())
  {
    //
    // If we can't use more parts from our current tile, get another.
    //
    if (parts_left_on_cur_tile == 0)
    {
      // Bump tile number.
      cur_tile_index++;
      if (cur_tile_index >= num_tiles)
      {
        cur_tile_index = 0;
        fill_a_first = !fill_a_first;
      }

      //
      // We don't just clip this to the pass size, because due to rounding,
      // a tile often has N * parts_per_pass + M parts assigned to it,
      // where M is something small like 1 or 2.  We have to use these, but
      // we can't possibly use just 1 part from a bunch of different tiles,
      // so instead we make the last set of parts a bit bigger.
      //
      if (parts[cur_tile_index] < 2 * parts_per_pass)
        parts_left_on_cur_tile = parts[cur_tile_index];
      else
        parts_left_on_cur_tile = parts_per_pass;

      parts[cur_tile_index] -= parts_left_on_cur_tile;
    }

    // Fill up to one half of the current map entry, or flush it out if
    // it's full.
    switch (state)
    {
    case MAP_EMPTY:
      // None of this map entry's parts have been assigned yet.

      if (parts_left_on_cur_tile >= parts_per_map_entry)
      {
        // We can assign all parts of this map entry to the current tile.
        this_ent.tile_id_a = tiles[cur_tile_index];
        this_ent.tile_id_b = tiles[cur_tile_index];
        this_ent.frac = parts_per_map_entry - 1;
        parts_left_on_cur_tile -= parts_per_map_entry;
        state = MAP_FULL;
      }
      else
      {
        //
        // We can assign all remaining parts of the current tile to this
        // map entry.  Note that the rule for frac is that tile_id_b gets
        // the cacheline if the hash value is < frac; thus, we make frac
        // equal to the number of parts we assign to tile_id_b.
        //
        if (fill_a_first)
        {
          this_ent.tile_id_a = tiles[cur_tile_index];
          this_ent.frac = parts_per_map_entry - parts_left_on_cur_tile;
          parts_left_on_cur_tile = 0;
          state = MAP_HALF_FULL_A;
        }
        else
        {
          this_ent.tile_id_b = tiles[cur_tile_index];
          this_ent.frac = parts_left_on_cur_tile;
          parts_left_on_cur_tile = 0;
          state = MAP_HALF_FULL_B;
        }
      }
      break;

    case MAP_HALF_FULL_A:
      //
      // Some of this map entry's parts have been assigned to its
      // tile_id_a.
      //
      this_ent.tile_id_b = tiles[cur_tile_index];
      parts_left_on_cur_tile -= this_ent.frac;
      state = MAP_FULL;
      break;

    case MAP_HALF_FULL_B:
      //
      // Some of this map entry's parts have been assigned to its
      // tile_id_b.
      //
      this_ent.tile_id_a = tiles[cur_tile_index];
      parts_left_on_cur_tile -= parts_per_map_entry - this_ent.frac;
      state = MAP_FULL;
      break;

    case MAP_FULL:
      //
      // All of this entry's parts have been assigned to tiles, so write it
      // to the output and start another.
      //
      home_map[cur_map_entry] = this_ent.word;
      cur_map_entry++;
      this_ent.word = 0;
      state = MAP_EMPTY;
      break;
    }
  }

  assert(parts_left_on_cur_tile == 0);

}


/** Dump a home map.
 * @param home_map Home map array.
 */
void
dump_home_map(uint32_t* home_map)
{
  for (int i = 0; i < CHIP_CBOX_HOME_MAP_SIZE(); i++)
  {
    SPR_CBOX_HOME_MAP_DATA_t this_ent = { .word = home_map[i] };
        tprintf("home_map[%3d]: a (%d,%d) frac %3d b (%d,%d)\n",
                i,
                this_ent.tile_id_a >> 4, this_ent.tile_id_a & 0xF,
                this_ent.frac,
                this_ent.tile_id_b >> 4, this_ent.tile_id_b & 0xF);
  }
}


/** Install a home map in the local tile.
 * @param home_map_tiles Map to install.
 */
void
configure_tile_home_mask(tile_mask* home_map_tiles)
{
  r2c_mask(home_map_tiles, &home_map_tiles_client);
  flushable_tiles_client = msg_tiles_client;
  bis_client_tile_mask(&flushable_tiles_client, &home_map_tiles_client);
}


/** Get this client's home map mask (client-based format).
 * @param home_map_mask Pointer to the returned tile mask.
 */
void
get_client_home_mask(client_tile_mask* home_map_mask)
{
  *home_map_mask = home_map_tiles_client;
}


/** Install a home map in the local tile.
 * @param home_map Map to install.
 */
void
configure_tile_home_map(uint32_t* home_map)
{
  for (int i = 0; i < CHIP_CBOX_HOME_MAP_SIZE(); i++)
  {
    __insn_mtspr(SPR_CBOX_HOME_MAP_ADDR, i);
    __insn_mtspr(SPR_CBOX_HOME_MAP_DATA, home_map[i]);
  }
}



/** Configure the client's physical memory range on each shim.
 *  While doing this, we also fill client_fence_incoherent_pas and
 *  hv_fence_incoherent_pas with the appropriate list of PAs, terminated
 *  by an entry with the value -1.
 *
 * @param base Array holding real PAs which correspond to the lowest CPA on
 *   each shim.
 * @param size Array holding the number of bytes client is permitted to
 *   access on each shim.
 */
void
configure_client_physmem(const PA base[MAX_MSHIMS], const PA size[MAX_MSHIMS])
{
  //
  // If the user has requested contiguous client PAs, validate that we
  // can actually satisfy the request now.  The shims must all be the
  // same size, and a power of two, except for shim zero, which must be
  // no bigger than the others, and which we renumber to be the last
  // reported controller.  If this isn't the case then we can't report
  // them all as contiguous memory to the client.
  //
  int valid_shims = 0;
  PA shim_size = 0;
  int log2_shim_size = 0;
  if (config.contig_pa)
  {
    for (int i = MAX_MSHIMS - 1; i >= 0; i--)
    {
      if (size[i] == 0)
        continue;
      if (shim_size == 0)
        shim_size = size[i];
      else if ((i != 0 && size[i] != shim_size) ||
               (i == 0 && size[i] > shim_size))
        config.contig_pa = 0;
      ++valid_shims;
    }

    // Validate that shim size is a power of two.
    log2_shim_size = __builtin_ctzl(shim_size);
    if (__builtin_popcountl(shim_size) != 1)
      config.contig_pa = 0;
  }

  //
  // If we are reporting contiguous client PAs, compute the value of
  // cpa_range_shift to use, and start client shims at last_shim instead of 0.
  // Also decrease pg_shift_max to reflect the actual shim sizes.
  //
  int next_hv_shim = 0;
  int next_client_shim = 0;
  if (config.contig_pa)
  {
    cpa_range_shift = log2_shim_size;
    next_client_shim = valid_shims - 1;
    pg_shift_max = min(PG_SHIFT_MAX, log2_shim_size);
  }

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (size[i])
    {
      c_cpa_offset[next_client_shim] = base[i];
      c_cpa_limit[next_client_shim] = size[i] - 1;
      c_cpa_valid[next_client_shim] = 1;

      r_cpa_low_offset[i] = base[i] & RMASK64(MSH_MAX_SIZE_SHIFT);
      r_cpa_high_offset[i] = (CPA) next_client_shim << cpa_range_shift;
      r_cpa_limit[i] = size[i] - 1;
      r_cpa_valid[i] = 1;

      client_fence_incoherent_pas[next_client_shim] =
        base[i] + (i * CHIP_L2_LINE_SIZE());

      if (++next_client_shim == valid_shims)
        next_client_shim = 0;
    }

    if (mshims[i])
      hv_fence_incoherent_pas[next_hv_shim++] =
        mshim_bases[i] + (i * CHIP_L2_LINE_SIZE());
  }
  client_fence_incoherent_pas[next_client_shim] = -1;
  hv_fence_incoherent_pas[next_hv_shim] = -1;
}


/** Translate a CPA to a real PA.
 * @param client_paddr Client physical address.
 * @param len Number of bytes to validate.
 * @param real_paddr Pointer to returned real physical address.
 * @return Zero if all bytes in [client_paddr, client_paddr + len) are
 *          valid; nonzero otherwise.
 */
uint32_t
c2r_pa(CPA client_paddr, CPA len, PA* real_paddr)
{
  uint32_t cpa_range = client_paddr >> cpa_range_shift;
  client_paddr &= RMASK64(cpa_range_shift);

  if (cpa_range >= MAX_MSHIMS || !c_cpa_valid[cpa_range] ||
      client_paddr > c_cpa_limit[cpa_range])     // Completely out-of-range
  {
    return (1);
  }

  *real_paddr = client_paddr + c_cpa_offset[cpa_range];

  CPA end_cpa = client_paddr + len - 1;

  if (end_cpa <= c_cpa_limit[cpa_range] &&   // Completely in-range
      end_cpa >= client_paddr)
  {
    return (0);
  }
  else                              // Partially in-range
  {
    return (1);
  }
}


/** How much memory is available for the client on a given memory controller.
 * @param mshim Shim number to check.
 * @return Amount of memory on that shim, or -1 if an invalid mshim.
 */
CPA
client_mshim_size(int mshim)
{
  if (mshim >= MAX_MSHIMS || !c_cpa_valid[mshim])
    return -1ULL;

  return c_cpa_limit[mshim];
}


/** Translate a real PA to a CPA.
 * @param real_paddr Real physical address.
 * @param len Number of bytes to validate.
 * @param client_paddr Pointer to returned client physical address.
 * @return Zero if all bytes in [client_paddr, client_paddr + len) are
 *          available to this client; nonzero otherwise.
 */
uint32_t
r2c_pa(PA real_paddr, PA len, CPA* client_paddr)
{
  int mshim_idx;

  {
    mshim_idx = real_paddr >> MSH_MAX_SIZE_SHIFT;
    real_paddr &= RMASK64(MSH_MAX_SIZE_SHIFT);
  }

  CPA cpa = real_paddr - r_cpa_low_offset[mshim_idx];
  CPA end_cpa = cpa + len - 1;

  if (!r_cpa_valid[mshim_idx] || end_cpa > r_cpa_limit[mshim_idx] ||
      end_cpa < cpa)
    return (1);

  *client_paddr = cpa | r_cpa_high_offset[mshim_idx];
  return (0);
}


/** Return one of the client's physical memory ranges.
 * @param idx Range number requested.
 * @return Physical range structure.
 */
HV_PhysAddrRange
syscall_inquire_physical(int idx)
{
  HV_PhysAddrRange retval;

  SYSCALL_TRACE("inquire_physical(idx=%d)\n", idx);

  //
  // If cpa_range_shift is less than CPA_RANGE_SHIFT, we need to accumulate
  // controllers together when reporting them back to the client, since the
  // clients can assume that the index is specified at CPA_RANGE_SHIFT.
  //
  int count = 1 << (CPA_RANGE_SHIFT - cpa_range_shift);
  int shim = idx * count;
  int end_shim = min(shim + count, MAX_MSHIMS);

  if (shim < MAX_MSHIMS && c_cpa_valid[shim])
  {
    retval.start = (CPA) idx << CPA_RANGE_SHIFT;
    retval.size = 0;
    int j = shim;
    do
      retval.size += c_cpa_limit[j] + 1;
    while (++j < end_shim && c_cpa_valid[j]);
    retval.controller =
      mshim_controller[c_cpa_offset[shim] >> MSH_MAX_SIZE_SHIFT];

    return (retval);
  }

  retval.start = 0;
  retval.size = 0;
  retval.controller = 0;

  return (retval);
}


/** Return one of the client's ASID ranges.
 * @param idx Range number requested.
 * @return ASID range structure.
 */
HV_ASIDRange
syscall_inquire_asid(int idx)
{
  HV_ASIDRange retval;

  SYSCALL_TRACE("inquire_asid(idx=%d)\n", idx);

  if (idx == 0)
  {
    retval.start = 0;
    retval.size = asid_limit + 1;
  }
  else
  {
    retval.start = 0;
    retval.size = 0;
  }

  return (retval);
}


/** Return the client's topology.
 * @return Topology structure.
 */
HV_Topology
syscall_inquire_topology()
{
  HV_Topology retval;
  HV_Coord coord;
  Lotar client_lotar;

  NOISY_SYSCALL_TRACE("inquire_topology()\n");

  (void) r2c_lotar(my_lotar, &client_lotar);

  coord.x = HV_LOTAR_X(client_lotar);
  coord.y = HV_LOTAR_Y(client_lotar);
  retval.coord = coord;
  retval.width = lotar_x_limit + 1;
  retval.height = lotar_y_limit + 1;
  return (retval);
}


/** Return one of the client's tile sets.
 * @param set Which set of tiles to retrieve.
 * @param cpumask Pointer to the returned mask.
 * @param length Length of the mask.
 * @return Error code.
 */
HV_Errno
syscall_inquire_tiles(HV_InqTileSet set, char* cpumask, int length)
{
  SYSCALL_TRACE("inquire_tiles(set=%d, cpumask=%p, length=%d)\n",
                set, cpumask, length);

  client_tile_mask* target_mask;
  client_tile_mask tiles_client;

  switch (set)
  {
  case HV_INQ_TILES_AVAIL:
    target_mask = &msg_tiles_client;
    break;

  case HV_INQ_TILES_HFH_CACHE:
    target_mask = &home_map_tiles_client;
    break;

  case HV_INQ_TILES_LOTAR:
    r2c_mask(&lotar_tiles, &tiles_client);
    target_mask = &tiles_client;
    break;

  case HV_INQ_TILES_SHARED:
    r2c_mask(&config.shr_tile_mask, &tiles_client);
    and_client_tile_mask(&tiles_client, &msg_tiles_client);
    target_mask = &tiles_client;
    break;

  default:
    return HV_EINVAL;
  }

  int bytes2copy =
    (length > sizeof (*target_mask)) ? sizeof (*target_mask) : length;
  int bytes2zero = length - bytes2copy;

  ON_FAULT_RETURN_EFAULT(cpumask, length);

  memcpy(cpumask, target_mask, bytes2copy);
  memset(cpumask + bytes2copy, 0, bytes2zero);

  FAULT_END();

  return (0);
}


/** Return information about a specific memory controller, including the
 *  relative position of the controller from the given coordinate.
 * @param coord Coordinate of the tile we're measuring from.
 * @param controller Number of the memory controller we're measuring to.
 *   Note that this is the real mshim port number, not the mshims[] index.
 * @return Controller information structure.
 */
HV_MemoryControllerInfo
syscall_inquire_memory_controller(HV_Coord coord, int controller)
{
  HV_MemoryControllerInfo error_retval = { .coord = { 1 << 30, 1 << 30 } };
                                                    // "Infinite" distance

  SYSCALL_TRACE("inquire_memory_controller(coord=(%d, %d), controller = %d)\n",
    coord.x, coord.y, controller);

  int mshim_idx = -1;

  for (int i = 0; i < MAX_MSHIMS; i++)
    if (mshims[i] && mshim_controller[i] == controller)
    {
      mshim_idx = i;
      break;
    }

  //
  // Note that we no longer prevent the client from asking about controllers
  // that it doesn't actually have access to.  This is so that it can get
  // speed values for use with mcstat.
  //
  if (mshim_idx < 0)
    return (error_retval);

  int x = coord.x;
  int y = coord.y;

  if (x > lotar_x_limit || y > lotar_y_limit)
    return (error_retval);

  x += lotar_x_offset;
  y += lotar_y_offset;

  pos_t pos = { .bits.x = x, .bits.y = y};
  int portidx = mshim_portidx_from_pos(pos, mshim_idx);

  int shim_x = mshims[mshim_idx]->mdn_ports[portidx].bits.x;
  int shim_y = mshims[mshim_idx]->mdn_ports[portidx].bits.y;
  shim_x = (shim_x == 0xF) ? -1 : shim_x;
  shim_y = (shim_y == 0xF) ? -1 : shim_y;

  HV_Coord retcoord = { shim_x - x, shim_y - y };
  HV_MemoryControllerInfo retval = { retcoord, mshim_speeds[mshim_idx] };

  for (int i = 0; i < HV_MSH_MAX_DIMMS; i++)
    retval.dimm[i] = mshim_dimm_info[mshim_idx][i];

  return (retval);
}
