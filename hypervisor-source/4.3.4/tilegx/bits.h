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
 * Definitions for bit-manipulation macros.
 */

#ifndef _SYS_HV_TILEGX_BITS_H
#define _SYS_HV_TILEGX_BITS_H

#ifdef __ASSEMBLER__
#define ONE 1             ///< 1
#define ONE64 1           ///< 64-bit version of 1
#else
#define ONE 1             ///< 1
#define ONE64 1UL         ///< 64-bit version of 1
#endif

/// Round down to alignment; align must be power of two
#define ROUND_DN(val,align) ((val) & -((__typeof(val)) (align)))
/// Round up to alignment; align must be power of two
#define ROUND_UP(val,align) (((val) + (align) - 1) & -((__typeof(val)) (align)))
/// Round up to word size
#define ROUND_UP_WD(val)    ROUND_UP((val), 8)
/// Round down to word size
#define ROUND_DN_WD(val)    ROUND_DN((val), 8)

/// Integer divide, rounding up fractions
#define DIV_ROUND_UP(dividend, divisor) (((dividend) + (divisor) - 1)/(divisor))

/// Bytes to words, round up
#define B2W_UP(val)         (((val) + 7) >> 3)
/// Bytes to words, round down
#define B2W_DN(val)         ((val) >> 3)
/// Words to bytes
#define W2B(val)            ((val) << 3)

/// Right-justified mask of n bits.  The ugly definition for C is to
/// prevent compiler "shift count too large" complaints for RMASK(64),
/// without making the non-constant case take longer.
#ifdef __ASSEMBLER__
#define RMASK(n)            ((ONE64 << (n)) - 1)
#else
#define RMASK(n) \
  (__builtin_constant_p(n) ? ((n) == 64 ? ~0ULL : (ONE64 << (n)) - 1) \
                           : (ONE64 << (n)) - 1)
#endif
/// Right-justified 64-bit mask of n bits
#define RMASK64(n)          RMASK(n)

/// Left-justified mask of n bits
#define LMASK(n)            (~((ONE64 << (NBPW - n)) - 1))

#define NBPW                64      ///< Bits per word
#define NBPB                8       ///< Bits per byte

#endif /* _SYS_HV_TILEGX_BITS_H */
