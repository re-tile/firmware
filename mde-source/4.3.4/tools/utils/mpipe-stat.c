/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

#include <sys/errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <gxio/mpipe.h>
#include <gxio/common.h>
#include <arch/mpipe_gbe_def.h>
#include <arch/mpipe_xaui_def.h>

/******************************************************************************
 *
 * STATIC LOCAL VARIABLES
 *
 *****************************************************************************/

#define MAX_MPIPE_CONTEXT GXIO_MPIPE_INSTANCE_MAX
/* gxio mpipe contexts body. */
static gxio_mpipe_context_t  mpipe_context[MAX_MPIPE_CONTEXT];

/* flag to indicate if mpipe_context was initialized. */
static bool  mpipe_context_init[MAX_MPIPE_CONTEXT];

#define MAX_MPIPE_LINK     48
/* gxio mpipe links. */
static gxio_mpipe_link_t     mpipe_link[MAX_MPIPE_LINK];

/* gxio mpipe link ptr array */
static gxio_mpipe_link_t    *mpipe_link_ptr[MAX_MPIPE_LINK];

/* mpipe link stat */
static int64_t              mpipe_link_stat[MAX_MPIPE_LINK];

/* gxio mpipe context pointer array.*/
static gxio_mpipe_context_t *mpipe_context_ptr[MAX_MPIPE_LINK];

/* link count. */
static int   link_count;

/* link names. */
static char  link_name[MAX_MPIPE_LINK][GXIO_MPIPE_LINK_NAME_LEN + 1];

#define MAX_NUM_COUNTER  32
/* Mpipe counter index. */
static int   counter_idx[MAX_NUM_COUNTER];

/* Latest mpipe counters values. */
static uint64_t mpipe_counter[MAX_NUM_COUNTER];

/* Total mpipe counters if --total option is enabled. */
static uint64_t mpipe_counter_save[MAX_NUM_COUNTER];

/* Number of mpipe counters. */
static int   num_counter;

/* Display link stat and mac registers. */
static bool  verbose;

/* List all available links. */
static bool  list;

/* Interval in second. */
static int   interval;

/* Config commands */
static uint32_t config_val[3];
/* Config command index in array config_command[] */
static uint32_t config_val_idx[3];

/* Flags for counter config. and packet drop scan. */
static bool     config_f, scan_f;

/* Indicate display count per second. */
static bool  rate;

/* Display in total. */
static bool  total;

/* Include packet preamble in byte count. */
static bool preamble;

/* Display packet drop count if true. */
static bool drop;

/* Print numbers in human readable format. */
static bool human = false;

/* Display mPIPE buffer stack status. */
static bool buffer_stack_manager = false;


/* mpipe_state from link management. */
static gxio_mpipe_stats_t mpipe_stats[MAX_MPIPE_CONTEXT];
static uint64_t packet_drop_start[MAX_MPIPE_CONTEXT];

#define GBE_MAC(_IDX_)   (((_IDX_) >= 0) && ((_IDX_) < E_MPIPE_GBE_END))
#define XGBE_MAC(_IDX_)  (((_IDX_) >= E_MPIPE_XAUI_BEGIN)  &&   \
                          ((_IDX_) < E_MPIPE_XAUI_END))

/* enum mac registers need to read. */
enum _e_mac_reg {

  /* GBE */
  E_MPIPE_GBE_BEGIN        = 0,
  E_MPIPE_GBE_TX_BEGIN     = E_MPIPE_GBE_BEGIN,
  E_MPIPE_GBE_OCTETS_TX_LO = E_MPIPE_GBE_TX_BEGIN,
  E_MPIPE_GBE_OCTETS_TX_HI,
  E_MPIPE_GBE_FRAMES_TX,
  E_MPIPE_GBE_BCST_FRAMES_TX,
  E_MPIPE_GBE_MCST_FRAMES_TX,
  E_MPIPE_GBE_PAUSE_FRAMES_TX,
  E_MPIPE_GBE_64_BYTE_FRAMES_TX,
  E_MPIPE_GBE_65_TO_127_BYTE_FRAMES_TX,
  E_MPIPE_GBE_128_TO_255_BYTE_FRAMES_TX,
  E_MPIPE_GBE_256_TO_511_BYTE_FRAMES_TX,
  E_MPIPE_GBE_512_TO_1023_BYTE_FRAMES_TX,
  E_MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_TX,
  E_MPIPE_GBE_GREATER_THAN_1518_BYTE_FRAMES_TX,
  E_MPIPE_GBE_TX_UNDER_RUNS,
  E_MPIPE_GBE_SINGLE_COLLISION_FRAMES,
  E_MPIPE_GBE_MULTIPLE_COLLISION_FRAMES,
  E_MPIPE_GBE_EXCESSIVE_COLLISIONS,
  E_MPIPE_GBE_LATE_COLLISIONS,
  E_MPIPE_GBE_DEFERRED_TRANSMISSION_FRAMES,
  E_MPIPE_GBE_CARRIER_SENSE_ERRORS,
  E_MPIPE_GBE_RX_BEGIN,
  E_MPIPE_GBE_OCTETS_RX_LO = E_MPIPE_GBE_RX_BEGIN,
  E_MPIPE_GBE_OCTETS_RX_HI,
  E_MPIPE_GBE_FRAMES_RX,
  E_MPIPE_GBE_BCST_FRAMES_RX,
  E_MPIPE_GBE_MCST_FRAMES_RX,
  E_MPIPE_GBE_PAUSE_FRAMES_RX,
  E_MPIPE_GBE_64_BYTE_FRAMES_RX,
  E_MPIPE_GBE_65_TO_127_BYTE_FRAMES_RX,
  E_MPIPE_GBE_128_TO_255_BYTE_FRAMES_RX,
  E_MPIPE_GBE_256_TO_511_BYTE_FRAMES_RX,
  E_MPIPE_GBE_512_TO_1023_BYTE_FRAMES_RX,
  E_MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_RX,
  E_MPIPE_GBE_GREATER_THAN_1518_BYTE_FRAMES_RX,
  E_MPIPE_GBE_UNDERSIZE_FRAMES_RX,
  E_MPIPE_GBE_OVERSIZE_FRAMES_RX,
  E_MPIPE_GBE_JABBERS_RX,
  E_MPIPE_GBE_FRAME_CHECK_SEQUENCE_ERRORS,
  E_MPIPE_GBE_LENGTH_FRAME_ERRORS,
  E_MPIPE_GBE_RECEIVE_SYMBOL_ERRORS,
  E_MPIPE_GBE_ALIGNMENT_ERRORS,
  E_MPIPE_GBE_RECEIVE_OVERRUNS,
  E_MPIPE_GBE_IP_HEADER_CHECKSUM_ERRORS,
  E_MPIPE_GBE_TCP_CHECKSUM_ERRORS,
  E_MPIPE_GBE_UDP_CHECKSUM_ERRORS,
  E_MPIPE_GBE_END,

  /* XAUI */
  E_MPIPE_XAUI_BEGIN = E_MPIPE_GBE_END,
  E_MPIPE_XAUI_OCTETS_TX_LO = E_MPIPE_XAUI_BEGIN,
  E_MPIPE_XAUI_OCTETS_TX_HI,
  E_MPIPE_XAUI_FRAMES_TX_LO,
  E_MPIPE_XAUI_FRAMES_TX_HI,
  E_MPIPE_XAUI_BCST_FRAMES_TX,
  E_MPIPE_XAUI_MCST_FRAMES_TX,
  E_MPIPE_XAUI_PAUSE_FRAMES_TX,
  E_MPIPE_XAUI_64_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_65_TO_127_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_128_TO_255_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_256_TO_511_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_512_TO_1023_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_1024_TO_1518_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_GREATER_THAN_1518_BYTE_FRAMES_TX,
  E_MPIPE_XAUI_TRANSMITTED_ERROR_FRAMES,
  E_MPIPE_XAUI_OCTETS_RX_LO,
  E_MPIPE_XAUI_OCTETS_RX_HI,
  E_MPIPE_XAUI_FRAMES_RX_LO,
  E_MPIPE_XAUI_FRAMES_RX_HI,
  E_MPIPE_XAUI_BCST_FRAMES_RX,
  E_MPIPE_XAUI_MCST_FRAMES_RX,
  E_MPIPE_XAUI_PAUSE_FRAMES_RX,
  E_MPIPE_XAUI_64_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_65_TO_127_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_128_TO_255_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_256_TO_511_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_512_TO_1023_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_1024_TO_1518_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_GREATER_THAN_1518_BYTE_FRAMES_RX,
  E_MPIPE_XAUI_SHORT_FRAMES_RX,
  E_MPIPE_XAUI_OVERSIZE_FRAMES_RX,
  E_MPIPE_XAUI_JABBERS_RX,
  E_MPIPE_XAUI_FRAME_CRC_ERRORS_RX,
  E_MPIPE_XAUI_LENGTH_FIELD_ERRORS_RX,
  E_MPIPE_XAUI_RECEIVE_SYMBOL_ERRORS_RX,
  E_MPIPE_XAUI_END
};

