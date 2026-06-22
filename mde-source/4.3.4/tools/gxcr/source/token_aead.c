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
#include <gxcr/aead.h>
#include <gxcr/tokens/aead.h>

#include "common.h"

#define AEAD_INSTR_00 0x00000000 // header (add packet len)
#define AEAD_INSTR_01 0x00000000 // input packet pointer
#define AEAD_INSTR_02 0x00000000 // output pointer
#define AEAD_INSTR_03 0x00000000 // context pointer
#define AEAD_INSTR_04 0x03000000 // hash associated data
#define AEAD_INSTR_06 0x0f020000 // hash/crypto rest of packet, last hash

// insert IV into packet, and hash
#define ENCRYPT_INSTR_05_iv_insert 0x23a00000
// replace IV into packet, and hash
#define ENCRYPT_INSTR_05_iv_replace 0x33a00000
// get explicit iv from packet, load into regs
#define ENCRYPT_INSTR_05_iv_load    0x43a00000
// get explicit iv from packet, load into regs and strip IV
#define ENCRYPT_INSTR_05_iv_strip   0x42a00000

#define ENCRYPT_INSTR_07 0x21e60000 // insert icv

#define DECRYPT_INSTR_07 0x40e60000 // get icv from hash engine
#define DECRYPT_INSTR_08 0xd0070000 // verify icv in pkt

#define NOP_INSTR       0x20000004
#define IIR_INSTR       0xa0800000  // Result remove
#define ZERO_INSTR      0x25000010  // Insert 16 zero bytes

static uint32_t aead_token[] =
  {
    AEAD_INSTR_00,
    AEAD_INSTR_01,
    AEAD_INSTR_02,
    AEAD_INSTR_03,
    AEAD_INSTR_04,
    NOP_INSTR,
    AEAD_INSTR_06,
    NOP_INSTR,
    NOP_INSTR
  };

static uint32_t aead_aes_gcm_token[12] =
  {
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR,
    NOP_INSTR
  };

#define ENCRYPT_PACKET_LEN_OFFSET          0
#define ENCRYPT_HASH_ONLY_LEN_OFFSET       4
#define ENCRYPT_HASH_IV_LEN_OFFSET         5
#define ENCRYPT_CRYPTO_AND_HASH_LEN_OFFSET 6
#define ENCRYPT_ICV_LEN_OFFSET             7
#define ENCRYPT_VERIFY_ICV_LEN_OFFSET      8

static unsigned short
round_up_to_power_of_2(unsigned short x)
{
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x++;

  return x;
}


int
aead_setup(gxcr_aead_context_t* op_ctx)
{
  gxcr_token_info_t* token_info = &op_ctx->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;
  int i;

  // Copy token into metadata memory
  for (i = 0;
       i < sizeof(aead_token) / sizeof(*aead_token);
       i++)
    *(tkn + i) = cpu_to_le32(aead_token[i]);

#define MAX_ICV 64
  token_info->dst_size_code = __insn_ctz(round_up_to_power_of_2(MAX_ICV)) + 1;

  return 0;
}


