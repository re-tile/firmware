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

#include <arch/spr.h>

#include <hv/hypervisor.h>
#include "sys/libc/include/util.h"

#include "debug.h"
#include "downcall.h"
#include "fault.h"
#include "mapping.h"
#include "msg.h"
#include "types.h"

/** Global downcall/interrupt state. */
downcall_info_t downcall_info;

uint32_t downcall_dma_missreason;
uint32_t downcall_dma_faultaddr;

uint32_t downcall_sni_missreason;
uint32_t downcall_sni_faultaddr;


void
downcall_message()
{
  int ics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  downcall_info.pending_downcalls |= DOWNCALL_MESSAGE_RCV;
  __insn_mtspr(INTCTRL_CL_STATUS, 1);

  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, ics);
}


void
downcall_dma(uint32_t faultaddr, uint32_t missreason, int is_accvio)
{
  int ics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  if (downcall_info.pending_downcalls &
      (DOWNCALL_DMATLB_MISS | DOWNCALL_DMATLB_ACCESS))
    panic("multiple simultaneous DMA downcalls; last fault %#x, this fault %#x",
          downcall_dma_faultaddr, faultaddr);

  downcall_dma_missreason = missreason;
  downcall_dma_faultaddr = faultaddr;
  downcall_info.pending_downcalls |= (is_accvio ? DOWNCALL_DMATLB_ACCESS :
                                    DOWNCALL_DMATLB_MISS);
  __insn_mtspr(INTCTRL_CL_STATUS, 1);

  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, ics);
}


void
downcall_sni(uint32_t faultaddr)
{
  int ics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  if (downcall_info.pending_downcalls & DOWNCALL_SNITLB_MISS)
    panic("multiple simultaneous SNI downcalls; last fault %#x, this fault %#x",
          downcall_dma_faultaddr, faultaddr);

  downcall_sni_missreason = 0;
  downcall_sni_faultaddr = faultaddr;
  downcall_info.pending_downcalls |= DOWNCALL_SNITLB_MISS;
  __insn_mtspr(INTCTRL_CL_STATUS, 1);

  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, ics);
}


void
downcall_clear(uint32_t mask)
{
  int ics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  downcall_info.pending_downcalls &= ~mask;

  if (!downcall_info.pending_downcalls)
    __insn_mtspr(INTCTRL_CL_STATUS, 0);

  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, ics);
}


void
init_client_intrs()
{
}
