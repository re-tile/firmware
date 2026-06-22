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
 * General hypervisor definitions.
 */

#ifndef _SYS_HV_HV_H
#define _SYS_HV_HV_H

#include "param.h"
#include "tsb.h"
#include "types.h"

#ifndef __ASSEMBLER__
#include "misc.h"
#include "tile_mask.h"
#endif /* !__ASSEMBLER__ */


/** Catenate strings. */
#define _CAT(a, b, c) a ## b ## c
/** Catenate strings. */
#define CAT(a, b, c)  _CAT(a, b, c)


/** First address in hypervisor's VA region */
#define HV_VA_BASE                  0xfffffffc00000000UL
/** First address after hypervisor's VA region.  We must avoid the last 2
 * GB of the address space since that region is used by client programs
 * compiled in ILP32 mode. */
#define HV_VA_LIMIT                 (HV_VA_BASE + (1UL << 34) - (1UL << 31))

/** Hypervisor system save register zero */
#define SYSTEM_SAVE_HV_0          CAT(SPR_SYSTEM_SAVE_, HV_PL, _0)
/** Hypervisor system save register one */
#define SYSTEM_SAVE_HV_1          CAT(SPR_SYSTEM_SAVE_, HV_PL, _1)
/** Hypervisor system save register two */
#define SYSTEM_SAVE_HV_2          CAT(SPR_SYSTEM_SAVE_, HV_PL, _2)
/** Hypervisor system save register three */
#define SYSTEM_SAVE_HV_3          CAT(SPR_SYSTEM_SAVE_, HV_PL, _3)
/** Hypervisor exceptional context register zero */
#define EX_CONTEXT_HV_0           CAT(SPR_EX_CONTEXT_, HV_PL, _0)
/** Hypervisor exceptional context register one */
#define EX_CONTEXT_HV_1           CAT(SPR_EX_CONTEXT_, HV_PL, _1)
/** Hypervisor interrupt mask register */
#define INTERRUPT_MASK_HV         CAT(SPR_INTERRUPT_MASK_, HV_PL,)
/** Hypervisor set interrupt mask register */
#define INTERRUPT_MASK_SET_HV     CAT(SPR_INTERRUPT_MASK_SET_, HV_PL,)
/** Hypervisor reset interrupt mask register */
#define INTERRUPT_MASK_RESET_HV   CAT(SPR_INTERRUPT_MASK_RESET_, HV_PL,)
/** Hypervisor interrupt control status register */
#define INTCTRL_HV_STATUS         CAT(SPR_INTCTRL_, HV_PL,_STATUS)
/** Hypervisor interrupt control status interrupt */
#define INT_INTCTRL_HV            CAT(INT_INTCTRL_, HV_PL,)

/** Hypervisor interrupt vector base register */
#define INTERRUPT_VECTOR_BASE_HV  CAT(SPR_INTERRUPT_VECTOR_BASE_, HV_PL,)

/** Hypervisor IPI mask register */
#define IPI_MASK_HV               CAT(SPR_IPI_MASK_, HV_PL,)
/** Hypervisor IPI mask reset register */
#define IPI_MASK_RESET_HV         CAT(SPR_IPI_MASK_RESET_, HV_PL,)
/** Hypervisor IPI mask set register */
#define IPI_MASK_SET_HV           CAT(SPR_IPI_MASK_SET_, HV_PL,)
/** Hypervisor IPI event register */
#define IPI_EVENT_HV              CAT(SPR_IPI_EVENT_, HV_PL,)
/** Hypervisor IPI event reset register */
#define IPI_EVENT_RESET_HV        CAT(SPR_IPI_EVENT_RESET_, HV_PL,)
/** Hypervisor IPI event set register */
#define IPI_EVENT_SET_HV          CAT(SPR_IPI_EVENT_SET_, HV_PL,)
/** Hypervisor IPI interrupt */
#define INT_IPI_HV                CAT(INT_IPI_, HV_PL,)

/** PL of client program */
#if HV_PL == 3
#define CLIENT_PL               2
#define GUEST_PL                1
#elif HV_PL == 2
#define CLIENT_PL               1
#else
#error Code only supports HV_PL of 2 or 3
#endif


/** Base VA for client interrupt vectors */
#define CLIENT_INT_VEC_BASE     (0xfc000000 | (CLIENT_PL << 24))

