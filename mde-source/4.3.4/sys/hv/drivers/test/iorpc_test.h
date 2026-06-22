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
 * Test driver for iorpc.
 */
#ifndef _DRIVERS_TEST_IORPC_TEST_H_
#define _DRIVERS_TEST_IORPC_TEST_H_

#include <stdbool.h>
#include <hv/drv_test_intf.h>
#include <hv/iorpc.h>

#include "lock.h"

/** Maximum number of service domains on the test device. */
#define MAX_SVC_DOM 2

/** Maximum number of buffers that can be registered. */
#define MAX_BUFFERS 4

/** State maintained for each service domain. */
typedef struct
{
  /** Our virtual registers. */
  struct test_iorpc_regs regs;
  
  /** Last Buffer registered by the user. */
  struct
  {
    bool valid;             /**< User has registered a buffer. */
    PA pa;                  /**< Physical address. */
    size_t size;            /**< Size. */
    struct iorpc_mem_attr attr;  /**< Homing and performance bits. */
  }
  buffers[MAX_BUFFERS];
}
iorpc_svc_dom_t;

/** A shared memory state object. */
typedef struct
{
  /** Lock that must be held when reading or modifying this stucture. */
  spinlock_t lock;
  
  /** A bit set for each unallocated svc_dom. */
  int svc_dom_avail_mask;

  /** State for each service domain. */
  iorpc_svc_dom_t svc_doms[MAX_SVC_DOM];

  /** A single global "large data array". */
  struct test_data_array data_array;
}
iorpc_test_state_t;

#endif /* ! _DRIVERS_TEST_IORPC_TEST_H_ */
