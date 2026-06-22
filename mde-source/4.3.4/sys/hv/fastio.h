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
 * Fast I/O trap definitions.
 */

#ifndef _SYS_HV_FASTIO_H
#define _SYS_HV_FASTIO_H

#include "bits.h"

/** Width of a fast I/O index value. */
/** Originally this was 6, reserving 32 fastio dispatch table entries for
    user level access. On a fully-loaded system, e.g. 4 eth and at least one
    PCIe, we need more than 32 fastio entries. So we change it to 7 because
    it doubles the table size and has the lowest memory cost. */
#define FASTIO_INDEX_WIDTH  7
/** Mask for a fast I/O index value. */
#define FASTIO_INDEX_MASK   RMASK(FASTIO_INDEX_WIDTH)
/** Number of bits to shift the index to get to the "user mode" bit; this
    is nonzero if a program at PL0 is allowed to make this fast I/O call. */
#define FASTIO_USER_SHIFT   (FASTIO_INDEX_WIDTH - 1)

#ifndef __ASSEMBLER__
void fastio_init(void);
#endif /* ! __ASSEMBLER__ */

#endif /* _SYS_HV_FASTIO_H */
