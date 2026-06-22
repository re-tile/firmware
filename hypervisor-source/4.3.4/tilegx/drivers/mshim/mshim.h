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
 * Definitions for the memory shim driver.
 */

#ifndef _SYS_HV_DRV_MSHIM_H
#define _SYS_HV_DRV_MSHIM_H

#include <hv/drv_mshim_intf.h>

#include "drvintf.h"


/** Device state structure. */
typedef struct
{
  /** Shim instance number. */
  int instance;

  /** Shim location. */
  pos_t shim_pos;
 
  /** Shim channel. */
  unsigned long shim_chan;
 
  /** Nonzero if ECC is enabled on this shim. */
  int ecc;

  /** Counts of single-bit ECC errors.  We update this via fetchadd so
   *  that we don't need to lock. */
  uint32_t single_bit_err_cnt;
}
mshim_state_t;

#endif /* _SYS_HV_DRV_MSHIM_H */
