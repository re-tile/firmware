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
//! Spin-based memory synchronization primitives for use with
//! one-task-per-core applications.
//!

//! @addtogroup tmc_spin
//! @{
//!
//! Spin-based memory synchronization primitives for use with
//! one-task-per-core applications.
//!
//! TMC provides both "kernel scheduler" and "spinning"
//! implementations of shared memory synchronization primitives.  The
//! spinning variants, described in this section, do not yield the
//! core (that is, context-switch to another process or thread)
//! if a resource is unavailable.  Instead, they will poll
//! continually until forward progress can be made.  Spinning
//! primitives can be more efficient than the versions that interact
//! with the kernel scheduler, but they are less flexible.
//!
//! The spinning synchronization primitives should only be used by
//! tasks that do not share a core with any other tasks.  Failure to
//! observe this rule can result in severe performance problems when,
//! for example, a task holding a ::tmc_spin_mutex_t is context-swapped
//! out for a tenth of a second so another task can run.  In such
//! cases, no other task can access the mutex until the holding task
//! is swapped back in.  This problem could potentially bring an
//! entire application to a complete stop for many milliseconds.  The
//! kernel scheduling synchronization primitives avoid this problem by
//! informing the kernel scheduler that particular tasks are blocked
//! on the mutex, causing them to yield the core so the holding task
//! can swap back in and release the mutex.
//!
//! The spin-based synchronization primitives work best when running
//! in Tilera's Zero-Overhead Linux&tm; (ZOL) environment.  When running
//! on ZOL tiles, it is possible to avoid the latency "hiccups" that
//! might result from a Linux timer tick firing while a task is
//! holding a spin mutex.
//!
//! To synchronize between a ZOL tile and a regular tile, you may wish
//! to use a ::tmc_sync_mutex_t lock and use the
//! tmc_sync_mutex_spin_lock() API on the ZOL tiles.
//!
//! @section tmc_spin_perf Mutex Performance Recommendations
//!
//! For some applications, the choice of mutex implementation can have
//! a significant performance impact.  In general, the spin-based
//! mutex implementations are faster than the tmc_sync_mutex_t or
//! pthread_mutex_t implementations, but they can only be used on ZOL
//! tiles.
//!
//! <tmc/spin.h> provides two mutex implementations:
//! ::tmc_spin_mutex_t and ::tmc_spin_queued_mutex_t.  The queued
//! mutex requires slightly more overhead but provides much better
//! fairness.  Fairness can yield more predictable performance in
//! applications where a lock is highly contended, since no one core
//! tends to win the mutex more than the others.  Both mutex
//! implementations perform better when lock contention is low.
//!
//! In general, we recommend that applications use
//! ::tmc_spin_queued_mutex_t rather than ::tmc_spin_mutex_t.
//! ::tmc_spin_mutex_t should only be used in cases where mutex
//! overhead is truly critical and lock contention is low.
//!
//! For applications that require read-write locks, the
//! ::tmc_spin_rwlock_t type is also provided.  It allows multiple
//! read-lockers simultaneously, but only a single write-locker,
//! which then also excludes any read-lockers.  The performance of
//! lock acquisition/release for read locks is generally slightly
//! slower than the spinning mutexes described above when unloaded,
//! and intermediate between pure spin and queued spin mutexes
//! when contended.
//!
//! The following table provides a comparison of mutex performance on
//! the TILE@e Pro&tm; processor.  For each type of mutex, we measure the
//! number of cycles required to acquire a lock, bump a counter, and
//! release the lock.  Measurements are provided for several
//! situations, including: a single core locking a mutex homed on that
//! core, a single core locking a mutex homed on a remote core, and
//! many cores (~50) all contending for the same lock.
//! Read-locks of ::tmc_spin_rwlock_t are included, though they
//! are not "mutexes" properly speaking.
//!
//! Note: the performance of many cores contending shows the aggregate
//! throughput of lock acquire/release for the single lock, thus for
//! example a value of 200 would mean that on average, some core is
//! acquiring and releasing the lock every 200 cycles.
//!
//! <table>
//! <tr>
//!    <td><b> Mutex Type </b></td>
//!    <td><b> Single Core Local</b></td>
//!    <td><b> Single Core Remote </b></td>
//!    <td><b> Many Core Contended </b></td>
//!    <td><b> Fair </b></td>
//! </tr>
//! <tr>
//!    <td> tmc_spin_mutex_t </td>
//!    <td> 41</td>
//!    <td> 125 </td>
//!    <td> 100 </td>
//!    <td> No </td>
//! </tr>
//! <tr>
//!    <td> tmc_spin_queued_mutex_t </td>
//!    <td> 60 </td>
//!    <td> 135 </td>
//!    <td> 300 </td>
//!    <td> Yes </td>
//! </tr>
//! <tr>
//!    <td> tmc_spin_rwlock_t (read) </td>
//!    <td> 60 </td>
//!    <td> 170 </td>
//!    <td> 200 </td>
//!    <td> N/A </td>
//! </tr>
//! <tr>
//!    <td> tmc_sync_mutex_t </td>
//!    <td> 225 </td>
//!    <td> 250 </td>
//!    <td> 500 </td>
//!    <td> No </td>
//! </tr>
//! <tr>
//!    <td> pthread_mutex_t </td>
//!    <td> 300 </td>
//!    <td> 330 </td>
//!    <td> 600 </td>
//!    <td> No </td>
//! </tr>
//! </table> 
//!

