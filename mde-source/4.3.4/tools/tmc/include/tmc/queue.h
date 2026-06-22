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
//! Macro-based support for shared memory FIFOs.

//! @addtogroup tmc_queue
//! @{
//!
//! Macro-based support for shared memory FIFOs.
//!
//! The tmc/queue.h and tmc/cacheline_queue.h headers provide
//! different implementations of the TMC_QUEUE() macro.  This macro
//! typedefs a queue object type and methods for managing that type.
//! The tmc/queue.h version of the macro is the most flexible
//! implementation, while tmc/cacheline_queue.h provides better
//! performance but requires that:
//!
//! - The data objects stored in the queue are less than a cacheline
//! length in size.
//! - The queue memory itself is aligned to a cacheline length, i.e.
//! 64 bytes.


#ifndef __TMC_QUEUE_H__
#define __TMC_QUEUE_H__

#include <arch/atomic.h>

#include <tmc/queue_internal.h>
#include <tmc/spin.h>

/** Define a queue type and methods for initializing, enqueueing, and
 * dequeueing from that type.
 *
 * This macro defines a new type NAME_t, and definitions for the
 * following methods:
 *
 * @verbatim
  int NAME_init(NAME_t *queue)
  int NAME_enqueue(NAME_t *queue, OBJ_TYPE object)
  int NAME_enqueue_multiple(NAME_t *queue, OBJ_TYPE *object, int count)
  int NAME_dequeue(NAME##_t *queue, OBJ_TYPE *output)
  @endverbatim
 *
 * The enqueue and dequeue methods return -1 if the queue is full or
 * empty, respectively.  A blocking enqueue or dequeue can be
 * implemented by spinning until the function returns 0.
 *
 * @param NAME The typename for the newly typedef'ed queue.
 * @param OBJ_TYPE The type of object stored in the queue.
 * @param LOG2_ENTRIES Logarithm, base 2, of the number of entries in
 * the queue.
 * @param FLAGS Performance optimization flags, including
 * ::TMC_QUEUE_SINGLE_SENDER or ::TMC_QUEUE_SINGLE_RECEIVER.
 */
#define TMC_QUEUE(NAME, OBJ_TYPE, LOG2_ENTRIES, FLAGS)                        \
                                                                              \
typedef struct                                                                \
{                                                                             \
  tmc_spin_queued_mutex_t enqueue_mutex;                                      \
  unsigned int enqueue_count;                                                 \
                                                                              \
  tmc_spin_queued_mutex_t dequeue_mutex __attribute__((aligned(64)));         \
  unsigned int dequeue_count;                                                 \
                                                                              \
  OBJ_TYPE array[(1 << LOG2_ENTRIES)] __attribute__((aligned(64)));           \
} NAME##_t;                                                                   \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_init(NAME##_t *queue)                                                  \
{                                                                             \
  tmc_spin_queued_mutex_t empty = TMC_SPIN_QUEUED_MUTEX_INIT;                 \
  queue->enqueue_mutex = empty;                                               \
  queue->dequeue_mutex = empty;                                               \
  queue->enqueue_count = 0;                                                   \
  queue->dequeue_count = 0;                                                   \
  __insn_mf();                                                                \
                                                                              \
  return 0;                                                                   \
}                                                                             \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_enqueue(NAME##_t *queue, OBJ_TYPE object)                              \
{                                                                             \
  int result;                                                                 \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
    tmc_spin_queued_mutex_lock(&queue->enqueue_mutex);                        \
                                                                              \
  if (__builtin_expect((queue->enqueue_count -                                \
                        arch_atomic_access_once(queue->dequeue_count)) >=     \
                       (1 << LOG2_ENTRIES), 0))                               \
  {                                                                           \
    result = -1;                                                              \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    unsigned int i = queue->enqueue_count & ((1 << LOG2_ENTRIES) - 1);        \
    queue->array[i] = object;                                                 \
    __insn_mf(); /* Make sure stores are visible. */                          \
    queue->enqueue_count++;                                                   \
                                                                              \
    /* Prefetch more enqueue data in advance. */                              \
    __insn_prefetch((char*) &queue->array[i] + 64);                           \
    result = 0;                                                               \
  }                                                                           \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
    tmc_spin_queued_mutex_unlock(&queue->enqueue_mutex);                      \
                                                                              \
  return result;                                                              \
}                                                                             \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_enqueue_multiple(NAME##_t *queue,                                      \
                        OBJ_TYPE *object,                                     \
                        unsigned int enqueue_num)                             \
{                                                                             \
  int result;                                                                 \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
    tmc_spin_queued_mutex_lock(&queue->enqueue_mutex);                        \
                                                                              \
  if(__builtin_expect((queue->enqueue_count + enqueue_num -                   \
                       arch_atomic_access_once(queue->dequeue_count)) >       \
                      (1 << LOG2_ENTRIES), 0))                                \
  {                                                                           \
    result = -1;                                                              \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    unsigned int enqueue_count = queue->enqueue_count;                        \
                                                                              \
    while (__builtin_expect(enqueue_num-- != 0, 1))                           \
    {                                                                         \
      unsigned int i = enqueue_count & ((1 << LOG2_ENTRIES) - 1);             \
      queue->array[i] = *(object++);                                          \
                                                                              \
      /* Prefetch more enqueue data in advance. */                            \
      __insn_prefetch((char*) &queue->array[i] + 64);                         \
                                                                              \
      enqueue_count++;                                                        \
    }                                                                         \
    __insn_mf(); /* Make sure stores are visible. */                          \
    queue->enqueue_count = enqueue_count;                                     \
                                                                              \
    result = 0;                                                               \
  }                                                                           \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_SENDER))                                     \
    tmc_spin_queued_mutex_unlock(&queue->enqueue_mutex);                      \
                                                                              \
  return result;                                                              \
}                                                                             \
                                                                              \
static __USUALLY_INLINE int                                                   \
NAME##_dequeue(NAME##_t *queue, OBJ_TYPE *output)                             \
{                                                                             \
  int result;                                                                 \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_RECEIVER))                                   \
    tmc_spin_queued_mutex_lock(&queue->dequeue_mutex);                        \
                                                                              \
  if (__builtin_expect(arch_atomic_access_once(queue->enqueue_count) ==       \
                       queue->dequeue_count, 0))                              \
  {                                                                           \
    result = -1;                                                              \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    unsigned int i = queue->dequeue_count & ((1 << LOG2_ENTRIES) - 1);        \
    *output = queue->array[i];                                                \
                                                                              \
    /* Compiler barrier requires preceding store to *output to have issued, and\
     * thus the load from queue->array[] to have completed, prior to updating \
     * dequeue_count. */                                                      \
    arch_atomic_compiler_barrier();                                           \
    queue->dequeue_count++;                                                   \
                                                                              \
    /* Prefetch more dequeue data in advance. */                              \
    __insn_prefetch((char*) &queue->array[i] + 64);                           \
    result = 0;                                                               \
  }                                                                           \
                                                                              \
  if (!(FLAGS & TMC_QUEUE_SINGLE_RECEIVER))                                   \
    tmc_spin_queued_mutex_unlock(&queue->dequeue_mutex);                      \
                                                                              \
  return result;                                                              \
}                                                                             \


#endif // __TMC_QUEUE_H__

//! @}
