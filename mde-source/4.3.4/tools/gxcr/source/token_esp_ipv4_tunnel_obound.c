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

#include <gxcr/ipsec.h>
#include <gxcr/tokens/esp_ipv4_tunnel_obound.h>

#include "common.h"

// blank len fields and 0 header
#define OUTBND_INSTR_00 0x00000000 // header (add packet len)
#define OUTBND_INSTR_01 0x00000000 // input packet pointer
#define OUTBND_INSTR_02 0x00000000 // output pointer
#define OUTBND_INSTR_03 0x00000000 // context pointer
#define OUTBND_INSTR_04 0x21d80000 // insert outer ip header (add outer ip header length)
#define OUTBND_INSTR_05 0x00000000 // outer ip header
#define OUTBND_INSTR_06 0x00000000 // outer ip header
#define OUTBND_INSTR_07 0x00000000 // outer ip header
#define OUTBND_INSTR_08 0x00000000 // outer ip header
#define OUTBND_INSTR_09 0x00000000 // outer ip header
#define OUTBND_INSTR_10 0x23900000 // insert ESP header (SPI and seq num)(add 8 for esp hdr len)
#define OUTBND_INSTR_11 0x23a00000 // insert IV into packet (from CR)
#define OUTBND_INSTR_GCM_11        0x21a80000 // insert IV into packet (from CR)
#define OUTBND_INSTR_GCM_RESULT_12 0xa0800000 // Length = 16
#define OUTBND_INSTR_GCM_INSERT_13 0x25000010 // Insert 1 zero block
#define OUTBND_INSTR_14 0x07000000 // hash/crypto all but icv (add pkt len)
#define OUTBND_INSTR_15 0x2f220800 // insert ipsec padding (add pad len)
#define OUTBND_INSTR_16 0x21e6000c // insert icv
#define OUTBND_INSTR_17 0xe12e1800 // update seq num in ctx record
#define OUTBND_INSTR_NOP 0x20000004 // NOP

// Note that with this method, the length of the Outer IP header must be fixed
// at compile time.  FIXME: if we do it this way, put the length right in the
// instruction definition and remove the step where it is updated.
static uint32_t esp_outbound_ipv4_tunnel[] =
  {
    OUTBND_INSTR_00,
    OUTBND_INSTR_01,
    OUTBND_INSTR_02,
    OUTBND_INSTR_03,
    OUTBND_INSTR_04,
    OUTBND_INSTR_05,
    OUTBND_INSTR_06,
    OUTBND_INSTR_07,
    OUTBND_INSTR_08,
    OUTBND_INSTR_09,
    OUTBND_INSTR_10,
    OUTBND_INSTR_11,
    OUTBND_INSTR_NOP,
    OUTBND_INSTR_NOP,
    OUTBND_INSTR_14,
    OUTBND_INSTR_15,
    OUTBND_INSTR_16,
    OUTBND_INSTR_17
  };


#define OUTBND_OUTER_IP_HDR_LEN 20
#define NULL_ENCRYPTION_PAD_BLOCK_SIZE 8

#define OUTBND_PACKET_LEN_OFFSET          0
#define OUTBND_OUTER_IP_HDR_LEN_OFFSET    4
#define OUTBND_ESP_HDR_LEN_OFFSET        10

#define OUTBND_IV_LEN_OFFSET             11
#define OUTBND_INSTR_GCM_RESULT_OFFSET   12
#define OUTBND_INSTR_GCM_INSERT_OFFSET   13

#define OUTBND_BEGIN_HASH_OFFSET         14
#define OUTBND_PAD_LEN_OFFSET            15
#define OUTBND_CTX_SEQNUM_OFFSET         17


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