#define MAC_REG_Defbit(_NAME_, _B_)           \
  B_MAC_##_NAME_ = _B_,                       \
    M_MAC_##_NAME_ = 1<<_B_

enum _e_mac_reg_attr {

  MAC_REG_Defbit(TX,   0),
  MAC_REG_Defbit(RX,   1),
  MAC_REG_Defbit(HI,   2),
  MAC_REG_Defbit(LO,   3),
  MAC_REG_Defbit(ERR,  4),
};

/*
 * struct to describe a MAC register.
 */
typedef struct {

  uint32_t    offset; // Register Offset.
  uint32_t    attri;  // Attributes defined by enum _e_mac_reg_attr.
  const char* name;   // Register name.

} mpipe_mac_reg_t;

static const mpipe_mac_reg_t  mpipe_mac_reg_tab[] = {

  [E_MPIPE_GBE_OCTETS_TX_LO] =
  {
    MPIPE_GBE_OCTETS_TX_LO,
    M_MAC_TX|M_MAC_LO,
    "Octets transmitted"
  },

  [E_MPIPE_GBE_OCTETS_TX_HI] =
  {
    MPIPE_GBE_OCTETS_TX_HI,
    M_MAC_TX|M_MAC_HI,
    "Octets transmitted"
  },

  [E_MPIPE_GBE_FRAMES_TX] =
  {
    MPIPE_GBE_FRAMES_TX,
    M_MAC_TX,
    "Frames transmitted"
  },

  [E_MPIPE_GBE_BCST_FRAMES_TX] =
  {
    MPIPE_GBE_BCST_FRAMES_TX,
    M_MAC_TX,
    "Broadcast frames"
  },

  [E_MPIPE_GBE_MCST_FRAMES_TX] =
  {
    MPIPE_GBE_MCST_FRAMES_TX,
    M_MAC_TX,
    "Multicast frames"
  },

  [E_MPIPE_GBE_PAUSE_FRAMES_TX] =
  {
    MPIPE_GBE_PAUSE_FRAMES_TX,
    M_MAC_TX,
    "Pause frames"
  },

  [E_MPIPE_GBE_64_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_64_BYTE_FRAMES_TX,
    M_MAC_TX,
    "64 byte frames"
  },

  [E_MPIPE_GBE_65_TO_127_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_65_TO_127_BYTE_FRAMES_TX,
    M_MAC_TX,
    "65-127 byte frames"
  },

  [E_MPIPE_GBE_128_TO_255_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_128_TO_255_BYTE_FRAMES_TX,
    M_MAC_TX,
    "128-255 byte frames"
  },

  [E_MPIPE_GBE_256_TO_511_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_256_TO_511_BYTE_FRAMES_TX,
    M_MAC_TX,
    "256-511 byte frames"
  },

  [E_MPIPE_GBE_512_TO_1023_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_512_TO_1023_BYTE_FRAMES_TX,
    M_MAC_TX,
    "512-1023 byte frames"
  },

  [E_MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_TX,
    M_MAC_TX,
    "1024-1518 byte frames"
  },

  [E_MPIPE_GBE_GREATER_THAN_1518_BYTE_FRAMES_TX] =
  {
    MPIPE_GBE_GREATER_THAN_1518_BYTE_FRAMES_TX,
    M_MAC_TX,
    "> 1518 byte frames"
  },

  [E_MPIPE_GBE_TX_UNDER_RUNS] =
  {
    MPIPE_GBE_TX_UNDER_RUNS,
    M_MAC_TX|M_MAC_ERR,
    "Transmit underruns"
  },

  [E_MPIPE_GBE_SINGLE_COLLISION_FRAMES] =
  {
    MPIPE_GBE_SINGLE_COLLISION_FRAMES,
    M_MAC_TX|M_MAC_ERR,
    "Single collision frames"
  },

  [E_MPIPE_GBE_MULTIPLE_COLLISION_FRAMES] =
  {
    MPIPE_GBE_MULTIPLE_COLLISION_FRAMES,
    M_MAC_TX|M_MAC_ERR,
    "Mult. collision frames"
  },

  [E_MPIPE_GBE_EXCESSIVE_COLLISIONS] =
  {
    MPIPE_GBE_EXCESSIVE_COLLISIONS,
    M_MAC_TX|M_MAC_ERR,
    "Excessive collisions"
  },

  [E_MPIPE_GBE_LATE_COLLISIONS] =
  {
    MPIPE_GBE_LATE_COLLISIONS,
    M_MAC_TX|M_MAC_ERR,
    "Late collisions"
  },

  [E_MPIPE_GBE_DEFERRED_TRANSMISSION_FRAMES] =
  {
    MPIPE_GBE_DEFERRED_TRANSMISSION_FRAMES,
    M_MAC_TX|M_MAC_ERR,
    "Deferred tx frames"
  },

  [E_MPIPE_GBE_CARRIER_SENSE_ERRORS] =
  {
    MPIPE_GBE_CARRIER_SENSE_ERRORS,
    M_MAC_TX|M_MAC_ERR,
    "Carrier sense errors"
  },

  [E_MPIPE_GBE_OCTETS_RX_LO] =
  {
    MPIPE_GBE_OCTETS_RX_31_0,
    M_MAC_RX|M_MAC_LO,
    "Octets received"
  },

  [E_MPIPE_GBE_OCTETS_RX_HI] =
  {
    MPIPE_GBE_OCTETS_RX_47_32,
    M_MAC_RX|M_MAC_HI,
    "Octets received"
  },

  [E_MPIPE_GBE_FRAMES_RX] =
  {
    MPIPE_GBE_FRAMES_RX,
    M_MAC_RX,
    "Frames received"
  },

  [E_MPIPE_GBE_BCST_FRAMES_RX] =
  {
    MPIPE_GBE_BROADCAST_FRAMES_RX,
    M_MAC_RX,
    "Broadcast frames"
  },

  [E_MPIPE_GBE_MCST_FRAMES_RX] =
  {
    MPIPE_GBE_MULTICAST_FRAMES_RX,
    M_MAC_RX,
    "Multicast frames"
  },

  [E_MPIPE_GBE_PAUSE_FRAMES_RX] =
  {
    MPIPE_GBE_PAUSE_FRAMES_RX,
    M_MAC_RX,
    "Pause frames"
  },

  [E_MPIPE_GBE_64_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_64_BYTE_FRAMES_RX,
    M_MAC_RX,
    "64 byte frames"
  },

  [E_MPIPE_GBE_65_TO_127_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_65_TO_127_BYTE_FRAMES_RX,
    M_MAC_RX,
    "65-127 byte frames"
  },

  [E_MPIPE_GBE_128_TO_255_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_128_TO_255_BYTE_FRAMES_RX,
    M_MAC_RX,
    "128-255 byte frames"
  },

  [E_MPIPE_GBE_256_TO_511_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_256_TO_511_BYTE_FRAMES_RX,
    M_MAC_RX,
    "256-511 byte frames"
  },

  [E_MPIPE_GBE_512_TO_1023_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_512_TO_1023_BYTE_FRAMES_RX,
    M_MAC_RX,
    "512-1023 byte frames"
  },

  [E_MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_1024_TO_1518_BYTE_FRAMES_RX,

    M_MAC_RX,
    "1024-1518 byte frames"
  },

  [E_MPIPE_GBE_GREATER_THAN_1518_BYTE_FRAMES_RX] =
  {
    MPIPE_GBE_1519_TO_MAXIMUM_BYTE_FRAMES_RX,
    M_MAC_RX,
    "> 1518 byte frames"
  },

  [E_MPIPE_GBE_UNDERSIZE_FRAMES_RX] =
  {
    MPIPE_GBE_UNDERSIZED_FRAMES_RX,
    M_MAC_RX|M_MAC_ERR,
    "Undersize frames"
  },

  [E_MPIPE_GBE_OVERSIZE_FRAMES_RX] =
  {
    MPIPE_GBE_OVERSIZE_FRAMES_RX,
    M_MAC_RX|M_MAC_ERR,
    "Oversize frames"
  },

  [E_MPIPE_GBE_JABBERS_RX] =
  {
    MPIPE_GBE_JABBERS_RX,
    M_MAC_RX|M_MAC_ERR,
    "Jabbers"
  },

  [E_MPIPE_GBE_FRAME_CHECK_SEQUENCE_ERRORS] =
  {
    MPIPE_GBE_FRAME_CHECK_SEQUENCE_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "Frame chk sequence errs"
  },

  [E_MPIPE_GBE_LENGTH_FRAME_ERRORS] =
  {
    MPIPE_GBE_LENGTH_FIELD_FRAME_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "Length frame errors"
  },

  [E_MPIPE_GBE_RECEIVE_SYMBOL_ERRORS] =
  {
    MPIPE_GBE_RECEIVE_SYMBOL_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "Receive symbol errors"
  },

  [E_MPIPE_GBE_ALIGNMENT_ERRORS] =
  {
    MPIPE_GBE_ALIGNMENT_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "Alignment errors"
  },

  [E_MPIPE_GBE_RECEIVE_OVERRUNS] =
  {
    MPIPE_GBE_RECEIVE_OVERRUNS,
    M_MAC_RX|M_MAC_ERR,
    "Receive overruns"
  },

  [E_MPIPE_GBE_IP_HEADER_CHECKSUM_ERRORS] =
  {
    MPIPE_GBE_IP_HEADER_CHECKSUM_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "IP header checksum errs"
  },

  [E_MPIPE_GBE_TCP_CHECKSUM_ERRORS] =
  {
    MPIPE_GBE_TCP_CHECKSUM_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "TCP checksum errors"
  },

  [E_MPIPE_GBE_UDP_CHECKSUM_ERRORS] =
  {
    MPIPE_GBE_UDP_CHECKSUM_ERRORS,
    M_MAC_RX|M_MAC_ERR,
    "UDP checksum errors"
  },


  [E_MPIPE_XAUI_OCTETS_TX_LO] =
  {
    MPIPE_XAUI_TRANSMITTED_OCTETS_LO,
    M_MAC_TX|M_MAC_LO,
    "Octets transmitted"
  },

  [E_MPIPE_XAUI_OCTETS_TX_HI] =
  {
    MPIPE_XAUI_TRANSMITTED_OCTETS_HI,
    M_MAC_TX|M_MAC_HI,
    "Octets transmitted"
  },

  [E_MPIPE_XAUI_FRAMES_TX_LO] =
  {
    MPIPE_XAUI_TRANSMITTED_FRAMES_LO,
    M_MAC_TX|M_MAC_LO,
    "Frames transmitted"
  },

  [E_MPIPE_XAUI_FRAMES_TX_HI] =
  {
    MPIPE_XAUI_TRANSMITTED_FRAMES_HI,
    M_MAC_TX|M_MAC_HI,
    "Frames transmitted"
  },

  [E_MPIPE_XAUI_BCST_FRAMES_TX] =
  {
    MPIPE_XAUI_TRANSMITTED_BROADCAST_FRAMES,
    M_MAC_TX,
    "Bcst frames"
  },

  [E_MPIPE_XAUI_MCST_FRAMES_TX] =
  {
    MPIPE_XAUI_TRANSMITTED_MULTICAST_FRAMES,
    M_MAC_TX,
    "Mcst frames"
  },

  [E_MPIPE_XAUI_PAUSE_FRAMES_TX] =
  {
    MPIPE_XAUI_TRANSMITTED_PAUSE_FRAMES,
    M_MAC_TX,
    "Pause frames"
  },

  [E_MPIPE_XAUI_64_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_64_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "64 byte frames"
  },

  [E_MPIPE_XAUI_65_TO_127_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_65_127_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "65-127 byte frames"
  },

  [E_MPIPE_XAUI_128_TO_255_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_128_255_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "128-255 byte frames"
  },

  [E_MPIPE_XAUI_256_TO_511_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_256_511_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "256-511 byte frames"
  },

  [E_MPIPE_XAUI_512_TO_1023_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_512_1023_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "512-1023 byte frames"
  },

  [E_MPIPE_XAUI_1024_TO_1518_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_1024_1518_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "1024-1518 byte frames"
  },

  [E_MPIPE_XAUI_GREATER_THAN_1518_BYTE_FRAMES_TX] =
  {
    MPIPE_XAUI_1519_MAX_BYTE_FRAMES_TRANSMITTED,
    M_MAC_TX,
    "> 1518 byte frames"
  },

  [E_MPIPE_XAUI_TRANSMITTED_ERROR_FRAMES] =
  {
    MPIPE_XAUI_TRANSMITTED_ERROR_FRAMES,
    M_MAC_TX|M_MAC_ERR,
    "Transmitted err frames"
  },

  [E_MPIPE_XAUI_OCTETS_RX_LO] =
  {
    MPIPE_XAUI_RECEIVED_OCTETS_LO,
    M_MAC_RX|M_MAC_LO,
    "Octets received"
  },

  [E_MPIPE_XAUI_OCTETS_RX_HI] =
  {
    MPIPE_XAUI_RECEIVED_OCTETS_HI,
    M_MAC_RX|M_MAC_HI,
    "Octets received"
  },

  [E_MPIPE_XAUI_FRAMES_RX_LO] =
  {
    MPIPE_XAUI_RECEIVED_FRAMES_LO,
    M_MAC_RX|M_MAC_LO,
    "Frames received"
  },

  [E_MPIPE_XAUI_FRAMES_RX_HI] =
  {
    MPIPE_XAUI_RECEIVED_FRAMES_HI,
    M_MAC_RX|M_MAC_HI,
    "Frames received"
  },

  [E_MPIPE_XAUI_BCST_FRAMES_RX] =
  {
    MPIPE_XAUI_RECEIVED_BROADCAST_FRAMES,
    M_MAC_RX,
    "Bcst frames"
  },

  [E_MPIPE_XAUI_MCST_FRAMES_RX] =
  {
    MPIPE_XAUI_RECEIVED_MULTICAST_FRAMES,
    M_MAC_RX,
    "Mcst frames"
  },

  [E_MPIPE_XAUI_PAUSE_FRAMES_RX] =
  {
    MPIPE_XAUI_RECEIVED_PAUSE_FRAMES,
    M_MAC_RX,
    "Pause frames"
  },

  [E_MPIPE_XAUI_64_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_64_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "64 byte frames"
  },

  [E_MPIPE_XAUI_65_TO_127_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_65_127_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "65-127 byte frames"
  },

  [E_MPIPE_XAUI_128_TO_255_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_128_255_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "128-255 byte frames"
  },

  [E_MPIPE_XAUI_256_TO_511_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_256_511_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "256-511 byte frames"
  },

  [E_MPIPE_XAUI_512_TO_1023_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_512_1023_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "512-1023 byte frames"
  },

  [E_MPIPE_XAUI_1024_TO_1518_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_1024_1518_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "1024-1518 byte frames"
  },

  [E_MPIPE_XAUI_GREATER_THAN_1518_BYTE_FRAMES_RX] =
  {
    MPIPE_XAUI_1519_MAX_BYTE_FRAMES_RECEIVED,
    M_MAC_RX,
    "> 1518 byte frames"
  },

  [E_MPIPE_XAUI_SHORT_FRAMES_RX] =
  {
    MPIPE_XAUI_RECEIVED_SHORT_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Short frames"
  },

  [E_MPIPE_XAUI_OVERSIZE_FRAMES_RX] =
  {
    MPIPE_XAUI_RECEIVED_OVERSIZE_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Oversize frames"
  },

  [E_MPIPE_XAUI_JABBERS_RX] =
  {
    MPIPE_XAUI_RECEIVED_JABBER_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Jabbers"
  },

  [E_MPIPE_XAUI_FRAME_CRC_ERRORS_RX] =
  {
    MPIPE_XAUI_RECEIVED_CRC_ERROR_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Frame crc errors"
  },

  [E_MPIPE_XAUI_LENGTH_FIELD_ERRORS_RX] =
  {
    MPIPE_XAUI_RECEIVED_LENGTH_FIELD_ERROR_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Length field errors"
  },

  [E_MPIPE_XAUI_RECEIVE_SYMBOL_ERRORS_RX] =
  {
    MPIPE_XAUI_RECEIVED_SYMBOL_CODE_ERROR_FRAMES,
    M_MAC_RX|M_MAC_ERR,
    "Recv symbol errors"
  }
};

