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

#ifndef __TMC_CACHELINE_QUEUE_H__
#define __TMC_CACHELINE_QUEUE_H__

#include <arch/atomic.h>

#include <tmc/queue_internal.h>
#include <tmc/spin.h>

// The credit counter lives in the high 32 bits.
#define TMC_QUEUE_CREDIT_SHIFT    (32)

// Bit mask of the credit counter.
#define TMC_QUEUE_CREDIT_MASK     ((1ULL << TMC_QUEUE_CREDIT_SHIFT) - 1)

// A single slot modifier that is to subtract one credit and add one to the
// index.
#define TMC_QUEUE_SINGLE_MODIFIER ((-1ULL << TMC_QUEUE_CREDIT_SHIFT) | 1)

// Bit mask of the credits_and_next_index. One bit is reserved to avoid
// the next_index running into the credits.
#define TMC_QUEUE_CREDIT_INDEX_MASK  (~(1ULL << (TMC_QUEUE_CREDIT_SHIFT - 1)))

#define TMC_QUEUE(NAME, OBJ_TYPE, LOG2_ENTRIES, FLAGS)                        \
                                                                              \
typedef struct                                                                \
{                                                                             \
  union                                                                       \
  {                                                                           \
      /* High 32 bits are a count of available enqueue credits, low 32 bits are\
       * the next slot index to enqueue. This is used when there are more than\
       * one enqueueing tasks. */                                             \
      int64_t credits_and_next_index;                                         \
                                                                              \
      /* Total 64 bits are a count of available enqueue credits, with default \
       * value 0. This is used when there is only a single enqueueing task. */\
      uint64_t write_credits;                                                 \
  };                                                                          \
} NAME##_credit_t;                                                            \
                                                                              \
typedef struct                                                                \
{                                                                             \
  NAME##_credit_t credits;                                                    \
                                                                              \
  uint64_t enqueue_count __attribute__((aligned(64)));                        \
  uint64_t dequeue_count __attribute__((aligned(64)));                        \
                                                                              \
  /* A lazy-updated count of how many slots have been dequeued. */            \
  uint64_t dequeue_complete_count __attribute__((aligned(64)));               \
                                                                              \
  struct                                                                      \
  {                                                                           \
    OBJ_TYPE obj;                                                             \
    unsigned char enqueue_gen;                                                \
  } __attribute__((aligned(32)))                                              \
    array[(1 << LOG2_ENTRIES)] __attribute__((aligned(64)));                  \
} NAME##_t;                                                                   \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_init(NAME##_t *queue)                                                  \
{                                                                             \
  memset(queue, 0, sizeof(*queue));                                           \
                                                                              \
  /* Make sure object is in the same cacheline. */                            \
  if (sizeof(queue->array[0]) > 64)                                           \
    return -1;                                                                \
                                                                              \
  /* Make sure array is 64B aligned. */                                       \
  if ((intptr_t)queue->array & (64 - 1))                                      \
    return -1;                                                                \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
  {                                                                           \
    queue->credits.credits_and_next_index =                                   \
      (1ULL << LOG2_ENTRIES) << TMC_QUEUE_CREDIT_SHIFT;                       \
  }                                                                           \
                                                                              \
  __insn_mf();                                                                \
                                                                              \
  return 0;                                                                   \
}                                                                             \
                                                                              \
