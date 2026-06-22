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
 *
 * Implementation of mica queue calls.
 */

#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arch/atomic.h>
#include <arch/cycle.h>
#include <gxio/common.h>
#include <gxio/mica.h>
#include <gxio/mica_queue.h>


// An additional queue_flags constant.
#define INITIALIZED   0x20

#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))
#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))

#define ROUNDUP(val, align)  (((val) + ((align) - 1)) & (~ ((align) - 1)))

#define Assert(cond)

#define NOINLINE __attribute__((noinline))
#define INLINE   __attribute__((always_inline))


#define MICA_SPIN_CURRENT_SHIFT  17
#define MICA_SPIN_NEXT_MASK      0x7FFF
#define MICA_SPIN_NEXT_OVERFLOW  0x8000

// Read the word at "p" without loading the line into the cache.
// This avoids subsequent sharer invalidation when the line is written.
// Choose an unlikely "compare" value to avoid causing invalidations.
#define read_noalloc(p) arch_atomic_val_compare_and_exchange((p), -77, -77)


#define LOCK_QUEUE(queue_flags, queue)                      \
    ({                                                      \
        if ((queue_flags & SHARED_MICA_QUEUE) != 0)         \
            mica_spin_lock(&queue->mutex);                  \
    })

#define UNLOCK_QUEUE(queue_flags, queue)                    \
    ({                                                      \
        if ((queue_flags & SHARED_MICA_QUEUE) != 0)         \
            mica_spin_unlock(&queue->mutex);                \
    })

#define LOCK_RESULT_FIFO(queue_flags, queue, result_fifo_idx)    \
    ({                                                           \
        if ((queue_flags & SHARED_RESULT_FIFO) != 0)             \
            mica_spin_lock(&queue->mutex);                       \
    })

#define UNLOCK_RESULT_FIFO(queue_flags, queue, result_fifo_idx)  \
    ({                                                           \
        if ((queue_flags & SHARED_RESULT_FIFO) != 0)             \
            mica_spin_unlock(&queue->mutex);                     \
    })

extern int gxio_mica_destroy(gxio_mica_context_t *context);


typedef struct // 32 or 48 bytes long
{
    uint64_t  opcode;
    uintptr_t src;
    uintptr_t dst;
    uintptr_t extra_data;
    uintptr_t user_ptr;
    uint32_t  result_fifo_idx;
} gxio_mica_req_t;

typedef struct  // 32 bytes long
{
    gxio_mica_req_t *base_ptr;
    gxio_mica_req_t *head_ptr;
    gxio_mica_req_t *tail_ptr;
    gxio_mica_req_t *max_ptr;
} req_fifo_t;

typedef struct  // 32 bytes long
{
    gxio_mica_result_t *base_ptr;
    gxio_mica_result_t *head_ptr;
    gxio_mica_result_t *tail_ptr;
    gxio_mica_result_t *max_ptr;
} result_fifo_t;

typedef struct
{
    // Low 15 bits are "next"; high 15 bits are "current".
    uint32_t lock;
} mica_spin_lock_t;

struct gxio_mica_queue_s
{
    uint8_t  queue_flags;
    uint8_t  mallocd_mem;
    uint8_t  type;
    uint8_t  pad;
    uint16_t is_alloc_mask;  // Bit set for each mica context acquired
    uint16_t is_busy_mask;   // Bit set for bits in is_alloc_mask that are busy

    mica_spin_lock_t mutex;

    uint32_t queue_size_in_bytes;
    uint32_t req_fifo_entries;
    uint32_t result_fifo_cnt;
    uint32_t result_fifo_entries;
    uint32_t reserved_result_idxs;

    req_fifo_t req_fifo;

    uint64_t last_poll_cycles;
    uint64_t sync_context_status;
    mica_queue_stats_t stats;

    uint8_t            *mica_in_use_ptrs[MAX_CONTEXTS_PER_QUEUE];
    uintptr_t           user_ptrs[MAX_CONTEXTS_PER_QUEUE];
    uint8_t             result_fifo_idxs[MAX_CONTEXTS_PER_QUEUE];
    gxio_mica_context_t contexts[MAX_CONTEXTS_PER_QUEUE];
    result_fifo_t       result_fifos[MAX_RESULT_FIFO_CNT];  // 512 bytes long
//  gxio_mica_req_t     req_fifo_data[];
//  gxio_mica_result_t  result_fifos_data[][];
};



static NOINLINE void mica_queue_delay(uint32_t cycles)
{
    uint32_t loop_cnt, loop;

    loop_cnt = (cycles - 8) / 8;
    for (loop = 1;  loop < loop_cnt;  loop++)
        cycle_relax();
}


// Wait until the high bits (current) match my ticket.
// If we notice the overflow bit set on entry, we clear it.
static void mica_spin_lock_slow(mica_spin_lock_t *mutex,
                                uint32_t          my_ticket)
{
    uint32_t val, delta;

    if (my_ticket & MICA_SPIN_NEXT_OVERFLOW)
    {
        __insn_fetchand4(&mutex->lock, ~MICA_SPIN_NEXT_OVERFLOW);
        my_ticket &= ~MICA_SPIN_NEXT_OVERFLOW;
    }

    while (1)
    {
        val   = read_noalloc(&mutex->lock);
        delta = my_ticket - (val >> MICA_SPIN_CURRENT_SHIFT);
        if (delta == 0)
            return;

        mica_queue_delay(128 * delta);
    }
}

