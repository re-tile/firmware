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
 * Prototypes for I2C access routines.
 */

#ifndef _SYS_COMMON_HVBME_I2C_H
#define _SYS_COMMON_HVBME_I2C_H

//
// I2C device flags- ORed into the "dev" argument to i2c routines.
//

/** This device wants 16-bit addresses. */
#define I2C_DEV_16BIT      0x100
/** This device wants no address; available on TILE-Gx, ignored elsewhere. */
#define I2C_DEV_NOADDR     0x200
/** Actual 8-bit device part of the dev argument. */
#define I2C_DEV_ADDR_MASK  0x0FF

//
// I2C access routines.
//

//
// These routines are called by the Ethernet link management code, which is
// built as part of the common HV/BME library, and thus they must be
// defined in a common HV/BME header file.  However, they aren't yet
// implemented in the BME, so we don't document them there.
//
#ifndef __DOXYGEN__
int i2c_rd_bus(int bus, int dev, int addr, int len, void* buf);
int i2c_wr_bus(int bus, int dev, int addr, int len, void* buf);
int i2c_switch_swing(int bus, int switch_inst, int switch_chan);
void i2c_switch_release(int bus, int switch_inst);
#endif

#endif /* _SYS_COMMON_HVBME_I2C_H */
