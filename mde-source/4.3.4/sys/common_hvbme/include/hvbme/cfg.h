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

#ifndef _SYS_COMMON_HVBME_CFG_H
#define _SYS_COMMON_HVBME_CFG_H

#include <stdint.h>

/**
 * @file
 * Routines to do shim configuration operations, shared between the
 * hypervisor and the BME.
 */

/** Write to an I/O shim configuration register.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).  The FB bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 */
void cfg_wr(uint32_t dest, unsigned long chan, unsigned long addr,
            unsigned long data);

/** Read from an I/O shim configuration register.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).  The FB bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @return Data item read.
 */
unsigned long cfg_rd(uint32_t dest, unsigned long chan, unsigned long addr);

/** Write 4 bytes to a 4-byte I/O shim configuration register.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).  The FB bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 */
void cfg_wr32(uint32_t dest, unsigned long chan, unsigned long addr,
              unsigned int data);

/** Read 4 bytes from a 4-byte I/O shim configuration register.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).  The FB bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @return Data item read.
 */
unsigned int cfg_rd32(uint32_t dest, unsigned long chan, unsigned long addr);

#endif
