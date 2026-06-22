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
 * symmetric mica driver.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board_info.h"
#include "hv.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hw_config.h"
#include "lock.h"
#include "mica.h"
#include "msg.h"
#include "pka_fw.h"

#include "mica_rpc_dispatch.h"
#include "pka_rpc_dispatch.h"

#include <arch/mica_def.h>
#include <arch/mica_comp_def.h>
#include <arch/mica_crypto_def.h>
#include <arch/mica_comp_ctx_sys.h>
#include <arch/mica_comp_ctx_sys_def.h>
#include <arch/mica_comp_eng_defl.h>
#include <arch/mica_comp_eng_defl_def.h>
#include <arch/mica_comp_eng_infl.h>
#include <arch/mica_comp_eng_infl_def.h>
#include <arch/mica_crypto_eng.h>
#include <arch/mica_crypto_eng_def.h>
#include <arch/rsh_def.h>
#include <arch/sim.h>

#include <hv/drv_mica_intf.h>
#include <hv/drv_pka_intf.h>

/** Macro to calculate register number of the first word of a context's TLB.
 */
#define MICA_CTX_2_TLB_TABLE__FIRST_WORD(context)                       \
  ((MICA_ADDRESS_SPACE_CTX_SYS__PARTITION_VAL_CONTEXT_SYSTEM <<         \
    MICA_ADDRESS_SPACE_CTX_SYS__PARTITION_SHIFT) |                      \
   (((context) & MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_RMASK) <<          \
    MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_SHIFT) | MICA_TLB_TABLE__FIRST_WORD)

/** Macro to calculate register number of the last word of a context's TLB.
 */
#define MICA_CTX_2_TLB_TABLE__LAST_WORD(context)                        \
  ((MICA_ADDRESS_SPACE_CTX_SYS__PARTITION_VAL_CONTEXT_SYSTEM <<         \
    MICA_ADDRESS_SPACE_CTX_SYS__PARTITION_SHIFT) |                      \
   (((context) & MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_RMASK) <<          \
    MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_SHIFT) | MICA_TLB_TABLE__LAST_WORD)

/** Macro to calculate the address of the ADDR portion of the first entry
 * of a context's TLB.
 */
#define MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(c)        \
  (MICA_CTX_2_TLB_TABLE__FIRST_WORD(c))

/** Macro to calculate the address of the ATTR portion of the first entry
 * of a context's TLB.
 */
#define MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(c)        \
  (MICA_CTX_2_TLB_TABLE__FIRST_WORD(c) + 8)

/** Macro to calculate a MiCA engine register address */
#define MICA_ENG_REG_ADDR(eng, reg)                                     \
  ((MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL <<                  \
    MICA_ADDRESS_SPACE__PARTITION_SHIFT) |                              \
   ((eng) << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT) | (reg))

/** The base address of the PKA Window RAM in the shim's MMIO space. */
static const uint64_t pka_window_base_addr =
  (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL << 
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
  (0x14ull << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);

/** The base address of the Tilera PKA control registers in the shim's MMIO
 * space.
 */
static const uint64_t pka_tilera_ctl_base_addr = 
  (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL << 
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
  (0x14ull << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT) |
  0x10000;

/** The base address of the PKA engine (EIP154) control registers in the shim's
 * MMIO space.
 */
static const uint64_t pka_ctl_base_addr = 
  (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL << 
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
  (0x10ull << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);

/** The base addresses of the control registers for the two EIP96s
 * (engines 2 and 3) in the shim's MMIO space.
 */
static const uint64_t pp_ctl_base_addr[2] = {
  (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL << 
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
  (0x2ull << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT),

  (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL << 
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
  (0x3ull << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT)
};

/** Four engines' worth of address space is used for PKA control registers. */
static const uint32_t pka_ctl_len =
  4 << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT;

/** 64kB of space for PKA window RAM. */
static const uint32_t pka_window_len = 64 * 1024;

/** Tracing infrastructure for debug. */
#if 0
#define TRACE(...) tprintf("mica: " __VA_ARGS__)
#else
#define TRACE(...)
#endif

/** The largest RPC buffer we're willing to put on the stack. */
#define MAX_STACK_BYTES 4096

/** Identifier for PKA device handle, that will never overlap with a
 * context number.  
 */
#define MICA_PKA_DRIVER_DEVHDL 0x10000

/** Identifier for PKA TRNG device handle, that will never overlap with a
 * context number.  
 */
#define MICA_PKA_TRNG_DRIVER_DEVHDL 0x10001

/** Lock used to make sure that only one tile allocates shared state. */
static spinlock_t mica_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state object. */
mica_state_t* mica_state[MAX_MICA_COMPS + MAX_MICA_CRYPTOS] _SHARED = { 0 };

/** Helper function for clearing all of the IOTLB entries in a context. */
static void
mica_context_clear_iotlbs(mica_state_t* ms, int context_num)
{
  for (int entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    TRACE("MICA driver clearing tlb entry %d for context %d\n",
          entry, context_num);

    MICA_TLB_TABLE_t table = {{ .entry = entry }};
    unsigned long mmio_addr =
      MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num) + table.word;
    cfg_wr(ms->shim_pos.word, 0, mmio_addr, 0);

    mmio_addr = MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) +
      table.word;
    cfg_wr(ms->shim_pos.word, 0, mmio_addr, 0);
  }

  ms->iotlb_entries_used[context_num] = 0;
}

/** Helper function for reserving a context on a mica shim. */
static int
mica_reserve_context(mica_state_t* ms)
{
  unsigned long bitmask = ms->reserved_contexts_bitmask;
  int context_num;
  for (context_num = 0; context_num < HV_MICA_NUM_CONTEXTS; context_num++)
  {
    if ((bitmask & (1ULL << context_num)) == 0)
      break;
  }
  if (context_num >= HV_MICA_NUM_CONTEXTS)
    return GXIO_MICA_ERR_NO_CONTEXT;

  ms->reserved_contexts_bitmask |= (1ULL << context_num);

  TRACE("reserving context number %d\n", context_num);

  return context_num;
}

/** Helper function for unreserving a context on a mica shim. */
static int
mica_unreserve_context(mica_state_t* ms, int context_num)
{
  if (context_num >= HV_MICA_NUM_CONTEXTS)
    return GXIO_MICA_ERR_NO_CONTEXT;

  ms->reserved_contexts_bitmask &= ~(1ULL << context_num);

  mica_context_clear_iotlbs(ms, context_num);

  TRACE("unreserving context number %d\n", context_num);

  return 0;
}

/** Is a particular context already in use? */
static int
mica_context_is_open(mica_state_t* ms, unsigned int context_num)
{
  return context_num < HV_MICA_NUM_CONTEXTS &&
    (ms->reserved_contexts_bitmask & (1ULL << context_num));
}

/** Handler for registering a page of user memory in the IOTLB for a given
 * hardware context.
 */
int
handle_gxio_mica_register_page_aux(mica_state_t* ms, int context_num,
                                   PA page_pa, size_t page_size,
                                   struct iorpc_mem_attr page_attr,
                                   uint64_t vpn)
{
  // Verify page size.
  int log2_page_size = __builtin_ctzl(page_size);
  if (log2_page_size < 12 || log2_page_size > CHIP_PA_WIDTH())
    return GXIO_ERR_IOTLB_ENTRY;

  // Verify there is an IOTLB entry available.
  if (ms->iotlb_entries_used[context_num] >= ms->num_iotlb_entries)
    return GXIO_ERR_IOTLB_ENTRY;

  // Find first available entry for this page.
  int entry;
  for (entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    MICA_TLB_TABLE_t table = {{ .entry = entry }};

    MICA_TLB_ENTRY_ATTR_t attr = {
      .word =
      cfg_rd(ms->shim_pos.word, 0,
             MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) +
             table.word)
    };

    if (attr.vld == 0)
      break;
  }

  TRACE("context number %d using entry %d\n", context_num, entry);

  MICA_TLB_TABLE_t table = {{ .entry = entry }};

  MICA_TLB_ENTRY_ADDR_t addr = {{
      .pfn = page_pa >> 12,
      .vpn = vpn,
    }};

  unsigned long mmio_addr =
    MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num) + table.word;

  TRACE("MICA driver adding tlb entry %lx to addr %lx for context %d\n",
        (unsigned long)addr.word, mmio_addr, context_num);

  cfg_wr(ms->shim_pos.word, 0, mmio_addr, addr.word);

  MICA_TLB_ENTRY_ATTR_t attr = {{
      .vld = 1,
      .ps = log2_page_size - 12,
      .home_mapping = !page_attr.hfh,
      .pin = page_attr.io_pin,
      .nt_hint = page_attr.nt_hint,
      .loc_y_or_offset = page_attr.lotar_y,
      .loc_x_or_mask = page_attr.lotar_x,
      //.lru = UNUSED
    }};
  mmio_addr = MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) + table.word;

  cfg_wr(ms->shim_pos.word, 0, mmio_addr, attr.word);
  TRACE("MICA driver adding tlb entry %lx to addr %lx for context %d\n",
        (unsigned long)attr.word, mmio_addr, context_num);
  TRACE(".home_mapping = %d, .pin = %d, .nt_hint = %d, .loc_y_or_offset = %d, "
        ".loc_x_or_mask = %d\n",
        attr.home_mapping, attr.pin, attr.nt_hint, attr.loc_y_or_offset,
        attr.loc_x_or_mask);

  ms->iotlb_entries_used[context_num]++;

  return 0;
}