static
uint64_t mpipe_mac_reg_value_tab[MAX_MPIPE_LINK][E_MPIPE_XAUI_END];

/* Save copy of previous registers values. */
static
uint64_t mpipe_mac_reg_value_tab_save[MAX_MPIPE_LINK][E_MPIPE_XAUI_END];

/* Timers. */
static struct timeval start_time, stop_time;


static const struct _t_config_command
{
  char *    arg;
  uint16_t  command;
  uint16_t  val;
  bool      packet_drop;
  char *    label;
}
  config_command[]  =
    {
      /* 0 */
      {
        "lbl=0",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL,
        true,
        "Packets dropped by [Classifier/LBal]"
      },
      {
        "lbl=drop",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL_ONLY,
        true,
        "Packets dropped by load balancer only"
      },
      {
        "lbl=bkt",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL_BKT,
        true,
        "Packets dropped by load balancer due to bucket full"
      },
      {
        "lbl=nr",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL_NR,
        true,
        "Packets dropped by load balancer due to NotifRing full"
      },
      {
        "lbl=pkts",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL_PKTS,
        false,
        "Packet descriptors passed through the load balancer"
      },
      {
        "lbl=pick",
        GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
        GXIO_MPIPE_STAT_CONFIG_VAL_LBL_PICK,
        false,
        "Packets assigned to new NR due to NR full"
      },
      /* 6 */
      {
        "ipkt=0",
        GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        GXIO_MPIPE_STAT_CONFIG_VAL_IPKT,
        true,
        "Packets dropped or truncated due to [iPKT FIFO full]"
      },
      {
        "ipkt=trunc",
        GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_TRUNCATED,
        true,
        "Packets truncated due to out of iPkt buffer"
      },
      {
        "ipkt=drop",
        GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_DROP,
        true,
        "Packets dropped due to out of iPkt buffer"
      },
      {
        "ipkt=pkts",
        GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
        GXIO_MPIPE_STAT_CONFIG_VAL_IPKT_PKTS,
        false,
        "Packets received from MAC(s)"
      },
      /* 10 */
      {
        "idma=0",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA,
        true,
        "Packets dropped or truncated due to [No Buffer]"
      },
      {
        "idma=bsm",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_STALL,
        false,
        "Cycles stalled due to waiting for buffers"
      },
      {
        "idma=tlb",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_TLB_STALL,
        false,
        "Cycles stalled due to TLB miss"
      },
      {
        "idma=pkts",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_PKTS,
        false,
        "Packets written to Tile memory"
      },
      {
        "idma=bufs",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BUFS,
        false,
        "Buffers consumed"
      },
      {
        "idma=retries",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_RETRIES,
        false,
        "Number of retried commands"
      },
      {
        "idma=sdn_pkts",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_SDN_PKTS,
        false,
        "Packet of SDN sent"
      },
      {
        "idma=sdn_af",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_SDN_AF,
        false,
        "Cycles stalled due to SDN back pressure"
      },
      {
        "idma=trk",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_TRK_AF,
        false,
        "Cycles stalled due to request tracker back pressure"
      },
      {
        "idma=ntf" ,
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_NTF_AF,
        false,
        "Cycles stalled due to notif-Q back pressure"
      },
      {
        "idma=bsm_spill",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_SPILL,
        false,
        "Number of buffer stack spills"
      },
      {
        "idma=bsm_fill",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_FILL,
        false,
        "Number of buffer stack fills"
      },
      {
        "idma=bsm_edma",
        GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
        GXIO_MPIPE_STAT_CONFIG_VAL_IDMA_BSM_EDMA,
        false,
        "Number of buffers returned by eDMA"
      }
    };


