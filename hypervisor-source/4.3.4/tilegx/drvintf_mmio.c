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
 * Device driver interface routines for MMIO devices.
 */

#include <stdio.h>
#include <string.h>

#include <arch/chip.h>
#include <arch/mpipe.h>

#include "sys/libc/include/util.h"

#include "client_obj.h"
#include "config.h"
#include "drvintf.h"
#include "hv.h"
#include "hv/iorpc.h"
#include "mapping.h"
#include "msg.h"
#include "tsb.h"


/** Page shift for MMIO pages, used only for our internal data structures.
 *  This is really arbitrary, and was originally chosen so that an
 *  mmio_perm_t would fit into a 64-bit word; however, it's now wired into
 *  the interface definition of the permission routines, so it's not easily
 *  changeable.
 */
#define MMIO_PAGE_SHIFT 12

/** Page mask for MMIO pages. */
#define MMIO_PAGE_MASK ((1UL << MMIO_PAGE_SHIFT) - 1)

/** Structure defining a range of permissible MMIO addresses.   The
 *  bitfields are arranged so that we can search the table for the
 *  appropriate entry with simple 64-bit comparisons; if we concoct a search
 *  key mmio_perm_t using the values corresponding to the requested address,
 *  then the table entry which might possibly permit that access is the
 *  largest one which is less than or equal to the search key.  (We still
 *  have to take the end value into account once that entry has been found.)
 */
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /** End physical page frame number (i.e., first invalid PFN) */
    PA end_ppfn: CHIP_PA_WIDTH() - MMIO_PAGE_SHIFT;

    /** Start physical page frame number (i.e., first valid PFN) */
    PA start_ppfn: CHIP_PA_WIDTH() - MMIO_PAGE_SHIFT;

    /** Shim Y coordinate */
    unsigned int shim_y:4;

    /** Shim X coordinate */
    unsigned int shim_x:4;
#else
    /** Shim X coordinate */
    unsigned int shim_x:4;

    /** Shim Y coordinate */
    unsigned int shim_y:4;

    /** Start physical page frame number (i.e., first valid PFN) */
    PA start_ppfn: CHIP_PA_WIDTH() - MMIO_PAGE_SHIFT;

    /** End physical page frame number (i.e., first invalid PFN) */
    PA end_ppfn: CHIP_PA_WIDTH() - MMIO_PAGE_SHIFT;
#endif
  };
  /** Packed version for use by search code */
  unsigned long word;
}
mmio_perm_t;

/* Table of MMIO address ranges permitted on this tile.  To make searching
 * easier (see below), the last entry in this table is a sentinel, with all
 * bits 1.  This cannot match any valid mmio_perm_t, since (0xF,0xF) is an
 * impossible location for an I/O shim. */
static mmio_perm_t* mmio_perm_table = NULL;

/* Total number of entries allocated in mmio_perm_table[]. */
static int mmio_perm_table_size = 0;

/* Number of valid entries in mmio_perm_table[]. */
static int num_mmio_perms = 0;


/* Find the entry in the MMIO table which could potentially cover a
 * specific entry; this is also the spot after which a new entry
 * would be inserted.
 * @param key_entry Entry to look for.
 * @return Index of the largest entry in the mmio_perm_table which is
 *   less than or equal to the search key.  If there are no such entries,
 *   -1 is returned.  Note that this return value ensures that if we search
 *   for a value which we intend to insert into the table, the insertion
 *   point for that new value is always one greater than the return value,
 *   even when all of the table entries are larger than the searched-for key.
 */
