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
 * Definitions for the POST RAM tests.
 */

#ifndef _SYS_HV_POST_RAM_H
#define _SYS_HV_POST_RAM_H

/** Max errors to report per shim before just exiting. */
#ifndef POST_RAM_MAX_ERROR
#define POST_RAM_MAX_ERROR 5
#endif

/** Struct for return from the one_pass assembly routine. */
typedef struct
{
  /** Address that failed, or value of ~0 indicates test passed .*/
  PA fail_addr;
  /** If failure, data that was expected. */
  uint64_t expected_data;
  /** If failure, incorrect data that was actually read. */
  uint64_t actual_data;
} PostReturnVal;

/** Assembly routine to make one pass through a range of memory.
 * @param start_addr Starting (low) adddress to test.
 * @param end_addr Ending (exclusive) address to test.
 * @param mask Value to XOR with pattern written to memory on this pass.
 * @param mshim_loc Route header indicating location of Mshim to test.
 * @return PostReturnVal struct. 3 words: fail_addr, expected and actual data.
 */
PostReturnVal post_ram_l1boot_one_pass(PA start_addr, PA end_addr,
                                       uint64_t mask, pos_t mshim_loc);

int post_ram_quick(int64_t size, pos_t shimaddr);
int post_ram_l1boot(PA start_addr, PA end_addr, pos_t shimaddr);
int post_ram_hv(VA start_va, PA start_pa, uint32_t mem_size, int error_count);

uint32_t handle_test_memory(PA base_pa, PA len, uint32_t init_mem);
int test_memory(PA* test_pa, PA* test_len, uint32_t init_mem);

#endif /* _SYS_HV__POST_RAM_H */