static __USUALLY_INLINE void                                                  \
NAME##_enqueue_update_credits(NAME##_t *queue)                                \
{                                                                             \
  /* Read the 64-bit dequeue complete count without touching the cache, so    \
   * we later avoid having to evict any sharers of this cacheline when we     \
   * update it below. */                                                      \
  uint64_t orig_dequeue_complete_count =                                      \
    arch_atomic_val_compare_and_exchange(&queue->dequeue_complete_count,      \
                                         -1, -1);                             \
                                                                              \
  /* Make sure the load completes before we access the dequeue count. */      \
  arch_atomic_acquire_barrier_value(orig_dequeue_complete_count);             \
                                                                              \
  /* Read the dequeue count which is updated by the receiver(s). */           \
  uint64_t count = arch_atomic_access_once(queue->dequeue_count);             \
                                                                              \
  /* Calculate the number of dequeue completions since we last updated        \
   * the 64-bit counter. */                                                   \
  uint64_t delta = (count - orig_dequeue_complete_count) &                    \
                   TMC_QUEUE_CREDIT_MASK;                                     \
  if (delta == 0)                                                             \
    return;                                                                   \
                                                                              \
  /* Try to write back the count, advanced by delta. If we race with another  \
   * thread, this might fail, in which case we return immediately on the      \
   * assumption that some credits are (or at least were) available. */        \
  uint64_t new_count = orig_dequeue_complete_count + delta;                   \
  if (arch_atomic_val_compare_and_exchange(&queue->dequeue_complete_count,    \
                                           orig_dequeue_complete_count,       \
                                           new_count) !=                      \
                                           orig_dequeue_complete_count)       \
    return;                                                                   \
                                                                              \
  /* We succeeded in advancing the completion count; add back the corresponding\
   * number of dequeue credits. */                                            \
  if (__builtin_expect(__insn_fetchadd(&queue->credits.credits_and_next_index,\
                       delta << TMC_QUEUE_CREDIT_SHIFT) &                     \
                       ~TMC_QUEUE_CREDIT_INDEX_MASK, 0))                      \
  {                                                                           \
    /* Clear the reserved bit to avoid the next_index running into the        \
     * credits. */                                                            \
    __insn_fetchand(&queue->credits.credits_and_next_index,                   \
                    TMC_QUEUE_CREDIT_INDEX_MASK);                             \
  }                                                                           \
}                                                                             \
                                                                              \
