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
 *
 * Bare Metal Environment interrupt support.  The BME runtime library provides
 * a set of interrupt vectors, which are automatically linked in with the BME
 * executable.  The vectors are mapped into the application's virtual address
 * space at the interrupt base virtual address, so that they are invoked when
 * interrupts occur.  When executed, these vectors call a C-language interrupt
 * handler.  The default interrupt handler prints an error message with
 * processor state information, and then terminates the application.  This
 * default handler may be replaced by a function provided by the application,
 * by using the routines defined in this file; if desired, a different handler
 * may be supplied for each type of interrupt.
 *
 * If you wish to implement your own assembly-language interrupt routines, you
 * are currently advised to modify the statically-compiled set of interrupt
 * vectors by modifying the BME runtime source (src/sys/bme/intvec.S).  Future
 * releases of the BME may provide more flexible methods for installing
 * user-developed assembly-language interrupt handlers.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_INTERRUPTS_H
#define _SYS_BME_INTERRUPTS_H

#include <features.h>
#include <stdint.h>

__BEGIN_DECLS

/** Caller-saved registers from the thread of execution that incurred an
 *  interrupt.
 */
struct bme_saved_regs
{
  uint32_t intmask_1;           /**< Previous interrupt mask word 1 */
  uint32_t intmask_0;           /**< Previous interrupt mask word 0 */
  uint32_t ex_context_0;        /**< EX_CONTEXT_BME_0 after interrupt */
  uint32_t ex_context_1;        /**< EX_CONTEXT_BME_1 after interrupt */
  uint32_t r29_to_r0[30];       /**< Saved registers (array index 0 is r29,
                                 *   array index 1 is r28, and so forth) */
  uint32_t lr;                  /**< Saved link (return address) register */
  uint32_t sp;                  /**< Saved stack pointer */
};

/** All registers from the thread of execution that incurred an interrupt.
 */
struct bme_saved_regs_full
{
  uint32_t intmask_1;           /**< Previous interrupt mask word 1 */
  uint32_t intmask_0;           /**< Previous interrupt mask word 0 */
  uint32_t ex_context_0;        /**< EX_CONTEXT_BME_0 after interrupt */
  uint32_t ex_context_1;        /**< EX_CONTEXT_BME_1 after interrupt */
  uint32_t r53_to_r0[54];       /**< Saved registers (array index 0 is r53,
                                 *   array index 1 is r52, and so forth) */
  uint32_t lr;                  /**< Saved link (return address) register */
  uint32_t sp;                  /**< Saved stack pointer */
};

/** Dump out a set of saved registers.
 * @param sr Pointer to the saved register structure.
 */
void bme_dump_saved_regs(struct bme_saved_regs *sr);

/** Dump out a full set of saved registers.
 * @param sr Pointer to the saved register structure.
 */
void bme_dump_saved_regs_full(struct bme_saved_regs_full *sr);

/** Dump out the DMA engine's registers.  If the engine is active, we stop it
 *  before dumping, and restart it afterward.
 */
void bme_dump_dma_regs(void);

/** Handler for a bad (unimplemented) interrupt.
 * @param int_number Interrupt number.
 * @param sr Pointer to saved registers from time of interrupt.
 */
void bme_bad_intr(int int_number, struct bme_saved_regs_full *sr);

/** Base interrupt handler, calls installed handler.
 * @param int_number Interrupt number.
 * @param sr Pointer to saved registers from time of interrupt.
 */
void bme_base_intr(int int_number, struct bme_saved_regs_full *sr);

/** Array of interrupt names, indexed by interrupt number */
extern const char* const bme_int_names[];

/** Mask an interrupt at PL2.
 * @param interrupt Interrupt number.
 */
void bme_mask_interrupt(int interrupt);

/** Unmask an interrupt at PL2.
 * @param interrupt Interrupt number.
 */
void bme_unmask_interrupt(int interrupt);

/** Type for an interrupt routine. */
typedef void bme_interrupt_handler_t(int interrupt,
                                     struct bme_saved_regs_full *sr);

/** Install an interrupt handler.
 * @param interrupt Interrupt number.
 * @param func Exception handling function.
 * @return Zero if handler was successfully installed, otherwise an error
 * code.
 */
int bme_install_interrupt_handler(int interrupt,
                                  bme_interrupt_handler_t* func);

/** Remove an interrupt handler by reinstalling the default.
 * @param interrupt Interrupt number.
 * @return Zero if handler was successfully uninstalled, otherwise an error
 * code.
 */
int bme_uninstall_interrupt_handler(int interrupt);

__END_DECLS

#endif /* _SYS_BME_INTERRUPTS_H */

/** @} */
