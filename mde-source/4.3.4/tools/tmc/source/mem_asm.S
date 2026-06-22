// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

#include <arch/abi.h>
        
#include <hv/syscall_public.h>

// Invoke the hypervisor's fence_incoherent syscall, which guarantees
// that all victims for cachelines homed on this tile have reached
// memory.
        .section .text.__tmc_mem_sys_fence_incoherent, "ax"
        .type __tmc_mem_sys_fence_incoherent,@function
        .global __tmc_mem_sys_fence_incoherent
        .align 8
__tmc_mem_sys_fence_incoherent:
        moveli TREG_SYSCALL_NR_NAME, HV_SYS_fence_incoherent
        swint2
        jrp lr

        .size __tmc_mem_sys_fence_incoherent, .-__tmc_mem_sys_fence_incoherent
