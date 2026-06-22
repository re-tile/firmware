
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
 * BME global data structure and supporting types.  This data structure is
 * supplied by the hypervisor to the BME application.  It informs the
 * application about available hardware, such as memory and I/O devices, and
 * provides system configuration details, such as memory mappings for each
 * BME tile.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_SYS_INFO_H
#define _SYS_BME_SYS_INFO_H

#include <features.h>

#include <arch/chip.h>
#include <arch/interrupts.h>

#include <hv/hypervisor.h>

__BEGIN_DECLS

/** The current BME Major version number */
#define BME_CURRENT_VERSION_MAJOR 1

/** The current BME Minor version number */
#define BME_CURRENT_VERSION_MINOR 0

/** A macro for defining the BME revision.
 *
 * @param major Major revision number, new values signal incompatible changes.
 * @param minor Minor revision number, new values should be compatible.
 */
#define BME_VERSION_DEF(major, minor) \
        (((major) << 8) | (minor))

/** Extract 'major' from a version number. */
#define BME_VERSION_MAJOR(x)    (((x) >> 8) & 0xFF)

/** Extract 'minor' from a version number. */
#define BME_VERSION_MINOR(x)    ((x) & 0xFF)

//
// These BME-specific values must be consistent with similar values used in
// the hypervisor; see code below.
//
/** Maximum number of shared tiles per instance. */
#define BME_DRV_STILE_MAX            1

/** Maximum number of dedicated tiles per instance. */
#define BME_DRV_DTILE_MAX            7

/** Maximum number of IDN ports per shim. */
#define BME_MAX_IDN_PORTS            2

/** Maximum number of MDN ports per shim. */
#define BME_MAX_MDN_PORTS            3

/** Maximum number of memory shims. */
#define BME_MAX_MSHIMS               4

/** Maximum number of tiles supported per client. */
#define BME_MAX_TILES               256

#ifdef __HV__

//
// We don't really want to export hypervisor header files to be #included
// into BME applications, but we need to make sure that the BME data
// structures are consistent with similar data structures within the
// hypervisor.  This code verifies that everything is OK, when this file is
// compiled as part of the hypervisor build.
//
// The somewhat obnoxious full path names for the #includes are needed in
// order to keep us from finding similarly named files in sys/bme/bme, since
// the compiler looks in the directory that this file is resident in before
// looking at its search path.
//

#include "sys/hv/drvintf.h"
#include "sys/hv/types.h"
#include "sys/hv/tilegx/param.h"
#include "sys/hv/tilegx/tte.h"

#if BME_DRV_STILE_MAX < DRV_STILE_MAX
#error BME_DRV_STILE_MAX is too small
#endif

#if BME_DRV_DTILE_MAX < DRV_DTILE_MAX
#error BME_DRV_DTILE_MAX is too small
#endif

#if BME_MAX_IDN_PORTS < MAX_IDN_PORTS
#error BME_MAX_IDN_PORTS is too small
#endif

#if BME_MAX_MDN_PORTS < MAX_MDN_PORTS
#error BME_MAX_MDN_PORTS is too small
#endif

#if BME_MAX_MSHIMS < MAX_MSHIMS
#error BME_MAX_MSHIMS is too small
#endif

#if BME_MAX_TILES < HV_TILES
#error BME_MAX_TILES is too small
#endif

#else

//
// If we're not building the hypervisor we need to include BME-specific
// versions of these header files.
//
#include <bme/tte.h>
#include <bme/types.h>

#endif


//
// When the hypervisor boots a BME tile, it provides information about the
// configuration of the entire system, and information about the
// resources dedicated to each particular BME tile.  This file contains
// the data structures used to communicate this information.
//

/** Maximum number of memory segments, limited to match the number of 
 *  TLB entries.
 */
#define BME_MAX_MEM_SEGMENTS (CHIP_ITLB_ENTRIES() + CHIP_DTLB_ENTRIES())

/** Maximum length of a character string used in BME data structures. */
#define BME_MAX_NAME_LEN 32

