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
 * POST RAM test run from hypervisor.
 */

#include <stdio.h>
#include <string.h>

#include <arch/msh.h>
#include <arch/sim.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "msg.h"
#include "hv.h"
#include "mshim_acc.h"
#include "page.h"
#include "post/post_ram.h"
#include "tlb.h"
#include "tsb.h"
#include "tte.h"
#include "types.h"

/** Number of passes to run in post_ram_hv(). */
#define NUM_PASSES 4

#define dF "0x%016lx"   /**< printf format for test data */

/** Exhaustively test a region of DRAM.  This runs under the hypervisor, and
 *  uses cacheable loads/stores to test RAM.  Addresses are virtual, and it is
 *  assumed the caller has already setup the necessary DTLB mapping.
 * @param start_va Starting virtual address of the region to test.
 * @param start_pa Starting physical address of the region to test (used for
 *   error messages).
 * @param mem_size Size in bytes of the region to test.
 * @param error_count Initial number of errors (number of addresses that
 *   failed).
 * @return Updated number of errors.
 */
int
post_ram_hv(VA start_va, PA start_pa, uint32_t mem_size, int error_count)
{
  if (!post_is_thorough)
    return (0);

#ifdef POST_VERBOSE
  tprintf("post: testing %#x bytes starting at VA %#lx, PA %#llx\n",
          mem_size, start_va, start_pa);
#endif

  //
  // Under simulation, reduce the amount of memory we test.
  //
  if (sim_is_simulator() && mem_size > 0x20000)
    mem_size = 0x20000;

  // Do NUM_PASSES complete write/read passes through the range of memory.
  // On each pass, XOR one of these masks with the write data.
  // This ensures that each bit of memory is tested as both a zero and one, and
  // tests for interference between adjacent bits.
  static const uint64_t masks[NUM_PASSES] =
  {
    0x0000000000000000UL, 0xffffffffffffffffUL,
    0x5555555555555555UL, 0xaaaaaaaaaaaaaaaaUL
  };
  for (int pass_num = 0; pass_num < NUM_PASSES; pass_num++)
  {
    unsigned long* addr = (unsigned long*)start_va;
    unsigned long accum = 1;
    for (int i = 0; i < (mem_size / sizeof (*addr)); i++)
    {
      accum = __insn_crc32_32(accum, 0);
      accum = (accum << 32) | __insn_crc32_32(accum, 0);
      *addr = accum ^ masks[pass_num];
      addr++;
    }

    addr = (unsigned long*)start_va;
    accum = 1;
    for (int i = 0; i < (mem_size / sizeof (*addr)); i++)
    {
      accum = __insn_crc32_32(accum, 0);
      accum = (accum << 32) | __insn_crc32_32(accum, 0);
      unsigned long expected_data = accum ^ masks[pass_num];
      unsigned long actual_data = *addr;

      if (expected_data != actual_data)
      {
        printf("post: ERROR: memory test mismatch at PA %#llX\n",
               start_pa + (VA) addr - start_va);
        printf("      expected data " dF " but got " dF " (XOR " dF ")\n",
               expected_data, actual_data, actual_data ^ expected_data);
        error_count++;

        // Exit early if error_count is over maximum.
        if (error_count >= POST_RAM_MAX_ERROR)
          return (error_count);
      }
      addr++;
    }
  }

  return (error_count);
}


/** Shift value for page size we use to temporarily map in memory under test. */
#define MEMORY_TEST_PAGE_SHIFT  PG_SHIFT_16M
/** Actual size of the page we'll use for memory testing. */
#define MEMORY_TEST_PAGE_SIZE   (1U << MEMORY_TEST_PAGE_SHIFT)
/** VA that we'll use for memory testing.  We haven't started a client yet,
 *  so it's safe to just use a random low address in the client's VA range. */
#define MEMORY_TEST_VA  MEMORY_TEST_PAGE_SIZE

/** Handle a remote request to test or init a segment of DRAM.
 * @param base_pa Physical address of first byte to test.
 * @param len Number of bytes to test.
 * @param init_mem Flag indicating if this is ECC init.
 * @return Error count, which is the number of addresses that failed.
 */