#ifndef __TMC_SPIN_H__
#define __TMC_SPIN_H__

#include <features.h>
#include <arch/atomic.h>
#include <arch/inline.h>

__BEGIN_DECLS

/***********************************************************************/
/*                              Simple Spinlocks                       */
/***********************************************************************/

//! A shared memory spinlock data structure.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
typedef struct
{
  //! A spinlock word manipulated by the "tns" (test and set word) instruction.
  int lock;
}
tmc_spin_mutex_t;

//! Static initializer for a tmc_spin_mutex_t.
#define TMC_SPIN_MUTEX_INIT { 0 }

//! Initialize a mutex to the unlocked state.
//!
//! @param mutex The shared-memory mutex object to be initialized.
//!
static __USUALLY_INLINE void
tmc_spin_mutex_init(tmc_spin_mutex_t* mutex)
{
  tmc_spin_mutex_t empty = TMC_SPIN_MUTEX_INIT;
  *mutex = empty;
}

#ifndef __DOXYGEN__
extern void
__tmc_spin_mutex_wait(tmc_spin_mutex_t* mutex);
#endif

//! Lock a mutex.
//!
//! This function polls repeatedly until the lock is available.
//!
//! A task will self-deadlock if it attempts to re-lock a locked mutex.
//!
//! @param mutex The mutex object.
//!
static __USUALLY_INLINE void
tmc_spin_mutex_lock(tmc_spin_mutex_t* mutex)
{
  if (__insn_tns(&mutex->lock))
    __tmc_spin_mutex_wait(mutex);
  arch_atomic_acquire_barrier();
}

//! Try to lock a mutex, but return immediately.
//!
//! @param mutex The mutex object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
static __USUALLY_INLINE int
tmc_spin_mutex_trylock(tmc_spin_mutex_t* mutex)
{
  // insn_tns returns 1 if the lock was previously held, 0 if free.
  // Thus, we can return its result directly since we return 1 for
  // failure and 0 for success.
  int rc = __insn_tns(&mutex->lock);

  // Use a heavier-weight atomic barrier here, in case the user
  // proceeds to read some values and only later verifies that they
  // actually had the lock and thus read valid data.
  arch_atomic_acquire_barrier_value(rc);

  return rc;
}

//! Release an already held mutex.
//!
//! Any task (not just the mutex owner) can unlock a locked mutex.
//!
//! @param mutex The mutex object.
//!
static __USUALLY_INLINE void
tmc_spin_mutex_unlock(tmc_spin_mutex_t *mutex)
{
  // Make all of our memory ops visible.
  arch_atomic_release_barrier();

  // Release the lock.
  mutex->lock = 0;
}


