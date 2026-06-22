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

#ifndef __GXCR_IPSEC_H__
#define __GXCR_IPSEC_H__

#include <stdbool.h>
#include <stdint.h>
#include <gxio/mica.h>

#include "gxcr.h"

/**
 * @file
 *
 * An API for hardware-accelerated IPsec packet processing.
 *
 * @addtogroup gxcr_ipsec_
 * @{
 *
 * The gxcr_ipsec_ API, declared in gxcr/ipsec.h, provides a basic
 * framework for IPsec processing with the MiCA Packet Processing
 * engine.  It allows the user to set up a Security Association data
 * structure based on the parameters in gxcr_ipsec_params_t.  However,
 * the actual packet processing requires the programming of a custom
 * "token" for the engine.  The token is a series of instructions,
 * some of which have fields that vary on a per-SA basis, some of
 * which vary on a per-packet basis.  Specific examples of this custom
 * layer are in tokens/esp_ipv4_tunnel_ibound.h and
 * tokens/esp_ipv4_tunnel_obound.h.  See this example program for more
 * information about how to use the gxcr_ipsec_ API:
 *
 * - @ref mica/ipsec/app.c : IPsec ESP/IPv4/Tunneling Outbound and Inbound
 * packet processing.
 *
 * The Packet Processing engine is a very flexible, programmable
 * engine which is capable of many types of IPsec, SSL, and other
 * packet processing.  Please read the IO Device Guide (UG404)
 * Appendix D for more information on the programming of the Packet
 * Processing engine.
 */

/** Parameters used to set up an IPsec SA.  These determine the 
 * exact operations to be performed on the packet, and the format
 * of the internal context record.  Once established by these
 * parameters, the format of context record cannot change.
 */
typedef struct
    {
      /** Points to template for token. */
      gxcr_token_info_t const * token_template;
      /** The Security Parameter Index. */
      int spi;
      /** size of sequence number, in bits.  Must be 32, 48 or 64. */
      int seqnum_size;
      /** Size of the sequence number anti-replay window.  Must be
       * 32, 64, or 128. */
      int seqnum_replay_window_size;
      /** Cipher algorithm for encryption, value implies key length. */
      gxcr_cipher_t cipher;
      /** Digest algorithm for ICV. */
      gxcr_digest_t digest;
      /** True if there will be an explicit IV included in every packet,
       * and not in the context record. */
      bool explicit_iv;
      /** True if the explicit IV for outbound packets is to be generated
       * automatically from the engine's pseudo-random number generator.
       * If false the IV must be provided by the user in the context record.
       */
      bool explicit_iv_from_prng;
      /** True if this is for inbound processing, false for outbound. */
      bool inbound;
      /** Digest is HMAC mode */
      bool hmac_mode;
} gxcr_ipsec_params_t;

/** Data structure that encapsulates a pointer to the memory that is to be
 * used with shim for the metadata, and the information about where the
 * token and the context record fields are within that memory.  This is
 * not for direct use by the user.
 */
typedef struct
{
  /** Pointer to the gxio-registered I/O memory in which this data resides */
  unsigned char* metadata_mem;

  /** Internal information needed by this SA. */
  gxcr_token_info_t token_info;

  /** True if the explicit IV for outbound packets is to be generated
   * automatically from the engine's pseudo-random number generator.
   * If false the IV must be provided by the user in the context record.
   */
  bool explicit_iv_from_prng;

  /** Cipher algorithm */
  gxcr_cipher_t cipher;

} gxcr_ipsec_sa_t;