static int
find_mmio_entry(mmio_perm_t key_entry)
{
  //
  // The search implemented here is a standard binary search, but instead
  // of looking for one entry in the table which matches the presented key,
  // we're really looking for two sequential entries, the first of which is
  // less than or equal to the presented key, and the second of which is
  // greater than the presented key.  This is why we start out the table
  // with an all 1's entry; it means that once there are any entries
  // in the table, we can search from the first entry to the penultimate
  // entry, and we will always be able to compare against the current
  // search point and the entry following.
  //

  //
  // If there's only 1 entry in the table, it's not a real entry, so
  // return failure.
  //
  if (num_mmio_perms < 2)
    return (-1);

  //
  // We only want to compare on the shim coordinates and starting PPFN;
  // the end PPFN is immaterial when deciding which entry could be a
  // match for the key.
  //
  key_entry.end_ppfn = (1ULL << (CHIP_PA_WIDTH() - MMIO_PAGE_SHIFT)) - 1;

  //
  // Note that we subtract 1 from the initial value of high_idx so that
  // we don't consider the final sentinel entry for matching.
  //
  int low_idx = 0;
  int high_idx = num_mmio_perms - 1;

  while (low_idx < high_idx)
  {
    int mid = (low_idx + high_idx) / 2;

    if (key_entry.word < mmio_perm_table[mid].word)
      high_idx = mid;
    else if (key_entry.word >= mmio_perm_table[mid + 1].word)
      low_idx = mid + 1;
    else
      //
      // The key is >= the entry at mid, and < the entry at mid + 1,
      // so the entry at mid is our match.
      //
      return (mid);
  }

  return (-1);
}


#if 0

/**
 * This routine may be useful in debugging the MMIO table code below.
 */
static void
dump_mmio()
{
  printf("    mmio ptr %p, %d used, %d avail\n",
         mmio_perm_table, num_mmio_perms, mmio_perm_table_size);

  for (int i = 0; i < num_mmio_perms; i++)
    printf("    mmio[%3d] = (%x,%x) %10lx-%10lx\n",
           i,
           mmio_perm_table[i].shim_x,
           mmio_perm_table[i].shim_y,
           (PA) mmio_perm_table[i].start_ppfn << MMIO_PAGE_SHIFT,
           ((PA) mmio_perm_table[i].end_ppfn << MMIO_PAGE_SHIFT) - 1);
}

#endif


/** Make a range of memory-mapped I/O addresses available to this tile.
 *  The region must not currently be partially or wholly available
 *  on this tile.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @return Zero if the region is successfully made available; HV_EINVAL if
 *   the region, shim, or client is incorrectly specified (e.g., start/len
 *   are unaligned); HV_EBUSY if the region is already partially or wholly 
 *   available to the specified client; HV_ENOMEM if memory could not
 *   be allocated or reallocated for the MMIO permissions table.
 */
int
permit_mmio_access(pos_t shimaddr, PA start, PA len)
{
  // Last byte of the range, plus one
  PA endp1 = start + len;

  //
  // Make sure we have space in our table.
  //
  if (num_mmio_perms >= mmio_perm_table_size)
  {
    if (mmio_perm_table_size == 0)
      mmio_perm_table_size = 128;
    else
      mmio_perm_table_size *= 2;

    mmio_perm_t* new_table =
      local_alloc(mmio_perm_table_size * sizeof (*mmio_perm_table), 0);
    if (!new_table)
      return (HV_ENOMEM);

    memcpy(new_table, mmio_perm_table,
           num_mmio_perms * sizeof (*mmio_perm_table));

    //
    // Note: we intentionally leak the old table, since there is no
    // local_free().  We don't expect this will happen often enough
    // for it to be a problem.
    //
    mmio_perm_table = new_table;

    if (num_mmio_perms == 0)
    {
      mmio_perm_table[0] = (mmio_perm_t) { .word = ~0UL };
      num_mmio_perms = 1;
    }
  }

  //
  // Figure out where our new entry will go in the table.
  //
  mmio_perm_t new_entry = { .word = 0 };
  new_entry.shim_x = shimaddr.bits.x;
  new_entry.shim_y = shimaddr.bits.y;
  new_entry.end_ppfn = endp1 >> MMIO_PAGE_SHIFT;
  new_entry.start_ppfn = start >> MMIO_PAGE_SHIFT;

  int match_entry_idx = find_mmio_entry(new_entry);

  if (match_entry_idx >= 0)
  {
    //
    // There's an entry that might match; make sure it doesn't.
    //
    mmio_perm_t match_entry = mmio_perm_table[match_entry_idx];
    if (new_entry.shim_x == match_entry.shim_x &&
        new_entry.shim_y == match_entry.shim_y &&
        new_entry.start_ppfn < match_entry.end_ppfn)
      return (HV_EBUSY);
  }

  int new_entry_idx = match_entry_idx + 1;

  //
  // Even when there isn't a matching entry, this entry might overlap the
  // one at the insertion point, so make sure it doesn't.  Note that the
  // extra sentinel at the end of the table (see above) means there always
  // is an entry at new_entry_idx.
  //
  mmio_perm_t prev_entry = mmio_perm_table[new_entry_idx];

  if (new_entry.shim_x == prev_entry.shim_x &&
      new_entry.shim_y == prev_entry.shim_y &&
      new_entry.end_ppfn > prev_entry.start_ppfn)
    return (HV_EBUSY);

  memmove(mmio_perm_table + new_entry_idx + 1,
          mmio_perm_table + new_entry_idx,
          (num_mmio_perms - new_entry_idx) * sizeof (*mmio_perm_table));

  mmio_perm_table[match_entry_idx + 1] = new_entry;

  num_mmio_perms++;

  return (0);
}


