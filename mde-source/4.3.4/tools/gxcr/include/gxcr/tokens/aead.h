// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

#ifndef __GXCR_TOKENS_AEAD_H__
#define __GXCR_TOKENS_AEAD_H__


#include <stdint.h>
#include "../gxcr.h"
#include "../aead.h"


#include <gxio/mica.h>

/** Use initialization vector in data packet */
#define AEAD_GENIV_NONE    0

/** Insert initialization vector during encrypt */
#define AEAD_GENIV_INSERT  1

/** Replace initialization vector during encrypt */
#define AEAD_GENIV_REPLACE 0x1000

/** Strip initialization vector during decrypt*/
#define AEAD_GENIV_STRIP   0x1001

/**
 * @addtogroup gxcr_aead_token_
 * @{
 *
 * The AEAD token processes AEAD requests.
 *
 * Before packets are processed with this interface, basic data
 * structures need to be initialized at the gxcr_aead level.
 *
 * The setup function sets initialization-time fields in the token.
 * This must be called before data is processed with the process functions
 * aead_process() or aead_process_start().
 *
 * The processing functions perform the crypto/digest operation.
 * The result of the crypto operation is stored in the destination
 * memory provided as an argument to the process function, the result
 * of a digest operation is appended to the end of the result data.
 */

/** Token for AEAD processing. */
extern gxcr_token_info_t aead_token_info;

/** Token for AEAD processing with AES-GCM. */
extern gxcr_token_info_t aead_aes_gcm_token_info;

/** Function to set up for AEAD processing.
 * @param op_ctx - An AEAD context initialized via gxcr_aead_init_context().
 */
extern int
aead_setup(gxcr_aead_context_t* op_ctx);


/** Function to perform AEAD processing on a packet.  This function kicks off
 * the operation and returns immediately.
 * @param mica_context - An initialized MiCA context.
 * @param op_ctx - An initialized operation-specific AEAD context.
 * @param packet - The packet to be processed.
 * @param packet_len - Length, in bytes, of the packet to be processed.
 * @param assoc_data_len - Length, in bytes, of the data at the start of the
 *   packet, that is to be hashed but not encrypted.
 * @param dst - Destination memory for the result packet.  If dst is equal to
 *   packet, the memory pointed to by packet is overwritten.  The memory
 *   pointed to by packet and by dst must not overlap otherwise.
 * @param dst_len - Length, in bytes, of destination memory for the result
 *   packet.
 * @param icv_len - Length, in bytes, of the Integrity Check Value in the
 *   result packet.
 * @param encrypt - Encrypt then hash the packet if non-zero, hash then
     decrypt the packet if 0.
 * @param geniv - If none-zero and equals AEAD_GENIV_REPLACE, generate and
     replace the geniv_len byte initialization vector(IV) at the end of
     the associate data. If equals AEAD_GENIV_INSERT, generate an initialization
     vector and embed it in the packet as the last part of the associated data.
     If AEAD_GENIV_NONE or 0, assume that the initialization vector is in the
     context record and in the packet.
 * @param geniv_len - Length, in bytes, of the initialization vector to be
 *   generated.
 * @returns 0 on success, error code on failure.
 */
extern int
aead_process_packet_start
(gxio_mica_context_t* mica_context,
 gxcr_aead_context_t* op_ctx,
 void* packet, int packet_len,
 int assoc_data_len,
 void* dst, int dst_len,
 int icv_len,
 int encrypt,
 int geniv, int geniv_len);

/** Function to perform AEAD processing
 * on a packet.  This function blocks until completion.
 * @param mica_context - An initialized MiCA context.
 * @param op_ctx - An initialized operation-specific AEAD context.
 * @param packet - The IPv4 packet to be encapsulated.
 * @param packet_len - Length, in bytes, of the packet to be processed.
 * @param assoc_data_len - Length, in bytes, of the data at the start of the
 *   packet, that is to be hashed but not encrypted.
 * @param dst - Destination memory for the result packet.  If dst is equal to
 *   packet, the memory pointed to by packet is overwritten.  The memory
 *   pointed to by packet and by dst must not overlap otherwise.
 * @param dst_len - Length, in bytes, of destination memory for the result
 *   packet.
 * @param icv_len - Length, in bytes, of the Integrity Check Value in the
 *   result packet.
 * @param encrypt - Encrypt then hash the packet if non-zero, hash then
     decrypt the packet if 0.
 * @param geniv - If none-zero and equals AEAD_GENIV_REPLACE, generate and
     replace the geniv_len byte initialization vector(IV) at the end of
     the associate data. If equals AEAD_GENIV_INSERT, generate an initialization
     vector and embed it in the packet as the last part of the associated data.
     If AEAD_GENIV_NONE or 0, assume that the initialization vector is in the
     context record and in the packet.
 * @param geniv_len - Length, in bytes, of the initialization vector to be
 *   generated.
 * @returns 0 on success, error code on failure.
 */
// FIXME: take care of these fixmes
// FIXME: may want these args to be something more like:
// assoc ptr, assoc len
// encrypt ptr, encrypt len
// dst ptr, dst len
// gen iv ptr, gen iv len (if non-zero, generate iv)
// icv ptr, icv len
// FIXME: may also have to decide encrypt vs decrypt at runtime
extern int
aead_process_packet
(gxio_mica_context_t* mica_context,
 gxcr_aead_context_t* op_ctx,
 void* packet, int packet_len,
 int assoc_data_len,
 void* dst, int dst_len,
 int icv_len,
 int encrypt,
 int geniv, int geniv_len);


/** Function to perform AEAD processing
 * on a packet.  This function blocks until completion.
 * @param mica_context - An initialized MiCA context.
 * @param op_ctx - An initialized operation-specific AEAD context.
 * @param packet - The IPv4 packet to be encapsulated.
 * @param packet_len - Length, in bytes, of the packet to be processed.
 * @param assoc_data_len - Length, in bytes, of the data at the start of the
 *   packet, that is to be hashed but not encrypted.
 * @param aad_len - Length, in byte, of the AAD of AES-GCM algorithm.
 * @param dst - Destination memory for the result packet.  If dst is equal to
 *   packet, the memory pointed to by packet is overwritten.  The memory
 *   pointed to by packet and by dst must not overlap otherwise.
 * @param dst_len - Length, in bytes, of destination memory for the result
 *   packet.
 * @param icv_len - Length, in bytes, of the Integrity Check Value in the
 *   result packet.
 * @param encrypt - Encrypt then hash the packet if non-zero, hash then
     decrypt the packet if 0.
 * @param geniv - If none-zero and equals AEAD_GENIV_REPLACE, generate and
     replace the geniv_len byte initialization vector(IV) at the end of
     the associate data. If equals AEAD_GENIV_INSERT, generate an initialization
     vector and embed it in the packet as the last part of the associated data.
     If AEAD_GENIV_NONE or 0, assume that the initialization vector is in the
     context record and in the packet.
 * @param geniv_len - Length, in bytes, of the initialization vector to be
 *   generated.
 * @returns 0 on success, error code on failure.
 */

extern int
aead_process_packet_generic
(gxio_mica_context_t* mica_context,
 gxcr_aead_context_t* op_ctx,
 void* packet, int packet_len,
 int assoc_data_len, int aad_len,
 void* dst, int dst_len,
 int icv_len,
 int encrypt,
 int geniv, int geniv_len);

/** @} */

#endif // __GXCR_TOKENS_AEAD_H__
