/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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
 * Interface definitions for the SPI Flash ROM driver.
 */

#ifndef _SYS_HV_INCLUDE_DRV_SROM_INTF_H
#define _SYS_HV_INCLUDE_DRV_SROM_INTF_H

/** Read this offset to get the total device size. */
#define SROM_TOTAL_SIZE_OFF   0xF0000000

/** Read this offset to get the device sector size. */
#define SROM_SECTOR_SIZE_OFF  0xF0000004

/** Read this offset to get the device page size. */
#define SROM_PAGE_SIZE_OFF    0xF0000008

/** Write this offset to flush any pending writes. */
#define SROM_FLUSH_OFF        0xF1000000

/** Write this offset, plus the byte offset of the start of a sector, to
 *  erase a sector.  Any write data is ignored, but there must be at least
 *  one byte of write data.  Only applies when the driver is in MTD mode.
 */
#define SROM_ERASE_OFF        0xF2000000

#endif /* _SYS_HV_INCLUDE_DRV_SROM_INTF_H */
