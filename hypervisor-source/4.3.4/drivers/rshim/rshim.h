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
 * Definitions for the rshim driver.
 */

#ifndef _SYS_HV_DRV_RSHIM_H
#define _SYS_HV_DRV_RSHIM_H

#include "drvintf.h"
#include <hv/hypervisor.h>

/** A state object kept by every tile in the system. */
typedef struct rshim_state
{
  pos_t my_pos;                  /**< This tile's coordinates */
  pos_t fwd_tile;                /**< Forward requests here */
  uint32_t fwd:1;                /**< Forward requests to fwd_tile? */
  const struct dev_info* infop;  /**< Device information */
}
rshim_state_t;

#endif /* _SYS_HV_DRV_RSHIM_H */
