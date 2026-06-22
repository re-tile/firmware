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
 * Routines to manage the translation storage buffer.
 */

#ifndef _SYS_HV_TSB_H
#define _SYS_HV_TSB_H

#include <arch/abi.h>

#include "bits.h"
#include "page.h"
#include "param.h"
#include "tte.h"
#include "types.h"

/** Mask for small TSB index from a small PFN */
#define TSB_S_IDX_MASK  RMASK(TSB_S_IDX_WIDTH)
/** Number of entries in the small TSB */
#define TSB_S_ENTRIES   (1 << TSB_S_IDX_WIDTH)
/** Size in bytes of the small TSB */
#define TSB_S_SIZE      (TSB_S_ENTRIES << CTTE_SHIFT)
/** Start of small TSB entry area in tsb[] as an index */
#define TSB_S_START     0

/** Mask for large TSB index from a large PFN */
#define TSB_L_IDX_MASK  RMASK(TSB_L_IDX_WIDTH)
/** Number of entries in the large TSB */
#define TSB_L_ENTRIES   (1 << TSB_L_IDX_WIDTH)
/** Size in bytes of the large TSB */
#define TSB_L_SIZE      (TSB_L_ENTRIES << CTTE_SHIFT)
/** Start of large TSB entry area in tsb[] as an index */
#define TSB_L_START     TSB_S_ENTRIES

/** Mask for jumbo TSB index from a jumbo PFN */
#define TSB_J_IDX_MASK  RMASK(TSB_J_IDX_WIDTH)
/** Number of entries in the jumbo TSB */
#define TSB_J_ENTRIES   (1 << TSB_J_IDX_WIDTH)
/** Size in bytes of the jumbo TSB */
#define TSB_J_SIZE      (TSB_J_ENTRIES << CTTE_SHIFT)
/** Start of jumbo TSB entry area in tsb[] as an index */
#define TSB_J_START     (TSB_S_ENTRIES + TSB_L_ENTRIES)

/** Total number of entries in the TSB. */
#define TSB_ALL_ENTRIES  (TSB_S_ENTRIES + TSB_L_ENTRIES + TSB_J_ENTRIES)

// TLB type parameters to TSB miss or TLB access violation handlers, noting the
// relevant TLB.  The TLB type is a mix of the true type of the TLB and
// (for virtualization) how we used it.

#define TSB_D           0   /**< Data TLB */
#define TSB_I           1   /**< Main processor instruction TLB */

#define TSB_V           2   /**< Virtualization page table miss */
#define TSB_GUEST       3   /**< Virtualization guest illegal page table */

// Method for packing various flags into an int.  We do this because we want
// to pass a number of small values, plus a fault address, to the TLB miss
// handler, but we don't have enough free registers to pass them all as
// separate arguments.

// PL of the miss
#define TSB_MISS_PL_SHIFT       0  /**< Miss PL shift */
#define TSB_MISS_PL_WIDTH       2  /**< Miss PL width */
#define TSB_MISS_PL_RMASK       RMASK(TSB_MISS_PL_WIDTH)
                                   /**< Miss PL RJ mask */
#define TSB_MISS_PL_MASK        (TSB_MISS_PL_RMASK << TSB_MISS_PL_SHIFT)
                                   /**< Miss PL mask */
#define TSB_MISS_PL_TOP         (TSB_MISS_PL_SHIFT + TSB_MISS_PL_WIDTH - 1)
                                   /**< Miss PL top bit */

// Type of the miss (see TSB_xxx defines above)
#define TSB_MISS_TYPE_SHIFT     2  /**< Miss type shift */
#define TSB_MISS_TYPE_WIDTH     2  /**< Miss type width */
#define TSB_MISS_TYPE_RMASK     RMASK(TSB_MISS_TYPE_WIDTH)
                                   /**< Miss type RJ mask */
#define TSB_MISS_TYPE_MASK      (TSB_MISS_TYPE_RMASK << TSB_MISS_TYPE_SHIFT)
                                   /**< Miss type mask */
#define TSB_MISS_TYPE_TOP       (TSB_MISS_TYPE_SHIFT + TSB_MISS_TYPE_WIDTH - 1)
                                   /**< Miss type top bit */

