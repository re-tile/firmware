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
 *
 * Routines to perform shim configuration operations.  These functions send
 * messages over the IDN to I/O shims, retrieving or modifying the contents
 * of shim configuration registers.  These routines send IDN messages, and
 * expect to be able to accept replies, on IDN0; that endpoint must be
 * enabled (with any tag), and message reception on it must not provoke any
 * interrupts.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_CFG_H
#define _SYS_BME_CFG_H

#include <features.h>

#include <bme/types.h>

__BEGIN_DECLS

/** Number of times cfg_probe() checks for a reply from an I/O shim before
 *  deciding it doesn't exist. */
#define SHIM_PROBE_TIMEOUT 4000

/** Write to an I/O shim configuration register.
 * @param dest Destination.  The FB bit is always set on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 */
void bme_cfg_wr(pos_t dest, uint32_t chan, uint32_t addr, uint32_t data);

/** Read from an I/O shim configuration register.
 * @param dest Destination.  The FB bit is always set on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @return Data item read.
 */
uint32_t bme_cfg_rd(pos_t dest, uint32_t chan, uint32_t addr);

/** Probe an I/O shim configuration register.  Tries to read the register,
 *  and times out if no response is received.
 * @param dest Destination.  The Final Route Selection (FB) bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param rval Pointer where the read value is written, if the return value
 *        is 1.
 * @return 1 if the register was successfully read, else 0.
 */
int bme_cfg_probe(pos_t dest, uint32_t chan, uint32_t addr, uint32_t* rval);

__END_DECLS

#endif /* _SYS_BME_CFG_H */

/** @} */
