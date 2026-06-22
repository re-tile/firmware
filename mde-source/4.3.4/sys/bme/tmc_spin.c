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

#include "tmc/spin.h"
#include <arch/atomic.h>
#include <arch/chip.h>
#include <arch/spr.h>

#ifdef __BME__
#include <bme/sys_info.h>
#endif

#ifndef __BME__
#include <sys/time.h>
#include <errno.h>
#else
// The actual value doesn't really matter as long as it's non-zero.
#define EOVERFLOW 75
#endif

#include "backoff.h"

// macro for MIN(a,b)
#define MIN(A,B) (((A)<(B))?(A):(B))

// macro to wrap a boolean with __builtin_expect
#define likely(cond) __builtin_expect((cond), 1)
#define unlikely(cond) __builtin_expect((cond), 0)


// Use cmpexch4 to avoid invalidating the cache line of the lock holder.
void
__tmc_spin_mutex_wait(tmc_spin_mutex_t* mutex)
{
  int backoff = 0;

  while (arch_atomic_val_compare_and_exchange(&mutex->lock, 0, 1) != 0)
    exp_backoff(backoff++);
}


// Read the word at "p" without loading the line into the cache.
// This avoids subsequent sharer invalidation when the line is written.
// Choose an unlikely "compare" value to avoid causing invalidations.
//
#define read_noalloc(p) arch_atomic_val_compare_and_exchange((p), -77, -77)


// Wait until the high bits (current) match my ticket.
// If we notice the overflow bit set on entry, we clear it.
//
void
__tmc_spin_queued_mutex_lock_slow(tmc_spin_queued_mutex_t* mutex,
                                  unsigned int my_ticket)
{
  if (unlikely(my_ticket & TMC_SPIN_NEXT_OVERFLOW))
  {
    __insn_fetchand4(&mutex->lock, ~TMC_SPIN_NEXT_OVERFLOW);
    my_ticket &= ~TMC_SPIN_NEXT_OVERFLOW;
  }
  
  while (1)
  {
    unsigned int val = read_noalloc(&mutex->lock);
    unsigned int delta = my_ticket - __tmc_spin_current(val);
    if (delta == 0)
      return;
    relax((128 / CYCLES_PER_RELAX_LOOP) * delta);
  }
}


// Check the lock to see if it is plausible, and try to get it with cmpxchg.
//
int tmc_spin_queued_mutex_trylock(tmc_spin_queued_mutex_t* mutex)
{
  unsigned int val = read_noalloc(&mutex->lock);
  if (unlikely(__tmc_spin_current(val) != __tmc_spin_next(val)))
    return 1;
  return arch_atomic_bool_compare_and_exchange(
    &mutex->lock, val, (val + 1) & ~TMC_SPIN_NEXT_OVERFLOW) ? 0 : 1;
}



// If the read lock fails due to a writer, we retry with backoff until
// the lock is non-negative, thus giving priority to waiting writers.
//
void __tmc_spin_rwlock_rdlock_slow(tmc_spin_rwlock_t* rwlock)
{
  int iterations = 0;
  int val;
  do
  {
    exp_backoff(iterations++);
    val = __insn_fetchaddgez4(&rwlock->lock, 1);
  } while (unlikely(val < 0));
}

// If the write lock fails because another writer had it, we wait for
// that writer to release the lock, and re-acquire it for ourselves;
// otherwise we just leave our lock bit in place and wait for all
// the readers to finish.
//
void __tmc_spin_rwlock_wrlock_slow(tmc_spin_rwlock_t* rwlock, int val)
{
  unsigned int iterations = 0;

  while (val < 0)
  {
    // Wait for any writers to finish.
    while (read_noalloc(&rwlock->lock) < 0)
      exp_backoff(iterations++);

    // See if we can grab the writer bit.
    val = __insn_fetchor4(&rwlock->lock, TMC_SPIN_WRITE_LOCK_BIT);

    // Grabbed, and we have no more readers, so we're good.
    if (likely(val == 0))
      return;

    // Go back and see if we at least grabbed the write lock.
    iterations = 0;
  }

  // Now we have the write lock, so just wait for readers to finish.
  while ((read_noalloc(&rwlock->lock) & ~TMC_SPIN_WRITE_LOCK_BIT) != 0)
    exp_backoff(iterations++);
}


int
tmc_spin_barrier_wait(tmc_spin_barrier_t* barrier)
{
  int backoff = 0;
  int result = 0;

  // Make sure all previous memory writes are visible.
  // We treat a barrier wait as a critical-section "release" operation
  // since threads may assume shared memory is in some "well-known"
  // state when they enter the region following the barrier.
  arch_atomic_release_barrier();

  int old = arch_atomic_sub(&barrier->val, __TMC_SPIN_BARRIER_COUNT_DELTA);

#ifdef __BME__
  // Check for a statically initialized barrier that is supposed to
  // go across all BME cpus.  If so, everyone tries to set the count
  // field based on how far it's been decremented, and the tile for which
  // the value of "old" does not change since it was read succeeds.
  if (unlikely(old <= 0))
  {
    old -= __TMC_SPIN_BARRIER_COUNT_DELTA;  // value we left in memory
    int num_tiles = bme_num_tiles();
    int reset = num_tiles;
    int count = num_tiles + __TMC_SPIN_BARRIER_COUNT(old);
    int new = __TMC_SPIN_BARRIER_VALUE(0, count, reset);
    arch_atomic_val_compare_and_exchange(&barrier->val, old, new);

    // If we're the last tile then adjust "old" so that the rest of
    // the logic works and we release and reset the barrier.  The count
    // only decrements if we're in this statement, so we don't have to
    // think about the third-from-last tile seeming like the last.
    if (count == 0)
      old += __TMC_SPIN_BARRIER_COUNT_DELTA * 2;
  }
#endif

  int parity = __TMC_SPIN_BARRIER_PARITY(old);
  int count = __TMC_SPIN_BARRIER_COUNT(old);
  int reset = __TMC_SPIN_BARRIER_RESET(old);
  if (count == 1)
  {
    // Last thread to visit barrier.
    int new = __TMC_SPIN_BARRIER_VALUE(parity ^ 1, reset, reset);
    arch_atomic_exchange(&barrier->val, new); // store modified val immediately
    result = 1;
  }
  else
  {
    // Wait for release.
    backoff = 0;
    while (1)
    {
      if (__TMC_SPIN_BARRIER_PARITY(barrier->val) != parity)
        break;

      exp_backoff(backoff++);
    }
  }

  return result;
}

