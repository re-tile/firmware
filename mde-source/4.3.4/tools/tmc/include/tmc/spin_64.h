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
//! the TILE-Gx processor.  For each type of mutex, we measure the
//! number of cycles required to acquire a lock, bump a counter, and
//! release the lock.  Measurements are provided for several
//! situations, including: a single core locking a mutex homed on that
//! core, a single core locking a mutex homed on a remote core, and
//! many cores (~50) all contending for the same lock.
//! Read-locks of ::tmc_spin_rwlock_t are included, though they
//! are not "mutexes" properly speaking.
//!
//! It is frequently the case that atomic instructions can provide
//! even faster updates to shared data structures than mutexes.
//! Please see the <arch/atomic.h> header documentation for more
//! information on the available atomic operations.
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
//!    <td> 44 </td>
//!    <td> 155 </td>
//!    <td> 200 </td>
//!    <td> No </td>
//! </tr>
//! <tr>
//!    <td> tmc_spin_queued_mutex_t </td>
//!    <td> 48 </td>
//!    <td> 160 </td>
//!    <td> 300 </td>
//!    <td> Yes </td>
//! </tr>
//! <tr>
//!    <td> tmc_spin_rwlock_t (read) </td>
//!    <td> 44 </td>
//!    <td> 160 </td>
//!    <td> 65 </td>
//!    <td> N/A </td>
//! </tr>
//! <tr>
//!    <td> tmc_sync_mutex_t </td>
//!    <td> 50 </td>
//!    <td> 165 </td>
//!    <td> 250 </td>
//!    <td> No </td>
//! </tr>
//! <tr>
//!    <td> pthread_mutex_t </td>
//!    <td> 191 </td>
//!    <td> 345 </td>
//!    <td> 500 </td>
//!    <td> No </td>
//! </tr>
//! </table> 
//!

#ifndef __TMC_SPIN_H__
#define __TMC_SPIN_H__

#include <features.h>
#include <string.h>
#include <arch/atomic.h>
#include <arch/chip.h>
#include <arch/inline.h>
#include <arch/spr_def.h>

struct timespec;

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
  //! A spinlock word manipulated by an atomic exchange instruction.
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
  if (__builtin_expect(__insn_exch4(&mutex->lock, 1), 0))
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
  // The exch returns 1 if the lock was previously held, 0 if free.
  // Thus, we can return its result directly since we return 1 for
  // failure and 0 for success.
  int rv = __insn_exch4(&mutex->lock, 1);

  // Use a heavier-weight atomic barrier here, in case the user
  // proceeds to read some values and only later verifies that they
  // actually had the lock and thus read valid data.
  arch_atomic_acquire_barrier_value(rv);

  return rv;
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

  // Release the lock ("exchange" will push the value immediately).
  arch_atomic_exchange(&mutex->lock, 0);
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
  //! Low 15 bits are "next"; high 15 bits are "current".
  unsigned int lock;
}
tmc_spin_queued_mutex_t;

#ifndef __DOXYGEN__
// Shifts and masks for the various fields in "lock".
#define TMC_SPIN_CURRENT_SHIFT  17
#define TMC_SPIN_NEXT_MASK      0x7fff
#define TMC_SPIN_NEXT_OVERFLOW  0x8000

// Return the "current" portion of a ticket lock value,
// i.e. the number that currently owns the lock.
#define __tmc_spin_current(val) ((val) >> TMC_SPIN_CURRENT_SHIFT)

// Return the "next" portion of a ticket lock value,
// i.e. the number that the next task to try to acquire the lock will get.
#define __tmc_spin_next(val) ((val) & TMC_SPIN_NEXT_MASK)

// Internal function; do not use.
void __tmc_spin_queued_mutex_lock_slow(tmc_spin_queued_mutex_t*, unsigned int);
#endif

//! Static initializer for a tmc_spin_mutex_t.
#define TMC_SPIN_QUEUED_MUTEX_INIT { 0 }

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
static __USUALLY_INLINE void
tmc_spin_queued_mutex_lock(tmc_spin_queued_mutex_t* mutex)
{
  // Grab the "next" ticket number and bump it atomically.
  // If the current ticket is not ours, go to the slow path.
  // We also take the slow path if the "next" value overflows.
  unsigned int val = __insn_fetchadd4(&mutex->lock, 1);
  unsigned int ticket = val & (TMC_SPIN_NEXT_MASK | TMC_SPIN_NEXT_OVERFLOW);
  if (__builtin_expect(__tmc_spin_current(val) != ticket, 0))
    __tmc_spin_queued_mutex_lock_slow(mutex, ticket);
  arch_atomic_acquire_barrier();
}

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
  // Bump the current ticket so the next task owns the lock.
  arch_atomic_release_barrier();
  __insn_fetchadd4(&mutex->lock, 1U << TMC_SPIN_CURRENT_SHIFT);
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
  //! High bit is "writer owns"; low 31 bits are a count of readers.
  int lock;
}
tmc_spin_rwlock_t;

#ifndef __DOXYGEN__

#define TMC_SPIN_WRITE_LOCK_BIT (1 << 31)