/** Handler for unregistering a page of user memory in the IOTLB for a given
 * hardware context.
 */
int
handle_gxio_mica_unregister_page_aux(mica_state_t* ms, int context_num,
                                     PA page_pa)
{
  // Find the IOTLB entry which contains the target page.
  for (int entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    MICA_TLB_TABLE_t table = {{ .entry = entry }};
    MICA_TLB_ENTRY_ADDR_t addr = {
      .word = cfg_rd(ms->shim_pos.word, 0,
                     MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num) +
                     table.word)
    };

    // Find the registered IOTLB entry.
    if (addr.pfn == (page_pa >> 12))
    {
      // First clear MICA_TLB_ENTRY_ATTR, to invalidate the entry.
      // And leave the MICA_TLB_ENTRY_ADDR intact.
      MICA_TLB_ENTRY_ATTR_t attr;
      attr.word = 0;
      cfg_wr(ms->shim_pos.word, 0,
             MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) + table.word,
             attr.word);

      ms->iotlb_entries_used[context_num]--;

      return 0;
    }
  }

  // No IOTLB entry found.
  return GXIO_ERR_IOTLB_ENTRY;
}

/** Return the base PTE that the client should use to access our
 * shim's MMIO registers.
 */
int
handle_gxio_mica_get_mmio_base(mica_state_t* ms, int context_num, HV_PTE *base)
{
  PA pa = HV_MICA_CONTEXT_USER_MMIO_OFFSET(context_num);

  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(ms->shim_pos.bits.x,
                                             ms->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;
  TRACE("MMIO Base of shim at %d,%d, pa = 0x%llx\n",
        ms->shim_pos.bits.x, ms->shim_pos.bits.y, pa);

  return 0;
}

static int contained_by(unsigned long bound_offset,
                        unsigned long bound_size,
                        unsigned long input_offset,
                        unsigned long input_size)
{
  if (input_offset < bound_offset ||
      input_offset + input_size > bound_offset + bound_size ||
      input_offset + input_size < input_offset)
    return 0;

  return 1;
}

/** Check whether an MMIO range is legal. */
int
handle_gxio_mica_check_mmio_offset(mica_state_t* ms, int context_num,
                                   unsigned long offset, unsigned long size)
{
  if (contained_by(0, HV_MICA_CONTEXT_USER_MMIO_SIZE, offset, size))
    return 0;

  TRACE("check_mmio_offset() failed: bound_offset = 0x%x, bound_size = 0x%lx,"
        " offset = 0x%lx, size = 0x%lx\n",
        0, (unsigned long)HV_MICA_CONTEXT_USER_MMIO_SIZE, offset, size);
        
  return GXIO_ERR_MMIO_ADDRESS;
}


/** Handle the hv work for configuring a completion interrupt. */
int
handle_gxio_mica_cfg_completion_interrupt(mica_state_t* ms,
                                          int context_num,
                                          int x, int y,
                                          int ipi, int event)
{
  // Do the mmio access here to set up the bindings.
  MICA_COMP_INT_t comp = {{
      .event_num = event,
      .int_num = ipi,
      .y_coord = y,
      .x_coord = x,
    }};

  unsigned long mmio_addr = 
    (MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_SYSTEM <<
     MICA_ADDRESS_SPACE__PARTITION_SHIFT) |
    (context_num << 
     MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_SHIFT);

  cfg_wr(ms->shim_pos.word, 0, mmio_addr | MICA_COMP_INT, comp.word);

  return 0;
}

/* Handle request to register all of a client's memory with a 
   particular context's IOTLB. */
int
handle_gxio_mica_register_client_memory(mica_state_t* ms, int context_num,
                                        unsigned int iotlb,
                                        HV_PTE pte, unsigned int flags)
{
  // Verify all IOTLB entries are available.
  if (ms->iotlb_entries_used[context_num] != 0)
    return GXIO_ERR_IOTLB_ENTRY;

  int err =
    drv_map_cpa_space_to_iotlb(ms->shim_pos, 0, pte,
                            MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num),
                               flags);
  if (err != 0)
    return err;

  ms->iotlb_entries_used[context_num] = ms->num_iotlb_entries - 1;

  return 0;
}

int
handle_gxio_mica_pka_cfg_cmd_queue_empty_interrupt(mica_state_t* ms,
                                                   int intr_x, int intr_y,
                                                   int intr_ipi,
                                                   int intr_event, int ring)
{
  MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_0_EMPTY_t interrupt = {{
      .event_num = intr_event,
      .int_num = intr_ipi,
      .y_coord = intr_y,
      .x_coord = intr_x,
    }};

  if ((ring < 0) || (ring > 3))
  {
    TRACE("invalid ring number %d\n", ring);
    return -1;
  }

  // Unmask (enable) the command queue low water mark interrupt for this ring.
  cfg_wr(ms->shim_pos.word, 0,
         pka_tilera_ctl_base_addr + MICA_CRYPTO_ENG_PKA_INT_MASK_RESET,
         1 << ring);


  // We want an interrupt if the command queue is empty or
  // if there is at least one result.
  // FIXME: parameterize the high and low water marks.  Then we will have
  // to rmw the registers.
  int irq_thresh_val = 0x00010000;

  // We could do some arithmetic on the register number but the
  // address space is wacky enough to make that slightly unnatural
  switch (ring)
  {
  case 0:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_0, irq_thresh_val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_0_EMPTY,
           interrupt.word);
    break;
  case 1:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_1, irq_thresh_val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_1_EMPTY,
           interrupt.word);
    break;
  case 2:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_2, irq_thresh_val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_2_EMPTY,
           interrupt.word);
    break;
  case 3:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_3, irq_thresh_val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_3_EMPTY,
           interrupt.word);
    break;
  }

  return 0;
}

int
handle_gxio_mica_pka_cfg_res_queue_full_interrupt(mica_state_t* ms,
                                                  int intr_x, int intr_y,
                                                  int intr_ipi, int intr_event,
                                                  int ring)
{
  MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_0_RESULT_t interrupt = {{
      .event_num = intr_event,
      .int_num = intr_ipi,
      .y_coord = intr_y,
      .x_coord = intr_x,
    }};

  if ((ring < 0) || (ring > 3))
  {
    TRACE("invalid ring number %d\n", ring);
    return -1;
  }

  // Unmask (enable) the result queue high water mark interrupt for this ring.
  cfg_wr(ms->shim_pos.word, 0,
         pka_tilera_ctl_base_addr + MICA_CRYPTO_ENG_PKA_INT_MASK_RESET,
         (1 << ring) << 4);

  // Give an interrupt if the command queue totally empty or
  // if there is at least one result.
  // FIXME: parameterize the high and low water marks.  Then we will have
  // to rmw the registers.
  int val = 0x00010000;

  switch (ring)
  {
  case 0:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_0, val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_0_RESULT,
           interrupt.word);
    break;
  case 1:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_1, val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_1_RESULT,
           interrupt.word);
    break;
  case 2:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_2, val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_2_RESULT,
           interrupt.word);
    break;
  case 3:
    cfg_wr(ms->shim_pos.word, 0,
           pka_ctl_base_addr + MICA_CRYPTO_ENG_IRQ_THRESH_3, val);
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr +
           MICA_CRYPTO_ENG_INT_BINDING_PKA_QUEUE_3_RESULT,
           interrupt.word);
    break;
  }

  return 0;
}