/** Returns a pointer to the cipher key data for a gxcr_ipsec_sa_t,
 * NULL if this gxcr_ipsec_sa_t doesn't do a cipher.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_key(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem + 
    ipsec_sa->token_info.key_offset;
}

/** Returns the cipher key length for a gxcr_ipsec_sa_t, 0 if this
 * gxcr_ipsec_sa_t doesn't do a cipher.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_key_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.key_len;
}

/** Returns a pointer to the initialization vector data for a gxcr_ipsec_sa_t,
 * NULL if this gxcr_ipsec_sa_t doesn't do a cipher.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_iv(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.iv_offset;
}

/** Returns the length of the initialization vector for a gxcr_ipsec_sa_t,
 * 0 if this gxcr_ipsec_sa_t doesn't do a cipher.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_iv_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.iv_len;
}

/**  Returns a pointer to the calculated digest data for a gxcr_ipsec_sa_t,
 * NULL if this gxcr_ipsec_sa_t doesn't do a hash.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_digest0(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.digest0_offset;
}

/**  Returns the length of the digest for a gxcr_ipsec_sa_t, 0 if this
 * gxcr_ipsec_sa_t doesn't do a hash.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_digest0_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.digest0_len;
}

/**  Returns a pointer to the calculated digest data for a gxcr_ipsec_sa_t,
 * NULL if this gxcr_ipsec_sa_t doesn't do a hash.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_digest1(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem + 
    ipsec_sa->token_info.digest1_offset;
}

/**  Returns the length of the digest for a gxcr_ipsec_sa_t, 0 if this
 * gxcr_ipsec_sa_t doesn't do a hash.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_digest1_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.digest1_len;
}

/**  Returns a pointer to the SPI field for a gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_spi(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.spi_offset;
}

/**  Returns the length of the SPI field for a gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_spi_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.spi_len;
}

/**  Returns a pointer to the sequence number field for a gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_seqnum(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.seqnum_offset;
}

/**  Returns the length of the sequence number field for a gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_seqnum_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.seqnum_len;
}

/**  Returns a pointer to the sequence number mask field for a
 * gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline void*
gxcr_ipsec_sa_seqnum_mask(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.seqnum_mask_offset;
}

/**  Returns the length of the sequence number mask field for a
 * gxcr_ipsec_sa_t.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline int
gxcr_ipsec_sa_seqnum_mask_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.seqnum_mask_len;
}

#ifndef __DOXYGEN_API_REF__
static __inline void*
gxcr_ipsec_sa_arc4_sp(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.arc4_sp_offset;
}

static __inline int
gxcr_ipsec_sa_arc4_sp_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.arc4_sp_len;
}
static __inline void*
gxcr_ipsec_sa_ijp(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->metadata_mem +
    ipsec_sa->token_info.ijp_offset;
}

static __inline int
gxcr_ipsec_sa_ijp_len(gxcr_ipsec_sa_t* ipsec_sa)
{
  return ipsec_sa->token_info.ijp_len;
}
#endif

/** Returns a pointer to the result token associated with this IPsec SA.
 * @param ipsec_sa Pointer to the IPsec SA.
 */
static __inline gxcr_result_token_t*
gxcr_ipsec_result(gxcr_ipsec_sa_t* ipsec_sa)
{
  return (gxcr_result_token_t*)(ipsec_sa->metadata_mem +
                                (ipsec_sa->token_info.total_len_div_8 << 3));
}

/** Function to find out how much memory to allocate for a particular 
 * ipsec_sa.
 * @param ipsec_params Parameters for the definition of the IPsec SA.
 */
extern size_t 
gxcr_ipsec_calc_sa_bytes(gxcr_ipsec_params_t* ipsec_params);


/** Function to initialize an ipsec_sa with cipher/digest algorithms.
 * This sets up the metadata memory with the token and the context record,
 * and sets up internal data structures with information on the sizes and 
 * offsets of fields in the context record so they can be accessed via
 * accessor functions.
 * @param ipsec_sa Pointer to the context that will be initialized.
 * @param metadata_mem Points to memory for metadata to be used with shim.
 *             This memory must be registered with the shim.
 * @param metadata_mem_len Length, in bytes, of metadata memory.
 * @param ipsec_params Specifies cipher, digest, and other params pertaining
 *             to IPsec operation to perform on packet.
 * @param key  Pointer to memory containing initial value for the key.  Length
 *             is determined by the algorithms selected in the params.
 * @param iv   Pointer to memory containing initial value for IV.  Length is
 *             determined by the algorithms selected in the params.
 */
extern int
gxcr_ipsec_init_sa(gxcr_ipsec_sa_t* ipsec_sa,
                   void* metadata_mem, int metadata_mem_len,
                   gxcr_ipsec_params_t* ipsec_params,
                   unsigned char* key, unsigned char* iv);


/** Function to find out how much MiCA-registered memory to allocate for use by
* gxcr_ipsec_precalc() for the purpose of performing the precalculation step
* needed when using HMAC or XCBC digests.
* @param ipsec_params Parameters for the definition of the IPsec SA.
* @param key_len Length of the digest key for this algorithm.
*/
extern size_t
gxcr_ipsec_precalc_calc_memory_size(gxcr_ipsec_params_t* ipsec_params,
                                    int key_len);


/** This function uses the MiCA accelerator to perform further initialization
* of the gxcr_ipsec_sa_t data structure, when either an HMAC or XCBC digest
* is used.  This function must be run before the gxcr_ipsec_sa_t is used
* for a packet operation if one of these digest types is used.  It must not
* be run if a regular digest will not be used.
* @param mica_context An initalized MiCA context.
* @param ipsec_sa Pointer to an SA that has been initialized via
*   gxcr_ipsec_init_sa().
* @param scratch_mem Pointer to scratch memory to be used by the shim.
*   Memory must be 32-bit aligned and registed with the MiCA shim via
*   gxio_mica_register_page().  The function
*   gxcr_ipsec_precalc_calc_memory_size() tells how much MiCA-registered memory
*   must be provided to this function.  The memory may be freed after this
*   function returns.
* @param scratch_mem_len Length of provided precalc metadata memory, in bytes.


* @param digest_key Pointer to the digest key.
* @param digest_key_len The length, in bytes, of the digest key.
*/
extern int
gxcr_ipsec_precalc(gxio_mica_context_t* mica_context,
                   gxcr_ipsec_sa_t* ipsec_sa,
                   void* scratch_mem, int scratch_mem_len,
                   void* digest_key, int digest_key_len);
/** @} */

#endif // __GXCR_IPSEC_H__
