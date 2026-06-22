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


#ifndef _PKA_ALLOC_H_
#define _PKA_ALLOC_H_

#include <stdint.h>


#define OPERAND_MEM_SIZE  0xE000
#define MIN_ALLOC_SIZE    192
#define MAX_ALLOC_SIZE    2560

// Note that micaNum's range from 0 to 3.  Before a given micaNum can be
// used, InitMemAllocator with that micaNum must be called to allocate the
// per mica data structures and initialize the,
#define FIRST_MICA_NUM    0
#define LARGEST_MICA_NUM  3

// operand_mem_avail can be used to tell whether or not alloc_operand_mem
// will succeed or not.  Returns 1 if alloc_operand_mem will succeed and
// 0 if alloc_operand_mem may fail.
uint32_t operand_mem_avail (uint32_t micaNum, uint32_t sizeInBytes);

uint16_t alloc_operand_mem (uint32_t micaNum, uint32_t sizeInBytes);
void     free_operand_mem  (uint32_t micaNum, uint16_t operandMemOffset);


uint32_t operand_mem_size (uint32_t micaNum, uint16_t operandMemOffset);

// Stats API.
uint32_t largest_contig_avail_mem (uint32_t micaNum);  // in bytes.

// init_pka_mem_allocator must be called once per mica instance, before any
// other fcns are called.
void init_pka_mem_allocator (uint32_t micaNum);

#endif
