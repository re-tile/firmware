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
#include <stdio.h>
#include <string.h>
#include <gxcr/ipsec.h>
#include <gxio/mica.h>

#include "common.h"

// debug.  this is a general-purpose function that will be moved to 
// another file if it turns out to be useful.
void
gxcr_dump_token(char* str, void* token, int token_len)
{
  if (str) printf("\n%s\n", str);
  for (int i = 0; i < token_len; i++)
  {
    printf("0x%08x\n", *((uint32_t*)token + i));
  }
  printf("\n");
}

static int 
setup_context_control_words(gxcr_context_control_words_t* ccw,
                            gxcr_ipsec_params_t* params)
{
  /* 
    ciphers:
    aes-128: key_len = 4 words (16 bytes, 128 bits) 
    aes-192: key_len = 6 words (24 bytes, 192 bits) 
    aes-256: key_len = 8 words (32 bytes, 256 bits) 
    des:     key_len = 2 words (8 bytes,  64 bits) 
    3des:    key_len = 6 words (24 bytes, 192 bits) 

    digests:     
    hmac-md5:   digest 0 and digest 1 each are 4 words (16 bytes)
    hmac-sha-1: digest 0 and digest 1 each are 5 words (20 bytes)
    hmac-sha-256: digest 0 and digest 1 each are 8 words (32 bytes)
  */  

  memset(ccw, 0, sizeof (*ccw));

  // set up ipsec stuff based on params
  ccw->SPI = 1;
  ccw->IV_format = 0;            /* full iv format */
  ccw->pad_type = 4;             /* IPSec padding */

  // do common stuff assuming we're doing cipher, then turn it off if 
  // we're not
  int cipher_required = 1;
  ccw->key = 1;
  ccw->crypto_mode_plus_feedback = 1;

  // NOTE: In the explicit IV case: For outbound, the IV is taken either from
  // the context record or from the PRNG and placed in the packet.  For
  // inbound the IV is taken from the packet.
  // The EIP96 hardware manual says to include the IV in the context record
  // for outbound but not inbound.  However always including the IV in the
  // context record makes token setup easier, and internally it doesn't make
  // a difference.  There is a little more extra data DMAed, though.
  ccw->IV0 = 1;
  ccw->IV1 = 1;
  ccw->IV2 = 1;
  ccw->IV3 = 1;
  if (params->explicit_iv)
  {
    ccw->crypto_store = 1;
  }

  switch (params->cipher)
  {
  case GXCR_CIPHER_NONE:
    cipher_required = 0;
    ccw->key = 0;
    ccw->IV0 = 0;
    ccw->IV1 = 0;
    ccw->IV2 = 0;
    ccw->IV3 = 0;
    break;

  case GXCR_CIPHER_AES_CBC_128:
    ccw->crypto_algorithm = 5;
    break;
  case GXCR_CIPHER_AES_CBC_192:
    ccw->crypto_algorithm = 6;
    break;
  case GXCR_CIPHER_AES_CBC_256:
    ccw->crypto_algorithm = 7;
    break;
  case GXCR_CIPHER_DES_CBC:
    ccw->crypto_algorithm = 0;
    break;
  case GXCR_CIPHER_3DES_CBC:
    ccw->crypto_algorithm = 2;
    break;
  case GXCR_CIPHER_AES_GCM_128:
    ccw->crypto_algorithm = 5;
    ccw->crypto_mode_plus_feedback = 2;
    ccw->IV_format = 1;
    ccw->IV0 = 1;
    if (params->explicit_iv_from_prng)
    {
      ccw->IV1 = 0;
      ccw->IV2 = 0;
    }
    else
    {
      ccw->IV1 = 1;
      ccw->IV2 = 1;
    }
    ccw->IV3 = 0;
    break;
  case GXCR_CIPHER_AES_GCM_192:
    ccw->crypto_algorithm = 6;
    ccw->crypto_mode_plus_feedback = 2;
    ccw->IV_format = 1;
    ccw->IV0 = 1;
    if (params->explicit_iv_from_prng)
    {
      ccw->IV1 = 0;
      ccw->IV2 = 0;
    }
    else
    {
      ccw->IV1 = 1;
      ccw->IV2 = 1;
    }
    ccw->IV3 = 0;
    break;
  case GXCR_CIPHER_AES_GCM_256:
    ccw->crypto_algorithm = 7;
    ccw->crypto_mode_plus_feedback = 2;
    ccw->IV_format = 1;
    ccw->IV0 = 1;
    if (params->explicit_iv_from_prng)
    {
      ccw->IV1 = 0;
      ccw->IV2 = 0;
    }
    else
    {
      ccw->IV1 = 1;
      ccw->IV2 = 1;
    }
    ccw->IV3 = 0;
    break;
  default:
    return GXCR_OPERATION_NOT_SUPPORTED;
  }

  // assume digest, then turn off if we're not
  int digest_required = 1;
  ccw->digest_type = params->hmac_mode ? 3 : 1;

  // Hash algorithm.  See section B.2.1 of the EIP-96 HW Guide.
  switch (params->digest)
  {
  case GXCR_DIGEST_NONE:
    digest_required = 0;
    ccw->digest_type = 0;
    break;
  case GXCR_DIGEST_AES_GCM:
    ccw->encrypt_hash_result = 1;
    ccw->digest_type = 2;
    ccw->hash_algorithm = 4;
    break;
  case GXCR_DIGEST_MD5:
    ccw->hash_algorithm = 0;
    break;
  case GXCR_DIGEST_SHA1:
    ccw->hash_algorithm = 2;
    break;
  case GXCR_DIGEST_SHA_256:
    ccw->hash_algorithm = 3;
    break;
  case GXCR_DIGEST_AES_XCBC_MAC_96:
    ccw->hash_algorithm = 1;
    ccw->digest_type = 2;
    break;
  default:
    return GXCR_OPERATION_NOT_SUPPORTED;
  }

  // Type of Packet. See section B.2.1 of the EIP-96 HW Guide.
  if (params->inbound)
  {
    // FIXME: see B.2.6.4.  Says this can't be used with extended sequence
    // number estimation, but needed for inbound normal mode (in most cases).
    ccw->seq_nbr_store = 1;

    if (cipher_required & digest_required)
      ccw->ToP = 0xf;             /* inbound hash then decrypt */
    else if (cipher_required)
      ccw->ToP = 5;             /* inbound decrypt */
    else if (digest_required)
      ccw->ToP = 3;             /* inbound hash */
    else
      ccw->ToP = 1;             /* inbound null */
  }
  else
  {
    if (cipher_required & digest_required)
      ccw->ToP = 6;           /* outbound encrypt then hash */
    else if (cipher_required)
      ccw->ToP = 4;             /* outbound encrypt */
    else if (digest_required)
      ccw->ToP = 2;             /* outbound hash */
    else
      ccw->ToP = 0;             /* outbound null */
  }

  // Set up the sequence number context record field and control field.
  switch (params->seqnum_size)
  {
  case 32:
    ccw->SEQ = 1;
    break;
  case 48:
    ccw->SEQ = 2;
    break;
  case 64:
    ccw->SEQ = 3;
    break;
  default:
    return GXCR_BAD_PARAM;
    break;
  }

  // Set up the sequence number mask context record field and control field.
  switch (params->seqnum_replay_window_size)
  {
  case 32:
    ccw->MASK1 = 1;
    ccw->MASK0 = 0;
    break;
  case 64:
    ccw->MASK1 = 0;
    ccw->MASK0 = 1;
    break;
  case 128:
    ccw->MASK1 = 1;
    ccw->MASK0 = 1;
    break;
  default:
    return GXCR_BAD_PARAM;
    break;
  }

  return GXCR_NO_ERROR;
}

