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
 * Hypervisor level-1 boot program.
 */

#include <alloca.h>
#include <stdint.h>
#include <string.h>
#include <util.h>

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/idn.h>
#include <arch/interrupts.h>
#include <arch/ipi.h>
#include <arch/mica_comp.h>
#include <arch/mica_crypto.h>
#include <arch/mpipe.h>
#include <arch/msh.h>
#include <arch/rsh.h>
#include <arch/sim.h>
#include <arch/trio.h>
#include <arch/spr.h>
#include <arch/uart.h>
#include <arch/udn.h>

#include <hvbme/jtag.h>

#include "bits.h"
#include "board_info.h"
#include "boot_error.h"
#include "boot_params.h"
#include "cons_rshim.h"
#include "cfg.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "hvgdb.h"
#include "i2c_acc.h"
#include "idn.h"
#include "misc.h"
#include "mshim_acc.h"
#include "mshim_cfg.h"
#include "msgtag.h"
#include "page.h"
#include "param.h"
#include "post/post_ram.h"
#include "srom_acc.h"
#include "sromboot.h"
#include "types.h"
#include "uart.h"


#ifndef POST_QUERY_DEFAULT
/** For now, thorough POST is the default on Gx. */
#define POST_QUERY_DEFAULT POST_THOROUGH
#endif

//
// Tile position data.
//
pos_t feedaddr;   /**< Coordinates of tile which is feeding us; if the high
                   *   bit of the word is set, the I/O shim is feeding (i.e.,
                   *   this tile is the master). */
pos_t myaddr;     /**< Coordinates of this tile. */
pos_t masteraddr; /**< Coordinates of boot master tile. */
pos_t ulhc;       /**< Coordinates of tile area's upper left hand corner. */
pos_t lrhc;       /**< Coordinates of tile area's lower right hand corner. */
pos_t grid_ulhc;  /**< Coordinates of grid's upper left hand corner. */
pos_t grid_lrhc;  /**< Coordinates of grid's lower right hand corner. */
pos_t rshimaddr;  /**< Coordinates of the rshim. */
pos_t gpioaddr;   /**< Coordinates of the GPIO shim. */

/** Extra configuration data. */
union boot_params boot_params = BOOTPARAMS_INIT;

//
// Other boot state data.
//

/** Are we the boot master tile? */
int master_boot;
/** Saved SROM boot stream position; nonzero if we're booting from SROM via
 *  the SROM booter. */
uint64_t srom_addr;
/** SROM device cookie. */
int srom_dev;
/** Should we try to set up memory striping? */
uint8_t striping_requested;
/** Should we try to set up memory striping, even if we'd lose memory
 *  by doing so? */
uint8_t striping_forced;

//
// Data about our memory placement, so we know what to copy to successors.
//
extern unsigned long _lh_start[];  /**< Entry point of the L 0.5 booter. */
extern unsigned long _lh_end[];    /**< End of the L 0.5 booter. */
extern unsigned long _start[];     /**< Entry point of this program. */
extern unsigned long _stext[];     /**< Start of our text segment. */
extern unsigned long _sbss[];      /**< Start of our BSS segment. */
extern unsigned long _ebss[];      /**< End of our BSS segment. */

PA boot_shim_size;       /**< The size in bytes of the mshim we're using for
                              hypervisor code and data. */
PA boot_shim_base;       /**< The base offset in bytes of the mshim we're
                              using for hypervisor code and data. */

#if !defined(ROUTE_MDN_X_Y)
/** TILE_COORD SPR bits to set Y-X routing for MDN, TDN, and VDN */
#define TILE_COORD_STATE 0x7
#else
/** Reserved for extra TILE_COORD state as needed */
#define TILE_COORD_STATE 0
#endif

/** Core frequency used by drv_udelay(), etc. */
static long core_freq = REFCLK;

/** Are we a Gx72 chip? */
int is_gx72;

static inline uint32_t
crc32_64(uint32_t crc, uint64_t input)
{
  uint32_t t_crc = __insn_crc32_32(crc, (uint32_t) input);
  return __insn_crc32_32(t_crc, (uint32_t) (input >> 32));
}



















































/** Main boot routine - runs on all tiles.  This code figures out which of
 *  its neighbors receive the boot program, sends it to them, then calls
 *  either the boot master or boot slave code.
 * @param boot_cycle The cycle count register at the very start of the
 *        boot process (from _start).
 * @param srom_flags SROM boot flags.  Zero if we aren't booting from SROM.
 * @param lh_flags Flags from the L0.5 booter.
 */
void
boot(uint64_t boot_cycle, int srom_flags, int lh_flags)
{
  uint32_t board_flags = 0;

  //
  // Record where we are, and where the rshim is; we'll need these
  // below.
  //
  myaddr.word = __insn_mfspr(SPR_TILE_COORD);
  rshimaddr.word = __insn_mfspr(SPR_RSHIM_COORD);





  //
  // Read in booter configuration data.
  //
  uint32_t input_crc;
  uint32_t crc = ~0;
  const int cfg_space = sizeof (boot_params) / sizeof (uint64_t);

  if (srom_flags & SROMBOOT_SROMBOOT)
  {
    uint64_t tmp[5];

    srom_dev = early_srom_get_dev(rshimaddr.word);

    early_srom_read(rshimaddr.word, srom_dev, &tmp, 4 * 2);

    feedaddr.word = tmp[0];
    crc = crc32_64(crc, feedaddr.word);
    masteraddr.word = tmp[1];
    crc = crc32_64(crc, masteraddr.word);
    ulhc.word = tmp[2];
    crc = crc32_64(crc, ulhc.word);
    lrhc.word = tmp[3];
    crc = crc32_64(crc, lrhc.word);

    int xwords = feedaddr.bits.len >> 1;
    if (xwords)
    {
      uint64_t wds[xwords];
      early_srom_read(rshimaddr.word, srom_dev, wds, xwords * 2);

      for (int i = 0; i < xwords; i++)
      {
        if (i < cfg_space)
          boot_params.words[i] = wds[i];
        crc = crc32_64(crc, wds[i]);
      }
    }

    early_srom_read(rshimaddr.word, srom_dev, &tmp[4], 1 * 2);
    input_crc = tmp[4];

    srom_addr = srom_get_addr(rshimaddr.word, srom_dev);

    board_flags |= BOARD_BOOTED_SROM | BOARD_SROM_HVFS;

    if (srom_flags & SROMBOOT_BADCRC_REBOOT)
      board_flags |= BOARD_BADCRC_REBOOT;
  }
  else
  {

    feedaddr.word = udn0_receive();
    crc = crc32_64(crc, feedaddr.word);
    masteraddr.word = udn0_receive();
    crc = crc32_64(crc, masteraddr.word);
    ulhc.word = udn0_receive();
    crc = crc32_64(crc, ulhc.word);
    lrhc.word = udn0_receive();
    crc = crc32_64(crc, lrhc.word);

    int xwords = feedaddr.bits.len >> 1;
    if (xwords)
    {
      for (int i = 0; i < xwords; i++)
      {
        uint64_t wd = udn0_receive();
        if (i < cfg_space)
          boot_params.words[i] = wd;
        crc = crc32_64(crc, wd);
      }
    }

    input_crc = udn0_receive();

































  }

  //
  // Verify booter configuration data.
  //
  if (~crc != input_crc)
  {
    if (board_flags & BOARD_BADCRC_REBOOT)
      boot_reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
    else
      boot_error(BOOT_ERR_CONFIG_BAD_CRC);
  }

  master_boot = ((feedaddr.word & 0x80000000) != 0);






  striping_requested = ((feedaddr.word & 0x60000000) != 0);
  striping_forced = ((feedaddr.word & 0x20000000) != 0);

  //
  // Initialize the UART in case we need to do any output.
  //
  if (master_boot)
    console_init(REFCLK, NULL, &board_flags);

  //
  // Get tile grid size.
  //
  RSH_FABRIC_DIM_t grid_size =
    { .word = cfg_rd(rshimaddr.word, 0, RSH_FABRIC_DIM) };
  grid_ulhc.bits.x = 0;
  grid_ulhc.bits.y = 0;
  grid_lrhc.bits.x = grid_size.dim_x - 1;
  grid_lrhc.bits.y = grid_size.dim_y - 1;

  //
  // See if we're a Gx72, since there are a couple minor differences we
  // need to account for.
  //
  RSH_REV_ID_t rev_id = 
    { .word = cfg_rd(rshimaddr.word, 0, RSH_REV_ID) };
  is_gx72 =
    (rev_id.chip_rev_id & 0xF0) == RSH_REV_ID__CHIP_REV_ID_VAL_TILEGX72;

  //
  // The master ignores the input tile ULHC and LRHC and instead gets
  // them from the hardware.
  //
  if (master_boot)
  {
    masteraddr = myaddr;
    ulhc = grid_ulhc;
    lrhc = grid_lrhc;

#ifdef TLR_SKU
    //
    // If we're simulating a smaller chip, disable I/O devices and tiles as
    // needed.
    //

#if TLR_SKU == 0x3680

    // Default, do nothing
    const int force_dim_x = 6;
    const int force_dim_y = 6;

#elif TLR_SKU == 0x950

    const int force_dim_x = 3;
    const int force_dim_y = 3;
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE0, 0x07e40456bfcff20fUL);
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE1, 0x00000000000000ffUL);

#elif TLR_SKU == 0x980

    const int force_dim_x = 3;
    const int force_dim_y = 3;
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE0, 0x07e404103f8fc000UL);
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE1, 0x00000000000000f0UL);

#elif TLR_SKU == 0x1650

    const int force_dim_x = 4;
    const int force_dim_y = 4;
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE0, 0x07e40056bf8fc20fUL);
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE1, 0x00000000000000ffUL);

#elif TLR_SKU == 0x1680

    const int force_dim_x = 4;
    const int force_dim_y = 4;
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE0, 0x07e000463e08400fUL);
    cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE1, 0x00000000000000f0UL);

#else

#error "Unrecognized value for TLR_SKU"

