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
 * Miscellaneous support routines.
 */

#include <stdio.h>
#include <string.h>

#include <arch/cycle.h>
#include <arch/interrupts.h>
#include <arch/ipi.h>
#include <arch/msh.h>
#include <arch/opcode.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "sys/libc/include/util.h"

#include "board_info.h"
#include "cfg.h"
#include "client_msg.h"
#include "client_obj.h"
#include "debug.h"
#include "devices.h"
#include "fault.h"
#include "hv.h"
#include "hw_config.h"
#include "mapping.h"
#include "misc.h"
#include "msg.h"
#include "patch.h"
#include "physacc.h"
#include "syscall.h"
#include "tlb.h"
#include "tsb.h"

/** Fence to guarantee visibility of stores to incoherent memory. */
__inline__ void
mf_incoherent()
{
  __insn_mf();

  void __mf_incoherent(void);
  __mf_incoherent();
}


/** Flush a range of addresses from the L2 cache back to memory, and wait for
 *  any memory writes to complete.
 * @param va Starting virtual address.
 * @param len Number of bytes to flush.
 */
void
flush_range(VA va, size_t len)
{
  VA start_va = ROUND_DN(va, CHIP_FLUSH_STRIDE());
  VA end_va = ROUND_DN(va + len - 1, CHIP_FLUSH_STRIDE());
  for (VA flush_va = start_va; flush_va <= end_va;
       flush_va += CHIP_FLUSH_STRIDE())
    __insn_flush((void*) flush_va);

#if 0 // FIXME: GX: not yet implemented
  mf_incoherent();
#else
  __insn_mf();
#endif
}


/** Flush and invalidate a range of addresses from the L2 cache back to memory,
 *  and wait for any memory writes to complete.
 * @param va Starting virtual address.
 * @param len Number of bytes to flush and invalidate.
 */
void
finv_range(VA va, size_t len)
{
  VA start_va = ROUND_DN(va, CHIP_FINV_STRIDE());
  VA end_va = ROUND_DN(va + len - 1, CHIP_FINV_STRIDE());
  for (VA flush_va = start_va; flush_va <= end_va;
       flush_va += CHIP_FINV_STRIDE())
    __insn_finv((void*) flush_va);

#if 0 // FIXME: GX: not yet implemented
  mf_incoherent();
#else
  __insn_mf();
#endif
}


/** Dump out a set of saved registers.
 * @param sr Pointer to the saved register structure.
 */
void
dump_saved_regs(struct saved_regs *sr)
{
  tprintf("Saved register dump:\n");

  for (int i = 0; i < 10; i++)
    utprintf("r%-2d %'019lx  r%-2d %'019lx  r%-2d %'019lx\n",
             i,      sr->r29_to_r0[29 - i],
             i + 10, sr->r29_to_r0[29 - 10 - i],
             i + 20, sr->r29_to_r0[29 - 20 - i]);

  utprintf("lr  %'019lx  sp  %'019lx\n", sr->lr, sr->sp);
  utprintf("im  %'019lx  ex0 %'019lx  ex1 %'019lx\n",
           sr->intmask, sr->ex_context_0, sr->ex_context_1);
}


/** Dump out a full set of saved registers.
 * @param sr Pointer to the saved register structure.
 */
void
dump_saved_regs_full(struct saved_regs_full *sr)
{
  tprintf("Full saved register dump:\n");

  for (int i = 0; i < 17; i++)
    utprintf("r%-2d %'019lx  r%-2d %'019lx  r%-2d %'019lx\n",
             i,      sr->r53_to_r0[53 - i],
             i + 18, sr->r53_to_r0[53 - 18 - i],
             i + 36, sr->r53_to_r0[53 - 36 - i]);

  utprintf("r17 %'019lx  r35 %'019lx  tp  %'019lx\n",
           sr->r53_to_r0[53 - 17], sr->r53_to_r0[53 - 35], sr->r53_to_r0[0]);

  utprintf("lr  %'019lx  sp  %'019lx\n", sr->lr, sr->sp);

  utprintf("im  %'019lx  ex0 %'019lx  ex1 %'019lx\n",
           sr->intmask, sr->ex_context_0, sr->ex_context_1);
}


/** Do interrupt downcall to Linux for MEM_ERROR interrupt.
 * @param int_number Interrupt number.
 * @params sr Pointer to the saved register structure.
 */
static void
handle_interrupt_downcall(int int_number, struct saved_regs_full *sr)
{
#if CLIENT_PL > 1
  int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;
  if (pl == 0 && have_virt_pt())
  {
    __insn_mtspr(EX_CONTEXT_G_0, sr->ex_context_0);
    __insn_mtspr(EX_CONTEXT_G_1, sr->ex_context_1);
    sr->ex_context_0 = __insn_mfspr(INTERRUPT_VECTOR_BASE_G) +
      (int_number << 8);
    sr->ex_context_1 = (1 << SPR_EX_CONTEXT_2_1__ICS_SHIFT) |
      (1 << SPR_EX_CONTEXT_2_1__PL_SHIFT);
    return;
  }
#endif
  __insn_mtspr(EX_CONTEXT_CL_0, sr->ex_context_0);
  __insn_mtspr(EX_CONTEXT_CL_1, sr->ex_context_1);
  sr->ex_context_0 = __insn_mfspr(INTERRUPT_VECTOR_BASE_CL) +
                     (int_number << 8);
  sr->ex_context_1 = (1 << SPR_EX_CONTEXT_2_1__ICS_SHIFT) |
                     (CLIENT_PL << SPR_EX_CONTEXT_2_1__PL_SHIFT);
}


/** Helper macro to describe the bit length of an error status register. */
#define STATUS_REG_LEN 64

/** A structure to describe a status bit and its description to user. */
struct status_desc
{
  int bit;         /**< Bit number in a status register. */
  const char* str; /**< Description string pointer to a corresponding status. */
};

/** Error status description string arrays for sbox, cbox and mbox. */
static const struct status_desc sbox_error_strs[] = 
{
  { 25, "Parity error was detected in way 0 of the L1 Icache Data." },
  { 26, "Parity error was detected in way 1 of the L1 Icache Data." },
  { 27, "Parity error was detected in way 0 of the L1 Icache Tag." },
  { 28, "Parity error was detected in way 1 of the L1 Icache Tag." },
  { 29, "More than one entry in ITLB matched the input parameters." },
  { 30, "A write to ITLB_CURRENT_ATTR was done and the entry was illegal." },
  { 62, "There are more than one outstanding un-masked memory errors." },
  { 63, "An L1 ICache error was detected." }, 
};

static const struct status_desc cbox_status_strs[] = 
{
  { 1,  "L2 data ram 1-bit error detected and corrected." },
  { 2,  "L2 data ram 2-bit error detected." },
  { 3,  "L2 tag ram error." },
  { 4,  "L2 state ram error." },
  { 5,  "RDN ack packet error detected in a RDN ingress request." },
  { 6,  "RDN data resp or ack packet error detected in an RDN ingress "
        "request." },
  { 7,  "L2 MAF request timeout." },
  { 8,  "Share invalidation matching a dirty block." },
  { 17, "There are more than one outstanding un-masked memory errors." },
};