static int
setup_token_info(gxcr_token_info_t* token_info, gxcr_ipsec_params_t* params,
                 unsigned char* pmetadata)
{
  /* 
    ciphers:
    aes-128: key_len = 4 words (16 bytes, 128 bits) 
    aes-192: key_len = 6 words (24 bytes, 192 bits) 
    aes-256: key_len = 8 words (32 bytes, 256 bits) 
    des:     key_len = 2 words (8 bytes,  64 bits) 
    3des:    key_len = 6 words (24 bytes, 192 bits) 

    digests:     
    hmac-md5:   digest 0 and digest 1 each are 4 words (16 bytes)
    hmac-sha-1: digest 0 and digest 1 each are 5 words (20 bytes)
    hmac-sha-256: digest 0 and digest 1 each are 8 words (32 bytes)
  */  

  memset(token_info, 0, sizeof(*token_info));

  int token_len = params->token_template->token_len;

  if ((token_len > GXCR_MAX_TOKEN_LEN) || token_len == 0)
    return GXCR_ERR_INVAL_TOKEN_SIZE;
 
  gxcr_memcpy(pmetadata, params->token_template->token, token_len);
  token_info->token = pmetadata;
  token_info->token_len = token_len;
  token_info->token_len_div_4 = (token_len + 3) / 4;
  token_info->ccw_offset = params->token_template->ccw_offset;

  // The context record contains certain fields, depending on parameters
  // such as which cipher and which digest we choose.  The length of
  // these fields may vary depending on the algorithms chosen.
  // The first field starts after the 2 control words.
  int cr_offset = token_len + 8;
  // FIXME: make sure that the token_info portion of the ipsec_sa
  // is set to 0

  // do common stuff assuming we're doing cipher, then turn it off if 
  // we're not
  int cipher_required = 1;

  switch (params->cipher)
  {
  case GXCR_CIPHER_NONE:
    cipher_required = 0;
    break;
  case GXCR_CIPHER_AES_CBC_128:
    token_info->key_len = GXCR_AES_128_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_IV_SIZE;
    break;
  case GXCR_CIPHER_AES_CBC_192:
    token_info->key_len = GXCR_AES_192_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_IV_SIZE;
    break;
  case GXCR_CIPHER_AES_CBC_256:
    token_info->key_len = GXCR_AES_256_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_IV_SIZE;
    break;
  case GXCR_CIPHER_AES_GCM_128:
    token_info->key_len = GXCR_AES_128_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_GCM_IV_SIZE;
    break;
  case GXCR_CIPHER_AES_GCM_192:
    token_info->key_len = GXCR_AES_192_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_GCM_IV_SIZE;
    break;
  case GXCR_CIPHER_AES_GCM_256:
    token_info->key_len = GXCR_AES_256_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_AES_GCM_IV_SIZE;
    break;
  case GXCR_CIPHER_DES_CBC:
    token_info->key_len = GXCR_DES_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_DES_IV_SIZE;
    break;
  case GXCR_CIPHER_3DES_CBC:
    token_info->key_len = GXCR_3DES_KEY_SIZE;
    token_info->key_offset = cr_offset;
    token_info->iv_len = GXCR_DES_IV_SIZE;
    break;
  default:
    return GXCR_OPERATION_NOT_SUPPORTED;
  }
  cr_offset += token_info->key_len;

  switch (params->digest)
  {
  case GXCR_DIGEST_NONE:
    break;
  case GXCR_DIGEST_AES_GCM:
    token_info->digest0_len = GXCR_AES_IV_SIZE;
    token_info->digest0_offset = cr_offset;
    cr_offset += token_info->digest0_len;
    break;
  case GXCR_DIGEST_MD5:
    token_info->digest0_len = GXCR_MD5_DIGEST_SIZE;
    token_info->digest0_offset = cr_offset;
    cr_offset += token_info->digest0_len;
    if (params->hmac_mode)
    {
      token_info->digest1_len = GXCR_MD5_DIGEST_SIZE;
      token_info->digest1_offset = cr_offset;
      cr_offset += token_info->digest1_len;
    }
    break;
  case GXCR_DIGEST_SHA1:
    token_info->digest0_len = GXCR_SHA1_DIGEST_SIZE;
    token_info->digest0_offset = cr_offset;
    cr_offset += token_info->digest0_len;
    if (params->hmac_mode)
    {
      token_info->digest1_len = GXCR_SHA1_DIGEST_SIZE;
      token_info->digest1_offset = cr_offset;
      cr_offset += token_info->digest1_len;
    }
    break;
  case GXCR_DIGEST_SHA_256:
    token_info->digest0_len = GXCR_SHA2_256_DIGEST_SIZE;
    token_info->digest0_offset = cr_offset;
    cr_offset += token_info->digest0_len;
    if (params->hmac_mode)
    {
      token_info->digest1_len = GXCR_SHA2_256_DIGEST_SIZE;
      token_info->digest1_offset = cr_offset;
      cr_offset += token_info->digest1_len;
    }
    break;
  case GXCR_DIGEST_AES_XCBC_MAC_96:
    token_info->digest0_len = GXCR_AES_128_KEY_SIZE * 2;
    token_info->digest0_offset = cr_offset;
    cr_offset += token_info->digest0_len;
    token_info->digest1_len = GXCR_AES_128_KEY_SIZE;
    token_info->digest1_offset = cr_offset;
    cr_offset += token_info->digest1_len;
    break;
  default:
    return GXCR_OPERATION_NOT_SUPPORTED;
  }

  // SPI context record field
  token_info->spi_offset = cr_offset;
  token_info->spi_len = 4;
  cr_offset += token_info->spi_len;

  // Set up the sequence number context record field and control field.
  switch (params->seqnum_size)
  {
  case 32:
    token_info->seqnum_len = 4;
    break;
  case 48:
    // FIXME: who will mask this?  Are there init requirements?  See B.2.6
    token_info->seqnum_len = 8;
    break;
  case 64:
    token_info->seqnum_len = 8;
    break;
  default:
    return GXCR_BAD_PARAM;
    break;
  }
  token_info->seqnum_offset = cr_offset;
  cr_offset += token_info->seqnum_len;

  // Set up the sequence number mask context record field and control field.
  switch (params->seqnum_replay_window_size)
  {
  case 32:
    // FIXME: who will mask this? Are there init requirements?  See B.2.6
    token_info->seqnum_mask_len = 8;
    break;
  case 64:
    token_info->seqnum_mask_len = 8;
    break;
  case 128:
    token_info->seqnum_mask_len = 16;
    break;
  default:
    return GXCR_BAD_PARAM;
    break;
  }
  token_info->seqnum_mask_offset = cr_offset;
  cr_offset += token_info->seqnum_mask_len;

  // IV field
  if (cipher_required)
  {
    token_info->iv_offset = cr_offset;
    cr_offset += token_info->iv_len;
  }

  token_info->total_len_div_8 = (cr_offset + 7) / 8;

  return GXCR_NO_ERROR;
}

