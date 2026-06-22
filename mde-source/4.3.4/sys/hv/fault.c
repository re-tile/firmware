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
 * Data related to handling faults on access to client or user VAs.
 */

#include <setjmp.h>
#include <util.h>

#include "debug.h"
#include "fault.h"
#include "mapping.h"

jmp_buf fault_jmp_buf;

static int fault_expected_var = 0;

int
fault_begin(const void* va, int len)
{
  if (len && c_va_invalid((VA) va, len))
    return (1);

  assert(!fault_expected_var);
  fault_expected_var = 1;

  return (0);
}


void
fault_add_addr(const void* va, int len)
{
  if (len && c_va_invalid((VA) va, len))
    fault_encountered();
}


void
fault_end()
{
  assert(fault_expected_var);
  fault_expected_var = 0;
}


int
fault_expected()
{
  return (fault_expected_var);
}


void
fault_encountered()
{
  fault_end();
  longjmp(fault_jmp_buf, 1);
}