static const struct status_desc mbox_status_strs[] =
{
  { 1,  "L1D data ram error detected in a tag hit." },
  { 2,  "L1D tag ram error detected in a tag match." },
  { 3,  "Multiple DTLB matches detected." },
  { 4,  "Illegal DTLB entry written." },
  { 5,  "Atomic with illegal attribute." },
  { 6,  "Flush/Flush-Inv/Inv/WH64/Prefetch with illegal attribute." },
  { 13, "There are more than one outstanding un-masked memory errors." },
}; 

/** Dump out error status descriptions for sbox, cbox and mbox.
 * @param status Value of a box error status register.
 * @param strs Pointer to a status description string array.
 * @param size Number of members in a status description string array.
 */
static void
dump_error_status(uint_reg_t status, const struct status_desc *strs, 
                  size_t size)
{
  for (int i = 0; i < STATUS_REG_LEN; i++)
    if ((status >> i) & 1)
    {
      for (int j = 0; j < size; j++)
        if (i == strs[j].bit)
          tprintf("          %s\n", strs[j].str);
    }
}


/** Helper macro for SET_MPL; allows use of macros as the second argument
 *  to SET_MPL(). */
#define _SET_MPL(name, pl)  __insn_mtspr(SPR_MPL_ ## name ## _SET_ ## pl , 1);
/** Set an MPL to a specific value. */
#define SET_MPL(name, pl)    _SET_MPL(name, pl)
/** Set an MPL to the PL of the client. */
#define SET_MPL_CLIENT(name) SET_MPL(name, CLIENT_PL)

/** Set the MPL SPRs for CLIENT_PL to 1 for the list of client exceptions.
*/
void set_client_mpls()
{
  SET_MPL_CLIENT(AUX_PERF_COUNT);
  SET_MPL_CLIENT(GPV);
#if (!defined(QUIESCE_ON_ILL) && !defined(ILL_DEBUG))
  SET_MPL_CLIENT(ILL);
#endif
  SET_MPL_CLIENT(ILL_TRANS);
#if CLIENT_PL > 1
  SET_MPL_CLIENT(INTCTRL_2);
#endif
  SET_MPL_CLIENT(INTCTRL_1);
  SET_MPL_CLIENT(INTCTRL_0);
#if CLIENT_PL > 1
  SET_MPL_CLIENT(IPI_2);
#endif
  SET_MPL_CLIENT(IPI_1);
  SET_MPL_CLIENT(IPI_0);
  SET_MPL_CLIENT(PERF_COUNT);
#if CLIENT_PL > 1
  SET_MPL_CLIENT(SINGLE_STEP_2);
#endif
  SET_MPL_CLIENT(SINGLE_STEP_1);
  SET_MPL_CLIENT(SINGLE_STEP_0);
  SET_MPL_CLIENT(SWINT_0);
  SET_MPL_CLIENT(SWINT_1);
  SET_MPL_CLIENT(TILE_TIMER);
  SET_MPL_CLIENT(AUX_TILE_TIMER);

  SET_MPL_CLIENT(UDN_ACCESS);
  SET_MPL_CLIENT(UDN_AVAIL);
  SET_MPL_CLIENT(UDN_COMPLETE);
  SET_MPL_CLIENT(UDN_FIREWALL);
  SET_MPL_CLIENT(UDN_TIMER);

  SET_MPL_CLIENT(UNALIGN_DATA);
  SET_MPL_CLIENT(WORLD_ACCESS);
}

/** Enable MMIO accesses to the closest IPI shim from each client.  Should
 *  only be called by the master tile.
 */
void setup_client_ipi_access()
{
  // Enable each per-tile MMIO region.
  // FIXME: combine regions for efficiency?
  for (int cl = 0; cl < config.nclients; cl++)
  {
    struct client_config *client = &config.clients[cl];
    if (client->flags & CLIENT_BME)
      continue;
    
    for (int y = client->ulhc.bits.y; y <= client->lrhc.bits.y; y++)
    {



      PA mmio_width = (1ULL << IPI_REMOTE_TRIGGER_ADDR__TILE_Y_SHIFT);

      for (int x = client->ulhc.bits.x; x <= client->lrhc.bits.x; x++)
      {
        if (in_tile_mask(&client->tiles, (pos_t){ .bits.x = x, .bits.y = y}))
        {
          for (int pl = CLIENT_PL; pl >= 0; pl--)
          {







            IPI_REMOTE_TRIGGER_ADDR_t addr = {{
                .tile_y = y,
                .tile_x = x,
                .ipi = pl,
            }};

            pos_t pseudo_ipi_pos = { .bits.x = 0, .bits.y = 0 };
            drv_permit_mmio_access(pseudo_ipi_pos, addr.word, mmio_width, cl);
          }
        }
      }
    }
  }
}

/** Return an interrupt name, or "invalid".
 */
static const char* interrupt_name(int num)
{
  if (num >= 0 && num < NUM_INTERRUPTS && int_names[num])
    return int_names[num];
  else
    return "invalid";
}

/** Helper function to set cache error message.
 * @param msg Pointer to cache error message.
 * @param type The type of cache error(HV_CE_xxx).
 * @param info Additional information identifying cache error.
 */
static void set_edac_msg(HV_EdacMsg *msg, uint32_t type, uint64_t info)
{
  Lotar client_lotar;

  (void) r2c_lotar(my_lotar, &client_lotar);
  msg->tile.x = HV_LOTAR_X(client_lotar);
  msg->tile.y = HV_LOTAR_Y(client_lotar);
  msg->edactype = type;
  msg->edacinfo = info;
}

/** Handler for a bad (unimplemented) interrupt.
 * @param int_number Interrupt number.
 * @param sr Pointer to saved registers from time of interrupt.
 */
