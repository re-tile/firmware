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
 * Routines to do shim configuration operations.
 */

#ifndef _SYS_HV_CFG_H
#define _SYS_HV_CFG_H

#include <arch/idn.h>

#include "types.h"
#include "hvbme/cfg.h"

/** Number of times we check for a reply from an I/O shim before deciding
 *  it doesn't exist */
#define SHIM_PROBE_TIMEOUT 4000

/** DynamicHeader for a configuration write. */
#define CFG_WR_HDR FILL_DYNAMIC_HEADER(0, 0, 1, 4)

/** DynamicHeader for a configuration read. */
#define CFG_RD_HDR FILL_DYNAMIC_HEADER(0, 0, 1, 3)

void cfg_double_wr(uint32_t dest, unsigned long chan, unsigned long addr,
                   unsigned long data0, unsigned long data1);

#endif /* _SYS_HV_CFG_H */
