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
 * Things that aren't really implemented yet on TILE-Gx.
 */

#include <arch/spr.h>
#include <arch/sim.h>

#include "hv.h"
#include "util.h"

#ifndef __DOXYGEN__

#define P panic("bad call at %s, line %d", __FILE__, __LINE__);

//-----------------------------------------------------------------------------

#include "hvgdb.h"

PA hvgdb_init2(PA pa) { return 0; }
int hvgdb_store_mapping(VA va, uint32_t len, PA pa) { return 0; }

//-----------------------------------------------------------------------------

#endif  // __DOXYGEN__
