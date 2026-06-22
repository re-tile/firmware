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
 * Virtual address usage for Bogux.
 * @file
 */

#ifndef _SYS_BOGUX_MEM_LAYOUT_H
#define _SYS_BOGUX_MEM_LAYOUT_H

/* Where we start trying to mmap from (to try to keep 0..64K unmapped) */
#define MEM_USERSTART_VA              0x40000000

/* The arbitrary spot where we try to start putting common pages. */
#define MEM_COMMONSTART_VA            0x80000000

/* The top of user memory. */
#define MEM_USER_TOP       0xfffffff400000000

/* The stack lives at the top of user memory. */
#define MEM_USERSTACK_TOP  MEM_USER_TOP
#define MEM_USERSTACK_SIZE (1 * 1024 * 1024)

/* Note that the initial page table setup assumes that all of Bogux's code
 * and data is in the same jumbo page -- i.e., that all of the MEM_xxx_VA
 * symbols match in bits [39:32].
 */

/* Data (.rodata, .data, and .bss) is loaded in the next chunk.
 * It follows the code in the ELF image and is mapped with one large TLB entry.
 * The initial kernel stack by default starts at the very top of this section.
 */
#define MEM_DATA_VA        0xfffffff400000000
#define MEM_DATA_PA                0x01000000
#define MEM_DATA_SIZE              0x01000000
#define MEM_DATA_PA_ADJUST (MEM_DATA_VA - MEM_DATA_PA)
#define MEM_INITIAL_SP     (MEM_DATA_VA + MEM_DATA_SIZE)

/* One L2 page table is used for miscellaneous supervisor purposes */
#define MEM_SVMISC_VA      0xfffffff401000000
#define MEM_SVMISC_SIZE            0x01000000

/* The first chunk of supervisor memory is used for per-tile state.
 * It includes the MEM_SVMISC L2 page table and the L1 page table.
 */
#define MEM_TILESTATE_VA   0xfffffff401000000
#define MEM_TILESTATE_SIZE         0x00010000

/* After a skip for a stack redzone, we allocate the per-tile stack.
 * For now follow Linux and assume 8KB is enough.
 */
#define MEM_STACK_VA       0xfffffff401020000
#define MEM_STACK_SIZE             0x00002000
#if MEM_STACK_SIZE < HV_PAGE_SIZE_SMALL
#undef MEM_STACK_SIZE
#define MEM_STACK_SIZE HV_PAGE_SIZE_SMALL
#endif
#define MEM_STACK_INIT     (MEM_STACK_VA + MEM_STACK_SIZE)

/* Skip up a bit to get away from the useful tile data and also leave
 * a hole below us for the stack.
 */
#define MEM_PAMAP_VA       0xfffffff401040000
#define MEM_PAMAP_SIZE             0x00020000

/* One page used for locks (the .lock section).
 * We skip up a bunch here to give a visual cue that we are no longer
 * in the tile-specific address range.
 */
#define MEM_LOCKS_VA       0xfffffff401100000
#define MEM_LOCKS_SIZE             0x00001000

/* This symbol must be set by inspection of the generated bogux image.
 * It is tested on each build to make sure it is still correct.
 * It is used in start.S in a context where we can't use a global.
 */
#define MEM_L2_PAGE_TABLE_VA (MEM_DATA_VA + 0x20000)

/* Code loads at the address of the supervisor interrupt vector.
 * It is the first thing loaded into memory.
 */
#define MEM_CODE_VA        0xfffffff402000000
#define MEM_CODE_PA                0x00000000
#define MEM_CODE_PA_ADJUST (MEM_CODE_VA - MEM_CODE_PA)

/* Location of the VA hole. */
#define MEM_HOLE_BEGIN     (1UL << (CHIP_VA_WIDTH() - 1))
#define MEM_HOLE_END       ~((1UL << (CHIP_VA_WIDTH() - 1)) - 1)

#endif /* _SYS_BOGUX_MEM_LAYOUT_H */
