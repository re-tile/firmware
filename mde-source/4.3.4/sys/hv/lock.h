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
 * Locking routines.
 */

#ifndef _SYS_HV_LOCK_H
#define _SYS_HV_LOCK_H

/** Static initializer for a spinlock_t. */
#define SPINLOCK_INIT { 0 }

/** A spinlock. */
typedef struct
{
  /** Lock word; 1 if the lock is held, 0 otherwise. */
  int lock;
}
spinlock_t;

/** Initialize a mutex to the unlocked state. */
static inline void
spin_lock_init(spinlock_t* mutex)
{
  mutex->lock = 0;
}

void spin_lock(spinlock_t* mutex);
int spin_trylock(spinlock_t* mutex);
void spin_unlock(spinlock_t* mutex);

#endif /* _SYS_HV_LOCK_H */