int
handle_gxio_mica_pka_get_mmio_base(mica_state_t* ms, HV_PTE *base)
{
  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(ms->shim_pos.bits.x,
                                             ms->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pka_ctl_base_addr);

  *base = pte;
  TRACE("PKA MMIO Base of shim at %d,%d, pa = 0x%llx\n",
        ms->shim_pos.bits.x, ms->shim_pos.bits.y, pka_ctl_base_addr);

  return 0;
}

int
handle_gxio_mica_pka_check_mmio_offset(mica_state_t* ms,
                                       unsigned long offset,
                                       unsigned long size)
{
  uint64_t bound_size = pka_ctl_base_addr + pka_ctl_len + pka_window_len;

  if (contained_by(0, bound_size, offset, size))
    return 0;

  TRACE("pka_check_mmio_offset() failed: bound_offset = 0x%x, "
        " bound_size = 0x%lx, offset = 0x%lx, size = 0x%lx\n",
        0, (unsigned long)bound_size, offset, size);
        
  return GXIO_ERR_MMIO_ADDRESS;
}

static void
load_pka_firmware(uint32_t shim_addr)
{
  TRACE("---------------------Begin setup-------------------------------\n");

  TRACE("shim_pos = 0x%x\n", shim_addr);
  TRACE("pka_ctl_base_addr = %p\n", (void*)pka_ctl_base_addr);

  TRACE("Write AIC registers\n");
  // Setup AIC registers in PKA
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_POL_CTRL, 0x1ff);
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_TYPE_CTRL, 0x10f);
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_ENABLE_CTRL, 0x1ff);
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_ENABLED_STAT, 0x1ff);

  int buffer_ram_data_size = sizeof(buffer_ram_data) / sizeof(uint32_t);
  TRACE("Loading BUFFER_RAM size = %d base_addr = %p\n",
        buffer_ram_data_size, (void*)pka_ctl_base_addr);
  for (int i = 0; i < buffer_ram_data_size; i++)
  {
    cfg_wr(shim_addr, 0,
           pka_ctl_base_addr | (MICA_CRYPTO_ENG_PKA_BUFFER_RAM + i * 8),
           (uint64_t)buffer_ram_data[i]);
  }

  TRACE("Verifying BUFFER_RAM\n");
  int mismatches = 0;
  for (int i = 0; i < buffer_ram_data_size; i++)
  {
    uint64_t rd = cfg_rd(shim_addr, 0,
                         pka_ctl_base_addr |
                         (MICA_CRYPTO_ENG_PKA_BUFFER_RAM + i * 8));
    if (rd != (uint64_t)buffer_ram_data[i])
    {
      mismatches++;
      TRACE("Error: addr: 0x%llx exp data: 0x%x act data: 0x%llx\n",
            pka_ctl_base_addr + MICA_CRYPTO_ENG_PKA_BUFFER_RAM + i * 8,
            buffer_ram_data[i], rd);
    }
    if (mismatches > 0)
    {
      TRACE("....mismatches: %d\n", mismatches);
      return;
    }
  }

  TRACE("Write MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL RESET to 1\n");
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL, 0x80000000);

  int master_prog_ram_boot_data_size = sizeof(master_prog_ram_data_boot) / 
    sizeof(uint32_t);
  TRACE("Loading MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM BOOT %d\n",
        master_prog_ram_boot_data_size);
  for (int i = 0; i < master_prog_ram_boot_data_size; i++)
  {
    cfg_wr(shim_addr, 0, pka_ctl_base_addr |
           (MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8),
           (uint64_t)master_prog_ram_data_boot[i]);
  }

  TRACE("Verifying MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM BOOT\n");
  mismatches = 0;
  for (int i = 0; i < master_prog_ram_boot_data_size; i++)
  {
    uint64_t rd = cfg_rd(shim_addr, 0,
                         pka_ctl_base_addr |
                         (MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8));
    if (rd != (uint64_t)master_prog_ram_data_boot[i])
    {
      mismatches++;
      TRACE("Error: addr: 0x%llx  exp data: 0x%x  act data: 0x%llx \n",
            pka_ctl_base_addr + MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8,
            master_prog_ram_data_boot[i], rd);
    }
    if (mismatches > 0)
    {
      TRACE("....mismatches: %d\n", mismatches);
      return;
    }
  }

  TRACE("Write MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL RESET to 0\n");
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL, 0);

  // Poll for pka_master_irq bit in AIC_ENABLED_STAT register to indicate
  // sequencer is initialized
  int pka_master_irq = 0;
  TRACE("Poll for MASTER_IRQ...\n");

  uint64_t timer = drv_timer_start(100000);       // 100 msec
  while (pka_master_irq == 0)
  {
    pka_master_irq =
      cfg_rd(shim_addr, 0,
             pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_ENABLED_STAT) & 0x100;

    if (drv_timer_done(timer))
    {
      panic("PKA firmware failed to load\n");
    }
  }
  TRACE("...MASTER_IRQ was set\n");

  TRACE("Write AIC_ENABLED_STAT to clear MASTER_IRQ\n");
  cfg_rd(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_ENABLED_STAT);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_AIC_ENABLED_STAT,
         0x00000100);
  TRACE("Write MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL RESET TO 1\n");
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL,
         0x80000000);

  int master_prog_ram_execute_data_size =
    sizeof(master_prog_ram_data_execute) / sizeof(uint32_t);
  TRACE("Loading MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM EXECUTE %d\n",
        master_prog_ram_execute_data_size);
  for (int i = 0; i < master_prog_ram_execute_data_size; i++)
  {
    cfg_wr(shim_addr, 0, pka_ctl_base_addr |
           (MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8),
           (uint64_t)master_prog_ram_data_execute[i]);
  }

  TRACE("Verifying MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM EXECUTE\n");
  mismatches = 0;
  for (int i = 0; i < master_prog_ram_execute_data_size; i++)
  {
    uint64_t rd = cfg_rd(shim_addr, 0,
                         pka_ctl_base_addr |
                         (MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8));
    if (rd != (uint64_t)master_prog_ram_data_execute[i])
    {
      mismatches++;
      TRACE("Error: addr: 0x%llx exp data: 0x%x act data: 0x%llx\n",
             pka_ctl_base_addr + MICA_CRYPTO_ENG_PKA_MASTER_PROG_RAM + i * 8,
             master_prog_ram_data_execute[i], rd);
    }
    if (mismatches > 0)
    {
      panic("PKA firmware verification failed: %d mismatches", mismatches);
    }
  }
}

static int
init_trng(uint32_t shim_addr)
{
  uint64_t rd;

  TRACE(" ----------- Begin TRNG setup --------------");

  // turn on debug mode
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_MODE_SELECTION, 0x4c00);
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_MODE_SELECTION, 0x4800);
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_MODE_SELECTION, 0x4400);
  rd = cfg_rd(shim_addr, 0,
              pka_ctl_base_addr | MICA_CRYPTO_ENG_MODE_SELECTION);
  if (((rd >> 14) & 1) == 0)
  {
    TRACE("Error - MODE_SELECTION debug bit is not set");
    return -1;
  }

  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_CLK_FORCE,
         0x00000800);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_CONTROL,
         0x00000000);
  // Disable all FROs initially
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_FROENABLE,
         0x00000000);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_FRODETUNE,
         0x00000000);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_FROENABLE,
         0x00ffffff);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_ALARMMASK,
         0x00000000);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_ALARMSTOP,
         0x00000000);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_ALARMCNT,
         0x000200ff);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_INTACK,
         0x000000ff);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_BLOCKCNT,
         0x00000000);
  // Make sample_div field 3 (4 cycles)
  //  crypto clock 800MHz and slowest FRO 400MHz
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_CONFIG,
         0x00020301);
  //  Setup the key and V registers (TRNG_PS_AI_x registers for
  //  SP 800-90 configuration):
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_0,
         0x11111111);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_1,
         0x22222222);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_2,
         0x33333333);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_3,
         0x44444444);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_4,
         0x55555555);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_5,
         0x66666666);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_6,
         0x77777777);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_PKA_KDK_1_7,
         0x88888888);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_V_0,
         0xaaaaaaaa);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_V_1,
         0xbbbbbbbb);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_V_2,
         0xcccccccc);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_V_3,
         0xdddddddd);
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_CONTROL,
         0x00031401);
  TRACE("Poll trng_status blocks_available");
  uint64_t blocks = 0;
  while (blocks == 0)
  {
    rd = cfg_rd(shim_addr, 0,
                pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_STATUS);
    blocks = (rd >> 16) & 0xff;
  }
  TRACE(" ... done");
  cfg_wr(shim_addr, 0, pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_INTACK,
         0x00000001);
  TRACE(" ----------- End of TRNG setup -------------");

  return 0;
}

