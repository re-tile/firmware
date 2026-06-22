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
 * Routines to do miscellaneous operations.
 */

#ifndef _SYS_HV_MISC_H
#define _SYS_HV_MISC_H

#include <stddef.h>

#include <arch/rsh.h>

//
// Including this on Pro breaks the hvgdb build, and we don't need it there.
//
#include "cfg.h"

#include "param.h"
#include "patch.h"
#include "sromboot.h"
#include "tte.h"
#include "types.h"

/** Caller-saved registers from the thread of execution which incurred an
 *  interrupt.
 */
struct saved_regs
{
  unsigned long intmask;         /**< Previous interrupt mask */



  unsigned long ex_context_0;    /**< EX_CONTEXT_HV_0 after interrupt */
  unsigned long ex_context_1;    /**< EX_CONTEXT_HV_1 after interrupt */
  unsigned long r29_to_r0[30];   /**< Saved registers (array index 0 is r29,
                                  *   array index 1 is r28, and so forth) */

  unsigned long idn_avail;       /**< Previous value of IDN_AVAIL SPR */

  unsigned long lr;              /**< Saved lr */
  unsigned long sp;              /**< Saved sp */
};

/** All registers from the thread of execution which incurred an interrupt.
 */
struct saved_regs_full
{
  unsigned long intmask;         /**< Previous interrupt mask */



  unsigned long ex_context_0;    /**< EX_CONTEXT_HV_0 after interrupt */
  unsigned long ex_context_1;    /**< EX_CONTEXT_HV_1 after interrupt */
  unsigned long r53_to_r0[54];   /**< Saved registers (array index 0 is r53,
                                  *   array index 1 is r52, and so forth) */
  unsigned long lr;              /**< Saved lr */
  unsigned long sp;              /**< Saved sp */
};

/** All registers from the thread of execution which made a syscall.
 */
struct saved_regs_syscall
{
  unsigned long intmask;         /**< Previous interrupt mask */



  unsigned long ex_context_0;    /**< EX_CONTEXT_HV_0 after interrupt */
  unsigned long ex_context_1;    /**< EX_CONTEXT_HV_1 after interrupt */
  unsigned long lr;              /**< Saved lr */
  unsigned long sp;              /**< Saved sp */
};

/** Fault mapping structure, used to handle faults in fast hypervisor I/O
 *  routines. */
typedef struct
{
  /** PC which has a potentially faulting load/store instruction. */
  unsigned long fault_pc;
  /** PC to which we'll return if we take a fault on the PC in fault_pc. */
  unsigned long recovery_pc;
} fault_map_t;

/** Top of stack; struct saved_regs_xxx right below this point. */
extern char _estack[];

/** Start of the collected set of fault map structures. */
extern fault_map_t fault_map_start[];
/** End of the collected set of fault map structures. */
extern fault_map_t fault_map_end[];

extern const char* const int_names[];  /**< Array of interrupt names, indexed
                                            by interrupt number */

void flush_range(VA va, size_t len);
void finv_range(VA va, size_t len);
void dump_saved_regs(struct saved_regs *sr);
void dump_saved_regs_full(struct saved_regs_full *sr);
void bad_intr(int int_number, struct saved_regs_full *sr);
void reset_chip(uint32_t flags) __attribute__((noreturn));
void mf_incoherent(void);
void jump2_vaplsp_mappings(VA pc, int pl, VA sp, VA arg0,
                           const tte_t itte[], int n_itte,
                           const tte_t dtte[], int n_dtte);

/** Invalidate part of the L2 cache via displacement flush.
 * @param va Virtual address of the block to read.
 * @param len Length of the block to read.
 */
void disp_flush(VA va, size_t len);

/** Flush the whole I-cache. */
void flush_icache(void);

/** Jump to a virtual address at specified PL.
 * @param pc Virtual address to jump to.
 * @param pl Protection level to jump to.
 */
void jump2_vapl(VA pc, int pl) __attribute__((__noreturn__));

