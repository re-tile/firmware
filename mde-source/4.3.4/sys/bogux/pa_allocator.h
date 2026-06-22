/**
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
 *
 * Physical memory allocator interface.
 * @file
 */

#ifndef PA_ALLOCATOR_H
#define PA_ALLOCATOR_H

#include <hv/hypervisor.h>
#include <hv/pagesize.h>

#include <arch/chip.h>

#if HV_LOG2_PAGE_SIZE_SMALL < 15
/** Number of possible page colors.
 * We have 15 bits altogether controlling which cache line we end up
 * in (and what offset in that line).  So if we shift out the page
 * size, we're left with a few bits that we should make sure are the
 * same between VA and PA so that contiguous VAs don't alias in the cache.
 */
#define PA_COLORS (32768 / HV_PAGE_SIZE_SMALL)
#else
/** Degenerate value for number of possible page colors.
 * For pages this big, there is no need for coloring.
 */
#define PA_COLORS 1
#endif

#ifndef __ASSEMBLER__

#include <stdint.h>

#ifndef __cplusplus
#define PA_TYPE(t) void
#else
#define PA_TYPE(t) t
extern "C" {
#endif

typedef uint64_t PA_Addr;  /**< A physical address */

/* Jump through some hoops to keep PA_Allocator a 64-byte multiple */
#define PA_FIELDCOUNT 4  /**< the base, size, and large_heap_* fields */
#define PA_NUM_FREELISTS \
   ((((PA_COLORS + 2) + PA_FIELDCOUNT + 15) & ~15) - PA_FIELDCOUNT)

/** Basic type to hold the state for an instance of the allocator */
typedef struct PA_Allocator {

  /** Pointer to lock for this heap, or NULL for no locking.
   * If the word pointed to is non-zero, the heap is locked. */
  int* lock;

  /** Pad out to the next cache line so we can still read the
   * address of the lock from the first cache line without
   * disturbing the --grind-coherence checker.
   */

  char pad[CHIP_L2_LINE_SIZE() - sizeof (int*)];


  /** Unused base of memory (in page units) */
  uint32_t base;

  /** Remaining unused size (in page units). */
  uint32_t size;

  /** Base of the large page portion of the heap (in page units). */
  uint32_t large_heap_base;

  /** Size of the large page portion of the heap (in page units).
   * If non-zero, it includes the whole heap from large_heap_base to
   * the end, and "size" is reset to go just to the start of the
   * large-page area.
   */
  uint32_t large_heap_size;

  /** Per-color freelists, plus some extras for common page-group sizes.
   * This is worst-case space, since we need to pad to a cache line.
   */
  uint32_t freelist[PA_NUM_FREELISTS];









} PA_Allocator;

/** Failure value returned by allocators to indicate out of memory. */
#define PA_FAILED ((PA_Addr)(-1))

/** Initialize an instance of the allocator.
 * @param p An uninitialized chunk of memory of size sizeof(PA_Allocator).
 * @param base The start address of the heap to use for the allocator.
 * @param size The amount of physical memory for the allocator to use.
 * @return An initialized PA_Allocator object, not using locks.
 */
PA_Allocator* pa_init(PA_Allocator* p, PA_Addr base, uint64_t size);

/** Set lock pointer for allocator.
 * @param lock A pointer to a word used for locking.
 */
void pa_set_lock(PA_Allocator* p, int* lock);

/** Allocate an uninitialized HV_SMALL_PAGE_SIZE chunk of memory.
 * @param p Pointer to an initialized PA_Allocator.
 * @param va The virtual address that gives the color to use for the
 * returned physical address.
 * @return The address of the uninitialized allocated physical memory,
 * or PA_FAILED on failure.
 */
PA_Addr pa_alloc_page(PA_Allocator* p, const void* va);

/** Allocate an uninitialized HV_LARGE_PAGE_SIZE chunk of memory.
 * @param p Pointer to an initialized PA_Allocator.
 * @return The address of the uninitialized allocated physical memory,
 * or PA_FAILED on failure.
 */
PA_Addr pa_alloc_large_page(PA_Allocator* p);

/** Allocate a physically contiguous, size-aligned chunk of memory.
 * @param p Pointer to an initialized PA_Allocator.
 * @param va The virtual address that gives the color to use for the
 * returned physical address.
 * @param bytes Amount of memory to allocate.
 * @return The address of the uninitialized allocated physical memory,
 * or PA_FAILED on failure.
 */
PA_Addr pa_alloc_pages(PA_Allocator* p, const void* va, unsigned int bytes);

/** Free a HV_SMALL_PAGE_SIZE chunk of memory.
 * The page must have no dirty cache lines referencing it in any tile
 * before it is returned to the free list.
 * @param p Pointer to an initialized PA_Allocator.
 * @param pa Pointer to the memory to be freed.
 */
void pa_free_page(PA_Allocator* p, PA_Addr pa);

/** Free a HV_LARGE_PAGE_SIZE chunk of memory.
 * The page must have no dirty cache lines referencing it in any tile
 * before it is returned to the free list.
 * @param p Pointer to an initialized PA_Allocator.
 * @param pa Pointer to the memory to be freed.
 */
void pa_free_large_page(PA_Allocator* p, PA_Addr pa);

/** Free multiple small pages.
 * The pages must have no dirty cache lines referencing them in any tile
 * before they are returned to the allocator.
 * They are returned to the free pool as multiple small pages and will
 * not be available for re-allocation via pa_alloc_pages() again.
 * @param p Pointer to an initialized PA_Allocator.
 * @param pa Pointer to the memory to be freed.
 * @param bytes Number of bytes of memory to free.
 */
void pa_free_pages(PA_Allocator* p, PA_Addr pa, unsigned int bytes);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* !__ASSEMBLER__ */

#endif