static uint32_t
get_rand32(uint32_t shim_addr)
{
  // Wait until there is a random number available.  The only way this
  // would hang is if the TRNG never initialized, and we would not call this
  // function if that happened.
  int max_tries = 10000;
  do {
    uint64_t reg = cfg_rd(shim_addr, 0,
                          pka_ctl_base_addr  | MICA_CRYPTO_ENG_TRNG_STATUS);
    if (reg & 1) break;
  } while (--max_tries);

  if (max_tries == 0)
  {
    pos_t shim_pos = {.word = shim_addr };
    printf("hv_warning: mica at (%d, %d) got error obtaining random number\n",
           shim_pos.bits.x, shim_pos.bits.y);
  }

  // Read the data
  uint32_t data = cfg_rd(shim_addr, 0,
                 pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_OUTPUT_0);

  // Tell the hardware to advance
  cfg_wr(shim_addr, 0,
         pka_ctl_base_addr | MICA_CRYPTO_ENG_TRNG_INTACK, 1);

  return data;
}

static void
init_prng(uint32_t shim_addr, uint64_t pp_eng_ctl_base_addr)
{
  // Initialize the following with random numbers.  The seed should be
  // random, the DES key and LFSR registers have to be non-zero.  We're
  // using random numbers for those because if you were choosing values for
  // these from a software app you'd use random numbers.
  // PRNG Seed Low
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x48 << 3),
         get_rand32(shim_addr));

  // PRNG Seed High
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x4c << 3),
         get_rand32(shim_addr));

  // PRNG Key0 Low
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x50 << 3),
         get_rand32(shim_addr));

  // PRNG Key0 High
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x54 << 3),
         get_rand32(shim_addr));

  // PRNG Key1 Low
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x58 << 3),
         get_rand32(shim_addr));

  // PRNG Key1 High
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x5c << 3),
         get_rand32(shim_addr));

  // PRNG LFSR Low
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x70 << 3),
         get_rand32(shim_addr));

  // PRNG LFSR High
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x74 << 3),
         get_rand32(shim_addr));

  // Put the PRNG into "Auto"  mode, so that the engine itself can use it,
  // and enable the automatic generation of PRNs.
  cfg_wr(shim_addr, 0, pp_eng_ctl_base_addr | (0x44 << 3), 3);
}


// Shim Reset Code

// These are the lists of MiCA register offsets.  Commented-out registers are
// either read-only or have unwanted side effects when written.  They are listed
// here for explictness.

static uint64_t global_regs[] = {
  MICA_CLOCK_CONTROL,
  // MICA_CLOCK_COUNT,
  MICA_DEV_CTL,
  // MICA_DEV_INFO,
  MICA_EGRESS_CREDIT,
  // MICA_ENGINE_DISABLE,
  // MICA_ENGINE_RESET,
  MICA_HFH_INIT_CTL,
  MICA_HFH_INIT_DAT,
  MICA_INT_MASK,
  // MICA_INT_MASK_RESET,
  // MICA_INT_MASK_SET,
  // MICA_IN_USE_CONTEXTS_0,
  // MICA_IN_USE_CONTEXTS_1,
  // MICA_IN_USE_CONTEXTS_2,
  // MICA_IN_USE_CONTEXTS_3,
  MICA_MEM_INFO,
  MICA_MMIO_INFO,
  MICA_SCHED_0_CTL,
  MICA_SCHED_1_CTL,
  MICA_SCHED_2_CTL,
  MICA_SCHED_3_CTL,
  MICA_SCHED_4_CTL,
  MICA_SCHED_5_CTL,
  MICA_SCHED_6_CTL,
  MICA_SCHED_7_CTL,
  MICA_SCRATCHPAD,
  MICA_SEMAPHORE0,
  MICA_SEMAPHORE1,
};

static uint64_t deflate_eng_regs[] = {
  MICA_COMP_ENG_DEFL_REG_CUST_CTL,
  MICA_COMP_ENG_DEFL_REG_DEF_CTL,
  MICA_COMP_ENG_DEFL_REG_DMA_SEL,
  MICA_COMP_ENG_DEFL_REG_INT_VEC,
  MICA_COMP_ENG_DEFL_REG_INT_VEC_BIND,
  MICA_COMP_ENG_DEFL_REG_INT_VEC_MASK,
  // MICA_COMP_ENG_DEFL_REG_INT_VEC_W1TC,
  MICA_COMP_ENG_DEFL_REG_MATCH_CTL,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_0,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_1,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_2,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_3,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_4,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_5,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_6,
  // MICA_COMP_ENG_DEFL_REG_PERF_CNT_7,
  MICA_COMP_ENG_DEFL_REG_PERF_CTL_0,
  MICA_COMP_ENG_DEFL_REG_PERF_CTL_1,
};

static uint64_t inflate_eng_regs[] = {
  MICA_COMP_ENG_INFL_REG_INF_CTL,
  // MICA_COMP_ENG_INFL_REG_INT_VEC,
  MICA_COMP_ENG_INFL_REG_INT_VEC_BIND,
  MICA_COMP_ENG_INFL_REG_INT_VEC_MASK,
  // MICA_COMP_ENG_INFL_REG_INT_VEC_W1TC,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_0,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_1,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_2,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_3,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_4,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_5,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_6,
  // MICA_COMP_ENG_INFL_REG_PERF_CNT_7,
  MICA_COMP_ENG_INFL_REG_PERF_CTL_0,
  MICA_COMP_ENG_INFL_REG_PERF_CTL_1,
};

static uint64_t context_sys_regs[] = {
  MICA_CONTROL,
  MICA_COMP_CTX_SYS_COMP_INT,
  MICA_COMP_CTX_SYS_CONTROL,
  MICA_COMP_CTX_SYS_INT_MASK,
  // MICA_COMP_CTX_SYS_INT_MASK_RESET,
  // MICA_COMP_CTX_SYS_INT_MASK_SET,
  // MICA_PROBE_STATUS,
  // MICA_PROBE_VA,
  // MICA_COMP_CTX_SYS_MISS_VA,
  // MICA_COMP_CTX_SYS_PROBE_STATUS,
  // MICA_COMP_CTX_SYS_PROBE_VA,
  // MICA_COMP_CTX_SYS_STATUS,
  MICA_COMP_CTX_SYS_TLB_MISS_INT,
};

static uint64_t context_user_regs[] = {
  MICA_SRC_DATA,
  MICA_DEST_DATA,
  MICA_EXTRA_DATA_PTR,
  // Writing to the OPCODE register starts an operation. It must be last in
  // this list, in order to be restored only after all of the other registers
  // have been restored.
  MICA_OPCODE,
};


#define MICA_NUM_GLOBAL_REGS (sizeof(global_regs) / sizeof(*global_regs))
#define MICA_NUM_DEFL_ENGINE_REGS (sizeof(deflate_eng_regs) /          \
                                   sizeof(*deflate_eng_regs))
#define MICA_NUM_INFL_ENGINE_REGS (sizeof(inflate_eng_regs) /          \
                                   sizeof(*inflate_eng_regs))
#define MICA_NUM_ENGINE_REGS (MICA_NUM_DEFL_ENGINE_REGS +               \
                              (2 * MICA_NUM_INFL_ENGINE_REGS))
#define MICA_NUM_CONTEXT_SYS_REGS ((sizeof(context_sys_regs) /          \
                                    sizeof(*context_sys_regs)) + (16 * 2))
#define MICA_NUM_CONTEXT_USER_REGS  (sizeof(context_user_regs) /        \
                                     sizeof(*context_user_regs))

#define MICA_SHIM_TOTAL_REGS (MICA_NUM_GLOBAL_REGS +                    \
                              MICA_NUM_ENGINE_REGS +                    \
                              (HV_MICA_NUM_CONTEXTS *                   \
                               (MICA_NUM_CONTEXT_SYS_REGS +             \
                                MICA_NUM_CONTEXT_USER_REGS)))

static uint64_t*
mica_context_save_iotlbs(mica_state_t* ms, uint64_t* buf, int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;

  for (int entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    MICA_TLB_TABLE_t table = {{ .entry = entry }};

    MICA_TLB_ENTRY_ATTR_t attr = {
      .word = cfg_rd(shim_addr, 0,
                     MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) +
                     table.word)
    };

    if (attr.vld)
    {
      *buf++ = cfg_rd(shim_addr, 0,
                      MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num) +
                      table.word);
      *buf++ = attr.word;
    }
  }
  return buf;
}