static INLINE void mica_spin_lock(mica_spin_lock_t *mutex)
{
    // Grab the "next" ticket number and bump it atomically.
    // If the current ticket is not ours, go to the slow path.
    // We also take the slow path if the "next" value overflows.
    uint32_t val, ticket;

    val    = __insn_fetchadd4(&mutex->lock, 1);
    ticket = val & (MICA_SPIN_NEXT_MASK | MICA_SPIN_NEXT_OVERFLOW);
    if ((val >> MICA_SPIN_CURRENT_SHIFT) != ticket)
        mica_spin_lock_slow(mutex, ticket);

    arch_atomic_acquire_barrier();
}

static INLINE void mica_spin_unlock(mica_spin_lock_t *mutex)
{
    // Bump the current ticket so the next task owns the lock.
    arch_atomic_release_barrier();
    __insn_fetchadd4(&mutex->lock, 1 << MICA_SPIN_CURRENT_SHIFT);
}

static INLINE void mica_append_req_fifo(gxio_mica_queue_t *queue,
                                        uint32_t           result_fifo_idx,
                                        void              *src,
                                        void              *dst,
                                        void              *extra_data,
                                        gxio_mica_opcode_t opcode,
                                        void              *user_ptr)
{
    gxio_mica_req_t *request;
    req_fifo_t      *req_fifo;

    req_fifo                 = &queue->req_fifo;
    request                  = req_fifo->tail_ptr;
    request->src             = (uintptr_t) src;
    request->dst             = (uintptr_t) dst;
    request->extra_data      = (uintptr_t) extra_data;
    request->opcode          = (uint64_t)  opcode.word;
    request->user_ptr        = (uintptr_t) user_ptr;
    request->result_fifo_idx = result_fifo_idx;

    // Advance request tail.
    queue->stats.cur_req_fifo_cnt++;
    Assert(queue->stats.cur_req_fifo_cnt < queue->req_fifo_entries);
    if (queue->stats.max_req_fifo_cnt < queue->stats.cur_req_fifo_cnt)
        queue->stats.max_req_fifo_cnt = queue->stats.cur_req_fifo_cnt;

    req_fifo->tail_ptr++;
    if (req_fifo->max_ptr <= req_fifo->tail_ptr)
        req_fifo->tail_ptr = req_fifo->base_ptr;
}

static INLINE void mica_append_result_fifo(gxio_mica_queue_t *queue,
                                           uint64_t           user_ptr,
                                           uint64_t           status,
                                           uint32_t           result_fifo_idx)
{
    gxio_mica_result_t *result;
    result_fifo_t      *result_fifo;
    uint32_t            fifo_cnt;

    fifo_cnt = queue->stats.cur_result_fifo_cnt[result_fifo_idx];
    if (queue->result_fifo_entries <= fifo_cnt)
    {
        queue->stats.result_fifo_overflows++;
        return;
    }

    fifo_cnt++;
    result_fifo = &queue->result_fifos[result_fifo_idx];
    result      = result_fifo->tail_ptr;
    queue->stats.cur_result_fifo_cnt[result_fifo_idx] = fifo_cnt;
    result->user_ptr       = user_ptr;
    result->context_status = status;
    if (queue->stats.max_result_fifo_cnt[result_fifo_idx] < fifo_cnt)
        queue->stats.max_result_fifo_cnt[result_fifo_idx] = fifo_cnt;

    result_fifo->tail_ptr++;
    if (result_fifo->max_ptr <= result_fifo->tail_ptr)
        result_fifo->tail_ptr = result_fifo->base_ptr;
}

static INLINE void mica_queue_start_op(gxio_mica_queue_t *queue,
                                       uint32_t           result_fifo_idx,
                                       uint32_t           context_idx,
                                       void              *src,
                                       void              *dst,
                                       void              *extra_data,
                                       gxio_mica_opcode_t opcode,
                                       void              *user_ptr)
{
    gxio_mica_context_t *context;
    uint8_t             *hw_base;

    context = &queue->contexts[context_idx];
    hw_base = (uint8_t *) context->mmio_context_user_base;
    __gxio_mmio_write(hw_base + MICA_SRC_DATA,       (uintptr_t) src);
    __gxio_mmio_write(hw_base + MICA_DEST_DATA,      (uintptr_t) dst);
    __gxio_mmio_write(hw_base + MICA_EXTRA_DATA_PTR, (uintptr_t) extra_data);
    __insn_mf();

    __gxio_mmio_write(hw_base + MICA_OPCODE, (uint64_t) opcode.word);

    Assert((queue->is_busy_mask & (1 << context_idx)) == 0);
    queue->user_ptrs[context_idx]        = (uintptr_t) user_ptr;
    queue->result_fifo_idxs[context_idx] = result_fifo_idx;
    queue->stats.cur_contexts_busy++;
    if (queue->stats.max_contexts_busy < queue->stats.cur_contexts_busy)
        queue->stats.max_contexts_busy = queue->stats.cur_contexts_busy;

    queue->is_busy_mask |= 1 << context_idx;
}

