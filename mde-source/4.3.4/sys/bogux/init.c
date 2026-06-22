/**
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
 *
 * Initial startup for Bogux
 * @file
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>

#include <hv/hypervisor.h>
#include <hv/pagesize.h>

#include <arch/interrupts.h>
#include <arch/sim.h>

#include "bogux.h"
#include "debug.h"
#include "files.h"
#include "mman.h"
#include "loader.h"
#include "syscall.h"
#include "tokenizer.h"
#include "messaging.h"
#include "mem_layout.h"
#include "devices.h"
#include "interrupt.h"


extern HV_PTE supervisor_page_table[HV_L1_ENTRIES];

char ts_cmdline_buf[1024] _TILESTATE;
char** ts_argv _TILESTATE;

static void
tokenize_argv(const char* cmdline, char* bytes, int bytemax)
{
  // Count how many tokens and chars we have
  TokenState ts = tokenizer_init(cmdline);
  int tokens = 0;
  int char_space = 0;
  while (!tokenizer_done(&ts))
  {
    char c = tokenizer_next(&ts);
    ++char_space;
    if (c == '\0')
      ++tokens;
  }

  // Allocate enough space for them.
  char_space = ROUND_UP(char_space, sizeof(int));
  int token_space = (tokens+1) * sizeof(char*);  // include NULL last pointer
  if (token_space + char_space > bytemax)
    panic("Initial command line too big for %d holding buffer", bytemax);
  char** token_ptr = (char**) bytes;
  char* char_ptr = bytes + token_space;

  // Write data into the buffer
  ts = tokenizer_init(cmdline);
  bool new_token = true;
  while (!tokenizer_done(&ts))
  {
    if (new_token)
      *token_ptr++ = char_ptr;
    char c = tokenizer_next(&ts);
    *char_ptr++ = c;
    new_token = (c == '\0');
  }
  *token_ptr = NULL;
}

/* Do the secondary tiles have their memory allocated yet?
 * The primary tile sets this after it has allocated tilestate and stack
 * memory for all the secondary tiles.
 *
 * The L2 alignment is not strictly necessary but avoids
 * "tile-sim --grind-coherence" warnings.
 */
static struct
{
  bool ready;
  char padding[CHIP_L2_LINE_SIZE() - sizeof(bool)];
} _mem_ready _L2ALIGNED;
#define mem_ready _mem_ready.ready

/** Tell the simulator (if there is one) about a new process. */
#define sim_notify_fork(fork_pid) \
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_FORK | \
               ((fork_pid) << _SIM_CONTROL_OPERATOR_BITS))

/** Tell the simulator (if there is one) we are running a different process. */
#define sim_notify_switch(pid) \
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_SWITCH | \
               ((pid) << _SIM_CONTROL_OPERATOR_BITS))

/** This is the first procedure called at its final, mapped address.
 * On entry, the zero pages used for starting up are still mapped
 * and need to be discarded.
 */