void
bad_intr(int int_number, struct saved_regs_full *sr)
{
  switch (int_number)
  {
    case INT_MEM_ERROR:
    {
      uint_reg_t sbox_error = __insn_mfspr(SPR_SBOX_ERROR);
      uint_reg_t cbox_status = __insn_mfspr(SPR_MEM_ERROR_CBOX_STATUS);
      uint_reg_t mbox_status = __insn_mfspr(SPR_MEM_ERROR_MBOX_STATUS);
      uint_reg_t xdn_error = __insn_mfspr(SPR_XDN_DEMUX_ERROR);

      int nonfatal_error = 0;
      HV_EdacMsg msg;

      if (cbox_status & SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_CORRECTED_MASK)
      {
        //
        // If there is an L2_DATA_CORRECTED ECC error, print a message,
        // and finv the faulting cacheline to recover.
        // 
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_CBOX_ADDR);

        set_edac_msg(&msg, HV_CE_L2_D_ECC_ERROR, physaddr);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: L2$ correctable data ECC error at PA %#llx\n", 
                  physaddr);

        //
        // Setup AAR to visit physical memory.
        //
        SPR_AAR_t aar = 
        {{
          .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
          .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
          .cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE,
          .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT,
          .physical_memory_mode = 1,
        }};
          
        //
        // Finv the whole corrupt cacheline using the physical address.
        //
        phys_finv(physaddr, aar.word);
        __insn_mf();
        nonfatal_error = 1;
      }

      if (cbox_status & SPR_MEM_ERROR_CBOX_STATUS__OVERFLOW_MASK)
      {
        set_edac_msg(&msg, HV_CE_L2_OVERFLOW_ERROR, 0);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: multiple simultaneous L2$/L3$ errors\n");
        nonfatal_error = 1;
      }

      if (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__L1_D_TAG_MASK)
      {
        //
        // If there is an L1_D_TAG parity error, print a message, and do
        // cacheline replacement flushing to recover.
        //
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_MBOX_ADDR);

        set_edac_msg(&msg, HV_CE_L1_D_TAG_ERROR, physaddr);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: L1D$ tag parity error at PA %#llx\n",
                  physaddr);

        //
        // We need to displacement flush the line with the bad tag out of
        // the L1, or we'll keep getting tag errors.  You could do this
        // fairly quickly by hand, since we only need to replace one set in
        // the L1D$.  However, cache-flushing code is hard to get right; a
        // previous version of this routine that tried to have high
        // performance got it wrong.  So we're just going to call the code
        // we already have that flushes the entire L1D$ and L2$.  If you're
        // getting enough L1D$ tag errors that this is a performance
        // impact, you've got bigger problems.
        //
        // Note that we disable the L1D$ tag interrupt while we flush, and
        // reenable it afterward.  This is because the tag error is going
        // to happen again on probes to the faulting set until the bad line
        // is evicted, and since there's at least one error already present
        // in MBOX_STATUS, that'll lead to the multiple errors bit being
        // set, which we currently treat as a fatal error.
        //
        uint_reg_t old_mee = __insn_mfspr(SPR_MEM_ERROR_ENABLE);
        __insn_mtspr(SPR_MEM_ERROR_ENABLE,
                     old_mee & ~SPR_MEM_ERROR_ENABLE__L1_D_TAG_MASK);
        inv_whole_l2();
        __insn_mtspr(SPR_MEM_ERROR_ENABLE, old_mee);
        nonfatal_error = 1;
      }

      if (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__L1_D_DATA_MASK)
      {
        //
        // If there is an L1_D_DATA parity error, print a message, and finv
        // the corrupt cacheline to recover.
        //
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_MBOX_ADDR);

        set_edac_msg(&msg, HV_CE_L1_D_DATA_ERROR, physaddr);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: L1D$ data parity error at PA %#llx\n",
                  physaddr);

        //
        // Setup AAR to visit physical memory.
        //
        SPR_AAR_t aar =
        {{
          .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
          .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
          .cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE,
          .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT,
          .physical_memory_mode = 1,
        }};

        //
        // Finv the whole corrupt cacheline using the physical address.
        //
        phys_finv(physaddr, aar.word);
        __insn_mf();
        nonfatal_error = 1;
      }

      if (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__OVERFLOW_MASK)
      {
        set_edac_msg(&msg, HV_CE_L1_D_OVERFLOW_ERROR, 0);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: multiple simultaneous L1D$/TLB errors\n");
        nonfatal_error = 1;
      }

      if (sbox_error & SPR_SBOX_ERROR__L1_I_MASK)
      {
        //
        // If there is an L1I$ data parity error, print a message, and icoh
        // the corrupt data by index to recover.
        //
        uint64_t index = (sbox_error & SPR_SBOX_ERROR__INDEX_MASK) << 3;

        set_edac_msg(&msg, HV_CE_L1_I_ERROR, index);
        deliver_local_message(HV_MSG_EDAC, &msg, sizeof(msg));
        if (!config.mem_error_silent)
          tprintf("hv_warning: L1I$ parity error at index %#llx\n", index);

        //
        // Evict the corrupt cacheline out by icoh.
        //
        __insn_icoh((void*) index);
        __insn_drain();
        __insn_mf();
        nonfatal_error = 1;
      }

      if (cbox_status & SPR_MEM_ERROR_CBOX_STATUS__L2_RDN_READ_MASK)
      {
        int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;
   
        if (pl < HV_PL)
        {
          //
          // If the interrupt happened at or below the client's PL (typically 
          // an MMIO read from a disabled PIO region), then we want the 
          // supervisor to take care of it, so we don't print any messages; 
          // we just run the supervisor interrupt handler as if it was called 
          // directly.
          //
          handle_interrupt_downcall(INT_MEM_ERROR, sr);
        }
        else
        {
          //
          // Otherwise, just panic.
          // 
          panic_start("got read error response on RDN interrupt: "
                      "PC %#lX, ICS/PL %#lx", sr->ex_context_0,
                      sr->ex_context_1);
          dump_saved_regs_full(sr);
          panic_end();
        }
      }

      if (cbox_status & SPR_MEM_ERROR_CBOX_STATUS__L2_RDN_WRITE_MASK)
      {
        int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;

        if (pl < HV_PL)
        {
          //
          // If the interrupt happened at or below the client's PL (typically 
          // an MMIO write to a disabled PIO region), then we want the 
          // supervisor to take care of it, so we don't print any messages; 
          // we just run the supervisor interrupt handler as if it was called 
          // directly.
          //
          handle_interrupt_downcall(INT_MEM_ERROR, sr);
        }
        else
        {
          //
          // Otherwise, just panic.
          //
          panic_start("got write error ack on RDN interrupt: "
                      "PC %#lX, ICS/PL %#lx", sr->ex_context_0,
                      sr->ex_context_1);
          dump_saved_regs_full(sr);
          panic_end();
        }
      }

      if (mbox_status & 
          SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_ATOMIC_ATTRIBUTE_MASK)
      {
        int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;

        if (pl < HV_PL)
        {
          //
          // If the interrupt happened at or below the client's PL, then we
          // want the supervisor to take care of it, so we don't print any
          // messages; we just run the supervisor interrupt handler as if
          // it was called directly.
          //
          handle_interrupt_downcall(INT_MEM_ERROR, sr);
        }
        else
        {
          //
          // Otherwise, just panic.
          //
          panic_start("got illegal atomic attribute interrupt: "
                      "PC %#lX, ICS/PL %#lx", sr->ex_context_0,
                      sr->ex_context_1);
          dump_saved_regs_full(sr);
          panic_end();
        }
      }

      int fatal_error =
        (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__DTLB_MULTI_MATCH_MASK) ||
        (cbox_status & (SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_FATAL_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_TAG_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_STATE_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_MAF_TIMEOUT_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__SHARE_INVALIDATION_MASK)) ||
        (sbox_error & SPR_SBOX_ERROR__ITLB_MULTI_MATCH_MASK) ||
        (xdn_error & (SPR_XDN_DEMUX_ERROR__XDN_DEMUX_ERROR_OVERFLOW_MASK |
                      SPR_XDN_DEMUX_ERROR__XDN_DEMUX_ERROR_PENDING_MASK));

      //
      // If we're using small pages smaller than 64K, then we expect to see
      // illegal TLB entry errors.  Otherwise, those are fatal.
      //
      if (!fatal_error && page_size_small >= 64 * 1024)
      {
        fatal_error =
          (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_DTLB_ENTRY_MASK) ||
          (sbox_error & SPR_SBOX_ERROR__ILLEGAL_ITLB_ENTRY_MASK);
      }

      if (fatal_error || (nonfatal_error && config.mem_error_panic))
      {
        struct itlb_state itlb_state;
        struct dtlb_state dtlb_state;

        //
        // Save state early, while it is still impossible for an
        // interrupt or a message from another tile to interrupt us
        // and cause the TLB state to get modified.
        //
        save_itlb(&itlb_state);
        save_dtlb(&dtlb_state);

        //
        // If we saw any fatal errors, panic.
        //
        panic_start("got processor error: PC %#lX, ICS/PL %#lx",
                    sr->ex_context_0, sr->ex_context_1);

        utprintf("SBOX_ERROR:            0x%'019lx\n", sbox_error);
        dump_error_status(sbox_error, sbox_error_strs, 
                          (sizeof(sbox_error_strs) / 
                           sizeof(sbox_error_strs[0])));
        utprintf("MEM_ERROR_CBOX_ADDR:   0x%'019lx\n",
                 __insn_mfspr(SPR_MEM_ERROR_CBOX_ADDR));
        utprintf("MEM_ERROR_CBOX_STATUS: 0x%'019lx\n", cbox_status);
        dump_error_status(cbox_status, cbox_status_strs,
                          (sizeof(cbox_status_strs) / 
                           sizeof(cbox_status_strs[0])));
        utprintf("MEM_ERROR_MBOX_ADDR:   0x%'019lx\n",
                 __insn_mfspr(SPR_MEM_ERROR_MBOX_ADDR));
        utprintf("MEM_ERROR_MBOX_STATUS: 0x%'019lx\n", mbox_status);
        dump_error_status(mbox_status, mbox_status_strs,
                          (sizeof(mbox_status_strs) / 
                           sizeof(mbox_status_strs[0])));
        utprintf("XDN_DEMUX_ERROR:       0x%'019lx\n", xdn_error);

        for (int i = 0; i < MAX_MSHIMS; i++)
          if (mshims[i])
          {
            utprintf("%s:\n", mshims[i]->name);
            utprintf("    MSH_INT_VEC0_W1TC:          0x%'019lx\n",
                     cfg_rd(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                            MSH_INT_VEC0_W1TC));
            utprintf("    MSH_ACC_ERROR_INFO:         0x%'019lx\n",
                     cfg_rd(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                            MSH_ACC_ERROR_INFO));
            utprintf("    MSH_ECC_ERROR_INFO:         0x%'019lx\n",
                     cfg_rd(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                            MSH_ECC_ERROR_INFO));
            utprintf("    MSH_MMIO0_ERROR_INFO:       0x%'019lx\n",
                     cfg_rd(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                            MSH_MMIO0_ERROR_INFO));
            utprintf("    MSH_MMIO1_ERROR_INFO:       0x%'019lx\n",
                     cfg_rd(mshims[i]->idn_ports[0].word, mshims[i]->channel,
                            MSH_MMIO1_ERROR_INFO));
          }

        dump_saved_regs_full(sr);
        dump_saved_itlb(&itlb_state, 0);
        dump_saved_dtlb(&dtlb_state, 0);
        panic_end();
      }

      //
      // Clear any error bits we handled so that we don't take the
      // interrupt again.  The XDN error register is different than the
      // others; it's not write-one-to-clear, just read/write.
      //
      __insn_mtspr(SPR_SBOX_ERROR, sbox_error);
      __insn_mtspr(SPR_MEM_ERROR_CBOX_STATUS, cbox_status);
      __insn_mtspr(SPR_MEM_ERROR_MBOX_STATUS, mbox_status);
      if (xdn_error & (SPR_XDN_DEMUX_ERROR__XDN_DEMUX_ERROR_OVERFLOW_MASK |
                       SPR_XDN_DEMUX_ERROR__XDN_DEMUX_ERROR_PENDING_MASK))
        __insn_mtspr(SPR_XDN_DEMUX_ERROR, 0);
    }
    break;

    case INT_GPV:
    {
      SPR_GPV_REASON_t gpv_reason = { .word =  __insn_mfspr(SPR_GPV_REASON) };
      if (gpv_reason.iret_error)
        panic_start("got GPV interrupt: PC %#lX, ICS/PL %#lx, iret violation",
                    sr->ex_context_0, sr->ex_context_1);
      else if (gpv_reason.mt_error)
        panic_start("got GPV interrupt: PC %#lX, ICS/PL %#lx, mtspr %u",
                    sr->ex_context_0, sr->ex_context_1, gpv_reason.spr_index);
      else if (gpv_reason.mf_error)
        panic_start("got GPV interrupt: PC %#lX, ICS/PL %#lx, mfspr %u",
                    sr->ex_context_0, sr->ex_context_1, gpv_reason.spr_index);
      else
        panic_start("got GPV interrupt: PC %#lX, ICS/PL %#lx, unknown reason",
                    sr->ex_context_0, sr->ex_context_1);
      dump_saved_regs_full(sr);
      panic_end();
    }
    break;

    case INT_ILL_TRANS:
    {
      //
      // If we expected a fault, we don't want to panic.
      //
      if (fault_expected())
        fault_encountered();

      panic_start("got illegal translation interrupt: PC %#lX, ICS/PL %#lx",
                  sr->ex_context_0, sr->ex_context_1);
      utprintf("ILL_TRANS_REASON:      0x%'019lx\n",
               __insn_mfspr(SPR_ILL_TRANS_REASON));
      utprintf("ILL_VA_PC:             0x%'019lx\n",
               __insn_mfspr(SPR_ILL_VA_PC));

      dump_saved_regs_full(sr);
      panic_end();
    }
    break;

    case INT_UNALIGN_DATA:
    {
      panic_start("got unaligned data interrupt: PC %#lX, ICS/PL %#lx",
                  sr->ex_context_0, sr->ex_context_1);
      dump_saved_regs_full(sr);
      panic_end();
    }
    break;

    //
    // These two interrupts are owned by the HV, but user programs can provoke
    // them.  In that case, we don't want to panic; we want to let the
    // supervisor kill the user process.
    //
    case INT_IDN_ACCESS:
    case INT_SWINT_3:
    {
      int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;

      if (pl < CLIENT_PL)
      {
        //
        // If the interrupt happened at PL0, then we want the supervisor
        // to take care of it, so we don't print any messages; we just run
        // the supervisor interrupt handler as if it was called directly.
        //
        handle_interrupt_downcall(int_number, sr); 

        return;
      }

      panic_start("got unimplemented %s interrupt (#%d): PC %#lX, ICS/PL %#lx",
                  interrupt_name(int_number), int_number,
                  sr->ex_context_0, sr->ex_context_1);
      dump_saved_regs_full(sr);
      panic_end();
    }
    break;

    case INT_DOUBLE_FAULT:
    {
      int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;
      SPR_LAST_INTERRUPT_REASON_t last_int =
        { .word =  __insn_mfspr(SPR_LAST_INTERRUPT_REASON) };

      if (pl < CLIENT_PL)
      {
        //
        // If the double fault happened below the client's PL, then we
        // expect the supervisor to take care of it, so we don't print any
        // messages; we just run the supervisor double fault handler as if
        // it was called directly.  We provide the last interrupt reason
        // for the supervisor.
        //
#if CLIENT_PL > 1
        if (pl == 0 && have_virt_pt())
          __insn_mtspr(SYSTEM_SAVE_G_2, last_int.word);
        else
#endif
          __insn_mtspr(SYSTEM_SAVE_CL_2, last_int.word);
        handle_interrupt_downcall(INT_DOUBLE_FAULT, sr);

        return;
      }

      tprintf("got double fault interrupt: PC %#lX, ICS/PL %#lx\n",
              sr->ex_context_0, sr->ex_context_1);
      tprintf("       last interrupt %s, next-to-last interrupt %s\n",
              interrupt_name(last_int.last_reason),
              interrupt_name(last_int.last_last_reason));

      if (last_int.last_reason == INT_ILL_TRANS ||
          last_int.last_last_reason == INT_ILL_TRANS)
        tprintf("       ILL_TRANS_REASON %lld, ILL_VA_PC %#llX\n",
                __insn_mfspr(SPR_ILL_TRANS_REASON),
                __insn_mfspr(SPR_ILL_VA_PC));

      if (pl == HV_PL)
      {
        tprintf("       HVPL system save regs: 0: %#llX  1: %#llX  2: %#llX  "
                "3: %#llX\n",
                __insn_mfspr(SYSTEM_SAVE_HV_0),
                __insn_mfspr(SYSTEM_SAVE_HV_1),
                __insn_mfspr(SYSTEM_SAVE_HV_2),
                __insn_mfspr(SYSTEM_SAVE_HV_3));
      }
      if (pl == CLIENT_PL)
      {
        tprintf("       original client PL fault PC %#llX, ICS/PL %#llx\n",
                __insn_mfspr(EX_CONTEXT_CL_0),
                __insn_mfspr(EX_CONTEXT_CL_1));
        tprintf("       client PL system save regs: "
                "0: %#llX  1: %#llX  2: %#llX  3: %#llX\n",
                __insn_mfspr(SYSTEM_SAVE_CL_0),
                __insn_mfspr(SYSTEM_SAVE_CL_1),
                __insn_mfspr(SYSTEM_SAVE_CL_2),
                __insn_mfspr(SYSTEM_SAVE_CL_3));
      }
      dump_saved_regs_full(sr);

      if (pl == CLIENT_PL)
      {
        //
        // If the double fault happened at the client's PL, we've printed a
        // register dump, but we want to return to the supervisor to give
        // it a chance to do a stack backtrace, a crash dump, or whatever
        // it feels like doing.  (The register dump is really just in case
        // it's so messed up it can't do anything else, which is certainly
        // possible.)  We don't want to get into an infinite loop, so we
        // set a flag that says we've been here; if we see it again we just
        // panic.
        //
        static int client_double_fault_seen;

        if (client_double_fault_seen)
          panic("recursive supervisor double fault");
        else
          client_double_fault_seen = 1;

        //
        // We want the supervisor double-fault handler to get the PC and PL
        // from the last fault in its EX_CONTEXT registers, since that's
        // what it'll see for a double fault that happened at PL0.  However,
        // we don't want to lose the information from the first fault, so
        // we stuff that into the PL1 system save registers.
        //
#if CLIENT_PL > 1
        if (pl == 0 && have_virt_pt())
        {
          __insn_mtspr(SYSTEM_SAVE_G_2, __insn_mfspr(EX_CONTEXT_G_0));
          __insn_mtspr(SYSTEM_SAVE_G_3, __insn_mfspr(EX_CONTEXT_G_1));
        }
        else
#endif
        {
          __insn_mtspr(SYSTEM_SAVE_CL_2, __insn_mfspr(EX_CONTEXT_CL_0));
          __insn_mtspr(SYSTEM_SAVE_CL_3, __insn_mfspr(EX_CONTEXT_CL_1));
        }
        handle_interrupt_downcall(INT_DOUBLE_FAULT, sr);
      }
      else
      {
        //
        // If the double fault happened at PL2 or 3, there's not much else
        // we can do, so we die.
        //
        panic("hypervisor double fault");
      }
    }
    break;

    case INT_ILL:
    {
      //
      // Loading from the fault PC is a bit sketchy when ILL_DEBUG is defined,
      // since in that case it could be a client VA which might or might not
      // actually be mapped in the DTLB.  That's not the common case, though.
      //
      uint64_t* iptr = (uint64_t*) sr->ex_context_0;
      uint64_t bundle_before = *iptr;

      //
      // Arithmetic exceptions are signalled with a very specific illegal
      // instruction combined with a very specific move instruction which
      // tells us exactly what type of exception it is.  Since the
      // hypervisor doesn't use any floating-point, the only one of these
      // we can get, and thus want to check for, is divide-by-zero.  We
      // could check a dozen or so instruction characteristics with the
      // opcode macros, but it's going to end up being one specific bundle,
      // so we just check for that.
      //
      if (bundle_before == 0x286a44ae90048fffULL)
      {
        panic_start("got divide-by-zero exception: PC %#lX, ICS/PL %#lx",
                    sr->ex_context_0, sr->ex_context_1);

        dump_saved_regs_full(sr);
      }
      else
      {
        panic_start("got illegal instruction interrupt: PC %#lX, ICS/PL %#lx",
                    sr->ex_context_0, sr->ex_context_1);
        tprintf("       faulting bundle before finv: %016llx\n", bundle_before);
        __insn_finv(iptr);
        uint64_t bundle_after = *iptr;
        tprintf("       faulting bundle after finv:  %016llx\n", bundle_after);
        tprintf("       XOR of before/after bundles: %016llx\n",
                bundle_before ^ bundle_after);

        dump_saved_regs_full(sr);
        dump_itlb(0);
        dump_dtlb(0);
      }
      panic_end();
    }
    break;

    default:
    {
      panic_start("got unimplemented %s interrupt (#%d): PC %#lX, ICS/PL %#lx",
                  interrupt_name(int_number), int_number,
                  sr->ex_context_0, sr->ex_context_1);
      dump_saved_regs_full(sr);
      panic_end();
    }
    break;
  }
}