static NOINLINE void mica_queue_start_req_head(gxio_mica_queue_t *queue,
                                               uint32_t           context_idx)
{
    gxio_mica_context_t *context;
    gxio_mica_req_t     *request;
    req_fifo_t          *req_fifo;
    uint8_t             *hw_base;

    // First get the head of the request fifo.
    Assert (queue->stats.cur_req_fifo_cnt != 0);
    req_fifo = &queue->req_fifo;
    context  = &queue->contexts[context_idx];
    request  = req_fifo->head_ptr;

    hw_base = (uint8_t *) context->mmio_context_user_base;
    __gxio_mmio_write(hw_base + MICA_SRC_DATA,       request->src);
    __gxio_mmio_write(hw_base + MICA_DEST_DATA,      request->dst);
    __gxio_mmio_write(hw_base + MICA_EXTRA_DATA_PTR, request->extra_data);
    __insn_mf();

    __gxio_mmio_write(hw_base + MICA_OPCODE, request->opcode);

    Assert((queue->is_busy_mask & (1 << context_idx)) == 0);
    queue->user_ptrs[context_idx]        = request->user_ptr;
    queue->result_fifo_idxs[context_idx] = request->result_fifo_idx;
    queue->stats.cur_contexts_busy++;
    if (queue->stats.max_contexts_busy < queue->stats.cur_contexts_busy)
        queue->stats.max_contexts_busy = queue->stats.cur_contexts_busy;

    queue->is_busy_mask |= 1 << context_idx;

    // Now remove the head of the request fifo (i.e. advance the fifo).
    queue->stats.cur_req_fifo_cnt--;
    req_fifo->head_ptr++;
    if (req_fifo->max_ptr <= req_fifo->head_ptr)
        req_fifo->head_ptr = req_fifo->base_ptr;
}

uint32_t mica_queue_ctxts_complete(uint8_t *mica_in_use_ptrs[],
                                   uint32_t is_busy_mask)
{
    uint8_t      *in_use_ptr;
    uint32_t      busy_mask, population_cnt, idx, context_idx, done_mask;
    MICA_IN_USE_t inuse_array[MAX_CONTEXTS_PER_QUEUE];

    population_cnt = __insn_pcnt(is_busy_mask);
    busy_mask      = is_busy_mask;
    for (idx = 0;  idx < population_cnt;  idx++)
    {
        context_idx = __insn_ctz(busy_mask);
        in_use_ptr  = mica_in_use_ptrs[context_idx];
        busy_mask  ^= 1 << context_idx;
        inuse_array[context_idx].word = __gxio_mmio_read(in_use_ptr);
    }

    Assert(busy_mask == 0);
    busy_mask = is_busy_mask;
    done_mask = 0;
    for (idx = 0;  idx < population_cnt;  idx++)
    {
        context_idx = __insn_ctz(busy_mask);
        if (inuse_array[context_idx].in_use)
            done_mask |= 1 << context_idx;

        busy_mask  ^= 1 << context_idx;
    }

    return done_mask;
}

static INLINE uint8_t mica_queue_poll_ctxt(gxio_mica_queue_t *queue,
                                           uint32_t           context_idx,
                                           uint8_t            add_result_fifo)
{
    gxio_mica_context_t *context;
    MICA_IN_USE_t        inuse;
    uint64_t             user_ptr, context_status;
    uint32_t             result_fifo_idx;
    uint8_t             *hw_base;

    context    = &queue->contexts[context_idx];
    hw_base    = (uint8_t *) context->mmio_context_user_base;
    inuse.word = __gxio_mmio_read(hw_base + MICA_IN_USE);
    if (inuse.in_use)
        return 0;

    context_status = __gxio_mmio_read(hw_base + MICA_CONTEXT_STATUS);
    if (add_result_fifo)
    {
        // Append to the result_fifo.
        user_ptr        = queue->user_ptrs[context_idx];
        result_fifo_idx = queue->result_fifo_idxs[context_idx];
        mica_append_result_fifo(queue, user_ptr, context_status,
                                result_fifo_idx);
    }
    else
        queue->sync_context_status = context_status;

    queue->stats.total_dones++;
    Assert((queue->is_busy_mask & (1 << context_idx)) != 0);
    queue->is_busy_mask ^= 1 << context_idx;
    Assert((queue->is_busy_mask & (1 << context_idx)) == 0);
    queue->stats.cur_contexts_busy--;
    return 1;
}

static INLINE uint32_t mica_queue_get_empty_ctxt(gxio_mica_queue_t *queue)
{
    uint32_t is_avail_mask, context_idx;

    is_avail_mask = queue->is_alloc_mask - queue->is_busy_mask;
    context_idx = __insn_ctz(is_avail_mask);
    Assert((queue->is_alloc_mask & (1 << context_idx)) != 0);
    Assert((queue->is_busy_mask  & (1 << context_idx)) == 0);
    return context_idx;
}

static NOINLINE int mica_queue_wait_empty_ctxt(gxio_mica_queue_t *queue,
                                               uint64_t           max_cycles,
                                               uint32_t          *ctxt_idx_ptr)
{
    uint64_t start_cycles, elapsed_cycles;
    uint32_t num_contexts, context_idx;

    num_contexts   = queue->stats.num_contexts;
    start_cycles   = get_cycle_count();
    elapsed_cycles = 0;
    while ((elapsed_cycles < max_cycles) || (max_cycles == 0))
    {
        for (context_idx = 0;  context_idx < num_contexts;  context_idx++)
        {
            if (mica_queue_poll_ctxt(queue, context_idx, 1) != 0)
            {
                *ctxt_idx_ptr = context_idx;
                return 0;
            }
        }

        mica_queue_delay(200);
        elapsed_cycles = get_cycle_count() - start_cycles;
    }

    return -1;
}

