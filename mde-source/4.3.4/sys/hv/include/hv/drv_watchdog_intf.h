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
 * Interface definitions for the I2C WATCHDOG driver.
 */

#ifndef _SYS_HV_INCLUDE_DRV_WATCHDOG_INTF_H
#define _SYS_HV_INCLUDE_DRV_WATCHDOG_INTF_H

/** Enable the watchdog. Write-only,
 *  takes a 4-byte value in seconds. */
#define WATCHDOG_ENABLE_OFF   0xF0000000

/** Disable the watchdog. Write-only, takes a dummy argument. */
#define WATCHDOG_DISABLE_OFF   0xF0000004

/** Pat the watchdog. Write-only, takes a dummy argument. */
#define WATCHDOG_PAT_OFF   0xF0000008

/** Get the timer countdown value in effect. Read-only,
 *  returns a 4-byte value in seconds. */
#define WATCHDOG_GET_COUNTDOWN_OFF   0xF000000C

#endif /* _SYS_HV_INCLUDE_DRV_WATCHDOG_INTF_H */
