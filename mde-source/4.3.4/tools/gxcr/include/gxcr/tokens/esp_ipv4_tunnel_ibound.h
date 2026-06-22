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

#ifndef __GXCR_TOKENS_ESP_IPV4_TUNNEL_IBOUND_H__
#define __GXCR_TOKENS_ESP_IPV4_TUNNEL_IBOUND_H__

#include <stdint.h>
#include <gxio/mica.h>
#include "../gxcr.h"
#include "../ipsec.h"

/**
 * @addtogroup gxcr_ipsec_esp_ibound_
 * @{
 *
 * The ESP IPv4 Tunneling Inbound token decapsulates an IPv4 packet.
 * It strips off the outer IP header, validates the ESP header's SPI
 * and sequence number against a specified SA, decrypts the inner IPv4
 * packet, removes and validates any padding, and compares an ICV
 * calculated on the packet against the ICV at the end of the packet.
 *
 * Before packets are processed with this interface, basic data
 * structures need to be initialized at the gxcr_ipsec level.  See
 * @ref ipsec.h.  This is the level at which IPsec parameters such as
 * the encryption and authentication algorithms and keys are
 * specified.
 *
 * Certain parameters are determined at Security Association setup
 * time, others are not known until a particular packet is being
 * processed.  The function ipsec_esp_ipv4_tunnel_ibound_setup() sets
 * up the SA-specific data.  This function must be called once, at
 * initialization time, for each Security Association.
 *
 * The setup function leaves as little work as possible for the
 * runtime function, ipsec_esp_ipv4_tunnel_ibound_process_packet() or
 * ipsec_esp_ipv4_tunnel_ibound_process_packet_start(), to perform for
 * each packet.  The runtime function expects a packet in the same form
 * as that output by the ipsec_esp_ipv4_tunnel_obound token generates.
 * It validates the SPI and sequence number against the SA, updates
 * the replay window in the SA, and compares the hash performed on the
 * packet against the trailing ICV.  The result packet is an
 * unencrypted IPv4 packet with an updated header.
 *
 * See this example program for an illustration of how to use this API
 * (along with its outbound conterpart): @ref mica/ipsec/app.c : IPsec
 * ESP/IPv4/Tunneling Outbound and Inbound packet processing.
 */

/** Token for ESP/IPv4/Tunneling Inbound packet processing. */
extern gxcr_token_info_t esp_ipv4_tunnel_ibound;

/** Data structure that encapsulates general IPsec SA and
 * operation-specific parameters.  The operation-specific parameters
 * are initialized at setup time and used at runtime.
 */
typedef struct 
{
  /** General IPsec Security Association */
  gxcr_ipsec_sa_t ipsec_sa; 

  /** Length, in bytes, of the ESP header in packets handled by this
   * operation.
   */
  int nonpayload_len;

  /** The maximum amount by which a result packet is larger, for this
   * specific combination of token operation, algorithm, and parameters.
   */
  int additional_packet_len;
} ipsec_esp_ipv4_tunnel_ibound_sa_t;

/** Function to set up for inbound IPsec ESP IPv4 tunnel mode processing.
 * @param op_sa - An IPsec SA initialized via gxcr_ipsec_init_sa().
 * @param outer_ip_hdr_len - Length, in bytes, of the IP tunneling header
 *                 expected on inbound packet.
 * @param esp_hdr_len - Length, in bytes, of the ESP header expected in
 *                 inbound packets.
 */
extern int
ipsec_esp_ipv4_tunnel_ibound_setup
(ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 int outer_ip_hdr_len,
 int esp_hdr_len);


/** Function to perform inbound IPsec ESP IPv4 tunnel mode processing
 * on a packet.  This function returns immediately.
 * @param mica_context - An initialized MiCA context.
 * @param op_sa - An initialized operation-specific SA.
 * @param packet - The packet to be processed.
 * @param packet_len - Length, in bytes, of the packet to be processed.
 * @param dst - Destination memory for the result packet.  If dst is equal to
 *   packet, the memory pointed to by packet is overwritten.  The memory
 *   pointed to by packet and by dst must not overlap otherwise.
 * @param dst_len - Length, in bytes, of destination memory for the result
 *   packet.
 * @returns 0 on success, error code on failure.
 */
extern int
ipsec_esp_ipv4_tunnel_ibound_process_packet_start
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 void* packet, int packet_len,
 void* dst, int dst_len);

/** Function to perform inbound IPsec ESP IPv4 tunnel mode processing
 * on a packet.  This function blocks until completion.
 * @param mica_context - An initialized MiCA context.
 * @param op_sa - An initialized operation-specific SA.
 * @param packet - The packet to be processed.  If dst is equal to
 *   packet, the memory pointed to by packet is overwritten.  The memory
 *   pointed to by packet and by dst must not overlap otherwise.
 * @param packet_len - Length, in bytes, of the packet to be processed.
 * @param dst - Destination memory for the result packet.
 * @param dst_len - Length, in bytes, of destination memory for the result
 *   packet.
 * @returns 0 on success, error code on failure.
 */
extern int
ipsec_esp_ipv4_tunnel_ibound_process_packet
(gxio_mica_context_t* mica_context,
 ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
 void* packet, int packet_len,
 void* dst, int dst_len);

/** Function to calculate the minimum length of the output
 * (destination) buffer for functions:
 * ipsec_esp_ipv4_tunnel_ibound_process_packet_start or
 * ipsec_esp_ipv4_tunnel_ibound_process_packet. Calls to those
 * functions supplying an output buffer whose size, as given
 * by the dst_len parameter, is smaller than the value returned
 * by this function will fail.
 * @param op_sa An initialized operation-specific SA.
 * @param src_len Length, in bytes, of the packet to be processed.
 * @return The minimum output data length in bytes.
 */
extern int
ipsec_esp_ipv4_tunnel_ibound_minimum_dst_len(
  ipsec_esp_ipv4_tunnel_ibound_sa_t* op_sa,
  int src_len);

/** @} */

#endif // __GXCR_TOKENS_ESP_IPV4_TUNNEL_IBOUND_H__
