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
 * Bare Metal Environment runtime entry point.
 */
#include <stdlib.h>
#include <stdio.h>
#include <util.h>
#include <string.h>

#include <arch/idn.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include <bme/tlb.h>
#include <bme/sys_info.h>

#include <tmc/spin.h>
#include <tmc/mspace.h>

#include "bme_state.h"
#include "misc.h"
#include "tokenizer.h"

/** The reference to this symbol links in our default set of interrupt
 *  vectors. */
extern char interrupt_vectors[];
static char* load_interrupt_vectors __attribute__((used)) = interrupt_vectors;

/** Pointer to heap mspace, for bme_malloc/free. */
extern tmc_mspace bme_heap_mspace[BME_MAX_TILES];

#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
/** List of physical addresses to use when doing the fence_incoherent()
 * operation; copied from the global info struct, and placed in static data to
 * make life easier for _bme_mem_sys_fence_incoherent.  This is read-only data
 * once it's initialized, and will take the same values on all tiles, so the
 * fact that it may end up being shared is OK. */
PA _bme_fence_incoherent_pas[BME_MAX_MSHIMS + 1];
#endif

/** Initialize the BME runtime, and call the user application.
 * @param local_info Pointer to our per-tile local information structure which
 *        we got from the hypervisor.
 */
