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
 * Indices for the hypervisor system calls.
 */

#ifndef _SYS_HV_INCLUDE_SYSCALL_H
#define _SYS_HV_INCLUDE_SYSCALL_H

#include <hv/syscall_public.h>

/** Bit location for flagging syscalls that can be called from guest PL.
 * This is not set by default for any call, but should be set for calls
 * where the guest is directly invoking the hypervisor.
 */
#define HV_SYS_GUEST_SHIFT                12

/** Syscall allowed from guest PL bit mask. */
#define HV_SYS_GUEST_MASK                 (1 << HV_SYS_GUEST_SHIFT)

/* Hypervisor syscalls. These are NOT part of the fixed binary API,
 * as they should never be used directly. Instead, call the hv_* functions,
 * such as hv_init().
 */
/** downcall_dispatch; this syscall number must be zero */
#define HV_SYS_downcall_dispatch          0
/** install_context */
#define HV_SYS_install_context            1
/** sysconf */
#define HV_SYS_sysconf                    2
/** get_rtc */
#define HV_SYS_get_rtc                    3
/** set_rtc */
#define HV_SYS_set_rtc                    4
/** flush_asid */
#define HV_SYS_flush_asid                 5
/** flush_page */
#define HV_SYS_flush_page                 6
/** flush_pages */
#define HV_SYS_flush_pages                7
/** restart */
#define HV_SYS_restart                    8
/** halt */
#define HV_SYS_halt                       9
/** power_off */
#define HV_SYS_power_off                 10
/** inquire_physical */
#define HV_SYS_inquire_physical          11
/** inquire_memory_controller */
#define HV_SYS_inquire_memory_controller 12
/** inquire_virtual */
#define HV_SYS_inquire_virtual           13
/** inquire_asid */
#define HV_SYS_inquire_asid              14
/** console_read_if_ready */
#define HV_SYS_console_read_if_ready     15
/** console_write */
#define HV_SYS_console_write             16
/** init */
#define HV_SYS_init                      17
/** inquire_topology */
#define HV_SYS_inquire_topology          18
/** fs_findfile */
#define HV_SYS_fs_findfile               19
/** fs_fstat */
#define HV_SYS_fs_fstat                  20
/** fs_pread */
#define HV_SYS_fs_pread                  21
/** physaddr_read64 */
#define HV_SYS_physaddr_read64           22
/** physaddr_write64 */
#define HV_SYS_physaddr_write64          23
/** get_command_line */
#define HV_SYS_get_command_line          24
/** set_caching */
#define HV_SYS_set_caching               25
/** bzero_page */
#define HV_SYS_bzero_page                26
/** register_message_state */
#define HV_SYS_register_message_state    27
/** send_message */
#define HV_SYS_send_message              28
/** receive_message */
#define HV_SYS_receive_message           29
/** inquire_context */
#define HV_SYS_inquire_context           30
/** start_all_tiles */
#define HV_SYS_start_all_tiles           31
/** dev_open */
#define HV_SYS_dev_open                  32
/** dev_close */
#define HV_SYS_dev_close                 33
/** dev_pread */
#define HV_SYS_dev_pread                 34
/** dev_pwrite */
#define HV_SYS_dev_pwrite                35
/** dev_poll */
#define HV_SYS_dev_poll                  36
/** dev_poll_cancel */
#define HV_SYS_dev_poll_cancel           37
/** dev_preada */
#define HV_SYS_dev_preada                38
/** dev_pwritea */
#define HV_SYS_dev_pwritea               39
/** flush_remote */
#define HV_SYS_flush_remote              40
/** console_putc */
#define HV_SYS_console_putc              41
/** inquire_tiles */
#define HV_SYS_inquire_tiles             42
/** confstr */
#define HV_SYS_confstr                   43
/** reexec */
#define HV_SYS_reexec                    44
/** set_command_line */
#define HV_SYS_set_command_line          45


/** fence_incoherent (exported in <hv/syscall_public.h>) */
#define HV_SYS_fence_incoherent         (51 | HV_SYS_FAST_MASK \
                                       | HV_SYS_FAST_PL0_MASK)
/** store_mapping */
#define HV_SYS_store_mapping             52
/** inquire_realpa */
#define HV_SYS_inquire_realpa            53
/** flush_all */
#define HV_SYS_flush_all                 54

/** get_ipi_pte */
#define HV_SYS_get_ipi_pte               55

/** set_pte_super_shift */
#define HV_SYS_set_pte_super_shift       56
/** set_speed */
#define HV_SYS_set_speed                 57
/** install_virt_context */
#define HV_SYS_install_virt_context      58
/** inquire_virt_context */
#define HV_SYS_inquire_virt_context      59
/** inquire_guest_context */
#define HV_SYS_install_guest_context     60
/** inquire_guest_context */
#define HV_SYS_inquire_guest_context     61

/** console_set_ipi */
#define HV_SYS_console_set_ipi           62

/** close_all_devices */
#define HV_SYS_close_all_devices         63

/** send_nmi */
#define HV_SYS_send_nmi                  64


/** Number of the largest-numbered syscall. */
#define HV_MAX_SYSCALL                   64

#endif /* !_SYS_HV_INCLUDE_SYSCALL_H */