static NOINLINE int mica_queue_wait_on_ctxt(gxio_mica_queue_t *queue,
                                            uint32_t           special_ctxt_idx,
                                            uint64_t           max_cycles)
{
    uint64_t start_cycles, elapsed_cycles;
    uint32_t busy_mask, pop_cnt, idx, context_idx;
    uint8_t  add_to_result_fifo, found;

    start_cycles   = get_cycle_count();
    elapsed_cycles = 0;
    while ((elapsed_cycles < max_cycles) || (max_cycles == 0))
    {
        busy_mask = queue->is_busy_mask;
        pop_cnt   = __insn_pcnt(busy_mask);
        for (idx = 0;  idx < pop_cnt;  idx++)
        {
            context_idx = __insn_ctz(busy_mask);
            Assert((busy_mask & (1 << context_idx)) != 0);
            busy_mask ^= 1 << context_idx;
            Assert((busy_mask & (1 << context_idx)) == 0);

            add_to_result_fifo = context_idx != special_ctxt_idx;
            found              = mica_queue_poll_ctxt(queue, context_idx,
                                                      add_to_result_fifo);
            if (found)
            {
                if (queue->stats.cur_req_fifo_cnt != 0)
                    // Remove the head of the request fifo and copy its
                    // parameters into the just processed context.
                    mica_queue_start_req_head(queue, context_idx);

                if (context_idx == special_ctxt_idx)
                    return 0;
            }
        }

        mica_queue_delay(200);
        elapsed_cycles = get_cycle_count() - start_cycles;
    }

    return -1;
}

static NOINLINE uint32_t internal_queue_poll(gxio_mica_queue_t *queue,
                                             uint32_t           result_fifo_idx)
{
    uint32_t busy_mask, population_cnt, idx, context_idx;
    uint8_t  found;

    busy_mask      = queue->is_busy_mask;
    population_cnt = __insn_pcnt(busy_mask);

    for (idx = 0;  idx < population_cnt;  idx++)
    {
        Assert(busy_mask != 0);
        context_idx = __insn_ctz(busy_mask);
        Assert((busy_mask & (1 << context_idx)) != 0);
        busy_mask ^= 1 << context_idx;

        found = mica_queue_poll_ctxt(queue, context_idx, 1);
        if (found & (queue->stats.cur_req_fifo_cnt != 0))
            // Remove the head of the request fifo and copy its parameters
            // into the just processed context.
            mica_queue_start_req_head(queue, context_idx);
    }

    return queue->stats.cur_result_fifo_cnt[result_fifo_idx];
}

static NOINLINE int mica_queue_destroy_contexts(gxio_mica_queue_t *queue,
                                                uint32_t           num_contexts)
{
    gxio_mica_context_t *context;
    uint32_t             context_idx;
    int                  rc, result;

    // Loop thru and call gxio_mica_destroy on all of the contexts
    // that we successfully acquired.
    result = 0;
    for (context_idx = 0;  context_idx < num_contexts;  context_idx++)
    {
        context = &queue->contexts[context_idx];
        rc      = gxio_mica_destroy(context);
        if ((rc < 0) && (result == 0))
            result = rc;  // Return the first error result code.
    }

    return result;
}

uint32_t gxio_mica_queue_count(uint32_t num_of_threads,
                               uint32_t num_mica_contexts)
{
    uint32_t num_mica_queues, ctxts_per_queue;

    num_mica_queues = num_of_threads;
    ctxts_per_queue = num_mica_contexts / num_mica_queues;

    if (ctxts_per_queue < 2)
    {
        num_mica_queues = (num_of_threads + 1) / 2;
        ctxts_per_queue = num_mica_contexts / num_mica_queues;

        if (ctxts_per_queue < 3)
        {
            num_mica_queues = (num_of_threads + 3) / 4;
            ctxts_per_queue = num_mica_contexts / num_mica_queues;

            if (ctxts_per_queue < 4)
            {
                num_mica_queues = (num_of_threads + 5) / 6;
                ctxts_per_queue = num_mica_contexts / num_mica_queues;

                if (ctxts_per_queue < 5)
                    num_mica_queues = (num_of_threads + 7) / 8;
            }
        }
    }

    return num_mica_queues;
}

// Note that this implementations assumes 2 MiCA shims per chip.  It will
// have to be modified if and when that changes.

