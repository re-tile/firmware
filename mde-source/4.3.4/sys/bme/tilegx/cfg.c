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

// FIXME: Not ported to Gx yet.  Will need the assembly
// file sys/hv/physacc_asm.S  But since comments here indicate that this
// is used for common_hvbme stuff it's well into runtime before we need
// it.

#if 0

#include <util.h>

#include <arch/idn.h>

#include <bme/cfg.h>
#include <bme/types.h>

/// DynamicHeader for a configuration write
#define CFG_WR_HDR FILL_DYNAMIC_HEADER(0, 0, 1, 4)

/// DynamicHeader for a configuration read
#define CFG_RD_HDR FILL_DYNAMIC_HEADER(0, 0, 1, 3)

#ifndef __DOXYGEN__
//
// Doxygen doesn't want to document these since they're internal
// functions, used to support the common hv/bme library.
//
static unsigned long
coord_to_mmio_aar(uint32_t dest)
{
  pos_t pos = { .word = dest };
  SPR_AAR_t aar =
  {{
    .physical_memory_mode = 1,
    .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO,
    .location_x_or_page_mask = pos.bits.x,
    .location_y_or_page_offset = pos.bits.y,
  }};

  return (aar.word);
}


void
cfg_wr(uint32_t dest, uint32_t chan, uint32_t addr, uint32_t data)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  phys_wr64(addr + chan, data, aar);
}


uint32_t
cfg_rd(uint32_t dest, uint32_t chan, uint32_t addr)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  return phys_rd64(addr + chan, aar);
}

#endif // __DOXYGEN__


void
bme_cfg_wr(pos_t dest, uint32_t chan, uint32_t addr, uint32_t data)
{
  cfg_wr(dest.word, chan, addr, data);
}


uint32_t
bme_cfg_rd(pos_t dest, uint32_t chan, uint32_t addr)
{
  return cfg_rd(dest.word, chan, addr);
}

#endif