uint32_t
handle_test_memory(PA base_pa, PA len, uint32_t init_mem)
{
  INIT_TRACE("handle_test_memory(base_pa=%#llx, len=%#llx, init_mem=%u)\n",
              base_pa, len, init_mem);

  uint32_t nerrors = 0;

  //
  // We pick the first unwired TLB entry for mapping in entries.  At
  // this point, this should be invalid, so we don't bother saving and
  // restoring it.  We shouldn't take any TLB misses during the test, so
  // we don't bother wiring the new entry.
  //
  int wired_idx = __insn_mfspr(SPR_WIRED_DTLB);

  //
  // To test the memory we've been given, we break it up into page-sized
  // chunks; for each chunk, we map it into VA space, test it, and then
  // unmap it.
  //
  while (len)
  {
    PA chunk_pa = base_pa & ~RMASK64(MEMORY_TEST_PAGE_SHIFT);
    VA chunk_offset = base_pa - chunk_pa;
    uint32_t chunk_len = min(len, MEMORY_TEST_PAGE_SIZE - chunk_offset);

    tte_t tte = {
      .w0 = {{
          .ps = TTE_SHIFT_TO_PS(MEMORY_TEST_PAGE_SHIFT),
          .g = 1,
          .asid = 0,
          .v = 1,
          .w = 1,
          .mpl = HV_PL,
          .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT,
          .cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE,
          .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
          .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
        }},
      .w1 = { .word = MEMORY_TEST_VA },
      .w2 = { .word = chunk_pa }
    };
    WRITE_TLB_AT(D, wired_idx, tte);
      
    nerrors = post_ram_hv(MEMORY_TEST_VA + chunk_offset, base_pa, chunk_len,
                          nerrors);
    if (nerrors >= POST_RAM_MAX_ERROR)
      break;

    base_pa += chunk_len;
    len -= chunk_len;
  }

  //
  // Invalidate the temporary entry.
  //
  WRITE_TLB_AT(D, wired_idx, TTE_ZERO);

  //
  // Finally, flush the whole L2$ and L1D$, to get rid of any data from the
  // pages we tested.
  //
  inv_whole_l2();

  return (nerrors);
}


/** Test or init all available memory by handling out segments to other tiles.
 *  If any memory fails, remove it from the configuration.
 * @param test_pa Base physical address at which to start testing each
 *   memory shim.
 * @param test_len Number of bytes to test on each memory shim.  If a shim
 *   is disabled as a result of test failures, its length is set to zero in
 *   this array upon return.
 * @param init_mem Flag indicating if this is ECC init.
 * @return Nonzero if any shims were disabled, zero otherwise.
 */