/** Memory segment types */
typedef enum
{
  /** Unknown segment. */
  SEG_TYPE_UNKNOWN,
  /** Executable instructions. */
  SEG_TYPE_TEXT,
  /** Read-only data. */
  SEG_TYPE_RODATA,
  /** Read-write data. */
  SEG_TYPE_RWDATA,
  /** Executable instructions and read-only data. */
  SEG_TYPE_TEXT_RODATA,
} bme_seg_type_t;


/** Executable types */
typedef enum
{
  EXEC_TYPE_UNKNOWN,
  EXEC_TYPE_HV,
  EXEC_TYPE_LINUX,
  EXEC_TYPE_BME,
} bme_executable_type_t;


/** Mapping information for a segment of memory. */
typedef struct bme_mem_seg_info
{
  PA base_pa;                   /**< Base physical address of this segment */
  VA base_va;                   /**< Base virtual address of this segment */
  uint32_t size;                /**< Length of this segment, in bytes */
  int cache_mode;               /**< Caching mode */
  pos_t cache_coords;           /**< Coordinates of tile on which this is
                                     cached, if cache_mode is
                                     BME_CACHE_MODE_COORDS */
  bme_seg_type_t seg_type;      /**< Type of segment. */
  int itlb_index;               /**< Index in the ITLB, -1 if not in ITLB */
  int dtlb_index;               /**< Index in the DTLB, -1 if not in DTLB */
} bme_mem_seg_info_t;


/** Information for the free memory in the system. */
typedef struct bme_free_mem_info
{
  PA base_pa;                   /**< Base physical address of the free memory 
                                     space */
  uint64_t len;                 /**< Length of the free memory, in bytes */
} bme_free_mem_info_t;


/** Information for extra files in the system. */
typedef struct bme_extra_file_info
{
  PA base_pa;                   /**< Base physical address of the "extra"
                                     file */
  uint32_t len;                 /**< Length of the extra file, in bytes */
  char name[BME_MAX_NAME_LEN];  /**< Name of the extra file */
} bme_extra_file_info_t;


/** Information for a particular I/O device. */
typedef struct bme_io_dev_info
{
  /** Device shim code */
  uint32_t shim_type;
  /** Instance number */
  int instance;
  /** Device flags (DEV_FLG_xxx) */
  uint32_t flags;
  /** Shared tiles for this device */
  pos_t stiles[BME_DRV_STILE_MAX];
  /** Number of shared tiles */
  int num_stiles;
  /** Dedicated tiles for this device */
  pos_t dtiles[BME_DRV_DTILE_MAX];
  /** Number of dedicated tiles */
  int num_dtiles;
  /** Address used when sending requests to IDN ports */
  pos_t idn_ports[BME_MAX_IDN_PORTS];
  /** Number of IDN ports */
  int num_idn_ports;
  /** Off-grid coordinates of the MDN ports */
  pos_t mdn_ports[BME_MAX_MDN_PORTS];
  /** Number of MDN ports */
  int num_mdn_ports;
  /** Channel used when sending requests to this device */
  int channel;
  /** Lowest interrupt channel assigned to this device */
  int intchan;
  /** Number of interrupt channels assigned to this device */
  int num_intchan;
  /** Base name of device */
  char name[BME_MAX_NAME_LEN];
  /** If 1, BME has exclusive use of this device */
  int owned_by_bme;
} bme_io_dev_info_t;


/** Information for a particular tile. */
typedef struct bme_tile_info_t
{
  /** Coordinates of tile */
  pos_t pos;
  /** Index of tile in global array */
  int index;  
  /** Executable type running on tile */
  int exec_type;                       
  /** Client number; identical for two tiles if and only if they are in the
   *  same BME application or are under the control of the same supervisor */
  int client_num;
  /** Number of memory segments that are permanently mapped in this tile's
   *  TLBs */
  int num_mem_segs;
  /** Memory segments assigned to tile */
  bme_mem_seg_info_t mem_seg[BME_MAX_MEM_SEGMENTS];
  /** Size of heap for this tile, in bytes */
  uint32_t heap_size;
  /** Virtual address of start of heap for this tile */
  VA heap_start_va;
} bme_tile_info_t;