/** Client system save register zero */
#define SYSTEM_SAVE_CL_0          CAT(SPR_SYSTEM_SAVE_, CLIENT_PL, _0)
/** Client system save register one */
#define SYSTEM_SAVE_CL_1          CAT(SPR_SYSTEM_SAVE_, CLIENT_PL, _1)
/** Client system save register two */
#define SYSTEM_SAVE_CL_2          CAT(SPR_SYSTEM_SAVE_, CLIENT_PL, _2)
/** Client system save register three */
#define SYSTEM_SAVE_CL_3          CAT(SPR_SYSTEM_SAVE_, CLIENT_PL, _3)
/** Client exceptional context register zero */
#define EX_CONTEXT_CL_0           CAT(SPR_EX_CONTEXT_, CLIENT_PL, _0)
/** Client exceptional context register one */
#define EX_CONTEXT_CL_1           CAT(SPR_EX_CONTEXT_, CLIENT_PL, _1)
/** Client interrupt mask register */
#define INTERRUPT_MASK_CL         CAT(SPR_INTERRUPT_MASK_, CLIENT_PL,)
/** Client set interrupt mask register */
#define INTERRUPT_MASK_SET_CL     CAT(SPR_INTERRUPT_MASK_SET_, CLIENT_PL,)
/** Client reset interrupt mask register */
#define INTERRUPT_MASK_RESET_CL   CAT(SPR_INTERRUPT_MASK_RESET_, CLIENT_PL,)
/** Client interrupt control status register */
#define INTCTRL_CL_STATUS         CAT(SPR_INTCTRL_, CLIENT_PL, _STATUS)
/** Client interrupt control status interrupt */
#define INT_INTCTRL_CL            CAT(INT_INTCTRL_, CLIENT_PL,)

/** Client interrupt vector base register */
#define INTERRUPT_VECTOR_BASE_CL  CAT(SPR_INTERRUPT_VECTOR_BASE_, CLIENT_PL,)

/** Hypervisor interrupt control status interrupt */
#define INT_IPI_CL                CAT(INT_IPI_, CLIENT_PL,)


#ifdef GUEST_PL
/** Guest system save register zero */
#define SYSTEM_SAVE_G_0           CAT(SPR_SYSTEM_SAVE_, GUEST_PL, _0)
/** Guest system save register one */
#define SYSTEM_SAVE_G_1           CAT(SPR_SYSTEM_SAVE_, GUEST_PL, _1)
/** Guest system save register two */
#define SYSTEM_SAVE_G_2           CAT(SPR_SYSTEM_SAVE_, GUEST_PL, _2)
/** Guest system save register three */
#define SYSTEM_SAVE_G_3           CAT(SPR_SYSTEM_SAVE_, GUEST_PL, _3)
/** Guest exceptional context register zero */
#define EX_CONTEXT_G_0            CAT(SPR_EX_CONTEXT_, GUEST_PL, _0)
/** Guest exceptional context register one */
#define EX_CONTEXT_G_1            CAT(SPR_EX_CONTEXT_, GUEST_PL, _1)
/** Guest interrupt mask register */
#define INTERRUPT_MASK_G          CAT(SPR_INTERRUPT_MASK_, GUEST_PL,)
/** Guest set interrupt mask register */
#define INTERRUPT_MASK_SET_G      CAT(SPR_INTERRUPT_MASK_SET_, GUEST_PL,)
/** Guest reset interrupt mask register */
#define INTERRUPT_MASK_RESET_G    CAT(SPR_INTERRUPT_MASK_RESET_, GUEST_PL,)
/** Guest interrupt control status register */
#define INTCTRL_G_STATUS          CAT(SPR_INTCTRL_, GUEST_PL, _STATUS)
/** Guest interrupt control status interrupt */
#define INT_INTCTRL_G             CAT(INT_INTCTRL_, GUEST_PL,)
/** Guest interrupt vector base register */
#define INTERRUPT_VECTOR_BASE_G   CAT(SPR_INTERRUPT_VECTOR_BASE_, GUEST_PL,)
/** Hypervisor interrupt control status interrupt */
#define INT_IPI_G                 CAT(INT_IPI_, GUEST_PL,)
#endif


/** PL of user program */
#define USER_PL                 0
/** Base VA for user interrupt vectors */
#define USER_INT_VEC_BASE       0xfc000000

#ifndef __ASSEMBLER__

/** Absolute LOTAR value for this tile. */
extern Lotar my_lotar;

