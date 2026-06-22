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

#include <gxcr/tokens/esp_ipv4_tunnel_ibound.h>

#include "common.h"

#define INBND_INSTR_00 0x01400000 // header (add packet len)
#define INBND_INSTR_01 0x00000000 // input packet pointer
#define INBND_INSTR_02 0x00000000 // output pointer
#define INBND_INSTR_03 0x00000000 // context pointer
#define INBND_INSTR_04 0x40d80000 // chop off enet hdr and outer ip hdr
#define INBND_INSTR_05 0x42900000 // verify esp hdr (add 8 for esp hdr len)
#define INBND_INSTR_06 0x42a00000 // get explicit iv from packet, load into regs
#define INBND_INSTR_AES_GCM_06     0x40a80000 // Get iv from packet.
#define INBND_INSTR_CR_IV0_07      0x20a00004 // Load 4 octet salt from CR
#define INBND_INSTR_GCM_RESULT_08  0xa0800000 // Result removal, Length = 16
#define INBND_INSTR_GCM_INSERT_09  0x25000010 // Insert 1 zero block
#define INBND_INSTR_10 0x0f020000 // crypto/hash packet (add original payload len)
#define INBND_INSTR_11 0x40e6000c // get icv from ctx rec
#define INBND_INSTR_12 0xdd07000c // verify spi, seq num, and icv in pkt
#define INBND_INSTR_13 0xe32e1800 // update ctx rec
#define INBND_INSTR_NOP 0x20000004

static uint32_t esp_inbound_ipv4_tunnel[] =
  {
    INBND_INSTR_00,
    INBND_INSTR_01,
    INBND_INSTR_02,
    INBND_INSTR_03,
    INBND_INSTR_04,
    INBND_INSTR_05,
    INBND_INSTR_06,
    INBND_INSTR_CR_IV0_07,
    INBND_INSTR_NOP,
    INBND_INSTR_NOP,
    INBND_INSTR_10,
    INBND_INSTR_11,
    INBND_INSTR_12,
    INBND_INSTR_13
  };

#define INBND_PACKET_LEN_OFFSET            0
#define INBND_OUTER_IP_HDR_LEN_OFFSET      4
#define INBND_ESP_HDR_LEN_OFFSET           5
#define INBND_IV_LEN_OFFSET                6

#define INBND_INSTR_GCM_RESULT_OFFSET      8
#define INBND_INSTR_GCM_INSERT_OFFSET      9
#define INBND_ORIG_PAYLOAD_LEN_OFFSET     10
#define INBND_CTX_SEQNUM_OFFSET           13

#define ICV_LEN 12

// These functions are specific to the token, and therefore already
// know the length
int
ipsec_esp_ipv4_tunnel_ibound_setup
(ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 int outer_ip_hdr_len,
 int esp_hdr_len)
{
  // FIXME: validate args

  // FIXME: make these macro names match better, so that this is a little
  // easier to check visually

  gxcr_token_info_t* token_info = &op_sa->ipsec_sa.token_info;
  assert(token_info);
  uint32_t* tkn = (uint32_t*)token_info->token;
  assert(tkn);

  // Save off information that will be needed at runtime.
  int iv_len = token_info->iv_len;
  op_sa->nonpayload_len = outer_ip_hdr_len + esp_hdr_len + iv_len + ICV_LEN;

  assert(token_info->token);

  // Copy token into metadata memory
  for (int i = 0;
       i < sizeof(esp_inbound_ipv4_tunnel) / sizeof(*esp_inbound_ipv4_tunnel);
       i++)
    *(tkn + i) = cpu_to_le32(esp_inbound_ipv4_tunnel[i]);

  *(tkn + INBND_OUTER_IP_HDR_LEN_OFFSET) =
    cpu_to_le32(INBND_INSTR_04 | outer_ip_hdr_len);

  if (op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_128 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_192 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_256)
  {
    /* Remove 4 octet salt, b/c it is not transmitted. */
    op_sa->nonpayload_len -= 4;

    /* Add "L" bit in INSTR_05 */
    *(tkn + INBND_ESP_HDR_LEN_OFFSET) =
      cpu_to_le32(INBND_INSTR_05 | 0x08000000 | esp_hdr_len);
    /* fill-in two nop tokens */
    *(tkn + INBND_INSTR_GCM_RESULT_OFFSET) =
      cpu_to_le32(INBND_INSTR_GCM_RESULT_08);
    *(tkn + INBND_INSTR_GCM_INSERT_OFFSET) =
      cpu_to_le32(INBND_INSTR_GCM_INSERT_09);

    *(tkn + INBND_IV_LEN_OFFSET) =
      cpu_to_le32(INBND_INSTR_AES_GCM_06 | 8);
  }
  else
  {
    *(tkn + INBND_ESP_HDR_LEN_OFFSET) =
      cpu_to_le32(INBND_INSTR_05 | esp_hdr_len);

    *(tkn + INBND_IV_LEN_OFFSET) =
      cpu_to_le32(INBND_INSTR_06 | iv_len);
  }

  *(tkn + INBND_CTX_SEQNUM_OFFSET) =
    cpu_to_le32(INBND_INSTR_13 | ((token_info->seqnum_offset -
                                   token_info->token_len) / 4));

  // This is the maximum amount by which a result packet is larger, for
  // this specific token operation and algorithm combination.
  op_sa->additional_packet_len = 0;

  // This token does not require that the destination memory be larger
  // than the original source data.
  token_info->dst_size_code = 0;

  return 0;
}