int
test_memory(PA* test_pa, PA* test_len, uint32_t init_mem)
{
  int retval = 0;

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    INIT_TRACE("test_memory: segment %d PA %#llx len %#llx\n",
               i, test_pa[i], test_len[i]);
    //
    // Clear any mshim interrupts.
    //
    if (test_len[i])
    {
      pos_t shimaddr = mshims[i]->idn_ports[0];
      (void) cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);
    }
  }

  //
  // First create a list of tiles.  We omit ourselves from the list, since
  // we want to be able to handle printf requests from the slaves, and we
  // can't do that if we're busy testing memory ourselves.
  //
  struct
  {
    /** Destination tile */
    pos_t tile;
    /** Channel on which we expect a reply from this tile */
    uint32_t reply_channel;
    /** Base physical address to test */
    PA base_pa;
    /** Number of bytes to test */
    PA len;
    /** Shim number this tile is testing */
    int mshim_idx;
    /** Return value we got from this tile */
    struct hv_msg_test_memory_reply retval;
  }
  tile_info[HV_TILES];

  memset(tile_info, 0, sizeof (tile_info));

  int ntiles = 0;

  for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
    for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };
      if (tile.word != my_pos.word)
        tile_info[ntiles++].tile = tile;
    }

  //
  // Note that taking ourselves out of the list of worker tiles means we
  // can't actually test any memory in a 1-tile config.  Since that only
  // ever happens in simulation, and we don't really want to test memory
  // in that case, this is OK.
  //
  if (ntiles == 0)
    return (0);

  //
  // Now split up the memory to be tested among those tiles.
  // First figure out the total amount of memory.
  //
  PA bytes_left = 0;
  for (int i = 0; i < MAX_MSHIMS; i++)
    bytes_left += test_len[i];

  //
  // Now, for each shim, figure out the proportional number of tiles it should
  // get based on its size.  Then, split up its memory among that many tiles.
  //
  int tiles_left = ntiles;
  int next_tile = 0;

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (test_len[i] == 0)
      continue;

    int tiles_this_shim = (tiles_left * test_len[i]) / bytes_left;

    tiles_left -= tiles_this_shim;
    bytes_left -= test_len[i];

    PA bytes_left_this_shim = test_len[i];
    PA next_pa_this_shim = test_pa[i];

    for (; tiles_this_shim; tiles_this_shim--)
    {
      PA bytes_this_tile =
        ROUND_UP(bytes_left_this_shim / tiles_this_shim, CHIP_L2_LINE_SIZE());

      tile_info[next_tile].base_pa = next_pa_this_shim;
      tile_info[next_tile].len = bytes_this_tile;
      tile_info[next_tile].mshim_idx = i;
      next_tile++;

      bytes_left_this_shim -= bytes_this_tile;
      next_pa_this_shim += bytes_this_tile;
    }
  }

  //
  // Go through our list and send all of the messages.
  //
  for (int idx = 0; idx < ntiles; idx++)
  {
    struct hv_msg_test_memory msg =
    {
      .base_pa = tile_info[idx].base_pa,
      .len = tile_info[idx].len,
      .init_mem = init_mem,
    };

    send_var(tile_info[idx].tile, HV_TAG_TEST_MEMORY, &msg, sizeof (msg),
             NULL, 0, &tile_info[idx].reply_channel, &tile_info[idx].retval,
             sizeof (tile_info[idx].retval), 0);
  }

  //
  // Now go through again and wait for each reply; when we get one, accumulate
  // the number of errors we've found.
  //
  int nerrors[MAX_MSHIMS];
  memset(nerrors, 0, sizeof (*nerrors) * MAX_MSHIMS);

  for (int idx = 0; idx < ntiles; idx++)
  {
    size_t rcv_replylen;

    uint32_t rcv_type =
      getreply(tile_info[idx].reply_channel, &rcv_replylen, 1);

    if (rcv_type != HV_TAG_TEST_MEMORY)
      panic("message type mismatch: sent %#x, got %#x", HV_TAG_TEST_MEMORY,
            rcv_type);

    if (rcv_replylen != sizeof (tile_info[idx].retval))
      panic("message length error for HV_TAG_TEST_MEMORY reply");

    if (!init_mem)
      nerrors[tile_info[idx].mshim_idx] += tile_info[idx].retval.nerrors;
  }

  //
  // If any of the shims failed testing, then mark them as unavailable
  // in the test_len[] array.
  //

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    if (mshims[i] && nerrors[i])
    {
      int controller = mshim_controller[i];
#if 0
      //
      // FIXME: this is broken.  We _do_ now support multiple DIMMs per
      // shim, even on Pro, so we need to either (a) print all of the DIMM
      // labels, or, better yet, (b) figure out where the error occurred
      // and just print its label.  Also, this doesn't take striping into
      // account.
      //

      //
      // Note that we don't currently support more than one DIMM per memory
      // shim, so we just find the label for DIMM 0.
      //
      char label_string[80];
      bi_ptr_t resptr;
      uint32_t desc;

      desc = bi_getparam(BI_TYPE_DIMM_LABEL, controller << 3, &resptr, NULL);
      if (desc == BI_NULL)
        *label_string = '\0';
      else
        snprintf(label_string, sizeof (label_string), " (DIMM %.*s)",
                 BI_BYTES(desc), (char*) resptr);
#else
      char* label_string = "";
#endif
      printf("post: ERROR: %d errors found on memory shim %d%s, "
             "disabling\n", nerrors[i], controller, label_string);

      test_len[i] = 0;

      retval++;
    }

    //
    // See if we're using ECC; if so, make sure we didn't correct any
    // errors.
    //
    if (mshims[i] && test_len[i])
    {
      pos_t shimaddr = mshims[i]->idn_ports[0];

      MSH_CONTROL_t mc = { .word = cfg_rd(shimaddr.word, 0, MSH_CONTROL) };
      if (mc.ecc_cor)
      {
        uint_reg_t intrs = cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);

        if (intrs & (MSH_INT_VEC0__ECC_1BIT_MASK | MSH_INT_VEC0__ECC_2BIT_MASK))
        {
          printf("post: ERROR: ECC errors detected on memory shim %d\n",
                 mshim_controller[i]);
          test_len[i] = 0;
          retval++;
        }
      }
    }
  }

  return (retval);
}