/******************************************************************************
 *
 * STATIC FUNCTIONS
 *
 *****************************************************************************/

static inline bool  GBE_LINK(int n)
{
  return (strstr(link_name[n], "gbe") && (link_name[n][0] != 'x'));
}

static inline bool  XGBE_LINK(int n)
{
  return strstr(link_name[n], "xgbe");
}

static bool  LINK_MAC(int n, int i)
{
  if (GBE_LINK(n) && GBE_MAC(i))
    return true;

  if (XGBE_LINK(n) && XGBE_MAC(i))
    return true;

  return false;
}

static const char *N_to_STR(char *str, int len, uint64_t value)
{
#define  KILO  (  1000ULL  )
#define  MEGA  (KILO * KILO)
#define  GIGA  (MEGA * KILO)
#define  TERA  (GIGA * KILO)
#define  PETA  (TERA * KILO)

  if (!human)
  {
    snprintf(str, len, "%lld", (unsigned long long)value);
    return str;
  }

  if (value < KILO)
  {
    snprintf(str, len, "%d", (int)value);
  }
  else if (value < MEGA)
  {
    snprintf(str, len, "%d.%02dK",
             (int)(value / KILO), (int)((value % KILO) / (KILO / 100)));
  }
  else if (value < GIGA)
  {
    snprintf(str, len, "%d.%02dM",
             (int)(value / MEGA), (int)((value % MEGA) / (MEGA / 100)));
  }
  else if (value < TERA)
  {
    snprintf(str, len, "%d.%02dG",
             (int)(value / GIGA), (int)((value % GIGA) / (GIGA / 100)));
  }
  else if (value < PETA)
  {
    snprintf(str, len, "%d.%02dT",
             (int)(value / TERA), (int)((value % TERA) / (TERA / 100)));
  }
  else
  {
    snprintf(str, len, "%d.%02dP",
             (int)(value / PETA), (int)((value % PETA) / (PETA / 100)));
  }

  return str;
}

/******************************************************************************
 * void init(void)
 * Description: Zero up all static variables.
 * Modified: all statics.
 * input:  void
 * Return: void
 **/

static void init(void)
{
  list = false;
  link_count = 0;
  num_counter = 0;
  verbose = false;
  rate = false;
  interval = 0;

  /* Default index on array config_command[]. */
  for(int i = 0; i < sizeof(config_command) / sizeof(config_command[0]); i++)
  {
    if (config_command[i].val == 0 && config_command[i].command < 3)
    {
      config_val_idx[config_command[i].command] = i;
    }
  }

  scan_f = false;
  config_f = false;
  total = false;
}


/******************************************************************************
 * void save_mac_reg(void)
 * Description: Accumulate current content of mpipe_mac_reg_value_tab into
 *   buffer - mpipe_mac_reg_value_tab_save.
 * Modified: mpipe_mac_reg_value_tab_save[][].
 * Input: void
 * Output: void
 *
 **/

static void save_mac_reg(void)
{
  int i, n;
  for (n = 0; n < link_count; n++)
  {
    for (i = 0; i < E_MPIPE_XAUI_END; i++)
    {
      /* Accumulate current values onto save buffer. */
      mpipe_mac_reg_value_tab_save[n][i] += mpipe_mac_reg_value_tab[n][i];
    }
  }
}

/******************************************************************************
 * void do_mac_reg(void)
 * Description: Read MAC registers defined in mpipe_mac_reg_tab.
 * Modified: mpipe_link_ptr[], mpipe_context[], mpipe_context_init[],
 *           mpipe_context_ptr[], mpipe_mac_reg_value_tab[][].
 * Input: void
 * Return: void
 *
 **/

static void do_mac_reg(void)
{
  int n, i;

  /* Loop for every given link name. */
  for (n = 0; n < link_count; n++)
  {
    /* Open the link if we have not done.*/
    if ((mpipe_link_ptr[n] == NULL) && (mpipe_context_ptr[n] == NULL))
    {
      i = gxio_mpipe_link_instance(link_name[n]);
      if ((i >= 0) && (i < 2))
      {
        /* init mpipe context if it not initialized yet. */
        if (!mpipe_context_init[i])
        {
          if (!gxio_mpipe_init(&mpipe_context[i], i))
          {
            mpipe_context_init[i] = true;
          }
        }

        mpipe_context_ptr[n] = &mpipe_context[i];

        if (mpipe_context_init[i])
        {
          if (!gxio_mpipe_link_open(&mpipe_link[n], mpipe_context_ptr[n],
                                    link_name[n],
                                    GXIO_MPIPE_LINK_NO_DATA |
                                    GXIO_MPIPE_LINK_NO_CTL |
                                    GXIO_MPIPE_LINK_STATS |
                                    GXIO_MPIPE_LINK_AUTO_NONE))
          {
            mpipe_link_ptr[n] = &mpipe_link[n];
          }
        }
      }
      else
      {
        mpipe_link_ptr[n] = NULL;
        mpipe_context_ptr[n] = &mpipe_context[0];
      }
    }

    if (mpipe_link_ptr[n])
    {
      for (i = 0; i < E_MPIPE_XAUI_END; i++)
      {
        /*
         * Read a mac register value, if highest bit set, indicate read
         * fails.
         */
        if (!LINK_MAC(n, i))
        {
          /* Link n has no mac register i. */
          mpipe_mac_reg_value_tab[n][i] = 0;
          continue;
        }
        mpipe_mac_reg_value_tab[n][i] =
          gxio_mpipe_link_mac_rd(mpipe_link_ptr[n],
                                 mpipe_mac_reg_tab[i].offset);
      }
    }
  }

  if (total)
  {
    /* Accumulate mac register values on to save buffers. */
    save_mac_reg();
  }
}


/******************************************************************************
 * display_mac_reg_of_link(int n)
 * Description: Display MAC Registers of a link, this routine will be invoked
 *              if the --verbose argument is used.
 * Modified: None
 * input:  int n -  n is an index of link.
 * return: void
 *
 **/