static uint64_t*
mica_context_restore_iotlbs(mica_state_t* ms, uint64_t* buf, int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;

  for (int entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    MICA_TLB_TABLE_t table = {{ .entry = entry }};
    unsigned long mmio_addr =
      MICA_CTX_2_TLB_ENTRY_ADDR__FIRST_WORD(context_num) + table.word;
    cfg_wr(shim_addr, 0, mmio_addr, *buf++);

    mmio_addr = MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) +
      table.word;
    cfg_wr(shim_addr, 0, mmio_addr, *buf++);
  }
  return buf;
}


static void
mica_context_validate_iotlbs(mica_state_t* ms, int context_num, int validate)
{
  // Invalidate, do not clear, the iotlbs.  We want to save/restore these
  // registers later.

  for (int entry = 0; entry < ms->num_iotlb_entries; entry++)
  {
    MICA_TLB_TABLE_t table = {{ .entry = entry }};
    unsigned long mmio_addr = 
      MICA_CTX_2_TLB_ENTRY_ATTR__FIRST_WORD(context_num) + table.word;

    MICA_TLB_ENTRY_ATTR_t attr = { .word = cfg_rd(ms->shim_pos.word, 0, mmio_addr) };
    attr.vld = validate ? 1 : 0;
    cfg_wr(ms->shim_pos.word, 0, mmio_addr, attr.word);
  }
}


static void
mica_validate_iotlbs(mica_state_t* ms, int validate)
{
  for (int i = 0; i < HV_MICA_NUM_CONTEXTS; i++)
    mica_context_validate_iotlbs(ms, i, validate);
}


#ifdef MICA_RESET_DEBUG
#define SAVE_REG(shimaddr, base, save, reg)                     \
  do                                                            \
  {                                                             \
    uint64_t val = cfg_rd(shimaddr, 0, (base) + (reg));         \
    printf("Saving 0x%lx for reg 0x%lx to addr 0x%lx\n",        \
           (long)val, (long)reg, (long)save);                   \
    *(save)++ = val;                                            \
  }                                                             \
  while (0)
#else
#define SAVE_REG(shimaddr, base, save, reg)             \
  *(save)++ = cfg_rd(shimaddr, 0, (base) + (reg))
#endif

#ifdef MICA_RESET_DEBUG
#define RESTORE_REG(shimaddr, base, save, reg)                  \
  do                                                            \
  {                                                             \
    uint64_t addr = (uint64_t)save;                             \
    uint64_t val = *((save)++);                                 \
    printf("Restoring 0x%lx for reg 0x%lx from addr 0x%lx\n",   \
           (long)val, (long)reg, (long)addr);                   \
    cfg_wr(shimaddr, 0, (base) + (reg), val);                   \
  }                                                             \
  while (0)
#else
#define RESTORE_REG(shimaddr, base, save, reg)          \
  cfg_wr(shimaddr, 0, (base) + (reg), *((save)++))
#endif


static uint64_t*
save_global_regs(uint64_t* save, mica_state_t* ms)
{
  uint32_t shim_addr = ms->shim_pos.word;

  uint64_t base = (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
                   MICA_ADDRESS_SPACE__PARTITION_SHIFT);

  int num_regs = sizeof(global_regs) / sizeof(*global_regs);

  for (int offset = 0; offset < num_regs; offset++)
    SAVE_REG(shim_addr, base, save, global_regs[offset]);

  return save;
}