/** The number of bits by which to shift the VA address when searching for
 * an unused VA range in the TLB.
 */
#define VA_REGION_SHIFT 24
/** The mask of address bits to examine when looking for an unused VA range. */
#define VA_REGION_MASK  0xff

/** Jump to client code, using a new set of mappings.  Used to transfer
 * control to BME clients.
 * @param pc Virtual address to jump to.
 * @param pl Protection level to jump to.
 * @param sp Value to load into the stack pointer.
 * @param arg0 Value to pass to the client in r0.
 * @param itte Array of TTEs to install in the ITLB.  Must be within the
 *   hypervisor's per-tile data area.
 * @param n_itte Length in entries of itte[].  Must be at least 1 less than
 *   the ITLB size.
 * @param dtte Array of TTEs to install in the DTLB.  Must be within the
 *   hypervisor's per-tile data area.
 * @param n_dtte Length in entries of dtte[].  Must be at least 1 less than
 *   the DTLB size.
 */
void
jump2_vaplsp_mappings(VA pc, int pl, VA sp, VA arg0,
                      const tte_t itte[], int n_itte,
                      const tte_t dtte[], int n_dtte)
{
  //
  // Prototype for our assembly-language helper function.  Should almost
  // certainly match our arguments.
  //
  extern void _jump2_vaplsp_mappings(VA pc, uint32_t pl, VA sp, VA arg0,
                                     const tte_t itte[], int n_itte,
                                     const tte_t dtte[], int n_dtte);

  //
  // Verify that we have room for all the requested entries in the TLBs.
  //
  int nent_i = __insn_mfspr(SPR_NUMBER_ITLB);
  int nent_d = __insn_mfspr(SPR_NUMBER_DTLB);

  if (n_itte >= nent_i)
    panic("too many ITLB mappings");
  if (n_dtte >= nent_d)
    panic("too many DTLB mappings");

  //
  // Clear out all unwired entries in the TLBs.
  //
  clean_itlb(0);
  clean_dtlb(0);

  // Find two VA regions which aren't used by the current wired TLB
  // entries or the input set of TTEs, to use for temporary mappings.
  // Since we're looking at 256 regions and have far fewer tlb entries,
  // we're sure to find some vast unused regions.
  int8_t pages_used[VA_REGION_MASK + 1];
  memset(pages_used, 0, sizeof (pages_used));

  int nwent_i = __insn_mfspr(SPR_WIRED_ITLB);
  for (int i = 0; i < nwent_i; i++)
  {
    LOAD_TLB(I, i);
    pages_used[(__insn_mfspr(SPR_ITLB_CURRENT_VA) >> VA_REGION_SHIFT) & 
               VA_REGION_MASK] = 1;
  }

  int nwent_d = __insn_mfspr(SPR_WIRED_DTLB);
  for (int i = 0; i < nwent_d; i++)
  {
    LOAD_TLB(D, i);
    pages_used[(__insn_mfspr(SPR_DTLB_CURRENT_VA) >> VA_REGION_SHIFT) &
               VA_REGION_MASK] = 1;
  }

  for (int i = 0; i < n_itte; i++)
    pages_used[(itte[i].w1.word >> VA_REGION_SHIFT) & VA_REGION_MASK] = 1;

  for (int i = 0; i < n_dtte; i++)
    pages_used[(dtte[i].w1.word >> VA_REGION_SHIFT) & VA_REGION_MASK] = 1;

  int page_index = 0;
  while (pages_used[page_index])
    page_index++;

  VA temp_i_va = page_index++ << VA_REGION_SHIFT;

  while (pages_used[page_index])
    page_index++;

  uint64_t temp_d_va = page_index << VA_REGION_SHIFT;

  //
  // Disable interrupts before we start messing with the TLBs.
  //
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
  
  //
  // Map in the hypervisor code with a temporary mapping in the last ITLB
  // entry.
  //
  PA code_pa = vtop((VA) &_jump2_vaplsp_mappings);
  PA code_page_pa = code_pa & ~0xFFFFFFULL;

  tte_t temp_itte = {
    {{
        .v = 1,
        .w = 0,
        .mpl = HV_PL,
        .g = 1,
        .ps = TTE_PS_16M,
        .asid = 0,
        .memory_attribute = 
        SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping = // FIXME: verify
        SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .location_y_or_page_offset = HV_LOTAR_X(my_lotar),
        .location_x_or_page_mask =  HV_LOTAR_Y(my_lotar),
      }},
    { .word = temp_i_va },
    { .word = code_page_pa },
  };

  __insn_mtspr(SPR_ITLB_INDEX, nent_i - 1);
  WRITE_TLB(I, temp_itte);

  //
  // Map in our data arrays with a temporary mapping in the last DTLB entry.
  //
  PA itte_pa = vtop((VA) itte);
  PA itte_page_pa = itte_pa & ~0xFFFFFFULL;
  PA dtte_pa = vtop((VA) dtte);
  assert(itte_page_pa == (dtte_pa & ~0xFFFFFFULL));

  tte_t temp_dtte = {
    {{
        .v = 1,
        .w = 0,
        .mpl = HV_PL,
        .g = 1,
        .ps = TTE_PS_16M,
        .asid = 0,
        .memory_attribute = 
        SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping = // FIXME: verify
        SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .location_y_or_page_offset = HV_LOTAR_X(my_lotar),
        .location_x_or_page_mask =  HV_LOTAR_Y(my_lotar),
      }},
    { .word = temp_d_va },
    { .word = itte_page_pa },
  };

  __insn_mtspr(SPR_DTLB_INDEX, nent_d - 1);
  WRITE_TLB(D, temp_dtte);

  //
  // Call our assembly helper to install the entries and jump to the client.
  // Note that we need to use the remapped VA's to call it, and to pass the
  // TTE arrays.
  //
  typedef void (*j2_type)(VA, uint32_t, VA, VA, const tte_t[], int,
                          const tte_t[], int);
  // The next assignment is for type checking, to verify that the signature of
  // the routine we're indirectly calling matches the type above.
  j2_type j2_ptr = &_jump2_vaplsp_mappings;
  j2_ptr = (j2_type) (temp_i_va | ((VA) code_pa & 0xFFFFFF));

  tte_t* remap_itte = (tte_t*) (temp_d_va | ((VA) itte_pa & 0xFFFFFF));
  tte_t* remap_dtte = (tte_t*) (temp_d_va | ((VA) dtte_pa & 0xFFFFFF));

  j2_ptr(pc, pl, sp, arg0, remap_itte, n_itte, remap_dtte, n_dtte);
}


