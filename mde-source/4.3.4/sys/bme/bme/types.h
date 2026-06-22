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
 * Types used by BME routines.  These mirror similar types used by the
 * hypervisor since some header files are shared between the two.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_TYPES_H
#define _SYS_BME_TYPES_H

#ifndef __ASSEMBLER__

#include <features.h>
#include <stdint.h>

__BEGIN_DECLS

/** Location override target. */
typedef uint32_t Lotar;

/** Address space identifier. */
typedef uint32_t Asid;

/** Virtual address. */
typedef unsigned long VA;

/** Physical address. */
typedef uint64_t PA;

/** Type for tile coordinates; matches that used by hardware in most
 *  contexts, such as when sending an IDN or UDN message. */
typedef union
{
  struct {
    uint32_t len:  7;   /**< Length (when used as an xDN header) */
    uint32_t   y: 11;   /**< Y coordinate */
    uint32_t   x: 11;   /**< X coordinate */
    uint32_t  fb:  1;   /**< FBit (when pos_t used as an xDN address) */
    uint32_t  fr:  2;   /**< Final route */
  } bits;               /**< Bitfield for set/get */
  uint32_t word;        /**< Word for send/receive */
} pos_t;

__END_DECLS

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_BME_TYPES_H */

/** @} */
