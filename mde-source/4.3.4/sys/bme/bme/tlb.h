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
 *
 * Routines to perform TLB operations.  These routines manage the processor's
 * Translation Lookaside Buffers, which control the translation of virtual
 * to physical addresses.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_TLB_H
#define _SYS_BME_TLB_H

#include <features.h>

#include <hv/hypervisor.h>

#include <bme/tte.h>
#include <bme/types.h>

__BEGIN_DECLS

/** Dump the main processor instruction TLB.
 * @param title Optional title to print in dump header (can be NULL).
 */
void bme_dump_itlb(char* title);

/** Dump the data TLB.
 * @param title Optional title to print in dump header (can be NULL).
 */
void bme_dump_dtlb(char* title);

/** Zero out the main processor instruction TLB.
 * @param clean_wired Iff nonzero, zero out the wired TLB entries.
 */
void bme_clean_itlb(int clean_wired);

/** Zero out the data TLB.
 * @param clean_wired Iff nonzero, zero out the wired TLB entries.
 */
void bme_clean_dtlb(int clean_wired);

/** Translate a physical address, virtual address, and a hypervisor PTE (as
 *  might be obtained via the Linux bme_mem driver) to a tte.
 * @param va Virtual address to use.
 * @param pa Physical address to use.  Note that the PFN contained within the
 *        actual PTE itself is ignored by this routine.
 * @param pte Page table entry to use.
 * @param allow_incoherent If nonzero, allow us to create a mapping that
 *        will not be coherent with the mapping on the original Linux tile
 *        that supplied the PTE.  Normally this will result in an error
 *        return.
 * @param ttep Pointer to the returned tte.
 * @return 0 if the tte was successfully produced; nonzero if there was some
 *        error within the PTE, or if it would produce an incoherent mapping
 *        and allow_incoherent is zero.
 */
int bme_pte2tte(VA va, PA pa, HV_PTE pte, int allow_incoherent, tte_t* ttep);

/** Install a new data TLB entry.
 * @param ttep The DTLB entry to be installed.
 * @param index The index of the entry to be overwritten; or
 *        BME_TTE_INDEX_WIRED to use the next unwired entry, that will then
 *        be wired; or BME_TTE_INDEX_RANDOM to use the index suggested by the
 *        hardware random replacement algorithm.
 * @return The TLB index used on success, or -1 if the requested entry cannot
 *        be used (either it is too large for the TLB, or BME_TTE_INDEX_WIRED
 *        was specified and no more wired entries are available).
 */
int bme_install_dtte(tte_t* ttep, int index);

/** Install the TTE in the next available unwired slot, then wire it. */
#define BME_TTE_INDEX_WIRED -1

/** Install the TTE in the unwired slot suggested by the hardware random
 *  replacement algorithm. */
#define BME_TTE_INDEX_RANDOM -2

/** Remove a TLB entry, and, if it is the last wired entry, unwire it.
 * @return 0 on success, non-zero if the requested entry was wired, but not
 *        the last wired entry; in this case the entry will still be
 *        removed but will not be unwired.
 */
int bme_remove_dtte(int index);

/** Install a new instruction TLB entry.
 * @param ttep The ITLB entry to be installed.
 * @param index The index of the entry to be overwritten; or
 *        BME_TTE_INDEX_WIRED to use the next unwired entry (which will then
 *        be wired); or BME_TTE_INDEX_RANDOM to use the index suggested by the
 *        hardware random replacement algorithm.
 * @return The TLB index used on success, or -1 if the requested entry cannot
 *        be used (either it's too large for the TLB, or BME_TTE_INDEX_WIRED
 *        was specified and no more wired entries are available).
 */
int bme_install_itte(tte_t* ttep, int index);


/** Remove a TLB entry, and, if it is the last wired entry, unwire it.
 * @return 0 on success, non-zero if the requested entry was wired, but not
 *        the last wired entry; in this case the entry will still be
 *        removed but will not be unwired.
 */
int bme_remove_itte(int index);


/**
 * Set a TLB's index register, loading data into the xxx_CURRENT registers.
 *
 * FIXME: This works if the drain is replaced with a nop, but the hardware
 * documentation doesn't tell you what's architecturally required; once it
 * does this may change.
 */
#define BME_LOAD_TLB(tlb, index) \
  do \
  { \
    __insn_mtspr(SPR_ ## tlb ## TLB_INDEX, (index) | (1 << 31)); \
    asm("drain"); \
  } while (0)

__END_DECLS

#endif /* _SYS_BME_TLB_H */

/** @} */