/** Number of calls to map_remote() without a corresponding unmap_remote(). */
static long n_remote_outstanding = 0;

/** Table of precalculated TLB attributes and physical addresses for each
 *  tile's local data area. */
static struct
{
  tte_w0_t attr;
  PA pa;
} remote_map_info[HV_TILES];


/** Initialize data used by map_remote(). */
void
init_map_remote()
{
  for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
  {
    for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };

      //
      // Compute this tile's data page PA, based on its offset from us
      // and our PA.  Note that later tiles are at lower physical
      // addresses, which is why we do our_pos - their_pos.
      //
      int tile_delta = my_pos.bits.x - tile.bits.x + 
        (chip_lrhc.bits.x - chip_ulhc.bits.x + 1) *
        (my_pos.bits.y - tile.bits.y);
      PA tile_pa = my_data_pa + tile_delta * HV_DATA_PAGE_SIZE;

      //
      // Compute the attributes.  Note that we optimize for the case where
      // the remote mapping is writable; if it isn't, the w bit will be
      // cleared in map_remote().
      //
      tte_w0_t attr =
      {{
        .ps = TTE_SHIFT_TO_PS(HV_DATA_PAGE_SHIFT),
        .g = 1,
        .asid = 0,
        .v = 1,
        .w = 1,
        .mpl = HV_PL,
        .memory_attribute =  
          SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping =  
          SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .location_x_or_page_mask = tile.bits.x,
        .location_y_or_page_offset = tile.bits.y,
      }};

      remote_map_info[POS2IDX(tile)].attr = attr;
      remote_map_info[POS2IDX(tile)].pa = tile_pa;
    }
  }
}