// These functions are specific to the token, and therefore already
// know the length
int
ipsec_esp_ipv4_tunnel_obound_setup
(ipsec_esp_ipv4_tunnel_obound_sa_t* op_sa,
 void* outer_ip_hdr,
 int outer_ip_hdr_len,
 int esp_hdr_len)
{
  // Save off information that will be needed at runtime.
  op_sa->esp_hdr_len = esp_hdr_len;

  // FIXME: validate args, especially outer_ip_hdr_len
  // Need to return error code

  gxcr_token_info_t* token_info = &op_sa->ipsec_sa.token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;

  assert(outer_ip_hdr_len == OUTBND_OUTER_IP_HDR_LEN);

  // Copy token into metadata memory
  for (int i = 0;
       i < sizeof(esp_outbound_ipv4_tunnel) / sizeof(*esp_outbound_ipv4_tunnel);
       i++)
    *(tkn + i) = cpu_to_le32(esp_outbound_ipv4_tunnel[i]);

  // Copy the outer IP header into the token
  for (int i = 0; i < OUTBND_OUTER_IP_HDR_LEN / 4; i++)
    *(tkn + OUTBND_OUTER_IP_HDR_LEN_OFFSET + 1 + i) =
      cpu_to_le32(*((uint32_t*)outer_ip_hdr + i));

  *(tkn + OUTBND_OUTER_IP_HDR_LEN_OFFSET) =
    cpu_to_le32(OUTBND_INSTR_04 | OUTBND_OUTER_IP_HDR_LEN);

  if (op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_128 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_192 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_256)
  {
    /* Add "L" bit in INSTR_10 */
    *(tkn + OUTBND_ESP_HDR_LEN_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_10 | 0x08000000 | esp_hdr_len);

    /* fill-in two nop tokens */
    *(tkn + OUTBND_INSTR_GCM_RESULT_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_GCM_RESULT_12 | (20 + esp_hdr_len + 8));

    /* Insert zero block */
    *(tkn + OUTBND_INSTR_GCM_INSERT_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_GCM_INSERT_13);

    /* Insert IV from CR */
    *(tkn + OUTBND_IV_LEN_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_GCM_11 | 8);
  }
  else
  {
    *(tkn + OUTBND_ESP_HDR_LEN_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_10 | esp_hdr_len);

    *(tkn + OUTBND_IV_LEN_OFFSET) =
      cpu_to_le32(OUTBND_INSTR_11 | token_info->iv_len);
  }

  *(tkn + OUTBND_CTX_SEQNUM_OFFSET) =
    cpu_to_le32(OUTBND_INSTR_17 | ((token_info->seqnum_offset -
                                    token_info->token_len) / 4));

  // This is the maximum amount by which a result packet is larger, for
  // this specific combination of token operation, algorithm, and parameters.
  op_sa->additional_packet_len = outer_ip_hdr_len + esp_hdr_len +
    token_info->iv_len /* IV len */ +
    2 /* ESP trailer */ +
    12 /* IPsec ICV length */ +
    token_info->key_len - 1 /* crypto padding */;

  // This token requires that the destination memory contain room for the extra
  // data added to the packet: the outer IP header, the ESP header, Explicit
  // IV, IPSec padding, and the ESP trailer (pad length, next header, ICV).
  unsigned short exp = round_up_to_power_of_2(op_sa->additional_packet_len);
  if (exp)
    token_info->dst_size_code =__insn_ctz(exp) + 1;
  else
    token_info->dst_size_code = 0;

  return 0;
}

