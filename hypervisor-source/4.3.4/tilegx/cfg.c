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

#include <arch/spr.h>

#include "sys/libc/include/util.h"

#include "cfg.h"
#ifdef L1BOOT
#include "hv_l1boot.h"
#include "boot_error.h"
#else
#include "hv.h"
#endif
#include "physacc.h"
#include "types.h"

static unsigned long
coord_to_mmio_aar(uint32_t dest)
{
  SPR_AAR_t aar =
  {{
    .physical_memory_mode = 1,
    .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO,
  }};
  //
  // The X and Y coordinates in the destination have the same spacing as
  // those in the AAR, so we can save time by just shifting the destination
  // appropriately and ORing it into the AAR, versus extracting/inserting
  // the coordinates separately.
  //
  return aar.word |
         ((uint_reg_t) dest << (SPR_AAR__LOCATION_Y_OR_PAGE_OFFSET_SHIFT -
                                SPR_TILE_COORD__Y_COORD_SHIFT));
}


/** Write to an I/O shim configuration register, performing appropriate
 *  byte-swapping as necessary depending on the processor's endian mode.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 */
void
cfg_wr(uint32_t dest, unsigned long chan,
       unsigned long addr, unsigned long data)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  phys_wr64(addr + chan, cpu_to_le64(data), aar);
}


/** Write 4 bytes to a 4-byte I/O shim configuration register,
 *  performing appropriate byte-swapping as necessary depending on
 *  the processor's endian mode.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 */
void
cfg_wr32(uint32_t dest, unsigned long chan,
         unsigned long addr, unsigned int data)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  phys_wr32(addr + chan, cpu_to_le32(data), aar);
}


/** Write two values to an I/O shim configuration register in quick
 *  succession.  Note that this routine is only used in the booter, which
 *  is always little-endian, so no byte-swapping is done.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data0 First data item to write.
 * @param data1 Second data item to write.
 */
void
cfg_double_wr(uint32_t dest, unsigned long chan,
              unsigned long addr, unsigned long data0, unsigned long data1)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  phys_double_wr64(addr + chan, data0, data1, aar);
}


/** Read from an I/O shim configuration register, performing appropriate
 *  byte-swapping as necessary depending on the processor's endian mode.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @return Data item read.
 */
unsigned long
cfg_rd(uint32_t dest, unsigned long chan, unsigned long addr)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  return le64_to_cpu(phys_rd64(addr + chan, aar));
}


/** Read 4 bytes from a 4-byte I/O shim configuration register,
 *  performing appropriate byte-swapping as necessary depending on
 *  the processor's endian mode.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @return Data item read.
 */
unsigned int
cfg_rd32(uint32_t dest, unsigned long chan, unsigned long addr)
{
  unsigned long aar = coord_to_mmio_aar(dest);

  return le32_to_cpu(phys_rd32(addr + chan, aar));
}
