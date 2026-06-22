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
 * Hypervisor parameters.
 *
 * This header file defines anything which we might want to change for
 * different builds of the hypervisor: table sizes, optional feature enable
 * \#defines, and so forth.  Everything in here should be overridable via
 * a -D option on the tile-cc command line.  Things which are computed
 * based on these parameters and aren't overridable don't belong here, but
 * belong in other header files.
 *
 */

#ifndef _SYS_HV_TILEGX_PARAM_H
#define _SYS_HV_TILEGX_PARAM_H

#include <arch/chip.h>

/* Protection */

#ifndef HV_PL
/** Protection level at which the hypervisor runs. */
#define HV_PL                   3
#endif

/* Hardware configuration items */

#ifndef HV_XBITS
/** Number of bits in a tile's 0-based X coordinate, or more precisely, log2
 *  of the maximum width of a chip's tile grid. */
#define HV_XBITS                4
#endif

#ifndef HV_YBITS
/** Number of bits in a tile's 0-based Y coordinate, or more precisely, log2
 *  of the maximum height of a chip's tile grid. */
#define HV_YBITS                4
#endif

/** Number of tiles */
#ifndef HV_TILES
#define HV_TILES                (1 << (HV_XBITS + HV_YBITS))
#endif

/* Device properties */

#ifndef MAX_DEVICE_CLOCKS
/** Maximum number of speeds (independently configurable PLLs) per shim. */
#define MAX_DEVICE_CLOCKS 2
#endif


/* Shims */

#ifndef MAX_MSHIMS
/** Maximum number of memory shims.  Due to various hardware limitations (for
 *  instance, the way in which SPR reads and writes must be done) there is
 *  some code which knows we have 4 shims, and which would need to be changed
 *  if this value were to be increased; see set_cbox_mmap_spr() in particular.
 */
#define MAX_MSHIMS              4
#endif

#ifndef MAX_RSHIMS
/** Maximum number of miscellaneous I/O shims. */
#define MAX_RSHIMS              1
#endif

#ifndef MAX_IPI_SHIMS
/** Maximum number of IPI shims. */
#define MAX_IPI_SHIMS           4
#endif

#ifndef MAX_IDN_PORTS
/** Maximum number of IDN ports per shim. */
#define MAX_IDN_PORTS           2
#endif

#ifndef MAX_MDN_PORTS
/** Maximum number of MDN ports per shim.  As noted above, some code would
 *  have to be changed to increase this number.
 */
#define MAX_MDN_PORTS           3
#endif

#ifndef MAX_GPIOS
/** Maximum number of GPIO interfaces. */
#define MAX_GPIOS               1
#endif

#ifndef MAX_MPIPES
/** Maximum number of mPIPE interfaces. */
#define MAX_MPIPES              2
#endif

#ifndef MAX_PCIES
/** Maximum number of Trio interfaces. */
#define MAX_PCIES               3
#endif

#ifndef MAX_MICA_COMPS
/** Maximum number of MiCA compression interfaces. */
#define MAX_MICA_COMPS          4
#endif

#ifndef MAX_MICA_CRYPTOS
/** Maximum number of MiCA cryptography interfaces. */
#define MAX_MICA_CRYPTOS        4
#endif

#ifndef MAX_I2CMS
/** Maximum number of I2C master interfaces. */
#define MAX_I2CMS               3
#endif

#ifndef MAX_I2C_SWITCHES
/** Maximum number of I2C switches per I2C bus. */
#define MAX_I2C_SWITCHES 2
#endif

#ifndef MAX_USB_HOSTS
/** Maximum number of USB host interfaces. */
#define MAX_USB_HOSTS           2
#endif

#ifndef MAX_USB_DEVS
/** Maximum number of USB device interfaces. */
#define MAX_USB_DEVS            1
#endif

/* Caches */

#ifndef L2_WAY_SIZE
/** Size of one way of the level-2 cache in bytes. */
#define L2_WAY_SIZE             (CHIP_L2_CACHE_SIZE() / CHIP_L2_ASSOC())
#endif

#ifndef L2_GUARANTEED_WAYS
/** Number of ways that the hardware guarantees we will have */
#define L2_GUARANTEED_WAYS      CHIP_L2_ASSOC()
#endif

#ifndef L2_GUARANTEED_SIZE
/** Guaranteed minimum amount of space in the level-2 cache in bytes. */
#define L2_GUARANTEED_SIZE      (L2_WAY_SIZE * L2_GUARANTEED_WAYS)
#endif

/* Hypervisor physical memory layout */

#ifndef HV_CODE_PAGE_SHIFT
/** Log2 of the size of our single hypervisor code & initial data page. */
#define HV_CODE_PAGE_SHIFT      PG_SHIFT_1M
#endif

#ifndef HV_DATA_PAGE_SHIFT
/** Log2 of the size of our per-tile single hypervisor live data page. */
#define HV_DATA_PAGE_SHIFT      PG_SHIFT_1M
#endif

#ifndef HV_FS_PAGE_SHIFT
/** Log2 of the size of the pages holding the hypervisor filesystem. */
#define HV_FS_PAGE_SHIFT        PG_SHIFT_256K
#endif

#ifndef HV_SHARED_PAGE_SHIFT
/** Log2 of the size of the pages holding shared data.  Must not be
 *  smaller than the largest possible client small page size. */
#define HV_SHARED_PAGE_SHIFT    PG_SHIFT_64K
#endif

#ifndef HV_FLUSH_PAGE_SHIFT
/** Log2 of the size of the page used for L2 cache flushing. */
#define HV_FLUSH_PAGE_SHIFT     PG_SHIFT_1M
#endif

#ifndef HV_CLIENT_SHARED_PAGE_SHIFT
/** Log2 of the size of pages shared with the client (used for fast device
 *  communication, etc.)  This is the size we physically allocate, and must
 *  be no larger than the client small page size; if it's smaller than that,
 *  a shared page will appear to be mapped multiple times within one client
 *  small page. */
#define HV_CLIENT_SHARED_PAGE_SHIFT    PG_SHIFT_64K
#endif

#ifndef HV_NUM_SHARED_PAGES
/** Number of pages potentially used for shared memory. */
#define HV_NUM_SHARED_PAGES            256
#endif

#ifndef HV_NUM_CLIENT_SHARED_PAGES
/** Number of pages shared with the client. */
#define HV_NUM_CLIENT_SHARED_PAGES     5
#endif


/* Memory management items */

#ifndef TSB_S_IDX_WIDTH
/** Log2 of the number of entries in the small-page TSB. */
#define TSB_S_IDX_WIDTH 12      /* 4K entries */
#endif

#ifndef TSB_L_IDX_WIDTH
/** Log2 of the number of entries in the large-page TSB. */
#define TSB_L_IDX_WIDTH 9       /* 512 entries */
#endif

#ifndef TSB_J_IDX_WIDTH
/** Log2 of the number of entries in the jumbo-page TSB. */
#define TSB_J_IDX_WIDTH 8       /* 256 entries */
#endif


/* Clients */

#ifndef MAX_CLIENTS
/** Number of regular clients supported.  Note that right now, much of the
 *  code necessary to support this being larger than 1 does not exist. */
#define MAX_CLIENTS 1
#endif

#ifndef MAX_BME
/** Number of BME clients supported.  Currently much of the code to
 *  support this being larger than 1 does not exist. */
#define MAX_BME 1
#endif


/* RSHIM and UART */

#ifndef UART_SPEED
/** Speed to set the UART to. */
#define UART_SPEED              115200
#endif


#endif /* _SYS_HV_TILEGX_PARAM_H */