#ifndef __BME__
// Note that glibc's implementation of gettimeofday() uses a vdso page
// to avoid invoking the kernel, so calling gettimeofday() in a loop
// is reasonable if we just need to be spinning anyway.
static int
tmc_spin_cond_timedout(const struct timespec *ts)
{
  if (ts)
  {
    struct timeval now;
    if (gettimeofday(&now, NULL) < 0)
      return errno;
    if (now.tv_sec > ts->tv_sec ||
        (now.tv_sec == ts->tv_sec && now.tv_usec * 1000 >= ts->tv_nsec))
      return ETIMEDOUT;
  }
  return 0;
}
#else
// Provide a default function that always just returns "no timeout".
int __attribute__((weak))
tmc_spin_cond_timedout(const struct timespec *ts)
{
  return 0;
}
#endif

// We use atomic "and" in the code below for correctness, to handle the
// possibility of racing with other signalers.  In other places we also
// use atomic ops when accessing the wait[] array, rather than regular
// memory ops, to take advantage of the fact that atomic ops do not
// pull the cache line into the local core's cache, which would then
// require the home cache to have to spend time invalidating the copied
// cache lines as soon as another atomic modification was done to it.
// We use "or with zero" to inspect the values with altering them.
#define arch_atomic_read(p) arch_atomic_or((p), 0)

#define TMC_SPIN_COND_TEMPLATE(LOCKNAME, LOCKTYPE)                      \
  int                                                                   \
  tmc_spin_cond_timedwait_##LOCKNAME(tmc_spin_cond_t* cond,             \
                                     LOCKTYPE##_t* lock,                \
                                     const struct timespec *ts)         \
  {                                                                     \
    unsigned int my_word;                                               \
    unsigned long my_bitmask;                                           \
    int retval = 0;                                                     \
    int backoff = 0;                                                    \
                                                                        \
    for (my_word = 0; ; ++my_word)                                      \
    {                                                                   \
      if (my_word >= __TMC_SPIN_COND_WORDS)                             \
        return EOVERFLOW;                                               \
      unsigned long word = ~cond->alloc[my_word];                       \
      if (word)                                                         \
      {                                                                 \
        my_bitmask = 1UL << __builtin_ctzl(word);                       \
        break;                                                          \
      }                                                                 \
    }                                                                   \
                                                                        \
    /* Set the "allocated" and "waiting" bits. */                       \
    cond->alloc[my_word] |= my_bitmask;                                 \
    arch_atomic_or(&cond->wait[my_word], my_bitmask);                   \
                                                                        \
    LOCKTYPE##_unlock(lock);                                            \
    while ((arch_atomic_read(&cond->wait[my_word]) & my_bitmask) != 0)  \
    {                                                                   \
      retval = tmc_spin_cond_timedout(ts);                              \
      if (retval)                                                       \
      {                                                                 \
        /* Clear the waiter bit since we're giving up. */               \
        arch_atomic_and(&cond->wait[my_word], ~my_bitmask);             \
        break;                                                          \
      }                                                                 \
      exp_backoff(backoff++);                                           \
    }                                                                   \
    LOCKTYPE##_lock(lock);                                              \
                                                                        \
    /* Clear the "allocated" bit. */                                    \
    cond->alloc[my_word] &= ~my_bitmask;                                \
                                                                        \
    return retval;                                                      \
  }

TMC_SPIN_COND_TEMPLATE(spin, tmc_spin_mutex)
TMC_SPIN_COND_TEMPLATE(queued, tmc_spin_queued_mutex)

// Signal one of the tasks that are blocked on the condition variable.
// Note that if the lock is held, the "alloc" array will be frozen and
// no waiters can come or go; they can just be signaled and have their
// "wait" bit cleared.  As a result, there's no possibility of a race
// between this signalling code and the waiter.  However, if the lock is
// not held, a new waiter can start on a low-index word while we are
// busy searching the high-index words, and we might return without
// signaling someone who started to wait after we were called.
//
int
tmc_spin_cond_signal(tmc_spin_cond_t* cond)
{
  for (int i = 0; i < __TMC_SPIN_COND_WORDS; i++)
  {
    unsigned long word = arch_atomic_read(&cond->wait[i]);

    while (word)
    {
      // Get the first waiter in this word and remove it from wait[].
      unsigned long mask = 1UL << __builtin_ctzl(word);
      word = arch_atomic_and(&cond->wait[i], ~mask);

      // If that waiter was still waiting, we're done.
      if (word & mask)
        return 0;
    }
  }

  // We found no waiters.
  return 0;
}

// If the lock is held, we guarantee that no new waiter bits are
// currently being set, so we will safely clear them all.
// 
int
tmc_spin_cond_broadcast(tmc_spin_cond_t* cond)
{
  for (int i = 0; i < __TMC_SPIN_COND_WORDS; ++i)
  {
    // Clear all the waiter bits.
    // Use atomic op to avoid filling the cache.
    arch_atomic_and(&cond->wait[i], 0);
  }
  return 0;
}
