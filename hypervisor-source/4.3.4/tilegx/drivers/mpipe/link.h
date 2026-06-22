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

/**
 * @file
 * Link-related definitions for the mPIPE driver.
 */
#ifndef _DRIVERS_MPIPE_LINK_H_
#define _DRIVERS_MPIPE_LINK_H_

#include "drvintf.h"
#include "lock.h"

#include "hvbme/enet.h"

/** Maximum number of interrupt channels which will be consumed by all
 *  links on any one mPIPE shim.  If there are more links than this, some
 *  links will share interrupt channels. */
#define LINK_INTR_PER_MPIPE  7

/** Entry in the MAC table.  An index into this table is called a "MAC
 *  table index"; this is distinct from a "MAC number", which identifies
 *  which hardware MAC is in use.  The former value changes depending on
 *  which other MACs exist in the system; the latter is fixed based on
 *  the interface type and which SERDES lanes it's connected to. */
struct mac_state
{
  /** Link config object, used with the enet_xxx routines to actually
   *  manipulate the link. */
  enet_link_config_t link_config;

  /** Pointer to next MAC table entry which shares its interrupt channel
   *  with this one. */
  struct mac_state* intr_next;

  /** Nonzero if we've called enet_mac_config() on this MAC. */
  uint8_t mac_configured:1;

  /** Number of shared opens of this MAC with data permission, or -1 if
   *  a client has exclusive data permission. */
  int8_t num_data_opens;

  /** Number of shared opens of this MAC with stats permission, or -1 if
   *  a client has exclusive stats permission. */
  int8_t num_stats_opens;

  /** Number of shared opens of this MAC with control permission, or -1 if
   *  a client has exclusive control permission. */
  int8_t num_control_opens;

  /** Set of channel numbers used by this MAC.  We give the lowest of
   *  these to the user. */
  uint_reg_t channels;

  /** Signal descriptor for a link LED. */
  sigdesc_t led_sig;
};


/** Entry in the link name table. */
struct link_name_table
{
  /** Link name (gbe0, xgbe1, loop2, etc.) */
  char name[7];
  /** Shim index. */
  uint8_t shim;
  /** Index in our shim's MAC table. */
  uint8_t mac_tbl_idx;
  /** Channel number.  FIXME: get this from the MAC table entry instead. */
  uint8_t channel;
  /** MAC address. */
  uint8_t mac[6];
};

#endif /* ! _DRIVERS_MPIPE_LINK_H_ */