/***********************************************************************/
/*                             Queued Spinlocks                        */
/***********************************************************************/

//! A shared memory, spinning, fair mutex.
//!
//! Queued locks (also known as ticket locks) locks provide fairness
//! when under contention, but still maintain efficiency similar to
//! that of a simple spin lock.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
typedef struct
{
  //! Next ticket number to dispense.
  int next_ticket;

  //! The ticket number that currently owns this lock.
  int current_ticket;
}
tmc_spin_queued_mutex_t;

#ifndef __DOXYGEN__
// We only use even ticket numbers so the '1' inserted by a tns is an
// unambiguous "ticket is busy" flag.
#define TMC_SPIN_TICKET_QUANTUM 2
#endif

//! Static initializer for a tmc_spin_mutex_t.
#define TMC_SPIN_QUEUED_MUTEX_INIT { 0, 0 }

//! Initialize a mutex to the unlocked state.
//!
//! @param mutex The shared-memory mutex object to be initialized.
//!
static __USUALLY_INLINE void
tmc_spin_queued_mutex_init(tmc_spin_queued_mutex_t* mutex)
{
  tmc_spin_queued_mutex_t empty = TMC_SPIN_QUEUED_MUTEX_INIT;
  *mutex = empty;
}

//! Lock a queued mutex.
//!
//! This function polls repeatedly until the lock is available.
//! If the lock is unavailable, the calling task joins a list of
//! waiting tasks and spins until its turn arrives.  This scheme
//! achieves better fairness than a simple spinlock.
//!
//! A task will self-deadlock if it attempts to re-lock a locked mutex.
//!
//! @param mutex The mutex object.
//!
extern void
tmc_spin_queued_mutex_lock(tmc_spin_queued_mutex_t* mutex);

//! Try to lock a queued mutex, but return immediately.
//!
//! @param mutex The mutex object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
extern int
tmc_spin_queued_mutex_trylock(tmc_spin_queued_mutex_t* mutex);

//! Release an already held queued mutex.
//!
//! Any task (not just the mutex owner) can unlock a locked mutex.
//!
//! @param mutex The mutex object.
//!
static __inline void
tmc_spin_queued_mutex_unlock(tmc_spin_queued_mutex_t *mutex)
{
  // For efficiency, overlap fetching the old ticket with the barrier.
  int old_ticket = mutex->current_ticket;
  arch_atomic_release_barrier();
  mutex->current_ticket = old_ticket + TMC_SPIN_TICKET_QUANTUM;
}


/***********************************************************************/
/*                       Reader/Writer Spinlocks                       */
/***********************************************************************/

//! A shared memory reader/writer spinlock data structure.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
//! Reader-write spinlocks can have multiple simultaneous readers, but
//! a writer has exclusive access.  Once a would-be writer starts
//! trying to lock the rwlock, no additional readers can lock the rwlock
//! until the writer has acquired and released it.
//!
typedef struct
{
  //! A spinlock word manipulated by the "tns" (test and set word) instruction.
  unsigned int lock;
}
tmc_spin_rwlock_t;

#ifndef __DOXYGEN__
// Internal layout of the word; do not use.
#define TMC_SPIN_WR_NEXT_SHIFT 8
#define TMC_SPIN_RD_COUNT_SHIFT 24
#define TMC_SPIN_RD_COUNT_WIDTH 8

// Internal functions; do not use.
void __tmc_spin_rwlock_rdlock_slow(tmc_spin_rwlock_t*, unsigned int);
unsigned int __tmc_spin_rwlock_get(tmc_spin_rwlock_t*);
void __tmc_spin_rwlock_wrlock_slow(tmc_spin_rwlock_t*, unsigned int);
void __tmc_spin_rwlock_wrunlock_slow(tmc_spin_rwlock_t*, unsigned int);
#endif

//! Static initializer for a tmc_spin_rwlock_t.
#define TMC_SPIN_RWLOCK_INIT { 0 }

