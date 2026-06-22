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
 * Miscellaneous memory shim routines.
 */

#include <arch/spr.h>

#include <limits.h>
#include <stdlib.h>

#include "boot_error.h"
#include "devices.h"
#include "mshim_acc.h"
#include "types.h"

#include "sys/libc/include/util.h"


void
set_cbox_mmap_spr(int index, uint32_t value)
{
  //
  // This routine lets us use loops to do things to all of the memory shims,
  // even though the compiler can't turn a variable into an SPR reference.
  //

  switch (index)
  {
  case 0:
    __insn_mtspr(SPR_CBOX_MMAP_0, value);
    break;

  case 1:
    __insn_mtspr(SPR_CBOX_MMAP_1, value);
    break;

  case 2:
    __insn_mtspr(SPR_CBOX_MMAP_2, value);
    break;

  case 3:
    __insn_mtspr(SPR_CBOX_MMAP_3, value);
    break;

  default:
#ifdef L1BOOT
    boot_error(BOOT_ERR_BAD_CBOX_MMAP_INDEX);
#else
    panic("bogus index %d passed to set_cbox_mmap_spr", index);
#endif
  }
}


int
mshim_portidx_from_pos(pos_t pos, int mshim_idx)
{
  int portidx;
  const struct dev_info* mshim = mshims[mshim_idx];
  if (mshim == NULL)
    panic("bad mshim index %#x in mshim_portidx_from_pos()", mshim_idx);

#ifdef ROUND_ROBIN_MSHIMS
  portidx = (pos.bits.x + pos.bits.y + mshim_idx) % mshim->num_mdn_ports;
#else
  //
  // On Pro, we tweak the distribution slightly here, but on Gx the ports
  // are separated from each other, so straight Manhattan distance is the
  // right way to go.
  //
  int min_dist = INT_MAX;
  int min_port = 0;
  for (int i = 0; i < mshim->num_mdn_ports; i++)
  {
    int dist = manhattan(mshim->mdn_ports[i], pos);
    if (dist <= min_dist)
    {
      min_dist = dist;
      min_port = i;
    }
  }
  portidx = min_port;
#endif /* ! ROUND_ROBIN_MSHIMS */

  return portidx;
}
