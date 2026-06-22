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
 * POST RAM quick test.
 */

#include <arch/msh.h>

#include "cfg.h"
#include "hv_l1boot.h"
#include "mshim_cfg.h"
#include "post/post_ram.h"

/** Read and check one memory word using mshim diag access.
 * @param shimaddr Address of the mshim to test.
 * @param portnum Shim port number.
 * @param addr Address of memory to read.
 * @param data Expected data.
 * @param error_count Pointer to running error count.
 * @return 1 if max error count exceeded, otherwise 0.
 */
int
test_one_addr(pos_t shimaddr, int portnum, PA addr, uint64_t data,
              int* error_count)
              
{
  uint64_t read_result; // Actual data read

  read_result = mshim_diag_read(shimaddr, addr);
  if (read_result != data)
  {
    boot_printf("POST_ERROR: DRAM on msh%d, address 0x%010llx\n",
                portnum, addr);
    boot_printf("  Expected 0x%016llx, actual 0x%016llx, XOR 0x%016llx\n",
                data, read_result, data ^ read_result);
    (*error_count)++;
  }

  if (*error_count > POST_RAM_MAX_ERROR)
    return 1;
  else
    return 0;
}

/** Run a quick test on an mshim by just checking the address and data lines.
 *  This test uses the mshim diagnostic registers to test the RAM.
 * @param size Size of memory on this shim, in bytes.
 * @param shimaddr Address of the mshim to test.
 * @return Error count - number of addresses that failed.
 */
int
post_ram_quick(int64_t size, pos_t shimaddr)
{
  PA addr;       // Address under test
  PA addr_mask;  // Mask to help create legal addresses
  uint64_t data; // Data being written, or expected data on read
  int error_count; // count of errors

  MSH_DEV_INFO_t inforeg = { .word = cfg_rd(shimaddr.word, 0, MSH_DEV_INFO) };
  int portnum = inforeg.instance;

  error_count = 0;

#ifdef POST_VERBOSE
  boot_printf("Quick testing DRAM on msh%d, size %#llx\n", portnum, size);
#endif  /* POST_VERBOSE */

  //
  // Algorithm overview:
  // We are trying to detect opens/shorts in the interconnect between the
  // Tilera chip and the memory.  The most concise way to do that is with
  // address and data values that are all "0" except one bit (iterating
  // over all bits), and then values that are all "1" except one bit
  // (iterating over all bits).  This is called "walking 1" and "walking
  // 0".
  //
  // So, each iteration writes:
  //  Walking 1 data to walking 1 address
  //  Walking 0 data to walking 0 address
  //
  // We have more data bits than address bits, so in order to make sure
  // we have full coverage of the data bits, we do an extra loop afterward
  // to hit the high data bits.  We don't bother doing walking 1's/0's
  // on the address bus for these values.
  //

  //
  // We invert the walking 1 value to get the walking 0 value.  When we do
  // that to an address, to make it legal, we need to mask off some low
  // bits (because the diag feature requires 64-byte aligned addresses) and
  // high bits (since they may be invalid).  Contruct that mask.
  //
  addr_mask = (1UL << (63 - __builtin_clzl(size))) - 64;

  //
  // The size of the shim may not be a power of two (for instance, we may
  // have 3 DIMMs).  In this case, there's one extra address we want to
  // use, in the walking 1's case only.  For instance, if we had 3
  // 512 MB DIMMs we'd have 0x18000000000 bytes.  In that case, our mask
  // woudl be 0x7fffffffc0.  However, we also want to test the case where
  // the high address bit is 1, or 0x10000000000.  We can't test the
  // walking 0's version of that because it's the same address we got for
  // walking 1's address 0.
  //
  int extra_w1_test = ((size - 64) > addr_mask);

  //
  // Address write loop.
  //
  addr = 0;
  data = 1;
  while (addr < addr_mask)
  {
    // Walking 1 addr
    mshim_diag_write(shimaddr, addr, data);
    // Walking 0 addr
    mshim_diag_write(shimaddr, ~addr & addr_mask, ~data);

    // Update data.
    data <<= 1;

    // Update Address.
    // Special case - address is zero in first iteration.
    addr = (addr == 0) ? 64 : addr << 1;
  }
  if (extra_w1_test)
    mshim_diag_write(shimaddr, addr, data);

  //
  // Address read loop.
  //
  addr = 0;
  data = 1;
  while (addr < addr_mask)
  {
    if (test_one_addr(shimaddr, portnum, addr, data, &error_count) ||
        test_one_addr(shimaddr, portnum, ~addr & addr_mask, ~data,
                      &error_count))
      // Break out early if error count exceeds maximum.
      return error_count;

    // Update data.
    data <<= 1;

    // Update Address.
    // Special case - address is zero in first iteration.
    addr = (addr == 0) ? 64 : addr << 1;
  }
  if (extra_w1_test)
  {
    if (test_one_addr(shimaddr, portnum, addr, data, &error_count))
      return error_count;
    data <<= 1;
  }

  //
  // Remaining data write loop.
  //
  addr = 0;
  uint64_t datapass_start = data;
  while (data)
  {
    mshim_diag_write(shimaddr, addr, data);
    addr += 64;

    mshim_diag_write(shimaddr, addr, ~data);
    addr += 64;

    data <<= 1;
  }

  //
  // Remaining data read loop.
  //
  addr = 0;
  data = datapass_start;
  while (data)
  {
    if (test_one_addr(shimaddr, portnum, addr, data, &error_count))
      return error_count;
    addr += 64;

    if (test_one_addr(shimaddr, portnum, addr, ~data, &error_count))
      return error_count;
    addr += 64;

    data <<= 1;
  }

#ifdef POST_VERBOSE
  boot_printf("Quick test complete, %d errors\n", error_count);
#endif  /* POST_VERBOSE */

  return (error_count);
}