//! Initialize an rwlock to the unlocked state.
//!
//! @param rwlock The shared-memory rwlock object to be initialized.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_init(tmc_spin_rwlock_t* rwlock)
{
  tmc_spin_rwlock_t empty = TMC_SPIN_RWLOCK_INIT;
  *rwlock = empty;
}

//! Lock an rwlock for reading.
//!
//! This function polls repeatedly until the lock is available for read,
//! potentially sharing the lock with other tasks that also have it
//! locked for reading.
//!
//! A task can recursively re-acquire a read lock on a read-locked rwlock.
//! Note that there is a limit of 255 simultaneous readers, since the
//! reader count is maintained in a single byte of the lock word.
//! The recommended model is to have only one task per cpu, and have that
//! task only acquire any given rwlock once.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_rdlock(tmc_spin_rwlock_t* rwlock)
{
  unsigned int val = __insn_tns((int*)&rwlock->lock);
  if (__builtin_expect(val << TMC_SPIN_RD_COUNT_WIDTH, 0))
    __tmc_spin_rwlock_rdlock_slow(rwlock, val);
  else
    rwlock->lock = val + (1 << TMC_SPIN_RD_COUNT_SHIFT);
  arch_atomic_acquire_barrier();
}

//! Lock an rwlock for writing.
//!
//! This function polls repeatedly until the lock is available for write.
//! Once a task calls this function, no other reader can acquire the lock.
//! The lock will block until all the readers release their locks, then
//! it will be acquired as an exclusive write lock for the caller.
//!
//! A task will self-deadlock if it attempts to re-lock a write-locked rwlock.
//!
//! Lock acquisition by writers is fair, in a similar manner to
//! @ref tmc_spin_queued_mutex_t locks.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_wrlock(tmc_spin_rwlock_t* rwlock)
{
  unsigned int val = __insn_tns((int*)&rwlock->lock);
  if (__builtin_expect(val != 0, 0))
    __tmc_spin_rwlock_wrlock_slow(rwlock, val);
  else
    rwlock->lock = 1 << TMC_SPIN_WR_NEXT_SHIFT;
  arch_atomic_acquire_barrier();
}

//! Try to read-lock an rwlock, but return immediately.
//!
//! @param rwlock The rwlock object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
static __USUALLY_INLINE int
tmc_spin_rwlock_tryrdlock(tmc_spin_rwlock_t* rwlock)
{
  int locked;
  unsigned int val = __insn_tns((int*)&rwlock->lock);
  if (__builtin_expect(val & 1, 0))
    val = __tmc_spin_rwlock_get(rwlock);
  locked = (val << TMC_SPIN_RD_COUNT_WIDTH) == 0;
  rwlock->lock = val + (locked << TMC_SPIN_RD_COUNT_SHIFT);
  arch_atomic_acquire_barrier();
  return !locked;
}

//! Try to write-lock an rwlock, but return immediately.
//!
//! @param rwlock The rwlock object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
static __USUALLY_INLINE int
tmc_spin_rwlock_trywrlock(tmc_spin_rwlock_t* rwlock)
{
  unsigned int val = __insn_tns((int*)&rwlock->lock);

  // If a tns is in progress, or there's a waiting or active locker,
  // or active readers, we can't take the lock, so give up.
  if (__builtin_expect(val != 0, 0))
  {
    if (!(val & 1))
      rwlock->lock = val;
  }
  else
  {
    // Set the "next" field to mark it locked.
    rwlock->lock = 1 << TMC_SPIN_WR_NEXT_SHIFT;
  }
  arch_atomic_acquire_barrier();
  return val;
}

//! Release an rwlock that was locked for reading.
//!
//! Any task (not just the rwlock owner) can unlock a locked rwlock.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_rdunlock(tmc_spin_rwlock_t *rwlock)
{
  unsigned int val;
  arch_atomic_release_barrier();
  val = __insn_tns((int*)&rwlock->lock);
  if (__builtin_expect(val & 1, 0))
    val = __tmc_spin_rwlock_get(rwlock);
  rwlock->lock = val - (1 << TMC_SPIN_RD_COUNT_SHIFT);
}

