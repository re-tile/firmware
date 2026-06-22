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
 * rshim driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/rsh.h>


#include "sys/libc/include/util.h"

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "rshim.h"
#include "hv.h"
#include "types.h"


/** RShim driver probe routine. */
static int
rshim_probe(const char* drvname, int instance,
            pos_t tile, const struct dev_info* info)
{
  if (instance >= MAX_RSHIMS)
    return HV_ENODEV;

  rshims[instance] = info;

  return 0;
}

//
// We haven't yet ported the counter support to Gx, but we want the probe
// routine to work so that other drivers can find the rshim.
//

/** RShim driver operations vector */
static struct drv_ops rshim_ops = {
  .probe       = rshim_probe,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_rshim = {
  .shim_type  = RSH_DEV_INFO__TYPE_VAL_RSHIM,
  .name       = "rshim",
  .desc       = "Miscellaneous I/O",
  .ops        = &rshim_ops,
  .stilereq   = 1,
};