/** Remove a range of memory-mapped I/O addresses from the set of addresses
 *  available to this tile.  The region must currently be wholly available to
 *  this tile.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @return Zero if successfully mapped; HV_EINVAL if the region, shim, or
 *   client is incorrectly specified (e.g., start/len are unaligned);
 *   HV_ENOTREADY if the region was not wholly available to the specified
 *   client.
 */
int
deny_mmio_access(pos_t shimaddr, PA start, PA len)
{
  // Last byte of the range, plus one
  PA endp1 = start + len;

  if (!mmio_perm_table)
    return (HV_ENOTREADY);

  //
  // Figure out where our entry is in the table.
  //
  mmio_perm_t key_entry = { .word = 0 };
  key_entry.shim_x = shimaddr.bits.x;
  key_entry.shim_y = shimaddr.bits.y;
  key_entry.end_ppfn = endp1 >> MMIO_PAGE_SHIFT;
  key_entry.start_ppfn = start >> MMIO_PAGE_SHIFT;

  //
  // Find the entry which is either the first one we want to remove, or the
  // one right before it.
  //
  int first_entry_idx = find_mmio_entry(key_entry);

  mmio_perm_t first_entry;

  if (first_entry_idx >= 0)
    first_entry = mmio_perm_table[first_entry_idx];

  //
  // The entry we found might not match our coordinates, or may end before
  // our range; if so, bump the index and make sure the following entry at
  // least has good coordinates.  Note that the increment might leave us
  // pointing at the last sentinel entry, but in that case the second x,y
  // check should fail.
  //
  if (first_entry_idx < 0 ||
      key_entry.shim_x != first_entry.shim_x ||
      key_entry.shim_y != first_entry.shim_y ||
      key_entry.start_ppfn >= first_entry.end_ppfn)
  {
    first_entry = mmio_perm_table[++first_entry_idx];

    if (key_entry.shim_x != first_entry.shim_x ||
        key_entry.shim_y != first_entry.shim_y)
      return (HV_ENOTREADY);
  }

  //
  // If we don't at least completely cover this one entry, fail.
  //
  if (key_entry.start_ppfn > first_entry.start_ppfn ||
      key_entry.end_ppfn < first_entry.end_ppfn)
    return (HV_ENOTREADY);

  //
  // Okay, we're going to be removing at least one entry.  Now find the
  // last entry we might want to remove; it might be the same as the
  // first entry.
  //
  int last_entry_idx;
  mmio_perm_t last_entry = { .word = 0 };

  for (last_entry_idx = first_entry_idx + 1;
       last_entry_idx < num_mmio_perms; last_entry_idx++)
  {
    last_entry = mmio_perm_table[last_entry_idx];

    //
    // We know there's at least one entry which will meet our criteria.  We
    // keep going until we hit one which doesn't, then we back the pointer
    // up one slot.
    //
    if (key_entry.shim_x != last_entry.shim_x ||
        key_entry.shim_y != last_entry.shim_y ||
        key_entry.end_ppfn <= last_entry.start_ppfn)
    {
      last_entry = mmio_perm_table[--last_entry_idx];
      break;
    }
  }

  //
  // If we only partially overlap the last entry, fail.
  //
  if (key_entry.end_ppfn < last_entry.end_ppfn)
    return (HV_ENOTREADY);

  int num_deny = last_entry_idx - first_entry_idx + 1;

  memmove(mmio_perm_table + first_entry_idx,
          mmio_perm_table + first_entry_idx + num_deny,
          (num_mmio_perms - first_entry_idx - num_deny) *
          sizeof (*mmio_perm_table));

  num_mmio_perms -= num_deny;

  return (0);
}