// Miss reason (1 if write, 0 if read)
#define TSB_MISS_RSN_SHIFT      4  /**< Miss reason shift */
#define TSB_MISS_RSN_WIDTH      1  /**< Miss reason width */
#define TSB_MISS_RSN_RMASK      RMASK(TSB_MISS_RSN_WIDTH)
                                   /**< Miss reason RJ mask */
#define TSB_MISS_RSN_MASK       (TSB_MISS_RSN_RMASK << TSB_MISS_RSN_SHIFT)
                                   /**< Miss reason mask */
#define TSB_MISS_RSN_TOP        (TSB_MISS_RSN_SHIFT + TSB_MISS_RSN_WIDTH - 1)
                                   /**< Miss reason top bit */

/** Extract PL of miss from flags */
#define TSB_MISS_PL(arg)        ((arg >> TSB_MISS_PL_SHIFT) & \
                                 TSB_MISS_PL_RMASK)
/** Extract type of miss from flags */
#define TSB_MISS_TYPE(arg)      ((arg >> TSB_MISS_TYPE_SHIFT) & \
                                 TSB_MISS_TYPE_RMASK)
/** Extract reason for miss from flags */
#define TSB_MISS_RSN(arg)         ((arg >> TSB_MISS_RSN_SHIFT) & \
                                 TSB_MISS_RSN_RMASK)

#ifndef __ASSEMBLER__

#include "misc.h"

/** State passed to a TSB miss handler and then to tsb_downcall().
 *  tsb_downcall() fills in the vector and faultaddr elements if appropriate;
 *  when the handler returns to its caller, a downcall is performed.
 */
struct tsb_downcall_state
{
  /** Do client PL downcall even if virtualization context installed bit <11>;
      Fault in client critical section bit <10>; interrupt vector <9:2>;
      1 bit <1>; and miss reason bit <0>. */
  unsigned long vector;
  /** Address of the fault */
  VA faultaddr;
  /** Saved registers */
  struct saved_regs regs;
};


extern ctte_t tsb[];

void syscall_set_caching(uint32_t bitmask);
int pte2aar(HV_PTE pte, uint_reg_t* aar_p, int uc_ok);
int mmio_access_ok(pos_t shimaddr, PA start, PA len);
int do_install_context(CPA paddr, HV_PTE access, Asid asid, uint32_t flags);
HV_Context syscall_inquire_context(void);
void tsb_miss(VA fault_addr, uint32_t flags, VA inst_addr,
              struct tsb_downcall_state* state);
void tsb_miss_hv(VA fault_addr, uint32_t flags, VA inst_addr,
                 struct tsb_downcall_state* state);
void tsb_miss_shared(VA fault_addr, uint32_t flags, VA inst_addr,
                     struct tsb_downcall_state* state);
void tsb_access(VA fault_addr, uint32_t flags, VA inst_addr,
                struct tsb_downcall_state* state);

int syscall_flush_asid(Asid asid);
int syscall_flush_page(VA va, unsigned long page_size);
int syscall_flush_pages(VA va, unsigned long page_size, unsigned long size);
int syscall_flush_all(int preserve_global);
int syscall_flush_remote(HV_PhysAddr cache_pa, unsigned long cache_control,
                         unsigned long* cache_cpumask,
                         HV_VirtAddr tlb_va, unsigned long tlb_length,
                         unsigned long pgsize,
                         unsigned long* tlb_cpumask,
                         HV_Remote_ASID* asids, int asidcount);

int handle_flush_remote(PA cache_pa, unsigned long cache_control,
                        VA tlb_va, unsigned long tlb_len,
                        Asid asid, int flush_tlb, int large_page,
                        int flush_cache, int flush_asid);

void raw_tsb_downcall(struct tsb_downcall_state* state, int vector,
                      VA fault_addr);
void tsb_downcall(struct tsb_downcall_state* state, int tlb_type, int flags,
                  VA fault_addr, uint32_t miss_reason);
//
// Flags passed to tsb_downcall().
//
#define TSB_DOWNCALL_ACCVIO   0x1  /**< Miss was an access violation */
#define TSB_DOWNCALL_VIRT     0x2  /**< Notify host client rather than guest */

void tsb_fatal(struct tsb_downcall_state* state, int tlb_type, int is_accvio,
               VA fault_addr, uint32_t miss_reason);

void init_tsb(void);
void dump_tsb(int dump_invalid);
void enable_fake_physmem_mode(void);
int inv_whole_l2(void);
int have_virt_pt(void);
void text_set_writable(int writable);

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_TSB_H */
