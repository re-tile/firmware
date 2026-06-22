/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

#ifndef __GXPCI_HOST_PQ_H__
#define __GXPCI_HOST_PQ_H__

#include <features.h>
#include <stdint.h>
#include <unistd.h>

#include <asm/tilegxpci.h>

#include <gxio/trio.h>

#include <sys/types.h>

__BEGIN_DECLS

/**
 * Below is the host packet queue backing memory assignment.
 */

/** The h2t and t2h packet queue backing memory size. */
#define GXPCI_HOST_PQ_BACK_MEM_SIZE tmc_alloc_get_huge_pagesize()

/** The h2t and t2h packet queue's host driver-visible registers' offset
 *  in the backing memory.
 */
#define GXPCI_HOST_PQ_REGS_DRV_OFFSET 0x0

/** The h2t and t2h packet queue's host app-visible registers' offset
 *  in the backing memory. Note that this region follows the host
 *  driver-visible register region so that they can share a single
 *  TRIO Map-Memory region, even though they are mapped into host
 *  user space and kernel space, respectively.
 */
#define GXPCI_HOST_PQ_REGS_APP_OFFSET \
	GXPCI_HOST_PQ_REGS_DRV_MAP_SIZE

/** The h2t and t2h packet queue's host driver-visible registers' offset
 *  in the backing memory for the VF.
 */
#define GXPCI_VF_HOST_PQ_REGS_DRV_OFFSET 0x0

/** The h2t and t2h packet queue's host app-visible registers' offset
 *  in the backing memory for the VF.
 */
#define GXPCI_VF_HOST_PQ_REGS_APP_OFFSET \
                        GXPCI_VF_HOST_PQ_REGS_DRV_MAP_SIZE

/** The h2t and t2h packet queue's state structure's offset
    in the backing memory. */
#define GXPCI_HOST_PQ_STATE_STRUCT_OFFSET 0x80000

/** The h2t packet queue's data pull DMA command ring buffer's offset
    in the backing memory, for receiving data. */
#define GXPCI_HOST_PQ_PULL_DMA_CMDS_BUF_OFFSET 0x200000

/** The t2h packet queue's data push DMA command ring buffer's offset
    in the backing memory, for sending data. */
#define GXPCI_HOST_PQ_PUSH_DMA_CMDS_BUF_OFFSET 0x400000

__END_DECLS

#endif // __GXPCI_HOST_PQ_H__