static void display_mac_reg_of_link(int n)
{
  int i = 0, k = 0;
  uint64_t value, value_tx, value_rx;
  bool     tx_found, rx_found;
  uint32_t  delta_ms = (stop_time.tv_sec - start_time.tv_sec) * 1000 +
    (stop_time.tv_usec - start_time.tv_usec) / 1000;

  if (delta_ms == 0)
    delta_ms = 1; // In case divided by ZERO.

  printf("\n");

  do {
    /* Obtain one tx register. */
    tx_found = false;
    for (; i < E_MPIPE_XAUI_END; i++)
    {
      if ((mpipe_mac_reg_tab[i].attri & M_MAC_TX) &&
          LINK_MAC(n, i))
      {
        if (mpipe_mac_reg_tab[i].attri & M_MAC_LO)
        {
          if (total)
          {
            value = mpipe_mac_reg_value_tab_save[n][i] +
             (mpipe_mac_reg_value_tab_save[n][i+1] << 32);
          }
          else
          {
            value = mpipe_mac_reg_value_tab[n][i] +
              (mpipe_mac_reg_value_tab[n][i+1] << 32);
          }
          /* Skip the HI register. Assume HI is immediately following LO reg. */
          i++;
        }
        else
        {
          if (total)
            value = mpipe_mac_reg_value_tab_save[n][i];
          else
            value = mpipe_mac_reg_value_tab[n][i];
        }
        tx_found = true;
        value_tx = value;
        break;
      }
    }

    /* Obtain one rx register. */
    rx_found = false;
    for (; k < E_MPIPE_XAUI_END; k++)
    {
      if ((mpipe_mac_reg_tab[k].attri & M_MAC_RX) &&
          LINK_MAC(n, k))
      {
        if (mpipe_mac_reg_tab[k].attri & M_MAC_LO)
        {
          if (total)
          {
            value = mpipe_mac_reg_value_tab_save[n][k] +
              (mpipe_mac_reg_value_tab_save[n][k+1] << 32);
          }
          else
          {
            value = mpipe_mac_reg_value_tab[n][k] +
              (mpipe_mac_reg_value_tab[n][k+1] << 32);
          }
          /* Skip the HI register. Assume HI is immediately following LO reg. */
          k++;
        }
        else
        {
          if (total)
            value = mpipe_mac_reg_value_tab_save[n][k];
          else
            value = mpipe_mac_reg_value_tab[n][k];
        }
        rx_found = true;
        value_rx = value;
        break;
      }
    }

    if (tx_found && rx_found)
    {
      if (rate)
      {
        char str1[32], str2[32];

        value_tx =  (value_tx * 1000) / delta_ms;
        value_rx =  (value_rx * 1000) / delta_ms;
        printf(" %-23s %12s%2s   %-23s %12s%2s\n",
               mpipe_mac_reg_tab[i].name,
               N_to_STR(str1, sizeof(str1), value_tx), value_tx ? "/s" : "",
               mpipe_mac_reg_tab[k].name,
               N_to_STR(str2, sizeof(str2), value_rx), value_rx ? "/s" : "");
      }
      else
      {
        char str1[32], str2[32];
        printf(" %-23s %14s   %-23s %14s\n",
               mpipe_mac_reg_tab[i].name, N_to_STR(str1, sizeof(str1),
                                                   value_tx),
               mpipe_mac_reg_tab[k].name, N_to_STR(str2, sizeof(str2),
                                                   value_rx));
      }
    }
    else if (tx_found)
    {
      char str[32];
      if (rate)
      {
        value_tx = (value_tx * 1000) / delta_ms;
        printf(" %-23s %12s%2s  \n",
               mpipe_mac_reg_tab[i].name,
               N_to_STR(str, sizeof(str), value_tx),
               value_tx ? "/s" : "");
      }
      else
      {
        printf(" %-23s %14s  \n",
               mpipe_mac_reg_tab[i].name,
               N_to_STR(str, sizeof(str), value_tx));
      }
    }
    else if (rx_found)
    {
      char str[32];
      if (rate)
      {
        value_rx = (value_rx * 1000) / delta_ms;
        printf(" %-38s   %-23s %12s%2s\n", "",
               mpipe_mac_reg_tab[k].name,
               N_to_STR(str, sizeof(str), value_rx),
               value_rx ? "/s" : "");
      }
      else
      {
        printf(" %-38s   %-23s %14s\n", "",
               mpipe_mac_reg_tab[k].name,
               N_to_STR(str, sizeof(str), value_rx));
      }
    }
    i++;
    k++;
  } while(rx_found || tx_found);
}


/******************************************************************************
 * link_stat_display(int n)
 * Description: Display a Link's status.
 * Modified: None
 * input:  int n -  n is an index of link.
 * return: void
 *
 **/

static void link_stat_display(int n)
{
  static bool header = false;
  int i, j;
  uint64_t tx_packets = 0, rx_packets = 0, tx_bytes, rx_bytes,
    tx_packet_errors, rx_packet_errors;

  if (!XGBE_LINK(n) && !GBE_LINK(n))
    return;

  if ((header == false) && (!verbose))
  {
    /* Print header if not did yet or in verbose mode. */
    if (!rate)
      printf("%-8s %8s %16s %8s %16s %8s %8s\n",
             "Link", "Tx pkt", "Tx bits", "Rx pkt", "Rx bits",
             "Tx err", "Rx err");
    else
      printf("%-8s %8s %16s %8s %16s %8s %8s\n",
             "LINK", "Tx pkt/s", "Tx bits/s", "Rx pkt/s", "Rx bits/s",
             "Tx err/s", "Rx err/s");

    printf("------------------------------------------------"
           "--------------------------------\n");
    if (header == false) header = true;
  }

  if (verbose)
    printf("\n%s:", link_name[n]);
  else
    printf("%-8s ", link_name[n]);

  if (!mpipe_link_ptr[n])
  {
    printf(" *** Error. fail to open the link! ***\n");
    return;
  }

  if (GBE_LINK(n))
  {
    if (!total)
      tx_packets = (mpipe_mac_reg_value_tab[n][E_MPIPE_GBE_FRAMES_TX]);
    else
      tx_packets = (mpipe_mac_reg_value_tab_save[n][E_MPIPE_GBE_FRAMES_TX]);

    if (!total)
      rx_packets = (mpipe_mac_reg_value_tab[n][E_MPIPE_GBE_FRAMES_RX]);
    else
      rx_packets = (mpipe_mac_reg_value_tab_save[n][E_MPIPE_GBE_FRAMES_RX]);
  }

  if (XGBE_LINK(n))
  {
    if (!total)
    {
      tx_packets  =  (mpipe_mac_reg_value_tab[n][E_MPIPE_XAUI_FRAMES_TX_LO]);
      tx_packets +=
        ((mpipe_mac_reg_value_tab[n][E_MPIPE_XAUI_FRAMES_TX_HI])<<32);
    }
    else
    {
      tx_packets =
        (mpipe_mac_reg_value_tab_save[n][E_MPIPE_XAUI_FRAMES_TX_LO]);
      tx_packets +=
        ((mpipe_mac_reg_value_tab_save[n][E_MPIPE_XAUI_FRAMES_TX_HI])<<32);
    }

    if (!total)
    {
      rx_packets  =
        (mpipe_mac_reg_value_tab[n][E_MPIPE_XAUI_FRAMES_RX_LO]);
      rx_packets +=
        ((mpipe_mac_reg_value_tab[n][E_MPIPE_XAUI_FRAMES_RX_HI])<<32);
    }
    else
    {
      rx_packets =
        (mpipe_mac_reg_value_tab_save[n][E_MPIPE_XAUI_FRAMES_RX_LO]);
      rx_packets +=
        ((mpipe_mac_reg_value_tab_save[n][E_MPIPE_XAUI_FRAMES_RX_HI])<<32);
    }
  }

#define  PREAMBLE_BYTES   20

  if (XGBE_LINK(n))
  {
    i = E_MPIPE_XAUI_OCTETS_TX_HI;
    j = E_MPIPE_XAUI_OCTETS_TX_LO;
  }
  else
  {
    i = E_MPIPE_GBE_OCTETS_TX_HI;
    j = E_MPIPE_GBE_OCTETS_TX_LO;
  }

  if (!total)
  {
    tx_bytes  =
      ((mpipe_mac_reg_value_tab[n][i]<<32) +
       (mpipe_mac_reg_value_tab[n][j]));
  }
  else
  {
    tx_bytes  =
      ((mpipe_mac_reg_value_tab_save[n][i]<<32) +
       (mpipe_mac_reg_value_tab_save[n][j]));
  }

  tx_bytes += (preamble) ? (PREAMBLE_BYTES * tx_packets) : 0;

  if (XGBE_LINK(n))
  {
    i = E_MPIPE_XAUI_OCTETS_RX_HI;
    j = E_MPIPE_XAUI_OCTETS_RX_LO;
  }
  else
  {
    i = E_MPIPE_GBE_OCTETS_RX_HI;
    j = E_MPIPE_GBE_OCTETS_RX_LO;
  }

  if (!total)
  {
    rx_bytes  =
      ((mpipe_mac_reg_value_tab[n][i]<<32) +
       (mpipe_mac_reg_value_tab[n][j]));
  }
  else
  {
    rx_bytes  =
      ((mpipe_mac_reg_value_tab_save[n][i]<<32) +
       (mpipe_mac_reg_value_tab_save[n][j]));
  }

  rx_bytes += (preamble) ? (PREAMBLE_BYTES * rx_packets) : 0;

  tx_packet_errors = 0;
  rx_packet_errors = 0;

  for (i = 0; i < E_MPIPE_XAUI_END; i++)
  {
    if ((mpipe_mac_reg_tab[i].attri & M_MAC_TX) &&
        (mpipe_mac_reg_tab[i].attri & M_MAC_ERR))
    {
      tx_packet_errors += ((!total) ? (mpipe_mac_reg_value_tab[n][i]) :
                           (mpipe_mac_reg_value_tab_save[n][i]));
    }
    else if ((mpipe_mac_reg_tab[i].attri & M_MAC_RX) &&
             (mpipe_mac_reg_tab[i].attri & M_MAC_ERR))
    {
      rx_packet_errors += ((!total) ? (mpipe_mac_reg_value_tab[n][i]) :
                           (mpipe_mac_reg_value_tab_save[n][i]));
    }
  }

  if (!verbose)
  {
    char str[32];

    if (!rate)
    {
      N_to_STR(str, sizeof(str), tx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), tx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), rx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), rx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), tx_packet_errors);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), rx_packet_errors);
      printf("%8s\n", str);
    }
    else
    {
      /*
       * Display at rate. First figure out how long elapsed between start and
       * stop timers.
       */
      uint32_t  delta_ms = (stop_time.tv_sec - start_time.tv_sec) * 1000 +
        (stop_time.tv_usec - start_time.tv_usec) / 1000;

      if (delta_ms == 0)
        delta_ms = 1; // In case divided by ZERO.

      /* Calculate packet/byte rate, note: performance is not a big deal
       *  here.
       */
      tx_packets = (tx_packets * 1000) / delta_ms;
      rx_packets = (rx_packets * 1000) / delta_ms;
      tx_bytes   = (tx_bytes * 1000) / delta_ms;
      rx_bytes   = (rx_bytes * 1000) / delta_ms;
      tx_packet_errors = (tx_packet_errors * 1000) / delta_ms;
      rx_packet_errors = (rx_packet_errors * 1000) / delta_ms;

      N_to_STR(str, sizeof(str), tx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), tx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), rx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), rx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), tx_packet_errors);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), rx_packet_errors);
      printf("%8s\n", str);
    }
  }
  else
  {
    static const struct
    {
      uint32_t val;
      const char* str;
    } val2str[] =
        {
          { GXIO_MPIPE_LINK_10M,      "10 Mbps" },
          { GXIO_MPIPE_LINK_100M,     "100 Mbps" },
          { GXIO_MPIPE_LINK_1G,       "1 Gbps" },
          { GXIO_MPIPE_LINK_10G,      "10 Gbps" },
          { GXIO_MPIPE_LINK_12G,      "12 Gbps" },
          { GXIO_MPIPE_LINK_20G,      "20 Gbps" },
          { GXIO_MPIPE_LINK_25G,      "25 Gbps" },
          { GXIO_MPIPE_LINK_50G,      "50 Gbps" },
          { GXIO_MPIPE_LINK_FDX,      "Full-duplex" },
          { GXIO_MPIPE_LINK_HDX,      "Half-duplex" },
          { GXIO_MPIPE_LINK_LOOP_MAC, "MAC loopback" },
          { GXIO_MPIPE_LINK_LOOP_PHY, "PHY loopback" },
          { GXIO_MPIPE_LINK_LOOP_EXT, "external loopback" },
        };

    /* Display link stat. */
    if (mpipe_link_stat[n] >= 0)
    {
      uint64_t val = (uint64_t)mpipe_link_stat[n];
      if (val & GXIO_MPIPE_LINK_SPEED_MASK)
        printf("  Link Up");
      else
        printf("  Link Down");

      for (i = 0; i < sizeof (val2str) / sizeof (val2str[0]); i++)
      {
        if (val & val2str[i].val)
        {
          printf(", %s", val2str[i].str);
        }
      }
      printf("\n");
    }
    else
    {
      printf(" Link stat: unknown!\n");
    }

    /* Display MAC registers. */
    display_mac_reg_of_link(n);
  }
}