int
aead_process_packet_start(gxio_mica_context_t* mica_context,
                          gxcr_aead_context_t* aead_context,
                          void* src, int src_len,
                          int assoc_data_len,
                          void* dst, int dst_len, int icv_len, int encrypt,
                          int geniv, int geniv_len)
{
  // Make the necessary per-packet modifications to the token.
  gxcr_token_info_t* token_info = &aead_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;
  gxio_mica_opcode_t opcode_oplen;
  gxcr_context_control_words_t ccw_mem;
  int cryptlen = 0;

  if ((assoc_data_len + icv_len >= src_len) ||
      ((geniv && (geniv != AEAD_GENIV_STRIP)) && !encrypt))
    return GXCR_BAD_PARAM;

  // See section 8.2.1.1 of the EIP-96 documentation.
  if (geniv && encrypt)
  {
    uint32_t token;

    *(tkn + ENCRYPT_PACKET_LEN_OFFSET) =
      cpu_to_le32(AEAD_INSTR_00 | 0x04000000 | (src_len));

    *(tkn + ENCRYPT_HASH_ONLY_LEN_OFFSET) =
      cpu_to_le32(AEAD_INSTR_04 | (assoc_data_len - geniv_len));

    if (geniv == AEAD_GENIV_REPLACE)
      token = ENCRYPT_INSTR_05_iv_replace | geniv_len;
    else
      token = ENCRYPT_INSTR_05_iv_insert | geniv_len;

    *(tkn + ENCRYPT_HASH_IV_LEN_OFFSET) =
      cpu_to_le32(token);
  }
  else
  {
    *(tkn + ENCRYPT_PACKET_LEN_OFFSET) =
      cpu_to_le32(AEAD_INSTR_00 | src_len);

    *(tkn + ENCRYPT_HASH_ONLY_LEN_OFFSET) =
      cpu_to_le32(AEAD_INSTR_04 | assoc_data_len);
  }

  gxcr_read_context_control_words(&ccw_mem, aead_context->metadata_mem +
                                  aead_context->token_info.ccw_offset);

  if (encrypt)
  {
    ccw_mem.ToP = 6;
    *(tkn + ENCRYPT_ICV_LEN_OFFSET) = cpu_to_le32(ENCRYPT_INSTR_07 | icv_len);
    cryptlen = src_len;
    if (!geniv && geniv_len)
    {
      *(tkn + ENCRYPT_HASH_ONLY_LEN_OFFSET) =
        cpu_to_le32(AEAD_INSTR_04 | (assoc_data_len - geniv_len));
      *(tkn + ENCRYPT_HASH_IV_LEN_OFFSET) =
        cpu_to_le32(ENCRYPT_INSTR_05_iv_load | geniv_len);
    }
    else if (geniv == AEAD_GENIV_INSERT)
    {
      cryptlen += geniv_len;
    }
  }
  else
  {
    ccw_mem.ToP = 0xf;
    *(tkn + ENCRYPT_ICV_LEN_OFFSET) = cpu_to_le32(DECRYPT_INSTR_07 | icv_len);
    *(tkn + ENCRYPT_VERIFY_ICV_LEN_OFFSET) =
      cpu_to_le32(DECRYPT_INSTR_08 | icv_len);
    cryptlen = src_len - icv_len;

    if (geniv_len)
    {
      *(tkn + ENCRYPT_HASH_ONLY_LEN_OFFSET) =
        cpu_to_le32(AEAD_INSTR_04 | (assoc_data_len - geniv_len));

      if (geniv != AEAD_GENIV_STRIP)
      {
        *(tkn + ENCRYPT_HASH_IV_LEN_OFFSET) =
          cpu_to_le32(ENCRYPT_INSTR_05_iv_load | geniv_len);
      }
      else
      {
        *(tkn + ENCRYPT_HASH_IV_LEN_OFFSET) =
          cpu_to_le32(ENCRYPT_INSTR_05_iv_strip | geniv_len);
      }
    }
  }

  *(tkn + ENCRYPT_CRYPTO_AND_HASH_LEN_OFFSET) =
    cpu_to_le32(AEAD_INSTR_06 | (cryptlen - assoc_data_len));

  gxcr_write_context_control_words(&ccw_mem,
                                   aead_context->metadata_mem +
                                   aead_context->token_info.ccw_offset);

  // Put together the opcode.
  opcode_oplen.size = src_len;
  opcode_oplen.extra_data_size = token_info->total_len_div_8;
  opcode_oplen.engine_type = MICA_CRYPTO_CTX_USER_OPCODE__ENGINE_TYPE_VAL_PP;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = src == dst ?
    MICA_OPCODE__DEST_MODE_VAL_OVERWRITE_SRC :
    MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dm_specific = token_info->token_len_div_4;
  opcode_oplen.dst_size = token_info->dst_size_code;

  gxio_mica_start_op(mica_context, src, dst, aead_context->metadata_mem,
                     opcode_oplen);

  return 0;
}

