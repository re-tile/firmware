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
 * Bare Metal Environment interrupt support.
 */

#include <stdio.h>
#include <util.h>

#include <arch/chip.h>
#include <arch/interrupts.h>
#include <arch/spr.h>

#include <bme/interrupts.h>
#include <bme/sys_info.h>
#include <bme/tlb.h>

#include "bits.h"
#include "bme_pl.h"
#include "bme_state.h"


int 
bme_install_interrupt_handler(int interrupt, bme_interrupt_handler_t* func)
{
  if (interrupt >= NUM_INTERRUPTS)
  {
    panic("Interrupt %d out of range\n", interrupt);
  }

  _bme_state_t* statep =_bme_get_state();

  statep->int_handler[interrupt] = func;

  return 0;
}

int
bme_uninstall_interrupt_handler(int interrupt)
{
  bme_install_interrupt_handler(interrupt, bme_bad_intr);
  return 0;
}

void
bme_mask_interrupt(int interrupt)
{
    __insn_mtspr(INTERRUPT_MASK_SET_BME, 1ULL << interrupt);
}


void
bme_unmask_interrupt(int interrupt)
{
    __insn_mtspr(INTERRUPT_MASK_RESET_BME, 1ULL << interrupt);
}

void
bme_dump_saved_regs(struct bme_saved_regs *sr)
{
  tprintf("Saved register dump:\n");

  for (int i = 0; i < 28; i += 4)
    tprintf("r%-2d 0x%016x  r%-2d 0x%016x  r%-2d 0x%016x  r%-2d 0x%016x\n",
            i, sr->r29_to_r0[29 - i], i + 1, sr->r29_to_r0[28 - i],
            i + 2, sr->r29_to_r0[27 - i], i + 3, sr->r29_to_r0[26 - i]);

  tprintf("r28 0x%016x  r29 0x%016x  lr  0x%016x  sp  0x%016x\n",
          sr->r29_to_r0[1], sr->r29_to_r0[0], sr->lr, sr->sp);
  tprintf("im0 0x%016x  im1 0x%016x  ex0 0x%016x  ex1 0x%016x\n",
          sr->intmask_0, sr->intmask_1, sr->ex_context_0, sr->ex_context_1);
}


void
bme_dump_saved_regs_full(struct bme_saved_regs_full *sr)
{
  tprintf("Full saved register dump:\n");

  for (int i = 0; i < 52; i += 4)
    tprintf("r%-2d 0x%016x  r%-2d 0x%016x  r%-2d 0x%016x  r%-2d 0x%016x\n",
            i, sr->r53_to_r0[53 - i], i + 1, sr->r53_to_r0[52 - i],
            i + 2, sr->r53_to_r0[51 - i], i + 3, sr->r53_to_r0[50 - i]);

  tprintf("r52 0x%016x  tp  0x%016x  lr  0x%016x  sp  0x%016x\n",
          sr->r53_to_r0[1], sr->r53_to_r0[0], sr->lr, sr->sp);
  tprintf("im0 0x%016x  im1 0x%016x  ex0 0x%016x  ex1 0x%016x\n",
          sr->intmask_0, sr->intmask_1, sr->ex_context_0, sr->ex_context_1);
}