/** Global information, passed to each tile.  All tiles share the same copy. */
typedef struct bme_global_info_t
{
  /** Number of tiles in the system */
  int num_tiles;
  /** Information for all tiles on chip, indexed by tile number */
  bme_tile_info_t* tile_table;
  /** Number of free memory segments */
  int num_free_mem_segs;
  /** Global free memory description, indexed by segment number */
  bme_free_mem_info_t* free_mem;
  /** Nunber of "extra file" data segments */
  int num_extra_files;
  /** Placement descriptions of "extra files" */
  bme_extra_file_info_t* extra_file;
  /** Number of I/O devices in the system */
  int num_io_devices;
  /** Information for all I/O devices on chip, indexed as given by the
   *  hypervisor */
  bme_io_dev_info_t* io_table;
  /** Length (in bytes) of scratchpad memory */
  uint32_t scratchpad_len;
  /** Pointer to scratchpad memory */
  void* scratchpad;
  /** Coordinates of hypervisor console tile */
  pos_t console_tile;
  /** Argument string for BME application */
  char* bme_app_args;
#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
  /** List of physical addresses to use when performing the fence_incoherent()
   *  operation. */
  PA fence_incoherent_pas[BME_MAX_MSHIMS + 1];
#endif
  /** Base VA to be used for tmc_mem_flush_l2(). */
  VA flush_va;
  /** Page size used for tmc_mem_flush_l2(). */
  int flush_ps;
  /** Reserved PA used for tmc_mem_flush_l2(). */
  PA flush_pa;
  /** VA offset within the region used for tmc_mem_flush_l2(). */
  PA flush_offset;
  /** Board information block length */
  int bib_len;
  /** Board information block pointer */
  uint32_t* bib_buf;
  /** CPU speed */
  uint32_t cpu_speed;
  /** PA of the location of the lock shared between the hv and the BME */
  PA shared_lock_pa;
  /** Lotar of the location of the lock shared between the hv and the BME */
  Lotar shared_lock_lotar;
  /** Hypervisor interface version. */
  uint16_t hv_interface_version;
} bme_global_info_t;


/** Local information, passed to each tile on its stack.  Each tile gets 
 *  its own copy. 
 */
typedef struct bme_local_info_t
{
  /** Index of this tile in the global array */
  int index;
  /** Mapping information for the global data structure; maps the structure
   *  coherently and at an available VA */
  tte_t global_info_tte;
  /** Pointer to the global data structure, assuming it is mapped via
   *  global_info_tte */
  bme_global_info_t* global_info;
} bme_local_info_t;


//
// Access routines.
//

/** Map the global information structure with a wired TTE, at the address
 *  suggested by the hypervisor, and return a pointer to the structure.
 *  If called after the structure has been mapped, it is not mapped again,
 *  but a reference count is incremented.
 * @return Pointer to the global information structure, or NULL if there was
 *  no room in the TLB for its mapping.
 */
bme_global_info_t* bme_map_global_info(void);

/** Decrements the reference count for the global information structure; if
 * doing so causes the reference count to become zero, invalidates its mapping
 * in the DTLB.  Note: if other wired TTEs have been added since the structure
 * was mapped, this will leave an invalid but wired entry in the DTLB.
 *
 * @return Pointer to the global information structure.
 */
void bme_unmap_global_info(void);

/** Return the index of this tile in the BME global_information structure;
 *  that is, global_info.tile_table[bme_tile_index()] is this tile's entry in
 *  the tile table.
 * @return Tile index.
 */
int bme_tile_index(void);

/** Return the ordinal number of this tile within the BME application;
 *  ranges from zero to the number of application tiles minus 1.
 * @return Tile ordinal.
 */
int bme_tile_ordinal(void);

/** Return the number of tiles in this BME application.
 * @return Number of tiles.
 */
int bme_num_tiles(void);

__END_DECLS

#endif /* _SYS_BME_SYS_INFO_H */

/** @} */
