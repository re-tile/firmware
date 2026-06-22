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
 *
 * Bare Metal Environment parameters.
 *
 * This header file defines anything which we might want to change for
 * different builds of the BME: table sizes, optional feature enable
 * \#defines, and so forth.  Everything in here should be overridable via
 * a -D option on the tile-cc command line.  Things which are computed
 * based on these parameters and aren't overridable don't belong here, but
 * belong in other header files.
 */

#ifndef _SYS_BME_PARAM_H
#define _SYS_BME_PARAM_H

#include <arch/chip.h>


/* Protection level */

#ifndef BME_PL
/** Protection level at which the BME runs. */
#define BME_PL                       3
#endif

/* Hardware configuration items */
#ifndef BME_XBITS
/** Number of bits in a tile's 0-based X coordinate, or more precisely, log2
 *  of the maximum width of a chip's tile grid. */
#define BME_XBITS                3
#endif

#ifndef BME_YBITS
/** Number of bits in a tile's 0-based Y coordinate, or more precisely, log2
 *  of the maximum height of a chip's tile grid. */
#define BME_YBITS                3
#endif

/** Number of tiles */
#ifndef BME_TILES
#define BME_TILES                (1 << (BME_XBITS + BME_YBITS))
#endif

/* Shims */

#ifndef MAX_MSHIMS
/** Maximum number of memory shims.
 */
#define MAX_MSHIMS              4
#endif

/* Caches */

#ifndef L2_WAY_SIZE
/** Size of one way of the level-2 cache in bytes. */
#define L2_WAY_SIZE             (CHIP_L2_CACHE_SIZE() / CHIP_L2_ASSOC())
#endif

#endif /* _SYS_BME_PARAM_H */
