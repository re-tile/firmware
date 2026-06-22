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
 * mPIPE driver.
 */
#ifndef _DRIVERS_MPIPE_MPIPE_H_
#define _DRIVERS_MPIPE_MPIPE_H_

#include <types.h>

#include <arch/mpipe.h>
#include <arch/mpipe_constants.h>
#include <arch/mpipe_gbe.h>
#include <arch/mpipe_xaui.h>

#include <hv/drv_mpipe_intf.h>
#include <hv/iorpc.h>

#include "classifier.h"
#include "link.h"

#include "lock.h"


/** This service domain is reserved for use by the hypervisor. */
#define RESERVED_SVC_DOM 0

/** This fake service domain is used to denote a client using the
 *  mpipe_info IORPCs only. */
#define INFO_SVC_DOM MPIPE_MMIO_NUM_SVC_DOM

#define MPIPE_NUM_BUFFER_STACKS 32
#define MPIPE_NUM_NOTIF_RINGS 256

/** Each TOS_IDX in the MPIPE_BSM_INIT_DATA_0 represents
 *  12 buffers. */
#define BSM_BUFFER_COUNT_PER_TOS_IDX  12

/** Link interrupts for pollable file descriptors. */
typedef struct
{
  /** PA which, when written to, causes the requested IPI. */
  PA ipi_addr;

  /** Bitmap of MAC index numbers whose state change causes this interrupt.  If
   *  zero, interrupt is disabled. */
  uint32_t intr_macs;

  /** Is interrupt armed? */
  uint8_t armed;
}
pollfd_link_intr_t;

/** Number of link interrupts for pollable file descriptors per service
 *  domain.  This is pretty arbitrary and could be easily changed. */
#define MPIPE_POLLFD_LINK_INTR_PER_SD 16

/** Per-service-domain mPIPE state. */
typedef struct
{
  /** Resource consumption bitmask (notif rings and buckets). */
  /** FIXME - this is gx36-specific */
  MPIPE_MMIO_INIT_DAT_GX36_0_t data0;

  /** Resource consumption bitmask (buffer stacks, eDMA, and cfg). */
  MPIPE_MMIO_INIT_DAT_GX36_1_t data1;

  /** Resource consumption bitmask (notif groups). */
  unsigned int notif_group_mask;

  /** Channels associated with MACs opened with data permission. */
  uint32_t channels;

  /** MACs opened with data permission.  Bitmap of MAC table indices. */
  uint32_t data_macs;

  /** MACs opened with stats permission.  Bitmap of MAC table indices. */
  uint32_t stats_macs;

  /** MACs opened with control permission.  Bitmap of MAC table indices. */
  uint32_t control_macs;

  /** MACs opened with down-on-close enabled.  Bitmap of MAC table indices. */
  uint32_t auto_down_macs;

  /** Link interrupts for pollable file descriptors. */
  pollfd_link_intr_t pollfd_link_intrs[MPIPE_POLLFD_LINK_INTR_PER_SD];
}
mpipe_resources_t;


/** Global mPIPE state. */
typedef struct _mpipe_state
{
  /* Must hold this lock to modify shared data. */
  spinlock_t lock;

  /** Shim location. */
  pos_t shim_pos;

  /** Shim instance. */
  int instance;

  /** Shim virtual instance; this is the instance potentially translated
   *  through a SHIM_VIRT_INSTANCE BIB item, and is used to look up
   *  link-related BIB items. */
  int virt_instance;

  /** A bit set for each unallocated svc_dom. */
  unsigned long long svc_dom_avail_mask;

  /** Allocated resources. */
  mpipe_resources_t resources;

  /** Allocated resources for each service domain. */
  mpipe_resources_t svc_dom_resources[MPIPE_MMIO_NUM_SVC_DOM];

  /** Resource initialization bitmask (buffer stacks). */
  uint64_t initialized_stacks;

  /** Number of iotlb entries used per buffer stack. */
  // ISSUE: Could just use "uint8".
  unsigned int iotlb_entries_used[MPIPE_NUM_BUFFER_STACKS];

  /** Storage for rpc calls. */
  char rpc_buf[65536];

  /** The rules for each service domain. */
  gxio_mpipe_rules_list_t rules_list[MPIPE_MMIO_NUM_SVC_DOM];

  /** The classifier info. */
  classifier_info_t classifier_info;

  /** The classifier blast info. */
  classifier_blast_t classifier_blast;

  /** Our MAC table, kept in shared memory. */
  struct mac_state* macs;

  /** Number of MACs in the MAC table. */
  int n_macs;

  /** SERDES receive lane length, in notional mm. */
  int16_t serdes_rx_lane_length[16];

  /** SERDES transmit lane length, in notional mm. */
  int16_t serdes_tx_lane_length[16];

  /** Config stat flags. */
  uint8_t stat_config[GXIO_MPIPE_STAT_CONFIG_COMM_MAX];

  /** Config stat fd. */
  int stat_config_svc_dom;

  /** Config flag */
  bool stat_config_on;

  /** Config stat statistics. */
  struct {
    /** Ingress LBL counter */
    uint64_t ingress_lb_count;
    /** Ingress ipkt counter */
    uint64_t ipkt_count;
    /** iDMA drop counter */
    uint64_t idma_count;
  } config_stats;

  /** Accumulated statistics. */
  gxio_mpipe_stats_t stats;

  /** Accumulated counters. */
  uint64_t counters[32];

  /** Nonzero if we've set the mPIPE timestamp to run at the same rate as
   *  the cycle counter. */
  uint8_t tstamp_is_cycle: 1;

  /** Bitmap of in-use phy device addresses on the 1G MDIO bus */
  uint32_t gbe_mdio_dev_mask;

  /** Bitmap of in-use phy device addresses on the 10G MDIO bus */
  uint32_t xgbe_mdio_dev_mask;
}
mpipe_state_t;