/******************************************************************************
 * void counter_stat_display(void)
 * Description: Display a mpipe counter.
 * Modified: None
 * input:  void
 * return: void
 *
 **/

static void counter_stat_display(void)
{
  int i;
  char name[24];
  if (!rate)
  {
    if (num_counter)
    {
      printf(" MPIPE Counters:\n  ");
      for (i = 0; i < num_counter; i++)
      {
        long long delta = mpipe_counter[i] - mpipe_counter_save[i];
        sprintf(name, "C%d=ERR", counter_idx[i]);
        if (((i+1)%4 == 0) || ((i+1) == num_counter))
        {
          if (mpipe_counter[i] != -1)
          {
            sprintf(name, "C%d=%llu", counter_idx[i], delta);
          }
          printf("%-16s", name);
          if ((i+1) < num_counter) printf("\n  ");
        }
        else
        {
          sprintf(name, "C%d=%llu,", counter_idx[i], delta);
          printf("%-16s", name);
        }
      }
      printf("\n");
    }
  }
  else
  {
    /*
     * Display at rate. First figure out how long elapsed between start and
     * stop timers.
     */
    uint32_t  delta_ms = (stop_time.tv_sec - start_time.tv_sec) * 1000 +
                         (stop_time.tv_usec - start_time.tv_usec) / 1000;

    if (num_counter)
    {
      printf(" MPIPE Counters: /s\n  ");

      if (delta_ms == 0)
        delta_ms = 1; // In case divided by ZERO.

      for (i = 0; i < num_counter; i++)
      {
        long long delta = (1000*(mpipe_counter[i] -
                                 mpipe_counter_save[i]))/delta_ms;
        sprintf(name, "C%d=ERR!", counter_idx[i]);
        if (((i+1)%4 == 0) || ((i+1) == num_counter))
        {
          if (mpipe_counter[i] != -1)
          {
            sprintf(name, "C%d=%llu", counter_idx[i], delta);
          }
          printf("%-16s", name);
          if ((i+1) < num_counter) printf("\n  ");
        }
        else
        {
          if (mpipe_counter[i] != -1)
          {
            sprintf(name, "C%d=%llu,", counter_idx[i], delta);
          }
          printf("%-16s", name);
        }
      }
      printf("\n");
    }
  }
}


/******************************************************************************
 * void do_mpipe_stat_dispaly(void)
 * Description: Display a Link's status.
 * Modified: None
 * input:  void
 * return: void
 *
 **/

static void do_mpipe_stat_display(void)
{
  int n = 0;
  /* Display mpipe link status. */
  for (n = 0; n < link_count; n++)
  {
    link_stat_display(n);
  }

  if (!verbose && drop)
  {
    uint32_t  delta_ms = (stop_time.tv_sec - start_time.tv_sec) * 1000 +
      (stop_time.tv_usec - start_time.tv_usec) / 1000;
    char str[32];

    /* Display packet drop count delta. */
    for (int i = 0; i < MAX_MPIPE_CONTEXT; i++)
    {
      if (mpipe_context_init[i])
      {
        uint64_t delta = mpipe_stats[i].ingress_drops - packet_drop_start[i];

        if (!delta)
          continue;

        if (rate)
        {
          uint64_t temp = (delta * 1000)/delta_ms;
          printf("%60s drop/%d: %8s/s\n", "",
                 i, N_to_STR(str, sizeof(str), temp));
        }
        else
        {
          printf("%60s drop/%d: %8s\n", "",
                 i,  N_to_STR(str, sizeof(str), delta));
        }
      }
    }
  }

  /* Dispaly mPipe buffer stack status. */
  if (!verbose && buffer_stack_manager)
  {
    bool header;
    for (int i = 0; i < MAX_MPIPE_CONTEXT; i++)
    {
      if (mpipe_context_init[i])
      {
        header = false;
        for (int k = 0; k < HV_MPIPE_NUM_BUFFER_STACKS; k++)
        {
          int count = gxio_mpipe_get_buffer_count(&mpipe_context[i], k);
          if (count >= 0)
          {
            if (!header)
            {
              header = true;
              printf("                                          "
                     "mPIPE%d Buffer Stack: #%2d  %10d\n",
                     i, k, count);
            }
            else
            {
              printf("                                          "
                     "                     #%2d  %10d\n",
                     k, count);
            }
          }
        }
      }
    }
  }

  /* Display mpipe counters. */
  counter_stat_display();
  if (verbose)
  {
    int separator = 0;
    char str1[32], str2[32], str3[32], str4[32];

    for (int i = 0; i < MAX_MPIPE_CONTEXT; i++)
    {
      if (mpipe_context_init[i])
      {
        if (separator)
          printf("\n");
        printf("mPIPE%d Overall Statistics (cumulative)\n"
               " Bytes out         %20s   Bytes in          %20s\n"
               " Packets out       %20s   Packets in        %20s\n"
               " Total drops       %20llu   \n"
               " %-58s %20llu\n"
               " %-58s %20llu\n"
               " %-58s %20llu\n",
               i,
               N_to_STR(str1, sizeof(str1), mpipe_stats[i].egress_bytes),
               N_to_STR(str2, sizeof(str2), mpipe_stats[i].ingress_bytes),
               N_to_STR(str3, sizeof(str3), mpipe_stats[i].egress_packets),
               N_to_STR(str4, sizeof(str4), mpipe_stats[i].ingress_packets),
               (long long)mpipe_stats[i].ingress_drops,
               config_command[config_val_idx[2]].label,
               (long long)mpipe_stats[i].ingress_drops_no_buf,
               config_command[config_val_idx[1]].label,
               (long long)mpipe_stats[i].ingress_drops_ipkt,
               config_command[config_val_idx[0]].label,
               (long long)mpipe_stats[i].ingress_drops_cls_lb);
        separator = 1;
      }
    }
  }
}


