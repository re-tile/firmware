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

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>

#include <arch/opcode.h>

#include <tmc/mem.h>
#include <tmc/spin.h>

#include "tmc/interrupt.h"


// Mutex to protect mprotect() by multiple threads.
static tmc_spin_queued_mutex_t intr_mproc_mutex = TMC_SPIN_QUEUED_MUTEX_INIT;

// Maintain state as to whether this interrupt function is already installed.
static tmc_interrupt_func_t intr_func[NUM_INTERRUPTS];


int
tmc_interrupt_c_install(int intr_num, tmc_interrupt_func_t func)
{
  // Error checking on the input parameters.
  if (intr_num < 0 || intr_num >= NUM_INTERRUPTS)
  {
    errno = EINVAL;
    return -1;
  }

  if (func.func_no_arg == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  int retval = 0;

  // Lock the interrupt memory operation.  
  tmc_spin_queued_mutex_lock(&intr_mproc_mutex);

  // Copy the function into memory if it's not already been done.
  if (intr_func[intr_num].func_no_arg != func.func_no_arg)
  {
    // Assume failure while we are copying in the interrupt function.
    retval = -1;

    // Get the actual size of trampoline_body_copied function.
    size_t size = trampoline_body_copied_end - trampoline_body_copied;

    // Get current page size.
    size_t page_size = getpagesize();

    // Get interrupt vector entry address.
    char* addr = (char*)USER_INTERRUPT_VECTOR(intr_num);

    // Get the page_size aligned address.
    void* page = (void*)((uintptr_t)addr & -page_size);
	
    // Calculate high and low parts of the interrupt mask.
    uint32_t intr_bit_high, intr_bit_low;

    // For SPR_INTERRUPT_MASK_0_1. 
    if (intr_num > 31)
    {
      intr_bit_high = INT_MASK(intr_num - 32);
      intr_bit_low = 0;
    }
    else // For SPR_INTERRUPT_MASK_0_0.
    {
      intr_bit_high = 0;
      intr_bit_low = INT_MASK(intr_num);
    }

    // By default, the OS maps these vectors as read-only.  We extend
    // this to include "writable" permissions during the update, but
    // leave it executable to avoid any potential race condition issues.
    // We assume that trampolines never cross page boundaries.
    int result = mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (result != 0)
      goto done;

    // Set zero to non-code area.
    memset(addr + size, 0, MAX_TRAMPOLINE_SIZE - size);

    // Copy asm body to interrupt vector memory.
    memcpy(addr, trampoline_body_copied, size);
  
    // Fill in lo16 of interrupt mask high part.
    tile_bundle_bits* addr_lo16 = (void*)(addr + 
                                  (trampoline_body_copied_mask_high_lo16 - 
                                  trampoline_body_copied));
    *addr_lo16 |= create_Imm16_X0((intr_bit_high << 16) >> 16);

    // Fill in ha16 of interrupt mask high part.
    tile_bundle_bits* addr_ha16 = (void*)(addr + 
                                  (trampoline_body_copied_mask_high_ha16 - 
                                  trampoline_body_copied));
    *addr_ha16 |= create_Imm16_X1((intr_bit_high + 0x8000)>> 16);
	
    // Fill in lo16 of interrupt mask low part.
    addr_lo16 = (void*)(addr + (trampoline_body_copied_mask_low_lo16 - 
                trampoline_body_copied));
    *addr_lo16 |= create_Imm16_X0((intr_bit_low << 16) >> 16);

    // Fill in ha16 of interrupt mask low part.
    addr_ha16 = (void*)(addr + (trampoline_body_copied_mask_low_ha16 - 
                trampoline_body_copied));
    *addr_ha16 |= create_Imm16_X0((intr_bit_low + 0x8000) >> 16);

    // Fill in lo16 of user C callback function entry.
    addr_lo16 = (void*)(addr + (trampoline_body_copied_c_lo16 - 
                trampoline_body_copied));
    *addr_lo16 |= create_Imm16_X0((uintptr_t)func.func_no_arg);

    // Fill in ha16 of user C callback function entry.
    addr_ha16 = (void*)(addr + (trampoline_body_copied_c_ha16 - 
                trampoline_body_copied));
    *addr_ha16 |= create_Imm16_X0(((uintptr_t)func.func_no_arg + 0x8000) >> 16);
  
    // Force cpu to reload the new instructions.
    tmc_mem_invalidate_icache(addr, MAX_TRAMPOLINE_SIZE);

    // Reset protections to read-only.
    result = mprotect(page, page_size, PROT_READ | PROT_EXEC);
    if (result != 0)
      goto done;

    // Success!
    intr_func[intr_num] = func;
    retval = 0;
  }

done:
  // Unlock the interrupt memory operation.  
  tmc_spin_queued_mutex_unlock(&intr_mproc_mutex);

  return retval;
}
