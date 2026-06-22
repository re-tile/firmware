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

#ifndef __GXCR_TOKENS_HMAC_PRECALC_H__
#define __GXCR_TOKENS_HMAC_PRECALC_H__

#include <stdint.h>
#include "../gxcr.h"

#include <gxio/mica.h>

/**
 * @addtogroup gxcr_hmac_precalc_
 * @{
 *
 * The HMAC precalculation token is used to hardware-accelerate a necessary
 * initialization step on a context record before it can be used by the
 * Packet Processing engine to calculate a digest in HMAC mode.  When
 * calculating an HMAC digest, the hardware expects that the HMAC key has 
 * been transformed into an inner and an outer digest and placed in the
 * appropriate fields of the context record. This token is used internallly by
 * the gxcr functions to generate those values, but it can also be used 
 * by a custom function to perform this initialization step.  Many higher-level
 * protocols, such as IPsec, can use HMAC mode for digest calculations.
 */

/** Function to set up the token and hardware context record for calculating
 * HMAC inner and outer digests.
 * @param metadata_mem Pointer to memory that has been registered with the
 *  MiCA shim via gxio_mica_register_page().
 * @param metadata_mem_len Length, in bytes, of provided metadata memory.
 * @param hash_algorithm The digest algorithm to use.  This value is obtained
 *   from the hash_algorithm field of the gxcr_context_control_words_t of the
 *   initialized gxcr_context_t.
 * @param finish_digest if 1, run a complete digest (useful for reducing a long
 *   key).  if 0, leave digest incomplete (used when computing an inner or outer
 *   digest from a key).
 * @param digest_size Size, in bytes, of digest produced by this algorithm.
 * @param data_size Size, in bytes, of the data to be processed in this step.
 *   This will be the HMAC key length if this is a key-reduction step, or the
 *   digest block size if this is an inner or outer precalculation.
 */
extern int
hmac_precalc_setup(void* metadata_mem, int metadata_mem_len,
                   int hash_algorithm, int finish_digest, int digest_size,
                   int data_size);

/** Function to perform the precalculation step.  This function schedules the
 * operation with the MiCA shim and returns immediately. The function
 * gxio_mica_is_busy() may be called to poll for completion.
 * @param mica_context An initalized MiCA context.
 * @param metadata_mem Pointer to memory that has been registered with the
 *  MiCA shim via gxio_mica_register_page(), and has been initialized by the
 *  function hmac_precalc_setup.
 * @param metadata_mem_len Length, in bytes, of provided metadata memory.
 * @param data_mem Pointer to the data to be processed.  The memory must be
 *   registered with the MiCA shim via gxio_mica_register_page().
 * @param data_mem_len Length, in bytes, of the data to be processed.
 *  The returned data, written to data_mem, will be a multiple of 4 bytes,
 *  so the actual memory allocated may need to be up to 3 bytes larger than
 *  the processed amount.
 */
extern void
hmac_precalc_process_start(gxio_mica_context_t* mica_context,
                           void* metadata_mem, int metadata_mem_len,
                           void* data_mem, int data_mem_len);

/**
 * @param mica_context An initalized MiCA context.
 * @param metadata_mem Pointer to memory that has been registered with the
 *  MiCA shim via gxio_mica_register_page(), and has been initialized by the
 *  function hmac_precalc_setup.
 * @param metadata_mem_len Length, in bytes, of provided metadata memory.
 * @param data_mem Pointer to the data to be processed.  The memory must be
 *   registered with the MiCA shim via gxio_mica_register_page().
 * @param data_mem_len Length, in bytes, of the data to be processed.
 *  The returned data, written to data_mem, will be a multiple of 4 bytes,
 *  so the actual memory allocated may need to be up to 3 bytes larger than
 *  the processed amount.
 */
extern void
hmac_precalc_process(gxio_mica_context_t* mica_context,
                     void* metadata_mem, int metadata_mem_len,
                     void* data_mem, int data_mem_len);

/**
 * @param metadata_mem Pointer to metadata memory that has been processed
 *   by hmac_precalc_process() or hmac_precalc_process_start().  Returns
 *   a pointer to the calculated digest value.
 */
extern void*
hmac_precalc_digest(void* metadata_mem);

/** @} */

#endif // __GXCR_TOKENS_HMAC_PRECALC_H__
