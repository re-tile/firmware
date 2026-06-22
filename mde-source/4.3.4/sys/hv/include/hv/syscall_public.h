/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
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
 * Indices for the hypervisor system calls that are intended to be called
 * directly, rather than only through hypervisor-generated "glue" code.
 */

#ifndef _SYS_HV_INCLUDE_SYSCALL_PUBLIC_H
#define _SYS_HV_INCLUDE_SYSCALL_PUBLIC_H

/** Fast syscall flag bit location.  When this bit is set, the hypervisor
 *  handles the syscall specially.
 */
#define HV_SYS_FAST_SHIFT                 14

/** Fast syscall flag bit mask. */
#define HV_SYS_FAST_MASK                  (1 << HV_SYS_FAST_SHIFT)

/** Bit location for flagging fast syscalls that can be called from PL0. */
#define HV_SYS_FAST_PLO_SHIFT             13

/** Fast syscall allowing PL0 bit mask. */
#define HV_SYS_FAST_PL0_MASK              (1 << HV_SYS_FAST_PLO_SHIFT)

/** Perform an MF that waits for all victims to reach DRAM. */
#define HV_SYS_fence_incoherent         (51 | HV_SYS_FAST_MASK \
                                       | HV_SYS_FAST_PL0_MASK)

#endif /* !_SYS_HV_INCLUDE_SYSCALL_PUBLIC_H */
