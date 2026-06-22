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
 * Header for minimal supervisor
 * @file
 */

#ifndef _SYS_BOGUX_H
#define _SYS_BOGUX_H

#include <arch/chip.h>

/** Number of registers that may be saved/restored by syscalls */
# define PT_REGS_COUNT (TREG_TP - 30 + 1)

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <util.h>   // from sys/libc

#include <arch/abi.h>

typedef unsigned long VirtualAddress;

/** Write a string to the console attributed to some source. */
void console_write(int fd, char* s, int len);

/** Names for certain file descriptors. */
extern const char* const console_names[];

/** Initialize per-tile FILE */
void init_per_tile_stdout(void);

/** Change stdout to use per-tile data (call only on master tile). */
void reset_stdout(void);

/** Put a variable in the .locks section.
 * These can only be initialized to zero because the section data is
 * allocated by the kernel shortly after boot.
 */
#define _LOCKS       __attribute__((section(".locks")))

/** Put a variable in the .tilestate section.
 * These can only be initialized to zero because the section data is
 * allocated by the kernel shortly after boot.
 */

/* First item in the .tilestate section.  */
#define _TILESTATE_1 __attribute__((section(".tilestate.1")))

/* Second item in the .tilestate section.  */
#define _TILESTATE_2 __attribute__((section(".tilestate.2")))

/* Other items in the .tilestate section.  */
#define _TILESTATE   __attribute__((section(".tilestate")))

/** Put a variable in the .l2aligned section initialized to zero. */
#define _L2ALIGNED   __attribute__((section(".l2aligned")))

/** Put a variable in the .l2aligned section with explicit initialization. */
#define _L2ALIGNED_INIT   __attribute__((section(".l2aligned")))

/** Open stdin, stdout, stderr */
void open_std_fds(void);

/** Load & execute the program named on our command line */
void load_and_run_default_program(void);

/** Describe syscall save area in intvec.S */
struct pt_regs
{
  unsigned long regs_valid;            /* does regs[] array hold valid data? */
  unsigned long regs[PT_REGS_COUNT];   /* tp and callee-saved regs */
  unsigned long pc;
  unsigned long ex1;
  unsigned long lr;
  unsigned long sp;
};

#endif /* !__ASSEMBLER__ */

/** Make it clear when a printf is really a warning */
#define warn printf

/** Handy utility macro */
#define ROUND_UP(val,align) (((val) + (align) - 1) & -(align))

/** Maximum number of tiles we support */
#define MAX_TILES 100

// Return values for routines called by the intvec.S int_hand macro.
// The return-from-interrupt code needs to know if it's returning from
// a hypervisor downcall, so it can appropriately reset or not reset the
// interrupt mask for the downcall interrupt.  The called routine knows
// whether it's been downcalled or not, so we use the return value to
// inform the caller.

#define INT_HAND_NO_DOWNCALL 0  /**< Interrupt was not a hypervisor downcall */
#define INT_HAND_DOWNCALL    1  /**< Interrupt was a hypervisor downcall */

/** Clock speed for hardware that runs Bogux */
#define CHIP_CLOCK_SPEED  750000000

#ifndef BOGUX_PL
/** Our protection level */
#define BOGUX_PL               2
#endif

/** Catenate strings. */
#define _CAT(a, b, c) a ## b ## c
/** Catenate strings. */
#define CAT(a, b, c)  _CAT(a, b, c)


/** Base VA for Bogux interrupt vectors */
#define BOGUX_INT_VEC_BASE     (0xfc000000 | (BOGUX_PL << 24))

/** Bogux system save register zero */
#define SYSTEM_SAVE_BX_0          CAT(SPR_SYSTEM_SAVE_, BOGUX_PL, _0)
/** Bogux system save register one */
#define SYSTEM_SAVE_BX_1          CAT(SPR_SYSTEM_SAVE_, BOGUX_PL, _1)
/** Bogux system save register two */
#define SYSTEM_SAVE_BX_2          CAT(SPR_SYSTEM_SAVE_, BOGUX_PL, _2)
/** Bogux system save register three */
#define SYSTEM_SAVE_BX_3          CAT(SPR_SYSTEM_SAVE_, BOGUX_PL, _3)
/** Bogux exceptional context register zero */
#define EX_CONTEXT_BX_0           CAT(SPR_EX_CONTEXT_, BOGUX_PL, _0)
/** Bogux exceptional context register one */
#define EX_CONTEXT_BX_1           CAT(SPR_EX_CONTEXT_, BOGUX_PL, _1)
/** Bogux interrupt mask register */
#define INTERRUPT_MASK_BX         CAT(SPR_INTERRUPT_MASK_, BOGUX_PL, )
/** Bogux set interrupt mask register */
#define INTERRUPT_MASK_SET_BX     CAT(SPR_INTERRUPT_MASK_SET_, BOGUX_PL, )
/** Bogux reset interrupt mask register */
#define INTERRUPT_MASK_RESET_BX   CAT(SPR_INTERRUPT_MASK_RESET_, BOGUX_PL, )
/** Bogux interrupt control status register */
#define INTCTRL_BX_STATUS         CAT(SPR_INTCTRL_, BOGUX_PL, _STATUS)
/** Bogux interrupt control status interrupt */
#define INT_INTCTRL_BX            CAT(INT_INTCTRL_, BOGUX_PL, )
/** Bogux interrupt vector base register */
#define INTERRUPT_VECTOR_BASE_BX  CAT(SPR_INTERRUPT_VECTOR_BASE_, BOGUX_PL,)

#endif
