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
 * Routines to do TLB operations.
 */

#ifndef _SYS_HV_TLB_H
#define _SYS_HV_TLB_H

#include "types.h"
#include "tte.h"

/** Saved DTLB state. */
struct dtlb_state {
  tte_t tte[CHIP_DTLB_ENTRIES()];  /**< Individual TLB entries. */
};

/** Saved ITLB state. */
struct itlb_state {
  tte_t tte[CHIP_ITLB_ENTRIES()];  /**< Individual TLB entries. */
};

int search_dtlb_and_set_writable(VA va, Asid asid);
void dump_tte(tte_t tte, int index, int dump_bits);
void save_dtlb(struct dtlb_state*);
void dump_saved_dtlb(const struct dtlb_state*, char* title);
void save_itlb(struct itlb_state*);
void dump_saved_itlb(const struct itlb_state*, char* title);
void clean_itlb(int clean_wired);
void clean_dtlb(int clean_wired);
int install_wired_tte(tte_w0_t attr, VA va, PA pa);
int install_wired_mapping(VA va, PA pa, int ps, int mode, Lotar lotar);
int remove_wired_tte(void);
int remove_wired_tte_va(VA va);

static inline void dump_dtlb(char* title)
{
  struct dtlb_state state;
  save_dtlb(&state);
  dump_saved_dtlb(&state, title);
}

static inline void dump_itlb(char* title)
{
  struct itlb_state state;
  save_itlb(&state);
  dump_saved_itlb(&state, title);
}

/**
 * Set a TLB's index register, loading data into the xxx_CURRENT registers.
 *
 * FIXME: This works if the drain is replaced with a nop, but the hardware
 * documentation doesn't tell you what's architecturally required; once it
 * does this may change.
 */
#define LOAD_TLB(tlb, index_val) \
  do \
  { \
    SPR_ ## tlb ## TLB_INDEX_t reg = {{ .index = index_val, .r = 1 }}; \
    __insn_mtspr(SPR_ ## tlb ## TLB_INDEX, reg.word); \
    asm("drain"); \
  } while (0)

/**
 * Prepare to update TLBs when we need to do it with interrupts disabled.
 * We return the old ICS value.
 */
#define UPDATE_TLB_START() \
  ({ \
    long __ics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION); \
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1); \
    __ics; \
  })

/**
 * Finish updating TLBs.
 * Put the ICS value back the way it was before.
 */
#define UPDATE_TLB_FINISH(ics) \
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, ics)

/** Write the currently indexed TLB entry. */
#define WRITE_TLB(tlb, tte) \
  do \
  { \
    __insn_mtspr(SPR_ ## tlb ## TLB_CURRENT_VA, (tte).w1.word); \
    __insn_mtspr(SPR_ ## tlb ## TLB_CURRENT_PA, (tte).w2.word); \
    __insn_mtspr(SPR_ ## tlb ## TLB_CURRENT_ATTR, (tte).w0.word); \
  } while (0)

/** Read the currently indexed TLB entry. */
#define READ_TLB(tlb) \
  ({ \
    tte_t __tte; \
    __tte.w1.word = __insn_mfspr(SPR_ ## tlb ## TLB_CURRENT_VA); \
    __tte.w2.word = __insn_mfspr(SPR_ ## tlb ## TLB_CURRENT_PA); \
    __tte.w0.word = __insn_mfspr(SPR_ ## tlb ## TLB_CURRENT_ATTR); \
    __tte; \
  })

/** Read a TLB entry from a specific index. */
#define READ_TLB_AT(tlb, index_val) \
  ({ \
    LOAD_TLB(tlb, index_val); \
    READ_TLB(tlb); \
  })

/** Write a TLB entry to a specific index. */
#define WRITE_TLB_AT(tlb, index_val, tte) \
  do \
  { \
    __insn_mtspr(SPR_ ## tlb ## TLB_INDEX, index_val); \
    WRITE_TLB(tlb, tte); \
  } while (0)

#endif /* _SYS_HV_TLB_H */