/** Interrupt number to name translation. */
const char* const bme_int_names[] =
{
  "Memory Error",               /* 0 */
  "Single Step 3",              /* 1 */
  "Single Step 2",              /* 2 */
  "Single Step 1",              /* 3 */
  "Single Step 0",              /* 4 */
  "IDN Complete",               /* 5 */
  "UDN Complete",               /* 6 */
  "ITLB Miss",                  /* 7 */
  "Illegal Insruction",         /* 8 */
  "GPV",                        /* 9 */
  "IDN Access",                 /* 10 */
  "UDN Access",                 /* 11 */
  "Software Interrupt 3",       /* 12 */
  "Software Interrupt 2",       /* 13 */
  "Software Interrupt 1",       /* 14 */
  "Software Interrupt 0",       /* 15 */
  "Illegal Translation",        /* 16 */
  "Unaligned Data",             /* 17 */
  "DTLB Miss",                  /* 18 */
  "DTLB Access",                /* 19 */
  "IDN Firewall",               /* 20 */
  "UDN Firewall",               /* 21 */
  "Tile Timer",                 /* 22 */
  "Auxillary Tile Timer",       /* 23 */
  "IDN Timer",                  /* 24 */
  "UDN Timer",                  /* 25 */
  "IDN Available",              /* 26 */
  "Udn Available",              /* 27 */
  "IPI 3",                      /* 28 */
  "IPI 2",                      /* 29 */
  "IPI 1",                      /* 30 */
  "IPI 0",                      /* 31 */
  "Performance Counter",        /* 32 */
  "Auxillary Performance Counter", /* 33 */
  "INTCTRL 3",                  /* 34 */
  "INTCTRL 2",                  /* 35 */
  "INTCTRL 1",                  /* 36 */
  "INTCTRL 0",                  /* 37 */
  "Boot Access",                /* 38 */
  "World Access",               /* 39 */
  "I ASID",                     /* 40 */
  "D ASID",                     /* 41 */
  "Double Fault",               /* 42 */
};