/** Test whether a particular access to a particular region of MMIO address
 *  space is permitted on this tile.
 * @param shimaddr Coordinates of the I/O shim to which access is
 *   requested.
 * @param start First valid byte of the MMIO range to which access is
 *   requested.
 * @param len Number of bytes in the range to which access is requested.
 * @return Nonzero if access is permitted, zero if it is not permitted.
 */
int
mmio_access_ok(pos_t shimaddr, PA start, PA len)
{
  //
  // See if our entry is in the table.
  //
  mmio_perm_t key_entry = { .word = 0 };
  key_entry.shim_x = shimaddr.bits.x;
  key_entry.shim_y = shimaddr.bits.y;
  key_entry.start_ppfn = start >> MMIO_PAGE_SHIFT;
  key_entry.end_ppfn = (start + len + MMIO_PAGE_MASK) >> MMIO_PAGE_SHIFT;

  int match_entry_idx = find_mmio_entry(key_entry);

  if (match_entry_idx >= 0)
  {
    //
    // There's an entry that might match; see if it does.
    //
    mmio_perm_t match_entry = mmio_perm_table[match_entry_idx];
    if (key_entry.shim_x == match_entry.shim_x &&
        key_entry.shim_y == match_entry.shim_y &&
        key_entry.end_ppfn <= match_entry.end_ppfn)
      return (1);
  }

  return (0);
}


/** Validate arguments to a permit/deny function.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @param clientno Client number to which the region will be made
 *   available.
 * @return Zero if the arguments are valid; HV_EINVAL if the region, shim,
 *   or client are incorrectly specified (e.g., start/len are unaligned).
 */
static int
validate_mmio_access_args(pos_t shimaddr, PA start, PA len, int clientno)
{
  // Last byte of the range, plus one
  PA endp1 = start + len;

  //
  // Make sure addresses are properly aligned and in-range
  //
  if ((start & MMIO_PAGE_MASK) || (endp1 & MMIO_PAGE_MASK) || start >= endp1 ||
      endp1 > ((PA) 1 << CHIP_PA_WIDTH()))
    return (HV_EINVAL);

  //
  // Make sure that the shim coordinates are on the edge of the chip.
  // We allow (0,0) as a special case; it means "nearest IPI shim".
  //
  if ((shimaddr.bits.x != 0 || shimaddr.bits.y != 0) &&
      !(((shimaddr.bits.x == 0xF || shimaddr.bits.x == grid_lrhc.bits.x + 1) &&
         shimaddr.bits.y <= grid_lrhc.bits.y) ||
        ((shimaddr.bits.y == 0xF || shimaddr.bits.y == grid_lrhc.bits.y + 1) &&
         shimaddr.bits.x <= grid_lrhc.bits.x)))
    return (HV_EINVAL);

  if (clientno < 0 || clientno >= config.nclients ||
      (config.clients[clientno].flags & CLIENT_BME))
    return (HV_EINVAL);

  return (0);
}


/** Broadcast a permit/deny call to all tiles in a client, other than the
 *  current tile.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @param clientno Client number to which the region will be made
 *   available.
 * @param is_deny Nonzero if this is a deny request; zero if it is an
 *   allow request.
 * @return The first nonzero nonzero error code reported by any of the
 *   remote tiles, or zero if there were no errors.
 */