/** Map another tile's remote hypervisor memory into the local TLB.
 * @param tile Remote tile.
 * @param writable Nonzero if the memory should be mapped writably.
 * @return A remote memory handle, used with REMOTE_VAR() to produce lvals
 *  for remote variables.
 */
remote_handle_t
map_remote(pos_t tile, int writable)
{
  long tile_index = POS2IDX(tile);
  long page_index = n_remote_outstanding++;
  if (page_index >= HV_PDATA_NPAGES)
    panic("too many nested map_remote() calls");

  VA tile_va = HV_PDATA_VA + page_index * HV_DATA_PAGE_SIZE;
  tte_w0_t attr = remote_map_info[tile_index].attr;
  if (unlikely(!writable))
    attr.w = 0;
  install_wired_tte(attr, tile_va, remote_map_info[tile_index].pa);

  return (remote_handle_t) tile_va;
}


/** Unmap another tile's remote hypervisor memory, previously obtained via
 *  map_remote().  map_remote() - unmap_remote() pairs may be nested but
 *  must be unwound in reverse order: i.e., a = map(); b = map(); unmap(b);
 *  unmap(a).
 * @param rh Remote memory handle previously returned from map_remote().
 */
void
unmap_remote(remote_handle_t rh)
{
  long page_index = ((uintptr_t) rh  - HV_PDATA_VA) / HV_DATA_PAGE_SIZE;
  if (page_index != --n_remote_outstanding)
    panic("too many or misordered unmap_remote() calls");

  remove_wired_tte();
}


