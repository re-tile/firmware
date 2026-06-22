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
 * Debugging configuration and routines.
 * @file
 */

#ifndef _SYS_BOGUX_DEBUG_H
#define _SYS_BOGUX_DEBUG_H

#ifndef __ASSEMBLER__

#include <stdio.h>
#include <stdint.h>

/** Which debug options are enabled? */
extern uint32_t debug_flags;

#define DEBUG_SYSCALL        0x01    // Trace most system calls

/** Flags which are turned on by default (when DEBUG is defined) */
#define DEBUG_DEFAULT   0

#endif /* __ASSEMBLER__ */

#ifdef DEBUG

/** Default syscall trace */
#define SYSCALL_TRACE(fmt, ...) \
  if (debug_flags & DEBUG_SYSCALL) printf("syscall: " fmt, ## __VA_ARGS__)

/** Call the trace-wrapper versions of syscalls */
#define SYSCALL_FUNC(x) sys_ ## x ## _trace

#else

/** Null syscall trace */
#define SYSCALL_TRACE(...)

/** Call the regular versions of syscalls */
#define SYSCALL_FUNC(x) sys_ ## x

#endif

#endif /* _SYS_BOGUX_DEBUG_H */
