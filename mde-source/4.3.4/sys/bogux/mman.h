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
 * API for mmap().
 * @file
 */

#ifndef _BOGUX_MMAN_H
#define _BOGUX_MMAN_H

#include <stdbool.h>
#include <stdint.h>

/* Basic protections */
#define PROT_READ	0x1		/* Page can be read.  */
#define PROT_WRITE	0x2		/* Page can be written.  */
#define PROT_EXEC	0x4		/* Page can be executed.  */
#define PROT_NONE	0x0		/* Page can not be accessed.  */

/* Sharing types (must choose one and only one of these).
 * MAP_SHARED enables the old "MAP_COMMON" processing, which is sort
 * of like MAP_SHARED, but changes all tiles simultaneously.
 */
#define MAP_SHARED	0x01		/* Share changes.  */
#define MAP_PRIVATE	0x02		/* Changes are private.  */
#define MAP_TYPE	0x03		/* Mask for type of mapping.  */

/* Other flags.  */
#define MAP_FIXED	0x10		/* Interpret addr exactly.  */
#define MAP_FILE	0
#define MAP_ANONYMOUS	0x20		/* Don't use a file.  */
#define MAP_ANON	MAP_ANONYMOUS
#define MAP_HUGETLB     0x04000

/* These caching flags are meant to be a subset of the flags defined for
 * Linux in <asm/mman.h>, which should be considered canonical.
 */
#define _MAP_CACHE_HOME         0x80000
#define _MAP_CACHE_HOME_SHIFT   20
#define _MAP_CACHE_HOME_MASK    0x3ff
#define _MAP_CACHE_HOME_HERE    (_MAP_CACHE_HOME_MASK - 0)
#define _MAP_CACHE_HOME_NONE    (_MAP_CACHE_HOME_MASK - 1)
#define _MAP_CACHE_HOME_HASH    (_MAP_CACHE_HOME_MASK - 4)
#define _MAP_CACHE_INCOHERENT   0x40000
#define MAP_CACHE_NO_LOCAL      0x20000
#define MAP_CACHE_PRIORITY      0x02000
#define _MAP_CACHE_OLD          0x10000

#define MAP_CACHE_INCOHERENT ( \
  _MAP_CACHE_INCOHERENT | \
  _MAP_CACHE_HOME | \
  (_MAP_CACHE_HOME_NONE << _MAP_CACHE_HOME_SHIFT) \
  )

/** Max number of controllers (valid for first chip release) */
#define MAX_CONTROLLERS 4

/** Memory policies */
#define MPOL_DEFAULT    0
#define MPOL_BIND       1
#define MPOL_PREFERRED  2
#define MPOL_INTERLEAVE 3

#ifdef __BOGUX__

#include <hv/hypervisor.h>

typedef enum { MMAP_LAZY = 0, MMAP_ZERO = 1, MMAP_UNINIT = 2 } MmapInitialize;

/** A generic fd to be used for 'I need a huge TLB'
 */
#define MMAP_FD_HUGETLB -1

VirtualAddress do_mmap(VirtualAddress address_in, uint32_t size,
                       int32_t protection, int32_t flags, int fd,
                       size_t pgoffset, int32_t controller, MmapInitialize);
#define MMAP_RESULT_IS_ERROR(va) ((va) > (VirtualAddress)-HV_PAGE_SIZE_SMALL)
HV_PhysAddr va_to_cpa(const void* va);
HV_PhysAddr va_to_cpa_and_pte(const void* va, HV_PTE* ptep);
int is_valid_user_buf(const void* va, size_t size, int prot);
int is_valid_user_string(const void* va, int prot);
void munmap_user_pages(void);
void set_brk_start(VirtualAddress load_top);

/** Get a new page (uninitialized) from the pa-allocator.
 * FIXME: CONTROLLER_ANY should allow you to get memory from any
 * controller, but right now it just equates to the nearest one only.
 */
HV_PhysAddr get_new_page(unsigned long size, VirtualAddress, int controller);

/** Free a previously-allocated page. */
void free_page(unsigned long size, HV_PhysAddr cpa);

/** Map the initial stack into a new user process. */
void map_initial_stack(VirtualAddress sp, HV_PhysAddr base, int size);

/** Map a specific chunk of physical address space to our mapping window. */
void* map_window(HV_PhysAddr pa, size_t bytes);

/** Unmap a chunk of memory from the mapping window */
void unmap_window(void* va, size_t bytes);

struct AllocMemory
{
  HV_PhysAddr tile_state;
  HV_PhysAddr stack;
  int local_controller;
};
void init_physmem(HV_Topology);
void alloc_tile_mem(HV_Coord, struct AllocMemory*, HV_Coord);
void init_tile(HV_Coord, const struct AllocMemory*);
void init_final(void);

extern int width, height, num_avail_tiles;
extern HV_Coord ts_coord;

/** Convert coordinates to a linear tile ID */
static inline uint32_t tile_id(int x, int y)
  { return y * width + x; }

/** Return tile ID of this tile */
static inline uint32_t my_tile_id(void)
  { return tile_id(ts_coord.x, ts_coord.y); }

/** Report whether the given tile is available to Bogux. */
bool is_avail_tile(unsigned int tilenum);

/** Set up to use another tile's cache by default */
void set_default_oloc(uint32_t x, uint32_t y);

/** Set up to use random other caches by default */
void set_random_oloc(uint32_t seed);

/** Get the coordinates for a default page */
void get_oloc(uint32_t* x, uint32_t* y);

extern bool ts_default_oloc_enabled;

/** Current default memory controller */
extern uint32_t ts_controller;

#endif

#endif
