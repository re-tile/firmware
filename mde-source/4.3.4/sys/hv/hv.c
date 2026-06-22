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
 * Hypervisor main program.
 */

#include <stdio.h>
#include <util.h>

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include <hv/hypervisor.h>

#include "client.h"
#include "client_obj.h"
#include "config.h"
#include "bme.h"
#include "console.h"
#include "cons_uart.h"
#include "debug.h"
#include "devices.h"
#include "fastio.h"
#include "filesys.h"
#include "hv.h"
#include "hvgdb.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "idn.h"
#include "mapping.h"
#include "misc.h"
#include "msg.h"
#include "mshim_acc.h"
#include "post/post_ram.h"
#include "srom_acc.h"
#include "tile.h"
#include "tsb.h"

#include "cons_rshim.h"

#if !defined(__LP64__)
# error Must not specify -m32 when building the TILE-Gx hypervisor
#endif

Lotar my_lotar;
PA my_text_pa;
PA my_data_pa;

pos_t my_pos;

pos_t chip_lrhc;
pos_t chip_master;
pos_t chip_console;

pos_t chip_logical_ulhc;
pos_t chip_logical_lrhc;

pos_t grid_ulhc;
pos_t grid_lrhc;

pos_t client_ulhc;
pos_t client_lrhc;
tile_mask client_tiles;

int is_master;
int is_dedicated;
int post_is_thorough;

uint64_t init_cycle_count;

uint32_t cpu_speed _SHARED = INIT_CPU_SPEED;

uint32_t refclk_speed;
uint32_t board_flags;

size_t page_size_small = HV_DEFAULT_PAGE_SIZE_SMALL;
size_t page_size_large = HV_DEFAULT_PAGE_SIZE_LARGE;
int page_shift_small = HV_LOG2_DEFAULT_PAGE_SIZE_SMALL;
int page_shift_large = HV_LOG2_DEFAULT_PAGE_SIZE_LARGE;
size_t page_size_jumbo = HV_DEFAULT_PAGE_SIZE_JUMBO;
int page_shift_jumbo = HV_LOG2_DEFAULT_PAGE_SIZE_JUMBO;

int is_gx72;

// This stuff is normally in bme.c, but we don't yet build BME for Gx;
// remove these when we do.
PA shared_lock_page_pa;
Lotar shared_lock_page_lotar;

//
// Initial console access is via static network (on master), null console
// (on slaves).  Later, the master tile switches to the UART.  Finally, the
// designated console tile switches to the UART, and the remaining slaves
// and the master switch to the remote console.  stdin/out are used for
// I/O from the hypervisor itself, and client_stdin/out are used for I/O
// from a client supervisor.
//
FILE *stdout = &null_out;        /**< Standard output file */
FILE *stdin = &null_in;          /**< Standard input file */
FILE *client_stdout = &null_out; /**< Client standard output file */
FILE *client_stdin = &null_in;   /**< Client standard input file */

/** Set up [client_]std{in, out}.  Can't be called until the configuration
 *  has been parsed.
 */
static void
setup_stdio(uint32_t board_flags)
{
  if (my_pos.word == chip_console.word)
  {
    stdout = &switch_out_onlcr;
    stdin = &switch_in;

    if (config.nregclients > 1)
    {
      client_stdout = &client_out;
      client_stdin = &client_in;
      client_outdev = &switch_out;
      client_indev = &switch_in;
    }
    else
    {
      client_stdout = client_outdev = &switch_out;
      client_stdin = client_indev = &switch_in;
    }
  }
  else
  {
    stdout = &remote_out;
    stdin = &remote_in;
    client_stdout = &remote_client_out;
    client_stdin = &remote_client_in;
  }
}


/** Hypervisor main program.
 * @param text_pa Physical address of hypervisor text.
 * @param data_pa Physical address of this tile's hypervisor data.
 * @param ulhc Coordinate of chip's upper-left-hand-corner tile.
 * @param lrhc Coordinate of chip's lower-right-hand-corner tile.
 * @param master Coordinate of boot master tile.
 * @param rshim Coordinate of rshim tile. (Only passed to boot tile.)
 * @param shim_mask Mask of shims which were found to be present during boot
 *        probe. (Only passed to boot tile.)
 * @param boot_board_flags Board flags found by the booter (BOARD_xxx).
 *        (Only passed to boot tile).
 * @return 1 if hypervisor exits.
 */