/** Helpful macro for creating a range of contiguous bits. */
#define BIT_RANGE(LOW_BIT, HIGH_BIT)                            \
  ((-1ULL >> (64 - ((HIGH_BIT) - (LOW_BIT) + 1))) << (LOW_BIT))


/** A generic function for checking a range of resources in a bitmask.
 *
 * @param res_per_bit Number of resources per bit in bitmask.
 * @param in_use_mask Currently allocated resource bits.
 * @param bitmask_bits Number of valid bits that could be in bitmask.
 * @param first_res First resource number.
 *
 * @return true if the resource is legal and allocated, else false.
 */
static __inline bool
good_aux(unsigned int res,
         uint64_t in_use_mask,
         unsigned int res_per_bit,
         unsigned int bitmask_bits)
{
  unsigned int bit = res / res_per_bit;
  return (bit < bitmask_bits && (in_use_mask & (1ULL << bit)) != 0);
}


/** Determine if "ering" is legal and has been allocated. */
//
static __inline bool
good_ering(mpipe_state_t* ms, int svc_dom, unsigned int ering)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  return good_aux(ering, svc_dom_resources->data1.edma_post_mask,
                  HV_MPIPE_ALLOC_EDMA_RINGS_RES_PER_BIT,
                  HV_MPIPE_ALLOC_EDMA_RINGS_BITS);
}


/** Determine if "ring" is legal and has been allocated. */
static __inline bool
good_ring(mpipe_state_t* ms, int svc_dom, unsigned int ring)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  return good_aux(ring, svc_dom_resources->data0.notif_ring_mask,
                  HV_MPIPE_ALLOC_NOTIF_RINGS_RES_PER_BIT,
                  HV_MPIPE_ALLOC_NOTIF_RINGS_BITS);
}


/** Determine if "group" is legal and has been allocated. */
static __inline bool
good_group(mpipe_state_t* ms, int svc_dom, unsigned int group)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  return good_aux(group, svc_dom_resources->notif_group_mask,
                  HV_MPIPE_ALLOC_NOTIF_GROUPS_RES_PER_BIT,
                  HV_MPIPE_ALLOC_NOTIF_GROUPS_BITS);
}


/** Determine if "bucket" is legal and has been allocated. */
static __inline bool
good_bucket(mpipe_state_t* ms, int svc_dom, unsigned int bucket)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  if (bucket < HV_MPIPE_NUM_LO_BUCKETS)
  {
    return good_aux(bucket,
                    svc_dom_resources->data0.bucket_release_mask_lo,
                    HV_MPIPE_ALLOC_LO_BUCKETS_RES_PER_BIT,
                    HV_MPIPE_ALLOC_LO_BUCKETS_BITS);
  }
  else
  {
    return good_aux(bucket - HV_MPIPE_NUM_LO_BUCKETS,
                    svc_dom_resources->data0.bucket_release_mask_hi,
                    HV_MPIPE_ALLOC_HI_BUCKETS_RES_PER_BIT,
                    HV_MPIPE_ALLOC_HI_BUCKETS_BITS);
  }
}


/** Determine if "stack" is legal and has been allocated. */
static __inline bool
good_stack(mpipe_state_t* ms, int svc_dom, unsigned int stack)
{
  mpipe_resources_t* svc_dom_resources = &ms->svc_dom_resources[svc_dom];
  return good_aux(stack, svc_dom_resources->data1.buffer_stack_mask,
                  HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT,
                  HV_MPIPE_ALLOC_BUFFER_STACKS_BITS);
}


void init_link_data(mpipe_state_t* ms);
void init_link_intrs(mpipe_state_t* ms);


#endif /* ! _DRIVERS_MPIPE_MPIPE_H_ */