#endif

    if (is_gx72 ? (force_dim_x < 8 || force_dim_y < 9)
                : (force_dim_x < 6 || force_dim_y < 6))
      for (int i = 0; i < grid_size.dim_x; i++)
        cfg_wr(rshimaddr.word, 0,
               RSH_TILE_COL_DISABLE__FIRST_WORD + i * sizeof (uint_reg_t),
               (i >= force_dim_y) ? ~0ULL : ~0ULL << force_dim_x);

#endif // TLR_SKU

    //
    // We're going to read the rshim registers and see if any tiles are
    // disabled; if so, reduce the LRHC appropriately.  This means that we
    // only support a rectangular grid of enabled tiles; although the
    // hardware disable bits could describe more baroque configurations, we
    // have no plans to create any such chips, and making this restriction
    // makes things easier in the hypervisor.
    //
    for (int i = 0; i <= grid_lrhc.bits.x; i++)
    {
      //
      // Walk through the columns.  If we hit a column that's totally
      // disabled, set the LRHC X coordinate to the previous column, and
      // stop walking, since we can't use any other tiles.  Otherwise, clip
      // the LRHC Y coordinate based on the number of tiles in this column,
      // and keep going.
      //
      int thiscol_dis = cfg_rd(rshimaddr.word, 0,
                               RSH_TILE_COL_DISABLE__FIRST_WORD +
                               i * sizeof (uint_reg_t));
      thiscol_dis |= ~RMASK(grid_lrhc.bits.y + 1);
      int tiles = __builtin_ctz(thiscol_dis);

      if (tiles == 0)
      {
        lrhc.bits.x = i - 1;
        break;
      }
      lrhc.bits.y = min((int) lrhc.bits.y, tiles - 1);
    }
  }


  //
  // Now use the input data to figure out which tiles, if any, are the next
  // in our boot tree.
  //
  int num_output_tiles = 0;
  pos_t output_tiles[3];

  //
  // First figure out what side of the tile the boot stream is coming
  // from.
  //
  enum input_dir { UNKNOWN, NORTH, SOUTH, EAST, WEST } direction = UNKNOWN;
  if (master_boot)
  {
    //
    // Note that in the master boot case, if we're on a corner tile, we
    // can't actually tell which direction we're being driven from, but
    // it doesn't matter, so we just pick one.
    //
    if (myaddr.bits.x == ulhc.bits.x)
      direction = WEST;
    else if (myaddr.bits.x == lrhc.bits.x)
      direction = EAST;
    else if (myaddr.bits.y == ulhc.bits.y)
      direction = NORTH;
    else if (myaddr.bits.y == lrhc.bits.y)
      direction = SOUTH;
  }
  else
  {
    if (myaddr.bits.y != feedaddr.bits.y)
    {
      if (myaddr.bits.y > feedaddr.bits.y)
        direction = NORTH;
      else
        direction = SOUTH;
    }
    else
    {
      if (myaddr.bits.x > feedaddr.bits.x)
        direction = WEST;
      else
        direction = EAST;
    }
  }

  //
  // The algorithm used here is as follows:
  //
  // - If we're the boot tile, we boot all tiles adjacent to ourselves, other
  //   than the tile which booted us.
  //
  // - Otherwise, if the tile which booted us was above or below us, we boot
  //   all tiles adjacent to ourselves, other than the tile which booted us.
  //
  // - Otherwise, the tile which booted us was to the left or the right of
  //   us; we boot the tile on the other side of us from that tile, but don't
  //   boot tiles above or below us.
  //
  // Some example boot paths:
  //
  // 1. Boot tile is at (1, 4) (middle of west side of chip).
  //
  // 3>4>5>6>7>8>9>A
  // ^
  // 2>3>4>5>6>7>8>9
  // ^
  // 1>2>3>4>5>6>7>8
  // ^
  // 0>1>2>3>4>5>6>7
  // v
  // 1>2>3>4>5>6>7>8
  // v
  // 2>3>4>5>6>7>8>9
  // v
  // 3>4>5>6>7>8>9>A
  // v
  // 4>5>6>7>8>9>A>B
  //
  // 2. Boot tile is at (8, 0) (upper right-hand corner).
  //
  // 7<6<5<4<3<2<1<0
  //               v
  // 8<7<6<5<4<3<2<1
  //               v
  // 9<8<7<6<5<4<3<2
  //               v
  // a<9<8<7<6<5<4<3
  //               v
  // b<a<9<8<7<6<5<4
  //               v
  // c<b<a<9<8<7<6<5
  //               v
  // d<c<b<a<9<8<7<6
  //               v
  // e<d<c<b<a<9<8<7
  //
  // 2. Boot tile is at (4, 8) (middle of south side of chip).
  //
  // A<9<8<7>8>9>A>B
  //       ^
  // 9<8<7<6>7>8>9>A
  //       ^
  // 8<7<6<5>6>7>8>9
  //       ^
  // 7<6<5<4>5>6>7>8
  //       ^
  // 6<5<4<3>4>5>6>7
  //       ^
  // 5<4<3<2>3>4>5>6
  //       ^
  // 4<3<2<1>2>3>4>5
  //       ^
  // 3<2<1<0>1>2>3>4
  //
  //
  // This algorithm ensures that we never try to boot a tile which might
  // have already been booted by another tile.  An alternative would be
  // for the master boot tile to just loop through all of the other tiles,
  // but overlapping some of the work seems like a good idea.
  //
  switch (direction)
  {
  case NORTH:
    if (myaddr.bits.y != lrhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y + 1 };

    if (myaddr.bits.x != lrhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x + 1, .bits.y = myaddr.bits.y };

    if (myaddr.bits.x != ulhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x - 1, .bits.y = myaddr.bits.y };

    break;

  case EAST:
    //
    // We only go North/South from East if we're the boot tile.
    //
    if (master_boot)
    {
      if (myaddr.bits.y != ulhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y - 1 };

      if (myaddr.bits.y != lrhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y + 1 };
    }

    if (myaddr.bits.x != ulhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x - 1, .bits.y = myaddr.bits.y };

    break;

  case SOUTH:
    if (myaddr.bits.y != ulhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y - 1 };

    if (myaddr.bits.x != lrhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x + 1, .bits.y = myaddr.bits.y };

    if (myaddr.bits.x != ulhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x - 1, .bits.y = myaddr.bits.y };

    break;

  case WEST:
    //
    // We only go North/South from West if we're the boot tile.
    //
    if (master_boot)
    {
      if (myaddr.bits.y != ulhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y - 1 };

      if (myaddr.bits.y != lrhc.bits.y)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x, .bits.y = myaddr.bits.y + 1 };
    }

    if (myaddr.bits.x != lrhc.bits.x)
      output_tiles[num_output_tiles++] =
        (pos_t) { .bits.x = myaddr.bits.x + 1, .bits.y = myaddr.bits.y };

    break;

  default:
    boot_error(BOOT_ERR_INPUT_INVALID_DIRECTION);
  }

  //
  // Set up for IDN messaging.
  //

  //
  // Now send code, data, and config info to other tiles.
  //
  for (int i = 0; i < num_output_tiles; i++)
  {
    //
    // To make this code easier, we just send single-word UDN messages;
    // this is slightly inefficient but no one should really care.
    //
    output_tiles[i].bits.len = 1;
    unsigned long hdr = output_tiles[i].word;

    // First send the L 0.5 boot (L0 boot format).

    udn_send(hdr);
    udn_send(_lh_end - _lh_start);       // Length
    udn_send(hdr);
    udn_send((unsigned long) _lh_start); // Load address

    for (unsigned long* address = _lh_start; address < _lh_end; address++)
    {
      udn_send(hdr);
      udn_send(*address);                // Code & data
    }

    udn_send(hdr);
    udn_send((unsigned long)_lh_start);  // Jump-to address

    // Then send the L1 boot (secondary boot format).

#ifndef __DOXYGEN__
/** Send a word on the UDN and update the CRC in crc. */
#define udn_send_crc(val) \
    do \
    { \
      uint64_t x = (val); \
      crc = crc32_64(crc, x); \
      udn_send(x); \
    } while (0)
#endif /* __DOXYGEN__ */

    crc = ~0;

    udn_send(hdr);
    udn_send_crc(_sbss - _stext);        // Length
    udn_send(hdr);
    udn_send_crc((unsigned long) _stext);// Load address
    udn_send(hdr);
    udn_send(~crc);                      // CRC

    for (unsigned long* address = _stext; address < _sbss; address++)
    {
      udn_send(hdr);
      udn_send_crc(*address);            // Code & data
    }

    udn_send(hdr);
    udn_send_crc(0);                     // Zero length means jump to address
    udn_send(hdr);
    udn_send_crc((unsigned long) _start);// Jump-to address
    udn_send(hdr);
    udn_send(~crc);                      // CRC

    // Finally send the parameters.

    crc = ~0;

    udn_send(hdr);
    udn_send_crc(myaddr.word);           // Predecessor (feed) tile coordinates
    udn_send(hdr);
    udn_send_crc(masteraddr.word);       // Master tile coordinates
    udn_send(hdr);
    udn_send_crc(ulhc.word);             // Upper-left-hand-corner
    udn_send(hdr);
    udn_send_crc(lrhc.word);             // Lower-right-hand-corner
    udn_send(hdr);
    udn_send(~crc);                      // CRC
  }


























































  //
  // Set the Cache Invalid Compression Mode based on the chip size.  If
  // either WIDTH or HEIGHT is more than 6, we simply enable 2x2 (mode 3)
  // inval compression.  One could imagine geometries where we'd be better
  // off with mode 1 or mode 2, but it turns out there aren't any real
  // chips for which that's true.
  //
  if ((lrhc.bits.x >= 6) || (lrhc.bits.y >= 6))
  {
    __insn_mtspr(SPR_CACHE_INVALIDATION_COMPRESSION_MODE,
                 SPR_CACHE_INVALIDATION_COMPRESSION_MODE__CACHE_INVALIDATION_COMPRESSION_MODE_VAL_X_Y_COMPRESSION);

    //
    // We need to set the invalidation masks, too, so we start with all
    // tiles invalid, and only unmask those in our grid.
    //
    uint64_t mask[3] = {~0ULL, ~0ULL, ~0ULL};

    for (int x = ulhc.bits.x; x <= lrhc.bits.x; x++)
    {
      for (int y = ulhc.bits.y; y <= lrhc.bits.y; y++)
      {
        int offset = 4 * (6 * (x / 2) + (y / 2)) +
                     ((x & 1) ? 1 : 0) + ((y & 1) ? 2 : 0);
        mask[offset / 64] &= ~(1ULL << (offset % 64));
      }
    }

    __insn_mtspr(SPR_CACHE_INVALIDATION_MASK_0, mask[0]);
    __insn_mtspr(SPR_CACHE_INVALIDATION_MASK_1, mask[1]);
    __insn_mtspr(SPR_CACHE_INVALIDATION_MASK_2, mask[2]);
  }

  //
  // Go do the rest of our processing depending on what role we're playing.
  //
  if (myaddr.word == masteraddr.word)
    boot_master(boot_cycle, board_flags);
  else
    boot_slave();
}


