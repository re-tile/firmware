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
 * Routines to manage client downcalls.
 */

#ifndef _SYS_HV_DOWNCALL_H
#define _SYS_HV_DOWNCALL_H

#include "types.h"

/** Bits in pending_downcalls */
#define DOWNCALL_MESSAGE_RCV     0x01  /**< Message receive */
#define DOWNCALL_DMATLB_MISS     0x02  /**< DMA miss */
#define DOWNCALL_DMATLB_ACCESS   0x04  /**< DMA access violation */
#define DOWNCALL_SNITLB_MISS     0x08  /**< SNI miss */
#define DOWNCALL_DEV_INTR        0x10  /**< Device interrupt */

/** This structure is designed to keep all of our global downcall state
 *  together, so that we can efficiently access it from assembly routines.
 */
typedef struct downcall_info
{
  /** Bitmap of downcalls that the user hasn't gotten yet */
  uint32_t pending_downcalls;

} downcall_info_t;

/** Global downcall/interrupt state. */
extern downcall_info_t downcall_info;

/** DMA downcall state */
extern uint32_t downcall_dma_missreason;    /**< 1 for write, 0 for read */
extern uint32_t downcall_dma_faultaddr;     /**< Faulting address */

/** SNI downcall state */
extern uint32_t downcall_sni_missreason;    /**< always 0, but parallels DMA */
extern uint32_t downcall_sni_faultaddr;     /**< Faulting address */

/** Handle the hv_dev_register_intr_state() syscall. */
int syscall_dev_register_intr_state(VA* intr_state);

/** Allocate shared interrupt control flag and register the instant
 *  interrupt path. */
void init_client_intrs(void);

/** Enable the message downcall. */
void downcall_message(void);

/** Enable the DMA downcall.
 * @param faultaddr Fault address.
 * @param missreason Type of miss; 0 is read, 1 is write.
 * @param is_accvio Nonzero for an access violation, 0 for a miss.
 */
void downcall_dma(uint32_t faultaddr, uint32_t missreason, int is_accvio);

/** Enable the SNI downcall.
 * @param faultaddr Fault address.
 */
void downcall_sni(uint32_t faultaddr);

/** Disable one or more downcalls.
 * @param mask Downcall type bits (DOWNCALL_xxx).
 */
void downcall_clear(uint32_t mask);

#endif /* _SYS_HV_DOWNCALL_H */
