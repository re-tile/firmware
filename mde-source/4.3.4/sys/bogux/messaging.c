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

#include <stdio.h>
#include <assert.h>

#include <arch/spr.h>

#include "bogux.h"
#include "interrupt.h"
#include "messaging.h"

/** State for the messaging subsystem */
HV_MsgState ts_msgstate _TILESTATE;

void
init_messaging()
{
  int rc = hv_register_message_state(&ts_msgstate);
  if (rc != HV_OK)
    panic("hv_register_message_state returned unexpected error %d", rc);
}

int
hv_message_intr(const char* intname, int intnum)
{
  // Release the interrupt critical section.
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);

  union {
    SMsg_Exec exec;
    HV_IntrMsg him;
    int ints[HV_MAX_MESSAGE_SIZE / sizeof(int)];
  } message;

  int nmsgs = 0;
  HV_RcvMsgInfo rmi;

  while (1)
  {
    rmi = hv_receive_message(ts_msgstate,
                             (HV_VirtAddr)message.ints,
                             sizeof(message.ints));
    if (rmi.msglen <= 0)
      break;

    ++nmsgs;

    if (rmi.source == HV_MSG_TILE)
    {
      SupervisorMessageTag tag = (SupervisorMessageTag) message.ints[0];
      switch (tag)
      {
      case SMSG_EXEC:
        {
          assert(rmi.msglen == sizeof(SMsg_Exec));
          // Since we're idle, we must have closed all open files; need to
          // reopen the standard ones
          open_std_fds();
          int rc = do_execve(&message.exec.data);
          warn("remote exec message failed: errno %d\n", -rc);
          break;
        }

      case SMSG_FLUSH_MAPPINGS:
      case SMSG_UPDATE_CACHING:
        panic("Unimplemented supervisor message %d", tag);
        break;

      default:
        panic("Unknown supervisor message %d", tag);
        break;
      }
    }
    else if (rmi.source == HV_MSG_INTR)
    {
      put_msg_int(&message.him);
    }
  }

  // We shouldn't have gotten downcalled with no messages available.
  assert(nmsgs > 0);

  return INT_HAND_DOWNCALL;
}