/** Physical address of my text segment */
extern PA my_text_pa;

/** Physical address of my data segment */
extern PA my_data_pa;

/** This tile's coordinates. FIXME: this is the same as my_lotar; one of these
 *  needs to vanish. */
extern pos_t my_pos;

/** Coordinate of the tile at the upper left-hand corner of the chip. */
#define chip_ulhc ((pos_t) { .word = 0 } )

/** Coordinate of the tile at the lower right-hand corner of the chip. */
extern pos_t chip_lrhc;

/** Coordinate of the tile at the logical upper left-hand corner of the 
 *  chip. */
extern pos_t chip_logical_ulhc;

/** Coordinate of the tile at the logical lower right-hand corner of 
 *  the chip. */
extern pos_t chip_logical_lrhc;

/** Coordinate of the switch point at the upper left-hand corner of the 
 *  fabric. */
extern pos_t grid_ulhc;

/** Coordinate of the switch point at the lower right-hand corner of the 
 *  fabric. */
extern pos_t grid_lrhc;

/** Coordinate of the master (initial boot) tile. */
extern pos_t chip_master;

/** Coordinate of the tile which owns the real console; other tiles do
 *  console output by sending messages to this one. */
extern pos_t chip_console;

/** Nonzero if this tile is the master (initial boot) tile. */
extern int is_master;

/** Nonzero if this tile is a dedicated driver tile. */
extern int is_dedicated;

/** Nonzero if we're running the thorough version of POST. */
extern int post_is_thorough;

/** Coordinate of the tile at the upper left-hand corner of the client's
 *  rectangle. */
extern pos_t client_ulhc;

/** Coordinate of the tile at the lower right-hand corner of the client's
 *  rectangle. */
extern pos_t client_lrhc;

/** Mask of valid tiles in the client. */
extern tile_mask client_tiles;

/** Value of the cycle counter upon initial entry to the hypervisor's main
 *  routine. */
extern uint64_t init_cycle_count;

/** Nonzero if shared allocator is up and running. */
extern int shared_alloc_initialized;

/** Processor speed in hertz. */
extern uint32_t cpu_speed;

/** Initial guess at our CPU speed.  We set this to be larger than any
 *  possible real speed, so that any calculated delays will be too long,
 *  rather than too short. */
#define INIT_CPU_SPEED (2000 * 1000 * 1000)

/** Processor speed in hertz; used in routines which might be called very
 *  early in boot, before the shared allocator is running. */
static inline uint32_t
early_cpu_speed(void)
{
  return shared_alloc_initialized ? cpu_speed : INIT_CPU_SPEED;
}

/** Processor speed in hertz. */
extern uint32_t cpu_speed;

/** Reference clock speed in hertz. */
extern uint32_t refclk_speed;

/** Board flags. */
extern uint32_t board_flags;

/** Version string. */
extern const char hv_version[];

/** Small page size. */
extern size_t page_size_small;

/** Large page size. */
extern size_t page_size_large;

/** Jumbo page size. */
extern size_t page_size_jumbo;

/** Log2 of small page size. */
extern int page_shift_small;

/** Log2 of large page size. */
extern int page_shift_large;

/** Log2 of jumbo page size. */
extern int page_shift_jumbo;

/** Is this chip a Gx72 or derivative? */
extern int is_gx72;

#ifndef L1BOOT
/** Convert a pos_t to a tile index. */
#define POS2IDX(pos) _POS2IDX(pos, chip_ulhc)

/** Convert a tile index to a pos_t. */
#define IDX2POS(idx) _IDX2POS(idx, chip_ulhc)
#endif /* !L1BOOT */

/** Convert a raw chip coordinate to a user-printable one. */
#define _toU(x) ((x) == 0xF ? -1 : (x))

/** Extract an X coordinate from a pos_t and convert it to user space */
#define UX(pos) _toU((pos).bits.x - chip_ulhc.bits.x)

/** Extract a Y coordinate from a pos_t and convert it to user space */
#define UY(pos) _toU((pos).bits.y - chip_ulhc.bits.y)

/** Extract both the X and Y coordinates from a pos_t and convert them
 *  to user space; intended for use in printf argument lists */
#define UXY(pos) UX(pos), UY(pos)

/** Convert a pos_t to a real LOTAR, suitable for use in a TLB entry. */
#define POS2LOTAR(pos) (((pos).word >> 7) & 0x3FFFFF)

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_HV_H */
