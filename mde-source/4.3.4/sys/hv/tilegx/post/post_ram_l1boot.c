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
 * POST RAM test run from the L1 booter.
 */

#include <arch/msh.h>

#include "cfg.h"
#include "hv_l1boot.h"
#include "mshim_acc.h"
#include "post_ram.h"

/** Number of passes to make through the specified range of memory. */
#define NUM_PASSES 4

/** Exhaustively test a region of DRAM, from address start_addr to end_addr.
 *  This runs from the L1 Booter, and uses non-cacheable loads/stores to
 *  test the RAM.  RAM addresses are physical.
 * @param start_addr Starting (low) adddress of memory to test.
 * @param end_addr Last (inclusive) address of memory to test.
 * @param shimaddr Address of the mshim to test.
 * @return Error count, which is the number of addresses that failed.
 */
int
post_ram_l1boot(PA start_addr, PA end_addr, pos_t shimaddr)
{
  MSH_DEV_INFO_t inforeg = { .word = cfg_rd(shimaddr.word, 0, MSH_DEV_INFO) };
  int portnum = inforeg.instance;

#ifdef POST_VERBOSE
  boot_printf("Testing DRAM on msh%d, "
              "from address %#llx to address %#llx\n",
              portnum, start_addr, end_addr);
#else  /* POST_VERBOSE */
  boot_printf("msh%d ", portnum);
#endif  /* POST_VERBOSE */

  //
  // Clear any mshim interrupts.
  //
  (void) cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);

  // Drop down to assembly for each pass of the test because we need to exit
  // cache-as-ram mode and we cannot have any stack accesses then.
  // Pass to assembly the starting and ending addresses of the range to test,
  // and a mask to XOR with the generated pattern to write.
  // The return value is ~0 if the test passed, or the failing address
  // if it failed.

  // Do NUM_PASSES complete write/read passes through the range of memory test.
  // On each pass, XOR one of these masks with the write data.
  // This ensures that each bit of memory is tested as both a zero and one, and
  // tests for interference between adjacent bits.
  //
  static const uint64_t masks[NUM_PASSES] =
  {
    0x0000000000000000, 0xffffffffffffffff,
    0x5555555555555555, 0xaaaaaaaaaaaaaaaa
  };

  int error_count = 0;

  for (int pass_num = 0; pass_num < NUM_PASSES; pass_num++)
  {
    // The one_pass routine will return 0xffffffff if it passed, or a failing
    // address it there was a failure.
    // Note the assembly jumps out on the first failure, so if we have not
    // reached the failure limit, we'll want to re-call the assembly routine
    // with a new start_addr just after the one that failed.

    // Ensure our addresses are aligned to word boundaries.
    start_addr = (start_addr + 7) & ~(PA) 7;
    end_addr = (end_addr - 8) & ~(PA) 7;

    PA next_addr = start_addr;

#ifdef POST_VERBOSE
  boot_printf("Pass %d, mask 0x%016llx\n", pass_num, masks[pass_num]);
#else  /* POST_VERBOSE */
  boot_printf("%d", pass_num);
#endif  /* POST_VERBOSE */

    while (next_addr != ~(PA) 0 && next_addr <= end_addr)
    {
      PostReturnVal post_return_val =
        post_ram_l1boot_one_pass(next_addr, end_addr, masks[pass_num],
                                 shimaddr);

      next_addr = post_return_val.fail_addr;

      if (next_addr != ~(PA) 0)
      {
        error_count++;
        boot_printf("POST_ERROR: DRAM on msh%d, address %#llx\n",
                    portnum, next_addr);
        boot_printf("  Expected 0x%016llx, actual 0x%016llx, "
                    "XOR 0x%016llx\n",
                    post_return_val.expected_data,
                    post_return_val.actual_data,
                    post_return_val.expected_data ^
                    post_return_val.actual_data);

        next_addr += 8;
        if (error_count >= POST_RAM_MAX_ERROR)
          // Want to exit both loops and just return.
          return (error_count);
      }
    }
  }

  //
  // See if we're using ECC; if so, make sure we didn't correct any
  // errors.
  //
  MSH_CONTROL_t mc = { .word = cfg_rd(shimaddr.word, 0, MSH_CONTROL) };
  if (mc.ecc_cor)
  {
    uint_reg_t intrs = cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);

    if (intrs & (MSH_INT_VEC0__ECC_1BIT_MASK | MSH_INT_VEC0__ECC_2BIT_MASK))
    {
      boot_printf("POST ERROR: DRAM on msh%d, ECC errors detected\n", portnum);
      error_count++;
    }
  }

#ifdef POST_VERBOSE
  boot_printf("POST complete, %d errors found\n", error_count);
#else  /* POST_VERBOSE */
  boot_printf(" ");
#endif  /* POST_VERBOSE */

  return (error_count);
}