void
_bme_start(struct bme_local_info_t* local_info)
{
#if 0
  sim_set_tracing(SIM_TRACE_CYCLES | SIM_TRACE_DISASM |
                  SIM_TRACE_REGISTER_WRITES);
#endif

  //
  // Get rid of any remaining hypervisor mappings by removing any unwired
  // entries from the TLBs.
  //
  bme_clean_itlb(0);
  bme_clean_dtlb(0);

  //
  // Create a per-tile structure which will be used by the rest of the
  // runtime.  This is really only needed in the shared data model, but we use
  // it everywhere for simplicity.  We're just going to allocate it on our
  // stack; since we never return, that space will never be reclaimed.
  //
  _bme_state_t state;
  memset(&state, 0, sizeof (state));
  _bme_set_state(&state);
  state.local_info = local_info;

  //
  // Map the global info structure and compute some useful stuff for the state
  // structure from it.
  //
  bme_global_info_t* global_info = bme_map_global_info();

#if 0
  sim_dump(SIM_DUMP_ITLB | SIM_DUMP_DTLB);
#endif

  //
  // Walk the tile table, count the number of tiles in our client, and save
  // in our state structure for later use.  Also compute our tile ordinal.
  //
  bme_tile_info_t* p = global_info->tile_table;
  int my_client = global_info->tile_table[local_info->index].client_num;
  for (int i = 0; i < global_info->num_tiles; i++, p++)
  {
    if (i == local_info->index)
      state.ordinal = state.num_tiles;
    if (p->client_num == my_client)
      state.num_tiles++;
  }

  //
  // Set various output prefixes.
  //
  pos_t my_pos = { .word = __insn_mfspr(SPR_TILE_COORD) };
  snprintf(state.tprintf_prefix, sizeof (state.tprintf_prefix) - 1,
           "(%d,%d) ", my_pos.bits.x, my_pos.bits.y);
  snprintf(state.panic_prefix, sizeof (state.panic_prefix) - 1,
           "(%d,%d) bme_panic: ", my_pos.bits.x, my_pos.bits.y);
  tprintf_prefix = state.tprintf_prefix;
  panic_prefix = state.panic_prefix;

  // Check the version of this BME client against the version of BME software
  // that was used in compiling the hypervisor (which has provided libraries
  // and data structures).  Print a warning and continue booting.  Eventually
  // we may want to change this to a panic, or make whether to panic something
  // we can specify in an .hvc file.
  // 
  if (BME_VERSION_MAJOR(global_info->hv_interface_version) !=
      BME_CURRENT_VERSION_MAJOR)
  {
    printf("Warning: This BME client was compiled with the hypervisor "
           "associated with BME version %d.%d, but is being used against "
           "BME version %d.%d.  The system is probably unstable.",
           BME_CURRENT_VERSION_MAJOR, BME_CURRENT_VERSION_MINOR,
           BME_VERSION_MAJOR(global_info->hv_interface_version),
           BME_VERSION_MINOR(global_info->hv_interface_version));
  }

  // 
  // Synchronize with other tiles in this BME application via the
  // scratchpad in the global information structure.  We rely on the
  // fact the TMC_SPIN_BARRIER_ALL is 0 - the scratchpad is zero
  // initialized and there is no safe place to call the official
  // tmc_spin_barrier_init() function.
  //
  tmc_spin_barrier_t* spinbar = (tmc_spin_barrier_t*)global_info->scratchpad;
  tmc_spin_barrier_wait(spinbar);

  //
  // Parse our command line, from the global information structure, into a set
  // of argv strings.  It would be nice to do this in a function, but since
  // we're just stuffing things onto the stack with alloca, we need to do it
  // inline.  First we make a pass through to count how many tokens and chars
  // we have.
  //
  TokenState ts = tokenizer_init(global_info->bme_app_args);
  int tokens = 0;
  int char_space = 0;
  while (!tokenizer_done(&ts))
  {
    char c = tokenizer_next(&ts);
    ++char_space;
    if (c == '\0')
      ++tokens;
  }

  //
  // Now we alloca() enough space for them.
  //
  // Include space for the last NULL pointer in argv.
  int token_space = (tokens + 1) * sizeof (char*);
  char* char_ptr = alloca(char_space);
  char** token_ptr = alloca(token_space);

  int argc = tokens;
  char **argv = token_ptr;

  //
  // Finally we parse the string again and copy data into the buffers.
  //
  ts = tokenizer_init(global_info->bme_app_args);
  int new_token = 1;
  while (!tokenizer_done(&ts))
  {
    if (new_token)
      *token_ptr++ = char_ptr;
    char c = tokenizer_next(&ts);
    *char_ptr++ = c;
    new_token = (c == '\0');
  }
  *token_ptr = NULL;

  //
  // Copy data needed by the fence_incoherent operation.
  //
#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
  memcpy(_bme_fence_incoherent_pas, global_info->fence_incoherent_pas,
         sizeof (_bme_fence_incoherent_pas));
#endif

  //
  // Copy data needed by the L2 cache flush routine.
  //
  state.flush_va = global_info->flush_va;
  state.flush_pa = global_info->flush_pa;
  state.flush_ps = global_info->flush_ps;
  state.flush_offset = global_info->flush_offset;

  //
  // Initialize the console.  Uses the global info struct, so must be done
  // while it's still mapped.
  //
  if (stdout->ops->init)
    stdout->ops->init(stdout);
  
  // 
  // Initialize the heap mspace.
  //
  bme_tile_info_t* tile_info = &global_info->tile_table[bme_tile_index()];
  bme_heap_mspace[bme_tile_ordinal()] = 
    tmc_mspace_create_with_base((void*)tile_info->heap_start_va,
                                tile_info->heap_size, 0);

  //
  // We're done with the global info struct for now.
  //
  bme_unmap_global_info();

  //
  // Call any initialization routines.
  //
  extern void _init(void);
  _init();

  //
  // Run the user program.
  //
  extern int main(int, char**);
  int retval = main(argc, argv);

  //
  // Exit.
  //
  exit(retval);
}


/** Exit the application.
 * @param status Exit status.  Will wind up in the DONE SPR, which is useful
 *        when running under the simulator.
 */
void
exit(int status)
{
  //
  // Call any finalization routines.
  //
  extern void _fini(void);
  _fini();

  //
  // Terminate the simulator, then nap forever.
  //
  __insn_mtspr(SPR_DONE, status);
  while (1)
    __insn_nap();
}
