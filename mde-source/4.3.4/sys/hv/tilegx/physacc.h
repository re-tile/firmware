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
 * Routines to do direct access to physical memory, and the associated
 * syscalls.
 */

#ifndef _SYS_HV_TILEGX_PHYSACC_H
#define _SYS_HV_TILEGX_PHYSACC_H

#include <hv/hypervisor.h>

#include "types.h"

/** Read 8 bits from physical memory.
 * @param physaddr Physical address to read from.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 * @return Value read.
 */
uint8_t phys_rd8(PA physaddr, unsigned long aar);

/** Read 16 bits from physical memory.
 * @param physaddr Physical address to read from.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 * @return Value read.
 */
uint16_t phys_rd16(PA physaddr, unsigned long aar);

/** Read 32 bits from physical memory.
 * @param physaddr Physical address to read from.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 * @return Value read.
 */
uint32_t phys_rd32(PA physaddr, unsigned long aar);

/** Read 64 bits from physical memory.
 * @param physaddr Physical address to read from.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 * @return Value read.
 */
uint64_t phys_rd64(PA physaddr, unsigned long aar);


/** Write 8 bits to physical memory.
 * @param physaddr Physical address to write to.
 * @param data Data to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_wr8(PA physaddr, uint8_t data, unsigned long aar);

/** Write 16 bits to physical memory.
 * @param physaddr Physical address to write to.
 * @param data Data to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_wr16(PA physaddr, uint16_t data, unsigned long aar);

/** Write 32 bits to physical memory.
 * @param physaddr Physical address to write to.
 * @param data Data to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_wr32(PA physaddr, uint32_t data, unsigned long aar);

/** Write 64 bits to physical memory.
 * @param physaddr Physical address to write to.
 * @param data Data to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_wr64(PA physaddr, uint64_t data, unsigned long aar);

/** Write two 64-bit values to physical memory in quick succession.
 * @param physaddr Physical address to write to.
 * @param data0 First value to write.
 * @param data1 Second value to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_double_wr64(PA physaddr, uint64_t data0, uint64_t data1,
                      unsigned long aar);

/** Write 64 bits to physical memory, after flushing the target from the cache.
 * @param physaddr Physical address to write to.
 * @param data Data to write.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_finv_wr64(PA physaddr, uint64_t data, unsigned long aar);

/** Flush and invalidate a cacheline.
 * @param physaddr Physical address to flush and invalidate.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word.
 */
void phys_finv(PA physaddr, unsigned long aar);

/** Atomically read 64 bits from physical memory, compare it with a given
 *  value, and if the two values match, write back a second value.
 * @param physaddr Physical address to write to.
 * @param compare Data to compare with.
 * @param write Data to write back if compare succeeds.
 * @param aar AAR value, defining the caching mode to use.  Note that the
 *   physical memory mode bit must be on in this word; note also that it
 *   is not legal to use a memory attribute of uncacheable for this routine.
 * @return Value read from memory, which may or may not be equal to
 *   compare.
 */
uint64_t phys_cmpexch64(PA physaddr, uint64_t compare, uint64_t write,
                        unsigned long aar);


/** Read data from client physical memory.
 * @param cpa Source physical address.
 * @param access PTE to use to set caching bits, LOTAR, etc.
 * @return The 64-bit value from the referenced address.
 */
uint64_t syscall_physaddr_read64(CPA cpa, HV_PTE access);

/** Write data to client physical memory.
 * @param cpa Destination physical address.
 * @param access PTE to use to set caching bits, LOTAR, etc.
 * @param val Value to write.
 */
void syscall_physaddr_write64(CPA cpa, HV_PTE access, uint64_t val);

/** Get real PA from client PA.
 * @param cpa Client physical address.
 * @param len Length of memory at physical address.
 */
PA syscall_inquire_realpa(CPA cpa, uint32_t len);

#endif /* _SYS_HV_TILEGX_PHYSACC_H */