//! Release an rwlock that was locked for writing.
//!
//! Any task (not just the rwlock owner) can unlock a locked rwlock.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_wrunlock(tmc_spin_rwlock_t *rwlock)
{
  unsigned int val;
  arch_atomic_release_barrier();
  val = __insn_tns((int*)&rwlock->lock);
  if (__builtin_expect(val != (1 << TMC_SPIN_WR_NEXT_SHIFT), 0))
    __tmc_spin_rwlock_wrunlock_slow(rwlock, val);
  else
    rwlock->lock = 0;
}

//! Release an rwlock that was locked for reading or writing.
//!
//! Note that this routine is somewhat slower than the routines
//! that are specific to unlocking read- or write-locks.
//!
//! Any task (not just the rwlock owner) can unlock a locked rwlock.
//!
//! @param rwlock The rwlock object.
//!
void tmc_spin_rwlock_unlock(tmc_spin_rwlock_t *rwlock);


/***********************************************************************/
/*                           Spin-Based Barrier                        */
/***********************************************************************/

//! A shared memory, spin-based barrier structure.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
typedef union
{
  //! For bitfield access.
  struct {
    //! True if some other thread has this word locked.
    //! This bit is set by the @c tns (test and set word) instruction.
    unsigned int busy   : 1;
    //! This value alternates with each barrier wait.
    unsigned int parity : 1;
    //! The number of threads this barrier is designed for, used
    //! as the reset value for @c count each time a barrier completes.
    unsigned int reset  : 15;
    //! The number of threads that have not yet reached this barrier.
    unsigned int count  : 15;
  } s;

  //! For @c tns (test and set word) access; aliases with the @c busy bit.
  int n;
}
tmc_spin_barrier_t;


//! Static initializer for a tmc_spin_barrier_t.
//!
//! @param NUM_TASKS The number of processes or threads that must call
//! tmc_spin_barrier_wait() on this barrier in order for the barrier
//! operation to complete.
//!
#define TMC_SPIN_BARRIER_INIT(NUM_TASKS) \
{{ \
  /* .busy = */   0, \
  /* .parity = */ 0, \
  /* .reset = */  (NUM_TASKS), \
  /* .count = */  (NUM_TASKS) \
}}

#ifdef __BME__
//! Pass this value to the barrier initializer to create a barrier
//! across all BME cores.  This functionality is not available in the
//! Linux version of TMC.
#define TMC_SPIN_BARRIER_ALL 0
#endif

//! Initialize a barrier to the state in which no tasks have
//! arrived at the barrier.
//!
//! It is almost always a mistake to re-initialize a barrier, unless
//! the application can guarantee that no other thread is currently
//! waiting at the barrier.  Since barriers are reusable, it is rarely
//! helpful to reinitialize a particular barrier.
//!
//! @param barrier A shared-memory barrier object to be initialized.
//! @param num_tasks The number of processes or threads that must call
//! tmc_spin_barrier_wait() on this barrier in order for the barrier
//! operation to complete.
//!
static __inline void
tmc_spin_barrier_init(tmc_spin_barrier_t* barrier, unsigned num_tasks)
{
  tmc_spin_barrier_t empty = TMC_SPIN_BARRIER_INIT(0);
  *barrier = empty;

  // c89 can't handle static initializers with dynamic values.
  barrier->s.reset = num_tasks;
  barrier->s.count = num_tasks;
}

//! Wait until the specified number of tasks have arrived at a
//! barrier.
//!
//! This call may be used multiple times for a single barrier.
//! Each time all the tasks will have to arrive at the barrier
//! again before any task can proceed.
//!
//! @param barrier The barrier object.
//! @return 1 in the first process to exit the barrier, 0 in all other
//! processes.
//!
int tmc_spin_barrier_wait(tmc_spin_barrier_t* barrier);

__END_DECLS

#endif /* __TMC_SPIN_H__ */

//! @}
