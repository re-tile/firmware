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

#include <gxcr/gxcr.h>
#include <gxcr/tokens/bulk_successive.h>

#include "common.h"


#define BULK_SUC_INSTR_00 0x00000000 // header (add packet len)
#define BULK_SUC_INSTR_01 0x00000000 // input packet pointer
#define BULK_SUC_INSTR_02 0x00000000 // output pointer
#define BULK_SUC_INSTR_03 0x00000000 // context pointer
#define BULK_SUC_CCW_0    0x00000000 // placeholder for CCW0
#define BULK_SUC_CCW_1    0x00000000 // placeholder for CCW1
#define BULK_SUC_INSTR_04 0x0f000000 // crypto/hash packet (last data for hash)
#define BULK_SUC_INSTR_05 0x21e60000 // append hash to packet
#define BULK_SUC_INSTR_06 0xe0e63800 // write digest to context record, pass or fail
#define BULK_SUC_INSTR_07 0xe0a63800 // write iv to context record, pass or fail

#define BULK_SUC_DIG_INSTR_00 0x02000000 // header for successive digest
#define BULK_SUC_DIG_INSTR_04 0x0a000000 // hash packet (last data for hash)

#define BULK_SUC_NOP 0x20000004 // NOP

static uint32_t bulk_successive_token_template[] =
  {
    BULK_SUC_INSTR_00,
    BULK_SUC_INSTR_01,
    BULK_SUC_INSTR_02,
    BULK_SUC_INSTR_03,
    BULK_SUC_CCW_0,
    BULK_SUC_CCW_1,
    BULK_SUC_INSTR_04,
    BULK_SUC_INSTR_05,
    BULK_SUC_INSTR_06,
    BULK_SUC_INSTR_07,
  };

#define BULK_SUC_HEADER_OFFSET  0
#define BULK_SUC_PKT_LEN_OFFSET 6
#define BULK_SUC_HASH_LEN_OFFSET 7
#define BULK_SUC_HASH_CTX_ACC_OFFSET 8
#define BULK_SUC_IV_CTX_ACC_OFFSET 9

size_t
bulk_successive_calc_metadata_bytes(int digest_len)
{
  // The metadata size is the token, plus two control words (even though
  // they aren't used), plus the digest field, plus the result token.
  return sizeof(bulk_successive_token_template) +
    sizeof(gxcr_context_control_words_t) + digest_len +
    sizeof(gxcr_result_token_t);
}


int
bulk_successive_setup(gxcr_context_t* crypto_context)
{
  gxcr_token_info_t* token_info = &crypto_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;

  gxcr_context_control_words_t ccw_mem;
  gxcr_read_context_control_words(&ccw_mem, crypto_context->metadata_mem +
                                  crypto_context->token_info.ccw_offset);

  // FIXME: have a conversion somewhere for this (also in crypto.c)
  int block_size = 0;
  switch (ccw_mem.hash_algorithm)
  {
  case 0:
    block_size = GXCR_MD5_DIGEST_FIELD_SIZE;
    break;
  case 1:
  case 2:
    block_size = GXCR_SHA1_DIGEST_FIELD_SIZE;
    break;
  case 3:
  case 4:
    block_size = GXCR_SHA2_224_256_DIGEST_FIELD_SIZE;
    break;
  case 5:
  case 6:
    block_size = GXCR_SHA2_384_512_DIGEST_FIELD_SIZE;
    break;
  default:
    return -1;
  }

  if (ccw_mem.ToP & 2)
  {
    int digest_ctx_len = (block_size / 4) & 0xf;
    int digest_word_offset = (token_info->digest0_offset -
                              token_info->token_len) / 4;
    *(tkn + BULK_SUC_HASH_CTX_ACC_OFFSET) =
      cpu_to_le32(BULK_SUC_INSTR_06 | (digest_ctx_len << 24) |
                  digest_word_offset);
  }
  else
  {
    int iv_ctx_len = (token_info->iv_len / 4) & 0xf;
    int iv_word_offset = (token_info->iv_offset - token_info->token_len) / 4;

    *(tkn + BULK_SUC_IV_CTX_ACC_OFFSET) =
      cpu_to_le32(BULK_SUC_INSTR_07 | (iv_ctx_len << 24) | iv_word_offset);

    // Don't write digest back to context record if we're doing crypto.
    *(tkn + BULK_SUC_HASH_CTX_ACC_OFFSET) = cpu_to_le32(BULK_SUC_NOP);
  }

  return 0;
}