static int
broadcast_mmio_access_request(pos_t shimaddr, PA start, PA len, int clientno,
                              int is_deny)
{
  //
  // FIXME: this should arguably be a generic "broadcast message X to a set
  // of tiles" routine.  In fact, there are a number of other places in the
  // hypervisor where we do send messages to multiple tiles.  However, in
  // those other places, we send different messages to different tiles, and
  // that leads to a really clunky interface for a broadcast routine.  (The
  // caller ends up doing the loop that walks through all of the tiles
  // anyway, so why not just have it send the messages too?)  If we end up
  // with another place where we want to do a broadcast of the same message
  // to many tiles, with the same type of error code coalescing, we might
  // consider making this more generic.
  //

  // Set of tiles we'll be broadcasting to.  This shrinks as we build
  // the tile_info[] table.
  tile_mask tiles = config.clients[clientno].tiles;

  // We won't send a message to ourselves; our caller already did the
  // operation locally if necessary.
  del_tile_mask(&tiles, my_pos);

  // Number of tiles we'll be broadcasting to.
  int ntiles = pcnt_tile_mask(&tiles);

  //
  // First create our target list of tiles.
  //
  struct
  {
    /** Destination tile */
    pos_t tile;
    /** Channel on which we expect a reply from this tile */
    uint32_t reply_channel;
    /** Return value we got from this tile */
    struct hv_msg_mmio_access_reply retval;
  }
  tile_info[ntiles];

  memset(tile_info, 0, sizeof (tile_info));

  for (int i = 0; i < ntiles; i++)
  {
    ffs_tile_mask(&tiles, &tile_info[i].tile);
    del_tile_mask(&tiles, tile_info[i].tile);
  }

  //
  // Construct the message we'll send.
  //
  struct hv_msg_mmio_access msg =
  {
    .shimaddr = shimaddr,
    .start = start,
    .len = len,
  };
  uint32_t msg_type = (is_deny) ? HV_TAG_DENY_MMIO_ACC :
                                  HV_TAG_PERMIT_MMIO_ACC;

  //
  // Go through our list and send the message to everyone.
  //
  for (int i = 0; i < ntiles; i++)
  {
    send_var(tile_info[i].tile, msg_type, &msg, sizeof (msg),
             NULL, 0, &tile_info[i].reply_channel, &tile_info[i].retval,
             sizeof (tile_info[i].retval), 0);
  }

  //
  // Now go through again and wait for each reply, and coalesce any
  // errors we get.
  //
  int error = 0;

  for (int i = 0; i < ntiles; i++)
  {
    size_t rcv_replylen;

    uint32_t rcv_type = getreply(tile_info[i].reply_channel, &rcv_replylen, 0);

    if (rcv_type != msg_type)
      panic("message type mismatch: sent %#x, got %#x", msg_type,
            rcv_type);

    if (rcv_replylen != sizeof (tile_info[i].retval))
      panic("message length error for HV_TAG_xxx_MMIO_ACC reply");

    if (!error && tile_info[i].retval.retval)
      error = tile_info[i].retval.retval;
  }

  return (error);
}


int
drv_permit_mmio_access(pos_t shimaddr, PA start, PA len, int clientno)
{
  int error = validate_mmio_access_args(shimaddr, start, len, clientno);
  if (error)
    return (error);

  //
  // We run the request locally first, on the theory that it'll probably
  // succeed or fail equally on all tiles, and if it's going to fail we
  // might as well know before we go sending a bunch of messages.
  //
  if (clientno == my_client)
  {
    error = permit_mmio_access(shimaddr, start, len);
    if (error)
      return (error);
  }

  error = broadcast_mmio_access_request(shimaddr, start, len, clientno, 0);

  //
  // If the request succeeded locally, but failed on a remote tile,
  // something really bad is going on; panic.
  //
  if (error && clientno == my_client)
    panic("remote tile rejected mmio permit request: "
          "shim (%d,%d) start %#llX len %#llX client %d\n",
          UXY(shimaddr), start, len, clientno);

  return (error);
}


int
drv_deny_mmio_access(pos_t shimaddr, PA start, PA len, int clientno)
{
  int error = validate_mmio_access_args(shimaddr, start, len, clientno);
  if (error)
    return (error);

  //
  // We run the request locally first, on the theory that it'll probably
  // succeed or fail equally on all tiles, and if it's going to fail we
  // might as well know before we go sending a bunch of messages.
  //
  if (clientno == my_client)
  {
    error = deny_mmio_access(shimaddr, start, len);
    if (error)
      return (error);
  }

  error = broadcast_mmio_access_request(shimaddr, start, len, clientno, 1);

  //
  // If the request succeeded locally, but failed on a remote tile,
  // something really bad is going on; panic.
  //
  if (error && clientno == my_client)
    panic("remote tile rejected mmio deny request: "
          "shim (%d,%d) start %#llX len %#llX client %d\n",
          UXY(shimaddr), start, len, clientno);

  return (error);
}