// Function to find out how much memory to allocate for a particular 
// ipsec_sa.
size_t
gxcr_ipsec_calc_sa_bytes(gxcr_ipsec_params_t* ipsec_params)
{
  gxcr_token_info_t token_info;
  unsigned char dummy_metadata[GXCR_MAX_EXTRA_DATA_SIZE];
  setup_token_info(&token_info, ipsec_params, dummy_metadata);
  // Need room for metadata, and 32-byte result token at the end.
  return ((token_info.total_len_div_8 << 3) + sizeof(gxcr_result_token_t));
}

// Function to init an ipsec_sa with cipher/digest algorithms.
extern int
gxcr_ipsec_init_sa(gxcr_ipsec_sa_t* ipsec_sa,
                   void* metadata_mem, int metadata_mem_len,
                   gxcr_ipsec_params_t* ipsec_params,
                   unsigned char* key, unsigned char* iv)
{
  if (ipsec_params->token_template == NULL)
    return GXCR_BAD_PARAM;

  if (metadata_mem_len < gxcr_ipsec_calc_sa_bytes(ipsec_params))
    return GXCR_ERR_INVAL_MEMORY_SIZE;

  if (ipsec_params->digest == GXCR_DIGEST_AES_XCBC_MAC_96 &&
      ipsec_params->hmac_mode)
    return GXCR_BAD_PARAM;

  memset(ipsec_sa, 0, sizeof(*ipsec_sa));
  ipsec_sa->explicit_iv_from_prng = ipsec_params->explicit_iv_from_prng;
  ipsec_sa->metadata_mem = metadata_mem;
  ipsec_sa->cipher = ipsec_params->cipher;
  unsigned char* pmetadata = (unsigned char*)metadata_mem;
  gxcr_memclr(pmetadata, metadata_mem_len);

  gxcr_token_info_t* token_info = &ipsec_sa->token_info;
  setup_token_info(token_info, ipsec_params, pmetadata);

  // Tokens are always multiples of 4 bytes, so we have no alignment concerns
  gxcr_context_control_words_t ccw = {{ 0 }};
  setup_context_control_words(&ccw, ipsec_params);

  gxcr_write_context_control_words(&ccw, pmetadata + token_info->token_len);  

  // Now set initial values by copying keys/iv/digests/spi into the cr,
  // if they were provided.  Double-check that we have places to put them.
  // pmetadata points to the start of the context record.

  if (key && token_info->key_len)
  {
    gxcr_memcpy(pmetadata + token_info->key_offset, key, token_info->key_len);
  }

  if (iv && token_info->iv_len)
  {
    gxcr_memcpy(pmetadata + token_info->iv_offset, iv, token_info->iv_len);
  }

  // If this fires there is a bug in setup_token_info()
  assert (token_info->spi_len == sizeof(uint32_t));

  if (token_info->spi_offset)
    *((uint32_t*)(pmetadata + token_info->spi_offset)) =
      cpu_to_le32(ipsec_params->spi);

  return GXCR_NO_ERROR;
}