static void inline
ipsec_esp_ipv4_tunnel_ibound_runtime_token_mod(void* token,
                                               int packet_len,
                                               int payload_len)
{
  uint32_t* tkn = (uint32_t*)token;

  *(tkn + INBND_PACKET_LEN_OFFSET) =
    cpu_to_le32(INBND_INSTR_00 | packet_len);

  *(tkn + INBND_ORIG_PAYLOAD_LEN_OFFSET) =
    cpu_to_le32(INBND_INSTR_10 | payload_len);
}

// FIXME: why have dst_len if we don't use it?  Put in debug arg checking?
int
ipsec_esp_ipv4_tunnel_ibound_process_packet_start
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 void* src, int src_len,
 void* dst, int dst_len)
{
  gxcr_ipsec_sa_t* ipsec_sa = &op_sa->ipsec_sa;
  gxcr_token_info_t* token_info = &ipsec_sa->token_info;
  ipsec_esp_ipv4_tunnel_ibound_runtime_token_mod(token_info->token,
                                                 src_len,
                                                 src_len -
                                                 op_sa->nonpayload_len);

  // FIXME: store pre-configured opcode in op_sa
  gxio_mica_opcode_t opcode_oplen;
  opcode_oplen.size = src_len;
  opcode_oplen.extra_data_size = token_info->total_len_div_8;
  opcode_oplen.engine_type = MICA_CRYPTO_CTX_USER_OPCODE__ENGINE_TYPE_VAL_PP;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = src == dst ?
    MICA_OPCODE__DEST_MODE_VAL_OVERWRITE_SRC :
    MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dm_specific = token_info->token_len_div_4;
  opcode_oplen.dst_size = token_info->dst_size_code;

  // Make sure dst_len is large enough.
  int min_dst_len =
    ipsec_esp_ipv4_tunnel_ibound_minimum_dst_len(op_sa, src_len);

  if (__builtin_expect(dst_len < min_dst_len, 0))
    return GXCR_ERR_INVAL_MEMORY_SIZE;

  gxio_mica_start_op(mica_context, src, dst, ipsec_sa->metadata_mem,
                     opcode_oplen);

  return 0;
}

int
ipsec_esp_ipv4_tunnel_ibound_process_packet
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 void* src, int src_len,
 void* dst, int dst_len)
{
  int retval =
    ipsec_esp_ipv4_tunnel_ibound_process_packet_start(mica_context,
                                                      op_sa,
                                                      src, src_len,
                                                      dst, dst_len);

  if (__builtin_expect(retval, 0))
    return retval;

  gxcr_wait_for_completion(mica_context);

  return 0;
}

int
ipsec_esp_ipv4_tunnel_ibound_minimum_dst_len(
  ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
  int src_len)
{
  // For given src_len bytes input encrypted data, the length of
  // decrypted output depends on the padding length of the input.
  // This function is to return the potential maximum output length
  // for given src_len.
  //
  // src_len = dst_len + op_sa->nonpayload_len + pad_len + 2
  // pad_len = crypto_block_size - (dst_len + 2) % crypto_block_size
  //
  // The observation is: for given src_len, the corresponding dst_len has
  // a maximum (max_dst_len) in form:
  //          (crypto_block_size - 3) mod crypto_block_size.
  // and corresponding minimum pad_len = 1, when dst_len = max_dst_len,
  // FIXME! our obound implementation has the minimum pad = 1. But in
  // general it could be 0, to be safe, take 0. Therefore the
  // maximum output length:
  //      max_dst_len = src_len - op_sa->nonpayload_len - 2

  int max_dst_len = src_len - op_sa->nonpayload_len - 2;

  // Round up to 4 bytes aligned since Mica writes in 4-bytes.
  max_dst_len = (max_dst_len + 3) & ~3;

  return max_dst_len;
}

gxcr_token_info_t esp_ipv4_tunnel_ibound =
  {
    .token = (unsigned char*)esp_inbound_ipv4_tunnel,
    .token_len = sizeof (esp_inbound_ipv4_tunnel),
    .ccw_offset = sizeof (esp_inbound_ipv4_tunnel)
  };