void __tmc_spin_rwlock_rdlock_slow(tmc_spin_rwlock_t*);
void __tmc_spin_rwlock_wrlock_slow(tmc_spin_rwlock_t*, int);

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
//! In principle, the reader count can overflow, but only when acquiring
//! 2^31 reader locks on a given rwlock.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_rdlock(tmc_spin_rwlock_t* rwlock)
{
  int val = __insn_fetchaddgez4(&rwlock->lock, 1);
  if (__builtin_expect(val < 0, 0))
    __tmc_spin_rwlock_rdlock_slow(rwlock);
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
//! Note that we do not enforce fairness on the would-be writers when
//! acquiring the lock; instead, they all just spin trying to acquire it.
//!
//! @param rwlock The rwlock object.
//!
static __USUALLY_INLINE void
tmc_spin_rwlock_wrlock(tmc_spin_rwlock_t* rwlock)
{
  int val = __insn_fetchor4(&rwlock->lock, TMC_SPIN_WRITE_LOCK_BIT);
  if (__builtin_expect(val != 0, 0))
    __tmc_spin_rwlock_wrlock_slow(rwlock, val);
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
  // We return failure (1) when there's a writer - i.e. the old value
  // is negative.
  int rc = (int)__insn_fetchaddgez4(&rwlock->lock, 1) < 0;

  // Use a heavier-weight atomic barrier here, in case the user
  // proceeds to read some values and only later verifies that they
  // actually had the lock and thus read valid data.
  arch_atomic_acquire_barrier_value(rc);

  return rc;
}

//! Try to write-lock an rwlock, but return immediately.
//!
//! @param rwlock The rwlock object.
//! @return 0 on success, non-zero if the lock could not be obtained.
//!
static __USUALLY_INLINE int
tmc_spin_rwlock_trywrlock(tmc_spin_rwlock_t* rwlock)
{
  int val = __insn_fetchor4(&rwlock->lock, TMC_SPIN_WRITE_LOCK_BIT);
  if (__builtin_expect(val != 0, 0))
  {
    if (val >= 0)
      __insn_fetchand4(&rwlock->lock, ~TMC_SPIN_WRITE_LOCK_BIT);
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
  arch_atomic_release_barrier();
  __insn_fetchadd4(&rwlock->lock, -1);
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
  arch_atomic_release_barrier();

  // Release the lock ("exchange" will push the value immediately).
  arch_atomic_exchange(&rwlock->lock, 0);
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
static __USUALLY_INLINE void
tmc_spin_rwlock_unlock(tmc_spin_rwlock_t *rwlock)
{
  arch_atomic_release_barrier();
  // Try to cmpxchg() as if we are holding the write lock, and if
  // that fails, we are a read lock so decrement the read count.
  __insn_mtspr(SPR_CMPEXCH_VALUE, TMC_SPIN_WRITE_LOCK_BIT);
  if (__insn_cmpexch4(&rwlock->lock, 0) != TMC_SPIN_WRITE_LOCK_BIT)
    __insn_fetchadd4(&rwlock->lock, -1);
}


/***********************************************************************/
/*                           Spin-Based Barrier                        */
/***********************************************************************/

//! A shared memory, spin-based barrier structure.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
typedef struct
{
  //! A word holding the state of the barrier.
  int val;
}
tmc_spin_barrier_t;

#ifndef __DOXYGEN__
// "Parity" is bit 0, and alternates with each barrier wait.  "Count"
// is bits 1-15, representing the number of threads that have not yet
// reached this barrier, and "reset" is bits 17-31, representing the
// number of threads the barrier was designed for.  We leave an unused
// bit between "count" and "reset" to simplify the code.  "Parity"
// must always be one or zero with this macro; "count" and "reset"
// will have any bits above 15 masked off.
#define __TMC_SPIN_BARRIER_VALUE(p, c, r) \
  ((p) | ((c) & 0x7fff) << 1 | (r) << 17)

// Extract components of the spin barrier value.
// "Count" and "reset" are signed, so we always shift down from the top.
#define __TMC_SPIN_BARRIER_PARITY(n) ((n) & 1)
#define __TMC_SPIN_BARRIER_COUNT(n) (((n) << 16) >> 17)
#define __TMC_SPIN_BARRIER_RESET(n) ((n) >> 17)

// Amount to add to tmc_spin_barrier_t to increment the count field.
#define __TMC_SPIN_BARRIER_COUNT_DELTA 2
#endif

//! Static initializer for a tmc_spin_barrier_t.
//!
//! @param NUM_TASKS The number of processes or threads that must call
//! tmc_spin_barrier_wait() on this barrier in order for the barrier
//! operation to complete.
//!
#define TMC_SPIN_BARRIER_INIT(NUM_TASKS) \
  { __TMC_SPIN_BARRIER_VALUE(0, (NUM_TASKS), (NUM_TASKS)) }

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
  barrier->val = __TMC_SPIN_BARRIER_VALUE(0, num_tasks, num_tasks);
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


/***********************************************************************/
/*                      Spin-Based Condition Variable                  */
/***********************************************************************/

#ifndef __DOXYGEN__
// Number of words in the tmc_spin_cond_t alloc[] and wait[] arrays.
#define __TMC_SPIN_COND_WORDS (CHIP_L2_LINE_SIZE() / sizeof(unsigned long))
#endif

//! A shared memory, spin-based condition variable.
//!
//! This object can be located in any coherent shared memory region,
//! even regions that are mapped at different VAs in different
//! processes.
//!
//! The structure is aligned to a cacheline for performance
//! reasons, but it is not required to be aligned for correctness.
//!
typedef struct
{
  //! An array showing which id bits have been allocated to waiters.
  unsigned long alloc[__TMC_SPIN_COND_WORDS];

  //! An array showing which ids are currently waiting.
  unsigned long wait[__TMC_SPIN_COND_WORDS];

} __attribute__((aligned(CHIP_L2_LINE_SIZE())))
tmc_spin_cond_t;

//! Static initializer for a tmc_spin_cond_t.
#define TMC_SPIN_COND_INIT { { 0 }, { 0 } }

//! Initialize a condvar to have no waiters.
//!
//! @param cond The condition variable to be initialized.
//!
static __USUALLY_INLINE void
tmc_spin_cond_init(tmc_spin_cond_t* cond)
{
  memset(cond, 0, sizeof(*cond));
}

//! Wait until the given tmc_spin_cond_t has been signaled.
//!
//! Must be called with the given tmc_spin_mutex_t lock held.
//! The lock is released while the routine is spinning waiting to
//! to be signalled, and then reacquired before the routine returns.
//!
//! The same lock must be used for concurrent tmc_spin_cond_wait_spin()
//! or tmc_spin_cond_wait_queued() operations on the same condition variable.
//!
//! @param cond The condition variable being waited on.
//! @param lock The locked mutex used to guard the conditional variable.
//! @param abstime Absolute time at which to return with a timeout,
//!   or NULL to wait forever.
//! @return 0 on success, ETIMEDOUT if the timeout expires,
//!   or EOVERFLOW if there are too many waiters.
//!
int tmc_spin_cond_timedwait_spin(tmc_spin_cond_t* cond,
                                 tmc_spin_mutex_t* lock,
                                 const struct timespec *abstime);

//! Wait until the given tmc_spin_cond_t has been signaled.
//!
//! See tmc_spin_cond_timedwait_spin() for API details.
//!
int tmc_spin_cond_timedwait_queued(tmc_spin_cond_t* cond,
                                   tmc_spin_queued_mutex_t* lock,
                                   const struct timespec *abstime);

#ifdef __BME__
//! Specify a function that implements what a timeout means for BME.
//!
//! Note that BME does not define "struct timespec", so the application
//! itself must define the struct (perhaps just containing a single
//! 64-bit cycle counter value), as well as providing the implementation
//! of this function.  If no implementation is provided, then no
//! timeout functionality will be available in the BME application.
//!
//! @param ts The absolute time after which to return a timeout.
//! @return 0 for no timeout; any other value returned by the application's
//!    implementation of this function will be returned directly from
//!    the tmc_spin_cond_timedwait_spin() or time_spin_cond_timewait_queued()
//!    routines.
//!
int tmc_spin_cond_timedout(const struct timespec *ts);
#endif

//! Wait forever until the given tmc_spin_cond_t has been signaled.
//!
//! See tmc_spin_cond_timedwait_spin() for API details.
//!
static __USUALLY_INLINE int
tmc_spin_cond_wait_spin(tmc_spin_cond_t* cond, tmc_spin_mutex_t* lock)
{
  return tmc_spin_cond_timedwait_spin(cond, lock, NULL);
}

//! Wait forever until the given tmc_spin_cond_t has been signaled.
//!
//! See tmc_spin_cond_timedwait_spin() for API details.
//!
static __USUALLY_INLINE int
tmc_spin_cond_wait_queued(tmc_spin_cond_t* cond, tmc_spin_queued_mutex_t* lock)
{
  return tmc_spin_cond_timedwait_queued(cond, lock, NULL);
}

//! Signal one of the tasks that are blocked on the condition variable.
//!
//! Normally the same lock should be held when calling this function as is
//! held by the tmc_spin_cond_wait_spin() or tmc_spin_cond_wait_queued()
//! callers.  If not, it's possible that a concurrent call to this method
//! and one of the wait methods would result in a waiter left waiting.
//!
//! @param cond The condition variable being waited on
//! @return 0 on success
//!
int tmc_spin_cond_signal(tmc_spin_cond_t* cond);

//! Signal all of the tasks that are blocked on the condition variable.
//!
//! Normally the same lock should be held when calling this function as is
//! held by the tmc_spin_cond_wait_spin() or tmc_spin_cond_wait_queued()
//! callers.  If not, it's possible that a concurrent call to this method
//! and one of the wait methods would result in a waiter left waiting.
//!
//! @param cond The condition variable being waited on
//! @return 0 on success
//!
int tmc_spin_cond_broadcast(tmc_spin_cond_t* cond);

__END_DECLS

#endif /* __TMC_SPIN_H__ */

//! @}
