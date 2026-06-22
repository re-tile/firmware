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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <gxcr/tokens/bulk_crypto_digest.h>

#include "common.h"

#define BULK_INSTR_00 0x00000000 // header
#define BULK_INSTR_01 0x00000000 // input packet pointer
#define BULK_INSTR_02 0x00000000 // output pointer
#define BULK_INSTR_03 0x00000000 // context pointer
#define BULK_CCW_0    0x00000000 // context control word 0
#define BULK_CCW_1    0x00000000 // context control word 1
#define BULK_INSTR_04 0x0f020000 // crypto/hash packet (last data for hash)
#define BULK_INSTR_05 0x21e60000 // append hash to packet

#define BULK_DIG_INSTR_04 0x0a020000 // hash packet (last data for hash)


uint32_t bulk_crypto_digest[] =
  {
    BULK_INSTR_00,
    BULK_INSTR_01,
    BULK_INSTR_02,
    BULK_INSTR_03,
    BULK_INSTR_04,
    BULK_INSTR_05,
  };

#define BULK_PACKET_LEN_OFFSET            0
#define BULK_ORIG_PAYLOAD_LEN_OFFSET      4
#define BULK_HASH_LEN_OFFSET              5


int
bulk_crypto_digest_setup(gxcr_context_t* crypto_context)
{
  gxcr_token_info_t* token_info = &crypto_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;
  int digest_size = 0;
  gxcr_context_control_words_t ccw_mem;
  gxcr_read_context_control_words(&ccw_mem, crypto_context->metadata_mem + 
                                  crypto_context->token_info.ccw_offset);

  // FIXME: improve this.  Also similar code exists in crypto.c
  switch (ccw_mem.hash_algorithm)
  {
  case 0:
    digest_size = GXCR_MD5_DIGEST_SIZE;
    break;
  case 1:
  case 2:
    digest_size = GXCR_SHA1_DIGEST_SIZE;
    break;
  case 3:
    digest_size = GXCR_SHA2_256_DIGEST_SIZE;
    break;
  case 4:
    digest_size = GXCR_SHA2_224_DIGEST_SIZE;
    break;
  case 5:
    digest_size = GXCR_SHA2_512_DIGEST_SIZE;
    break;
  case 6:
    digest_size = GXCR_SHA2_384_DIGEST_SIZE;
    break;
  default:
    return -1;
  }

  // Add token BULK_INSTR_05 only if ccw_mem.hash_store is true. Otherwise
  // BULK_INSTR_05 with MD5 digest could be enabled as default when hash
  // operation is off.
  if (ccw_mem.hash_store)
    *(tkn + BULK_HASH_LEN_OFFSET) = cpu_to_le32(BULK_INSTR_05 | digest_size);

  return 0;
}

int
bulk_crypto_digest_process_start(gxio_mica_context_t* mica_context,
                                 gxcr_context_t* crypto_context,
                                 void* src, int src_len, 
                                 void* dst, int dst_len, 
                                 gxcr_digest_stage_t digest_stage)
{
  gxcr_token_info_t* token_info = &crypto_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;
  gxio_mica_opcode_t opcode_oplen;

  if (digest_stage != GXCR_DIGEST_STAGE_FINAL)
    return GXCR_OPERATION_NOT_SUPPORTED;

  // Check the src_len, which must be multiple of
  // crypto_context->src_data_block_size. Assume that block size is power of 2.
  if (!crypto_context->src_data_block_size ||
      src_len & (crypto_context->src_data_block_size - 1))
  {
    return GXCR_BAD_PARAM;
  }

  *(tkn + BULK_PACKET_LEN_OFFSET) = cpu_to_le32(BULK_INSTR_00 | src_len);

  if (crypto_context->digest_only)
    *(tkn + BULK_ORIG_PAYLOAD_LEN_OFFSET) = 
      cpu_to_le32(BULK_DIG_INSTR_04 | src_len);
  else
    *(tkn + BULK_ORIG_PAYLOAD_LEN_OFFSET) = 
      cpu_to_le32(BULK_INSTR_04 | src_len);

  // FIXME: store pre-configured opcode in crypto_context
  opcode_oplen.size = src_len;
  opcode_oplen.extra_data_size = token_info->total_len_div_8;
  opcode_oplen.engine_type = MICA_CRYPTO_CTX_USER_OPCODE__ENGINE_TYPE_VAL_PP;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = src == dst ? 
    MICA_OPCODE__DEST_MODE_VAL_OVERWRITE_SRC :
    MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dm_specific = token_info->token_len_div_4;

  // FIXME: this will depend on block size.  Also for block=512 this 
  // probably needs to be 8 (for 256, 128B + up to 3 bytes if src len is
  // not mod 4)
  opcode_oplen.dst_size = 7;

  gxio_mica_start_op(mica_context, src, dst, crypto_context->metadata_mem,
                     opcode_oplen);

  return 0;
}


int
bulk_crypto_digest_process(gxio_mica_context_t* mica_context,
                           gxcr_context_t* crypto_context,
                           void* src, int src_len,
                           void* dst, int dst_len,
                           gxcr_digest_stage_t digest_stage)
{
  int retval = bulk_crypto_digest_process_start(mica_context, crypto_context,
                                                src, src_len, dst, dst_len,
                                                digest_stage);

  if (retval)
    return retval;

  gxcr_wait_for_completion(mica_context);

  return 0;
}


gxcr_token_info_t bulk_crypto_digest_token = 
  {
    .token = (unsigned char*)bulk_crypto_digest,
    .token_len = sizeof (bulk_crypto_digest),
    .ccw_offset = sizeof (bulk_crypto_digest),
  };
