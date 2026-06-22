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
#include <stdbool.h>

#include <sys/mman.h>
#include <arch/opcode.h>

#include <tmc/mem.h>
#include <tmc/spin.h>

#include "tmc/interrupt.h"


// Mutex for mmap() by multiple threads.
static tmc_spin_queued_mutex_t intr_mmap_mutex = TMC_SPIN_QUEUED_MUTEX_INIT;

// Base address returned by mmap().
static char* addr_base = NULL;

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

  // Assume failure.
  int retval = -1;

  // Lock the interrupt memory operation.  
  tmc_spin_queued_mutex_lock(&intr_mmap_mutex);

  if (intr_func[intr_num].func_no_arg != func.func_no_arg)
  {
    // Get current page size.
    size_t page_size = getpagesize();

    // Check whether we are the 1st time touching mmap(). 
    bool first_time = (addr_base == NULL);

    if (first_time)
    {
      // Base address of all interrupt vectors.
      addr_base = mmap(NULL, NUM_INTERRUPTS << 8, 
                       PROT_READ | PROT_WRITE, 
                       MAP_SHARED | MAP_ANON,
                       -1, 0); 
      if (addr_base == MAP_FAILED)
        goto done;
    }

    // Real entry for this PL0 interrupt.
    char* addr = addr_base + (intr_num << 8);

    // Get the page_size aligned address.
    void* page = (void*)((uintptr_t)addr & -page_size);
	
    // Get the actual size of trampoline_body_copied function.
    size_t size = trampoline_body_copied_end - trampoline_body_copied;
  
    if (!first_time)
    {
      if (mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        goto done;
    }

    // Clear non-code segment.
    memset(addr + size, 0, MAX_TRAMPOLINE_SIZE - size);

    // Copy trampoline_body_copied code.
    memcpy(addr, trampoline_body_copied, size);

    // Fill in hw2 of interrupt mask.
    tilegx_bundle_bits* addr_hw2 = (void*)(addr + 
                                   (trampoline_body_copied_mask_hw2 - 
                                   trampoline_body_copied));
#ifdef __LP64__
    *addr_hw2 |= create_Imm16_X0((uintptr_t)INT_MASK(intr_num) >> 32);
#else
    *addr_hw2 |= create_Imm16_X0((intptr_t)INT_MASK(intr_num) >> 31);
#endif

    // Fill in hw1 of interrupt mask.
    tilegx_bundle_bits* addr_hw1 = (void*)(addr + 
                                   (trampoline_body_copied_mask_hw1 - 
                                   trampoline_body_copied));
    *addr_hw1 |= create_Imm16_X0(((uintptr_t)INT_MASK(intr_num) & 
                                 0xFFFF0000) >> 16);

    // Fill in hw0 of interrupt mask.
    tilegx_bundle_bits* addr_hw0 = (void*)(addr + 
                                   (trampoline_body_copied_mask_hw0 - 
                                   trampoline_body_copied));
    *addr_hw0 |= create_Imm16_X0((uintptr_t)INT_MASK(intr_num) & 0xFFFF);

    // Fill in hw2 of user C callback function entry.
    addr_hw2 = (void*)(addr + (trampoline_body_copied_c_hw2 -
               trampoline_body_copied));
#ifdef __LP64__
    *addr_hw2 |= create_Imm16_X0((uintptr_t)func.func_no_arg >> 32);
#else
    *addr_hw2 |= create_Imm16_X0((intptr_t)func.func_no_arg >> 31);
#endif

    // Fill in hw1 of user C callback function entry.
    addr_hw1 = (void*)(addr + (trampoline_body_copied_c_hw1 -
      	       trampoline_body_copied));
    *addr_hw1 |= create_Imm16_X0(((uintptr_t)func.func_no_arg & 
                                 0xFFFF0000) >> 16);

    // Fill in hw0 of user C callback function entry.
    addr_hw0 = (void*)(addr + (trampoline_body_copied_c_hw0 -
      	       trampoline_body_copied));
    *addr_hw0 |= create_Imm16_X0((uintptr_t)func.func_no_arg & 0xFFFF);

    // Force icache re-fetch. 
    tmc_mem_invalidate_icache(addr, MAX_TRAMPOLINE_SIZE);

    // Reset protections to read-only.
    int result = mprotect(page, page_size, PROT_READ | PROT_EXEC);
    if (result != 0)
      goto done;
  }

  // Setup SPR of interrupt base address by each thread.
  __insn_mtspr(SPR_INTERRUPT_VECTOR_BASE_0, (uintptr_t)addr_base);

  // Success!
  intr_func[intr_num] = func;
  retval = 0;

done:
  // Unlock the interrupt memory operation.  
  tmc_spin_queued_mutex_unlock(&intr_mmap_mutex);

  return retval;
}