int
drv_map_cpa_space_to_iotlb(pos_t shim_pos, unsigned int asid,
                           HV_PTE pte, PA tlb_entry_offset,
                           unsigned int flags)
{
  struct client_config* client = &config.clients[my_client];

  PA iotlb_pa[16];
  PA iotlb_cpa[16];
  PA iotlb_len[16];

  //
  // Compute start PAs and lengths for up to 16 IOTLB entries.  If we can't
  // cover the client's memory with those, then we fail.
  //
  int k = 0;

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    PA pa = client->mem_base[i];
    PA cpa;
    PA len = client->mem_len[i];

    //
    // We're assuming here that the PA range on each mshim is mapped into
    // contiguous CPA space.  It would be a lot harder to do otherwise, so
    // that seems pretty safe.
    //
    r2c_pa(pa, 1, &cpa);

    while (len != 0)
    {
      //
      // If we're out of IOTLB entries, return.
      //
      if (k == 16)
        return GXIO_ERR_CLIENT_MEMORY;

      //
      // Figure out the largest power-of-two number of bytes that can start
      // at the current PA, be properly aligned, and not run off the end of
      // the region.
      //
      PA n = 2048;
      while (n * 2 <= len && (pa & (n * 2 - 1)) == 0 &&
             (cpa & (n * 2 - 1)) == 0)
        n *= 2;

      //
      // If the space needing to be covered is less than our smallest page
      // size (4K), we can't cover it; return.
      //
      if (n < 4096)
        return GXIO_ERR_CLIENT_MEMORY;

      iotlb_pa[k] = pa;
      iotlb_cpa[k] = cpa;
      iotlb_len[k] = n;
      k++;

      pa += n;
      cpa += n;
      len -= n;
    }
  }

  //
  // If there wasn't anything to map, return.
  //
  if (k == 0)
    return GXIO_ERR_CLIENT_MEMORY;

  // ISSUE: Share this extraction code with "drv_translate_iorpc()"?

  //
  // Get mapping attributes out of the PTE which was passed to us.
  //
  struct iorpc_mem_attr mem_attr = { 0 };
  SPR_AAR_t aar;

  switch (hv_pte_get_mode(pte))
  {
  case HV_PTE_MODE_CACHE_TILE_L3:
    mem_attr.hfh = 0;
    break;
  case HV_PTE_MODE_CACHE_HASH_L3:
    mem_attr.hfh = 1;
    break;
  default:
    return GXIO_ERR_COHERENCE;
  }

  //
  // Use pte2aer() to make sure we translate the client lotar correctly.
  //
  if (pte2aar(pte, &aar.word, 0))
    return HV_EFAULT;
  mem_attr.lotar_x = aar.location_x_or_page_mask;
  mem_attr.lotar_y = aar.location_y_or_page_offset;

  mem_attr.io_pin = ((flags & IORPC_MEM_BUFFER_FLAG_IO_PIN) != 0);
  mem_attr.nt_hint = ((flags & IORPC_MEM_BUFFER_FLAG_NT_HINT) != 0);

  //
  // Stuff the actual entries into the IOTLB.
  //
  for (int i = 0; i < k; i++)
  {
    PA pa = iotlb_pa[i];
    PA cpa = iotlb_cpa[i];
    PA len = iotlb_len[i];

    uint ps = 0;
    while ((1UL << (ps + 12)) < len)
      ps++;

    //
    // Note that we use the mPIPE versions of these registers, even though
    // this routine is used for mPIPE, USB, TRIO and MiCA.  MiCA has a
    // different IOTLB layout than the rest of the shims, and we account
    // for that with the offset argument, but the individual registers have
    // the same format. mPIPE has more TLB entries than the other devices so
    // its TABLE_t is a superset of all the devices'.
    //
    MPIPE_TLB_TABLE_t table = {{
        .is_attr = 0,
        .entry = i,
        .asid = asid,
      }};

    MPIPE_TLB_ENTRY_ADDR_t addr = {{
        .pfn = pa >> 12,
        .vpn = cpa >> 12,
      }};
    cfg_wr(shim_pos.word, 0, tlb_entry_offset + table.word, addr.word);

    table.is_attr = 1;

    MPIPE_TLB_ENTRY_ATTR_t attr = {{
        .vld = 1,
        .ps = ps,
        .home_mapping = !mem_attr.hfh,
        .pin = mem_attr.io_pin,
        .nt_hint = mem_attr.nt_hint,
        .loc_y_or_offset = mem_attr.lotar_y,
        .loc_x_or_mask = mem_attr.lotar_x,
        //.lru = UNUSED
      }};
    cfg_wr(shim_pos.word, 0, tlb_entry_offset + table.word,
           attr.word);
  }

  return 0;
}