uint32_t gxio_mica_queue_split_ctxt_request(mica_ctxt_req_t *total_ctxt_request,
                                            uint32_t         num_of_queues,
                                            mica_ctxt_req_t  ctxt_requests[])
{
    uint32_t shim0_ctxts, shim1_ctxts, queueIdx, min_ctxts_per_q;
    uint32_t min_shim0_ctxts_per_q, min_shim1_ctxts_per_q;
    uint32_t extra_shim0_ctxts, extra_shim1_ctxts, extra_ctxts;
    uint32_t shim0_req, shim1_req, ctxts_used;

    shim0_ctxts = total_ctxt_request->num_contexts[0];
    shim1_ctxts = total_ctxt_request->num_contexts[1];

    min_shim0_ctxts_per_q = shim0_ctxts / num_of_queues;
    min_shim1_ctxts_per_q = shim1_ctxts / num_of_queues;
    min_ctxts_per_q       = min_shim0_ctxts_per_q + min_shim1_ctxts_per_q;

    while ((MAX_CONTEXTS_PER_QUEUE - 2) < min_ctxts_per_q)
    {
        // Decrement the larger of shim0_ctxts or shim1_ctxts and try again.
        if (shim0_ctxts < shim1_ctxts)
            shim1_ctxts--;
        else
            shim0_ctxts--;

        min_shim0_ctxts_per_q = shim0_ctxts / num_of_queues;
        min_shim1_ctxts_per_q = shim1_ctxts / num_of_queues;
        min_ctxts_per_q       = min_shim0_ctxts_per_q + min_shim1_ctxts_per_q;
    }

    extra_shim0_ctxts = shim0_ctxts - (min_shim0_ctxts_per_q * num_of_queues);
    extra_shim1_ctxts = shim1_ctxts - (min_shim1_ctxts_per_q * num_of_queues);

    if ((MAX_CONTEXTS_PER_QUEUE - 2) <= min_ctxts_per_q)
    {
        extra_shim0_ctxts = 0;
        extra_shim1_ctxts = 0;
    }

    // Determine the distribution of the extra_shim ctxts.  If number of
    // extra_shim ctxts <= num_of_queues, then just give the first N queues
    // one extra ctxt from alternating sets.  If num_of_queues <
    // extra_shim ctxts then give the first few queues a few extra.
    extra_ctxts = extra_shim0_ctxts + extra_shim1_ctxts;

    // *TBD* today only hanle case where extra_shim_ctxts <= num_of_queues.
    ctxts_used = 0;
    for (queueIdx = 0;  queueIdx < num_of_queues;  queueIdx++)
    {
        shim0_req = min_shim0_ctxts_per_q;
        shim1_req = min_shim1_ctxts_per_q;
        if ((extra_shim0_ctxts + extra_shim1_ctxts) != 0)
        {
            if (extra_shim0_ctxts < extra_shim1_ctxts)
            {
                shim1_req++;
                extra_shim1_ctxts--;
            }
            else
            {
                shim0_req++;
                extra_shim0_ctxts--;
            }
        }

        ctxt_requests[queueIdx].num_contexts[0] = shim0_req;
        ctxt_requests[queueIdx].num_contexts[1] = shim1_req;
        ctxts_used += shim0_req + shim1_req;
    }

    return ctxts_used;
}

uint32_t gxio_mica_queue_mem_size(gxio_mica_kind_t type,
                                  uint32_t         req_fifo_entries,
                                  uint32_t         result_fifo_cnt,
                                  uint32_t         result_fifo_entries,
                                  mica_ctxt_req_t *ctxt_request,
                                  uint32_t         queue_flags)
{
    uint32_t num_contexts, shim_idx, base_size, req_fifo_size, result_fifo_size;

    num_contexts = 0;
    for (shim_idx = 0;  shim_idx <= MAX_MICA_SHIM_IDX;  shim_idx++)
        num_contexts += ctxt_request->num_contexts[shim_idx];

    base_size        = ROUNDUP(sizeof(struct gxio_mica_queue_s), 64);
    req_fifo_size    = ROUNDUP(req_fifo_entries * sizeof(gxio_mica_req_t), 64);
    result_fifo_size = ROUNDUP(result_fifo_entries * sizeof(gxio_mica_result_t),
                               64);
    return base_size + req_fifo_size + (result_fifo_cnt * result_fifo_size);
}

static uint32_t pick_random_shim_idx(uint8_t ctxts_reqs[MAX_MICA_SHIM_IDX])
{
    uint32_t randomNum, shim_idx;

    randomNum = rand() & 0x3F;
    for (shim_idx = 0;  shim_idx <= MAX_MICA_SHIM_IDX;  shim_idx++)
    {
        if (ctxts_reqs[shim_idx] != 0)
        {
            randomNum--;
            if (randomNum == 0)
                return shim_idx;
        }
    }

    return (rand() & 0xFF) % (MAX_MICA_SHIM_IDX + 1);
}

static uint32_t pick_next_shim(uint32_t last_shim_idx,
                               uint8_t  ctxts_reqs[MAX_MICA_SHIM_IDX])
{
    uint32_t largest_shim_reqs, largest_shim_idx, start_idx, shim_idx;

    // Find the shim_idx with the largest ctxts_requested, but breaking ties
    // by finding the first such shim_idx, in a round robin fashion, starting
    // after the last_shim_idx picked.
    largest_shim_reqs = 0;
    largest_shim_idx  = 0;
    start_idx         = last_shim_idx + 1;
    if (MAX_MICA_SHIM_IDX < start_idx)
        start_idx = 0;

    for (shim_idx = start_idx;  shim_idx <= MAX_MICA_SHIM_IDX;  shim_idx++)
    {
        if (largest_shim_reqs < ctxts_reqs[shim_idx])
        {
            largest_shim_reqs = ctxts_reqs[shim_idx];
            largest_shim_idx  = shim_idx;
        }
    }

    for (shim_idx = 0;  shim_idx < start_idx;  shim_idx++)
    {
        if (largest_shim_reqs < ctxts_reqs[shim_idx])
        {
            largest_shim_reqs = ctxts_reqs[shim_idx];
            largest_shim_idx  = shim_idx;
        }
    }

    Assert(largest_shim_reqs != 0);
    return largest_shim_idx;
}

