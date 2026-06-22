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
 * Inter-tile messaging support.
 * @file
 */

#include "messaging.h"
#include "bogux.h"

#include <stdio.h>
#include <assert.h>

#include <arch/spr.h>

/** Log message interrupts received, for test purposes. */
#define INT_MSG_LIST_LEN 8
static HV_IntrMsg int_msg_list[INT_MSG_LIST_LEN] _TILESTATE;
int int_msg_list_head _TILESTATE;
int int_msg_list_tail _TILESTATE;

/** Add a message interrupt to the circular log buffer.
 * @param him Interrupt to add.
 */
void
put_msg_int(HV_IntrMsg* him)
{
  int newtail = (int_msg_list_tail + 1) % INT_MSG_LIST_LEN;
  if (newtail == int_msg_list_head)
    return;

  int_msg_list[int_msg_list_tail] = *him;
  int_msg_list_tail = newtail;
}

/** Retrieve the next message interrupt from the circular log buffer.
 * @param him Destination for the retrieved interrupt.
 * @return Nonzero iff an interrupt was available.
 */
int
get_msg_int(HV_IntrMsg* him)
{
  if (int_msg_list_tail == int_msg_list_head)
    return 0;

  *him = int_msg_list[int_msg_list_head];
  int_msg_list_head = (int_msg_list_head + 1) % INT_MSG_LIST_LEN;
  return 1;
}