/******************************************************************************
 * void do_mpipe_counter(bool acc)
 * Description: Read MPIPE internal counters.
 * Modified: mpipe_counter[], mpipe_counter_save[]
 * Input: void
 * Return: void
 *
 **/

static void do_mpipe_counter(bool acc)
{
  int i, c;

  for (c = 0; c < num_counter; c++)
  {
    mpipe_counter[c] = -1;
    i = (counter_idx[c] / 100);
    if (i < MAX_MPIPE_CONTEXT)
    {
      if (mpipe_context_init[i])
      {
        if (gxio_mpipe_get_counter(&mpipe_context[i], 0x1F &
                                   counter_idx[c], &mpipe_counter[c]))
        {
          mpipe_counter[c] = -1;
          /* Clean up the total counter under error condition. */
          mpipe_counter_save[c] = 0;
        }
        else if(acc)
        {
          /* Copy the counter to the total counter. Link management returns
           * contiguous increment counter. */
          mpipe_counter_save[c] = mpipe_counter[i];
        }
      }
    }
  }
}

/******************************************************************************
 * void save_mpipe_counters(void)
 * Description: Save current content of mpipe_counter into mpipe_counter_save.
 * Modified: mpipe_counter_save[].
 * Input: void
 * Output: void
 *
 **/
static void save_mpipe_counters(void)
{
  int i;
  /* Copy the counter to the total counter. Link management returns contiguous
     increment counter. */
  for (i = 0; i < num_counter; i++)
    mpipe_counter_save[i] = mpipe_counter[i];
}


/******************************************************************************
 * void do_help(void)
 * Description: Provide command usage.
 * Modified: none
 * Input: void
 * Return: void
 *
 **/

static void do_help(void)
{
  printf("\nusage: mpipe-stat  [--help] \n"
         "                   [-l | --list] \n"
         "                   [-i | --interval <interval>] \n"
         "                   [-v | --verbose] \n"
         "                   [-r | --rates] \n"
         "                   [-t | --totals] \n"
         "                   [-p | --preamble] \n"
         "                   [-d | --drop] \n"
         "                   [-h | --human-readable] \n"
         "                   [--buffer-stack] \n"
         "                   [--scan-drop] \n"
         "                   [--config-lbl=<nr>]\n"
         "                   [--config-ipkt=<drop>]\n"
         "                   [--config-idma=<tlb>]\n"
         "                   <link#1> <link#2> ... C<#1> C<#2> ...\n");
}


static void do_help_more(void)
{
  int i;

  do_help();

  printf(" -l | --list           List all potential links.\n"
         " -i | --interval <#>   Repeat run every # seconds.\n"
         " -v | --verbose        Verbose display.\n"
         " -r | --rates          Calculate bits/second data rate.\n"
         " -t | --total          Accumulate data trx.\n"
         " -p | --preamble       Include the packet preamble.\n"
         " -d | --drop           Display packet drop count.\n"
         " -h | --human-readable Print numbers in human readable format.\n"
         " --buffer-stack        Show mPIPE buffer numbers in buffer stacks.\n"
         " --scan-drop           scan packet drop counters.\n"
         " --config-lbl=<nr>     config. lbl counter as <lbl=nr>.\n"
         " --config-ipkt=<drop>  config. ipkt counter as <ipkt=drop>.\n"
         " --config-idma=<tlb>   config. idma counter as <imda=tlb>.\n");

  for (i = 0;
       i < sizeof(config_command) / sizeof(config_command[0]);
       i++)
  {
    printf("        %-16s -%s\n",
           config_command[i].arg,
           config_command[i].label);
  }
}


/******************************************************************************
 * void do_parse_args(int argc, char* argv[])
 * Description:Parse the command arguments.
 * Modified: list, rate, interval, total, verbose, preamble, num_counter,
 *           link_count.
 * Input: argc - argument count.
 *        argv[] - argument array.
 * Return: void
 *
 **/

static void do_parse_args(int argc, char* argv[])
{
  int  i = 0;

  if (argc == 1)
  {
    do_help();
    return;
  }

  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      do_help_more();
    }
    else if ((!strcmp(argv[i], "-l")) || (!strcmp(argv[i], "--list")))
    {
      list = true;
    }
    else if ((!strcmp(argv[i], "-r")) || (!strcmp(argv[i], "--rate")) ||
             (!strcmp(argv[i], "--rates")))
    {
      rate = true;
    }
    else if ((!strcmp(argv[i], "-i")) || (!strcmp(argv[i], "--interval")))
    {
      if(argc > ++i)
      {
        interval = atoi(argv[i]);
        if (interval < 0)
        {
          fprintf(stderr, "invalid arg %d, %s.\n", i, argv[i]);
          exit(1);
        }
        else if(interval > 60)
        {
          printf("warning: interval setting is more than 1 min!");
        }
      }
    }
    else if (!strcmp(argv[i], "--scan_drop") ||
             !strcmp(argv[i], "--scan-drop"))
    {
      scan_f = true;
    }
    else if (!strncmp(argv[i], "--config-lbl=", strlen("--config-lbl=")) ||
             !strncmp(argv[i], "--config-ipkt=", strlen("--config-ipkt=")) ||
             !strncmp(argv[i], "--config-idma=", strlen("--config-idma=")))
    {
      int k;
      for (k = 0;
           k < sizeof(config_command) / sizeof(config_command[0]);
           k++)
      {
        if (!strcmp(argv[i] + strlen("--config-"), config_command[k].arg))
        {
          config_val[config_command[k].command] = config_command[k].val;
          config_val_idx[config_command[k].command] = k;
#ifdef MPIPE_STAT_DEBUG
          printf("config %s: %d, %d, %s\n",
                 argv[i], config_command[k].command, config_command[k].val,
                 config_command[k].label);
#endif
          k = -1;
          break;
        }
      }
      if (k != -1)
      {
        fprintf(stderr, "Invalid arg %d, %s.\n", i, argv[i]);
      }
    }
    else if ((!strcmp(argv[i], "-t")) ||
             (!strcmp(argv[i], "--total")) || (!strcmp(argv[i], "--totals")))
    {
      total = true;
    }
    else if ((!strcmp(argv[i], "-v")) || (!strcmp(argv[i], "--verbose")))
    {
      verbose = true;
    }
    else if ((!strcmp(argv[i], "-p")) || (!strcmp(argv[i], "--preamble")))
    {
      preamble = true;
    }
    else if ((!strcmp(argv[i], "-d")) ||
             (!strcmp(argv[i], "--packet-drops")))
    {
      drop = true;
    }
    else if ((!strcmp(argv[i], "-h")) ||
             (!strcmp(argv[i], "--human-readable")))
    {
      human = true;
    }
    else if ((strstr(argv[i], "gbe")) || (strstr(argv[i], "loop")))
    {
      if (link_count < MAX_MPIPE_LINK)
      {
        strncpy(link_name[link_count], argv[i], GXIO_MPIPE_LINK_NAME_LEN);
        link_count++;
      }
    }
    else if (((argv[i][0] == 'C') || (argv[i][0] == 'c')) &&
             (strlen(argv[i]) > 1) && (strlen(argv[i]) < 5))
    {
      char *p = argv[i];
      if (num_counter < MAX_NUM_COUNTER)
      {
        counter_idx[num_counter] = atoi(++p);
        if (((counter_idx[num_counter]/100) < MAX_MPIPE_CONTEXT) &&
            ((counter_idx[num_counter] % 100) < MAX_NUM_COUNTER))
        {
          num_counter++;
        }
      }
    }
    else if (!strcmp(argv[i], "--buffer-stack"))
    {
      buffer_stack_manager = true;
    }
    else if ((strlen(argv[i]) > 2) && (argv[i][0] == '-') && (argv[i][1] != '-'))
    {
      fprintf(stderr, "Invalid arg: multi-character flags not supported. (%s)\n", argv[i]);
      exit(1);
    }
    else
    {
      fprintf(stderr, "Invalid arg: %d, %s.\n", i, argv[i]);
      exit(1);
    }
  }

  if (!link_count && (verbose || total || rate || preamble || interval))
  {
    fprintf(stderr, "Invalid args: no link name!\n");
    exit(1);
  }

  if (rate && !interval)
  {
    fprintf(stderr, "Invalid args: --rate requires --interval.\n");
    exit(1);
  }

  if (rate && total)
  {
    fprintf(stderr, "Invalid args: --rate and --total can't co-exist.\n");
    exit(1);
  }
}

/******************************************************************************
 * void do_list(void)
 * Description:list all available links for the system..
 * Modified: None
 * Input: void
 * Return: void
 *
 **/

static void do_list(void)
{
  int  i = 0, header = 0;
  char name[GXIO_MPIPE_LINK_NAME_LEN + 8];
  for (i = 0; i < MAX_MPIPE_LINK; i++)
  {
    if (!gxio_mpipe_link_enumerate(i, name))
    {
      if (!header)
        printf("Legal mpipe Link names:");
      if (i % 8 == 0) printf("\n");
      printf(" %s", name);
      header++;
    }
  }
  if (!header)
    printf("No legal mpipe Link found!");
  printf("\n");
}