static int init_mica_ctxts(gxio_mica_queue_t *queue,
                           gxio_mica_kind_t   type,
                           uint32_t           ctxts_to_assign,
                           uint8_t            ctxts_reqs[MAX_MICA_SHIM_IDX],
                           uint32_t          *ctxts_initd_ptr)
{
    gxio_mica_context_t *context;
    uint32_t             context_idx, shim_idx, ctxts_initd;
    int                  rc;

    ctxts_initd      = 0;
    *ctxts_initd_ptr = ctxts_initd;
    context_idx      = 0;
    shim_idx         = pick_random_shim_idx(ctxts_reqs);

    while (ctxts_to_assign != 0)
    {
        // Pick next shim_idx starting after the last one assigned, with the
        // largest count.
        shim_idx = pick_next_shim(shim_idx, ctxts_reqs);
        context  = &queue->contexts[context_idx];
        rc       = gxio_mica_init(context, type, shim_idx);
        if (rc < 0)
            return rc;

        ctxts_reqs[shim_idx]--;
        ctxts_to_assign--;
        context_idx++;
        ctxts_initd++;
        *ctxts_initd_ptr = ctxts_initd;
    }

    return 0;
}

gxio_mica_queue_t *gxio_mica_create_queue(void            *mem_ptr,
                                          uint32_t         mem_size,
                                          gxio_mica_kind_t type,
                                          uint32_t         req_fifo_entries,
                                          uint32_t         result_fifo_cnt,
                                          uint32_t         result_fifo_entries,
                                          mica_ctxt_req_t *ctxt_request,
                                          uint32_t         queue_flags)
{
    gxio_mica_context_t *context;
    gxio_mica_queue_t   *queue;
    result_fifo_t       *result_fifo;
    req_fifo_t          *req_fifo;
    uintptr_t            req_base_addr, result_fifo_base_addr;
    uint32_t             num_contexts, shim_idx, base_size, needed_mem;
    uint32_t             req_fifo_size, result_fifo_size, context_idx, ctxts;
    uint32_t             num_ctxts_initd, result_idx, req_fifo_base_offset;
    uint8_t             *hw_base, ctxts_requested[MAX_MICA_SHIM_IDX];
    int                  rc;

    // Count the total number of contexts requested.
    num_contexts = 0;
    for (shim_idx = 0;  shim_idx <= MAX_MICA_SHIM_IDX;  shim_idx++)
    {
        ctxts                     = ctxt_request->num_contexts[shim_idx];
        num_contexts             += ctxts;
        ctxts_requested[shim_idx] = ctxts;
    }

    if ((MAX_CONTEXTS_PER_QUEUE < num_contexts) ||
        (MAX_RESULT_FIFO_CNT    < result_fifo_cnt))
        return NULL;

    base_size        = ROUNDUP(sizeof(struct gxio_mica_queue_s), 64);
    req_fifo_size    = ROUNDUP(req_fifo_entries * sizeof(gxio_mica_req_t), 64);
    result_fifo_size = ROUNDUP(result_fifo_entries * sizeof(gxio_mica_result_t),
                               64);
    needed_mem       = base_size + req_fifo_size +
                           (result_fifo_cnt * result_fifo_size);

    // First either use the memory passed in or malloc a suitable size memory
    // block.
    if ((mem_ptr != NULL) && (mem_size < needed_mem))
        return NULL;

    if (mem_ptr == NULL)
    {
        mem_ptr = calloc(1, needed_mem);
        if (mem_ptr == NULL)
            return NULL;

        queue              = (gxio_mica_queue_t *) mem_ptr;
        queue->mallocd_mem = 1;
    }
    else
        memset(mem_ptr, 0, mem_size);

    // Now try to acquire the mica_contexts.  Make sure that we evenly
    // distribute the HW contexts amongst the shims AND pick the first shim
    // starting at the largest # or if equal, at random.
    queue = (gxio_mica_queue_t *) mem_ptr;
    rc    = init_mica_ctxts(queue, type, num_contexts, ctxts_requested,
                            &num_ctxts_initd);
    if (rc < 0)
    {
        // Loop thru and call gxio_mica_destroy on all of the contexts
        // that we previously acquired (successfully).
        printf("gxio_mica_create_queue failed with rc=%d after "
               "initing %u ctxts out of %u\n", rc, num_ctxts_initd,
               num_contexts);
        mica_queue_destroy_contexts(queue, num_ctxts_initd);
        if (queue->mallocd_mem)
            free(queue);

        return NULL;
    }

    queue->queue_size_in_bytes = needed_mem;
    req_fifo_base_offset       = base_size;
    req_base_addr              = ((uintptr_t) queue) + req_fifo_base_offset;
    queue->req_fifo_entries    = req_fifo_entries;
    req_fifo                   = &queue->req_fifo;
    req_fifo->base_ptr         = (gxio_mica_req_t *) req_base_addr;
    req_fifo->head_ptr         = req_fifo->base_ptr;
    req_fifo->tail_ptr         = req_fifo->base_ptr;
    req_fifo->max_ptr          = &req_fifo->base_ptr[req_fifo_entries];

    result_fifo_base_addr      = ((uintptr_t) queue) + base_size + req_fifo_size;
    queue->result_fifo_entries = result_fifo_entries;
    queue->result_fifo_cnt     = result_fifo_cnt;

    for (result_idx = 0;  result_idx < result_fifo_cnt; result_idx++)
    {
        result_fifo            = &queue->result_fifos[result_idx];
        result_fifo->base_ptr  = (gxio_mica_result_t *) result_fifo_base_addr;
        result_fifo->head_ptr  = result_fifo->base_ptr;
        result_fifo->tail_ptr  = result_fifo->base_ptr;
        result_fifo->max_ptr   = &result_fifo->base_ptr[result_fifo_entries];
        result_fifo_base_addr += result_fifo_size;
    }

    for (context_idx = 0;  context_idx < num_contexts;  context_idx++)
    {
        context    = &queue->contexts[context_idx];
        hw_base    = (uint8_t *) context->mmio_context_user_base;
        queue->mica_in_use_ptrs[context_idx] = hw_base + MICA_IN_USE;
    }

    if ((queue_flags & SHARED_RESULT_FIFO) != 0)
        queue_flags |= SHARED_MICA_QUEUE;

    queue->is_alloc_mask      = (1 << num_contexts) - 1;
    queue->is_busy_mask       = 0;
    queue->stats.num_contexts = num_contexts;
    queue->type               = type;
    queue->queue_flags        = queue_flags | INITIALIZED;
    queue->mutex.lock         = 0;
    return queue;
}

