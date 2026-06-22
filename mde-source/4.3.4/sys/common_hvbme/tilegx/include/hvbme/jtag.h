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
 * JTAG routines.
 */

#ifndef _SYS_COMMON_HVBME_JTAG_H
#define _SYS_COMMON_HVBME_JTAG_H

#include <stdint.h>

/** Write bits to a JTAG scan chain.
 * @param port Tile coordinates of the rshim.
 * @param channel Channel number of the rshim.
 * @param data Data bit string to send out JTAG.
 * @param jtag_inst JTAG instruction.
 * @param num_bits Number of bits to send.
 */
void rshim_jtag_send(uint32_t port, unsigned long channel,
                     const unsigned long* data, uint32_t jtag_inst,
                     int num_bits);

/** Insert a word into a bit string.
 * @param dest Destination bit string.  This is little-endian; the first bit
 *  of the string (bit position 0) is the least significant bit of dest[0].
 * @param dest_lsb Bit position of the least significant bit of the destination
 *  field.
 * @param dest_msb Bit position of the most significant bit of the destination
 *  field.  Must be no larger than dest_lsb + 8 * sizeof (unsigned long).
 * @param src Source word; the least significant bit of this value
 *  is placed into the dest_lsb'th bit of the destination, and so forth,
 *  stopping when the dest_msb'th bit of the destination is reached.
 */
void bit_insert(unsigned long* dest, int dest_lsb, int dest_msb,
                unsigned long src);

#endif /* _SYS_COMMON_HVBME_JTAG_H */
