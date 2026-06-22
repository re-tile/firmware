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

//! @file
//!
//! Routines for performing cross-process shared memory synchronization
//! without requiring pthreads.
//!

//! @addtogroup tmc_sync
//! @{
//!
//! Routines for performing cross-process shared memory synchronization
//! without requiring pthreads.
//!
//! The Tilera MDE provides the standard pthread synchronization
//! primitives, including pthread_mutex_t and pthread_barrier_t.
//! However, certain applications cannot use these primitives,
//! generally because the application uses shared memory between
//! multiple processes and does not want to link against pthreads.
//!
//! For such applications, TMC provides its own implementations of the
//! mutex and barrier primitives, ::tmc_sync_mutex_t and
//! ::tmc_sync_barrier_t.  The mutex primitive provides mutual
//! exclusion, guaranteeing that only one task can hold the 'lock' at
//! a time.  The barrier primitive waits for the specified number of
//! tasks to call tmc_sync_barrier_wait() before any of the tasks are
//! allowed to continue.
//!
//! The implementations of these primitives interacts with the kernel
//! scheduler so that tasks waiting for a mutex or barrier are
//! context-switched out.  As a result, these primitives are not the fastest
//! possible implementations, but they can be used in any application.
//! In particular, using these implementations is desirable for
//! applications that run multiple tasks on the same core, since blocking
//! one task on a synchronization operation causes that task to
//! release the processor and allows a different task to make progress.
//!
//! Applications running with one task per core may prefer to use @ref
//! tmc_spin.  The spin-based synchronization routines do not interact
//! with the Linux task scheduler, and as a result have somewhat lower
//! overhead.
//!
//! For sharing a lock between control-plane tasks (scheduled by the
//! kernel) and dataplane tasks (pinned one task per core) it can be
//! helpful to use the tmc_sync_mutex_spin_lock() API on the dataplane
//! cores and the regular tmc_sync_mutex_lock() on the control plane.
//! Using tmc_sync_mutex_spin_lock() never invokes the kernel from the
//! dataplane cores, and once the lock is taken with that routine,
//! other cores, including control-plane cores using tmc_sync_mutex_lock(),
//! will also spin when they try to acquire the lock.  This behavior
//! prevents the dataplane core from having to do kernel wakeups when
//! unlocking the lock.  When control-plane cores do hold the lock, it
//! functions as usual and cores will wait in and be woken from the kernel.

#ifndef __TMC_SYNC_H__
#define __TMC_SYNC_H__

#include <features.h>

#include <arch/atomic.h>
#include <arch/inline.h>

__BEGIN_DECLS

//! A shared memory mutex data structure.
//!
//! This object can be located in any shared memory region, even regions
//! that are mapped at different VAs in different processes.
//!
typedef int tmc_sync_mutex_t;

//! Static initializer for a tmc_sync_mutex_t.
#define TMC_SYNC_MUTEX_INIT 0

//! Initialize a mutex to the unlocked state.
//!
//! @param mutex The shared-memory mutex object to be initialized.
//!
static __USUALLY_INLINE void
tmc_sync_mutex_init(tmc_sync_mutex_t* mutex)
{
  *mutex = TMC_SYNC_MUTEX_INIT;
}

//! Lock a mutex.
//!
//! If the mutex is currently held, this task will be descheduled until
//! the mutex is released.
//!
//! A task will self-deadlock if it attempts to re-lock a locked mutex.
//!
//! @param mutex The mutex object.
//!
static __USUALLY_INLINE void
tmc_sync_mutex_lock(tmc_sync_mutex_t* mutex)
{
  extern void tmc_sync_mutex_wait(tmc_sync_mutex_t*);
  if (__builtin_expect(arch_atomic_val_compare_and_exchange(mutex, 0, 1), 0))
    tmc_sync_mutex_wait(mutex);
  arch_atomic_acquire_barrier();
}

//! Lock a mutex, spinning in userspace if the lock isn't available.
//!
//! If the mutex is currently held, the process will spin trying to
//! acquire the lock until the lock is acquired.  This is safe to do
//! on a dataplane tile as the routine will not enter the kernel.
//!
//! A task will self-deadlock if it attempts to re-lock a locked mutex.
//!
//! @param mutex The mutex object.
//!
static __USUALLY_INLINE void
tmc_sync_mutex_spin_lock(tmc_sync_mutex_t* mutex)
{
  extern void tmc_sync_mutex_spin_wait(tmc_sync_mutex_t*);
  if (__builtin_expect(arch_atomic_val_compare_and_exchange(mutex, 0, -1), 0))
    tmc_sync_mutex_spin_wait(mutex);
  arch_atomic_acquire_barrier();
}

//! Try to lock a mutex, but return immediately.
//!
//! @param mutex The mutex object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
static __USUALLY_INLINE int
tmc_sync_mutex_trylock(tmc_sync_mutex_t* mutex)
{
  int rc = arch_atomic_val_compare_and_exchange(mutex, 0, 1);

  // Use a heavier-weight atomic barrier here, in case the user
  // proceeds to read some values and only later verifies that they
  // actually had the lock and thus read valid data.
  arch_atomic_acquire_barrier_value(rc);

  return rc;
}

//! Release an already-held mutex.
//!
//! @param mutex The mutex object.
//!
//! Any task (not just the mutex owner) can unlock a locked mutex.
//!
static __USUALLY_INLINE void
tmc_sync_mutex_unlock(tmc_sync_mutex_t *mutex)
{
  extern void tmc_sync_mutex_wake(int *mutex);
  arch_atomic_release_barrier();
  if (__builtin_expect(arch_atomic_exchange(mutex, 0) == 2, 0))
    tmc_sync_mutex_wake(mutex);
}


//! A shared memory barrier data structure.
//!
//! This object can be located in any shared memory region, even regions
//! which are mapped at different VAs in different processes.
//!
typedef struct
{
  int generation;        ///< How many barriers have completed.
  int count;             ///< Number of tasks remaining in this barrier.
  int num_tasks;         ///< Number of tasks per barrier.
  tmc_sync_mutex_t lock; ///< For atomicity.
}
tmc_sync_barrier_t;

//! Static initializer for a tmc_sync_barrier_t.
//!
//! @param NUM_TASKS The number of processes or threads that must call
//! tmc_sync_barrier_wait() on this barrier in order for the barrier
//! operation to complete.
//!
#define TMC_SYNC_BARRIER_INIT(NUM_TASKS) \
  { 0, NUM_TASKS, NUM_TASKS, TMC_SYNC_MUTEX_INIT }

//! Initialize a barrier to the state in which no tasks have
//! arrived at the barrier.
//!
//! @param barrier A shared-memory barrier object to be initialized.
//! @param num_tasks The number of processes or threads that must call
//! tmc_sync_barrier_wait() on this barrier in order for the barrier
//! operation to complete.
//!
static __USUALLY_INLINE void
tmc_sync_barrier_init(tmc_sync_barrier_t* barrier, unsigned num_tasks)
{
  barrier->generation = 0;
  barrier->count = num_tasks;
  barrier->num_tasks = num_tasks;
  barrier->lock = TMC_SYNC_MUTEX_INIT;
}

//! Wait until the specified number of tasks have arrived at a
//! barrier.
//!
//! @param barrier The barrier object.
//! @return 1 in the first process to exit the barrier, 0 in all other
//! processes.
//!
extern int
tmc_sync_barrier_wait(tmc_sync_barrier_t* barrier);

__END_DECLS

#endif /* __TMC_SYNC_H__ */

//! @}
