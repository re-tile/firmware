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
 * Routines to handle faults on access to client or user VAs.
 */

#ifndef _SYS_HV_FAULT_H
#define _SYS_HV_FAULT_H

#include <setjmp.h>

#include "types.h"

/** Saved function context to longjmp back to on a fault. */
extern jmp_buf fault_jmp_buf;


/** This macro is called before starting a sequence of accesses to
 *  client or user VAs, which might fault.  Its value must be checked
 *  to see whether a fault occurred.
 * @return Zero if no fault was seen, nonzero otherwise.
 */
#define FAULT_BEGIN(va, len)  (fault_begin(va, len) ? 1 : setjmp(fault_jmp_buf))


/** Convenience macro for the common case of FAULT_BEGIN().
 */
#define ON_FAULT_RETURN_EFAULT(va, len) \
  if (FAULT_BEGIN(va, len)) return (HV_EFAULT)


/** This macro is called after FAULT_BEGIN() or ON_FAULT_RETURN_FAULT(), and
 *  adds another (va, len) pair to the set of client VAs which might fault.
 *  Unlike FAULT_BEGIN, this does not return a value; instead, if the
 *  passed va/len are illegal, the initial FAULT_BEGIN() will return 1.
 */
#define FAULT_ADD_ADDR(va, len) fault_add_addr(va, len)


/** This macro is called when the sequence of accesses to potentially
 *  faulting VAs has completed.
 */
#define FAULT_END()  fault_end()


/** Prepare to handle potentially-faulting accesses.
 * @param va First potentially-fautling virtual address.
 * @param len Length of buffer which will be checked.
 * @return Nonzero if the specified va and len aren't in the client's legal
 *         address space, else zero.
 */
int fault_begin(const void* va, int len);


/** Add another set of potentially-faulting addresses to a currently active
 *  sequence of accesses.  Unlike fault_begin(), this routine does not
 *  return a value; instead, if the passed va/len are illegal, the initial
 *  FAULT_BEGIN() returns 1.
 * @param va First potentially-fautling virtual address.
 * @param len Length of buffer which will be checked.
 */
void fault_add_addr(const void* va, int len);


/** Stop permitting potentially-faulting accesses.
 */
void fault_end(void);


/** Are faulting accesses expected?
 * @return Nonzero if faulting accesses are expected, zero otherwise.
 */
int fault_expected(void);


/** Handle a fault.  Should only be called by an interrupt handler when
 *  a fault is encountered and fault_expected() returns nonzero.
 */
void fault_encountered(void);

#endif /* _SYS_HV_FAULT_H */