/** Jump to a virtual address at specified PL, and use a specified SP.
 * @param pc Virtual address to jump to.
 * @param pl Protection level to jump to.
 * @param sp Value to load into the stack pointer.
 */
void jump2_vaplsp(VA pc, int pl, VA sp) __attribute__((__noreturn__));

/** Probe the DTLB.
 * @param va Address to probe.
 * @return Bitmask of DTLB entries which match the given address.
 */
unsigned long dtlb_probe(VA va);

/** Do an I/O configuration write which is expected to reset the chip, and
 *  set up the registers that inform the SROM booter of a soft reboot action.
 *  Note that this routine should not be called directly; call reset_chip() or
 *  boot_reset_chip() instead, which do some necessary housekeeping first.
 * @param dest Destination (x,y in the hardware packet header format; the
 *        remainder of the word must be zero).  The FB bit is always set
 *        on the sent message.
 * @param chan Channel to target on the shim.
 * @param addr Register number within the channel.
 * @param data Data item to write.
 * @param sromboot_flags SROM booter soft reboot action.
 */
void cfg_wr_reset(uint32_t dest, uint32_t chan, uint32_t addr, uint32_t data,
                  uint32_t sromboot_flags) __attribute__((__noreturn__));

/** Reset the chip and set up the registers that inform the SROM booter of a
 *  soft reboot action.  Note that this routine should not be called directly;
 *  call reset_chip() or boot_reset_chip() instead, which do some necessary
 *  housekeeping first.
 * @param rshim_addr Destination address of the rshim.
 * @param sromboot_flags SROM booter soft reboot action.
 */
static inline void do_soft_reset(pos_t rshim_addr, uint32_t sromboot_flags)
  __attribute__((__noreturn__));
static inline void do_soft_reset(pos_t rshim_addr, uint32_t sromboot_flags)
{
  uint_reg_t val = cfg_rd(rshim_addr.word, 0, RSH_BREADCRUMB0);
  val &= ~(uint_reg_t) SROMBOOT_BREADCRUMB0_MASK;
  val |= sromboot_flags;
  cfg_wr(rshim_addr.word, 0, RSH_BREADCRUMB0, val);
  cfg_wr(rshim_addr.word, 0, RSH_RESET_CONTROL,
         RSH_RESET_CONTROL__RESET_CHIP_VAL_KEY);
  //
  // This spin won't happen, but otherwise the compiler complains that
  // we're returning from this function when we said we wouldn't.
  //
  while (1)
    ;
}

/** Set the minimum protection levels for the exceptions to be
 * handled by the client.
 */
void set_client_mpls(void);

/** Enable MMIO mappings for client IPIs. */
void setup_client_ipi_access(void);

void global_exit(int status);


/** Handle representing a mapping to another tile's private memory. */
typedef void* remote_handle_t;

void init_map_remote(void);
remote_handle_t map_remote(pos_t tile, int writable);
void unmap_remote(remote_handle_t rh);

/** Return a reference to (i.e., an lvalue for) the var named by var in the
 *  remote tile represented by handle. */
#define REMOTE_VAR(handle, var) \
  (*(__typeof (&(var))) (handle + (uintptr_t) &(var) - (uintptr_t) _sstack))

/** Convert a pointer in a remote tile's address space to one in the current
 *  tile's address space. */
#define REMOTE_PTR(handle, ptr) \
  ((__typeof(ptr)) (handle + (uintptr_t) ptr - (uintptr_t) _sstack))

void dump_hv_stats(int clear);
size_t get_stats_string(char* str, size_t len, int x, int y, int clear);

/** Entry in a hypervisor statistics table. */
struct hv_stats
{
  /** Total cycles spent dealing with this event. */
  long tot_cycles;
  /** Maximum number of cycles spent dealing with one instance of this event. */
  long max_cycles;
  /** Number of events seen. */
  long num_events;
};

void patch(struct patch_table_entry* table);

/** Patch table for the hypervisor statistics feature. */
extern struct patch_table_entry patch_table_stats[];

#endif /* _SYS_HV_MISC_H */