/** Format an array of statistics elements into a string buffer.
 * @param array Pointer to the statistics array.
 * @param num_entries Number of elements in the array.
 * @param clear If nonzero, clear entries instead of dumping them.
 * @param type Character printed at the head of the output line to denote
 *  the type of entry.
 * @param names Array of names for each element of the array.
 * @param out Pointer to the address of the string buffer.  This will be
 *  incremented by the number of bytes that would have been written to the
 *  buffer if it had been able to receive the entire output string.
 * @param outlen Pointer to the length of the string buffer.  No more bytes
 *  than this will be written to the buffer; this value will be decremented
 *  by the number of bytes that would have been written to the buffer if it
 *  had been able to receive the entire output string.
 * @return Number of bytes, including the trailing NUL, that would have
 *  been written to the string buffer if it had been large enough to fit all
 *  of the bytes needed; if this is larger than outlen, the string was
 *  truncated.
 */
static size_t
dump_stats_array(struct hv_stats* ptr, int num_entries, int clear, char type,
                 const char* const* names, char** out, size_t *outlen)
{
  int byteswritten = 0;

  if (clear)
    memset(ptr, 0, sizeof (*ptr) * num_entries);
  else
  {
    for (int i = 0; i < num_entries; i++, ptr++)
    {
      if (ptr->num_events)
      {
        int len = snprintf(*out, *outlen,
                           "      %c %-25s %14ld %14ld %14ld\n", type, names[i],
                           ptr->num_events, ptr->tot_cycles / ptr->num_events,
                           ptr->max_cycles);
        if (len > 0)
        {
          size_t incrlen = min(len, *outlen);
          *out += incrlen;
          *outlen -= incrlen;
          byteswritten += len;
        }
      }
    }
  }

  return byteswritten;
}

/** Return a string containing statistics for a tile.
 * @param str String into which to write statistics.
 * @param len Length of string.  This routine will write no more bytes
 *  than this; note that if the string overflows, it will not contain
 *  a trailing NUL.
 * @param x Tile X coordinate.
 * @param y Tile Y coordinate.
 * @param clear If nonzero, clear entries instead of dumping them.
 * @return Number of bytes written.
 */
size_t
get_stats_string(char* str, size_t len, int x, int y, int clear)
{
  size_t rv = 0;
  remote_handle_t rh = map_remote((pos_t) {.bits.x = x, .bits.y = y}, clear);

  if (!clear)
  {
    int hdrlen = snprintf(str, len, "# (%d,%d) %-25s %14s %14s %14s\n",
                          x, y, "Name", "Events", "Mean cyc/evt",
                          "Max cyc/evt");
    if (hdrlen > 0)
    {
      size_t incrlen = min(hdrlen, len);
      str += incrlen;
      len -= incrlen;
      rv += hdrlen;
    }
  }

  //
  // Interrupt stats.
  //
  extern struct hv_stats intr_stats[];
  rv += dump_stats_array(REMOTE_VAR(rh, intr_stats), NUM_INTERRUPTS,
                         clear, 'I', int_names, &str, &len);

  //
  // Message stats.
  //
  MSG_NAME_TBL

  rv += dump_stats_array(REMOTE_VAR(rh, msg_stats), HV_MAX_TAG + 1,
                         clear, 'M', msg_names, &str, &len);

  //
  // Syscall stats.
  //
  extern struct hv_stats syscall_stats[];
  extern const char* const syscall_names[];

  rv += dump_stats_array(REMOTE_VAR(rh, syscall_stats), HV_MAX_SYSCALL + 1,
                         clear, 'S', syscall_names, &str, &len);

  unmap_remote(rh);

  return rv;
}

/** Dump hypervisor statistics to the console.
 * @param clear If nonzero, clear statistics instead of dumping them.
 */
void
dump_hv_stats(int clear)
{
  if (!config.stats)
  {
    printf("\nHypervisor statistics are not enabled.\n");
    return;
  }

  //
  // Start off with a newline in case the cursor wasn't at the left margin
  // when we were called.
  //
  printf("\n");

  char buf[16 * 1024];

  //
  // Go through each tile in our client, then collect and print its stats.
  //
  for (int y = client_ulhc.bits.y; y <= client_lrhc.bits.y; y++)
  {
    for (int x = client_ulhc.bits.x; x <= client_lrhc.bits.x; x++)
    {
      if (in_tile_mask(&client_tiles, (pos_t){ .bits.x = x, .bits.y = y}))
      {
        int len = get_stats_string(buf, sizeof (buf) - 1, x, y, clear);

        if (!clear)
        {
          buf[len] = '\0';
          putstr(buf);
          putstr("\n");
        }
      }
    }
  }
  if (clear)
    printf("Hypervisor statistics cleared.\n");
}


