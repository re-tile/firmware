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
 * Definitions for the SPI ROM driver.
 */

#ifndef _SYS_HV_DRIVERS_SROM_SROM_H
#define _SYS_HV_DRIVERS_SROM_SROM_H

#include "drvintf.h"
#include <hv/drv_srom_intf.h>

/** Number of partitions within the device. */
#define SROM_PARTITIONS 3

/** When writing a sector, we will not write it all of it at once,
    instead, we write no more than this many pages during each
    hypervisor system call. */
#define SROM_WRITE_CHUNK_PAGE 16

//
// Status when flushing a page
//

/** No need to write the page when flushing page */
#define FLUSH_PAGE_NOT_WRITTEN 0
/** Error happens when flushing page */
#define FLUSH_PAGE_ERROR -1
/** Page written when flushing page */
#define FLUSH_PAGE_PAGE_WRITTEN 1

//
// Status when flushing a sector
//

/** Error happens when flushing sector */
#define FLUSH_SECTOR_ERROR -1
/** Flushing sector done */
#define FLUSH_SECTOR_DONE 0
/** HW is erasing sector when flushing sector */
#define FLUSH_SECTOR_ERASING_SECTOR 1
/** Needs to write rest pages when flushing sector */
#define FLUSH_SECTOR_WRITE_PAGE_AGAIN 2

// Forward reference, defined below
struct srom_state;

/** A state object only allocated on the shared tile. */
typedef struct srom_mst_state
{
  /** Pointer to the per-tile state object. */
  struct srom_state *s_state;

  /** Base offset for each partition. */
  uint32_t base[SROM_PARTITIONS];
  /** Size for each partition. */
  uint32_t size[SROM_PARTITIONS];

  /** Page size of SROM in bytes. */
  uint32_t page_size;
  /** Sector size of SROM in bytes. */
  uint32_t sector_size;
  /** Total size of SROM in bytes. */
  uint32_t srom_size;
  /** Pages in each SROM sector. */
  uint32_t pages_per_sector;
  /** SROM device cookie. */
  int srom_dev;

  /** Flag to indicate the device is erasing a sector, so the device cann't
      handle some device operation from client.  This value will not be
      automatically updated. Client needs to call read / write again to
      update this flag */
  int erase_in_progress;

  /** SROM type ID. */
  uint64_t srom_id;

  /** Sector buffer.  This contains cached data for the current sector. */
  uint32_t* sector_buf;
  /** Sector address.  This is the address of the first byte in the
      sector buffer; since the buffer is aligned to a whole sector,
      this will be an integral multiple of the sector size. */
  uint32_t  sector_addr;
  /** Page valid bitmap.  This describes which pages within the current
      sector have valid data in sector_buf. */
  uint8_t* page_valid;
  /** Page dirty bitmap.  This describes which pages within the currrent
      sector have been modified; those pages must also be valid. */
  uint8_t* page_dirty;
  /** Sector valid flag.  This is nonzero if there is any data in the sector
      buffer (i.e., if there is a bit set in the page valid bitmap). */
  uint8_t sector_valid:1;
  /** Sector dirty flag.  This is nonzero if the current sector has been
      written to (i.e., if there is a bit set in the page dirty bitmap). */
  uint8_t sector_dirty:1;
  /** Sector needs erase flag.  This is nonzero if the current sector is
      dirty, and at least one bit in the sector has been changed from a
      one to a zero by a write; in this case, we must erase the sector
      before committing write data to it. */
  uint8_t sector_needs_erase:1;
}
srom_mst_state_t;

/** A state object kept by every tile in the system. */
typedef struct srom_state
{
  pos_t my_pos;                  /**< This tile's coordinates */
  pos_t fwd_tile;                /**< Forward requests here */
  uint32_t fwd:1;                /**< Forward requests to fwd_tile? */
  const struct dev_info* infop;  /**< Device information */

  srom_mst_state_t* mst_state;   /**< Non-NULL on shared tile. */
}
srom_state_t;

#endif /* _SYS_HV_DRIVERS_SROM_SROM_H */