static __NEVER_INLINE int                                                     \
NAME##_enqueue(NAME##_t *queue, OBJ_TYPE object)                              \
{                                                                             \
  uint64_t enqueue_count;                                                     \
                                                                              \
  /* Multiple enqueueing tasks with HW locks. */                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
  {                                                                           \
    /* Try to reserve one enqueue slot. We do this with the single slot modifier\
     * that subtracts one credit and adds one to the index, and using         \
     * fetchaddgez to only apply it if the credits count doesn't go negative. */\
    int64_t old = __insn_fetchaddgez(&queue->credits.credits_and_next_index,  \
                                     (int64_t) TMC_QUEUE_SINGLE_MODIFIER);    \
                                                                              \
    if (__builtin_expect(old + (int64_t) TMC_QUEUE_SINGLE_MODIFIER < 0, 0))   \
    {                                                                         \
      /* We are out of credits. Try once to get more by checking for completed\
       * dequeue count. */                                                    \
      NAME##_enqueue_update_credits(queue);                                   \
      old = __insn_fetchaddgez(&queue->credits.credits_and_next_index,        \
                               (int64_t) TMC_QUEUE_SINGLE_MODIFIER);          \
      if (old + (int64_t) TMC_QUEUE_SINGLE_MODIFIER < 0)                      \
        return -1;                                                            \
    }                                                                         \
                                                                              \
    /* Abstract the index to enqueue. */                                      \
    enqueue_count = old & TMC_QUEUE_CREDIT_MASK;                              \
  }                                                                           \
  else /* Only a single enqueueing task and no lock is needed. */             \
  {                                                                           \
    /* Update enqueue credit. */                                              \
    if (__builtin_expect(!queue->credits.write_credits, 0))                   \
    {                                                                         \
      queue->credits.write_credits = (uint64_t) (1 << LOG2_ENTRIES) -         \
        queue->enqueue_count + arch_atomic_access_once(queue->dequeue_count); \
                                                                              \
      if (!queue->credits.write_credits)                                      \
        return -1;                                                            \
    }                                                                         \
                                                                              \
    queue->credits.write_credits--;                                           \
    enqueue_count = queue->enqueue_count;                                     \
    queue->enqueue_count = enqueue_count + 1;                                 \
  }                                                                           \
                                                                              \
  unsigned int i = enqueue_count & ((1 << LOG2_ENTRIES) - 1);                 \
  unsigned char gen = (enqueue_count >> LOG2_ENTRIES) & 0xff;                 \
  unsigned char next_gen = (gen + 1) & 0xff;                                  \
                                                                              \
  queue->array[i].obj = object;                                               \
  arch_atomic_compiler_barrier(); /* Don't let the following store move up. */\
  queue->array[i].enqueue_gen = next_gen;                                     \
                                                                              \
  return 0;                                                                   \
}                                                                             \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_enqueue_multiple(NAME##_t *queue,                                      \
                        OBJ_TYPE *object,                                     \
                        unsigned int enqueue_num)                             \
{                                                                             \
  uint64_t enqueue_count;                                                     \
                                                                              \
  /* Multiple enqueueing tasks with HW locks. */                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
  {                                                                           \
    /* Try to reserve 'enqueue_num' enqueue slots. We do this by constructing a\
     * constant that subtracts N credits and adds N to the index, and using   \
     * fetchaddgez to only apply it if the credits count doesn't go negative. */\
    int64_t modifier = ((int64_t) (-enqueue_num)) << TMC_QUEUE_CREDIT_SHIFT | \
                       enqueue_num;                                           \
    int64_t old = __insn_fetchaddgez(&queue->credits.credits_and_next_index, modifier);\
                                                                              \
    if (__builtin_expect(old + modifier < 0, 0))                              \
    {                                                                         \
      /* We are out of credits. Try once to get more by checking for completed\
       * dequeue count. */                                                    \
      NAME##_enqueue_update_credits(queue);                                   \
      old = __insn_fetchaddgez(&queue->credits.credits_and_next_index, modifier);\
      if (old + modifier < 0)                                                 \
        return -1;                                                            \
    }                                                                         \
                                                                              \
    /* Abstract the first index to enqueue. */                                \
    enqueue_count = old & TMC_QUEUE_CREDIT_MASK;                              \
  }                                                                           \
  else /* Only a single enqueueing task and no lock is needed. */             \
  {                                                                           \
    if (__builtin_expect(queue->credits.write_credits < enqueue_num, 0))      \
    {                                                                         \
      queue->credits.write_credits = (uint64_t) (1 << LOG2_ENTRIES) -         \
        queue->enqueue_count + arch_atomic_access_once(queue->dequeue_count); \
                                                                              \
      if (queue->credits.write_credits < enqueue_num)                         \
        return -1;                                                            \
    }                                                                         \
                                                                              \
    queue->credits.write_credits -= enqueue_num;                              \
    enqueue_count = queue->enqueue_count;                                     \
    queue->enqueue_count = enqueue_count + enqueue_num;                       \
  }                                                                           \
                                                                              \
  while (__builtin_expect(enqueue_num-- != 0, 1))                             \
  {                                                                           \
    unsigned int i = enqueue_count & ((1 << LOG2_ENTRIES) - 1);               \
    unsigned char gen = (enqueue_count++ >> LOG2_ENTRIES) & 0xff;             \
    unsigned char next_gen = (gen + 1) & 0xff;                                \
                                                                              \
    queue->array[i].obj = *(object++);                                        \
    arch_atomic_compiler_barrier(); /* Don't let the following store move up. */\
    queue->array[i].enqueue_gen = next_gen;                                   \
  }                                                                           \
                                                                              \
  return 0;                                                                   \
}                                                                             \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_dequeue(NAME##_t *queue, OBJ_TYPE *output)                             \
{                                                                             \
  uint64_t dequeue_count = queue->dequeue_count;                              \
  unsigned int i = dequeue_count & ((1 << LOG2_ENTRIES) - 1);                 \
  unsigned char next_gen = ((dequeue_count >> LOG2_ENTRIES) + 1) & 0xff;      \
                                                                              \
  if (__builtin_expect(arch_atomic_access_once(queue->array[i].enqueue_gen) !=\
                       next_gen, 0))                                          \
    return -1;                                                                \
                                                                              \
  *output = queue->array[i].obj;                                              \
                                                                              \
  /* Compiler barrier requires preceding store to *output to have issued, and \
   * thus the load from queue->array[] to have completed, prior to updating   \
   * dequeue_count. */                                                        \
  arch_atomic_compiler_barrier();                                             \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_RECEIVER))                                   \
  {                                                                           \
    /* Try to write back the dequeue count, advanced by one. If we race with  \
     * another thread, this might fail, in which case we return immediately with\
     * an error which causes the user app to ignore the output data. */       \
    if (arch_atomic_val_compare_and_exchange(&queue->dequeue_count,           \
                                             dequeue_count,                   \
                                             dequeue_count + 1) !=            \
                                             dequeue_count)                   \
      return -1;                                                              \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    queue->dequeue_count = dequeue_count + 1;                                 \
  }                                                                           \
                                                                              \
  return 0;                                                                   \
}                                                                             \


#endif // __TMC_CACHELINE_QUEUE_H__