int
bulk_successive_process_start(gxio_mica_context_t* mica_context,
                              gxcr_context_t* crypto_context,
                              void* src, int src_len, void* dst, int dst_len,
                              gxcr_digest_stage_t digest_stage)
{
  gxcr_token_info_t* token_info = &crypto_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;

  // Check the src_len, which must be multiple of
  // crypto_context->src_data_block_size. Assume that block size is power of 2.
  if (!crypto_context->src_data_block_size ||
      src_len & (crypto_context->src_data_block_size - 1))
  {
    return GXCR_BAD_PARAM;
  }

  // FIXME: store pre-configured opcode in crypto_context
  gxio_mica_opcode_t opcode_oplen;
  opcode_oplen.size = src_len;
  opcode_oplen.extra_data_size = token_info->total_len_div_8;
  opcode_oplen.engine_type = MICA_CRYPTO_CTX_USER_OPCODE__ENGINE_TYPE_VAL_PP;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = src == dst ?
    MICA_OPCODE__DEST_MODE_VAL_OVERWRITE_SRC :
    MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dm_specific = token_info->token_len_div_4;

  // FIXME: do a calculation for this, ALSO BASED ON DIGEST LEN if we
  // are appending.  this should definitely get moved to setup.
#if 0
  opcode_oplen.dst_size = 3;    /* add up to 4 bytes. */
#else
  // FIXME: going to have to be 256 unless we're dealing with mod 4 src
  opcode_oplen.dst_size = 7;    /* add up to 128 bytes. */
#endif

  gxcr_context_control_words_t ccw_mem;
  gxcr_read_context_control_words(&ccw_mem, crypto_context->metadata_mem +
                                  crypto_context->token_info.ccw_offset);

  // Check if digest is enabled.
  void* digest_count_ptr = gxcr_digest_count(crypto_context);
  if (digest_count_ptr)
  {
    if (digest_stage != GXCR_DIGEST_STAGE_FINAL)
      if ((src_len % GXCR_DIGEST_SUC_BYTES_PER_BLOCK) != 0)
        return GXCR_BAD_PARAM;

    // If we are in the INIT stage, we want to reset the digest_count field.
    // Otherwise we increment the count by 1 for each
    // GXCR_DIGEST_SUC_BYTES_PER_BLOCK) bytes we've already processed.
    if (digest_stage == GXCR_DIGEST_STAGE_INIT)
    {
      memset(digest_count_ptr, 0, GXCR_DIGEST_COUNT_FIELD_SIZE);
      crypto_context->digest_count = src_len / GXCR_DIGEST_SUC_BYTES_PER_BLOCK;
    }
    else
    {
      memcpy(digest_count_ptr, &crypto_context->digest_count,
             GXCR_DIGEST_COUNT_FIELD_SIZE);
      crypto_context->digest_count += src_len / GXCR_DIGEST_SUC_BYTES_PER_BLOCK;
    }
  }

  int stat =
    (digest_stage == GXCR_DIGEST_STAGE_FINAL && digest_count_ptr) ? 2 : 6;

  if (crypto_context->digest_only)
    *(tkn + BULK_SUC_PKT_LEN_OFFSET) =
      cpu_to_le32(BULK_SUC_DIG_INSTR_04 | (stat << 16) | src_len);
  else
    *(tkn + BULK_SUC_PKT_LEN_OFFSET) =
      cpu_to_le32(BULK_SUC_INSTR_04 | (stat << 16) | src_len);

  // We only want to append the final digest to the end of the output
  // data.  Replace the instruction that appends the interim digest result
  // with a NOP unless this is the final stage.
  if (digest_stage != GXCR_DIGEST_STAGE_FINAL)
    *(tkn + BULK_SUC_HASH_LEN_OFFSET) = cpu_to_le32(BULK_SUC_NOP);
  else if (ccw_mem.ToP & 2)
  {
    // FIXME: store digest_size somewhere or at least do this
    // more efficiently
    int digest_size = 0;
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
    *(tkn + BULK_SUC_HASH_LEN_OFFSET) =
      cpu_to_le32(BULK_SUC_INSTR_05 | digest_size);
  }

  // This is somewhat of a hack.  It appears that the hardware can do
  // successive crypto or successive digest operations, but not both in one
  // operation.
  // The problem is that in order to do successive digest, we need to have
  // the context control words embedded in the token itself.  But, I have not
  // been able to get successive crypto (or any crypto) to work in that mode.
  // I see nothing in the documentation that indicates that it shouldn't work.
  // Update: after asking the vendor, it appears that part of the problem is
  // that we're not setting the context_length field of the ccw.  But even
  // after going down that path it still wasn't working, so there is more
  // work to do.
  // In the meantime I'm not willing to mess up the interface to accomodate
  // the workaround.  This workaround figures out whether we're doing crypto
  // or digest and makes the necessary modifications to the token.
  // Unfortunately the info we need is in the context control words (ccws).
  // The ccws would normally either be in the token (for successive digest) or
  // after the token (for most other operations, including successive crypto).
  // When the ccws are in the token the two words following the token are
  // ignored, so we will always write the ccw info to the location after the
  // token, and if we're doing successive digest we'll also write to the the
  // location in the token.
  if (ccw_mem.ToP & 2)
  {
    // successive digest: write modified ccws back to the token, and
    // set the header to use packet-based operations.
    *(tkn + BULK_SUC_HEADER_OFFSET) =
      cpu_to_le32(BULK_SUC_DIG_INSTR_00 | src_len);

    // Modify the packet-based options in the context control words embedded
    // in the token itself.
    ccw_mem.packet_based_options = digest_stage;

    gxcr_write_context_control_words(&ccw_mem,
                                     crypto_context->metadata_mem + 16);
  }
  else
  {
    // successive crypto: write the modified ccws back to the normal
    // location (after the token), use the normal header.
  *(tkn + BULK_SUC_HEADER_OFFSET) = cpu_to_le32(BULK_SUC_INSTR_00 | src_len);
  }

  gxio_mica_start_op(mica_context, src, dst, crypto_context->metadata_mem,
                     opcode_oplen);

  return GXCR_NO_ERROR;
}


int
bulk_successive_process(gxio_mica_context_t* mica_context,
                        gxcr_context_t* crypto_context,
                        void* src, int src_len, void* dst, int dst_len,
                        gxcr_digest_stage_t digest_stage)
{
  int result = bulk_successive_process_start(mica_context, crypto_context,
                                             src, src_len, dst, dst_len,
                                             digest_stage);
  if (result == GXCR_NO_ERROR)
    gxcr_wait_for_completion(mica_context);

  return result;
}

gxcr_token_info_t bulk_successive_token =
  {
    .token = (unsigned char*)bulk_successive_token_template,
    .token_len = sizeof (bulk_successive_token_template),
    .ccw_offset = sizeof (bulk_successive_token_template),
  };
