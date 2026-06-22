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
 * Physical memory access syscalls.
 */
#include <string.h>

#include <arch/spr.h>

#include "sys/libc/include/util.h"

#include "config.h"
#include "client_obj.h"
#include "debug.h"
#include "drvintf.h"
#include "physacc.h"
#include "mapping.h"
#include "tsb.h"


uint64_t
syscall_physaddr_read64(CPA cpa, HV_PTE access)
{
  PA pa;
  uint64_t retval;
  uint_reg_t aar;

  NOISY_SYSCALL_TRACE("physaddr_read64(pa=%#llX, access=%#llX)\n", cpa,
                      hv_pte_val(access));

  // Ensure address is 8-byte aligned and valid
  if ((cpa & 7) || c2r_pa(cpa, sizeof (uint64_t), &pa))
    panic("bogus client PA %#llX in physaddr_read64", cpa);

  if (pte2aar(access, &aar, 1))
    panic("bogus pte %#llX in physaddr_read64", hv_pte_val(access));

  retval = phys_rd64(pa, aar);
  NOISY_SYSCALL_TRACE("physaddr_read64 returns %#llX\n", retval);

  return (retval);
}


void
syscall_physaddr_write64(CPA cpa, HV_PTE access, uint64_t val)
{
  PA pa;
  uint_reg_t aar;

  NOISY_SYSCALL_TRACE("physaddr_write64(pa=%#llX, access=%#llX, "
                      "val=%#llX)\n", cpa, hv_pte_val(access), val);

  // Ensure address is 8-byte aligned and valid
  if ((cpa & 7) || c2r_pa(cpa, sizeof (uint64_t), &pa))
    panic("bogus client PA %#llX in physaddr_write64", cpa);

  if (pte2aar(access, &aar, 1))
    panic("bogus pte %#llX in physaddr_write64", hv_pte_val(access));

  phys_wr64(pa, val, aar);
}

PA
syscall_inquire_realpa(CPA cpa, uint32_t len)
{
  PA pa;

  NOISY_SYSCALL_TRACE("inquire_realpa(pa=%#llX, len=%#X)\n",
                      cpa, len);
  
  if (config.nbmeclients <= 0)
    return -1;

  uint32_t val = drv_cpa2pa(cpa, len, &pa);

  if (val)
    return -1;

  NOISY_SYSCALL_TRACE("inquire_realpa returns %#llX\n", pa);

  return pa;
}