/******************************************************************************
 * void do_mpipe_stat(void)
 * Description: Collect link stat and counter info.
 * Input: void
 * Return: void
 *
 **/

static void do_mpipe_stat(void)
{
  if (link_count)
  {
    /* Retrieve MAC registers. */
    do_mac_reg();
  }

  if (verbose && !config_f)
  {
    int i;
    for (i = 0; i < MAX_MPIPE_CONTEXT; i++)
    {
      if (mpipe_context_init[i])
      {
        int retval = 0;
        if (config_val[0])
        {
          retval = gxio_mpipe_config_stats(&mpipe_context[i],
                                           GXIO_MPIPE_STAT_CONFIG_COMM_LBL,
                                           config_val[0]);
          if (retval)
          {
            fprintf(stderr, "Error: config. mpipe #%d classifier/lbl "
                    "drop counters! %d, %s.\n", i,
                    config_val[0], gxio_strerror(retval));
            exit(1);
          }
        }

        if (config_val[1])
        {
          retval = gxio_mpipe_config_stats(&mpipe_context[i],
                                           GXIO_MPIPE_STAT_CONFIG_COMM_IPKT,
                                           config_val[1]);
          if (retval)
          {
            fprintf(stderr, "Error: config. mpipe #%d ipkt counter! "
                    "%d, %s.\n", i,
                    config_val[1], gxio_strerror(retval));
            exit(1);
          }
        }

        if (config_val[2])
        {
          retval = gxio_mpipe_config_stats(&mpipe_context[i],
                                           GXIO_MPIPE_STAT_CONFIG_COMM_IDMA,
                                           config_val[2]);
          if (retval)
          {
            fprintf(stderr, "Error: config. mpipe #%d idma counter! "
                    "%d, %s.\n", i,
                    config_val[2], gxio_strerror(retval));
            exit(1);
          }
        }
      }
    }
    config_f = true;
  }

  if (num_counter)
  {
    /* Retrieve MPIPE counters. */
    do_mpipe_counter(!total);
  }

  if (verbose || drop)
  {
    int i;

    for (i = 0; i < MAX_MPIPE_CONTEXT; i++)
    {
      if (mpipe_context_init[i])
      {
        /* Save the previous ingress drop count. */
        packet_drop_start[i] = mpipe_stats[i].ingress_drops;
        gxio_mpipe_get_stats(&mpipe_context[i], &mpipe_stats[i]);
      }
    }

    /* Retrieve link stat. */
    memset((void*)&mpipe_link_stat, 0, sizeof(mpipe_link_stat));
    for (i = 0; i < link_count; i++)
    {
      if (mpipe_link_ptr[i])
      {
        mpipe_link_stat[i] =
          gxio_mpipe_link_get_attr(mpipe_link_ptr[i],
                                   GXIO_MPIPE_LINK_CURRENT_STATE);
      }
    }
  }
}

/******************************************************************************
 * void scan_packet_drop(void)
 * Description: scan packet drop counters.
 * Input: void
 * Return: void
 * Description: Change collection reasons of packet drop counters and
 *              accumulate them for each different drop reason.
 **/

void scan_packet_drop(void)
{
  int n, tot, t;

  uint32_t drop_counter[MAX_MPIPE_CONTEXT][16];

  if (interval <= 0)
    interval = 1;

  do_mac_reg();

  memset((void*)drop_counter, 0, sizeof(drop_counter));

  /* Figure out how many programmed counters. */
  tot = 0;
  for (int i = 0;
       i < sizeof(config_command) / sizeof(config_command[0]);
       i++)
  {
    if (config_command[i].packet_drop)
    {
      tot++;
    }
  }

  /* time count t starts. */
  t = 0;

  while (1)
  {
    /*
     * Collect data!
     * Reset index = 0 to the drop_counter[0].
     */

    n = 0;

    for (int i = 0;
         i < sizeof(config_command) / sizeof(config_command[0]);
         i++)
    {
      if (config_command[i].packet_drop)
      {
        /* Only reconfig. the counter to collect packet drop event. */
        for (int k = 0; k < MAX_MPIPE_CONTEXT; k++)
        {
          if (mpipe_context_init[k])
          {
            int retval = gxio_mpipe_config_stats(&mpipe_context[k],
                                                 config_command[i].command,
                                                 config_command[i].val);
            if (retval)
            {
              fprintf(stderr, "error: fail to reconfig. mpipe packet "
                      "drop counters! %s\n", gxio_strerror(retval));
              exit(1);
            }

            retval = gxio_mpipe_get_stats(&mpipe_context[k],
                                          &mpipe_stats[k]);
            if (retval)
            {
              fprintf(stderr, "error: gxio_mpipe_get_stats() %s",
                      gxio_strerror(retval));
              exit(1);
            }

            switch (config_command[i].command)
            {
            case GXIO_MPIPE_STAT_CONFIG_COMM_LBL:
              drop_counter[k][n] = mpipe_stats[k].ingress_drops_cls_lb;
              break;
            case GXIO_MPIPE_STAT_CONFIG_COMM_IPKT:
              drop_counter[k][n] = mpipe_stats[k].ingress_drops_ipkt;
              break;
            case   GXIO_MPIPE_STAT_CONFIG_COMM_IDMA:
              drop_counter[k][n] = mpipe_stats[k].ingress_drops_no_buf;
              break;
            }
          }
        }

        /* To be simple, just delay interval seconds! */
        sleep(interval);

        /*
         * Bump up the time counter. This is pretty rough timer. Assume
         * it is accurate enough.
         */
        t++;

        for (int k = 0; k < MAX_MPIPE_CONTEXT; k++)
        {
          if (mpipe_context_init[k])
          {
            int retval = gxio_mpipe_get_stats(&mpipe_context[k],
                                              &mpipe_stats[k]);

            if (retval)
            {
              fprintf(stderr, "error: gxio_mpipe_get_stats() %s",
                      gxio_strerror(retval));
              exit(1);
            }

            switch (config_command[i].command)
            {
            case GXIO_MPIPE_STAT_CONFIG_COMM_LBL:
              drop_counter[k][n] =
                mpipe_stats[k].ingress_drops_cls_lb - drop_counter[k][n];
              break;
            case GXIO_MPIPE_STAT_CONFIG_COMM_IPKT:
              drop_counter[k][n] =
                mpipe_stats[k].ingress_drops_ipkt - drop_counter[k][n];
              break;
            case   GXIO_MPIPE_STAT_CONFIG_COMM_IDMA:
              drop_counter[k][n] =
                mpipe_stats[k].ingress_drops_no_buf - drop_counter[k][n];
              break;
            }
            n++;
          }
        }
      }
    }

    /* Show one round results. */
    bool header = false;
    for (int k = 0; k < MAX_MPIPE_CONTEXT; k++)
    {
      if (mpipe_context_init[k])
      {
        n = 0;
        if (!header)
        {
          printf("\n\n");
          printf(" mpipe:%d\n", k);
          printf(" %-54s  %12s  %9s\n",  "Packet drop reasons",
                 "drop count", "drop/sec.");
          printf(" %-54s  %12s  %9s\n",
                 "------------------------------------------",
                 "------------", "---------");
          header = true;
        }

        for (int i = 0;
             i < sizeof(config_command) / sizeof(config_command[0]);
             i++)
        {
          if (config_command[i].packet_drop)
          {
            printf(" %-54s  %12d  %7d%2s\n",
                   config_command[i].label,
                   (int)drop_counter[k][n] * tot,
                   (int)(((uint64_t)drop_counter[k][n]) *
                         tot/(t * interval)),
                   (drop_counter[k][n]) ? "/s" : "");
            n++;
          }
        }
      }
    }
  }
}

/******************************************************************************
 *
 *   Main function of mpipe-stat.
 *
 *****************************************************************************/

int main(int argc, char* argv[])
{
  /* Zero up all variables first. */
  init();

  /* parse the input arguments. */
  do_parse_args(argc, argv);

  if(!(link_count || num_counter || list))
  {
    /* Nothing to do, exit. */
    return 0;
  }

  if (list)
  {
    /* List all possible links. */
    do_list();
    list = 0;
  }

  if (!link_count) return 0;

  if (scan_f)
  {
    scan_packet_drop();
    return 0;
  }

  /* Mark the start time. */
  gettimeofday(&start_time, NULL);

  /* Collect link/counter stat first. */
  do_mpipe_stat();

  /* Save the initial mpipe counters. */
  save_mpipe_counters();

  /* Output link stat. */
  if (!rate)
    do_mpipe_stat_display();

  while (interval > 0)
  {
    /* Subsequent display in a loop every <internal> seconds, not need
     * preciously. */
    sleep(interval);

    /* Collect link/counter status. */
    do_mpipe_stat();

    /* Mark the stop time. */
    gettimeofday(&stop_time, NULL);

    /* Output link stat. */
    do_mpipe_stat_display();

    /* Save stop timer as start timer, in case in a loop. */
    if (!total)
      start_time = stop_time;

    fflush(stdout);
  }

  return 0;
}