/** Test a shim to see what type it is; then update the state structure
 *  appropriately.
 * @param test_shim Shim to test.
 * @param state Pointer to a state structure which will be filled in with
 *   information on the shim found, if it's a type we track.
 * @return 1 if a shim was found; since we don't call this routine on
 *   shims that don't exist, we always return 1.
 */
static int
boot_probe_shim(pos_t test_shim, struct boot_probe_shim_state* state)
{
  RSH_DEV_INFO_t info =
    { .word = cfg_rd(test_shim.word, 0, RSH_DEV_INFO) };

  switch (info.type)
  {
  case RSH_DEV_INFO__TYPE_VAL_DDR3:
    if (state->num_mshims < MAX_MSHIMS)
      state->mshims[state->num_mshims++] = test_shim;
    break;

  case RSH_DEV_INFO__TYPE_VAL_RSHIM:
    if (state->num_rshims < MAX_RSHIMS)
      state->rshims[state->num_rshims++] = test_shim;
    break;

  case RSH_DEV_INFO__TYPE_VAL_TRIO:
    if (state->num_pcies < MAX_PCIES)
      state->pcies[state->num_pcies++] = test_shim;
    break;

  case RSH_DEV_INFO__TYPE_VAL_MPIPE:
    if (state->num_mpipes < MAX_MPIPES)
      state->mpipes[state->num_mpipes++] = test_shim;
    break;

  case RSH_DEV_INFO__TYPE_VAL_COMPRESSION:
    if (state->num_mica_comps < MAX_MICA_COMPS)
      state->mica_comps[state->num_mica_comps++] = test_shim;

    //
    // Configure SDN route order setting to match tiles.
    //
    cfg_wr(test_shim.word, 0, MICA_COMP_DEV_CTL,
           cfg_rd(test_shim.word, 0, MICA_COMP_DEV_CTL) &
           ~MICA_COMP_DEV_CTL__SDN_ROUTE_ORDER_MASK);
    break;

  case RSH_DEV_INFO__TYPE_VAL_CRYPTO:
    if (state->num_mica_crypts < MAX_MICA_CRYPTOS)
      state->mica_crypts[state->num_mica_crypts++] = test_shim;

    //
    // Configure SDN route order setting to match tiles.
    //
    cfg_wr(test_shim.word, 0, MICA_CRYPTO_DEV_CTL,
           cfg_rd(test_shim.word, 0, MICA_CRYPTO_DEV_CTL) &
           ~MICA_CRYPTO_DEV_CTL__SDN_ROUTE_ORDER_MASK);
    break;

  case RSH_DEV_INFO__TYPE_VAL_GPIO:
    gpioaddr = test_shim;
    break;

  default:
    break;
  }

  return (1);
}


