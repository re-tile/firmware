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
 * User-managed shared memory locking.
 */

#ifndef TNSLOCK_H
#define TNSLOCK_H

#include <sys/types.h>

/** Lock via TNS.
 * The lock pointer must reference uncached or single-cached (oloc) memory.
 * @param lock_ptr Pointer to the word that will be used for TNS
 */
void tnslock_rawlock(int* lock_ptr);

/** Unlock via TNS.
 * The lock pointer must reference uncached or single-cached (oloc) memory.
 * @param lock_ptr Pointer to the word that will be used for TNS
 */
static inline void tnslock_rawunlock(int* lock_ptr)
  { *(volatile int*)lock_ptr = 0; }

/** Lock via TNS and prepare a given memory range for access.
 * The lock pointer must reference uncached or single-cached (oloc) memory.
 * If the lock pointer is NULL, the memory is flushed with no locking.
 * Both the object pointer and the size must be a multiple of the L2
 * cache line size.
 * @param object The start of the desired memory range
 * @param size The size of the desired memory range
 * @param lock_ptr Pointer to the word that will be used for TNS
 */
void tnslock_lock(void* object, size_t size, int* lock_ptr);

/** Unlock a memory range locked via TNS and flush the associated memory range.
 * The unlock is done by a simple write of zero.
 * If the lock pointer is NULL, the memory is flushed with no unlocking.
 * @param object The start of the desired memory range
 * @param size The size of the desired memory range
 * @param lock_ptr Pointer to the word that was used for TNS
 */
void tnslock_unlock(void* object, size_t size, int* lock_ptr);

#endif