static int
aead_process_packet_start_aes_gcm(gxio_mica_context_t* mica_context,
                                  gxcr_aead_context_t* aead_context,
                                  void* src, int src_len,
                                  int assoc_data_len, int aad_len,
                                  void* dst, int dst_len, int icv_len,
                                  int encrypt, int geniv, int geniv_len)
{
  // Make the necessary per-packet modifications to the token.
  gxcr_token_info_t* token_info = &aead_context->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;
  gxio_mica_opcode_t opcode_oplen;
  gxcr_context_control_words_t ccw_mem;

  if (assoc_data_len + icv_len >= src_len)
    return GXCR_BAD_PARAM;

  gxcr_read_context_control_words(&ccw_mem, aead_context->metadata_mem +
                                  aead_context->token_info.ccw_offset);

  ccw_mem.IV1 = ccw_mem.IV2 = 0;

  if (encrypt)
  {
    if (!((geniv == AEAD_GENIV_REPLACE || geniv == 0) &&
          (geniv_len == 0 || geniv_len == 8)))
      return GXCR_BAD_PARAM;

    if (geniv == 0)
      // IVs are from the input packet.
      *(tkn + 0) = cpu_to_le32(AEAD_INSTR_00 | (src_len));
    else
      // IVs are from PRNG
      *(tkn + 0) = cpu_to_le32(AEAD_INSTR_00 | 0x04000000 | (src_len));

    // Output the hdr part in assoc data.
    if (aad_len + 8 < assoc_data_len)
      *(tkn + 4) = cpu_to_le32(0x01000000 |
                               (assoc_data_len - aad_len - 8));
    else
      *(tkn + 4)  = cpu_to_le32(NOP_INSTR);

    // Hash AAD.
    *(tkn + 5) = cpu_to_le32(0x0b000000 | aad_len);

    if (geniv == 0)
    {
      if (geniv_len)
        // Read the IV1/2 from input packet
        *(tkn + 6) = cpu_to_le32(0x41a80008);
      else
      {
        // Use the IV1/2 from CR
        ccw_mem.IV1 = 1;
        ccw_mem.IV2 = 1;
        *(tkn + 6) = cpu_to_le32(0x31a80008);
      }
    }
    else
      // Replace IV1 and 2
      *(tkn + 6) = cpu_to_le32(0x31a80008);

    // Encrypt the data
    *(tkn + 9) = cpu_to_le32(0x0f020000 | (src_len - assoc_data_len));

    // Insert ICV
    *(tkn + 10) = cpu_to_le32(0x21e60000 | icv_len);

    //*(tkn + 11) = cpu_to_le32(NOP_INSTR);

    ccw_mem.ToP = 6;
  }
  else
  {
    if (geniv || (geniv_len != 0 && geniv_len != 8))
      return GXCR_BAD_PARAM;

    // Token control
    *(tkn + ENCRYPT_PACKET_LEN_OFFSET) =
      cpu_to_le32(AEAD_INSTR_00 | (src_len));

    // Output the hdr part in assoc data.
    if (aad_len + 8 < assoc_data_len)
      *(tkn + 4) = cpu_to_le32(0x01000000 |
                               (assoc_data_len - aad_len - 8));
    else
      *(tkn + 4)  = cpu_to_le32(NOP_INSTR);

    // Hash AAD.
    *(tkn + 5) = cpu_to_le32(0x0b000000 | aad_len);

    if (geniv_len)
      // Load IV1/2 from input packet
      *(tkn + 6) = cpu_to_le32(0x41a80008);
    else
    {
      // Use IV1/2 from CR
      ccw_mem.IV1 = 1;
      ccw_mem.IV2 = 1;
      *(tkn + 6) = cpu_to_le32(0x31a80008);
    }

    // Decrypt the data
    *(tkn + 9) = cpu_to_le32(0x0f020000 | (src_len - assoc_data_len - icv_len));

    // Get ICV from hash engine
    *(tkn + 10) = cpu_to_le32(0x40e60000 | icv_len);

    // Verify the ICV
    *(tkn + 11) = cpu_to_le32(0xd0070000 | icv_len);

    ccw_mem.ToP = 0xf;
  }

  // Result removal
  *(tkn + 7) = cpu_to_le32(IIR_INSTR + assoc_data_len);

  // Insert 16 zero bytes
  *(tkn + 8) = cpu_to_le32(ZERO_INSTR);

  gxcr_write_context_control_words(&ccw_mem,
                                   aead_context->metadata_mem +
                                   aead_context->token_info.ccw_offset);

  // Put together the opcode.
  opcode_oplen.size = src_len;
  opcode_oplen.extra_data_size = token_info->total_len_div_8;
  opcode_oplen.engine_type = MICA_CRYPTO_CTX_USER_OPCODE__ENGINE_TYPE_VAL_PP;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = src == dst ?
    MICA_OPCODE__DEST_MODE_VAL_OVERWRITE_SRC :
    MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dm_specific = token_info->token_len_div_4;
  opcode_oplen.dst_size = token_info->dst_size_code;

  gxio_mica_start_op(mica_context, src, dst, aead_context->metadata_mem,
                     opcode_oplen);

  return 0;
}


int
aead_process_packet(gxio_mica_context_t* mica_context,
                    gxcr_aead_context_t* op_ctx,
                    void* src, int src_len,
                    int assoc_data_len,
                    void* dst, int dst_len,
                    int icv_len, int encrypt, int geniv, int geniv_len)
{
  int retval = aead_process_packet_start(
    mica_context, op_ctx, src, src_len, assoc_data_len,
    dst, dst_len, icv_len, encrypt, geniv, geniv_len);

  if (retval)
    return retval;

  gxcr_wait_for_completion(mica_context);

  return 0;
}

int
aead_process_packet_generic(gxio_mica_context_t* mica_context,
                            gxcr_aead_context_t* op_ctx,
                            void* src, int src_len,
                            int assoc_data_len, int aad_len,
                            void* dst, int dst_len, int icv_len,
                            int encrypt, int geniv, int geniv_len)
{
  int retval;

  if (op_ctx->aes_gcm)
    retval = aead_process_packet_start_aes_gcm(
      mica_context, op_ctx, src, src_len, assoc_data_len, aad_len,
      dst, dst_len, icv_len, encrypt, geniv, geniv_len);
  else
    retval = aead_process_packet_start(
      mica_context, op_ctx, src, src_len, assoc_data_len,
      dst, dst_len, icv_len, encrypt, geniv, geniv_len);

  if (retval)
    return retval;

  gxcr_wait_for_completion(mica_context);

  return 0;
}


gxcr_token_info_t aead_token_info =
  {
    .token = (unsigned char*)aead_token,
    .token_len = sizeof (aead_token),
    .ccw_offset = sizeof (aead_token)
  };

gxcr_token_info_t aead_aes_gcm_token_info =
  {
    .token = (unsigned char*)aead_aes_gcm_token,
    .token_len = sizeof (aead_aes_gcm_token),
    .ccw_offset = sizeof (aead_aes_gcm_token)
  };
