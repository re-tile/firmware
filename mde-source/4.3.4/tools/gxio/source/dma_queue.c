/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

#include "gxio/dma_queue.h"
#include <string.h>
#include <arch/spr_def.h>
#include <arch/atomic.h>

/** Wait for a memory read to complete. */
#define wait_for_value(val)                             \
  __asm__ __volatile__("move %0, %0" :: "r"(val))

/** The index is in the low 16. */
#define DMA_QUEUE_INDEX_MASK ((1 << 16) - 1)

/**
 * The hardware descriptor-ring type.
 * This matches the types used by mpipe (MPIPE_EDMA_POST_REGION_VAL_t)
 * and trio (TRIO_PUSH_DMA_REGION_VAL_t or TRIO_PULL_DMA_REGION_VAL_t).
 * See those types for more documentation on the individual fields.
 */
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    uint64_t ring_idx   : 16;
    uint64_t count      : 16;
    uint64_t gen        : 1;
    uint64_t __reserved : 31;
#else
    uint64_t __reserved : 31;
    uint64_t gen        : 1;
    uint64_t count      : 16;
    uint64_t ring_idx   : 16;
#endif
  };
  uint64_t word;
} __gxio_ring_t;


void
__gxio_dma_queue_init(__gxio_dma_queue_t *dma_queue,
                      void *post_region_addr,
                      unsigned int num_entries,
                      unsigned int flags)
{
  /*
   * Limit 65536 entry rings to 65535 credits because we only have a
   * 16 bit completion counter.
   */
  int64_t credits = (num_entries < 65536) ? num_entries : 65535;

  memset(dma_queue, 0, sizeof(*dma_queue));

  dma_queue->post_region_addr = post_region_addr;
  dma_queue->hw_complete_count = 0;
  dma_queue->credits_and_next_index = credits << DMA_QUEUE_CREDIT_SHIFT;
  dma_queue->flags = flags;
}


void __NEVER_INLINE
__gxio_dma_queue_update_credits(__gxio_dma_queue_t *dma_queue)
{
  __gxio_ring_t val;
  uint64_t count;
  uint64_t delta;
  uint64_t new_count;

  /*
   * Read the 64-bit completion count without touching the cache, so
   * we later avoid having to evict any sharers of this cache line
   * when we update it below.
   */
  uint64_t orig_hw_complete_count =
    arch_atomic_val_compare_and_exchange(&dma_queue->hw_complete_count,
                                         -1, -1);

  /* Make sure the load completes before we access the hardware. */
  wait_for_value(orig_hw_complete_count);

  /* Read the 16-bit count of how many packets it has completed. */
  val.word = __gxio_mmio_read(dma_queue->post_region_addr);
  count = val.count;

  /*
   * Calculate the number of completions since we last updated the
   * 64-bit counter.  It's safe to ignore the high bits because the
   * maximum credit value is 65535.
   */
  delta = (count - orig_hw_complete_count) & 0xffff;
  if (delta == 0)
    return;

  /*
   * Try to write back the count, advanced by delta.  If we race with
   * another thread, this might fail, in which case we return
   * immediately on the assumption that some credits are (or at least
   * were) available.
   */
  new_count = orig_hw_complete_count + delta;
  if (arch_atomic_val_compare_and_exchange(&dma_queue->hw_complete_count,
                                           orig_hw_complete_count,
                                           new_count) !=
      orig_hw_complete_count)
    return;

  /*
   *  Check if remote buffer return is enabled.
   *  This will add minimum run-time performance impact due to a quick
   *  check when remote buffer return is not enabled. But this should
   *  be OK since this routine is usually called every 256 packets
   *  instead of per packet.
   */
  if (dma_queue->flags & DMA_QUEUE_EQUEUE_RHWB)
  {
    /*
     * Call the equeue buffer return function and return new credits.
     */
    delta = __gxio_mpipe_remote_buffer_return(dma_queue,
                                              orig_hw_complete_count,
                                              new_count);
    if (!delta)
      return;
  }

  /*
   * We succeeded in advancing the completion count; add back the
   * corresponding number of egress credits.
   */
  __insn_fetchadd(&dma_queue->credits_and_next_index,
                  (delta << DMA_QUEUE_CREDIT_SHIFT));
}


/*
 * A separate 'blocked' method for put() so that backtraces and
 * profiles will clearly indicate that we're wasting time spinning on
 * egress availability rather than actually posting commands.
 */
int64_t __NEVER_INLINE
__gxio_dma_queue_wait_for_credits(__gxio_dma_queue_t *dma_queue,
                                  int64_t modifier)
{
  int backoff = 16;
  int64_t old;

  do
  {
    int i;
    /* Back off to avoid spamming memory networks. */
    for (i = backoff; i > 0; i--)
      __insn_mfspr(SPR_PASS);

    /* Check credits again. */
    __gxio_dma_queue_update_credits(dma_queue);
    old = __insn_fetchaddgez(&dma_queue->credits_and_next_index,
                             modifier);

    /* Calculate bounded exponential backoff for next iteration. */
    if (backoff < 256)
      backoff *= 2;
  } while (old + modifier < 0);

  return old;
}


int64_t
__gxio_dma_queue_reserve_aux(__gxio_dma_queue_t *dma_queue,
                             unsigned int num, int wait)
{
  return __gxio_dma_queue_reserve(dma_queue, num, wait != 0, true);
}


int
__gxio_dma_queue_is_complete(__gxio_dma_queue_t *dma_queue,
                             int64_t completion_slot,
                             int update)
{
  if (update)
  {
    if (arch_atomic_access_once(dma_queue->hw_complete_count) >
        completion_slot)
      return 1;

    __gxio_dma_queue_update_credits(dma_queue);
  }

  return
    arch_atomic_access_once(dma_queue->hw_complete_count) > completion_slot;
}
