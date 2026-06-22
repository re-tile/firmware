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
 * Register definitions for the SPI Flash ROM shim, and prototypes for our
 * access routines.
 */

#ifndef _SYS_HV_SROM_ACC_H
#define _SYS_HV_SROM_ACC_H

#include <arch/rsh.h>

#include "types.h"

//
// SROM access routines.
//
int srom_is_busy(pos_t pos, unsigned long chan);
void srom_boot(pos_t pos, unsigned long chan);
int srom_is_booting(pos_t pos, unsigned long chan);
int srom_get_dev(pos_t pos, unsigned long chan);
int srom_getinfo(pos_t pos, unsigned long chan, uint64_t* srom_id,
                 uint32_t* page_size, uint32_t* sector_size,
                 uint32_t* rom_size);
int srom_rd(pos_t pos, unsigned long chan, int dev, int addr, int len,
            void* buf);
int srom_wr_pg(pos_t pos, unsigned long chan, int dev, int addr, int len,
               void* buf);
int srom_erase(pos_t pos, unsigned long chan, int dev, int addr);
int srom_erasea(pos_t pos, unsigned long chan, int dev, int addr);
int srom_get_bootstream_addr(pos_t pos, unsigned long chan, int dev);
void srom_reset(pos_t pos, unsigned long chan, int dev);

/** Channel number on the rshim for the SROM */
#define SROM_CHAN ((uint64_t) RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_SPI << \
                   RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

#endif /* _SYS_HV_SROM_ACC_H */
