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
 * Device driver channel definitions.
 */

#ifndef _SYS_HV_TILEGX_DRVCHAN_H
#define _SYS_HV_TILEGX_DRVCHAN_H

#include "bits.h"

//
// Define size of the channel bits in a message.
//
#define DRV_CHAN_WIDTH 7                                  ///< Chan bits width
#define DRV_CHAN_SHIFT 0                                  ///< Chan bits shift
#define DRV_CHAN_RMASK RMASK(DRV_CHAN_WIDTH)              ///< Chan bits RJ mask
#define DRV_CHAN_MASK  (DRV_CHAN_RMASK << DRV_CHAN_SHIFT) ///< Chan bits mask

//
// In most cases, drivers should use the dynamically-allocated channel numbers
// found in their info structures.  In cases where this is not possible -- for
// instance, where the channel number must be used as a constant in assembly-
// language code -- a definition may be added to this file.
//

#define DRV_CHAN_UART         0  /**< UART messages */
#define DRV_CHAN_MESSAGE      1  /**< Hypervisor messages */
#define DRV_CHAN_REPLY        2  /**< Hypervisor replies */

#define DRV_CHAN_MAX   2  /**< Largest statically defined channel number */

#endif /* _SYS_HV_DRVCHAN_H */