//
// Values for the MEM_STRIPE_CONFIG_t SPR which produce various stripe
// configurations.
//
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_01 =
  {{ .mode0 = 1, .mode1 = 1, .mode2 = 0, .mode3 = 0, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_23 =
  {{ .mode0 = 0, .mode1 = 0, .mode2 = 1, .mode3 = 1, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_02 =
  {{ .mode0 = 2, .mode1 = 0, .mode2 = 2, .mode3 = 0, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_13 =
  {{ .mode0 = 0, .mode1 = 2, .mode2 = 0, .mode3 = 2, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_02_13 =
  {{ .mode0 = 2, .mode1 = 2, .mode2 = 2, .mode3 = 2, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_01_23 =
  {{ .mode0 = 1, .mode1 = 1, .mode2 = 1, .mode3 = 1, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_0123 =
  {{ .mode0 = 3, .mode1 = 3, .mode2 = 3, .mode3 = 3, .hash_enable = 7 }};
static const SPR_MEM_STRIPE_CONFIG_t STRIPE_NONE =
  {{ .mode0 = 0, .mode1 = 0, .mode2 = 0, .mode3 = 0, .hash_enable = 0 }};


/** Figure out our striping configuration.
 * @param shim_sizes Sizes, in bytes, of the available shims; unavailable
 *   shims have size zero.  Upon return, these values may be modified to
 *   represent the amount of memory which will actually be used on each
 *   shim; this only happens if we're forcing striping and decide to
 *   stripe shims that aren't all the same size.
 * @param board_flags Pointer to the board flags, which will have striping
 *   flags added as appropriate.
 * @return Value to be placed in each tile's stripe configuration SPR.
 */
static SPR_MEM_STRIPE_CONFIG_t
stripe_calc(int64_t shim_sizes[4], uint32_t* board_flags)
{
  if (striping_requested)
  {
    //
    // First see whether we can do 4-way striping.
    //
    if (shim_sizes[0] && shim_sizes[1] && shim_sizes[2] && shim_sizes[3])
    {
      //
      // Yes, we could; now see if we can do so without losing any memory,
      // if we care about that.
      //
      if (shim_sizes[0] == shim_sizes[1] &&
          shim_sizes[1] == shim_sizes[2] &&
          shim_sizes[2] == shim_sizes[3])
      {
        *board_flags |= BOARD_STRIPE_MEMORY;
        return STRIPE_0123;
      }
      else if (striping_forced)
      {
        int64_t minsize = min(min(shim_sizes[0], shim_sizes[1]),
                              min(shim_sizes[2], shim_sizes[3]));

        shim_sizes[0] = shim_sizes[1] =
          shim_sizes[2] = shim_sizes[3] = minsize;

        *board_flags |= BOARD_STRIPE_MEMORY | BOARD_STRIPE_LOSS;
        return STRIPE_0123;
      }
      else
      {
        //
        // We couldn't do 4-way, but we have 4 controllers, so can we
        // do 2 x 2-way?
        //
        if (shim_sizes[0] == shim_sizes[1] &&
            shim_sizes[2] == shim_sizes[3])
        {
          *board_flags |= BOARD_STRIPE_MEMORY;
          return STRIPE_01_23;
        }
        else if (shim_sizes[0] == shim_sizes[2] &&
                 shim_sizes[1] == shim_sizes[3])
        {
          *board_flags |= BOARD_STRIPE_MEMORY;
          return STRIPE_02_13;
        }
      }
    }

    //
    // Okay, we can't stripe everybody, so can we find one pair of
    // controllers we can stripe?  If we have more than one choice,
    // we pick the one which will lose the least memory.
    //
    struct stripe_pair_info
    {
      int s0;                       /* First shim in stripe */
      int s1;                       /* Second shim in stripe */
      SPR_MEM_STRIPE_CONFIG_t mode; /* Mode if these shims are striped */
    }
    spi[] =
    {
      { 0, 1, STRIPE_01 },
      { 2, 3, STRIPE_23 },
      { 0, 2, STRIPE_02 },
      { 1, 3, STRIPE_13 },
    };

    int64_t min_loss = (int64_t) 1 << MSH_MAX_SIZE_SHIFT;
    struct stripe_pair_info* min_loss_ptr = NULL;

    for (int i = 0; i < 4; i++)
    {
      if (shim_sizes[spi[i].s0] && shim_sizes[spi[i].s1])
      {
        int64_t losses = abs(shim_sizes[spi[i].s0] - shim_sizes[spi[i].s1]);
        if (losses < min_loss)
        {
          min_loss = losses;
          min_loss_ptr = &spi[i];
        }
      }
    }

    if (min_loss_ptr && (striping_forced || min_loss == 0))
    {
      shim_sizes[min_loss_ptr->s0] = shim_sizes[min_loss_ptr->s1] =
        min(shim_sizes[min_loss_ptr->s0], shim_sizes[min_loss_ptr->s1]);

      *board_flags |= BOARD_STRIPE_MEMORY;
      if (min_loss != 0)
        *board_flags |= BOARD_STRIPE_LOSS;
      return min_loss_ptr->mode;
    }
  }

  //
  // We either can't or don't want to stripe anyone.
  //
  return STRIPE_NONE;
}

#ifdef SPEED_DEBUG
/** Debug output for the frequency/voltage configuration code. */
#define SPEED_TRACE boot_printf
#else
/** No debug output for the frequency/voltage configuration code. */
#define SPEED_TRACE(...) do { } while (0)
#endif

/** Data used to track the state of various chip clocks. */
struct clock
{
  /** Name, for error/debug messages. */
  char* name;
  /** Shim coordinates. */
  pos_t shimaddr;
  /** PLL register address. */
  unsigned long pll_reg_addr;
  /** Fuse table pointer. */
  struct f2v_entry* fuses;
  /** Current frequency. */
  long cur_freq;
  /** Desired frequency (what the user asked for). */
  long des_freq;
  /** Target frequency (what we're actually going to set it to). */
  long targ_freq;
};

/** Maximum number of entries in the clock array. */
const int max_clocks = MAX_RSHIMS + MAX_MPIPES + MAX_PCIES +
                       MAX_MICA_COMPS + MAX_MICA_CRYPTOS + MAX_USB_HOSTS +
                       MAX_MSHIMS;

/** Request the highest possible speed, raising core voltage if needed. */
#define DESIRED_FREQ_MAX_RAISEV  LONG_MAX

/** Request the highest possible speed, without modifying core voltage. */
#define DESIRED_FREQ_MAX         (LONG_MAX - 1)



/** Add an entry to the clock table if necessary.
 * @param clocks Pointer to the base of the clock table.
 * @param n_clocks Pointer to the current length of the clock table, which
 *  will be updated if an entry is added.
 * @param name Clock name.
 * @param shimaddr Address of the target shim.  If 0, the shim was not
 *  probed and no entry will be added.
 * @param pll_reg_addr Address of the PLL register within the shim.
 * @param fuses Pointer to the frequency-to-voltage table for this clock.
 * @param freq_mhz Frequency in MHz for this shim; 0 if the shim was not
 *  configured, SPEED_DEFAULT if it was but no speed was identified.
 * @param bib_type For the core PLL, this is -1.  For other PLLs, this is
 *  the shim type from the bi_clock_inst BIB structure.
 */
static void
add_clock_entry(struct clock* clocks, int* n_clocks, char* name,
                pos_t shimaddr, unsigned long pll_reg_addr,
                struct f2v_entry* fuses, int freq_mhz, int bib_type)
{
  if (*n_clocks >= max_clocks || shimaddr.word == 0 || freq_mhz == 0)
    return;

  struct clock* clkp = &clocks[(*n_clocks)++];
  clkp->name = name;
  clkp->shimaddr = shimaddr;
  clkp->pll_reg_addr = pll_reg_addr;
  clkp->fuses = fuses;

  if (freq_mhz != SPEED_DEFAULT)
  {
    clkp->des_freq = freq_mhz * 1000L * 1000;
  }
  else
  {
    if (bib_type == -1)
    {
      //
      // See if there's a board default in the BIB, and if so, use it.
      // If not, specify the maximum possible speed.
      //
      bi_ptr_t bp;

      if (bi_getparam(BI_TYPE_NOM_TILE_FREQ, 0, &bp, NULL) != BI_NULL)
      {
        struct bi_nom_tile_freq* ntf = bp;
        clkp->des_freq = ntf->clock;
      }
      else
        clkp->des_freq = DESIRED_FREQ_MAX_RAISEV;
    }
    else
    {
      //
      // See if there's a board default in the BIB, and if so, use it.
      // If not, specify the maximum possible speed without raising the
      // voltage.
      //
      union
      {
        bi_inst_t inst;
        struct bi_clock_inst bci;
      }
      ci =
      {
        .bci.type = bib_type,
        //
        // Currently all shims are either instance 0, or they share a clock
        // with instance 0, so we only pay attention to one entry.  This
        // will probably need to be enhanced in the future.
        //
        .bci.shim = 0,
      };

      bi_ptr_t bp;

      if (bi_getparam(BI_TYPE_SHIM_CLOCK, ci.inst, &bp, NULL) != BI_NULL)
      {
        struct bi_shim_clock* sc = bp;
        clkp->des_freq = sc->freq;
      }
      else
        clkp->des_freq = DESIRED_FREQ_MAX;
    }
  }
}


/** Set a clock frequency.
 * @param clkp Pointer to the clock's data.
 * @param freq Frequency to set.
 */
static void
set_freq(struct clock* clkp, long freq)
{
  RSH_CLOCK_CONTROL_t rcc =
  {
    .word = cfg_rd(clkp->shimaddr.word, 0, clkp->pll_reg_addr)
  };

  //
  // We don't set the clock if it's already running.  This is so
  // that we don't mess up our debug interface (USB, PCIe) if the PLL
  // was automatically spun up via strapping pins.
  //
  if (rcc.ena)
    return;

  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  rcc = (RSH_CLOCK_CONTROL_t)
  {{
    .ena = 1,
    .pll_m = m,
    .pll_n = n,
    .pll_q = q,
    .pll_range = range,
  }};

  cfg_wr(clkp->shimaddr.word, 0, clkp->pll_reg_addr, rcc.word);
  __insn_mf();

  do
  {
    rcc.word = cfg_rd(clkp->shimaddr.word, 0, clkp->pll_reg_addr);
  }
  while (!rcc.clock_ready);
}


/** Get a clock frequency.
 * @param clkp Pointer to the clock's data.
 * @return Clock frequency in hertz.
 */
static long
get_freq(struct clock* clkp)
{
  RSH_CLOCK_CONTROL_t rcc =
  {
    .word = cfg_rd(clkp->shimaddr.word, 0, clkp->pll_reg_addr)
  };

  return pll_to_freq(!rcc.ena, rcc.pll_m, rcc.pll_n, rcc.pll_q, REFCLK);
}


/** Configure the processor and shim PLLs.
 * @param probe_state Shim state from our probe.
 * @param board_flags board flags.
 * @param mshim_freqs Frequencies for the mshims, in T/s (set by this routine).
 */
static void
set_speeds(struct boot_probe_shim_state* probe_state, uint32_t board_flags,
           long* mshim_freqs)
{
  //
  // The simulator doesn't implement real speed-setting support, or the
  // fuses, so we just do nothing.  We don't need to set mshim_freqs[]
  // because the simulator mshim config code won't end up looking at it.
  //
  if (sim_is_simulator())
  {
    core_freq = sim_query_cpu_speed();
    return;
  }

  //
  // The PLL registers on the FPGA aren't implemented, so we hang when
  // we wait for them to spin up; do nothing.  Again, the values in
  // mshim_freqs[] aren't relevant here.
  //
  if (board_flags & BOARD_FPGA)
  {
    core_freq = 20 * 1000 * 1000;
    return;
  }

  //
  // Get the fuses.
  //
  union fuses fuses;
  if (get_fuses(rshimaddr, &fuses))
    SPEED_TRACE("fuses invalid, using default values\n");

  //
  // Get the min/max voltage values.
  //
  unsigned int min_v = 0;
  unsigned int max_v = 0;
  if (get_voltage_range(&min_v, &max_v))
    SPEED_TRACE("voltage min %d uv, max %d uv from bib\n", min_v, max_v);
  else
    SPEED_TRACE("voltage min %d uv, max %d uv from VRM limits\n", min_v,
                max_v);

  //
  // Note: we must make both calls, so we need | below, not ||.
  //
  if (loadline_vid_to_chip(&min_v) | loadline_vid_to_chip(&max_v))
    SPEED_TRACE("doing load line adjustment, min/max now %d/%d uv\n",
                min_v, max_v);

  //
  // Now allocate and fill in the clock table.
  //
  int n_clocks = 0;
  struct clock clocks[max_clocks];

  //
  // Setting the core frequency to 0 is almost certainly a mistake, so use
  // the default instead.
  //
  int core_freq_mhz =
    (boot_params.cfg.speed_core) ? boot_params.cfg.speed_core : SPEED_DEFAULT;
  add_clock_entry(clocks, &n_clocks, "core", probe_state->rshims[0],
                  RSH_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_CORE],
                  core_freq_mhz, -1);

  add_clock_entry(clocks, &n_clocks, "trio/0", probe_state->pcies[0],
                  TRIO_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_TRIO],
                  boot_params.cfg.speed_trio_0, BI_CLOCK_INST_TYPE__VAL_TRIO);

  add_clock_entry(clocks, &n_clocks, "trio/1", probe_state->pcies[1],
                  TRIO_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_TRIO],
                  boot_params.cfg.speed_trio_1, BI_CLOCK_INST_TYPE__VAL_TRIO);

  add_clock_entry(clocks, &n_clocks, "mpipe/0 core", probe_state->mpipes[0],
                  MPIPE_PCLK_CONTROL, &fuses.data.f2v[FUSE_F2V_MPIPE_CORE],
                  boot_params.cfg.speed_mpipe_0_core,
                  BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN);

  add_clock_entry(clocks, &n_clocks, "mpipe/0 cls", probe_state->mpipes[0],
                  MPIPE_KCLK_CONTROL, &fuses.data.f2v[FUSE_F2V_MPIPE_CLS],
                  boot_params.cfg.speed_mpipe_0_cls,
                  BI_CLOCK_INST_TYPE__VAL_MPIPE_CLASSIFIER);

  add_clock_entry(clocks, &n_clocks, "mpipe/1 core", probe_state->mpipes[1],
                  MPIPE_PCLK_CONTROL, &fuses.data.f2v[FUSE_F2V_MPIPE_CORE],
                  boot_params.cfg.speed_mpipe_1_core,
                  BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN);

  add_clock_entry(clocks, &n_clocks, "mpipe/1 cls", probe_state->mpipes[1],
                  MPIPE_KCLK_CONTROL, &fuses.data.f2v[FUSE_F2V_MPIPE_CLS],
                  boot_params.cfg.speed_mpipe_1_cls,
                  BI_CLOCK_INST_TYPE__VAL_MPIPE_CLASSIFIER);

  add_clock_entry(clocks, &n_clocks, "comp/0", probe_state->mica_comps[0],
                  MICA_COMP_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_COMPRESS],
                  boot_params.cfg.speed_comp_0,
                  BI_CLOCK_INST_TYPE__VAL_MICA_COMPRESS);

  add_clock_entry(clocks, &n_clocks, "comp/1", probe_state->mica_comps[1],
                  MICA_COMP_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_COMPRESS],
                  boot_params.cfg.speed_comp_1,
                  BI_CLOCK_INST_TYPE__VAL_MICA_COMPRESS);

  add_clock_entry(clocks, &n_clocks, "crypto/0", probe_state->mica_crypts[0],
                  MICA_CRYPTO_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_CRYPTO],
                  boot_params.cfg.speed_crypto_0,
                  BI_CLOCK_INST_TYPE__VAL_MICA_CRYPTO);

  add_clock_entry(clocks, &n_clocks, "crypto/1", probe_state->mica_crypts[1],
                  MICA_CRYPTO_CLOCK_CONTROL, &fuses.data.f2v[FUSE_F2V_CRYPTO],
                  boot_params.cfg.speed_crypto_1,
                  BI_CLOCK_INST_TYPE__VAL_MICA_CRYPTO);

  //
  // The mshims are a special case, in a number of ways:
  //
  // - Their requested speed depends not only upon user input, but upon the
  //   characteristics of the installed memory; we need to do the first part
  //   of the memory config in order to compute the speeds.
  //
  // - We don't want to actually spin up the PLLs here; we'll do that when
  //   we really configure the shims, a bit later.  We set pll_reg_addr to
  //   0 to signify this, and check for that below.
  //
  // - There isn't a matching CLOCK_INST type for mshims, since we have
  //   special BIB entries describing memory characteristics including
  //   speed.  We won't even bother trying to look this up, since we'll
  //   always pass in an explicit speed, but we pass in 0xF just to be
  //   sure it doesn't match (the type field is 4 bits, and valid values
  //   are currently 0-5, so it seems unlikely that 0xF will ever be valid).
  //
  int mshim_max_speed[MAX_MSHIMS];
  char* mshim_names[MAX_MSHIMS] =
    { "mshim/0", "mshim/1", "mshim/2", "mshim/3" };

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    int boot_param_speed = boot_params.cfg.mem_speed[i];
    //
    // First we figure out the max speed that the shim can run at.  This
    // will be no larger than the speed we got in the boot params, and may
    // be 0 if the shim should be disabled.
    //
    int max_speed = mshim_preconfig_shim(probe_state->mshims[i], rshimaddr,
                                         board_flags, boot_param_speed);
    mshim_max_speed[i] = max_speed / 1000000;

    //
    // Now we figure out what speed to put in the clock table.  If the
    // speed was set in the boot params, or the shim is disabled, we use
    // the max speed (which will be no greater than that requested),
    // otherwise we'll figure out the highest speed based on the voltage.
    //
    int req_speed =
      (boot_param_speed || !mshim_max_speed[i]) ? mshim_max_speed[i]
                                                : SPEED_DEFAULT;

    add_clock_entry(clocks, &n_clocks, mshim_names[i], probe_state->mshims[i],
                    0, &fuses.data.f2v[FUSE_F2V_MSH], req_speed, 0xF);

    SPEED_TRACE("mshim/%d: boot_param speed %d, max_speed %d, req_speed %d\n",
                i, boot_param_speed, mshim_max_speed[i], req_speed);
  }

  //
  // Now determine our target voltage.
  //
  unsigned int target_v = 0;

  //
  // Walk the clock table, get each clock's current frequency, and update
  // the target voltage based on each clock's desired frequency.
  //
  for (int i = 0; i < n_clocks; i++)
  {
    struct clock* clkp = &clocks[i];

    clkp->cur_freq = clkp->pll_reg_addr ? get_freq(clkp) : REFCLK;

    SPEED_TRACE("%s: current %ld Hz, desired %ld Hz\n",
                clkp->name, clkp->cur_freq, clkp->des_freq);

    //
    // For explicitly requested frequencies, raise the target voltage
    // if needed.  DESIRED_FREQ_MAX means "as fast as possible, but
    // don't raise the voltage", so in that case we don't.
    //
    if (clkp->des_freq >= 0 && clkp->des_freq != DESIRED_FREQ_MAX)
    {
      unsigned int desired_v = freq_to_volt(clkp->des_freq, clkp->fuses,
                                            FUSE_NUM_F2V_ENTRIES);
      target_v = max(target_v, desired_v);
      SPEED_TRACE("desired voltage %d, new target voltage %d uv\n",
                  desired_v, target_v);
    }
  }

  //
  // Now clip the target voltage to fit within the board or chip limits.
  // We also round up the voltage to the next VID step; this is what
  // we're going to get when we eventually set it, so we might as well
  // take advantage of it.
  //
  target_v = min(target_v, max_v);
  target_v = VID_TO_UV(UV_TO_VID(target_v));
  target_v = max(target_v, min_v);

  SPEED_TRACE("target voltage clipped to %d uv\n", target_v);

  //
  // Now figure out what frequencies we can actually achieve for each
  // device, based on the voltage we'll be using.
  //
  for (int i = 0; i < n_clocks; i++)
  {
    clocks[i].targ_freq = min(clocks[i].des_freq,
                              volt_to_freq(target_v, clocks[i].fuses,
                                           FUSE_NUM_F2V_ENTRIES));

    SPEED_TRACE("%s: target %ld Hz\n", clocks[i].name, clocks[i].targ_freq);
  }

  //
  // Set the new voltage.
  //
  if (loadline_chip_to_vid(&target_v))
    SPEED_TRACE("doing load line adjustment, target voltage now %d\n",
                target_v);

  SPEED_TRACE("setting voltage to %d, VID 0x%x\n", target_v,
              UV_TO_VID(target_v));
  set_vid(rshimaddr, UV_TO_VID(target_v));

  //
  // Configure the core clock duty cycle if needed before we spin up the
  // core PLL.
  //
  if (fuses.data.duty_cycle_adjust_valid)
    set_cclk_duty(fuses.data.duty_cycle_adjust);

  //
  // Set any frequencies which are changing.
  //
  for (int i = 0; i < n_clocks; i++)
  {
    struct clock* clkp = &clocks[i];

    if (clkp->pll_reg_addr && clkp->targ_freq != clkp->cur_freq)
    {
      SPEED_TRACE("%s: setting to %ld Hz\n", clkp->name, clkp->targ_freq);
      set_freq(clkp, clkp->targ_freq);
    }
  }

  //
  // Go through the table and complain about any frequencies we couldn't
  // hit.  We don't do this for clocks set to either of the magic "as high
  // as possible" values, and we only complain if we're more than 1 MHz off.
  // While we're at it, get the core and mshim clock frequencies so we can
  // return them.
  //
  for (int i = 0; i < n_clocks; i++)
  {
    struct clock* clkp = &clocks[i];

    if (clkp->pll_reg_addr &&
        clkp->des_freq != DESIRED_FREQ_MAX &&
        clkp->des_freq != DESIRED_FREQ_MAX_RAISEV)
    {
      clkp->cur_freq = get_freq(clkp);

      if (abs(clkp->cur_freq - clkp->des_freq) > 1L * 1000 * 1000)
        boot_printf("boot_warning: %s: couldn't set requested "
                    "frequency %ld Hz, got %ld Hz\n", clkp->name,
                clkp->des_freq, clkp->cur_freq);
    }

    if (!strcmp(clkp->name, "core"))
      core_freq = get_freq(clkp);
    else if (!strncmp(clkp->name, "mshim/", strlen("mshim/")))
    {
      //
      // In the case where we just calculated a default speed based on the
      // voltage and F2V table, we might have come up with something the
      // memory shim can't do, so we need to clip to the max speed we saved
      // above.
      //
      int mshim_idx = clkp->name[strlen("mshim/")] - '0';
      long max_speed = mshim_max_speed[mshim_idx] * 1000000;
      mshim_freqs[mshim_idx] = min(clkp->targ_freq, max_speed);
      SPEED_TRACE("mshim/%d: calculated freq %ld, clipping to %ld\n",
                  mshim_idx, clkp->targ_freq, mshim_freqs[mshim_idx]);
    }
  }
}


/** Size of the memory used by the hypervisor. */
#define HV_POST_RAM_SIZE(ulhc, lrhc) \
        (HV_CODE_DATA_MEM_SIZE((ulhc), (lrhc)) + \
         HV_FLUSH_PAGE_SIZE /* + HVGDB_DATA_PAGE_SIZE */)


/** Probe our shims, looking for the mshims.  Configure all of the mshims,
 *  and for the first mshim that passes POST, set us up to use it.
 * @param shim_mask Pointer to the shim mask which is set by this routine.
 *        This is a bitmap, each bit corresponding to a shim; the bit is 1
 *        if a shim was found, and 0 otherwise.  The bits are in probe order,
 *        with the least significant bit of the mask being the last shim
 *        probed.
 * @param board_flags Pointer to the returned board flags word.
 * @return Nonzero if we found an mshim, zero otherwise.
 */
static int
config_shims(unsigned long* shim_mask, uint32_t* board_flags)
{
  //
  // We may need the GPIO shim to construct the BIB if there's an I2C bus
  // arbiter controlled by GPIO pins, as on the TILExtreme-Duo.  However,
  // we don't want to probe the shims before we've read the BIB, because it
  // might contain an IO_DISABLE item which would disable some shims.
  // While somewhat ugly, the only reasonable way to handle this is by
  // hardwiring in the known possible locations of the GPIO shim.  Note
  // that the probe step will overwrite this value, which means that other
  // shim locations are fine for any machine that doesn't use an I2C
  // arbiter.
  //
  if (is_gx72)
    gpioaddr = (pos_t) { .bits.x = 1, .bits.y = 0xF };
  else
    gpioaddr = (pos_t) { .bits.x = 4, .bits.y = 0xF };

  //
  // Load the board information block, if we might have one, and use it to
  // set the board flags.  Note that since we load it into alloca'ed space,
  // it's only usable from things we call from this routine, like
  // mshim_config_shim; once we return it will vanish.
  //
  if (sim_is_simulator())
    *board_flags |= BOARD_BRINGUP_BOARD;
  else
  {
    //
    // If the BIB is on I2C, it must be on bus 0, but we enable all of the
    // I2C slaves since we may need them for AIBs or for DIMM SPDs.
    //
    for (int i = 0; i < MAX_I2CMS; i++)
      i2c_enable(rshimaddr, I2CMS_CHAN(i));

    int bi_addr;
    int bi_len = bi_locate_boot(rshimaddr, &bi_addr);

    if (bi_len > 0)
    {
      //
      // We allocate the maximum BIB size, rather than the size we just
      // found, since we want to be able to merge in AIBs.
      //
      bi_len = BI_MAX_WDS * sizeof (uint32_t);
      uint32_t* bi_block = alloca(bi_len);
      bi_load_boot(rshimaddr, bi_block, bi_len, bi_addr);

      if (bi_addr >= 0)
        *board_flags |= BOARD_BI_I2C;

      uint32_t desc;
      bi_ptr_t bi;

      desc = bi_getparam(BI_TYPE_BOARD_PART_NUM, 0, &bi, NULL);
      //
      // We clip the length to 10 so we match all dash numbers.
      //
      int pn_len = min(BI_BYTES(desc), 10);

      if (desc != BI_NULL && !strncmp("402-99999-", (char*) bi, pn_len))
        *board_flags |= BOARD_FPGA;

      if (desc != BI_NULL && !strncmp("402-00034-", (char*) bi, pn_len))
        *board_flags |= BOARD_BRINGUP_BOARD;

      //
      // Handle the console configuration.
      //
      desc = bi_getparam(BI_TYPE_CONSOLE_CFG, 0, &bi, NULL);
      if (desc != BI_NULL)
      {
        struct bi_console_cfg* cfg = (struct bi_console_cfg*) bi;

        console_init(REFCLK, cfg, board_flags);

        if (cfg->port)
          *board_flags |= BOARD_CONSOLE_UART1;
      }

      //
      // See if we have an IO_DISABLE item in the BIB; if so, act on it,
      // before we do any probing.
      //
      desc = bi_getparam(BI_TYPE_IO_DISABLE, 0, &bi, NULL);
      if (desc != BI_NULL)
      {
        struct bi_io_disable* id = (struct bi_io_disable*) bi;

        for (int i = 0; i < BI_WDS(desc) / 2; i++)
          cfg_wr(rshimaddr.word, 0, RSH_GX36_IO_DISABLE0 + 8 * i,
                 id->disable[i]);
      }
    }
    else
    {
      boot_printf("boot_panic: no Board Information Block found\n");
      boot_error(BOOT_ERR_NO_BIB);
    }

    if (srom_is_booting(rshimaddr, SROM_CHAN))
      *board_flags |= BOARD_BOOTED_SROM;
  }

  struct boot_probe_shim_state probe_state =
    { .num_mshims = 0, .num_pcies = 0, .num_rshims = 0 };
  pos_t pos = { .word = 0 };

  RSH_FABRIC_CONN_t fabric_conn =
    { .word = cfg_rd(rshimaddr.word, 0, RSH_FABRIC_CONN) };

  *shim_mask = 0;

  int x, y;

  // Probe left side.

  pos.bits.x = 0xF;

  for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
  {
    pos.bits.y = y;
    *shim_mask <<= 1;
    if (fabric_conn.west & (1 << y))
      *shim_mask |= (boot_probe_shim(pos, &probe_state) != 0);
  }

  // Probe top edge.

  pos.bits.y = 0xF;

  for (x = grid_ulhc.bits.x; x <= grid_lrhc.bits.x; x++)
  {
    pos.bits.x = x;
    *shim_mask <<= 1;
    if (fabric_conn.north & (1 << x))
      *shim_mask |= (boot_probe_shim(pos, &probe_state) != 0);
  }

  // Probe bottom edge; note that this goes right-to-left.

  pos.bits.y = grid_lrhc.bits.y + 1;

  for (x = grid_lrhc.bits.x; x >= grid_ulhc.bits.x; x--)
  {
    pos.bits.x = x;
    *shim_mask <<= 1;
    if (fabric_conn.south & (1 << x))
      *shim_mask |= (boot_probe_shim(pos, &probe_state) != 0);
  }

  // Probe right side.

  pos.bits.x = grid_lrhc.bits.x + 1;

  for (y = grid_ulhc.bits.y; y <= grid_lrhc.bits.y; y++)
  {
    pos.bits.y = y;
    *shim_mask <<= 1;
    if (fabric_conn.east & (1 << y))
      *shim_mask |= (boot_probe_shim(pos, &probe_state) != 0);
  }

#if 0 // FIXME: GX: no PCIe shims yet.
  //
  // If the PCIe shims have ID information defined in the BIB or via #defines,
  // set that in the shims; if both are present the #defines are used.  Also,
  // we always set the flag which tells the host driver that we're booting
  // but not yet resettable.
  //
  for (int i = 0; i < probe_state.num_pcies; i++)
  {
    uint32_t pcie_id;
    int write_pcie_id = 0;

    uint32_t pcie_classcode;
    int write_pcie_classcode = 0;

    uint32_t pcie_subsystem =
      cfg_rd(probe_state.pcies[i].word, 0, PCIE_GPEX_SUBSYSTEM);
    // We always write the subsystem, so there's no write_xxx variable for it.

    uint32_t *resptr;
    uint32_t desc = bi_getparam(BI_TYPE_PCIE_ID, i, &resptr, NULL);

    if (desc != BI_NULL && BI_WDS(desc) >= 3)
    {
      pcie_classcode = resptr[0];
      pcie_id = resptr[1];
      pcie_subsystem = resptr[2];

      write_pcie_classcode = 1;
      write_pcie_id = 1;
    }

#ifdef PCIE_ID
    pcie_id = PCIE_ID;
    write_pcie_id = 1;
#endif

#ifdef PCIE_CLASSCODE
    pcie_classcode = PCIE_CLASSCODE;
    write_pcie_classcode = 1;
#endif

#ifdef PCIE_SUBSYSTEM
    pcie_subsystem = PCIE_SUBSYSTEM;
#endif

    if (write_pcie_id)
      cfg_wr(probe_state.pcies[i].word, 0, PCIE_GPEX_ID, pcie_id);

    if (write_pcie_classcode)
      cfg_wr(probe_state.pcies[i].word, 0, PCIE_GPEX_CLASSCODE, pcie_classcode);

    //
    // Note that we reserve the top 8 bits of the subsystem word for use
    // by the PCIe iBound and host driver.  FIXME: the fact that we use 8
    // bits ought to be defined in drv_pcie_common.h, not here.
    //
    pcie_subsystem &= RMASK(24);
    pcie_subsystem |= SUBSYSTEM_FLAG_BOOTED << SUBSYSTEM_FLAG_SHIFT;

    cfg_wr(probe_state.pcies[i].word, 0, PCIE_GPEX_SUBSYSTEM, pcie_subsystem);
  }
#endif

  enum post_modes
  {
    POST_QUICK,
    POST_QUERY,
    POST_THOROUGH
  }
  post_mode = POST_MODE;

  enum post_modes post_query_default = POST_QUERY_DEFAULT;

  if (boot_params.cfg.post_override)
  {
    post_query_default = (boot_params.cfg.post_thorough) ? POST_THOROUGH
                                                         : POST_QUICK;
    post_mode = (boot_params.cfg.post_query) ? POST_QUERY
                                             : post_query_default;
  }

  if (post_mode == POST_QUERY &&
#ifdef POST_QUERY_SROM_ONLY
      srom_addr &&
#endif
      !sim_is_simulator())
  {
    /* Turn off protocol mode before prompting. */
    uint_reg_t old_mode = boot_exit_protocol_mode();

    boot_printf("\nType \"t\" in 3 seconds to run thorough POST tests, "
                "\"q\" to run quick tests%s...",
                *board_flags & BOARD_BADCRC_REBOOT ?
                ",\nor \"a\" to boot alternate image" : "");

    int ichar = boot_getchar_timeout(3000);

    /* Restore protocol mode state. */
    boot_restore_mode(old_mode);

    if (ichar > 0)
      boot_printf("%c", ichar);
    boot_printf("\n");

    if ((*board_flags & BOARD_BADCRC_REBOOT) && (ichar == 'a' || ichar == 'A'))
    {
      boot_printf("Alternate image boot requested.\n");
      boot_flush_output();
      boot_reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
    }

    if (ichar == 't' || ichar == 'T')
      post_mode = POST_THOROUGH;
    else if (ichar == 'q' || ichar == 'Q')
      post_mode = POST_QUICK;
    else
      post_mode = post_query_default;

    boot_printf("%s POST tests will be run.\n",
                (post_mode == POST_THOROUGH) ? "Thorough" : "Quick");
  }

  if (post_mode == POST_THOROUGH)
    *board_flags |= BOARD_POST_THOROUGH;

#ifdef USE_PCIE_AS_MEMORY
  //
  // In memoryless mode we just use the TRIO shims instead of the mshims.
  //
  memset(probe_state.mshims, 0, sizeof(probe_state.mshims));
  memcpy(probe_state.mshims, probe_state.pcies,
         probe_state.num_pcies * sizeof (probe_state.pcies[0]));
  probe_state.num_mshims = probe_state.num_pcies;
#endif

  long mshim_freqs[MAX_MSHIMS] = { 0 };

  //
  // Configure the voltage and PLL frequencies.  Note that this also
  // preconfigures the mshims, since that's required to get their desired
  // frequencies.
  //
  set_speeds(&probe_state, *board_flags, mshim_freqs);

  //
  // Configure the DDR voltage.
  //
  mshim_config_ddr_voltage(rshimaddr, *board_flags);

  //
  // Now that we've found everything, we can configure the mshims.  (We
  // had to wait until now since we need to know where the rshim is in order
  // to read the SPD PROMs.)
  //
  int64_t mshim_sizes[MAX_MSHIMS] = { 0 };
  int64_t mshim_bases[MAX_MSHIMS] = { 0 };
#ifndef USE_PCIE_AS_MEMORY
  for (int i = 0; i < probe_state.num_mshims; i++)
  {
    int64_t size = mshim_config_shim(probe_state.mshims[i], rshimaddr,
                                     *board_flags, mshim_freqs[i]);
    if (size < 0)
      *board_flags |= BOARD_POST_FAILURE;
    else
      mshim_sizes[i] = size;
  }

  //
  // Check to see if any of the mshims we found have less than two network
  // ports; if so, disable the CBOX's SEND_COPY attribute.  This is for the
  // benefit of the FPGA.  We must do this before we do any remote loads;
  // before running POST seems like a good place.
  //
  for (int i = 0; i < probe_state.num_mshims; i++)
  {
    if (mshim_sizes[i])
    {
      MSH_MEM_INFO_t mmi =
      {
        .word = cfg_rd(probe_state.mshims[i].word, 0, MSH_MEM_INFO)
      };
      if (__insn_pcnt(mmi.req_ports) < 2)
      {
        SPR_CBOX_MSR_t cm = { .word = __insn_mfspr(SPR_CBOX_MSR) };
        cm.xdn_attr_send_copy_disable = 1;
        __insn_mtspr(SPR_CBOX_MSR, cm.word);
      }
    }
  }
#else // USE_PCIE_AS_MEMORY
  //
  // In memoryless mode, we always disable SEND_COPY and nontemporal
  // operations.
  //
  // FIXME: for now we're just hardwiring in a base and size, if we
  // end up using this for very long in different configurations we may
  // need to get those from the BTK or somewhere else.
  //
  SPR_CBOX_MSR_t cm = { .word = __insn_mfspr(SPR_CBOX_MSR) };
  cm.non_temporal_write_disable = 1;
  cm.xdn_attr_send_copy_disable = 1;
  __insn_mtspr(SPR_CBOX_MSR, cm.word);

  mshim_bases[0] = 0x940000000;
  mshim_sizes[0] =  0x40000000;
#endif // USE_PCIE_AS_MEMORY

  //
  // Zero out any shims on which we'll be using ECC, in parallel.  We do
  // this before we run POST since we want ECC enabled then; otherwise we
  // don't do any testing of the ECC bits.
  //
  struct mshim_zero_state zero_states[4];
  int shims_not_zeroed = 0;
  int64_t max_zero_size = 0;

  for (int i = 0; i < probe_state.num_mshims; i++)
  {
    if (mshim_sizes[i] <= 0)
      continue;

    MSH_CONTROL_t mc = { .word = cfg_rd(probe_state.mshims[i].word, 0,
                                        MSH_CONTROL) };
    if (!mc.ecc_cor)
      continue;

    mshim_zero_start(probe_state.mshims[i], mshim_sizes[i], &zero_states[i]);
    shims_not_zeroed |= 1 << i;

    if (mshim_sizes[i] > max_zero_size)
      max_zero_size = mshim_sizes[i];
  }

  //
  // It takes less than a second to zero out a 4 GB DIMM at 800 MT/s,
  // so to be generous we'll give ourselves 1 second per 2 GB.  We
  // always wait at least a second.
  //
  int zero_secs = max(max_zero_size >> 31, 1);
  uint64_t zero_end_cycle = get_cycle_count() + core_freq * zero_secs;

  while (shims_not_zeroed)
  {
    if (get_cycle_count() > zero_end_cycle)
    {
      boot_printf("boot_panic: BIST hung while zeroing memory\n");
      boot_error(BOOT_ERR_MSH_BIST_HANG);
    }

    for (int i = 0; i < probe_state.num_mshims; i++)
    {
      if ((shims_not_zeroed & (1 << i)) &&
          mshim_zero_done(probe_state.mshims[i], &zero_states[i]))
        shims_not_zeroed &= ~(1 << i);
    }
  }

  //
  // If we're doing thorough POST, loop through the mshims, and run POST on
  // the memory that will be used by the hypervisor; if a shim fails, mark
  // it has having zero bytes.  If we're not striping, we exit as soon
  // as we've found one shim that passes; otherwise, we test all of them,
  // since we don't know how we're going to stripe.  This could be
  // optimized with some more work if it looks like it's making boot take
  // too long.
  //
  if (*board_flags & BOARD_POST_THOROUGH)
  {
    for (int i = 0; i < probe_state.num_mshims; i++)
    {
      if (mshim_sizes[i] <= 0)
        continue;

      //
      // Note that in the striped case, we may end up testing more memory
      // than we need.  It's hard to avoid this, since we want to be safe
      // in the case where we decide not to stripe later.
      //
      long ram_amount = ROUND_UP(HV_POST_RAM_SIZE(ulhc, lrhc), 8192);

      int post_failed = post_ram_l1boot(mshim_sizes[i] + mshim_bases[i] -
                                        ram_amount,
                                        mshim_sizes[i] + mshim_bases[i],
                                        probe_state.mshims[i]);

      if (post_failed)
      {
        mshim_sizes[i] = 0;
        cfg_wr(probe_state.mshims[i].word, 0, MSH_BASELINE_CTL, 0);
        boot_error(POST_ERR_HV_RAM);
        *board_flags |= BOARD_POST_FAILURE;
      }
      else if (!striping_requested)
        break;
    }
    boot_printf("\n");
  }

  //
  // Squeeze any failed shims out of the list of shims.
  //
  int to_idx = 0;

  for (int from_idx = 0; from_idx < probe_state.num_mshims; from_idx++)
  {
    if (mshim_sizes[from_idx] != 0)
    {
      mshim_sizes[to_idx] = mshim_sizes[from_idx];
      probe_state.mshims[to_idx].word = probe_state.mshims[from_idx].word;
      to_idx++;
    }
  }

  probe_state.num_mshims = to_idx;

  //
  // stripe_calc doesn't take the number of shims probed into account, so
  // zero out entries for any shims past that number.
  //
  for (int i = to_idx; i < 4; i++)
    mshim_sizes[i] = 0;

  //
  // If we didn't find a good mshim, return 0.
  //
  if (!probe_state.num_mshims)
    return (0);

  //
  // Finally, set up the CBox map registers; figure out if/how we'll be
  // striping; then set the address range on the memory shims (since that
  // may change based on the striping configuration).  Note that we don't
  // go to any trouble to spread the load over the available mshim ports;
  // we assume that the hypervisor will fix that later.
  //
  for (int i = 0; i < probe_state.num_mshims; i++)
    set_cbox_mmap_spr(i, probe_state.mshims[i].word);

  SPR_MEM_STRIPE_CONFIG_t cfg = stripe_calc(mshim_sizes, board_flags);
  __insn_mtspr(SPR_MEM_STRIPE_CONFIG, cfg.word);

  for (int i = 0; i < probe_state.num_mshims; i++)
  {
#ifndef USE_PCIE_AS_MEMORY
    //
    // Set up address range for each shim's final address size.  Note that
    // we need to clear the top bit of the address mask in case we're
    // striping.
    //
    int mask_bits = 0x7fe << ((8 * sizeof (mshim_sizes[i])) -
                              __builtin_clzl(mshim_sizes[i] - 1) -
                              MSH_MIN_SIZE_SHIFT);
    mask_bits &= ~0x400;

    MSH_ADDRESS_RANGE_t mmar =
    {{
      .addr_mask = mask_bits,
      .addr_high = i << 10,
    }};
    cfg_wr(probe_state.mshims[i].word, 0, MSH_ADDRESS_RANGE, mmar.word);

    //
    // Stash size for the benefit of the mshim driver; eventually we may
    // want to put other data in here, like the number of DIMMs found.
    //
    cfg_wr(probe_state.mshims[i].word, 0, MSH_SCRATCHPAD, mshim_sizes[i]);
#else // USE_PCIE_AS_MEMORY
    //
    // Tell the hypervisor how much memory we have, and how it's mapped.
    // (We don't use this yet in the hypervisor, but we probably should.)
    //
    cfg_wr(probe_state.mshims[i].word, 0, TRIO_SCRATCHPAD,
           (mshim_sizes[i] >> 20) | ((mshim_bases[i] >> 20) << 32));
#endif // USE_PCIE_AS_MEMORY
  }

  boot_shim_size = mshim_sizes[0];
  boot_shim_base = mshim_bases[0];

  return (1);
}


/** Finish the boot process on the boot master tile.
 */
void
boot_master(uint64_t boot_cycle, uint32_t board_flags)
{

  // Wait for ACKs from all the tiles, so we know they're running.

  int nslaves = (lrhc.bits.x - ulhc.bits.x + 1) *
                (lrhc.bits.y - ulhc.bits.y + 1) - 1;

  while (nslaves)
  {
    hv_tag tag = { .word = idn0_receive() };
    (void) idn0_receive();
    if (tag.bits.type == BOOT_TAG_BOOT)
      nslaves--;
  }


  //
  // Probe the shims, looking for an rshim.  Note that config_shims also sets
  // up an mshim so it is ready to use when we turn off cache-as-RAM mode.
  //
  unsigned long shim_mask = 0;
  if (!config_shims(&shim_mask, &board_flags))
  {
    boot_printf("boot_panic: no working memory controller found\n");
    boot_error(BOOT_ERR_NO_MSHIM);
  }

  // The HV is loaded at the last page of the chosen mshim.
  PA hv_text_physmem = boot_shim_base + boot_shim_size - HV_CODE_PAGE_SIZE;

  //
  // Go load the hypervisor and then jump to it.
  //
  if (srom_addr != 0)
  {
    //
    // Load from the SPI ROM.
    //
    srom_set_addr(__insn_mfspr(SPR_RSHIM_COORD), srom_dev, srom_addr);
    load_and_go_srom(hv_text_physmem, _stext, _ebss, ulhc, lrhc, masteraddr,
                     rshimaddr, shim_mask, board_flags, srom_dev);
  }
  else
  {
    //
    // Load from the UDN.
    //
    load_and_go_net(hv_text_physmem, _stext, _ebss, ulhc, lrhc, masteraddr,
                    rshimaddr, shim_mask, board_flags);
  }

  //
  // We get here if the loaded hypervisor has a bad CRC.  We print a panic
  // message and either exit, or reboot, depending on whether the SROM booter
  // thinks there might be another image to try.
  //
  if (board_flags & BOARD_BADCRC_REBOOT)
  {
    boot_printf("boot: bad CRC for hypervisor image, will try alternate...\n");
    boot_flush_output();
    boot_reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
  }

  boot_printf("boot_panic: bad CRC for hypervisor image\n");

  boot_error(BOOT_ERR_HV_IMAGE_BAD_CRC);
}



/** Send an ACK.
 * @param dest Target tile.
 * @param msgtype Type of message.
 * @param ackval Value to send with the ACK.
 */
void
ack(pos_t dest, uint32_t msgtype, uint32_t ackval)
{
  //
  // Send the reply.
  //
  hv_tag tag =
  {
    .bits.len = 1,
    .bits.reply = 1,
    .bits.chan = POS2IDX(myaddr),
    .bits.type = msgtype,
  };

  idn_send(dest.word | 2);
  idn_send(tag.word);
  idn_send(ackval);
}




/** Finish the boot process on a non-boot master tile.
 */
void
boot_slave()
{
  //
  // Send an ACK to the master, so it knows we're running.
  //






  ack(masteraddr, BOOT_TAG_BOOT, 0);


  //
  // Now, wait for a command.
  //
  while (1)
  {
    union {
      struct boot_msg_execute execute;
      unsigned long words[HV_MAXMSGWDS];
    } arr;

    unsigned long* p = arr.words;















    hv_tag tag = { .word = idn0_receive() };
    uint64_t len = tag.bits.len;
    if (len > HV_MAXMSGWDS)
      boot_error(BOOT_ERR_MSG_TOO_BIG);
    pos_t sender = { .word = idn0_receive() };
    // Currently unused, so to prevent compiler complaints...
    (void) sender;
    while (len--)
      *p++ = idn0_receive();


    switch (tag.bits.type)
    {
    case BOOT_TAG_EXECUTE:
      {
        //
        // Configure our memory mapping registers, then jump to code in
        // physical memory.  Note that we don't send an ACK here; the
        // booted hypervisor will do so once it comes up.
        //
        struct boot_msg_execute *msg = &arr.execute;

        for (int i = 0;
             i < sizeof (msg->cbox_mmap) / sizeof (msg->cbox_mmap[0]); i++)
          if (msg->cbox_mmap[i] != CBOX_CONFIG_IGNORE)
            set_cbox_mmap_spr(i, msg->cbox_mmap[i]);
        if (msg->cbox_msr != CBOX_CONFIG_IGNORE)
          __insn_mtspr(SPR_CBOX_MSR, msg->cbox_msr);
        __insn_mtspr(SPR_MEM_STRIPE_CONFIG, msg->mem_stripe_config);
      
        // Get the physical base address of the text segment.
        PA hv_text_physmem = msg->paddr & (~((PA)HV_CODE_PAGE_SIZE - 1));
        go(hv_text_physmem, _stext, _ebss, ulhc, lrhc, masteraddr, msg->paddr);
      }
      break;

    default:
      // Unrecognized message; panic.
      boot_error(BOOT_ERR_UNRECOG_MSG);
    }
  }
}

static int uart_initialized = 0;

static unsigned long uart_chan = UART_CHANNEL;

/** Initialize the UART for console input and output.
 */
void
uart_init(uint32_t refclk, struct bi_console_cfg* cfg)
{
  if (cfg)
  {
    if (cfg->port)
      uart_chan = UART_CHANNEL_PORT1;

    //
    // If we set the baud rate to zero, UART output will hang forever, so
    // don't allow that.
    //
    long baud_rate = max(110, cfg->baud_rate);

    cfg_wr(rshimaddr.word, uart_chan, UART_DIVISOR,
           (refclk / 16) / baud_rate);
    cfg_wr(rshimaddr.word, uart_chan, UART_TYPE,
           (cfg->parity << UART_TYPE__PTYPE_SHIFT) |
           (cfg->data_bits << UART_TYPE__DBITS_SHIFT) |
           (cfg->stop_bits << UART_TYPE__SBITS_SHIFT));
  }
  else
  {
    cfg_wr(rshimaddr.word, uart_chan, UART_DIVISOR, (refclk / 16) /
           UART_SPEED);
    // Set for 8 bits, no parity, 1 stop bit
    cfg_wr(rshimaddr.word, uart_chan, UART_TYPE, 0);
  }

  uart_initialized = 1;
}


/** Write a character to the UART console.
 * @param c Character to write.
 */
static void
uart_putchar(char c)
{
  uint32_t status;

  if (!uart_initialized)
    return;

  if (c == '\n')
    uart_putchar('\r');

  do
    status = cfg_rd(rshimaddr.word, uart_chan, UART_FLAG);
  while (status & (UART_FLAG__WFIFO_FULL_MASK |
                   UART_FLAG__TFIFO_FULL_MASK));

  // Write character.
  cfg_wr(rshimaddr.word, uart_chan, UART_TRANSMIT_DATA, (uint32_t) c);
}


/** Get a character from the UART console, if one is available; if not, wait
 *  up to the specified timeout for one to show up.
 * @param msec Time to wait in milliseconds.
 * @return Character received, or -1 if none are available after the timeout
 *   period expires.
 */
static int
uart_getchar_timeout(int msec)
{
  int retval = -1;

  //
  // On Gx we assume that we just got reset and thus we're running at the
  // refclk frequency.  This means that this delay will be really long
  // on the FPGA.
  //
  uint_reg_t clk = __insn_mfspr(SPR_CYCLE) + msec * (REFCLK / 1000);

  do
  {
    if (!(cfg_rd(rshimaddr.word, uart_chan, UART_FLAG) &
          UART_FLAG__RFIFO_EMPTY_MASK))
    {
      retval = cfg_rd(rshimaddr.word, uart_chan, UART_RECEIVE_DATA);
      break;
    }
  }
  while (__insn_mfspr(SPR_CYCLE) <= clk);

  return (retval);
}

/** Take the UART out of protocol mode.
 * @return Cookie which can be passed to uart_restore_mode() to restore
 *  the previous protocol mode setting.
 */
static uint_reg_t
uart_exit_protocol_mode()
{
  uint_reg_t old_mode = cfg_rd(rshimaddr.word, uart_chan, UART_MODE);
  cfg_wr(rshimaddr.word, uart_chan, UART_MODE,
         UART_MODE__UART_MODE_MASK | UART_MODE__BYPASS_MASK);
  return old_mode;
}


/** Restore protocol mode state.
 * @param mode Cookie returned by uart_exit_protocol_mode().
 */
static void
uart_restore_mode(uint_reg_t mode)
{
  cfg_wr(rshimaddr.word, uart_chan, UART_MODE, mode);
}


/** Flush the UART (i.e., wait for any output characters to drain). */
void
uart_flush_output()
{
  uint_reg_t status;
  do
    status = cfg_rd(rshimaddr.word, uart_chan, UART_FLAG);
  while ((status & (UART_FLAG__WFIFO_EMPTY_MASK |
                    UART_FLAG__TFIFO_EMPTY_MASK)) !=
         (UART_FLAG__WFIFO_EMPTY_MASK | UART_FLAG__TFIFO_EMPTY_MASK));

  //
  // The last character doesn't seem to be completely done even when the
  // FIFOs are claimed to be empty, so delay a tiny bit longer.
  //
  uint64_t first = get_cycle_count();
  uint64_t last = first + 15000;  // About a character at 125 Mhz, 115200 baud
  while (get_cycle_count() < last)
    ;
}

/** If nonzero, emit console output on the UART. */
static int use_uart;
/** If nonzero, emit console output on the rshim. */
static int use_rshim;

/** Initialize the console.
 * @param refclk Reference clock speed.
 * @param cfg BIB console configuration item, or NULL.
 * @param board_flags Pointer to board flags to be updated, or NULL.
 */
void
console_init(uint32_t refclk, struct bi_console_cfg* cfg,
             uint32_t* board_flags)
{
  int use_tmf_con;
  uint_reg_t timeout_cycle;
  uint_reg_t timeout = cfg_rd(rshimaddr.word, 0, RSH_BREADCRUMB1) &
    CONS_RSHIM_BC1_DELAY_MASK;

  if (cfg && srom_addr)
    timeout = max(timeout, (int) cfg->early_console_delay);

  if (timeout == BI_CONSOLE_CFG_EARLY_CONSOLE_DELAY__VAL_FOREVER)
    timeout_cycle = ~(uint_reg_t) 0;
  else
    timeout_cycle = get_cycle_count() + refclk * timeout;

  do
    use_rshim = rshim_console_init(&use_tmf_con);
  while (!use_rshim && get_cycle_count() < timeout_cycle);

  if (use_rshim && board_flags)
    *board_flags |= BOARD_CONSOLE_RSHIM;

  if (use_tmf_con && board_flags)
    *board_flags |= BOARD_CONSOLE_TMFIFO;

  if (!use_rshim)
  {
    uart_init(refclk, cfg);
    use_uart = 1;
  }
}


/** Write a character to the boot console.
 * @param c Character to write.
 */
void
boot_putchar(char c)
{
  //
  // Typically we only want the character to go to either the UART or rshim
  // console, but having it go both places can be useful for debugging, so
  // we have 2 if's instead of an if/else.  To do that, hack the code
  // in console_init() to just comment out the if (!use_rshim) line.
  //
  if (use_uart)
    uart_putchar(c);

  if (use_rshim)
    rshim_putchar(c);
}


/** Get a character from the boot console, if one is available; if not, wait
 *  up to the specified timeout for one to show up.
 * @param msec Time to wait in milliseconds.
 * @return Character received, or -1 if none are available after the timeout
 *   period expires.
 */
int
boot_getchar_timeout(int msec)
{
  if (use_rshim)
    return rshim_getchar_timeout(msec);
  else
    return uart_getchar_timeout(msec);
}


/** Take the input stream out of protocol mode.
 * @return Cookie which can be passed to boot_restore_mode() to restore
 *  the previous protocol mode setting.
 */
uint_reg_t
boot_exit_protocol_mode()
{
  if (!use_rshim)
    return uart_exit_protocol_mode();

  return 0;
}


/** Restore protocol mode state.
 * @param mode Cookie returned by boot_exit_protocol_mode().
 */
void
boot_restore_mode(uint_reg_t mode)
{
  if (!use_rshim)
    uart_restore_mode(mode);
}


/** Flush the boot output stream (i.e., wait for any output characters to
 *  drain). */
void
boot_flush_output()
{
  if (use_uart)
    uart_flush_output();
  if (use_rshim)
    rshim_flush_output();
}


/** If nonzero, emit console output on the UART. */
/**
 * Reset the chip.
 * @param flags Soft reset flags for the SROM booter, or 0 if none.
 */
void
boot_reset_chip(unsigned long flags)
{
  pos_t rshimaddr = { .word = __insn_mfspr(SPR_RSHIM_COORD) };

  srom_set_addr(rshimaddr.word, srom_dev, 0);

  do_soft_reset(rshimaddr, flags);
}


/** Delay a number of microseconds.  This is a duplicate of drv_udelay()
 *  in drvintf.c, but that depends upon the global cpu_speed variable;
 *  more importantly, we don't want to have to compile that whole file
 *  for the booter just to get this one function (which is needed by
 *  some shared code in hw_config.c).
 * @param usec Number of microseconds to delay.
 */
void
drv_udelay(uint32_t usec)
{
  uint64_t end_time = get_cycle_count() +
                      (core_freq * usec + 1000000UL - 1) / 1000000UL;

  while (get_cycle_count() < end_time)
    ;
}