static uint64_t*
save_engine_regs(uint64_t* save, mica_state_t* ms)
{


  uint32_t shim_addr = ms->shim_pos.word;
  uint64_t base = (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
                   MICA_ADDRESS_SPACE__PARTITION_SHIFT);

  //  We can't read and write registers of disabled engines, so first check to
  // see which engines are disabled and remember them for the restore.
  ms->reset.disabled_engine_mask = cfg_rd(shim_addr, 0,
                                          base | MICA_ENGINE_DISABLE);

  // deflate engine is 0, inflate engines are 1 and 2

  int eng_no = 0;
  int num_regs = sizeof(deflate_eng_regs) / sizeof(*deflate_eng_regs);

  if (((ms->reset.disabled_engine_mask >> eng_no) & 1) == 0)
  {
    base = (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL <<
            MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
      (eng_no << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);

    for (int offset = 0; offset < num_regs; offset++)
      SAVE_REG(shim_addr, base, save, deflate_eng_regs[offset]);
  }

  num_regs = sizeof(inflate_eng_regs) / sizeof(*inflate_eng_regs);
  for (eng_no = 1; eng_no <= 2; eng_no++)
  {
    if (((ms->reset.disabled_engine_mask >> eng_no) & 1) == 0)
    {
      base = (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL <<
              MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
        (eng_no << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);
    
      for (int offset = 0; offset < num_regs; offset++)
        SAVE_REG(shim_addr, base, save, inflate_eng_regs[offset]);
    }
  }

  return save;
}

static uint64_t*
save_context_sys_regs(uint64_t* save, mica_state_t* ms, int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;
  unsigned long base = (MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_SYSTEM <<
     MICA_ADDRESS_SPACE__PARTITION_SHIFT) |
    (context_num << 
     MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_SHIFT);

  int num_regs = sizeof(context_sys_regs) / sizeof(*context_sys_regs);

  for (int offset = 0; offset < num_regs; offset++)
    SAVE_REG(shim_addr, base, save, context_sys_regs[offset]);

  // Save the individual TLB entries
  save = mica_context_save_iotlbs(ms, save, context_num);

  // Figure out whether this context was pending or running.
  // If it was running we have to fail it.  If it was pending we
  // know to restart it on restore.  Since all cores are in the hypervisor,
  // we know that we do not have a race condition where someone might be
  // starting an operation while we're saving.
  MICA_COMP_CTX_SYS_STATUS_t status =
    { .word = cfg_rd(shim_addr, 0, base | MICA_COMP_CTX_SYS_STATUS) };
  ms->reset.contexts_pending_mask |=
    (status.state == MICA_COMP_CTX_SYS_STATUS__STATE_VAL_RUN_WAIT) <<
    context_num;
  ms->reset.contexts_in_progress_mask |=
    (status.state == MICA_COMP_CTX_SYS_STATUS__STATE_VAL_RUN) << context_num;

  return save;
}


static uint64_t*
save_context_user_regs(uint64_t* save, mica_state_t* ms, int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;
  unsigned long base = (MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_USER <<
     MICA_ADDRESS_SPACE__PARTITION_SHIFT) |
    (context_num << 
     MICA_ADDRESS_SPACE_CTX_USER__CONTEXT_SHIFT);  

  int num_regs = sizeof(context_user_regs) / sizeof(*context_user_regs);

  for (int offset = 0; offset < num_regs; offset++)
    SAVE_REG(shim_addr, base, save, context_user_regs[offset]);

  return save;
}


static void
save_shim_regs(uint64_t* buf, mica_state_t* ms)
{
  uint32_t shim_addr = ms->shim_pos.word;

  // All cores are in the hypervisor at this point, so no configuration
  // changes will be made to the engines.

  uint64_t base = (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
                   MICA_ADDRESS_SPACE__PARTITION_SHIFT);

  // Save engine registers.
  buf = save_engine_regs(buf, ms);

  // Disable all engines to stall all operations in progress.
  cfg_wr(shim_addr, 0, base | MICA_ENGINE_DISABLE, 0xf);

  buf = save_global_regs(buf, ms);

  for (int i = 0; i < HV_MICA_NUM_CONTEXTS; i++)
    buf = save_context_sys_regs(buf, ms, i);

  for (int i = 0; i < HV_MICA_NUM_CONTEXTS; i++)
    buf = save_context_user_regs(buf, ms, i);

  // Invalidate all of the iotlb entries so that we don't reset the shim
  // while there are memory operations in progress.
  mica_validate_iotlbs(ms, 0);
}


static uint64_t*
restore_global_regs(uint64_t* restore, mica_state_t* ms)
{
  uint32_t shim_addr = ms->shim_pos.word;
  uint64_t base = (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
                   MICA_ADDRESS_SPACE__PARTITION_SHIFT);

  int num_regs = sizeof(global_regs) / sizeof(*global_regs);
  
  for (int offset = 0; offset < num_regs; offset++)
    RESTORE_REG(shim_addr, base, restore, global_regs[offset]);  

  return restore;
}


static uint64_t*
restore_engine_regs(uint64_t* restore, mica_state_t* ms)
{
  uint32_t shim_addr = ms->shim_pos.word;
  uint64_t base = (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
                   MICA_ADDRESS_SPACE__PARTITION_SHIFT);

  // First, enable only the engines that were enabled at reset.
  cfg_wr(shim_addr, 0, base | MICA_ENGINE_DISABLE,
         ms->reset.disabled_engine_mask);

  // deflate engine is 0, inflate engines are 1 and 2

  int eng_no = 0;
  int num_regs = sizeof(deflate_eng_regs) / sizeof(*deflate_eng_regs);

  if (((ms->reset.disabled_engine_mask >> eng_no) & 1) == 0)
  {
    base = (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL <<
            MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
      (eng_no << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);

    for (int offset = 0; offset < num_regs; offset++)
      RESTORE_REG(shim_addr, base, restore, deflate_eng_regs[offset]);
  }

  num_regs = sizeof(inflate_eng_regs) / sizeof(*inflate_eng_regs);
  for (eng_no = 1; eng_no <= 2; eng_no++)
  {
    if (((ms->reset.disabled_engine_mask >> eng_no) & 1) == 0)
    {
      base = (MICA_ADDRESS_SPACE__PARTITION_VAL_ENGINE_GLOBAL <<
              MICA_ADDRESS_SPACE__PARTITION_SHIFT) | 
        (eng_no << MICA_ADDRESS_SPACE_ENGINE__ENGINE_SHIFT);
    
      for (int offset = 0; offset < num_regs; offset++)
        RESTORE_REG(shim_addr, base, restore, inflate_eng_regs[offset]);
    }
  }

  return restore;
}

static uint64_t*
restore_context_sys_regs(uint64_t* restore, mica_state_t* ms, int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;

  unsigned long base = (MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_SYSTEM <<
     MICA_ADDRESS_SPACE__PARTITION_SHIFT) |
    (context_num << 
     MICA_ADDRESS_SPACE_CTX_SYS__CONTEXT_SHIFT);

  int num_regs = sizeof(context_sys_regs) / sizeof(*context_sys_regs);

  for (int offset = 0; offset < num_regs; offset++)
    RESTORE_REG(shim_addr, base, restore, context_sys_regs[offset]);

  restore = mica_context_restore_iotlbs(ms, restore, context_num);

  return restore;
}


static uint64_t*
restore_context_user_regs(uint64_t* restore, mica_state_t* ms,
                          int context_num)
{
  uint32_t shim_addr = ms->shim_pos.word;
  unsigned long base = (MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_USER <<
     MICA_ADDRESS_SPACE__PARTITION_SHIFT) |
    (context_num << 
     MICA_ADDRESS_SPACE_CTX_USER__CONTEXT_SHIFT);

  // We absolutely must restore the opcode register last, because that is
  // what kicks off an operation.  This also means that we must not restore
  // the opcode register unless this context had an operation in progress
  // when the shim was reset.
  int num_regs = sizeof(context_user_regs) / sizeof(*context_user_regs);

  for (int offset = 0; offset < num_regs - 1; offset++)
    RESTORE_REG(shim_addr, base, restore, context_user_regs[offset]);

  assert(context_user_regs[num_regs - 1] == MICA_OPCODE);

  // Only restart the operation by writing the opcode if this operation was
  // pending when we reset the shim.  If it was actually running, fail it
  // by writing an opcode to the extra data register.
  if ((ms->reset.contexts_pending_mask >> context_num) & 1)
  {
    RESTORE_REG(shim_addr, base, restore, MICA_OPCODE);
  }
  else
    restore++;

  if ((ms->reset.contexts_in_progress_mask >> context_num) & 1)
    cfg_wr(shim_addr, 0, base | MICA_EXTRA_DATA_PTR, 
           GXIO_MICA_ERR_OPERATION_FAILED);

  return restore;
}


static void
restore_shim_regs(uint64_t* buf, mica_state_t* ms)
{
  buf = restore_engine_regs(buf, ms);

  buf = restore_global_regs(buf, ms);

  for (int i = 0; i < HV_MICA_NUM_CONTEXTS; i++)
    buf = restore_context_sys_regs(buf, ms, i);

  mica_validate_iotlbs(ms, 1);

  for (int i = 0; i < HV_MICA_NUM_CONTEXTS; i++)
    buf = restore_context_user_regs(buf, ms, i);
}


static void
mica_reset_comp_shims(int devhdl)
{
  mica_state_t* mica_0 = mica_state[0];

  spin_lock(&mica_0->reset.lock);

  // Get all cores into the hypervisor, so we know that they are not actively
  // writing to the shims.
  mica_0->reset.shim_reset_counter++;

  for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
    {
      pos_t dest = { .bits.x = x, .bits.y = y };
      if (dest.word != my_pos.word && in_tile_mask(&client_tiles, dest))
      {
        int replybuf, replybuflen, replylen;
        replybuflen = sizeof replybuf;
        int ctr = mica_0->reset.shim_reset_counter;
        drv_send_msg(devhdl, &ctr, sizeof(ctr), &replybuf,
                     replybuflen, &replylen, dest);
      }
    }

  for (int i = 0; i < MAX_MICA_COMPS + MAX_MICA_CRYPTOS; i++)
  {
    mica_state_t* ms = mica_state[i];

    if (ms && ms->reset.is_resettable)
    {
      TRACE("Saving shim regs for shim %d\n", i);
      // Read all information out of the shim.  We know that all cores are in the 
      // hypervisor, so things won't be changing out from under us.
      // FIXME: this can't ever work with the BME.
      save_shim_regs(ms->reset.reg_save_buf, ms);
    }
  }

  TRACE("Resetting shim\n");

  // Reset or wait for reset of the shim.
  cfg_wr(rshims[0]->idn_ports[0].word, 0, RSH_GX36_IO_RESET,
         RSH_GX36_IO_RESET__COMPRESSION_RMASK <<
         RSH_GX36_IO_RESET__COMPRESSION_SHIFT);

  __insn_mf();

  drv_udelay(10);

  // Read back to verify that shim reset is complete.
  if (cfg_rd(mica_0->shim_pos.word, 0, MICA_DEV_INFO) != 
      MICA_DEV_INFO__TYPE_VAL_COMPRESSION)
    panic("Mica compression shim reset failed");

  for (int i = 0; i <  MAX_MICA_COMPS + MAX_MICA_CRYPTOS; i++)
  {
    mica_state_t* ms = mica_state[i];

    if (ms && ms->reset.is_resettable)
    {
      TRACE("Reset complete, restoring shim regs for shim %d.\n", i);

      // Write information back to the shim.
      restore_shim_regs(ms->reset.reg_save_buf, ms);

      // Clear info for next reset.
      ms->reset.contexts_pending_mask =
        ms->reset.contexts_in_progress_mask = 0;
    }
  }

  // Release all of the cores by incrementing the reset counter
  TRACE("Releasing cores\n");
  mica_0->reset.shim_reset_counter++;

  spin_unlock(&mica_0->reset.lock);
}


/** Mica driver init routine. */
static int
mica_init(const char* drvname, void** statepp, int instance,
          int tileno, pos_t tile, const struct dev_info* info,
          const char* args)
{
  if (instance >= MAX_MICA_COMPS + MAX_MICA_CRYPTOS)
  {
    tprintf("failed to init driver %s, max instances exceeded\n", drvname);
    return (HV_ENODEV);
  }
  mica_state_t* ms;
  spin_lock(&mica_alloc_lock);
  ms = mica_state[instance];
  if (ms == NULL)
  {
    ms = drv_shared_state_zalloc(sizeof(*ms), 0);
    if (ms == NULL)
    {
      spin_unlock(&mica_alloc_lock);
      return (HV_EFAULT);
    }
    mica_state[instance] = ms;
    ms->shim_pos = info->idn_ports[0];
    TRACE("drvname = %s SHIM POS = (%d,%d)\n", drvname, ms->shim_pos.bits.x,
          ms->shim_pos.bits.y);

    // If this is a compression shim, set up the match control register and
    // disable performance counter interrupts.
    if (!strcmp(drvname, "comp"))
    {
      ms->reset.is_resettable = 1;
      ms->reset.reg_save_buf =
        drv_shared_state_zalloc(MICA_SHIM_TOTAL_REGS *
                                sizeof(*ms->reset.reg_save_buf), 0);
      if (ms->reset.reg_save_buf == NULL)
      {
        spin_unlock(&mica_alloc_lock);
        return (HV_EFAULT);
      }

      // Use a non-default value for MATCH_CTL to work around a hardware
      // bug involving an 8KB pattern match.
      MICA_COMP_ENG_DEFL_REG_MATCH_CTL_t match_ctl = {{
          .efforts = 3,
          .max_dist = 1,
        }};

      // Disable performance counter interrupts on the deflate engines.
      MICA_COMP_ENG_DEFL_REG_INT_VEC_MASK_t defl_int_vec_mask = {{
          .reg_int_vec_mask = 0xff,
        }};

      // Disable performance counter interrupts on the inflate engine.
      MICA_COMP_ENG_INFL_REG_INT_VEC_MASK_t infl_int_vec_mask = {{
          .reg_int_vec_mask = 0xff,
        }};

      unsigned long addr = MICA_ENG_REG_ADDR(
        MICA_COMP_ADDRESS_SPACE_ENGINE__ENGINE_VAL_INFLATE0_ENGINE_IDX,
        MICA_COMP_ENG_DEFL_REG_MATCH_CTL);
      cfg_wr(ms->shim_pos.word, 0, addr, match_ctl.word);
      cfg_wr(ms->shim_pos.word, 0, addr, infl_int_vec_mask.word);

      addr = MICA_ENG_REG_ADDR(
        MICA_COMP_ADDRESS_SPACE_ENGINE__ENGINE_VAL_INFLATE1_ENGINE_IDX,
        MICA_COMP_ENG_DEFL_REG_MATCH_CTL);
      cfg_wr(ms->shim_pos.word, 0, addr, match_ctl.word);
      cfg_wr(ms->shim_pos.word, 0, addr, infl_int_vec_mask.word);

      addr = MICA_ENG_REG_ADDR(
        MICA_COMP_ADDRESS_SPACE_ENGINE__ENGINE_VAL_DEFLATE_ENGINE_IDX,
        MICA_COMP_ENG_DEFL_REG_INT_VEC_MASK);
      cfg_wr(ms->shim_pos.word, 0, addr, defl_int_vec_mask.word);
    }

    if (!strncmp(info->name, "crypto", 6) && !sim_is_simulator())
    {
      load_pka_firmware(ms->shim_pos.word);
      init_trng(ms->shim_pos.word);

      // Initialize the PRNG on the EIP96s using the TRNG on the EIP154 on
      // this same shim.
      init_prng(ms->shim_pos.word, pp_ctl_base_addr[0]);
      init_prng(ms->shim_pos.word, pp_ctl_base_addr[1]);
    }

    unsigned long mmio_addr = 
      (MICA_ADDRESS_SPACE__PARTITION_VAL_DM_GLOBAL <<
       MICA_ADDRESS_SPACE_GLOBAL__PARTITION_SHIFT) |
      MICA_MEM_INFO;

    MICA_MEM_INFO_t mem_info = 
      { .word = cfg_rd(ms->shim_pos.word, 0, mmio_addr) };
    ms->num_iotlb_entries = mem_info.num_tlb_ent;

    for (int ctx_num = 0; ctx_num < HV_MICA_NUM_CONTEXTS; ctx_num++)
      mica_context_clear_iotlbs(ms, ctx_num);
  }

  spin_unlock(&mica_alloc_lock);

  *statepp = ms;

  return 0;
}

/** mica driver open routine - a new context number for each open. */
static int
mica_open(int devhdl, void* statep, const char* suffix,
          uint32_t flags, pos_t tile)
{
  DEVICE_TRACE("mica_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  mica_state_t* ms = statep;
  int context_num = -1;

  DEVICE_TRACE("mica_open: shim_pos.word=%#x x=%d y=%d\n",
               (unsigned int)ms->shim_pos.word,
               ms->shim_pos.bits.x, ms->shim_pos.bits.y);

  if (!strcmp(suffix, "/reset"))
  {
    if (ms->reset.is_resettable)
      mica_reset_comp_shims(devhdl);
  }

  if (!strcmp(suffix, "/iorpc"))
  {
    spin_lock(&ms->lock);
    context_num = mica_reserve_context(ms);
    spin_unlock(&ms->lock);

    if (context_num == GXIO_MICA_ERR_NO_CONTEXT)
    {
      return context_num;
    }

    /* Permit MMIO access */
    int err =
      drv_permit_mmio_access(ms->shim_pos,
                             HV_MICA_CONTEXT_USER_MMIO_OFFSET(context_num),
                             HV_MICA_CONTEXT_USER_MMIO_SIZE, 0);

    if (err != 0)
    {
      TRACE("Unexpected permit_mmio_access() failure(1)\n");
      spin_lock(&ms->lock);
      mica_unreserve_context(ms, context_num);
      spin_unlock(&ms->lock);
      return err;
    }
    
    return context_num;
  }
  else if (!strcmp(suffix, "/pka/iorpc"))
  {
    if (ms->ctl_reg_access_ref_cnt == 0)
    {
      if (sim_is_simulator())
        return HV_ENODEV;

      /* Permit MMIO access to PKA control registers */
      int err = drv_permit_mmio_access(ms->shim_pos, pka_ctl_base_addr,
                                       pka_ctl_len, 0);
      if (err != 0)
      {
        TRACE("Unexpected permit_mmio_access() failure, err = %d\n", err);
        return err;
      }
    }
    ms->ctl_reg_access_ref_cnt++;

    /* Permit MMIO access to PKA window RAM */
    int err = drv_permit_mmio_access(ms->shim_pos, pka_window_base_addr,
                                     pka_window_len, 0);
    if (err != 0)
    {
      TRACE("Unexpected permit_mmio_access() failure(1)\n");
      return err;
    }

    return MICA_PKA_DRIVER_DEVHDL;
  }
  else if (!strcmp(suffix, "/trng/iorpc"))
  {
    if (sim_is_simulator())
      return HV_ENODEV;

    if (ms->trng_in_use)
      return HV_EBUSY;

    ms->trng_in_use = 1;

    if (ms->ctl_reg_access_ref_cnt == 0)
    {
      /* Permit MMIO access to PKA TRNG control registers */
      int err = drv_permit_mmio_access(ms->shim_pos, pka_ctl_base_addr,
                                       pka_ctl_len, 0);
      if (err != 0)
      {
        TRACE("Unexpected permit_mmio_access() failure, err = %d\n", err);
        return err;
      }
    }
    ms->ctl_reg_access_ref_cnt++;

    return MICA_PKA_TRNG_DRIVER_DEVHDL;
  }

  return (HV_ENODEV);
}

/** mica driver close routine. */
static int
mica_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("mica_close: devhdl %#x\n", devhdl);

  mica_state_t* ms = statep;

  if (DRV_HDL2BITS(devhdl) == MICA_PKA_DRIVER_DEVHDL)
  {
    // Disable all interrupts by setting their mask bits to 1.
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr + MICA_CRYPTO_ENG_PKA_INT_MASK_SET,
           0x3ff);

    if (--ms->ctl_reg_access_ref_cnt == 0)
      if (drv_deny_mmio_access(ms->shim_pos,
                               pka_ctl_base_addr, pka_ctl_len, 0))
        TRACE("Unexpected deny_mmio_access() failure at close\n");

    if (drv_deny_mmio_access(ms->shim_pos,
                             pka_window_base_addr, pka_window_len, 0))
      TRACE("Unexpected deny_mmio_access() failure at close\n");

    return 0;
  }
  else if (DRV_HDL2BITS(devhdl) == MICA_PKA_TRNG_DRIVER_DEVHDL)
  {
    if (--ms->ctl_reg_access_ref_cnt == 0)
      if (drv_deny_mmio_access(ms->shim_pos,
                               pka_ctl_base_addr, pka_ctl_len, 0))
        TRACE("Unexpected deny_mmio_access() failure at close\n");

    ms->trng_in_use = 0;

    return 0;
  }

  unsigned int context_num = DRV_HDL2BITS(devhdl);

  spin_lock(&ms->lock);
  mica_unreserve_context(ms, context_num);
  spin_unlock(&ms->lock);

  if (drv_deny_mmio_access(ms->shim_pos,
                           HV_MICA_CONTEXT_USER_MMIO_OFFSET(context_num),
                           HV_MICA_CONTEXT_USER_MMIO_SIZE, 0))
    TRACE("Unexpected deny_mmio_access() failure at close\n");

  return 0;
}

/** mica driver close_all routine. */
static int
mica_close_all(int dev_idx, void* statep)
{
  DEVICE_TRACE("mica_close: dev_idx %d\n", dev_idx);

  mica_state_t* ms = statep;

  if (ms->ctl_reg_access_ref_cnt)
  {
    // Disable all interrupts by setting their mask bits to 1.
    cfg_wr(ms->shim_pos.word, 0,
           pka_tilera_ctl_base_addr + MICA_CRYPTO_ENG_PKA_INT_MASK_SET,
           0x3ff);

    ms->trng_in_use = 0;
    ms->ctl_reg_access_ref_cnt = 0;

    if (drv_deny_mmio_access(ms->shim_pos,
                             pka_ctl_base_addr, pka_ctl_len, 0))
      TRACE("Unexpected deny_mmio_access() failure at close_all\n");

    //This code could fail in case "/pka/iorpc" was not opened,
    //but the feailure will be harmless.
    drv_deny_mmio_access(ms->shim_pos,
                         pka_window_base_addr, pka_window_len, 0);
  }

  for (unsigned long context_mask = ms->reserved_contexts_bitmask; context_mask;
       context_mask &= context_mask - 1)
  {
    unsigned int context_num = __builtin_ctzl(context_mask);

    spin_lock(&ms->lock);
    mica_unreserve_context(ms, context_num);
    spin_unlock(&ms->lock);

    if (drv_deny_mmio_access(ms->shim_pos,
                             HV_MICA_CONTEXT_USER_MMIO_OFFSET(context_num),
                             HV_MICA_CONTEXT_USER_MMIO_SIZE, 0))
      TRACE("Unexpected deny_mmio_access() failure at close_all\n");
  }

  return 0;
}

/** mica driver read routine. */
static int
mica_pread(int devhdl, void* statep, uint32_t flags, char* va,
           uint32_t len, uint64_t offset, pos_t tile)
{
  DEVICE_TRACE("mica_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[MAX_STACK_BYTES];
  mica_state_t* ms = statep;
  int result;

  if (len > sizeof(buf))
    return (HV_EINVAL);

  spin_lock(&ms->lock);

  int context = DRV_HDL2BITS(devhdl);

  switch (context)
  {
  case MICA_PKA_DRIVER_DEVHDL:
  case MICA_PKA_TRNG_DRIVER_DEVHDL:
    result = dispatch_gxio_mica_pka_read(offset, buf, len, ms);
    break;
  default:
    if (!mica_context_is_open(ms, context))
    {
      spin_unlock(&ms->lock);
      return GXIO_ERR_INVAL_SVC_DOM;
    }
    result = dispatch_gxio_mica_read(offset, buf, len, ms, context);
    break;
  }

  if (drv_copy_to_client(va, buf, len, flags))
  {
    result = HV_EFAULT;
  }

  spin_unlock(&ms->lock);

  return result;
}

/** mica driver write routine. */
static int
mica_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  DEVICE_TRACE("mica_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[MAX_STACK_BYTES];
  mica_state_t* ms = statep;
  int result;

  if (len > sizeof(buf))
    return (HV_EINVAL);

  spin_lock(&ms->lock);

  if (drv_copy_from_client(buf, va, len, flags))
  {
    spin_unlock(&ms->lock);
    return HV_EFAULT;
  }

  int context = DRV_HDL2BITS(devhdl);

  switch (DRV_HDL2BITS(devhdl))
  {
  case MICA_PKA_DRIVER_DEVHDL:
  case MICA_PKA_TRNG_DRIVER_DEVHDL:
    result = dispatch_gxio_mica_pka_write(offset, buf, len, ms);
    break;
  default:
    if (!mica_context_is_open(ms, context))
    {
      spin_unlock(&ms->lock);
      return GXIO_ERR_INVAL_SVC_DOM;
    }
    result = dispatch_gxio_mica_write(offset, buf, len, ms, context);
    break;
  }

  spin_unlock(&ms->lock);

  return result;
}


/** Get the current setting for the MiCA PLL. */
static long
mica_get_cur_freq(const struct dev_info* info, int clock_index)
{
  //
  // Both MiCA shims have the same format & address for the PLL register.
  //
  MICA_CRYPTO_CLOCK_CONTROL_t mcc = 
    {
      .word = cfg_rd(info->idn_ports[0].word, info->channel,
                     MICA_CRYPTO_CLOCK_CONTROL)
    };

  return pll_to_freq(!mcc.ena, mcc.pll_m, mcc.pll_n, mcc.pll_q, REFCLK);
}


/** Get the desired setting for the MiCA PLL. */
static long
mica_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // If it's set in the .hvc, use that value.
  //
  if (info->speeds[clock_index])
    return info->speeds[clock_index];

  //
  // See if there's a board default in the BIB, and if so, use it.
  //
  MICA_CRYPTO_DEV_INFO_t mdi = 
    {
      .word = cfg_rd(info->idn_ports[0].word, info->channel,
                     MICA_CRYPTO_DEV_INFO)
    };

  union
  {
    bi_inst_t inst;
    struct bi_clock_inst bci;
  }
  ci =
    {
      .bci.type = (mdi.type == MICA_CRYPTO_DEV_INFO__TYPE_VAL_CRYPTO) ?
      BI_CLOCK_INST_TYPE__VAL_MICA_CRYPTO :
      BI_CLOCK_INST_TYPE__VAL_MICA_COMPRESS,
      .bci.shim = mdi.instance,
    };

  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_SHIM_CLOCK, ci.inst, &bp, NULL) != BI_NULL)
  {
    struct bi_shim_clock* sc = bp;
    return sc->freq;
  }

  //
  // If we haven't found a value, we want to run as fast as we can subject
  // to the voltage required by other shims.
  //
  return DRV_DESIRED_FREQ_MAX;
}