int
ipsec_esp_ipv4_tunnel_obound_process_packet_start
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_obound_sa_t* op_sa,
 void* src, int src_len,
 void* dst, int dst_len)
{
  // Make the necessary per-packet modifications to the token.

  gxcr_ipsec_sa_t* ipsec_sa = &op_sa->ipsec_sa;
  gxcr_token_info_t* token_info = &ipsec_sa->token_info;
  uint32_t* tkn = (uint32_t*)token_info->token;

  // See section 8.2.1.1 of the EIP-96 documentation.
  *(tkn + OUTBND_PACKET_LEN_OFFSET) =
    cpu_to_le32(OUTBND_INSTR_00 |
                (op_sa->ipsec_sa.explicit_iv_from_prng ? 0x04000000 : 0) |
                src_len);

  *(tkn + OUTBND_BEGIN_HASH_OFFSET) = cpu_to_le32(OUTBND_INSTR_14 | src_len);

  // Calculate padding len.  The part of the packet that passes through the
  // crypto block (packet + padding + pad len + NEXT_HEADER field)
  // must be modulo the crypto blocks size in bytes. Don't subtract outer ip
  // header or esp header, since those aren't included in the original packet
  // length.  The value for this field includes padding length and the size
  // of the pad len and NEXT_HEADER fields, so add 2 to the result.
  // For NULL encryption, we also add padding (optional, according to the
  // relevant RFCs) per the user for whom this token was written.

  int crypto_block_size = token_info->iv_len ? token_info->iv_len :
    NULL_ENCRYPTION_PAD_BLOCK_SIZE;

  // Force cyrpto_block_size as NULL_ENCRYPTION_PAD_BLOCK_SIZE for AES_GCM
  if (op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_128 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_192 ||
      op_sa->ipsec_sa.cipher == GXCR_CIPHER_AES_GCM_256)
    crypto_block_size = NULL_ENCRYPTION_PAD_BLOCK_SIZE;

  // crypto_block_size is power of 2, use '&' to replace '%' in following
  // line to eliminate function call of __modsi3.
  // pad_len = (crypto_block_size - ((src_len + 2) % crypto_block_size)) + 2
  int pad_len = (crypto_block_size -
                 ((src_len + 2) & (crypto_block_size - 1))) + 2;

  *(tkn + OUTBND_PAD_LEN_OFFSET) = cpu_to_le32(OUTBND_INSTR_15 | pad_len);

  // Put together the opcode.
  // FIXME: probably store a pre-configured opcode and just OR in the
  // len.  Still would need to validate dst_len (or warn that it's not
  // validated), and do something about single vs. chained buffers.
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
  int min_len =
    ipsec_esp_ipv4_tunnel_obound_minimum_dst_len(op_sa, src_len);

  if (__builtin_expect(dst_len < min_len, 0))
    return GXCR_ERR_INVAL_MEMORY_SIZE;

  gxio_mica_start_op(mica_context, src, dst, ipsec_sa->metadata_mem,
                     opcode_oplen);

  return 0;
}

int
ipsec_esp_ipv4_tunnel_obound_process_packet
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_obound_sa_t* op_sa,
 void* src, int src_len,
 void* dst, int dst_len)
{
  int retval =
    ipsec_esp_ipv4_tunnel_obound_process_packet_start(mica_context,
                                                      op_sa,
                                                      src, src_len,
                                                      dst, dst_len);

  if (__builtin_expect(retval, 0))
    return retval;

  gxcr_wait_for_completion(mica_context);

  return 0;
}

int
ipsec_esp_ipv4_tunnel_obound_minimum_dst_len(
  ipsec_esp_ipv4_tunnel_obound_sa_t* op_sa,
  int src_len)
{
  gxcr_token_info_t* token_info = &op_sa->ipsec_sa.token_info;
  int crypto_block_size =
    token_info->iv_len ? token_info->iv_len : NULL_ENCRYPTION_PAD_BLOCK_SIZE;

  // pad_len = (crypto_block_size - ((src_len + 2) % crypto_block_size)) + 2
  // crypto_block_size is power of 2.
  int pad_len = (crypto_block_size -
                 ((src_len + 2) & (crypto_block_size - 1))) + 2;

  return (OUTBND_OUTER_IP_HDR_LEN + op_sa->esp_hdr_len + src_len +
          pad_len + token_info->iv_len + 12);
}

gxcr_token_info_t esp_ipv4_tunnel_obound =
  {
    .token = (unsigned char*)esp_outbound_ipv4_tunnel,
    .token_len = sizeof (esp_outbound_ipv4_tunnel),
    .ccw_offset = sizeof (esp_outbound_ipv4_tunnel)
  };