void
sv_go(HV_Topology topology)
{
  int tilenum = topology.coord.y * topology.width + topology.coord.x;

  // pt_mem[] holds allocated physical memory per-tile.
  assert(tilenum < MAX_TILES);
  static struct AllocMemory pt_mem[MAX_TILES];

  // The first tile initializes physical memory, then starts the others.
  if (!mem_ready)
  {
    // Make the mmap allocator aware of its physical memory.
    // We don't enable locking on the PA_Allocators yet.
    init_physmem(topology);

    // Allocate all the memory for page tables and tile state.
    int i = 0;
    HV_Coord coord;
    for (coord.y = 0; coord.y < topology.height; ++coord.y)
      for (coord.x = 0; coord.x < topology.width; ++coord.x, ++i)
        alloc_tile_mem(coord, &pt_mem[i], topology.coord);

    // Flush the pt_mem array back out to the memory controllers.
    for (int i = 0; i < sizeof(pt_mem); i += CHIP_L2_LINE_SIZE())
      __insn_flush((char*)pt_mem + i);

    // Initialize first tile's memory so he can init .locks easily.
    init_tile(topology.coord, &pt_mem[tilenum]);

    // Now that we've hit the PA allocator for each tile, we can
    // enable locking for all further PA allocation.  The only
    // restriction is that no tile may allocate before it sets
    // up its page table to include the .locks page.
    init_final();

    // Now that we have locking and tile-state up and ready, use
    // it globally.  Note that this does mean that the other tiles
    // will have a bogus stdout pointer until they get their per-tile
    // state mapped in at the end of init_tile().
    reset_stdout();

    // Mark memory as ready and flush so the other tiles see it; then wait
    // until that (and all of the other stuff we've modified) has hit the
    // memory controller.
    mem_ready = true;
    __insn_flush(&mem_ready);
    __insn_mf();

    // Start the other tiles.
    hv_start_all_tiles();
  }
  else
  {
    // Initialize virtual memory, etc., on this tile.
    // This removes our use of the initial supervisor_page_table/l2_page_table.
    init_tile(topology.coord, &pt_mem[tilenum]);
  }

  // Enable downcalls.
  __insn_mtspr(INTERRUPT_MASK_RESET_BX, INT_MASK(INT_INTCTRL_BX));

  // Make sure we can handle hypervisor messages
  init_messaging();

  // Each tile for now launches the same binary
  char cmdline[1024];
  unsigned int len = hv_get_command_line((HV_VirtAddr) cmdline, sizeof cmdline);
  if (len > sizeof cmdline)
    panic("Command line too long (%d bytes).", len);

  // If no user binary specified, just run the user idle loop.
  if (cmdline[0] == '\0')
    nap_user();

  // Tokenize argv.
  tokenize_argv(cmdline, ts_cmdline_buf, sizeof(ts_cmdline_buf));
  ts_argv = (char**) ts_cmdline_buf;

  //
  // Parse any of our parameters.  These come before the program name and take
  // the form "var=value".  The following parameters are accepted:
  //
  // crc=1           Emit CRCs of file output streams instead of the actual
  //                 output data.
  // debug=<flags>   Set Bogux debug word to <flags> (see debug.h for values).
  // oloc=<x>,<y>    When mmaping memory, remotely cache memory which would
  //                 normally be cached locally on tile (x,y) instead.
  // oloc=<seed>     When mmaping memory, remotely cache memory which would
  //                 normally be cached locally on a tile chosen pseudo-
  //                 randomly; <seed> seeds the per-tile pseudo-random
  //                 generator.
  // rerun=<n>       Re-run the command specified on our command line <n>
  //                 times.
  // seed=<n>        Set the seed for the /dev/random pseudo-random number
  //                 generator.  This is a per-tile value distinct from that
  //                 used with the oloc= parameter.
  // tile=<x>,<y>    Only execute the given user binary on this tile; the
  //                 others will nap.
  //
  bool tile_arg_seen = false;   // We got the "tile=" argument
  long tile_arg_x, tile_arg_y;  // Coordinates in the "tile=" argument

  while (*ts_argv)
  {
    if (strchr(*ts_argv, '=') == 0)
      break;

    if (!strcmp(*ts_argv, "crc=1"))
      do_filesums(true);
#ifdef DEBUG
    else if (!strncmp(*ts_argv, "debug=", 6))
    {
      long val;
      str2l(*ts_argv + 6, NULL, 0, &val);
      debug_flags = (uint32_t) val;
      printf("Debug flags set to %#x\n", debug_flags);
    }
#endif
    else if (!strncmp(*ts_argv, "oloc=", 5))
    {
      long valx, valy;
      char* x_stop;
      str2l(*ts_argv + 5, &x_stop, 0, &valx);
      if (*x_stop == ',')
      {
        str2l(x_stop + 1, NULL, 0, &valy);
        set_default_oloc((uint32_t) valx, (uint32_t) valy);
        printf("Will oloc memory to (%ld,%ld)\n", valx, valy);
      }
      else
      {
        set_random_oloc(valx);
        printf("Will oloc memory randomly, seed %ld\n", valx);
      }
    }
    else if (!strncmp(*ts_argv, "rerun=", 6))
    {
      long val;
      str2l(*ts_argv + 6, NULL, 0, &val);
      ts_rerun = (uint32_t) val;
      printf("Will rerun command %d times\n", ts_rerun);
    }
    else if (!strncmp(*ts_argv, "seed=", 5))
    {
      long val;
      str2l(*ts_argv + 5, NULL, 0, &val);
      ts_rand_seed = (uint32_t) val;
      printf("Random number seed set to 0x%x\n", ts_rand_seed);
    }
    else if (!strncmp(*ts_argv, "tile=", 5))
    {
      char* x_stop;
      str2l(*ts_argv + 5, &x_stop, 0, &tile_arg_x);
      if (*x_stop == ',')
      {
        str2l(x_stop + 1, NULL, 0, &tile_arg_y);
        tile_arg_seen = true;
      }
      else
        printf("Syntax error in tile= argument, ignored\n");
    }
    else
      printf("Unrecognized Bogux argument %s, ignored\n", *ts_argv);

    ts_argv++;
  }

  // Start up our device drivers.
  init_devices();

  if (tile_arg_seen &&
      (tile_arg_x != topology.coord.x || tile_arg_y != topology.coord.y))
    nap_user();

  // Tell the simulator, if there is one, about the phony process we
  // are creating. We have just one process per tile.
  int pid = tilenum + 100;
  sim_notify_fork(pid);
  sim_notify_switch(pid);

  load_and_run_default_program();
}


void
open_std_fds()
{
  // Open stdin, stdout, and stderr.
  // These are tile-specific for now.
  int fd0 = do_open("/dev/tty", O_RDONLY, 0);
  int fd1 = do_open("/dev/tty", O_WRONLY, 0);
  int fd2 = do_open("/dev/tty", O_WRONLY, 0);
  assert(fd0 == 0);
  assert(fd1 == 1);
  assert(fd2 == 2);
}


void
load_and_run_default_program()
{
  // Open the standard file descriptors.
  open_std_fds();
  // Load the binary which was specified on our command line; sv_go() has
  // already parsed that into ts_argv.
  ExecData data;
  int rc = build_exec_data(&data, false, false, ts_argv[0], ts_argv, NULL);
  if (rc < 0)
    panic("build_exec_data failed: %s: errno %d", ts_argv[0], -rc);
  rc = do_execve(&data);

  // We'll get here for a bad filename, file format, etc.
  panic("exec failed: %s: errno %d", ts_argv[0], -rc);
}
