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
 * Routines to wait for events while napping.
 */

#ifndef _SYS_HV_NAP_H
#define _SYS_HV_NAP_H

#include "misc.h"

/** Nap until a complete message is received on the IDN catch-all; the
 *  message may be either a request or a response.  Normally, the caller
 *  would use this routine by entering an interrupt critical section and
 *  checking for some condition which is affected by incoming messages.
 *  If the condition is not satisified, this routine is called, still within
 *  the interrupt critical section.  During execution of this routine, the
 *  critical section is exited to allow receipt of the IDN CA interrupt.
 *  Once a complete message has been received, this routine will return,
 *  having reentered the critical section.  The caller can then recheck the
 *  condition; if satisifed, it exits the critical section and continues
 *  its processing, and if not satisfied, it can again call this routine.
 *
 * @param cycles_to_wait Cycles to wait, or ULONG_MAX to nap forever.
 * @param msgtype Type of message being waited for
 * @param dest Destination of message being waited for
 * @param caller Caller PC, normally passed as __builtin_return_address(0).
 * @return Zero on success, or HV_EAGAIN if timeout expired.
 */
int nap_idn_ca_msg(unsigned long cycles_to_wait,
                   unsigned long msgtype, unsigned long dest, void* caller);

/** Signal that a complete IDN catch-all message has occurred, and wake
 *  up the interrupted thread of control if appropriate.
 * @param sr Saved registers from the thread of control which was interrupted.
 */
void wake_idn_ca_msg(struct saved_regs* sr);

#endif /* _SYS_HV_NAP_H */
