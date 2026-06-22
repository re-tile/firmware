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
 * Routines to access the BME global data structure.
 */

#include <util.h>

#include <arch/spr.h>

#include <bme/sys_info.h>
#include <bme/tlb.h>

#include "bme_state.h"
#include "misc.h"


bme_global_info_t*
bme_map_global_info()
{
  _bme_state_t* bst = _bme_get_state();

  bst->global_info_refcnt++;

  if (bst->global_info)
    return (bst->global_info);

  bst->global_info_index = bme_install_dtte(&bst->local_info->global_info_tte,
                                            BME_TTE_INDEX_WIRED);

  if (bst->global_info_index < 0)
    return (0);

  bst->global_info = bst->local_info->global_info;

  return (bst->global_info);
}


void
bme_unmap_global_info()
{
  _bme_state_t* bst = _bme_get_state();

  if (bst->global_info)
  {
    bst->global_info_refcnt--;
    if (bst->global_info_refcnt == 0)
    {
      bme_remove_dtte(bst->global_info_index);
      bst->global_info = 0;
    }
  }
  else
    panic("too many calls to bme_unmap_global_info()");
}


int
bme_tile_index()
{
  return _bme_get_state()->local_info->index;
}


int
bme_tile_ordinal()
{
  return _bme_get_state()->ordinal;
}


int
bme_num_tiles()
{
  return _bme_get_state()->num_tiles;
}