size_t
gxcr_ipsec_precalc_calc_memory_size(gxcr_ipsec_params_t* ipsec_params,
                                    int key_len)
{
  if (ipsec_params->hmac_mode)
  {
    // For hmac, a short key is padded out to the block size for the digest.
    int block_size = (ipsec_params->digest > GXCR_DIGEST_SHA_256) ? 128 : 64;
    int data_mem_len = (key_len > block_size) ? key_len : block_size;

    return data_mem_len + hmac_precalc_calc_metadata_bytes(key_len);
  }
  else if (ipsec_params->digest == GXCR_DIGEST_AES_XCBC_MAC_96)
  {
    if (key_len != GXCR_AES_128_KEY_SIZE)
      return GXCR_BAD_PARAM;
    return (GXCR_AES_128_KEY_SIZE * 2) + 
      gxcr_xcbc_precalc_calc_metadata_bytes();
  }
  else if (ipsec_params->digest == GXCR_DIGEST_AES_GCM)
  {
    switch(ipsec_params->cipher)
    {
    case GXCR_CIPHER_AES_GCM_128:
      return (GXCR_AES_IV_SIZE * 2 +
              gxcr_calc_context_bytes(GXCR_CIPHER_AES_ECB_128,
                                      GXCR_DIGEST_NONE, 0, 0));
    case GXCR_CIPHER_AES_GCM_192:
      return (GXCR_AES_IV_SIZE * 2 +
              gxcr_calc_context_bytes(GXCR_CIPHER_AES_ECB_192,
                                      GXCR_DIGEST_NONE, 0, 0));
    case GXCR_CIPHER_AES_GCM_256:
      return (GXCR_AES_IV_SIZE * 2 +
              gxcr_calc_context_bytes(GXCR_CIPHER_AES_ECB_256,
                                      GXCR_DIGEST_NONE, 0, 0));
    default:
      return GXCR_BAD_PARAM;
    }
  }
  else
    return -1;
}