int32_t gxio_reserve_result_fifo(gxio_mica_queue_t *queue)
{
    uint32_t result_idx, found, result_fifo_cnt;

    // Always use lock for this function, even if SHARED_MICA_QUEUE isn't set.
    mica_spin_lock(&queue->mutex);

    found           = 0;
    result_fifo_cnt = queue->result_fifo_cnt;
    for (result_idx = 0;  result_idx < result_fifo_cnt;  result_idx++)
    {
        if (((queue->reserved_result_idxs >> result_idx) & 1) == 0)
        {
            queue->reserved_result_idxs |= 1 << result_idx;
            found = 1;
            break;
        }
    }

    mica_spin_unlock(&queue->mutex);
    return found ? result_idx : -1;
}

int gxio_mica_destroy_queue(gxio_mica_queue_t *queue)
{
    uint8_t mallocd_mem, queue_flags = queue->queue_flags;
    int     rc, result;

    if ((queue_flags & INITIALIZED) == 0)
        return UNINITIALIZED_QUEUE_ERR;

    // First free/destroy all of the acquired contexts.
    LOCK_QUEUE(queue_flags, queue);
    result = 0;
    rc     = mica_queue_destroy_contexts(queue, queue->stats.num_contexts);
    if ((rc < 0) && (result == 0))
        result = rc;

    mallocd_mem = queue->mallocd_mem;
    UNLOCK_QUEUE(queue_flags, queue);

    if (mallocd_mem)
        free(queue);

    return result;
}

int gxio_mica_queue_register_page(gxio_mica_queue_t *queue,
                                  void              *page,
                                  size_t             page_size,
                                  uint32_t           page_flags)
{
    gxio_mica_context_t *context;
    uint32_t             num_contexts, context_idx;
    uint8_t              queue_flags = queue->queue_flags;
    int                  rc;

    if ((queue_flags & INITIALIZED) == 0)
        return UNINITIALIZED_QUEUE_ERR;

    num_contexts = queue->stats.num_contexts;
    for (context_idx = 0;  context_idx < num_contexts;  context_idx++)
    {
        context = &queue->contexts[context_idx];
        rc      = gxio_mica_register_page(context, page, page_size, page_flags);
        if (rc < 0)
            return rc;
    }

    return 0;
}

int gxio_mica_queue_op(gxio_mica_queue_t *queue,
                       uint32_t           result_fifo_idx,
                       void              *src,
                       void              *dst,
                       void              *extra_data,
                       gxio_mica_opcode_t opcode,
                       void              *user_ptr)
{
    uint64_t current_cycles, delta, last_poll_cycles = queue->last_poll_cycles;
    uint32_t context_idx;
    uint8_t  queue_flags = queue->queue_flags;

    LOCK_QUEUE(queue_flags, queue);
    current_cycles = get_cycle_count();
    Assert((queue_flags & INITIALIZED) != 0);
    queue->stats.total_requests++;
    delta = current_cycles - last_poll_cycles;
    if (((queue->stats.cur_contexts_busy == queue->stats.num_contexts) &&
         (2000 < delta)) ||
        ((queue->stats.cur_contexts_busy != 0) && (8000 < delta)))
        internal_queue_poll(queue, result_fifo_idx);

    if (queue->stats.cur_contexts_busy < queue->stats.num_contexts)
    {
        // This is the case where we bypass the request fifo and find a non-busy
        // MiCA HW context to load with these parameters.
        Assert(queue->stats.cur_req_fifo_cnt == 0);
        context_idx = mica_queue_get_empty_ctxt(queue);

        // Submit the request.
        mica_queue_start_op(queue, result_fifo_idx, context_idx, src, dst,
                            extra_data, opcode, user_ptr);
        UNLOCK_QUEUE(queue_flags, queue);
        return 0;
    }
    else if (queue->stats.cur_req_fifo_cnt < queue->req_fifo_entries)
    {
        queue->stats.total_req_fifo_enqueues++;
        mica_append_req_fifo(queue, result_fifo_idx, src, dst, extra_data,
                             opcode, user_ptr);
        UNLOCK_QUEUE(queue_flags, queue);
        return 0;
    }
    else
    {
        queue->stats.req_fifo_overflows++;
        UNLOCK_QUEUE(queue_flags, queue);
        return REQUEST_QUEUE_FULL;
    }
}