int
hv(PA text_pa, PA data_pa, pos_t ulhc, pos_t lrhc, pos_t master, pos_t rshim,
   unsigned long shim_mask, uint32_t boot_board_flags)
{
  struct slave_tile_state sts = { 0 };
  int srom_hvfs_addr = 0;

  init_cycle_count = get_cycle_count();

  my_lotar = (__insn_mfspr(SPR_TILE_COORD) >> 7) & 0x003FFFFF;
  my_text_pa = text_pa;
  my_data_pa = data_pa;

  my_pos.word = __insn_mfspr(SPR_TILE_COORD) & 0x1FFFFF80;
  chip_lrhc = lrhc;
  chip_master = master;

  is_master = (my_pos.word == chip_master.word);

  //
  // The master (initial boot) tile and the slave tiles take different
  // paths through this routine.  Here's a brief chronology:
  //
  // - The master tile enters main, and executes all of its code down to
  //   the very bottom where it calls init_slaves().  In some cases,
  //   the routines it calls save some of their internal state in the
  //   slave_tile_state structure, to be used later.
  //
  // - In init_slaves(), the master sends two requests to the booter
  //   on the slaves, telling them to (a) configure their memory mapping
  //   registers and then (b) jump to the hypervisor code.  Upon processing
  //   the first request, the slaves send their first acknowledgement to
  //   the master.  Upon receiving the second request, they jump to the
  //   hypervisor, and fairly soon, they call slave_init().
  //
  // - In slave_init(), the slaves send their second acknowledgement to
  //   the master (still in init_slaves()), telling it that they've
  //   initialized.
  //
  // - Once the master knows a slave is in the hypervisor, it sends a
  //   third request, giving the slave the state information from the
  //   slave_tile_state structure.
  //
  // - Upon receipt of this message, the slave returns from init_slave,
  //   having filled in the slave_tile_state structure, and then
  //   does the remainder of the initialization in this routine.
  //   Where appropriate, some of the init_xxx routines will just take
  //   data from slave_tile_state instead of actually doing anything.
  //
  // - At the end of main(), the slaves enter slave_idle(), where they
  //   send their third acknowledgement to the master, then wait for a
  //   request to start a client.
  //
  // - Having collected all of the acknowledgements from the slaves,
  //   the master now returns from init_slaves().
  //
  // - The master does some client initialization, then calls
  //   start_client(), which loads the client locally, then sends a
  //   fourth and final request to the slaves to start it there.
  //
  // - The slaves send their fourth and final acknowledgement to the master,
  //   then start the client.
  //

  debug_init(0);

  INIT_TRACE("init_idn\n");
  init_idn();

  INIT_TRACE("init_map_remote\n");
  init_map_remote();

  INIT_TRACE("probe_ipic\n");
  probe_ipic();

  if (is_master)
  {
    sts.shim_mask = shim_mask;
    sts.board_flags = board_flags = boot_board_flags;
    sts.rshim = rshim;
    refclk_speed = (board_flags & BOARD_100MHZ_REFCLK) ? 100000000 : 125000000;
    sts.refclk_speed = refclk_speed;

    //
    // If we're booting from the SROM via the new SROM booter, we want to
    // save the current position as soon as possible so that we don't
    // mess it up before we read in the HVFS.
    //
    if (board_flags & BOARD_SROM_HVFS)
    {
      int srom_dev = srom_get_dev(rshim, SROM_CHAN);
      srom_hvfs_addr = srom_get_bootstream_addr(rshim, SROM_CHAN, srom_dev);
    }

    //
    // Enable console output.  Note that we don't enable UART console input
    // until later, when we turn on UART interrupt processing.
    //
    if (rshim.word != ~0)
    {
      if (board_flags & BOARD_CONSOLE_RSHIM)
      {
        rshim_console_init(NULL);
        stdout = &rshim_out_onlcr;
      }
      else
      {
        init_uart_console(rshim.word, 1, 0,
                          (board_flags & BOARD_CONSOLE_UART1) ? 1 : 0);
        stdout = &uart_out_onlcr;
      }
    }

    chip_logical_lrhc = chip_lrhc;
    chip_logical_ulhc = chip_ulhc;




    if (board_flags & BOARD_TILEPRO36)
    {
      int max_x = chip_ulhc.bits.x + 5;
      chip_logical_lrhc.bits.x = chip_lrhc.bits.x > max_x ? max_x :
        chip_lrhc.bits.x;
      int max_y = chip_ulhc.bits.y + 5;
      chip_logical_lrhc.bits.y = chip_lrhc.bits.y > max_y ? max_y :
        chip_lrhc.bits.y;
    }

    RSH_FABRIC_DIM_t grid_size =
      { .word = cfg_rd(rshim.word, 0, RSH_FABRIC_DIM) };
    grid_ulhc.bits.x = 0;
    grid_ulhc.bits.y = 0;
    grid_lrhc.bits.x = grid_size.dim_x - 1;
    grid_lrhc.bits.y = grid_size.dim_y - 1;

    RSH_REV_ID_t rev_id = 
      { .word = cfg_rd(rshim.word, 0, RSH_REV_ID) };
    is_gx72 =
      (rev_id.chip_rev_id & 0xF0) == RSH_REV_ID__CHIP_REV_ID_VAL_TILEGX72;

    sts.chip_logical_ulhc = chip_logical_ulhc;
    sts.chip_logical_lrhc = chip_logical_lrhc;
    sts.grid_ulhc = grid_ulhc;
    sts.grid_lrhc = grid_lrhc;

    tprintf("Tilera Hypervisor, %s\n", hv_version);
  }
  else
  {
    slave_init(&sts);
    refclk_speed = sts.refclk_speed;
    board_flags = sts.board_flags;
    chip_logical_ulhc = sts.chip_logical_ulhc;
    chip_logical_lrhc = sts.chip_logical_lrhc;
    grid_ulhc = sts.grid_ulhc;
    grid_lrhc = sts.grid_lrhc;
    rshim = sts.rshim;
    shared_lock_page_pa = sts.shared_lock_pa;
    shared_lock_page_lotar = sts.shared_lock_lotar;
  }

  if (board_flags & BOARD_POST_THOROUGH)
    post_is_thorough = 1;

  INIT_TRACE("fastio_init\n");
  fastio_init();

  INIT_TRACE("init_local_alloc\n");
  init_local_alloc();

  // We have to probe devices before we initialize the PA allocator,
  // since probing the mshims tells us how much memory is available.  Also,
  // this makes parsing the devices section of the config file easier.
  INIT_TRACE("probe_devices\n");
  probe_devices(sts.shim_mask, rshim);

  // The VA and PA allocators have to be set up before hvgdb initialization.
  INIT_TRACE("init_va_pa_alloc\n");
  init_va_pa_alloc();

#if HV_PL == 2
  // We currently only support HVGDB when the HV is at PL2.
  if (is_master)
    sts.hvgdb_pa = hvgdb_init2(0);
  else
    hvgdb_init2(sts.hvgdb_pa);
#endif

  // Using any shared data will use the TSB, so it must be initialized
  // before we initialize the shared allocator.
  INIT_TRACE("init_tsb\n");
  init_tsb();

  INIT_TRACE("init_fs\n");
  init_fs(&sts, srom_hvfs_addr, rshim);

  //
  // On the master, parse_config() sets chip_console, but on the slaves, we
  // need to set it before calling parse_config().
  //
  if (!is_master)
    chip_console = sts.chip_console;

  INIT_TRACE("parse_config\n");
  parse_config();

  //
  // Patch various bits of code to enable statistics collection, if that
  // was requested in the config file.  We only have to do this once,
  // since code is shared between all tiles and we do this before anyone
  // but the boot master tile is out of the booter.
  //
  if (is_master && config.stats)
  {
    INIT_TRACE("patching to enable stats\n");
    patch(patch_table_stats);
  }

  if (is_master)
    sts.chip_console = chip_console;

  // setup_misc() may depend upon config file input, so run it as soon as
  // we can after we've parsed the configuration.
  INIT_TRACE("setup_misc\n");
  setup_misc();

  // The shared allocator uses data from the hvconfig in making its
  // decision about how to home shared space, so it must be initialized
  // after the config is parsed.
  INIT_TRACE("init_shared_alloc\n");
  init_shared_alloc(&sts);

  // Load the board information block; can't be done until the shared
  // allocator is available.
  INIT_TRACE("bi_load\n");
  bi_load();

  // Print out the configuration version, if there was one in the config file.
  if (config.config_ver.len > 0)
  {
    char config_ver[1024];

    int len = config.config_ver.len;
    if (len > sizeof (config_ver) - 1)
      len = sizeof (config_ver) - 1;

    fs_pread(config.config_ver.ino, config_ver, len, config.config_ver.off);
    config_ver[len] = '\0';

    tprintf("%s\n", config_ver);
  }

#if 0
  if (is_master)
    dump_client_config();
#endif

  INIT_TRACE("debug_init\n");
  debug_init(1);

  // Need to do this before initializing the drivers so the interrupt table
  // is set up, but after config so we know the size of the message table.
  INIT_TRACE("init_msg\n");
  init_msg();

  INIT_TRACE("Master tile (%d, %d), ULHC (%d, %d), LRHC (%d, %d)\n",
             chip_master.bits.x, chip_master.bits.y,
             chip_ulhc.bits.x, chip_ulhc.bits.y,
             chip_lrhc.bits.x, chip_lrhc.bits.y);

  determine_client_geometry(&client_ulhc, &client_lrhc, &client_tiles);

  INIT_TRACE("Client geometry: ULHC (%d, %d), LRHC (%d, %d)\n",
             client_ulhc.bits.x, client_ulhc.bits.y,
             client_lrhc.bits.x, client_lrhc.bits.y);

  INIT_TRACE("config_client_geom\n");
  configure_client_geometry(client_ulhc.bits.x, client_ulhc.bits.y,
                            client_lrhc.bits.x - client_ulhc.bits.x + 1,
                            client_lrhc.bits.y - client_ulhc.bits.y + 1,
                            &client_tiles);

  // Allow Linux to oloc memory to BME tiles.
  for (int ci = 0; ci < config.nclients; ci++)
    if (config.clients[ci].flags & CLIENT_BME)
      allow_client_pte_lotar_tile_mask(&config.clients[ci].tiles);

  INIT_TRACE("Client tiles:\n");
#ifdef DEBUG
  if (debug_flags & DEBUG_INIT)
    dump_tile_mask(&client_tiles);
#endif

  INIT_TRACE("config_client_asid\n");
  configure_client_asids(1, 255);


  {
    //
    // Compute the set of home map tiles, turn that into the actual map,
    // and program the map into the tile.
    //
    tile_mask client_home_map_tiles;
    uint32_t home_map[CHIP_CBOX_HOME_MAP_SIZE()];

    INIT_TRACE("determine_client_home_map_tiles\n");
    determine_client_home_map_tiles(&client_home_map_tiles);

    INIT_TRACE("Client home map tiles:\n");
#ifdef DEBUG
    if (debug_flags & DEBUG_INIT)
      dump_tile_mask(&client_home_map_tiles);
#endif

    INIT_TRACE("configure_tile_home_mask\n");
    configure_tile_home_mask(&client_home_map_tiles);

    INIT_TRACE("translate client_home_map\n");
    mask_to_home_map(&client_home_map_tiles, home_map);

    INIT_TRACE("configure_tile_home_map\n");
    configure_tile_home_map(home_map);
  }

  INIT_TRACE("set_intrs\n");
  set_intrs();

  if (is_master)
  {
    //
    // This actually configures the mshims, so we only want to do it on the
    // master tile.  We do it before we call device init routines in case
    // they feel like actually doing DMA.
    //
    INIT_TRACE("init_device_memory_regs\n");
    init_device_memory_regs();

    //
    // Initialize any board or chip resources (say, clock speed, or reset
    // of random peripherals that don't have their own drivers).
    //
    setup_board();

    //
    // Initialize the memory for the lock that controls access to resources
    // shared between the hypervisor and BME (JTAG, MDIO).  Note that we
    // need to have this set up before we initialize the device drivers,
    // since some of them need those resources and will be using that lock.
    //
    INIT_TRACE("init_shared_lock\n");
    init_shared_lock(&shared_lock_page_pa, &shared_lock_page_lotar);
    sts.shared_lock_pa = shared_lock_page_pa;
    sts.shared_lock_lotar = shared_lock_page_lotar;
  }
  else
  {
    //
    // Let's enable console output so that we can get printfs in driver init
    // routines, among other places.
    //
    // Note that we do this on all tiles, not just the console master tile,
    // so that we can do UART output from any tile in some unusual cases
    // (e.g., debugging the console-over-tmfifo code).  This works since
    //  we aren't reinitializing the UART hardware.
    //
    if (rshim.word != ~0)
      init_uart_console(rshim.word, 0, 0,
                        (board_flags & BOARD_CONSOLE_UART1) ? 1 : 0);

    setup_stdio(board_flags);
  }

  // Now that we've read the config file we can initialize the drivers.
  INIT_TRACE("init_drivers\n");
  init_drivers();

  if (is_master)
  {
    //
    // Now that we've read all of the boot stream, and initialized the
    // drivers, we can turn off the rshim console.  Note that we always
    // turn it off, even if we don't think it's enabled; it's possible that
    // the host turned it on after we started booting, and if we don't turn
    // it off the host will keep polling.  This is particularly problematic
    // if we then go to use the tile-monitor FIFO console, since the host
    // may not be listening to it yet.
    //
    if (board_flags & BOARD_CONSOLE_RSHIM)
    {
      rshim_console_fini(1);
      //
      // If we were booting over the UART itself, we need to give the host
      // time to reconfigure its serial port before we reconfigure ours and
      // start writing to it; wait a couple of seconds first.  Note that we
      // always configure the UART, even if we're going to use the TM FIFO
      // console, because we might want to use it for debugging the tmfifo
      // driver.
      //
      if (!(board_flags & BOARD_CONSOLE_TMFIFO))
        drv_udelay(2 * 1000 * 1000);
      init_uart_console(rshim.word, 1, 1,
                        (board_flags & BOARD_CONSOLE_UART1) ? 1 : 0);
      stdout = &switch_out_onlcr;
    }
    else
      rshim_console_fini(0);
  }

  INIT_TRACE("set_error_enable\n");
  set_error_enable();

  INIT_TRACE("setup_networks\n");
  setup_networks();

  if (rshim.word != ~0 && my_pos.word == chip_console.word)
  {
    //
    // Enable UART interrupt mode, and UART console input.  Note that this
    // also takes the UART out of protocol mode (or puts it into protocol
    // mode if the debug flag is set).  We don't do that before enabling
    // interrupts, since if we did, there'd be a window where we had disabled
    // protocol mode and would have no way to turn it back on, making it
    // impossible to debug that part of the hypervisor startup sequence.
    //
    INIT_TRACE("enable_uart_intr\n");
    enable_uart_intr();
    if (config.nregclients > 1)
      cons_alloc_output_buffers();
    // If we're the master we haven't set up our files yet.
    if (is_master)
      setup_stdio(board_flags);
  }

  if (is_master)
  {
    INIT_TRACE("init_memory_regs\n");
    init_memory_regs();

    INIT_TRACE("init_slaves\n");

    // Compute which tiles need to be started.
    tile_mask slave_tiles;
    init_tile_mask(&slave_tiles, chip_ulhc, chip_lrhc);
    del_tile_mask(&slave_tiles, my_pos);

    // If we're not the console tile, start it, then point our console at it.
    if (my_pos.word != chip_console.word)
    {
      // We want to home shared memory on the console tile.  However, we
      // initially homed it locally; we can't home on a tile until it's out
      // of the booter, since we could evict boot code or data.  Now that
      // we're starting the console tile, we will rehome shared memory
      // there.  We must do the rehoming before starting the console tile,
      // so that it will see correct values for any items already in shared
      // memory.  That means that we cannot touch any shared memory between
      // the time that rehome_shared() returns and the following
      // init_slaves() returns.
      rehome_shared();

      // Start just the one tile.
      tile_mask console_tile_mask;
      clear_tile_mask(&console_tile_mask);
      add_tile_mask(&console_tile_mask, chip_console);

      init_slaves(&sts, &console_tile_mask);

      // Note that we don't need to start that tile below.
      del_tile_mask(&slave_tiles, chip_console);

      // Re-set up our files now that we aren't running the console.
      setup_stdio(board_flags);
    }

    // Start all of the remaining tiles.
    init_slaves(&sts, &slave_tiles);

    //
    // Split up the available memory between all clients, based on their
    // requests.  Keep track of what's being used so we can only test the
    // memory which people care about.
    //
    INIT_TRACE("determine client memory\n");

    PA avail_pa[MAX_MSHIMS] = { 0 };    // Base of available region
    PA avail_len[MAX_MSHIMS] = { 0 };   // Length of available region
    PA test_pa[MAX_MSHIMS] = { 0 };     // Base of region we'll test
    PA test_len[MAX_MSHIMS] = { 0 };    // Length of region we'll test

    //
    // First figure out what's available.
    //
    avail_len[0] = test_len[0] = avail_phys(page_size_large);
    avail_pa[0] = test_pa[0] = mshim_bases[0];

    for (int i = 1; i < MAX_MSHIMS; i++)
    {
      if (mshims[i])
      {
        avail_len[i] = test_len[i] = mshim_sizes[i];
        avail_pa[i] = test_pa[i] = mshim_bases[i];
      }
    }

    //
    // Make a pass through all clients to allocate the explicit requests, and
    // to count the number of default requests.  We round up to the size of a
    // large page since Linux gets confused if things aren't aligned that way.
    //
    int default_count[MAX_MSHIMS] = { 0 };
    
    for (int ci = 0; ci < config.nclients; ci++)
      for (int i = 0; i < MAX_MSHIMS; i++)
        if (config.clients[ci].req_mem_len[i] != CLIENT_MEM_DEFAULT)
        {
          config.clients[ci].req_mem_len[i] =
            ROUND_UP(config.clients[ci].req_mem_len[i], page_size_large);
          if (config.clients[ci].req_mem_len[i] < avail_len[i])
            config.clients[ci].mem_len[i] = config.clients[ci].req_mem_len[i];
          else
            config.clients[ci].mem_len[i] = avail_len[i];
          avail_len[i] -= config.clients[ci].mem_len[i];
        }
        else
          default_count[i]++;

    //
    // Now figure out what's left over after those requests.  Again, for
    // Linux's benefit, we make sure things are an even multiple of a large
    // page; this time we round down so that we know we have enough available
    // to satisfy all requests.
    //
    PA default_len[MAX_MSHIMS] = { 0 };

    for (int i = 0; i < MAX_MSHIMS; i++)
      if (default_count[i])
        default_len[i] = ROUND_DN(avail_len[i] / default_count[i],
                                  page_size_large);

    //
    // Now do the actual assigment of base PAs, and the length assignment for
    // the default requests.  We do this in two passes, doing the BME clients
    // first, since we guarantee that BME clients get the lowest available
    // physical addresses.  Since we've already computed a set of lengths for
    // everybody which fit into the available amount this isn't unfair to the
    // regular clients.
    //
    for (int ci = 0; ci < config.nclients; ci++)
      if (config.clients[ci].flags & CLIENT_BME)
        for (int i = 0; i < MAX_MSHIMS; i++)
        {
          if (config.clients[ci].req_mem_len[i] == CLIENT_MEM_DEFAULT)
          {
            config.clients[ci].mem_len[i] = default_len[i];
            avail_len[i] -= config.clients[ci].mem_len[i];
          }
          config.clients[ci].mem_base[i] = avail_pa[i];
          avail_pa[i] += config.clients[ci].mem_len[i];
        }

    for (int ci = 0; ci < config.nclients; ci++)
      if (!(config.clients[ci].flags & CLIENT_BME))
        for (int i = 0; i < MAX_MSHIMS; i++)
        {
          if (config.clients[ci].req_mem_len[i] == CLIENT_MEM_DEFAULT)
          {
            config.clients[ci].mem_len[i] = default_len[i];
            avail_len[i] -= config.clients[ci].mem_len[i];
          }
          config.clients[ci].mem_base[i] = avail_pa[i];
          avail_pa[i] += config.clients[ci].mem_len[i];
        }

    //
    // We don't need to test anything that's still available, since nobody's
    // going to use it.
    //
    for (int i = 0; i < MAX_MSHIMS; i++)
      test_len[i] -= avail_len[i];

    //
    // Run the memory test if post is thorough.  This will disable failing
    // shims and zero out their lengths in the test_len[] array.  Once it
    // returns, if there were failures, then zero out the corresponding
    // memory lengths in the clients.  Finally, if any client has no memory,
    // panic.
    //
    if (post_is_thorough)
    {
      INIT_TRACE("test_memory\n");
      if (test_memory(test_pa, test_len, 0))
      {
        board_flags |= BOARD_POST_FAILURE;

        for (int ci = 0; ci < config.nclients; ci++)
          for (int i = 0; i < MAX_MSHIMS; i++)
            if (test_len[i] == 0)
              config.clients[ci].mem_len[i] = 0;
      }
    }

    for (int ci = 0; ci < config.nclients; ci++)
    {
      PA total_mem = 0;

      for (int i = 0; i < MAX_MSHIMS; i++)
        total_mem += config.clients[ci].mem_len[i];

      if (total_mem == 0)
        panic("no functional memory available for client at line %d",
              config.clients[ci].lineno);
    }

    //
    // We've run all the POST tests we're going to run.  If all was well,
    // turn off the FAIL LED.
    //
    if (!(board_flags & BOARD_POST_FAILURE))
      douse_fail_led();

    //
    // Enable client MMIO access to the IPI shims prior to starting clients.
    //
    if (is_master)
      setup_client_ipi_access();

    //
    // Start the dedicated tiles.
    //
    INIT_TRACE("start_dedicated_tiles\n");
    start_dedicated_tiles();

    //
    // Now we start one tile for each client, which will load the client and
    // then start its other tiles as needed.
    //
    INIT_TRACE("starting first tile on each client\n");
    for (int ci = 0; ci < config.nclients; ci++)
      if (config.clients[ci].start_tile.word != my_pos.word &&
          !tile_mask_is_empty(&config.clients[ci].tiles))
        start_slave_client(config.clients[ci].start_tile, ci);

    //
    // We configure client physmem in the case where we're the start tile
    // for a particular client, as well as in the case where we aren't.  It
    // turns out to be redundant if we are a part of a client, and we later
    // get a start client message, since the configuration will be re-done
    // in that case.  However, if we're a dedicated tile, it's important
    // that we have a valid physmem config; the PCIe driver, in particular,
    // depends upon this working.  (This means that when we start assigning
    // devices to clients, we need to make sure that a dedicated tile for a
    // device is assigned to the client which owns that device.)
    //
    configure_client_physmem(config.clients[my_client].mem_base,
                             config.clients[my_client].mem_len);

    if (config.clients[my_client].start_tile.word == my_pos.word)
    {
      INIT_TRACE("load_and_start_client\n");
      load_and_start_client();
    }
    else
    {
      INIT_TRACE("call_driver_service\n");
      call_driver_service();
      INIT_TRACE("wait_for_start_client\n");
      wait_for_start_client();
    }
  }
  else
    slave_idle();

  // Should only get here if start_client() failed

  exit(0);
}
