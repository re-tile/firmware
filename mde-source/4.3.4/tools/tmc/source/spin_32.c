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
#include <arch/chip.h>
#include <arch/spr.h>

#ifdef __BME__
#include <bme/sys_info.h>
#endif

#include "backoff.h"

// macro for MIN(a,b)
#define MIN(A,B) (((A)<(B))?(A):(B))

// macro to wrap a boolean with __builtin_expect
#define likely(cond) __builtin_expect((cond), 1)
#define unlikely(cond) __builtin_expect((cond), 0)


void
__tmc_spin_mutex_wait(tmc_spin_mutex_t* mutex)
{
  int backoff = 0;

  while (__insn_tns(&mutex->lock) != 0)
    exp_backoff(backoff++);
}



void
tmc_spin_queued_mutex_lock(tmc_spin_queued_mutex_t* mutex)
{
  int my_ticket;
  int backoff = 0;
  
  while ((my_ticket = __insn_tns(&mutex->next_ticket)) & 1)
    exp_backoff(backoff++);
  
  // Increment the next ticket number, implicitly releasing tns lock.
  mutex->next_ticket = my_ticket + TMC_SPIN_TICKET_QUANTUM;
  
  // Wait until it's our turn.
  int delta;
  while ((delta = my_ticket - mutex->current_ticket) != 0)
    relax((128 / CYCLES_PER_RELAX_LOOP) * delta);
}


int
tmc_spin_queued_mutex_trylock(tmc_spin_queued_mutex_t* mutex)
{
  // Grab a ticket; no need to retry if it's busy, we'll just treat
  // that the same as "locked", since someone else will lock it
  // momentarily anyway.
  int my_ticket = __insn_tns(&mutex->next_ticket);
  
  if (my_ticket == mutex->current_ticket)
  {
    // Not currently locked, so lock it by keeping this ticket.
    mutex->next_ticket = my_ticket + TMC_SPIN_TICKET_QUANTUM;
    // Success!
    return 0;
  }
  
  if (!(my_ticket & 1))
  {
    // Release next_ticket.
    mutex->next_ticket = my_ticket;
  }
  
  return 1;
}


// The low byte is always reserved to be the marker for a "tns" operation
// since the low bit is set to "1" by a tns.  The next seven bits are
// zeroes.  The next byte holds the "next" writer value, i.e. the ticket
// available for the next task that wants to write.  The third byte holds
// the current writer value, i.e. the writer who holds the current ticket.
// If current == next == 0, there are no interested writers.
#define WR_NEXT_SHIFT   8
#define WR_CURR_SHIFT   16
#define WR_WIDTH        8
#define WR_MASK         ((1 << WR_WIDTH) - 1)

// The last eight bits hold the active reader count.  This has to be
// zero before a writer can start to write.
#define RD_COUNT_SHIFT  24
#define RD_COUNT_WIDTH  8
#define RD_COUNT_MASK   ((1 << RD_COUNT_WIDTH) - 1)
#if RD_COUNT_SHIFT != TMC_SPIN_RD_COUNT_SHIFT || \
    RD_COUNT_WIDTH != TMC_SPIN_RD_COUNT_WIDTH || \
    WR_NEXT_SHIFT != TMC_SPIN_WR_NEXT_SHIFT
# error Mismatch between source and header
#endif


// Lock the word, spinning until there are no tns-ers.
__inline unsigned int
__tmc_spin_rwlock_get(tmc_spin_rwlock_t* rwlock)
{
  unsigned int iterations = 0;
  while (1)
  {
    unsigned int val = __insn_tns((int*)&rwlock->lock);
    if (unlikely(val & 1))
    {
      exp_backoff(iterations++);
      continue;
    }
    return val;
  }
}


void
__tmc_spin_rwlock_wrunlock_slow(tmc_spin_rwlock_t* rwlock, unsigned int val)
{
  unsigned int eq, mask = 1 << WR_CURR_SHIFT;
  while (unlikely(val & 1))
  {
    // Limited backoff since we are the highest-priority task.
    relax(4);
    val = __insn_tns((int*)&rwlock->lock);
  }
  val = __insn_addb(val, mask);
  eq = __insn_seqb(val, val << (WR_CURR_SHIFT - WR_NEXT_SHIFT));
  val = __insn_mz(eq & mask, val);
  rwlock->lock = val;
}

