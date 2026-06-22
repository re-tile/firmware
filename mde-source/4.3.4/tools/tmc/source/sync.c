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

#include <tmc/sync.h>
#include <sys/time.h>
#include <linux/futex.h>
#include <limits.h>
#include <stdio.h>

// Perform a futex syscall.
#include <unistd.h>
#include <sys/syscall.h>
static inline int __futex(void* futex, int op, int val)
{
  return syscall(SYS_futex, futex, op, val, NULL);
}

#include "backoff.h"

// Internally, use names for the numbers.  Obviously, changes here must
// also be reflected in the use of numbers in the fastpaths in the header.
#define UNLOCKED	0
#define LOCKED		1
#define CONTENDED	2
#define SPINLOCKED	-1

// FIXME: should we spin a little before suspending in the kernel?
void
tmc_sync_mutex_wait(int* mutex)
{
  // What we believe is the lock's current state.  We could do a little
  // better by passing this value in from the fast path (in particular we
  // might be able to skip the first cmpxchg on the slow path), but we'd have
  // to change the API to pass in the value, which is too much churn.
  // We use "LOCKED" because that causes the value to be read from
  // memory properly on the first pass through the loop.
  int val = LOCKED;

  // Normally this routine will enter with the mutex already LOCKED
  // and set it to CONTENDED before sleeping in the kernel (or enter
  // with it already in CONTENDED state).  This routine must ensure that
  // after the mutex is unlocked, it resets it into CONTENDED so that
  // the task does a futex wakeup on unlock.  However, if this code races
  // with an unlock and we see the mutex unlocked on entry here, we don't
  // have to worry about that and can just set it LOCKED and not do a wakeup
  // on unlock.  More significantly, if the mutex is SPINLOCKED, then
  // when we finally acquire it we can safely just set it to LOCKED,
  // since no other would-be locker could have gone into the kernel
  // to wait during that period.  So we start optimistically by hoping
  // that we can leave the lock just in LOCKED state.
  int locked = LOCKED;

  // When handling SPINLOCKED locks, do exponential backoff.
  int iterations = 0;

  do
  {
    // If the current owner used tmc_sync_mutex_spin_lock(), it doesn't
    // want to do a futex wake, so we'll just back off here instead.
    if (val < 0)  // i.e. SPINLOCKED
      exp_backoff(iterations++);

    // If it's locked, try to mark it contended.
    if (val == LOCKED)
      val = arch_atomic_val_compare_and_exchange(mutex, LOCKED, CONTENDED);

    if (val > 0)  // i.e. LOCKED or CONTENDED
    {
      // The lock is now CONTENDED, so wait in the kernel until it's unlocked.
      __futex(mutex, FUTEX_WAIT, CONTENDED);
      locked = CONTENDED;
      iterations = 0;
    }

    // Try to lock.
    val = arch_atomic_val_compare_and_exchange(mutex, UNLOCKED, locked);
  }
  while (val != UNLOCKED);
}

void
tmc_sync_mutex_spin_wait(int* mutex)
{
  int iterations = 0;
  do
    exp_backoff(iterations++);
  while (arch_atomic_val_compare_and_exchange(
           mutex, UNLOCKED, SPINLOCKED) != UNLOCKED);
}

void
tmc_sync_mutex_wake(int* mutex)
{
  __futex(mutex, FUTEX_WAKE, 1);
}

int
tmc_sync_barrier_wait(tmc_sync_barrier_t* barrier)
{
  tmc_sync_mutex_lock(&barrier->lock);

  barrier->count--;
  if (barrier->count != 0)
  {
    // Still waiting for some tasks; wait until generation # advances.
    int generation = barrier->generation;
    tmc_sync_mutex_unlock(&barrier->lock);
    do {
      // Note that this might return EINTR, but that's okay because
      // we'll check the generation to make sure we actually made
      // progress.
      __futex(&barrier->generation, FUTEX_WAIT, generation);
    }
    while (generation == barrier->generation);

    return 0;
  }
  else
  {
    // We're the last task to arrive; advance generation # and reset count.
    barrier->count = barrier->num_tasks;
    barrier->generation++;
    __futex(&barrier->generation, FUTEX_WAKE, INT_MAX);
    tmc_sync_mutex_unlock(&barrier->lock);

    return 1;
  }
}