void
bme_bad_intr(int int_number, struct bme_saved_regs_full *sr)
{
#if 1
  // FIXME: needs to be ported
  panic("bme_warning: bad interrupt %d\n", int_number);
#else
  switch (int_number)
  {
    case INT_MEM_ERROR:
    {
      uint_reg_t sbox_error = __insn_mfspr(SPR_SBOX_ERROR);
      uint_reg_t cbox_status = __insn_mfspr(SPR_MEM_ERROR_CBOX_STATUS);
      uint_reg_t mbox_status = __insn_mfspr(SPR_MEM_ERROR_MBOX_STATUS);
      uint_reg_t xdn_status = __insn_mfspr(SPR_XDN_DEMUX_ERROR);

      if (cbox_status & SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_CORRECTED_MASK)
      {
        //
        // If there is an L2_DATA_CORRECTED ECC error, print a message, 
        // finv the particular cacheline to recover, and finally clear the error
        // status bit.
        // 
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_CBOX_ADDR);
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
          .physical_memory_mode = SPR_AAR__PHYSICAL_MEMORY_MODE_MASK,
        }};
          
	//
	// Finv the whole corrupt cacheline using the physical address.
	//
        phys_finv(physaddr, aar.word);

	//
	// Clear corresponding error status (w1tc).
	//
        __insn_mtspr(SPR_MEM_ERROR_CBOX_STATUS, 
                     SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_CORRECTED_MASK);
        __insn_mf();
      }

      if (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__L1_D_TAG_MASK)
      {
        //
        // If there is an L1_D_TAG parity error, print a message,
        // do cacheline replacement flushing to recover, and finally clear the 
        // error status bit.
        //
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_MBOX_ADDR);
        tprintf("hv_warning: L1D$ tag parity error at PA %#llx\n",
                physaddr);

        physaddr &= ~(CHIP_L1D_LINE_SIZE() - 1);
 
	//
	// Physical address used by the new allocated TTE.
	//
	PA page_pa = ROUND_DN(physaddr, HV_FLUSH_PAGE_SIZE);
	PA offset = physaddr - page_pa;

	//
	// Virtual address used by the new allocated TTE.
	//
	VA flush_va = HV_FLUSH_VA + offset;

        //
	// Disable the dstream prefetcher before doing a cache flush; otherwise,
        // we could end up with data in the cache that we don't want there.
	//
	uint_reg_t old_dstream_pf = __insn_mfspr(SPR_DSTREAM_PF);
	__insn_mtspr(SPR_DSTREAM_PF, 0);
        __insn_mf();

        //
  	// We need to temporarily stuff a new entry into the TLB to do our
  	// eviction.  We pick the first unwired entry.  We also save the old
  	// entry and restore it afterwards. 
        //
  	int wired_idx = __insn_mfspr(SPR_WIRED_DTLB);
        tte_t old_tte = READ_TLB_AT(D, wired_idx);
  
  	tte_t tte = {
    	  .w0 = 
    	  {{
      	    .ps = TTE_SHIFT_TO_PS(HV_FLUSH_PAGE_SHIFT),
            .g = 1,
      	    .asid = 0,
      	    .v = 1,
      	    .w = 1,
      	    .pin = 1,
      	    .mpl = HV_PL,
      	    .memory_attribute =  
              SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
      	    .cache_home_mapping =  
              SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
      	    .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
      	    .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
    	  }},
    	  .w1 = { .word = HV_FLUSH_VA },
    	  .w2 = { .word = page_pa }
        };

        WRITE_TLB_AT(D, wired_idx, tte);
        __insn_mf();

	//
	// Do dummy reads to evict all possible corrupt cachelines.
	// The stride size between different ways is 16KB.
        // 	
        for (int i = 0; i < CHIP_L1D_ASSOC(); i++)
	{
          flush_va += i * CHIP_L1D_CACHE_SIZE() / CHIP_L1D_ASSOC();  
          __insn_wh64((void*) flush_va);
	  *(volatile char*) (flush_va);
	  __insn_inv((void*) flush_va);
          __insn_mf();
        }
	
  	//
  	// Restore the old TLB entry.
  	//
        WRITE_TLB_AT(D, wired_idx, old_tte);

        //
        // Clear corresponding error status (w1tc).
        //
        __insn_mtspr(SPR_MEM_ERROR_MBOX_STATUS,
                     SPR_MEM_ERROR_MBOX_STATUS__L1_D_TAG_MASK);
        
	//
	// Reenable the prefetcher.
        //
	__insn_mtspr(SPR_DSTREAM_PF, old_dstream_pf);
        __insn_mf();
      }

      if (mbox_status & SPR_MEM_ERROR_MBOX_STATUS__L1_D_DATA_MASK)
      {
        //
        // If there is an L1_D_DATA parity error, print a message,
        // finv the particular cacheline to recover, and finally clear the error
        // status bit.
        //
        PA physaddr = __insn_mfspr(SPR_MEM_ERROR_MBOX_ADDR);
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
          .physical_memory_mode = SPR_AAR__PHYSICAL_MEMORY_MODE_MASK,
        }};

        //
        // Finv the whole corrupt cacheline using the physical address.
        //
        phys_finv(physaddr, aar.word);

        //
        // Clear corresponding error status (w1tc).
        //
        __insn_mtspr(SPR_MEM_ERROR_MBOX_STATUS,
                     SPR_MEM_ERROR_MBOX_STATUS__L1_D_DATA_MASK);
        __insn_mf();
      }

      if (sbox_error & SPR_SBOX_ERROR__L1_I_MASK)
      {
        //
        // If there is an L1I$ data parity error, print a message, 
        // icoh the corrupt data by index to recover, and finally clear the 
        // error status bit.
        //
        uint64_t index = (sbox_error & SPR_SBOX_ERROR__INDEX_MASK) << 3;
        tprintf("hv_warning: L1I$ parity error at index %#llx\n", index);

	//
	// Evict the corrupt cacheline out by icoh.
	//
        __insn_icoh((void*) index);
        __insn_drain();

        //
        // Clear corresponding error status (w1tc).
        //
        __insn_mtspr(SPR_SBOX_ERROR, SPR_SBOX_ERROR__L1_I_MASK);
        __insn_mf();
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
        
          // 
          // Clear corresponding error status (w1tc).
          // 
          __insn_mtspr(SPR_MEM_ERROR_CBOX_STATUS,
                       SPR_MEM_ERROR_CBOX_STATUS__INT_L2_RDN_READ_MASK);
          __insn_mf();

          return;
        }
     
        //
        // Otherwise, just panic.
        // 
        panic_start(
          "got read error response on RDN interrupt: PC %#lX, ICS/PL %#lx", 
          sr->ex_context_0, sr->ex_context_1);
        dump_saved_regs_full(sr);
        panic_end();
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

          //
          // Clear corresponding error status (w1tc).
          //
          __insn_mtspr(SPR_MEM_ERROR_CBOX_STATUS,
                       SPR_MEM_ERROR_CBOX_STATUS__INT_L2_RDN_WRITE_MASK);
          __insn_mf();

          return;
        }

        //
        // Otherwise, just panic.
        //
        panic_start(
          "got write error ack on RDN interrupt: PC %#lX, ICS/PL %#lx", 
          sr->ex_context_0, sr->ex_context_1);
        dump_saved_regs_full(sr);
        panic_end();
      }

      if (mbox_status & 
          SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_ATOMIC_ATTRIBUTE_MASK)
      {
        int pl = sr->ex_context_1 & SPR_EX_CONTEXT_2_1__PL_MASK;

        if (pl < HV_PL)
        {
          //
          // If the interrupt happened at or below the client's PL, then we want
          // the supervisor to take care of it, so we don't print any messages; 
          // we just run the supervisor interrupt handler as if it was called 
          // directly.
          //
          handle_interrupt_downcall(INT_MEM_ERROR, sr);

          //
          // Clear corresponding error status (w1tc).
          //
          __insn_mtspr(SPR_MEM_ERROR_MBOX_STATUS,
            SPR_MEM_ERROR_MBOX_STATUS__ILLEGAL_ATOMIC_ATTRIBUTE_MASK);
          __insn_mf();

          return;
        }

        //
        // Otherwise, just panic.
        //
        panic_start(
          "got illegal atomic attribute interrupt: PC %#lX, ICS/PL %#lx", 
          sr->ex_context_0, sr->ex_context_1);
        dump_saved_regs_full(sr);
        panic_end();
      }

      int fatal_error =
        (mbox_status & (SPR_MEM_ERROR_MBOX_STATUS__DTLB_MULTI_MATCH_MASK |
                        SPR_MEM_ERROR_MBOX_STATUS__OVERFLOW_MASK)) ||
        (cbox_status & (SPR_MEM_ERROR_CBOX_STATUS__L2_DATA_FATAL_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_TAG_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_STATE_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__L2_MAF_TIMEOUT_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__SHARE_INVALIDATION_MASK |
                        SPR_MEM_ERROR_CBOX_STATUS__OVERFLOW_MASK)) ||
        (sbox_error & SPR_SBOX_ERROR__ITLB_MULTI_MATCH_MASK);

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

      if (fatal_error)
      {
        //
        // Fatal errors should go into panic.
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
        utprintf("XDN_DEMUX_ERROR:       0x%'019lx\n", xdn_status);

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
        dump_itlb(0);
        dump_dtlb(0);
        panic_end();
      }

      return;

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

      if (pl == 0)
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
        __insn_mtspr(SYSTEM_SAVE_CL_2, __insn_mfspr(EX_CONTEXT_CL_0));
        __insn_mtspr(SYSTEM_SAVE_CL_3, __insn_mfspr(EX_CONTEXT_CL_1));
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
      panic_start("got illegal instruction interrupt: PC %#lX, ICS/PL %#lx\n",
                  sr->ex_context_0, sr->ex_context_1);
      //
      // Loading from the fault PC is a bit sketchy when ILL_DEBUG is defined,
      // since in that case it could be a client VA which might or might not
      // actually be mapped in the DTLB.  That's not the common case, though.
      //
      uint32_t *iptr = (uint32_t *) sr->ex_context_0;
      tprintf("       bundle at PC: %016x%016x before finv\n", iptr[1],
              iptr[0]);
      __insn_finv(&iptr[0]);
      __insn_finv(&iptr[1]);
      tprintf("       bundle at PC: %016x%016x after finv\n", iptr[1],
              iptr[0]);
      dump_saved_regs_full(sr);
      dump_itlb(0);
      dump_dtlb(0);
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
#endif
}
