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
 * Device driver interface routines for MMIO devices.
 */

#ifndef _SYS_HV_DRVINTF_MMIO_H
#define _SYS_HV_DRVINTF_MMIO_H

//
// Note that these routines are part of the implementation of the MMIO
// permissions manager, but are not designed to be called by drivers;
// use the drv_ versions of the routines, which are defined in drvintf.h,
// instead.
//
int permit_mmio_access(pos_t shimaddr, PA start, PA len);
int deny_mmio_access(pos_t shimaddr, PA start, PA len);

#endif /* _SYS_HV_DRVINTF_MMIO_H */