int gxio_mica_queue_sync_op(gxio_mica_queue_t *queue,
                            uint32_t           result_fifo_idx,
                            void              *src,
                            void              *dst,
                            void              *extra_data,
                            gxio_mica_opcode_t opcode,
                            uint64_t           max_cycles,
                            uint64_t          *context_status)
{
    uint64_t start_cycles, elapsed_cycles;
    uint32_t num_contexts, context_idx;
    uint8_t  queue_flags = queue->queue_flags;
    int      rc;

    start_cycles = get_cycle_count();
    if ((queue_flags & INITIALIZED) == 0)
        return UNINITIALIZED_QUEUE_ERR;

    // First find a non-busy context.
    LOCK_QUEUE(queue_flags, queue);
    num_contexts = queue->stats.num_contexts;
    queue->stats.total_requests++;
    queue->stats.total_sync_ops++;
    if (queue->stats.cur_contexts_busy < num_contexts)
        context_idx = mica_queue_get_empty_ctxt(queue);
    else
    {
        rc = mica_queue_wait_empty_ctxt(queue, max_cycles, &context_idx);
        if (rc < 0)
        {
            UNLOCK_QUEUE(queue_flags, queue);
            return QUEUE_OP_TIMEOUT_ERR;
        }
    }

    // Now start the op.
    mica_queue_start_op(queue, result_fifo_idx, context_idx, src, dst,
                        extra_data, opcode, NULL);

    elapsed_cycles = get_cycle_count() - start_cycles;
    if (max_cycles != 0)
    {
        if (elapsed_cycles < max_cycles)
            max_cycles -= elapsed_cycles;
        else
            max_cycles = 1;
    }

    // Now spin (doing equivalent of gxio_mica_queue_poll) waiting for that
    // specific context to be done.
    rc = mica_queue_wait_on_ctxt(queue, context_idx, max_cycles);
    if ((0 <= rc) && (context_status != NULL))
        *context_status = queue->sync_context_status;

    UNLOCK_QUEUE(queue_flags, queue);
    return (rc < 0) ? QUEUE_OP_TIMEOUT_ERR : 0;
}

uint32_t gxio_mica_queue_poll(gxio_mica_queue_t *queue,
                              uint32_t           result_fifo_idx)
{
    uint64_t current_cycles, last_poll_cycles = queue->last_poll_cycles;
    uint32_t fifo_cnt;
    uint8_t  queue_flags = queue->queue_flags;

    LOCK_QUEUE(queue_flags, queue);
    current_cycles = get_cycle_count();
    if ((queue->is_busy_mask == 0) ||
        ((current_cycles - last_poll_cycles) < 4000))
        fifo_cnt = queue->stats.cur_result_fifo_cnt[result_fifo_idx];
    else
    {
        fifo_cnt = internal_queue_poll(queue, result_fifo_idx);
        queue->last_poll_cycles = current_cycles;
    }

    UNLOCK_QUEUE(queue_flags, queue);
    return fifo_cnt;
}

int gxio_mica_get_result(gxio_mica_queue_t  *queue,
                         uint32_t            result_fifo_idx,
                         gxio_mica_result_t *result_ptr)
{
    result_fifo_t *result_fifo;
    uint8_t        queue_flags = queue->queue_flags;

    if ((queue_flags & INITIALIZED) == 0)
        return UNINITIALIZED_QUEUE_ERR;

    LOCK_QUEUE(queue_flags, queue);
    if (queue->stats.cur_result_fifo_cnt[result_fifo_idx] == 0)
    {
        UNLOCK_QUEUE(queue_flags, queue);
        return 0;
    }

    result_fifo = &queue->result_fifos[result_fifo_idx];
    *result_ptr = *result_fifo->head_ptr;
    queue->stats.cur_result_fifo_cnt[result_fifo_idx]--;
    result_fifo->head_ptr++;
    if (result_fifo->max_ptr <= result_fifo->head_ptr)
        result_fifo->head_ptr = result_fifo->base_ptr;

    UNLOCK_QUEUE(queue_flags, queue);
    return 1;
}

void gxio_mica_queue_stats(gxio_mica_queue_t  *queue,
                           uint8_t             clear_counters,
                           mica_queue_stats_t *stats)
{
    uint8_t queue_flags = queue->queue_flags;

    LOCK_QUEUE(queue_flags, queue);
    Assert((queue_flags & INITIALIZED) != 0);
    if (stats != NULL)
        *stats = queue->stats;

    if (clear_counters != 0)
    {
        queue->stats.total_requests          = 0;
        queue->stats.total_sync_ops          = 0;
        queue->stats.total_req_fifo_enqueues = 0;
        queue->stats.total_dones             = 0;
        queue->stats.req_fifo_overflows      = 0;
        queue->stats.result_fifo_overflows   = 0;
        queue->stats.max_contexts_busy       = 0;
        queue->stats.max_req_fifo_cnt        = 0;
        memset(&queue->stats.max_result_fifo_cnt[0], 0,
               sizeof(queue->stats.max_result_fifo_cnt));
    }

    UNLOCK_QUEUE(queue_flags, queue);
}