void
tmc_spin_rwlock_unlock(tmc_spin_rwlock_t *rwlock)
{
  unsigned int val = __tmc_spin_rwlock_get(rwlock);
  if (val >> TMC_SPIN_RD_COUNT_SHIFT)  // we must be holding a read lock
    rwlock->lock = val - (1 << TMC_SPIN_RD_COUNT_SHIFT);
  else
    __tmc_spin_rwlock_wrunlock_slow(rwlock, val);
}


// We spin until everything but the reader bits (which are in the high
// part of the word) are zero, i.e. no active or waiting writers, no tns.
//
// ISSUE: This approach can permanently starve readers.  A reader who sees
// a writer could instead take a ticket lock (just like a writer would),
// and atomically enter read mode (with 1 reader) when it gets the ticket.
// This way both readers and writers will always make forward progress
// in a finite time.
//
void
__tmc_spin_rwlock_rdlock_slow(tmc_spin_rwlock_t* rwlock, unsigned int val)
{
  unsigned int iterations = 0;
  do
  {
    if (!(val & 1))
      rwlock->lock = val;
    exp_backoff(iterations++);
    val = __insn_tns((int*)&rwlock->lock);
  } while ((val << RD_COUNT_WIDTH) != 0);
  rwlock->lock = val + (1 << TMC_SPIN_RD_COUNT_SHIFT);
}


void
__tmc_spin_rwlock_wrlock_slow(tmc_spin_rwlock_t* rwlock, unsigned int val)
{
  // Take out the next ticket; this will also stop would-be readers.
  if (val & 1)
    val = __tmc_spin_rwlock_get(rwlock);
  rwlock->lock = __insn_addb(val, 1 << WR_NEXT_SHIFT);

  // Extract my ticket value from the original word.
  // The trailing underscore here and below (curr_) reminds us that
  // the high bits are garbage; we mask them out later on.
  unsigned int my_ticket_ = val >> WR_NEXT_SHIFT;

  // Wait until the "current" field matches our ticket, and
  // there are no remaining readers.
  while (1)
  {
    unsigned int curr_ = val >> WR_CURR_SHIFT;
    unsigned int readers = val >> RD_COUNT_SHIFT;
    unsigned int delta = ((my_ticket_ - curr_) & WR_MASK) + !!readers;
    if (likely(delta == 0))
      break;

    // Delay according to how many lock-holders are still out there.
    relax((256 / CYCLES_PER_RELAX_LOOP) * delta);

    // Get a non-tns value to check; we don't need to tns it ourselves.
    // Since we're not tns'ing, we retry more rapidly to get a valid value.
    while ((val = rwlock->lock) & 1)
      relax(4);
  }
}

#ifdef __BIG_ENDIAN__
# error Port barrier code to match big-endian TILE-Gx code
#endif

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

  tmc_spin_barrier_t old;
  while (1)
  {
    old.n = __insn_tns(&barrier->n);
    if (!old.s.busy)
      break;

    // Spin reading memory until it looks available (test-and-tns).
    while (barrier->s.busy)
      exp_backoff(backoff++);
  }
  
#ifdef __BME__
  // Check for a statically initialized barrier that is supposed to
  // go across all BME cpus.
  if (unlikely(old.s.count == TMC_SPIN_BARRIER_ALL))
  {
    unsigned int num_tiles = bme_num_tiles();
    old.s.count = num_tiles;
    old.s.reset = num_tiles;
  }
#endif

  if (old.s.count == 1)
  {
    // Last thread to visit barrier.
    old.s.count = old.s.reset;
    old.s.parity ^= 1;
    barrier->n = old.n;
    result = 1;
  }
  else
  {
    // Decrement reference count, releasing the tns lock.
    --old.s.count;
    barrier->n = old.n;

    // Wait for release.
    backoff = 0;
    while (1)
    {
      tmc_spin_barrier_t b = *barrier;
      if (b.s.parity != old.s.parity && !b.s.busy)
        break;

      exp_backoff(backoff++);
    }
  }

  return result;
}
