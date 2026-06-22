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
 * Convenient macros if you are using the hypervisor's default page sizes.
 */

#ifndef _HV_PAGESIZE_H
#define _HV_PAGESIZE_H
#ifndef __DOXYGEN__

/* Define the page sizes in terms of the default page sizes. */
#define HV_LOG2_PAGE_SIZE_SMALL HV_LOG2_DEFAULT_PAGE_SIZE_SMALL
#define HV_PAGE_SIZE_SMALL HV_DEFAULT_PAGE_SIZE_SMALL
#define HV_LOG2_PAGE_SIZE_LARGE HV_LOG2_DEFAULT_PAGE_SIZE_LARGE
#define HV_PAGE_SIZE_LARGE HV_DEFAULT_PAGE_SIZE_LARGE

/* Define the page table structure in terms of the default page sizes. */
#define HV_LOG2_L1_ENTRIES \
  _HV_LOG2_L1_ENTRIES(HV_LOG2_PAGE_SIZE_LARGE)
#define HV_L1_ENTRIES \
  _HV_L1_ENTRIES(HV_LOG2_PAGE_SIZE_LARGE)
#define HV_LOG2_L1_SIZE \
  _HV_LOG2_L1_SIZE(HV_LOG2_PAGE_SIZE_LARGE)
#define HV_L1_SIZE \
  _HV_L1_SIZE(HV_LOG2_PAGE_SIZE_LARGE)
#define HV_LOG2_L2_ENTRIES \
  _HV_LOG2_L2_ENTRIES(HV_LOG2_PAGE_SIZE_LARGE, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_L2_ENTRIES \
  _HV_L2_ENTRIES(HV_LOG2_PAGE_SIZE_LARGE, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_LOG2_L2_SIZE \
  _HV_LOG2_L2_SIZE(HV_LOG2_PAGE_SIZE_LARGE, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_L2_SIZE \
  _HV_L2_SIZE(HV_LOG2_PAGE_SIZE_LARGE, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_L1_INDEX(va) \
  _HV_L1_INDEX(va, HV_LOG2_PAGE_SIZE_LARGE)
#define HV_L2_INDEX(va) \
  _HV_L2_INDEX(va, HV_LOG2_PAGE_SIZE_LARGE, HV_LOG2_PAGE_SIZE_SMALL)

/* Define some conversions to and from the default-sized small page frames. */
#define HV_CPA_TO_PFN(p) _HV_CPA_TO_PFN(p, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_PFN_TO_CPA(p) _HV_PFN_TO_CPA(p, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_PTFN_TO_PFN(p) _HV_PTFN_TO_PFN(p, HV_LOG2_PAGE_SIZE_SMALL)
#define HV_PFN_TO_PTFN(p) _HV_PFN_TO_PTFN(p, HV_LOG2_PAGE_SIZE_SMALL)

/* Define constants for manipulating PFNs in PTEs. */
#define HV_PTE_INDEX_PFN _HV_PTE_INDEX_PFN(HV_LOG2_PAGE_SIZE_SMALL)
#define HV_PTE_INDEX_PFN_BITS _HV_PTE_INDEX_PFN_BITS(HV_LOG2_PAGE_SIZE_SMALL)

#ifndef __ASSEMBLER__

/* Set the PFN directly for the default small page size. */
static __inline unsigned int
hv_pte_get_pfn(const HV_PTE pte)
{
  return pte.val >> HV_PTE_INDEX_PFN;
}

/* Get the PFN directly for the default small page size. */
static __inline HV_PTE
hv_pte_set_pfn(HV_PTE pte, unsigned int val)
{
  /*
   * Note that the use of "PTFN" in the next line is intentional; we
   * don't want any garbage lower bits left in that field.
   */
  pte.val &= ~(((1ULL << HV_PTE_PTFN_BITS) - 1) << HV_PTE_INDEX_PTFN);
  pte.val |= (__hv64) val << HV_PTE_INDEX_PFN;
  return pte;
}

#endif

#endif /* !__DOXYGEN__ */
#endif /* _HV_PAGESIZE_H */