/** Set the MiCA PLL frequency. */
static int
mica_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  //
  // Both MiCA shims have the same format & address for the PLL register.
  //
  MICA_CRYPTO_CLOCK_CONTROL_t mcc = 
    {{
        .ena = 1,
        .pll_m = m,
        .pll_n = n,
        .pll_q = q,
        .pll_range = range,
      }};

  cfg_wr(info->idn_ports[0].word, info->channel, MICA_CRYPTO_CLOCK_CONTROL,
         mcc.word);
  __insn_mf();

  do
  {
    mcc.word = cfg_rd(info->idn_ports[0].word, info->channel,
                      MICA_CRYPTO_CLOCK_CONTROL);
  }
  while (!mcc.clock_ready);

  return 0;
}
 

static void
mica_msg(int devhdl, void* statep, drv_reply_msg_token_t reply_token,
       void* msg, int msglen, pos_t tile)
{
  mica_state_t* ms = statep;
  int val = *(int*)msg;

  TRACE("Waiting for shim reset counter at addr %p to equal %d, now is %d\n",
        &ms->reset.shim_reset_counter, val, ms->reset.shim_reset_counter);

  volatile int *pctr = &ms->reset.shim_reset_counter;
  while (*pctr != val)
    ;

  drv_reply_msg(reply_token, val, NULL, 0, tile);
  val++;

  TRACE("Waiting for shim reset counter at addr %p to equal %d, is now %d\n",
        &ms->shim_reset_counter, val, ms->shim_reset_counter);

  while (*pctr != val)
    ;

  TRACE("Core released.\n");
}


/** mica driver operations vector */
static struct drv_ops mica_ops = {
  .init             = mica_init,
  .open             = mica_open,
  .close            = mica_close,
  .close_all        = mica_close_all,
  .pread            = mica_pread,
  .pwrite           = mica_pwrite,
  .get_cur_freq     = mica_get_cur_freq,
  .get_desired_freq = mica_get_desired_freq,
  .set_freq         = mica_set_freq,
  .msg              = mica_msg,
};

//! Add a new "driver" entry for crypto.
static const __DRIVER_ATTR driver_t driver_crypto = {
  .shim_type  = MICA_CRYPTO_DEV_INFO__TYPE_VAL_CRYPTO,
  .name       = "crypto",
  .desc       = "Crypto Driver",
  .ops        = &mica_ops,
};

//! Add a new "driver" entry for compression.
static const __DRIVER_ATTR driver_t driver_comp = {
  .shim_type  = MICA_CRYPTO_DEV_INFO__TYPE_VAL_COMPRESSION,
  .name       = "comp",
  .desc       = "Compression Driver",
  .ops        = &mica_ops,
};