/** Adjust any PC-relative references in an instruction bundle so that we
 *  can move it to another location and have it do the same thing as it
 *  did in the original location.  Note that we assume the bundle is
 *  moving, but that anything it references is not moving.
 *
 *  Note that this routine does not check to see if you've moved the bundle
 *  so far that it's impossible to describe any references within the
 *  limits of the available offset; one could argue that we should do so.
 *
 * @param bundle Original instruction bundle.
 * @param orig_addr Pointer to where the bundle came from.
 * @param new_addr Pointer to where the bundle is going.
 * @return Adjusted bundle.
 */
static tilegx_bundle_bits
adjust_pcrel(tilegx_bundle_bits bundle, tilegx_bundle_bits* orig_addr,
             tilegx_bundle_bits* new_addr)
{
  //
  // Must be X format to contain a jump or conditional branch.
  //
  if (get_bundle_mode(bundle) == X_MODE)
  {
    switch (get_Opcode_X1(bundle))
    {
    case JUMP_OPCODE_X1:
      {
        intptr_t orig_offset = get_JumpOff_X1(bundle);
        tilegx_bundle_bits orig_field = create_JumpOff_X1(orig_offset);

        intptr_t new_offset = orig_offset + (orig_addr - new_addr);
        tilegx_bundle_bits new_field = create_JumpOff_X1(new_offset);

        bundle ^= orig_field ^ new_field;

        break;
      }

    case BRANCH_OPCODE_X1:
      {
        intptr_t orig_offset = get_BrOff_X1(bundle);
        tilegx_bundle_bits orig_field = create_BrOff_X1(orig_offset);

        intptr_t new_offset = orig_offset + (orig_addr - new_addr);
        tilegx_bundle_bits new_field = create_BrOff_X1(new_offset);

        bundle ^= orig_field ^ new_field;

        break;
      }
    }
  }
  return bundle;
}


/** Create a bundle containing only a jump instruction.
 * @param bundle_addr Where the bundle will reside.
 * @param target_addr Where the bundle should jump to.
 * @return New bundle.
 */
static tilegx_bundle_bits
create_jump(tilegx_bundle_bits* bundle_addr, tilegx_bundle_bits* target_addr)
{
  //
  // We put a nop in X0.
  //
  tilegx_bundle_bits nop_x0 =
    create_Opcode_X0(RRR_0_OPCODE_X0) |
    create_RRROpcodeExtension_X0(UNARY_RRR_0_OPCODE_X0) |
    create_UnaryOpcodeExtension_X0(FNOP_UNARY_OPCODE_X0);

  //
  // And the jump in X1.
  //
  tilegx_bundle_bits j_x1 =
    create_Opcode_X1(JUMP_OPCODE_X1) |
    create_JumpOpcodeExtension_X1(J_JUMP_OPCODE_X1) |
    create_JumpOff_X1(target_addr - bundle_addr);

  return nop_x0 | j_x1;
}


/** Modify an instruction bundle in memory.
 * @param bundle_addr Where the bundle will reside.
 * @param bundle The new bundle.
 */
static void
patch_bundle(tilegx_bundle_bits* bundle_addr, tilegx_bundle_bits bundle)
{
  *bundle_addr = bundle;
  __insn_icoh(bundle_addr);
  __insn_flush(bundle_addr);
}


/** Patch hypervisor code at runtime.  This routine implements a set of
 *  patches generated by the assembly macros in patch.h.  Currently we do
 *  not support doing any remote cache flushing of patched instructions;
 *  thus, to ensure that all patches are properly observed by all tiles,
 *  you must run this from the master boot tile before starting any slave
 *  tiles.
 * @param table Table of patches to perform, generated by the
 *  assembly-language patch_XXX macros.
 */
void
patch(struct patch_table_entry* table)
{
  //
  // Mask all interrupts while we're patching, since otherwise we could
  // try to execute code that's only half-patched, leading to chaos.
  //
  uint_reg_t save_intr = __insn_mfspr(INTERRUPT_MASK_HV);
  __insn_mtspr(INTERRUPT_MASK_HV, ~0ULL);

  text_set_writable(1);

  for (struct patch_table_entry* p = table; p->unpatched_code; p++)
  {
    switch (p->patch_mode)
    {
    case PATCH_MODE_REPLACE:
      //
      // Replace mode.
      //
      if (p->replaced_bundles == 1 && p->patch_bundles == 1)
      {
        //
        // Special case: if we're doing a replacement of just one bundle,
        // we copy the patch code over the top of the original code.  We
        // need to tweak the copied instructions to adjust PC-relative
        // offsets.
        //
        // Note that you could theoretically do this whenever we had the
        // same number of bundles in both original and patch, but then you
        // have to look at any control transfers to figure out if they're
        // jumping outside the patch, or not, to figure out if you want to
        // relocate them.  It turns out that the only things we have at the
        // moment where this could apply are 1-bundle replacements, so we
        // don't bother; things will work properly for larger replacements,
        // they just won't be as optimized as they could be.
        //
        // (In fact, if you wanted to optimize, you could also handle cases
        // where the patch is shorter than the original code; you'd have to
        // replace the end of the original node with nops, or more likely a
        // nop if there was one extra instruction and a jump if there were
        // more than one.)
        //
        for (int i = 0; i < p->replaced_bundles; i++)
          patch_bundle(&p->unpatched_code[i],
                       adjust_pcrel(p->patch_code[i], &p->patch_code[i],
                                    &p->unpatched_code[i]));
      }
      else
      {
        //
        // Otherwise, replace the first bundle of the original code with
        // a jump to the first bundle of the patch code.  The patch code
        // already has a trailing jump to the code after the original code.
        //
        patch_bundle(p->unpatched_code,
                     create_jump(p->unpatched_code, p->patch_code));
      }
      break;

    case PATCH_MODE_INSERT_BEFORE:
      //
      // Insert before mode.  Here, we copy the bundle at the insertion
      // point to the last bundle of the patch code (which is currently a
      // nop).  Then we replace it with a jump to the patch code.
      //
      patch_bundle(&p->patch_code[p->patch_bundles],
                   adjust_pcrel(*p->unpatched_code, p->unpatched_code,
                                &p->patch_code[p->patch_bundles]));
      patch_bundle(p->unpatched_code,
                   create_jump(p->unpatched_code, p->patch_code));

      break;

    case PATCH_MODE_INSERT_AFTER:
      //
      // Insert after mode.  Here, we copy the bundle before the insertion
      // point to the first bundle of the patch code (which is currently a
      // nop).  Then we replace it with a jump to the patch code.
      //
      patch_bundle(p->patch_code,
                   adjust_pcrel(*p->unpatched_code, p->unpatched_code,
                                p->patch_code));
      patch_bundle(p->unpatched_code,
                   create_jump(p->unpatched_code, p->patch_code));

      break;
    }
  }

  text_set_writable(0);

  __insn_mtspr(INTERRUPT_MASK_HV, save_intr);
}
