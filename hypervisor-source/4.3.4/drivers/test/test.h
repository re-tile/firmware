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
 * Definitions for the test driver.
 */

#ifndef _SYS_HV_DRV_TEST_H
#define _SYS_HV_DRV_TEST_H

#include "drvintf.h"
#include <hv/drv_test_intf.h>

/** Test driver state structure. */
typedef struct
{
  int instance;                  /**< This device's instance number */
  int tileno;                    /**< This device's dedicated tile number */
  pos_t my_pos;                  /**< This tile's coordinates */
  pos_t fwd_tile;                /**< Forward requests here */
  uint32_t fwd:1;                /**< Forward requests to fwd_tile? */
  const struct dev_info* infop;  /**< Device information */
  char* ctlbuf;                  /**< Holding buffer for control test
                                      read/write; NULL on all but shared tile */
  uint32_t pollstate;            /**< Events we're currently waiting for */
  pos_t poller;                  /**< Tile which is waiting */
  uint32_t poll_intarg;          /**< Argument for poller */

  uint32_t cause_instant_intr;   /**< Request an instant interrupt */
  pos_t dest_instant_intr;       /**< Destination for instant interrupt */
  uint32_t cause_delayed_intr;   /**< Request a delayed interrupt */
  pos_t dest_delayed_intr;       /**< Destination for delayed interrupt */

  uint32_t* last_instant_intr;   /**< Last instant interrupt received */
  uint32_t* last_delayed_intr;   /**< Last delayed interrupt received */

  uint32_t fastio_index;         /**< Index of our first fastio function */
  uint32_t last_fastio_a;        /**< Last arg to our first fastio function */

  struct test_shared_data* tsd;  /**< Pointer to our data shared with the
                                      client */
  VA ctsd;                       /**< Client address of the shared data */
  int intr_index;                /**< Index of first one-shot/level-sensitive
                                      interrupt */
}
test_state_t;

/** Number of instances supported */
#define TEST_MAX_INST 2

/** Control device flag */
#define TEST_CTL_MASK 1

/** Size of *ctlbuf; big enough to get multiple remote calls on a big
 *  request */
#define TEST_CTLBUF_SIZE  (4 * DRV_ATOMIC_LEN)

/** Number of bytes in special driver-messaging region */
#define TEST_DRVMSG_SIZE  128

/** Size of the shared buffer */
#define TEST_SHAREBUF_SIZE (128 * 1024)

/** Messages sent to driver dedicated tiles */
struct testdrv_msg
{
  int op;                        /**< Opcode for transform */
  int len;                       /**< Length of data */
  char data[TEST_DRVMSG_SIZE];   /**< Data */
};

#define TESTDRV_MSG_MINUSONE   0x0  /**< Transform bytes and return data - 1 */


drv_intr_func testdrv_instant_intr;       ///< Instant interrupt routine
drv_intr_func testdrv_delayed_intr;       ///< Delayed interrupt routine

drv_intr_func testdrv_trigger;            ///< Trigger one-shot interrupt
drv_intr_func testdrv_raise;              ///< Raise level-sensitive interrupt
drv_intr_func testdrv_lower;              ///< Lower level-sensitive interrupt

#endif /* _SYS_HV_DRV_TEST_H */
