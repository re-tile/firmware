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
 * Random-number function
 * @file
 */

#ifndef _BOGUX_RAND_H
#define _BOGUX_RAND_H

#include <stdint.h>

/** Return a random number more-or-less uniformly distibuted over a specified
 *  range, and update the seed used.
 * @param limit One larger than the largest number you expect to generate.
 * @param seed Pointer to the seed, which will be updated by the function.
 * @return A number between 0 and limit - 1, inclusive.
 */
static inline uint32_t rand_step(uint32_t limit, uint32_t* seed)
{
  *seed = *seed * 1664525 + 1013904223;
  return __insn_revbits(*seed) % limit;
}

#endif  /* !_BOGUX_RAND_H */