int
gxcr_ipsec_precalc(gxio_mica_context_t* mica_context,
                   gxcr_ipsec_sa_t* ipsec_sa,
                   void* scratch_mem, int scratch_mem_len,
                   void* digest_key, int digest_key_len)
{
  gxcr_context_control_words_t ccw_mem;
  gxcr_read_context_control_words(&ccw_mem, ipsec_sa->metadata_mem + 
                                  ipsec_sa->token_info.ccw_offset);
  
  if (ccw_mem.digest_type == 3)
  {
    return __gxcr_hmac_precalc(mica_context,
                               ipsec_sa->token_info,
                               ipsec_sa->metadata_mem, 
                               scratch_mem, scratch_mem_len,
                               digest_key, digest_key_len);
  }
  else if ((ccw_mem.digest_type == 2) && (ccw_mem.hash_algorithm == 1))
  {
    return __gxcr_xcbc_precalc(mica_context,
                               ipsec_sa->token_info,
                               ipsec_sa->metadata_mem, 
                               scratch_mem, scratch_mem_len,
                               digest_key, digest_key_len);
  }
  else if ((ccw_mem.digest_type == 2) && (ccw_mem.hash_algorithm == 4))
  {
    return __gxcr_ghash_precalc(mica_context,
                                ipsec_sa->token_info,
                                ipsec_sa->metadata_mem,
                                scratch_mem, scratch_mem_len,
                                digest_key, digest_key_len);
  }
  else
    return GXCR_BAD_PARAM;
}
