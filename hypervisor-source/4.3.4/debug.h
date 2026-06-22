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
 * Debugging configuration and routines.
 */

#ifndef _SYS_HV_DEBUG_H
#define _SYS_HV_DEBUG_H

#include <stdio.h>

#include "types.h"
#include "sys/libc/include/util.h"

void zero_save_regs(void);
void dump_save_regs(void);
void debug_init(int phase);

/** Which debug options are enabled? */
extern uint32_t debug_flags;

#define DEBUG_SYSCALL        0x0001  ///< Trace most system calls
#define DEBUG_TSB            0x0002  ///< Trace TSB misses/access violations
#define DEBUG_NOISY_SYSCALL  0x0004  ///< Trace usually uninteresting syscalls
#define DEBUG_INIT           0x0008  ///< Trace hypervisor initialization
#define DEBUG_DUMP_BI        0x0010  ///< Dump board information at startup
#define DEBUG_INIT_FS        0x0020  ///< Don't disable tracing in init_fs()
#define DEBUG_DEVICES        0x0040  ///< Trace device discovery and operations
#define DEBUG_CYCLES         0x0080  ///< Print cycles used on hv_halt()
#define DEBUG_SPEED          0x0100  ///< Trace speed/voltage settings
#define DEBUG_TSB_VERBOSE    0x0200  ///< Show each entry added to the TSB
#define DEBUG_ALLOC          0x0400  ///< Trace local_alloc() activity
#define DEBUG_UART_PROTO     0x0800  ///< Leave UART in protocol mode for debug
#define DEBUG_BME_BRINGUP    0x1000  ///< Debug BME bringup
#define DEBUG_BME_SMALLPAGES 0x2000  ///< Make BME use small pages, for testing

/** Flags which are turned on by default (when DEBUG is defined) */
#define DEBUG_DEFAULT   (0)

#ifdef DEBUG

/** Assertion */
#undef assert
#define assert(x) \
  do \
  { \
    if (unlikely(!(x))) \
      panic("assertion failed in %s at line %d: %s", \
            __FILE__, __LINE__, #x); \
  } while (0)

/** Default syscall trace */
#define SYSCALL_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_SYSCALL)) \
      tprintf("syscall: " fmt, ## __VA_ARGS__); \
  } while (0)

/** TSB trace */
#define TSB_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_TSB)) \
      tprintf("tsb: " fmt, ## __VA_ARGS__); \
  } while (0)

/** Noisy syscall trace (due to volume, not in default syscall trace) */
#define NOISY_SYSCALL_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_NOISY_SYSCALL)) \
      tprintf("syscall: " fmt, ## __VA_ARGS__); \
  } while (0)

/** Initialization trace */
#define INIT_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_INIT)) \
      tprintf("init: " fmt, ## __VA_ARGS__); \
  } while (0)

/** Device trace */
#define DEVICE_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_DEVICES)) \
      tprintf("device: " fmt, ## __VA_ARGS__); \
  } while (0)

/** Speed trace */
#define SPEED_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_SPEED)) \
      tprintf("speed: " fmt, ## __VA_ARGS__); \
  } while (0)

/** Allocation trace */
#define ALLOC_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_ALLOC)) \
      tprintf("alloc: " fmt, ## __VA_ARGS__); \
  } while (0)

/** BME bringup trace */
#define BME_TRACE(fmt, ...) \
  do \
  { \
    if (unlikely(debug_flags & DEBUG_BME_BRINGUP)) \
      tprintf("bme: " fmt, ## __VA_ARGS__); \
  } while (0)

#else

/** Null assertion. Note that we still compute the argument value in this
 *  case, we just don't check it.  The theory is that we don't want to not
 *  compute something which might be needed later, and if it isn't needed
 *  later, the compiler will optimize it away.
 */
#undef assert
#define assert(x)     x

/** Null syscall trace */
#define SYSCALL_TRACE(...)

/** Null TSB trace */
#define TSB_TRACE(...)

/** Null noisy syscall trace */
#define NOISY_SYSCALL_TRACE(...)

/** Null initialization trace */
#define INIT_TRACE(...)

/** Null device trace */
#define DEVICE_TRACE(...)

/** Null alloc trace */
#define ALLOC_TRACE(...)

/** Null BME trace */
#define BME_TRACE(...)

#endif


#endif /* _SYS_HV_DEBUG_H */
