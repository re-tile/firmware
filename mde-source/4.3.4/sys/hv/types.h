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
 * Types used by the hypervisor.
 */

#ifndef _SYS_HV_TYPES_H
#define _SYS_HV_TYPES_H

#ifndef __ASSEMBLER__

#include <arch/chip.h>

#include <stdint.h>

/** Location override target. */
typedef uint32_t Lotar;

/** Address space identifier. */
typedef uint32_t Asid;

/** Virtual address. */
typedef uintptr_t VA;

/** Real physical address. */
typedef uint64_t PA;

/** Client physical address. */
typedef uint64_t CPA;

/** Type for tile coordinates; matches that used by hardware in most
 *  contexts. */
typedef union
{
  struct {
#ifndef __BIG_ENDIAN__
    uint32_t len:  7;   /**< Length (when used as an xDN header) */
    uint32_t   y: 11;   /**< Y coordinate */
    uint32_t   x: 11;   /**< X coordinate */
    uint32_t  fb:  1;   /**< FBit (when pos_t used as an xDN address) */
    uint32_t  fr:  2;   /**< Final route */
#else   // __BIG_ENDIAN__
    uint32_t  fr:  2;   /**< Final route */
    uint32_t  fb:  1;   /**< FBit (when pos_t used as an xDN address) */
    uint32_t   x: 11;   /**< X coordinate */
    uint32_t   y: 11;   /**< Y coordinate */
    uint32_t len:  7;   /**< Length (when used as an xDN header) */
#endif
  } bits;               /**< Bitfield for set/get */
  uint32_t word;        /**< Word for send/receive */
} pos_t;

/** Convert a pos_t to a tile index. */
#define _POS2IDX(pos, ulhc) ((((pos).bits.x - ulhc.bits.x) << HV_YBITS) | \
                              ((pos).bits.y - ulhc.bits.y))

/** Convert a tile index to a pos_t. */
#define _IDX2POS(idx, ulhc) \
  ((pos_t) { .word = (((((idx) >> HV_YBITS) + ulhc.bits.x) << 18) | \
                      ((((idx) & RMASK(HV_YBITS)) + ulhc.bits.y) << 7)) })

/** Tag for globally shared data structures.  Variables defined with this
 *  tag (e.g., "int foo _SHARED;", or "long bar _SHARED = 1;") will be
 *  allocated in memory which is available to all tiles. */
#define _SHARED   __attribute__((section(".shared")))

/** Conversion of short, int and long between cpu and little-endian format. */
#ifdef __BIG_ENDIAN__
/** Convert a 2-byte short to little-endian format. */
#define cpu_to_le16(x) (__builtin_bswap32(x) >> 16)
/** Convert a 4-byte int to little-endian format. */
#define cpu_to_le32(x) __builtin_bswap32(x)
/** Convert an 8-byte long to little-endian format. */
#define cpu_to_le64(x) __builtin_bswap64(x)
#else
/** Convert a 2-byte short to little-endian format. */
#define cpu_to_le16(x) (x)
/** Convert a 4-byte int to little-endian format. */
#define cpu_to_le32(x) (x)
/** Convert an 8-byte long to little-endian format. */
#define cpu_to_le64(x) (x)
#endif

/** Convert a 2-byte short from little-endian format. */
#define le16_to_cpu cpu_to_le16
/** Convert a 4-byte int from little-endian format. */
#define le32_to_cpu cpu_to_le32
/** Convert an 8-byte long from little-endian format. */
#define le64_to_cpu cpu_to_le64

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_TYPES_H */
