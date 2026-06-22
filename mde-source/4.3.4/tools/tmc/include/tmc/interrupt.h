// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
//

//! @file
//!
//! Routines for installing a user-space interrupt with a specific interrupt
//! number and its user-space C callback handler.

//! @addtogroup tmc_interrupt
//! @{
//!
//! Routines for installing a user-space interrupt with a specific interrupt
//! number and its user-space C callback handler.
//! 
//! The <tmc/interrupt.h> API allows users to specify an interrupt and install
//! its corresponding user-space C callback function. This feature provides
//! much better performance than registering an interrupt via Linux, especially
//! for applications with a strict CPU cycle budget.
//! 
//! The Tile Processor&tm; has a distributed interrupt management mechanism
//! rather than a single centralized one. The <tmc/interrupt.h> API provides a
//! thread-safe way to allow installations of C callback functions among
//! different tiles even when they share a single interrupt number.
//! 
//! The assembly trampoline functions in <interrupt_asm.S> are used to save
//! and restore necessary register contexts during an interrupt, including PC,
//! SP, TP, PL, CSI and caller-saved registers. Nested interrupts are also
//! supported within tmc/interrupt lib.
//!
//! There are two types of C callback functions users can register.
//! The first type has no input argument but void as its return value.
//! The second type also has void as the return value, but two input
//! arguments: the interrupt number and the pointer of the sigcontext
//! structure.
//!
//! For example, to register a non-argument user-space C callback
//! function which binds to the interrupt vector INT_UDN_AVAIL, you
//! might do:
//!
//! @code
//! int result = tmc_interrupt_c_install(INT_UDN_AVAIL, on_udn_avail);
//! if (result != 0)
//!   tmc_task_die("Failure in 'tmc_interrupt_c_install().'");
//! @endcode
//!
//! where on_udn_avail is the C callback function which the CPU runs at user
//! level when it receives at least one inbound UDN message.
//!
//! @code
//! void
//! on_udn_avail(void)
//! {
//!   // Do something here with the UDN inbound message.
//!   ...
//!
//!   // Clear INT_UDN_AVAIL pending status.
//!   ...
//! }
//! @endcode
//!
//! Note that the user should enable the interrupt vector manually before a
//! real interrupt arrives, and also clear the interrupt's pending bit once
//! the interrupt has been serviced, to avoid permanent interrupt knocks.
//! 
//! Also note that user can use __thread variables to pass arguments to the 
//! callback function even if it has a void input argument.
//!

#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include <arch/interrupts.h>
#include <arch/spr_def.h>

#include <signal.h>

#ifndef __ASSEMBLER__
#include <features.h>
#include <stdint.h>
#include <stddef.h>

__BEGIN_DECLS

//! tmc_interrupt_func_t is the user-space C callback function which
//! can have two different types at the same time.
//! 
//! func_no_arg has no input argument but void as the return value.

//! func_two_args has void as its return value but two input arguments.
//! @param intr_num Interrupt vector number being received.
//! @param context Pointer to the sigcontext structure.
//! NOTE that only r0-r29, sp, lr and pc inside sigcontext are saved
//! properly to save time, since the other fields are either zero all
//! the time or not commonly used.  
//!

typedef union tmc_interrupt_func {

  //! Use this type if interrupt context information is not helpful.
  void (*func_no_arg)(void);

  //! Use this type if you want interrupt context information.
  void (*func_two_args)(int intr_num, struct sigcontext* context);

} __attribute__((__transparent_union__)) tmc_interrupt_func_t;

#ifndef __DOXYGEN__
// Max memory size (256 Bytes) for a single interrupt vector.
#define MAX_TRAMPOLINE_SIZE (8 * 32)

// Trampoline assembly symbols in <interrupt_asm.S>.
#ifndef __tilegx__
extern char trampoline_body_copied[];
extern char trampoline_body_copied_end[];
extern char trampoline_body_copied_mask_high_lo16[];
extern char trampoline_body_copied_mask_high_ha16[];
extern char trampoline_body_copied_mask_low_lo16[];
extern char trampoline_body_copied_mask_low_ha16[];
extern char trampoline_body_copied_c_lo16[];
extern char trampoline_body_copied_c_ha16[];
#else // __tilegx__
extern char trampoline_body_copied[];
extern char trampoline_body_copied_end[];
extern char trampoline_body_copied_mask_hw2[];
extern char trampoline_body_copied_mask_hw1[];
extern char trampoline_body_copied_mask_hw0[];
extern char trampoline_body_copied_c_hw2[];
extern char trampoline_body_copied_c_hw1[];
extern char trampoline_body_copied_c_hw0[];
#endif // __tilegx__
#endif // __DOXYGEN__

//! Function to install a C callback entry for an interrupt in user space.
//!
//! @param intr_num Interrupt vector number to register.
//! @param func User-space C callback function to bind to the interrupt.
//! @return 0 on success, or -1 on failure (and sets errno).
//!
int tmc_interrupt_c_install(int intr_num, tmc_interrupt_func_t func);

__END_DECLS

#endif // __ASSEMBLER__
#endif //__INTERRUPT_H__

//! @}
