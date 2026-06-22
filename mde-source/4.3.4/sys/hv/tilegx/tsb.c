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
 * Routines to manage the TSB.
 */

#include <string.h>
#include <arch/interrupts.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "sys/libc/include/util.h"
#include <hv/hypervisor.h>

#include "client.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "downcall.h"
#include "fault.h"
#include "hv.h"
#include "mapping.h"
#include "msg.h"
#include "physacc.h"
#include "tlb.h"
#include "tsb.h"
#include "types.h"


/** Translation storage buffer.  Each section of the TSB needs to be
 *  self-size-aligned; since we have fewer L entries than S entries,
 *  just aligning the S part aligns the L part. */
ctte_t tsb[TSB_ALL_ENTRIES]
  __attribute__((aligned(1 << (TSB_S_IDX_WIDTH + CTTE_SHIFT))));

/** Add an entry to the TSB. */
static void
tte2tsb(tte_t* ttep, size_t index)
{
  tte2ctte(ttep, &tsb[index]);
}

/** Description of a page table. */
struct pt_info
{
  CPA base;        /**< Base client address as passed to syscall. */
  PA real_base;    /**< Base PA. */
  Asid asid;       /**< Associated ASID. */
  uint32_t flags;  /**< Flags. */
  uint_reg_t aar;  /**< AAR for physical access to L0. */
  HV_PTE access;   /**< Access PTE as passed to syscall. */
  const char* tag; /**< Tag to use when displaying info. */
};

/** Current primary page table. */
static struct pt_info current_pt =
{
  .base = HV_CTX_NONE,
  .tag = "current",
};

/** Virtualization page table, if any. */
static struct pt_info virt_pt =
{
  .base = HV_CTX_NONE,
  .tag = "virt",
};

/** Guest page table, if any. */
static struct pt_info guest_pt =
{
  .base = HV_CTX_NONE,
  .tag = "guest",
};

/** Is this the guest page table? */
static inline int
is_guest_pt(struct pt_info *pt)
{
#ifndef GUEST_PL
  return 0;
#else
  return pt == &guest_pt;
#endif
}

/** Do we have a virtualization page table installed? */
int
have_virt_pt(void)
{
#ifndef GUEST_PL
  return 0;
#else
  return (virt_pt.base != HV_CTX_NONE);
#endif
}

/** Are we virtualizing this access? */
static inline int
is_virt_pl(int pl)
{
  return (have_virt_pt() && pl < CLIENT_PL);
}

/** Should we downcall this fault to the guest?
 * Downcalls go to the guest when we are running a virtualization
 * context, unless we are passed the TSB_DOWNCALL_VIRT flag.
 */
static inline int
is_guest_fault(int flags)
{
  return (have_virt_pt() && !(flags & TSB_DOWNCALL_VIRT));
}


/** Shift counts for HV_PTE_SUPER bit in PTE. */
enum
{
  PTE_SHIFT_JUMBO = 0,
  PTE_SHIFT_LARGE = 1,
  PTE_SHIFT_SMALL = 2
};
static int pte_super_shift[3];

/** Current bitmask from set_caching(); bit N is on if there are priority
 *  (small) pages whose PFN is N mod 8.
 */
static uint32_t current_caching_bitmask;

/** ASID of last client to install context with HV_CTX_DIRECTIO set. */
static Asid last_directio_asid;

/** Text strings for TSB_x values */
static char* tlb_names[] =
{
  [TSB_D] = "D",
  [TSB_I] = "I",
  [TSB_V] = "V",
  [TSB_GUEST] = "G",
};

/** Miss interrupt vectors for TSB_x values */
static int miss_vectors[] =
{
  [TSB_D] = INT_DTLB_MISS,
  [TSB_I] = INT_ITLB_MISS,
  [TSB_V] = INT_VPGTABLE_MISS_DWNCL,
  [TSB_GUEST] = INT_VGUEST_FATAL_DWNCL,
};

/** Access violation interrupt vectors for TSB_x values */
static int access_vectors[] =
{
  [TSB_D] = INT_DTLB_ACCESS,
  [TSB_I] = -1,
  [TSB_V] = INT_VPGTABLE_MISS_DWNCL,
  [TSB_GUEST] = INT_VGUEST_FATAL_DWNCL,
};

/** The client ASID which is used for fake physical memory mode. */
#define FAKE_PHYSMEM_ASID   1

/** Reason for the last tsb_fatal or tsb_downcall call. */
static char tsb_fatal_reason[160];

#ifdef DEBUG
/** Set up the fault reason for a subsequent tsb_downcall(). */
#define TSB_DOWNCALL_REASON(...) \
  do { \
    if (unlikely(debug_flags & DEBUG_TSB)) \
    { \
      snprintf(tsb_fatal_reason, sizeof (tsb_fatal_reason), "downcall: " \
               __VA_ARGS__); \
      TSB_TRACE("%s", tsb_fatal_reason); \
    } \
  } while (0)
#else
/** Set up the fault reason for a subsequent tsb_downcall(). */
#define TSB_DOWNCALL_REASON(...)
#endif

/** Set up the fault reason for a subsequent tsb_fatal().
 * Note that we may not use the reason if this is a context where
 * virtualization is running, but it's easiest to just do the snprintf
 * unconditionally anyway rather than sorting out the condition.
 */
#define TSB_FATAL_REASON(...) \
  do { \
    snprintf(tsb_fatal_reason, sizeof (tsb_fatal_reason), "fatal: " \
             __VA_ARGS__); \
  } while (0)

/** Size of the L1 page table given the current page sizes. */
#define HV_L1_SIZE _HV_L1_SIZE(page_shift_large)

/** Index into the L1 page table given the current page sizes. */
#define HV_L1_INDEX(va) _HV_L1_INDEX(va, page_shift_large)

/** Size of the L2 page table given the current page sizes. */
#define HV_L2_SIZE _HV_L2_SIZE(page_shift_large, page_shift_small)

/** Index into the L2 page table given the current page sizes. */
#define HV_L2_INDEX(va) _HV_L2_INDEX(va, page_shift_large, page_shift_small)

/** Result of doing a page-table lookup. */
struct pgtable_result
{
  int result;		/**< Primary result code (PGR_* code below). */
  VA fault_addr;	/**< Fault address. */
  HV_PTE pte;		/**< PTE that was found. */
  PA pte_pa;		/**< PA at which the PTE was found. */
  uint_reg_t pte_aar;	/**< AAR with which to read the PTE. */
  int page_shift;	/**< Log2 of page size. */
  int tsb_index;	/**< Index into tsb to cache result. */
};

/* Enum fields for pgtable_result.result */
#define PGR_OK		 0	/**< Lookup OK. */
#define PGR_MISSING	 1	/**< PTE not present in page table. */
#define PGR_FATAL	 2	/**< Invalid structure of page table. */
#define PGR_VIRT	 4	/**< Errors from the virtualization context. */
#define PGR_VIRT_MISSING (PGR_VIRT | PGR_MISSING)  /**< Virt PTE not present. */
#define PGR_VIRT_FATAL	 (PGR_VIRT | PGR_FATAL)    /**< Bad virt page table. */

/** Forward declaration. */
static struct pgtable_result devirtualize_pte(HV_PTE *pte);

/** Set all of our ASIDs.
 * @param asid ASID to set.
 */
static void
set_asids(int asid)
{
  __insn_mtspr(SPR_D_ASID, asid);
  __insn_mtspr(SPR_I_ASID, asid);
}

/** Reset the given page table to its initialization value.
 * @param pt Page table to reset.
 */
static void
reset_page_table(struct pt_info* pt)
{
  pt->base = HV_CTX_NONE;
  pt->real_base = 0;
  assert(!c2r_asid(FAKE_PHYSMEM_ASID, &pt->asid));
  pt->flags = 0;
  pt->aar = 0;
  pt->access = hv_pte(0);
}

/** Enable fake P=V mode for the client.
 */
void
enable_fake_physmem_mode()
{
  //
  // Because this routine may be called as part of an hv_reexec(), we zero
  // the TSB and TLBs so that we don't hit on old entries.
  //
  init_tsb();
  clean_dtlb(0);
  clean_itlb(0);

  reset_page_table(&current_pt);
  reset_page_table(&virt_pt);
  reset_page_table(&guest_pt);

  set_asids(current_pt.asid);
}


/** Tell the hypervisor which cache colors have priority pages associated
 *  with them.
 * @param bitmask Bit N is on if there are priority pages whose PFNs are N
 *        in their low 3 bits.
 */
void
syscall_set_caching(uint32_t bitmask)
{
#if 0 // FIXME: GX: need to reimplement this for new pinning scheme
  // If the bitmask changed from zero to nonzero, or vice versa, we need
  // to reset the cache bits on our wired pages.  Note that we don't care
  // about the actual value of the mask; since all of our pages are larger
  // than 32k, if any bits are on, our pages need to be black-only.
  if ((bitmask == 0) != (current_caching_bitmask == 0))
  {
    //
    // Rework our fixed TTEs.
    //
    int wired_ent;
    tte_w2_t w2;

    // First, the DTLB.
    wired_ent = __insn_mfspr(SPR_WIRED_DTLB);

    for (int i = 0; i < wired_ent; i++)
    {
      LOAD_TLB(D, i);
      w2.word = __insn_mfspr(SPR_DTLB_CURRENT_2);
      w2.bits.red_evict = (bitmask == 0);
      __insn_mtspr(SPR_DTLB_CURRENT_2, w2.word);
    }

    // Now, the ITLB.
    wired_ent = __insn_mfspr(SPR_WIRED_ITLB);

    for (int i = 0; i < wired_ent; i++)
    {
      LOAD_TLB(I, i);
      w2.word = __insn_mfspr(SPR_ITLB_CURRENT_2);
      w2.bits.red_evict = (bitmask == 0);
      __insn_mtspr(SPR_ITLB_CURRENT_2, w2.word);
    }
  }
  current_caching_bitmask = bitmask;
#endif // XXX
}


/** Convert a client PTE into the AAR value we'd use to access that
 *  memory in physical memory mode.
 * @param pte Page table entry.
 * @param aar_p Pointer to the returned AAR value.
 * @param uc_ok If nonzero, it's okay if the resulting AAR value will
 *   access memory uncacheably.  If zero, it's not okay, and such a PTE
 *   will result in our returning a nonzero value.
 * @return Nonzero if the conversion fails (due to a bad pte), otherwise 0.
 */
int
pte2aar(HV_PTE pte, uint_reg_t* aar_p, int uc_ok)
{
  SPR_AAR_t aar = {{ .physical_memory_mode = 1 }};

  switch (hv_pte_get_mode(pte))
  {
  case HV_PTE_MODE_UNCACHED:
    if (!uc_ok)
      return (1);
    aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE;
    break;

  case HV_PTE_MODE_CACHE_NO_L3:
    aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    aar.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE;

    aar.location_x_or_page_mask = HV_LOTAR_X(my_lotar);
    aar.location_y_or_page_offset = HV_LOTAR_Y(my_lotar);
    break;

  case HV_PTE_MODE_CACHE_TILE_L3:
  {
    Lotar lotar = hv_pte_get_lotar(pte);
    Lotar real_lotar;

    if (c2r_pte_lotar(lotar, &real_lotar))
      return (1);

    if (hv_pte_get_nc(pte))
      aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    else
      aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    aar.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE;
    aar.location_x_or_page_mask = HV_LOTAR_X(real_lotar);
    aar.location_y_or_page_offset = HV_LOTAR_Y(real_lotar);
    break;
  }

  case HV_PTE_MODE_CACHE_HASH_L3:
    if (hv_pte_get_nc(pte))
      aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    else
      aar.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    aar.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_HASH;
    aar.location_x_or_page_mask = 0xF;
    aar.location_y_or_page_offset = 0;
    break;

  default:
    return (1);
  }

  if (hv_pte_get_no_alloc_l1(pte))
    aar.no_l1d_allocation = 1;

  *aar_p = aar.word;
  return (0);
}


/** Install the specified page table into the given page table, and change
 *  the context flags.
 *
 * @param paddr Client physical address of the level-1 page table.
 * @param access PTE from which to get cache parameters for the table.
 * @param asid ASID value to associate with this table.
 * @param flags Flags to associate with this context.
 * @param pt Page table to manipulate.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
static int
install_context(CPA paddr, HV_PTE access, Asid asid, uint32_t flags,
                struct pt_info* pt)
{
  CPA save_paddr;
  HV_PTE save_access;
  Asid real_asid;
  PA real_paddr;
  uint_reg_t aar;
  int flush_all = 0;

  SYSCALL_TRACE("install_context(pa=%#llX, access=%#llX, asid=%#x, "
                "flags=%#x)\n", paddr, hv_pte_val(access), asid, flags);

  if (paddr == HV_CTX_NONE)
  {
    reset_page_table(pt);
    set_asids(pt == &virt_pt ? current_pt.asid : pt->asid);
    return (0);
  }

  // Save arguments for hv_inquire_context().
  save_paddr = paddr;
  save_access = access;

  // Devirtualize if we are installing a guest context.
  if (is_guest_pt(pt))
  {
    access = hv_pte_set_pa(access, paddr);
    struct pgtable_result pgr = devirtualize_pte(&access);
    if (pgr.result != PGR_OK)
    {
      SYSCALL_TRACE("install_context() failed (bad guest CPA/PTE)\n");
      return (HV_EFAULT);
    }
    paddr = hv_pte_get_pa(access);
    access = hv_pte_set_pa(access, 0);
  }

  if (paddr & (HV_PAGE_TABLE_ALIGN - 1))
  {
    SYSCALL_TRACE("install_context() failed (unaligned PA)\n");
    return (HV_EINVAL);
  }
  if (c2r_pa(paddr, HV_L0_SIZE, &real_paddr))
  {
    SYSCALL_TRACE("install_context() failed (bad PA)\n");
    return (HV_EFAULT);
  }
  if (c2r_asid(asid, &real_asid))
  {
    SYSCALL_TRACE("install_context() failed (bad ASID)\n");
    return (HV_EINVAL);
  }
  if (pte2aar(access, &aar, 0))
  {
    SYSCALL_TRACE("install_context() failed (bad access PTE)\n");
    return (HV_EINVAL);
  }

  //
  // Handle requesting a page size other than the default.
  //
  int page_flags = (flags & HV_CTX_PG_SM_MASK);
  int small_size;
  switch (page_flags)
  {
  case 0:
    small_size = HV_DEFAULT_PAGE_SIZE_SMALL;
    break;
  case HV_CTX_PG_SM_16K:
    small_size = 16384;
    break;
  case HV_CTX_PG_SM_64K:
    small_size = 65536;
    break;
// See intvec.S for the other test and more information.
#if HV_CLIENT_SHARED_PAGE_SIZE < 65536
# error Adjust client shared page shift or implement DO_PAGE_EXTENSION
#endif
  default:
    SYSCALL_TRACE("install_context() failed (bad page size flags %#x)\n",
                  page_flags);
    return (HV_EINVAL);
  }

  if (small_size != page_size_small)
  {
    if (small_size > HV_SHARED_PAGE_SIZE)
      panic("internal error: increase HV_SHARED_PAGE_SHIFT?");
    flush_all = 1;
    page_size_small = small_size;
    page_shift_small = __builtin_ctzl(small_size);
    init_tsb();
    clean_dtlb(0);
    clean_itlb(0);
    SPR_DTLB_TSB_FILL_CURRENT_ATTR_t attr = { .word = shared_attr };
    attr.ps = TTE_SHIFT_TO_PS(page_shift_small);
    shared_attr = attr.word;
    unsigned long enabled = __insn_mfspr(SPR_MEM_ERROR_ENABLE);
    if (small_size >= 65536)
      enabled |= (SPR_MEM_ERROR_ENABLE__ILLEGAL_ITLB_ENTRY_MASK |
                  SPR_MEM_ERROR_ENABLE__ILLEGAL_DTLB_ENTRY_MASK);
    else
      enabled &= ~(SPR_MEM_ERROR_ENABLE__ILLEGAL_ITLB_ENTRY_MASK |
                   SPR_MEM_ERROR_ENABLE__ILLEGAL_DTLB_ENTRY_MASK);
    __insn_mtspr(SPR_MEM_ERROR_ENABLE, enabled);
  }

  if (pt->base == HV_CTX_NONE && !flush_all && pt != &virt_pt)
    (void) syscall_flush_asid(FAKE_PHYSMEM_ASID);

  //
  // We need to make sure that any pages of user-accessible client-shared
  // memory are only available to folks with HV_CTX_DIRECTIO set.  We don't
  // want to necessarily flush them when we install some other context,
  // just another context which could potentially access them improperly.
  // So, we save the ASID of the last context with that flag.  If we install
  // another ASID with that flag, or we install the same ASID without
  // that flag, we flush the appropriate TTEs from the last-used ASID.
  //
  int do_shared_flush = 0;
  if (flags & HV_CTX_DIRECTIO)
  {
    if (last_directio_asid != real_asid)
      do_shared_flush = 1;
    last_directio_asid = real_asid;
  }
  else
  {
    if (last_directio_asid == real_asid)
      do_shared_flush = 1;
  }

  if (do_shared_flush && !flush_all)
  {
    //
    // We temporarily change the ASID so we actually get just what we want
    // flushed.  This is OK since we're just going to set it again right
    // afterward.  When we add the multi-tile flush support we might want
    // to make a "flush pages x to y from asid z" routine and call it here.
    // Note that we use flush_pages(), instead of flush_page(), since the
    // shared pages may be smaller than the small page size; flush_pages()
    // will still flush them if they are, but flush_page() won't.
    //
    // FIXME: this could potentially be more efficient if we ensured that
    // the user-accessible pages were contiguous so we could do one flush
    // call.
    //
    pt->asid = last_directio_asid;
    for (int i = 0; i < HV_NUM_CLIENT_SHARED_PAGES; i++)
      if (client_shared_map[i].valid && !client_shared_map[i].superonly)
        syscall_flush_pages(client_shared_client_va_base +
                            (i << page_shift_small),
                            page_size_small, page_size_small);
  }

  pt->base = save_paddr;
  pt->real_base = real_paddr;
  pt->asid = real_asid;
  pt->aar = aar;
  pt->access = save_access;
  pt->flags = flags;

  if (pt != &virt_pt)
    set_asids(real_asid);

  return (0);
}

/*
 * Note that syscall_install_context() is in intvec.S; it twiddles the
 * EX_CONTEXT state so that we return directly to the caller's lr, not to
 * the hypervisor glue, which might be mapped at a VA which this call is
 * invalidating.
 */

/** Install the primary context. */
int
do_install_context(CPA paddr, HV_PTE access, Asid asid, uint32_t flags)
{
  return install_context(paddr, access, asid, flags, &current_pt);
}

/** Install the virtualization context. */
int
syscall_install_virt_context(CPA paddr, HV_PTE access, Asid asid,
                             uint32_t flags)
{
#ifndef GUEST_PL
  SYSCALL_TRACE("install_virt_context() failed (no guest support)\n");
  return (HV_EINVAL);
#else
  return install_context(paddr, access, asid, flags, &virt_pt);
#endif
}

/** Install the guest context. */
int
do_install_guest_context(CPA paddr, HV_PTE access, Asid asid,
                              uint32_t flags)
{
  if (!have_virt_pt())
  {
    SYSCALL_TRACE("install_guest_context() failed (no virt context)\n");
    return (HV_EINVAL);
  }
  return install_context(paddr, access, asid, flags, &guest_pt);
}


/** Return information about the specified context.
 * @param pt Context to inquire about.
 * @return Information about the specified context.
 */
static HV_Context
do_inquire_context(struct pt_info* pt)
{
  HV_Context retval;

  SYSCALL_TRACE("inquire_%s_context()\n", pt->tag);

  if (pt->base == HV_CTX_NONE)
  {
    retval.page_table = HV_CTX_NONE;
    retval.access = hv_pte(0);
    retval.asid = 0;
  }
  else
  {
    retval.page_table = pt->base;
    retval.access = pt->access;
    r2c_asid(pt->asid, &retval.asid);
  }
  retval.flags = pt->flags;

  SYSCALL_TRACE("inquire_%s_context returns pa=%#llX, access=%#llX, "
                "asid=%#x, flags=%#x)\n", pt->tag, retval.page_table,
                hv_pte_val(retval.access), retval.asid, retval.flags);

  return (retval);
}

/** Return information about the primary context.
 * @return Information about the primary context.
 */
HV_Context
syscall_inquire_context(void)
{
  return do_inquire_context(&current_pt);
}

/** Return information about the virtualization context.
 * @return Information about the virtualization context.
 */
HV_Context
syscall_inquire_virt_context(void)
{
  return do_inquire_context(&virt_pt);
}

/** Return information about the guest context.
 * @return Information about the guest context.
 */
HV_Context
syscall_inquire_guest_context(void)
{
  return do_inquire_context(&guest_pt);
}


/** Set the number of pages ganged together by HV_PTE_SUPER at some level.
 * @param level Page table level (0, 1, or 2)
 * @param log2_count Base-2 log of the number of pages to gang together.
 * @return Zero on success, or a hypervisor error code on failure.
 */
int
syscall_set_pte_super_shift(int level, int log2_count)
{
  SYSCALL_TRACE("set_pte_super_shift(%d, %d)\n", level, log2_count);

  // Validate level.
  if (level < 0 || level > 2)
    return (HV_EINVAL);

  // Validate shift yields a power-of-four pagesize.
  if ((log2_count & 1) != 0)
    return (HV_EINVAL);

  // Check that the super page size is smaller than the next page size,
  // and no larger than the largest legal size (normally 64G, but if
  // arranging for contiguous striped CPAs, may be less).
  size_t sizes[4] = {
    (1ULL << pg_shift_max) + 1,
    HV_L1_SPAN,
    page_size_large,
    page_size_small
  };
  if ((sizes[level + 1] << log2_count) >= sizes[level])
    return (HV_EINVAL);

  // Dump the TSB/TLB if we are changing super sizes.
  int oldshift = pte_super_shift[level];
  if (oldshift != 0 && log2_count != oldshift)
    syscall_flush_all(0);

  pte_super_shift[level] = log2_count;

  return (0);
}


/** Validate that a TTE is legal.
 * If we are running with 4KB or 16KB client pages, we suspend
 * hardware validation so that we can tolerate VA-to-PA 16KB-aligned
 * mappings instead of the architecturally specified 64KB-aligned
 * mappings.  See the HV_CTX_PG_* flags for do_install_context() in
 * this file, and set_error_enable() in hw_config.c.
 *
 * @param t pointer to TTE we are validating.
 * @param tlb_type type of TLB entry (TSB_I or TSB_D).
 */
static void
validate_tte(tte_t *t, int tlb_type)
{
  // If this is true, the hardware is validating anyway, so don't bother.
  if (page_size_small >= 65536)
    return;

  // The VA value must be legal (properly sign extended).
  // Note we use "intptr_t" instead of "VA" here so we get sign extension.
  intptr_t va = t->w1.word;
  int sign_bits = CHIP_WORD_SIZE() - CHIP_VA_WIDTH();
  if (((va << sign_bits) >> sign_bits) != va)
    panic("illegal TTE (VA not properly sign-extended: %#lx)", va);

  // Handle uncacheable or MMIO specially.
  switch (t->w0.memory_attribute)
  {
  case SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE:
  case SPR_ITLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_MMIO:
    if (tlb_type == TSB_I)
    {
      // Illegal type for ITLB.
      dump_tte(*t, -1, 1);
      panic("illegal TTE (ITLB with bad memory attribute)");
    }
    else
    {
      // Type requires no VA/PA alignment checking for DTLB.
      return;
    }
  }

  // For 4KB pages the VA and PA must be aligned to 16KB.
  if (t->w0.ps == TTE_PS_4K) {
    PA pa = t->w2.word;
    long mask = ((1 << (PG_SHIFT_16K - PG_SHIFT_4K)) - 1) << PG_SHIFT_4K;
    if ((va & mask) != (pa & mask))
      panic("illegal TTE (VA %#lx misaligned to PA %#llx)", va, pa);
  }
}

/** Return the PA for a PTE, and the PTE itself.
 * The code is passed the base of a page table (any level) and a
 * valid index into the table (in units of PTEs).  If there is a
 * "present" PTE at that location, we return the address and the value;
 * otherwise we first check to see if there is a "present, super" PTE
 * located at the appropriate spot in the page table, and return that.
 * @param base base of the page table
 * @param index index into the page table at that level
 * @param level level of page table (0 is root, 2 is leaf)
 * @param aar how to read the memory to get the PTE
 * @param pte_pa pointer to where to write the PA of the PTE
 * @return the PTE found
 */
static HV_PTE
get_pte(PA base, int index, int level, uint_reg_t aar, PA* pte_pa)
{
  PA pa = base + (index << HV_LOG2_PTE_SIZE);
  HV_PTE pte = hv_pte(phys_rd64(pa, aar));

  // If invalid, check for a "super" PTE, otherwise send page fault to client
  if (!hv_pte_get_present(pte))
  {
    PA mask = -1ULL << pte_super_shift[level];
    PA super_pa = base + ((index & mask) << HV_LOG2_PTE_SIZE);
    HV_PTE super_pte = hv_pte(phys_rd64(super_pa, aar));
    if (hv_pte_get_present(super_pte) && hv_pte_get_super(super_pte) &&
        (level == 2 || hv_pte_get_page(super_pte)))
    {
      pa = super_pa;
      pte = super_pte;
    }
  }

  *pte_pa = pa;
  return pte;
}


/* Look up a virtual address in the specified page table.
 * Note that when called with pt as &virt_pt, the fault_addr is really
 * a guest physical address, not a true virtual address.
 * @param pt Page table to use for lookup.
 * @param fault_addr VA of the faulting reference.
 * @return A pgtable_result holding the result of the lookup.
 */
static struct pgtable_result
read_pgtable(struct pt_info* pt, VA fault_addr)
{
  struct pgtable_result pgr;
  int level;
  int is_guest = is_guest_pt(pt);
  HV_PTE pte;
  CPA page_cpa;
  unsigned long page_size;

  pgr.fault_addr = fault_addr;

  //
  // Get L0 PTE.
  //
  // The supervisor will want to model the address space of an m32 process
  // as being from 0 to 4G, but the actual pointers will be in the range
  // -2G to 2G, and the latter is what the hardware will see.  So we
  // use the actual (sign-extended) faulting address to update the TSB/TLB,
  // and use the truncated address to do lookups in the page table and
  // to pass to the supervisor in downcalls.
  //

  // Truncate the address if the m32 process address is in the range of top 2G.
  if ((pt->flags & HV_CTX_32BIT) && (fault_addr == (int) fault_addr))
    fault_addr = (unsigned int) fault_addr;

  pgr.pte_aar = pt->aar;
  level = 0;
  pte = get_pte(pt->real_base, HV_L0_INDEX(fault_addr), level,
                pgr.pte_aar, &pgr.pte_pa);

  // If invalid, report missing PTE.
  if (!hv_pte_get_present(pte))
  {
    TSB_DOWNCALL_REASON("%s: l0 pte %#llX at %#llX not valid\n",
                        pt->tag, hv_pte_val(pte), pgr.pte_pa);
    pgr.result = PGR_MISSING;
    return pgr;
  }

  if (hv_pte_get_page(pte))
  {
    // It's a jumbo page.
    pgr.page_shift = page_shift_jumbo;
    pgr.tsb_index =
      TSB_J_START + ((fault_addr >> pgr.page_shift) & TSB_J_IDX_MASK);
  }
  else
  {
    if (is_guest)
    {
      struct pgtable_result vpgr = devirtualize_pte(&pte);
      if (vpgr.result != PGR_OK)
        return vpgr;
    }

    // It's a pointer to an L1 PTE...
    PA l1_pa;
    CPA l1_cpa = hv_pte_get_pa(pte);

    // Make sure pointer is a valid address
    if (unlikely(c2r_pa(l1_cpa, HV_L1_SIZE, &l1_pa)))
    {
      TSB_FATAL_REASON("%s: invalid CPA for l1 pt %#llx\n", pt->tag, l1_cpa);
      pgr.result = PGR_FATAL;
      return pgr;
    }

    // Set up for physical access to the L1 PTE
    if (unlikely(pte2aar(pte, &pgr.pte_aar, 0)))
    {
      TSB_FATAL_REASON("%s: can't translate l1 PTE %#llx to AAR\n",
                       pt->tag, hv_pte_val(pte));
      pgr.result = PGR_FATAL;
      return pgr;
    }

    // FIXME: check alignment here!
    // if (unlikely(l1_cpa & (HV_L1_SIZE - 1)))
    // {
    //   TSB_FATAL_REASON(...);
    //   pgr.result = PGR_FATAL;
    //   return pgr;
    // }

    // Get L1 PTE.
    level = 1;
    pte = get_pte(l1_pa, HV_L1_INDEX(fault_addr),
                  level, pgr.pte_aar, &pgr.pte_pa);

    // If invalid, send page fault to client
    if (!hv_pte_get_present(pte))
    {
      TSB_DOWNCALL_REASON("%s: l1 pte %#llX at %#llX not valid\n",
                          pt->tag, hv_pte_val(pte), pgr.pte_pa);
      pgr.result = PGR_MISSING;
      return pgr;
    }

    if (hv_pte_get_page(pte))
    {
      // It's a large page.
      pgr.page_shift = page_shift_large;
      pgr.tsb_index =
        TSB_L_START + ((fault_addr >> pgr.page_shift) & TSB_L_IDX_MASK);
    }
    else
    {
      if (is_guest)
      {
        struct pgtable_result vpgr = devirtualize_pte(&pte);
        if (vpgr.result != PGR_OK)
          return vpgr;
      }
    
      // It's a pointer to an L2 PTE...
      PA l2_pa;
      CPA l2_cpa = hv_pte_get_pa(pte);

      // Make sure pointer is a valid address
      if (unlikely(c2r_pa(l2_cpa, HV_L2_SIZE, &l2_pa)))
      {
        TSB_FATAL_REASON("%s: invalid CPA for l2 pt %#llx\n", pt->tag, l2_cpa);
        pgr.result = PGR_FATAL;
        return pgr;
      }

      // Set up for physical access to the L2 PTE
      if (unlikely(pte2aar(pte, &pgr.pte_aar, 0)))
      {
        TSB_FATAL_REASON("%s: can't translate l1 PTE %#llx to AAR\n",
                         pt->tag, hv_pte_val(pte));
        pgr.result = PGR_FATAL;
        return pgr;
      }

      // FIXME: check alignment here!
      // if (unlikely(l2_cpa & (HV_L2_SIZE - 1)))
      // {
      //   TSB_FATAL_REASON(...);
      //   pgr.result = PGR_FATAL;
      //   return pgr;
      // }

      // Get PTE
      level = 2;
      pte = get_pte(l2_pa, HV_L2_INDEX(fault_addr),
                    level, pgr.pte_aar, &pgr.pte_pa);

      // If invalid, send page fault to client
      if (!hv_pte_get_present(pte))
      {
        TSB_DOWNCALL_REASON("%s: l2 pte %#llX at %#llX not valid\n",
                            pt->tag, hv_pte_val(pte), pgr.pte_pa);
        pgr.result = PGR_MISSING;
        return pgr;
      }

      // It's a small page.
      pgr.page_shift = page_shift_small;
      pgr.tsb_index =
        TSB_S_START + ((fault_addr >> pgr.page_shift) & TSB_S_IDX_MASK);
    }
  }

  // If the super bit is set, scale the page size up appropriately.
  if (hv_pte_get_super(pte))
    pgr.page_shift += pte_super_shift[level];

  // If the guest wants a huge page, synthesize a suitable small page PTE
  // instead, since the host client may be virtualizing with small pages.
  // FIXME: use large pages if the virtualization page table does.
  if (is_guest && pgr.page_shift > page_shift_small)
  {
    PA base_pa = hv_pte_get_pa(pte);
    VA huge_offset = (fault_addr & ((1UL << pgr.page_shift) - 1)) &
      -(1UL << page_shift_small);
    pte = hv_pte_set_pa(pte, base_pa + huge_offset);
    pgr.page_shift = page_shift_small;
    pgr.tsb_index = 
        TSB_S_START + ((fault_addr >> pgr.page_shift) & TSB_S_IDX_MASK);
  }

  if (is_guest)
  {
    struct pgtable_result vpgr = devirtualize_pte(&pte);
    if (vpgr.result != PGR_OK)
      return vpgr;
  }

  page_size = 1UL << pgr.page_shift;
 
  page_cpa = hv_pte_get_pa(pte);

  // If page unaligned then table inconsistent; terminate client
  if (unlikely(page_cpa & (page_size - 1)))
  {
    TSB_FATAL_REASON("%s: unaligned page\n", pt->tag);
    pgr.result = PGR_FATAL;
    return pgr;
  }

  pgr.pte = pte;
  pgr.result = PGR_OK;
  return pgr;
}


/*
 * If we have a PTE read from the current page table, and we have an
 * installed virtualization context, this routine will update the PTE
 * appropriately: set the PA as mapped by the virtualization context,
 * adjust the caching bits (unless the virtualization context was
 * installed with HV_CTX_GUEST_CACHE), and update the RWX access bits.
 * A full pgtable_result is returned so that on failure, we can
 * appropriately inform the client.
 * @param pte A pointer to the PTE to update.
 * @return A pgtable_result holding the result of the virtualization lookup.
 */
static struct pgtable_result
devirtualize_pte(HV_PTE *ptep)
{
  CPA cpa = hv_pte_get_pa(*ptep);
  struct pgtable_result vpgr = read_pgtable(&virt_pt, (VA)cpa);
  if (vpgr.result != PGR_OK)
  {
    vpgr.result |= PGR_VIRT;
    return vpgr;
  }

  /* Replace the PA in the PTE with the one from vpgr.pte. */
  cpa = hv_pte_get_pa(vpgr.pte) | (cpa & RMASK(vpgr.page_shift));
  *ptep = hv_pte_set_pa(*ptep, cpa);

  /* By default, update caching bits from the vpgr.pte. */
  if (!(virt_pt.flags & HV_CTX_GUEST_CACHE))
  {
    uint64_t mask = HV_PTE_NO_ALLOC_L1 | HV_PTE_NO_ALLOC_L2 |
      HV_PTE_CACHED_PRIORITY | HV_PTE_NC |
      (((1 << HV_PTE_MODE_BITS) - 1) << HV_PTE_INDEX_MODE);
    ptep->val &= ~mask;
    ptep->val |= (vpgr.pte.val & mask);

    /* Sanity-check that it's a legal memory mode. */
    switch (hv_pte_get_mode(*ptep))
    {
    case HV_PTE_MODE_UNCACHED:
    case HV_PTE_MODE_CACHE_NO_L3:
    case HV_PTE_MODE_CACHE_TILE_L3:
    case HV_PTE_MODE_CACHE_HASH_L3:
      break;
    default:
      TSB_DOWNCALL_REASON("Illegal cache mode bit in virt page table\n");
      vpgr.result = PGR_VIRT_FATAL;
      return vpgr;
    }
  }

  /*
   * We allow the client to own the RWX bits.  The qemu virtualization tool
   * on Linux doesn't request PROT_EXEC for memory given to the client,
   * and more generally it just seems unnatural to model booting up a
   * guest that doesn't have full access to the memory being given to it.
   */

  return vpgr;
}


/** Handle a TSB miss in fake P=V mode.
 * @param pt Page table to manipulate.
 * @param va VA of the faulting reference.
 * @return A pgtable_result holding the result of the virtualization lookup.
 */
static struct pgtable_result
tsb_miss_fake_physmem(struct pt_info* pt, VA va)
{
  struct pgtable_result pgr;
  Lotar client_lotar;
  uint64_t flags = HV_PTE_PRESENT |
    HV_PTE_READABLE | HV_PTE_WRITABLE | HV_PTE_EXECUTABLE |
    HV_PTE_ACCESSED | HV_PTE_DIRTY;
  int page_shift;

  TSB_TRACE("handling miss in P=V mode\n");

  // We use large pages in P=V mode for real mode, but small pages
  // for P=V for guest mode, since we don't know whether virtualization
  // will be using underlying small pages or huge pages.
  // FIXME: use large pages if the virtualization page table does.
  if (pt == &current_pt)
  {
    page_shift = page_shift_large;
    flags |= HV_PTE_PAGE;
  }
  else
  {
    page_shift = page_shift_small;
  }

  // Calculate base address of the new page.
  pgr.fault_addr = va;
  va &= ~((1UL << page_shift) - 1);

  // Make up a PTE that indicates VA=PA and caches on this core.
  pgr.pte = hv_pte_set_pa(hv_pte(flags), (CPA) va);
  pgr.pte = hv_pte_set_mode(pgr.pte, HV_PTE_MODE_CACHE_TILE_L3);
  (void) r2c_lotar(my_lotar, &client_lotar);
  pgr.pte = hv_pte_set_lotar(pgr.pte, client_lotar);

  // Construct a pgtable_result as if we'd looked it up.
  // We always default to caching on this core as our L3.
  // Note that by setting ACCESSED and DIRTY in pgr.pte it doesn't matter
  // what we set pte_pa to, since we won't use it to update those flags.
  pgr.result = PGR_OK;
  pgr.pte_pa = 0;
  pgr.page_shift = page_shift;
  if (page_shift == page_shift_large)
    pgr.tsb_index = TSB_L_START + ((va >> page_shift) & TSB_L_IDX_MASK);
  else
    pgr.tsb_index = TSB_S_START + ((va >> page_shift) & TSB_S_IDX_MASK);

  if (have_virt_pt())
  {
    struct pgtable_result vpgr = devirtualize_pte(&pgr.pte);
    if (vpgr.result != PGR_OK)
      return vpgr;
  }

  return pgr;
}


/** Handle a TSB miss.
 * @param fault_addr VA of the faulting reference.
 * @param flags Miss flags, extracted with the TSB_MISS_xxx() macros.
 * @param inst_addr VA of the instruction which caused the miss.
 * @param state State pointer for tsb_downcall() and tsb_fatal().
 */
void
tsb_miss(VA fault_addr, uint32_t flags, VA inst_addr,
         struct tsb_downcall_state* state)
{
  struct pgtable_result pgr;
  struct pt_info* pt;
  int tlb_type = TSB_MISS_TYPE(flags);
  int pl = TSB_MISS_PL(flags);
  HV_PTE pte;
  unsigned long page_size;
  unsigned long pte_set_bits = 0;
  CPA page_cpa;
  PA page_pa = 0;

  TSB_TRACE("%sTSB miss:   fault addr %#021lX, inst addr %#021lX, "
            "PL %d, %s\n",
            tlb_names[tlb_type], fault_addr, inst_addr, pl,
            (TSB_MISS_RSN(flags) ? "Wr" : "Rd"));

  //
  // TILE-Gx represents 32-bit unsigned values as 64-bit values, where the
  // top 32 bits are all equal to bit 31 (i.e., the 32-bit value is
  // sign-extended to 64 bits).  This also applies to pointers used by
  // applications compiled in ILP32 mode.
  //
  // The supervisor will want to model the address space of an m32 process
  // as being from 0 to 4G, but the actual pointers will be in the range
  // -2G to 2G, and the latter is what the hardware will see.  So we
  // use the actual (sign-extended) faulting address to update the TSB/TLB,
  // and use the truncated address to do lookups in the page table and
  // to pass to the supervisor in downcalls.
  //

  // Ensure client is allowed to use this virtual address

  if (unlikely(c_va_invalid(fault_addr, 1)))
  {
    if (pl == HV_PL)
    {
      tsb_miss_hv(fault_addr, flags, inst_addr, state);
      return;
    }
    if (fault_addr >= client_shared_client_va_base &&
        fault_addr <= client_shared_client_va_last)
    {
      tsb_miss_shared(fault_addr, flags, inst_addr, state);
      return;
    }
    TSB_DOWNCALL_REASON("invalid virtual address\n");
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  // Pick the appropriate page table.
  pt = is_virt_pl(pl) ? &guest_pt : &current_pt;

  // If we're in fake P=V mode, do the right thing.
  if (unlikely(pt->base == HV_CTX_NONE))
    pgr = tsb_miss_fake_physmem(pt, fault_addr);
  else
    pgr = read_pgtable(pt, fault_addr);

  switch (pgr.result)
  {
  case PGR_OK:
    break;
  case PGR_MISSING:
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_FATAL:
    tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_VIRT_MISSING:
    tsb_downcall(state, TSB_V, TSB_DOWNCALL_VIRT,
                 fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_VIRT_FATAL:
    tsb_fatal(state, tlb_type, TSB_DOWNCALL_VIRT,
              fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  pte = pgr.pte;
  page_cpa = hv_pte_get_pa(pte);
  page_size = 1UL << pgr.page_shift;

  // If page physical address invalid, terminate client.  Note that we do
  // the validity check for MMIO pages below.
  if (unlikely(hv_pte_get_mode(pte) != HV_PTE_MODE_MMIO &&
      c2r_pa(page_cpa, page_size, &page_pa)))
  {
    TSB_FATAL_REASON("invalid CPA for page %#llx\n", page_cpa);
    tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  // Make sure page is consistent with the TLB we missed on
  switch (tlb_type)
  {
  case TSB_D:
    if (!hv_pte_get_readable(pte))
    {
      TSB_DOWNCALL_REASON("page not readable for DTLB\n");
      tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }
    break;
  case TSB_I:
    if (!hv_pte_get_executable(pte))
    {
      TSB_DOWNCALL_REASON("page not executable for ITLB\n");
      tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }
    break;
  }

  // Do other consistency checks on PTE
  if (unlikely(!hv_pte_get_readable(pte) && hv_pte_get_writable(pte)))
  {
    TSB_FATAL_REASON("page writable but not readable\n");
    tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  // If this page isn't accessible to the user, send page fault to client
  if (!hv_pte_get_user(pte) && pl == 0)
  {
    TSB_DOWNCALL_REASON("page not accessible to user\n");
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  // Set up the contents of the new TTE.
  // NOTE: for now we bake in the assumption that we always get ASIDs
  // from the guest_pt if it is installed, i.e. we give the guest the 
  // full range of client ASIDs.  We may want to revisit this.
  VA page_va = ROUND_DN(fault_addr, page_size);
  page_va &= ~(page_size - 1);
  tte_t tte =
  {
    .w0 =
    {{
      .ps = TTE_SHIFT_TO_PS(pgr.page_shift),
      .g = hv_pte_get_global(pte),
      .asid = pt->asid,
      .v = 1,
    }},
    .w1 = { .word = page_va },
    .w2 = { .word = page_pa },
  };

  //
  // If the page is writable, and the dirty bit is set, it must have already
  // been in the TSB at one point and already been promoted from read-only
  // to read-write, so just set the write bit.  If the page is writable,
  // and this access was a write, just go ahead and make it writable and
  // set the dirty bit now.  Otherwise, we leave the write bit clear,
  // even if the page is writable; it'll get set on the first access
  // violation for the page (at which time we'll also set the dirty bit).

  if (hv_pte_get_writable(pte))
  {
    if (hv_pte_get_dirty(pte))
      tte.w0.w = 1;
    else if (TSB_MISS_RSN(flags))
    {
      tte.w0.w = 1;
      pte_set_bits |= HV_PTE_DIRTY;
    }
  }

  switch (hv_pte_get_mode(pte))
  {
  case HV_PTE_MODE_UNCACHED:
    tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE;
    break;

  case HV_PTE_MODE_CACHE_NO_L3:
    tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    tte.w0.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE;

    tte.w0.location_x_or_page_mask = HV_LOTAR_X(my_lotar);
    tte.w0.location_y_or_page_offset = HV_LOTAR_Y(my_lotar);
    break;

  case HV_PTE_MODE_CACHE_TILE_L3:
  {
    Lotar lotar = hv_pte_get_lotar(pte);
    Lotar real_lotar;

    if (unlikely(c2r_pte_lotar(lotar, &real_lotar)))
    {
      TSB_FATAL_REASON("illegal LOTAR %#x\n", lotar);
      tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }

    if (hv_pte_get_nc(pte))
      tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    else
      tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    tte.w0.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_TILE;
    tte.w0.location_x_or_page_mask = HV_LOTAR_X(real_lotar);
    tte.w0.location_y_or_page_offset = HV_LOTAR_Y(real_lotar);
    break;
  }

  case HV_PTE_MODE_CACHE_HASH_L3:
    if (hv_pte_get_nc(pte))
      tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_NONCOHERENT;
    else
      tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_COHERENT;
    tte.w0.cache_home_mapping = SPR_AAR__CACHE_HOME_MAPPING_VAL_HASH;
    tte.w0.location_x_or_page_mask = 0xF;
    tte.w0.location_y_or_page_offset = 0;
    break;

  case HV_PTE_MODE_MMIO:
  {
    Lotar lotar = hv_pte_get_lotar(pte);
    pos_t shimaddr = { .bits.x = HV_LOTAR_X(lotar),
                       .bits.y = HV_LOTAR_Y(lotar) };
    CPA page_cpa = hv_pte_get_pa(pte);

    if (unlikely(!mmio_access_ok(shimaddr, page_cpa, page_size)))
    {
      TSB_FATAL_REASON("invalid MMIO access, page %#llx, shim (%d,%d)\n",
                       page_cpa, shimaddr.bits.x, shimaddr.bits.y);
      tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }

    if (tlb_type != TSB_D)
    {
      TSB_DOWNCALL_REASON("executing MMIO page\n");
      tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }

    tte.w0.memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_MMIO;
    if (unlikely(shimaddr.word == 0))
    {
      //
      // (0,0) is a special case meaning "Use the nearest IPI shim".
      //
      tte.w0.location_x_or_page_mask = my_ipi_pos.bits.x;
      tte.w0.location_y_or_page_offset = my_ipi_pos.bits.y;
    }
    else
    {
      tte.w0.location_x_or_page_mask = HV_LOTAR_X(lotar);
      tte.w0.location_y_or_page_offset = HV_LOTAR_Y(lotar);
    }

    //
    // Note that the PFN installed above was zero, since c2r_pa() was
    // never called, so we have to set it here.
    //
    tte.w2.pfn = PFN(page_cpa);
    break;
  }

  default:
    TSB_FATAL_REASON("illegal caching mode %d: %#llx\n",
                     hv_pte_get_mode(pte), pte.val);
    tsb_fatal(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  if (hv_pte_get_no_alloc_l1(pte))
    tte.w0.no_l1d_allocation = 1;

  if (hv_pte_get_user(pte))
    tte.w0.mpl = USER_PL;
#ifdef GUEST_PL
  else if (is_guest_pt(pt))
    tte.w0.mpl = GUEST_PL;
#endif
  else
    tte.w0.mpl = CLIENT_PL;

  tte.w0.dtlbv = hv_pte_get_readable(pte);
  tte.w0.itlbv = hv_pte_get_executable(pte);

  validate_tte(&tte, tlb_type);
  tte2tsb(&tte, pgr.tsb_index);

#ifdef DEBUG
  if (debug_flags & DEBUG_TSB_VERBOSE)
    dump_tte(tte, pgr.tsb_index, 0);
#endif /* DEBUG */

  if (!hv_pte_get_accessed(pte))
    pte_set_bits |= HV_PTE_ACCESSED;

  if (pte_set_bits)
  {
    // If we need to set the dirty or accessed bits in the PTE, do it here.
    // We do this with cmpexch in case the client has changed the PTE since
    // we read it, but if the cmpexch fails, we don't care; in that case
    // the client can't expect those bits to be valid anyway.
    phys_cmpexch64(pgr.pte_pa, hv_pte_val(pte),
                   hv_pte_val(pte) | pte_set_bits, pgr.pte_aar);
  }

  // Stuff the PTE into the right TLB.  We know the TLB miss handler already
  // calculated a replacement index for us, and stuffed it into the index
  // register, so all we have to do is to store the words into the TLB.

  switch (tlb_type)
  {
  case TSB_D:
    WRITE_TLB(D, tte);
    break;
  case TSB_I:
    WRITE_TLB(I, tte);
    break;
  }
}


/** Nonzero if text is writable. */
static int text_writable = 0;

/** Set text writability.
 * @param writable If nonzero, make text writable; if zero, make it
 *  read-only.
 */
void
text_set_writable(int writable)
{
  text_writable = writable;
  //
  // There might be old mappings to text in the DTLB, so we need to flush;
  // this happens infrequently enough that it's easier to just trash the
  // whole thing rather than look for particular TLB entries.
  //
  clean_dtlb(0);
}


/** Handle a TSB miss for the hypervisor's non-wired VA space.
 * @param fault_addr VA of the faulting reference.
 * @param flags Miss flags, extracted with the TSB_MISS_xxx() macros.
 * @param inst_addr VA of the instruction which caused the miss.
 * @param state State pointer for tsb_downcall() and tsb_fatal().
 */
void
tsb_miss_hv(VA fault_addr, uint32_t flags, VA inst_addr,
            struct tsb_downcall_state* state)
{
  int tlb_type = TSB_MISS_TYPE(flags);
  tte_t tte;
  int tsb_index;

  TSB_TRACE("handling miss for hypervisor\n");

  // Calculate base virtual and physical addresses of the new page, along
  // with the page size; if the requested page isn't in our shared memory
  // area, and isn't our code, fail.

  if (fault_addr >= (VA) _sshared &&
      fault_addr < (VA) _sshared +
                   (HV_NUM_SHARED_PAGES << HV_SHARED_PAGE_SHIFT))
  {
    // Page is in the shared memory area.  Note that vtop() will
    // panic if it can't actually get us the PA, which is fine.

    VA page_va = ROUND_DN(fault_addr, page_size_small);
    PA page_pa = vtop(page_va);

    tte = (tte_t)
    {
      { .word = shared_attr },
      { .word = page_va },
      { .word = page_pa }
    };

    // For shared mappings, we put the entry into the TSB.
    tsb_index = (fault_addr >> page_shift_small) & TSB_S_IDX_MASK;

    tte2tsb(&tte, tsb_index);
  }
  else if (fault_addr >= (VA) _stext &&
           fault_addr < ROUND_UP((VA) _etext, HV_CODE_PAGE_SIZE))
  {
    //
    // Page is our code being mapped as data.  Note that we do not actually
    // update the TSB in this case, just the TLB: this allows us to use a
    // non-standard page size.  The theory is that we should be touching
    // this stuff so infrequently that adding it to the TSB isn't actually
    // much help, and in fact might be bad because of the potential to
    // evict client TSB entries.  Note also that we round up the area
    // covered so that we can access the prototype data area which follows
    // the text, in addition to the code.
    //
    VA page_va = ROUND_DN(fault_addr, HV_CODE_AS_DATA_PAGE_SIZE);
    PA page_pa = (page_va - (VA) _stext) + my_text_pa;

    // Set up the contents of the new TTE.

    tte = (tte_t) {
      .w0 =
      {{
        .ps = TTE_SHIFT_TO_PS(HV_CODE_AS_DATA_PAGE_SHIFT),
        .g = 1,
        .asid = 0,
        .v = 1,
        .w = text_writable != 0,
        .mpl = HV_PL,
        .memory_attribute =
          SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping =
          SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
        .location_y_or_page_offset = HV_LOTAR_Y(my_lotar)
      }},
      .w1 = { .word = page_va },
      .w2 = { .word = page_pa }
    };

    tsb_index = -1;
  }
  else
    panic("out-of-range hypervisor %sTLB miss to %#lX from %#lX",
          tlb_names[tlb_type], fault_addr, inst_addr);

#ifdef DEBUG
  if (debug_flags & DEBUG_TSB_VERBOSE)
    dump_tte(tte, 0, 0);
#endif /* DEBUG */

  validate_tte(&tte, tlb_type);

  // Stuff the PTE into the right TLB.  We know the TLB miss handler already
  // calculated a replacement index for us, and stuffed it into the index
  // register, so all we have to do is to store the words into the TLB.

  switch (tlb_type)
  {
  case TSB_D:
    WRITE_TLB(D, tte);
    break;
  case TSB_I:
    WRITE_TLB(I, tte);
    break;
  }
}


/** Handle a TSB miss for client-shared pages.
 * @param fault_addr VA of the faulting reference.
 * @param flags Miss flags, extracted with the TSB_MISS_xxx() macros.
 * @param inst_addr VA of the instruction which caused the miss.
 * @param state State pointer for tsb_downcall() and tsb_fatal().
 */
void
tsb_miss_shared(VA fault_addr, uint32_t flags, VA inst_addr,
                struct tsb_downcall_state* state)
{
  struct pt_info* pt;
  int tlb_type = TSB_MISS_TYPE(flags);
  int pl = TSB_MISS_PL(flags);
  int page_size_bits;
  VA page_va;

  TSB_TRACE("handling miss on shared page\n");

  client_shared_map_t* smp =
    &client_shared_map[(fault_addr - client_shared_client_va_base) >>
                       HV_CLIENT_SHARED_PAGE_SHIFT];

  if (!smp->valid)
  {
    TSB_DOWNCALL_REASON("shared page not valid\n");
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  pt = is_virt_pl(pl) ? &guest_pt : &current_pt;

  // Shared access is given to PLs less than client PL only if
  // the current_pt was installed with DIRECTIO, and if a virtualization
  // context is in effect and we are PL0, only if the guest_pt
  // included DIRECTIO as well.

  if (!smp->superonly)
  {
    if (!(current_pt.flags & HV_CTX_DIRECTIO) && pl < CLIENT_PL)
    {
      TSB_DOWNCALL_REASON("direct I/O not enabled\n");
      tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }
    if (is_guest_pt(pt) && !(pt->flags & HV_CTX_DIRECTIO) && pl == USER_PL)
    {
      TSB_DOWNCALL_REASON("guest direct I/O not enabled\n");
      tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
      return;
    }
  }

  // Make sure page is consistent with the TLB we missed on
  switch (tlb_type)
  {
  case TSB_D:
    break;
  case TSB_I:
    TSB_DOWNCALL_REASON("shared pages are not executable\n");
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  // If this page isn't accessible to the user, send page fault to client
  if (smp->superonly && pl < CLIENT_PL)
  {
    TSB_DOWNCALL_REASON("page not accessible to user\n");
    tsb_downcall(state, tlb_type, 0, fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  page_size_bits = TTE_SHIFT_TO_PS(HV_CLIENT_SHARED_PAGE_SHIFT);
  page_va = fault_addr & ~(HV_CLIENT_SHARED_PAGE_SIZE - 1);

  // Set up the contents of the new TTE.

  tte_t tte = {
    .w0 =
    {{
      .ps = page_size_bits,
      .g = smp->superonly,
      .asid = pt->asid,
      .v = 1,
      .w = !smp->readonly,
      .mpl = smp->superonly ? CLIENT_PL : USER_PL,
      .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
      .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
      .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
      .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
      .dtlbv = 1,
    }},
    .w1 = { .word = page_va },
    .w2 = { .word = smp->pa }
  };

  validate_tte(&tte, tlb_type);

  int tsb_index;
  if (page_size_small == HV_CLIENT_SHARED_PAGE_SIZE)
  {
    // If the page size allows, cache this entry in the TSB.
    tsb_index = (fault_addr >> page_shift_small) & TSB_S_IDX_MASK;
    tte2tsb(&tte, tsb_index);
  }
  else
  {
    tsb_index = -1;
  }

#ifdef DEBUG
  if (debug_flags & DEBUG_TSB_VERBOSE)
    dump_tte(tte, 0, 0);
#endif /* DEBUG */

  // Stuff the PTE into the DTLB.  We know the TLB miss handler already
  // calculated a replacement index for us, and stuffed it into the index
  // register, so all we have to do is to store the words into the TLB.

  WRITE_TLB(D, tte);
}


/** Handle a TSB access violation.
 * @param fault_addr VA of the faulting reference.
 * @param flags Miss flags, extracted with the TSB_MISS_xxx() macros.
 * @param inst_addr VA of the instruction which caused the fault.
 * @param state State pointer for tsb_downcall() and tsb_fatal().
 */
void
tsb_access(VA fault_addr, uint32_t flags, VA inst_addr,
           struct tsb_downcall_state* state)
{
  struct pgtable_result pgr;
  struct pt_info* pt;
  int tlb_type = TSB_MISS_TYPE(flags);
  int pl = TSB_MISS_PL(flags);
  HV_PTE pte;
  unsigned long page_size;

  // The TSB access violation flow is similar to the TSB miss flow, but
  // we omit a lot of the error checking for performance reasons.
  // We know that if we get an access violation, we have a TTE in the TLB
  // which satisfies all of the validity checks, except that its Writable
  // bit is off.  We thus know that when that TTE was installed, we had a
  // valid PTE in the page table which passed all of our consistency checks.
  //
  // Theoretically, the client could have changed the page table since then.
  // However, we aren't going to repeat any of those checks, except for the
  // ones which prevent the client from violating security policy (e.g.,
  // CPA validity checking).  If the client changed a page and made it
  // writable but not readable, which is an error, we'll merrily note that
  // the writable bit is on in the PTE and set it in the appropriate TTE.
  // This is the client's fault.

  TSB_TRACE("%sTSB accvio: fault addr %#021lX, inst addr %#021lX, PL %d\n",
            tlb_names[tlb_type], fault_addr, inst_addr, pl);

  // Pick the appropriate page table.
  pt = is_virt_pl(pl) ? &guest_pt : &current_pt;

  pgr = read_pgtable(pt, fault_addr);
  page_size = 1UL << pgr.page_shift;
  switch (pgr.result)
  {
  case PGR_OK:
    break;
  case PGR_MISSING:
    tsb_downcall(state, tlb_type, TSB_DOWNCALL_ACCVIO,
                 fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_FATAL:
    tsb_fatal(state, tlb_type, TSB_DOWNCALL_ACCVIO,
              fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_VIRT_MISSING:
    tsb_downcall(state, TSB_V, TSB_DOWNCALL_VIRT,
                 fault_addr, TSB_MISS_RSN(flags));
    return;
  case PGR_VIRT_FATAL:
    tsb_fatal(state, tlb_type, TSB_DOWNCALL_VIRT,
              fault_addr, TSB_MISS_RSN(flags));
    return;
  }

  pte = pgr.pte;

  // See whether the page is actually writable; if not, return the appropriate
  // status.

  if (!hv_pte_get_writable(pte))
  {
    TSB_DOWNCALL_REASON("write to read-only page\n");
    tsb_downcall(state, tlb_type, TSB_DOWNCALL_ACCVIO, fault_addr,
                 TSB_MISS_RSN(flags));
    return;
  }

  // See whether what's in the TSB actually matches this translation; if so,
  // set the writable bit.  (It might not match if the original entry had
  // been evicted after the entry was placed in the TLB.)

  ctte_t* cttep = &tsb[pgr.tsb_index];
  VA page_va = ROUND_DN(fault_addr, page_size);
  page_va &= ~((1UL << pgr.page_shift) - 1);
  if (ctte_get_vpfn(cttep) == VPFN(page_va) &&
      cttep->attr.ps == TTE_SHIFT_TO_PS(pgr.page_shift) &&
      (cttep->attr.g || (cttep->attr.asid == pt->asid)))
    cttep->attr.w = 1;

  // Now find the TTE in the TLB, and set the writable bit there, too.

  if (!search_dtlb_and_set_writable(fault_addr, pt->asid))
  {
    //
    // It should be impossible for the relevant translation to not be
    // in the TLB, so we're going to blow up here.
    //

    panic("can't find VA %#lX in DTLB after access violation", fault_addr);
  }

  // Set the dirty bit on the PTE if needed
  if (!hv_pte_get_dirty(pte))
  {
    // If we need to set the dirty bit in the PTE, do it here.  We do this
    // with cmpexch in case the client has changed the PTE since we read
    // it, but if the cmpexch fails, we don't care; in that case the client
    // can't expect the dirty bit to be valid anyway.
    phys_cmpexch64(pgr.pte_pa, hv_pte_val(pte),
                   hv_pte_val(pte) | (1UL << HV_PTE_INDEX_DIRTY), pgr.pte_aar);
  }
}


/** Number of times we unroll in the tsb[]-walking code.
 * This corresponds to CHIP_L2_LINE_SIZE() / sizeof(ctte_t).
 */
#define TSB_UNROLL_COUNT 4

/** Apply the given op to decide whether to flush cttes in the range [s,e).
 * Implement via unroll (one loop iteration per cache line) and prefetch.
 * We are touching over a thousand cache lines so it's worth optimizing a bit.
 * We prefetch four cache lines ahead just like memcpy().
 * @param s Starting index in tsb[]
 * @param e One past ending index in tsb[]
 * @param op Two-argument macro to test each entry (takes ctte_t*, ctte_attr_t)
 */
#define tsb_unroll_flush_op(s, e, op)                           \
  do                                                            \
  {                                                             \
    ctte_t* _t = &tsb[s];                                       \
    ctte_t* _end = &tsb[e];                                     \
    while (_t < _end)                                           \
    {                                                           \
      __insn_prefetch((char*)_t + (4 * CHIP_L2_LINE_SIZE()));   \
      ctte_t* _t0 = _t++;                                       \
      ctte_t* _t1 = _t++;                                       \
      ctte_t* _t2 = _t++;                                       \
      ctte_t* _t3 = _t++;                                       \
      ctte_attr_t _attr0 = _t0->attr;                           \
      ctte_attr_t _attr1 = _t1->attr;                           \
      ctte_attr_t _attr2 = _t2->attr;                           \
      ctte_attr_t _attr3 = _t3->attr;                           \
      if (op(_t0, _attr0))                                      \
        _t0->attr.word = 0;                                     \
      if (op(_t1, _attr1))                                      \
        _t1->attr.word = 0;                                     \
      if (op(_t2, _attr2))                                      \
        _t2->attr.word = 0;                                     \
      if (op(_t3, _attr3))                                      \
        _t3->attr.word = 0;                                     \
    }                                                           \
  }                                                             \
  while (0)

/** Do a single ctte flush check at the given index. */
#define tsb_flush_op(i, op)                                     \
  do                                                            \
  {                                                             \
    ctte_t* _t = &tsb[i];                                       \
    if (op(_t, _t->attr))                                       \
      _t->attr.word = 0;                                        \
  }                                                             \
  while (0)


/** Return true if a given ctte is currently visible, using "real_asid". */
#define tsb_flush_asid(t, attr) ((attr).asid == real_asid && !(attr).g)

/** Flush all pages from the TSB and TLB which are associated with the
 *  given ASID and not marked with the "global" bit.
 *  Called as a result of a client call to hv_flush_asid().
 * @param asid ASID value to flush.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
int
syscall_flush_asid(Asid asid)
{
  Asid real_asid;
  int i;
  int nent;
  tte_w0_t w0;

  SYSCALL_TRACE("flush_asid(asid=%#x)\n", asid);

  if (c2r_asid(asid, &real_asid))
    return (HV_EINVAL);

  // Flush the TSB
  tsb_unroll_flush_op(0, TSB_ALL_ENTRIES, tsb_flush_asid);
  
  // Flush the DTLB
  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  for (i = __insn_mfspr(SPR_WIRED_DTLB); i < nent; i++)
  {
    LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);

    if (w0.asid == real_asid && !w0.g && w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_DTLB_CURRENT_ATTR, w0.word);
    }
  }

  // Flush the ITLB
  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  for (i = __insn_mfspr(SPR_WIRED_ITLB); i < nent; i++)
  {
    LOAD_TLB(I, i);
    w0.word = __insn_mfspr(SPR_ITLB_CURRENT_ATTR);

    if (w0.asid == real_asid && !w0.g && w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_ITLB_CURRENT_ATTR, w0.word);
    }
  }

  return (0);
}

/** Return non-zero if a given ctte is currently visible to the installed
 * context (using "asid") and of size "page_size_bits" with VPFN between
 * "first_page_vpfn" and "last_page_vpfn".
 */
#define tsb_flush_page_range(t, attr)                           \
  ({                                                            \
    int retval = ((attr).ps == page_size_bits &&                \
                  ((attr).g || (attr).asid == asid));           \
    if (retval)                                                 \
    {                                                           \
      VA vpfn = ctte_get_vpfn(t);                               \
      if (vpfn < first_page_vpfn || vpfn > last_page_vpfn)      \
        retval = 0;                                             \
    }                                                           \
    retval;                                                     \
  })

/** Flush any pages in the given tsb[] range from first to last inclusive
 * that match via tsb_flush_page_range().  Uses tsb_unroll_flush_op()
 * with code to handle a prolog and epilog to the unrolled portion.
 * @param first Index of first entry in TSB to flush
 * @param last Index of last entry in TSB to flush
 * @param first_page_vpfn VPFN of first page in flush range
 * @param last_page_vpfn VPFN of last page in flush range
 * @param page_size_bits TTE encoding for page size of pages to flush
 * @param asid Real ASID to use for flushing pages
 */
static void flush_pages_aux(int first, int last, VA first_page_vpfn,
                            VA last_page_vpfn, uint32_t page_size_bits,
                            Asid asid)
{
  int i = first;
  for (; (i & (TSB_UNROLL_COUNT - 1)) != 0 && i <= last; i++)
    tsb_flush_op(i, tsb_flush_page_range);
  int aligned_end = (last + 1) & -TSB_UNROLL_COUNT;
  tsb_unroll_flush_op(i, aligned_end, tsb_flush_page_range);
  for (i = aligned_end; i <= last; i++)
    tsb_flush_op(i, tsb_flush_page_range);
}


/** Flush all references to the specified pages which are accessible to the
 *  current client from the TSB and TLB.  Called as a result of a client call
 *  to hv_flush_pages().
 * @param va Beginning virtual address to flush.
 * @param page_size Page size of pages to flush.
 * @param size Number of bytes to flush; we flush any page of the specified
 *        size which overlaps the VA range [va, va + size).
 * @param asid Real ASID to use for flushing pages.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
static int
do_flush_pages(VA va, unsigned long page_size, unsigned long size, Asid asid)
{
  uint32_t page_size_bits;
  int page_shift;
  int tsb_base;         // Index of first entry in TSB
  int tsb_end;          // Index of last entry in TSB
  int tsb_entries;      // Number of entries of this page size in TSB
  int tsb_first_index;  // Index of first TSB entry to flush
  int tsb_last_index;   // Index of last TSB entry to flush
  int tsb_idx_mask;     // Mask to apply to shifted VA to get TSB index
  int tsb_page_shift;   // Page shift to use in the TSB
  int i;
  int nent;
  tte_w0_t w0;
  VA first_page_va;
  VA last_page_va;
  VA first_page_vpfn;
  VA last_page_vpfn;

  if (page_size == page_size_jumbo ||
      page_size == (page_size_jumbo << pte_super_shift[PTE_SHIFT_JUMBO]))
  {
    tsb_base = TSB_J_START;
    tsb_entries = TSB_J_ENTRIES;
    tsb_idx_mask = TSB_J_IDX_MASK;
    tsb_page_shift = page_shift_jumbo;
  }
  else if (page_size == page_size_large ||
           page_size == (page_size_large << pte_super_shift[PTE_SHIFT_LARGE]))
  {
    tsb_base = TSB_L_START;
    tsb_entries = TSB_L_ENTRIES;
    tsb_idx_mask = TSB_L_IDX_MASK;
    tsb_page_shift = page_shift_large;
  }
  else if (page_size == page_size_small ||
           page_size == (page_size_small << pte_super_shift[PTE_SHIFT_SMALL]))
  {
    tsb_base = TSB_S_START;
    tsb_entries = TSB_S_ENTRIES;
    tsb_idx_mask = TSB_S_IDX_MASK;
    tsb_page_shift = page_shift_small;
  }
  else
  {
    SYSCALL_TRACE("invalid page size\n");
    return (HV_EINVAL);
  }

  // Compute TSB index and pointer to eventual TTE

  page_shift = __builtin_ctzl(page_size);
  page_size_bits = TTE_SHIFT_TO_PS(page_shift);
  first_page_va = va & ~(page_size - 1);
  last_page_va = ((va + size - 1) & ~(page_size - 1)) + page_size - 1;
  tsb_end = tsb_base + tsb_entries - 1;
  if ((size >> tsb_page_shift) >= tsb_entries)
  {
    tsb_first_index = tsb_base;
    tsb_last_index = tsb_end;
  }
  else
  {
    tsb_first_index = ((first_page_va >> tsb_page_shift) &
                       tsb_idx_mask) + tsb_base;
    tsb_last_index = ((last_page_va >> tsb_page_shift) &
                      tsb_idx_mask) + tsb_base;
  }

  first_page_vpfn = VPFN(first_page_va);
  last_page_vpfn = VPFN(last_page_va);

  if (tsb_first_index > tsb_last_index)
  {
    // We start at point X in the TSB, then wrap around the end and stop
    // at a point before X, so we need two loops.
    flush_pages_aux(tsb_base, tsb_last_index,
                    first_page_vpfn, last_page_vpfn, page_size_bits, asid);
    flush_pages_aux(tsb_first_index, tsb_end,
                    first_page_vpfn, last_page_vpfn, page_size_bits, asid);
  }
  else
  {
    // We have a non-wrapping range in the TSB (perhaps the whole thing).
    flush_pages_aux(tsb_first_index, tsb_last_index,
                    first_page_vpfn, last_page_vpfn, page_size_bits, asid);
  }

  // Flush the DTLB
  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  for (i = __insn_mfspr(SPR_WIRED_DTLB); i < nent; i++)
  {
    LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);
    VA entry_vpfn = VPFN(__insn_mfspr(SPR_DTLB_CURRENT_VA));

    if (entry_vpfn >= first_page_vpfn && entry_vpfn <= last_page_vpfn &&
        w0.ps == page_size_bits && (w0.g || (w0.asid == asid)) &&
        w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_DTLB_CURRENT_ATTR, w0.word);
    }
  }

  // Flush the ITLB
  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  for (i = __insn_mfspr(SPR_WIRED_ITLB); i < nent; i++)
  {
    LOAD_TLB(I, i);
    w0.word = __insn_mfspr(SPR_ITLB_CURRENT_ATTR);
    VA entry_vpfn = VPFN(__insn_mfspr(SPR_ITLB_CURRENT_VA));

    if (entry_vpfn >= first_page_vpfn && entry_vpfn <= last_page_vpfn &&
        w0.ps == page_size_bits && (w0.g || (w0.asid == asid)) &&
        w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_ITLB_CURRENT_ATTR, w0.word);
    }
  }

  return (0);
}


/** Flush all references to the specified page which are accessible to the
 *  current client from the TSB and TLB.  Called as a result of a client call
 *  to hv_flush_page().
 *
 *  FIXME: we use current_pt.asid since with the current workflow it's
 *  pointless to flush with guest ASIDs.  We handle guest hypervisor
 *  calls with swint0, which does a full TLB flush on entering the host
 *  Linux anyway.  A more efficient model would be to allow the guest
 *  Linux to issue direct swint2 calls for TLB flushing, then pass the
 *  ex1 value to the TLB flush routines so that they could know which PL
 *  called them and thus which context to get the ASID from.
 *
 * @param va Virtual address to flush.
 * @param page_size Page size of page to flush.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
static int
do_flush_page(VA va, unsigned long page_size)
{
  int tsb_index;
  uint32_t page_size_bits;
  VA page_vpfn;
  int i;
  int nent;
  tte_w0_t w0;

  // Compute TSB index and pointer to eventual TTE
  if (page_size == page_size_jumbo)
  {
    tsb_index = TSB_J_START + ((va >> page_shift_jumbo) & TSB_J_IDX_MASK);
    page_size_bits = TTE_SHIFT_TO_PS(page_shift_jumbo);
    page_vpfn = VPFN(va & ~((VA) page_size_jumbo - 1));
  }
  else if (page_size == page_size_large)
  {
    tsb_index = TSB_L_START + ((va >> page_shift_large) & TSB_L_IDX_MASK);
    page_size_bits = TTE_SHIFT_TO_PS(page_shift_large);
    page_vpfn = VPFN(va & ~((VA) page_size_large - 1));
  }
  else if (page_size == page_size_small)
  {
    tsb_index = TSB_S_START + ((va >> page_shift_small) & TSB_S_IDX_MASK);
    page_size_bits = TTE_SHIFT_TO_PS(page_shift_small);
    page_vpfn = VPFN(va & ~((VA) page_size_small - 1));
  }
  else
  {
    // Defer to do_flush_pages() for HV_PTE_SUPER checking.
    return do_flush_pages(va, page_size, page_size, current_pt.asid);
  }

  ctte_t* cttep = &tsb[tsb_index];

  // See whether what's in the TSB actually matches this translation; if so,
  // invalidate it.
  if (ctte_get_vpfn(cttep) == page_vpfn &&
      cttep->attr.ps == page_size_bits &&
      (cttep->attr.g || (cttep->attr.asid == current_pt.asid)))
    cttep->attr.word = 0;

  // Flush the DTLB
  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  for (i = __insn_mfspr(SPR_WIRED_DTLB); i < nent; i++)
  {
    LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);
    VA entry_vpfn = VPFN(__insn_mfspr(SPR_DTLB_CURRENT_VA));

    if (entry_vpfn == page_vpfn &&
        w0.ps == page_size_bits &&
        (w0.g || (w0.asid == current_pt.asid)) &&
        w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_DTLB_CURRENT_ATTR, w0.word);
    }
  }

  // Flush the ITLB
  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  for (i = __insn_mfspr(SPR_WIRED_ITLB); i < nent; i++)
  {
    LOAD_TLB(I, i);
    w0.word = __insn_mfspr(SPR_ITLB_CURRENT_ATTR);
    VA entry_vpfn = VPFN(__insn_mfspr(SPR_ITLB_CURRENT_VA));

    if (entry_vpfn == page_vpfn &&
        w0.ps == page_size_bits &&
        (w0.g || (w0.asid == current_pt.asid)) &&
        w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_ITLB_CURRENT_ATTR, w0.word);
    }
  }

  return (0);
}

/** Flush all references to the specified page which are accessible to the
 *  current client from the TSB and TLB.  Called as a result of a client call
 *  to hv_flush_page().
 *
 * @param va Virtual address to flush.
 * @param page_size Page size of page to flush.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
int
syscall_flush_page(VA va, unsigned long page_size)
{
  SYSCALL_TRACE("flush_page(va=%#lX, ps=%lx)\n", va, page_size);

  if (c_va_invalid(va, 1))
  {
    SYSCALL_TRACE("invalid va/size\n");
    return (HV_EFAULT);
  }

  int rc = do_flush_page(va, page_size);
  if (rc)
    return rc;

  if (current_pt.flags & HV_CTX_32BIT)
  {
    if (va >= 0x80000000UL && va + page_size - 1 <= 0xFFFFFFFFUL)
    {
      va -= 0x100000000UL;
      rc = do_flush_page(va, page_size);
      if (rc)
        return rc;
    }
  }

  return (0);
}

/** Flush all references to the specified pages with the specified ASID
 *  from the TSB and TLB.
 * @param va Beginning virtual address to flush.
 * @param page_size Page size of pages to flush.
 * @param size Number of bytes to flush; we flush any page of the specified
 *        size which overlaps the VA range [va, va + size).
 * @param asid Real ASID to use for flush.
 * @param is_m32 Non-zero to use -m32 flush semantics.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
static int
flush_pages_asid(VA va, unsigned long page_size, unsigned long size,
                 Asid asid, int is_m32)
{
  int rc = do_flush_pages(va, page_size, size, asid);
  if (rc)
    return rc;

  if (is_m32)
  {
    if (va >= 0x80000000UL && va + size - 1 <= 0xFFFFFFFFUL)
    {
      va -= 0x100000000UL;
      rc = do_flush_pages(va, page_size, size, asid);
      if (rc)
        return rc;
    }
    else if (va + size > 0x80000000UL)
    {
      size -= 0x80000000UL - va;
      va = 0xFFFFFFFF80000000UL;
      rc = do_flush_pages(va, page_size, size, asid);
      if (rc)
        return rc;
    }
  }

  return (0);
}

/** Flush all references to the specified pages which are accessible to the
 *  current client from the TSB and TLB.  Called as a result of a client call
 *  to hv_flush_pages().
 * @param va Beginning virtual address to flush.
 * @param page_size Page size of pages to flush.
 * @param size Number of bytes to flush; we flush any page of the specified
 *        size which overlaps the VA range [va, va + size).
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
int
syscall_flush_pages(VA va, unsigned long page_size, unsigned long size)
{
  SYSCALL_TRACE("flush_pages(va=%#lX, ps=%#lX, sz=%#lX)\n", va, page_size,
    size);

  if (c_va_invalid(va, size))
  {
    SYSCALL_TRACE("invalid va/size\n");
    return (HV_EFAULT);
  }

  return flush_pages_asid(va, page_size, size, current_pt.asid,
                          current_pt.flags & HV_CTX_32BIT);
}

/** Return true if a given ctte is not global. */
#define tsb_flush_non_global(t, attr) (!(attr).g)

/** Flush all pages (or all non-global pages) from the TSB and TLB.
 *  Called as a result of a client call to hv_flush_all().
 * @param preserve_global Non-zero if we want to preserve "global" mappings.
 * @return An error code if the client has made a fatal error, 0 otherwise.
 */
int
syscall_flush_all(int preserve_global)
{
  int i;
  int nent;
  tte_w0_t w0;

  SYSCALL_TRACE("flush_all(preserve_global=%d)\n", preserve_global);

  if (!preserve_global)
  {
    memset(&tsb, 0, sizeof (tsb));
    clean_dtlb(0);
    clean_itlb(0);
    return 0;
  }

  // Flush the TSB.
  tsb_unroll_flush_op(0, TSB_ALL_ENTRIES, tsb_flush_non_global);

  // Flush the DTLB
  nent = __insn_mfspr(SPR_NUMBER_DTLB);

  for (i = __insn_mfspr(SPR_WIRED_DTLB); i < nent; i++)
  {
    LOAD_TLB(D, i);
    w0.word = __insn_mfspr(SPR_DTLB_CURRENT_ATTR);

    if (!w0.g && w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_DTLB_CURRENT_ATTR, w0.word);
    }
  }

  // Flush the ITLB
  nent = __insn_mfspr(SPR_NUMBER_ITLB);

  for (i = __insn_mfspr(SPR_WIRED_ITLB); i < nent; i++)
  {
    LOAD_TLB(I, i);
    w0.word = __insn_mfspr(SPR_ITLB_CURRENT_ATTR);

    if (!w0.g && w0.v)
    {
      w0.v = 0;
      __insn_mtspr(SPR_ITLB_CURRENT_ATTR, w0.word);
    }
  }

  return (0);
}


// This stuff is really private to syscall_flush_remote() but is outside of
// that routine to make doxygen stop complaining about the \#defines.
/** Tile flush information structure */
struct
{
  uint32_t channel;  /**< Channel we sent message to this tile on */
  long retval;       /**< Return value we got from this tile */
  Asid asid;         /**< ASID we sent to this tile */
  uint8_t flags;     /**< Flags for this tile (TILE_INFO_DO_xxx) */
}
tile_info[1 << HV_XBITS][1 << HV_YBITS];

#define TILE_INFO_DO_CACHE 0x1  /**< Do a cache flush. */
#define TILE_INFO_DO_TLB   0x2  /**< Flush the TLB. */
#define TILE_INFO_DO_ASID  0x4  /**< Flush the specified ASID. */


/** Flush cache and/or TLB state on remote tiles.
 *
 * @param cache_pa Client physical address to flush from cache (ignored if
 *        the length encoded in cache_control is zero, or if
 *        HV_FLUSH_EVICT_L2 is set, or if cache_cpumask is NULL).
 * @param cache_control This argument allows you to specify a length of
 *        physical address space to flush (maximum HV_FLUSH_MAX_CACHE_LEN).
 *        You can "or" in HV_FLUSH_EVICT_L2 to flush the whole L2 cache.
 *        You can "or" in HV_FLUSH_EVICT_LI1 to flush the whole LII cache.
 * @param cache_cpumask Bitmask (in row-major order, supervisor-relative) of
 *        tile indices to perform cache flush on.  The low bit of the first
 *        word corresponds to the tile at the upper left-hand corner of the
 *        supervisor's rectangle.  If passed as a NULL pointer, equivalent
 *        to an empty bitmask.  On chips which support hash-for-home caching,
 *        if passed as -1, equivalent to a mask containing tiles which could
 *        be doing hash-for-home caching.
 * @param tlb_va Virtual address to flush from TLB (ignored if
 *        tlb_length is zero or tlb_cpumask is NULL).
 * @param tlb_length Number of bytes of data to flush from the TLB.
 * @param tlb_pgsize Page size to use for TLB flushes.
 *        tlb_va and tlb_length need not be aligned to this size.
 * @param tlb_cpumask Bitmask for tlb flush, like cache_cpumask.
 *        If passed as a NULL pointer, equivalent to an empty bitmask.
 * @param asids Pointer to an HV_Remote_ASID array of tile/ASID pairs to flush.
 * @param asidcount Number of HV_Remote_ASID entries in asids[].
 * @return Zero for success, or else HV_EINVAL or HV_EFAULT for errors that
 *        are detected while parsing the arguments.
 */
int
syscall_flush_remote(HV_PhysAddr cache_pa, unsigned long cache_control,
                     unsigned long* cache_cpumask,
                     HV_VirtAddr tlb_va, unsigned long tlb_length,
                     unsigned long tlb_pgsize, unsigned long* tlb_cpumask,
                     HV_Remote_ASID* asids, int asidcount)
{
  int retval = 0;

  SYSCALL_TRACE("flush_remote(cache_pa=%#llX, cache_control=%lu, "
                "cache_cpumask=%p, tlb_va=%#llX, tlb_length=%lu, pgsize=%ld, "
                "tlb_cpumask=%p, asids=%p, asidcount=%d)\n",
                cache_pa, cache_control, cache_cpumask, tlb_va, tlb_length,
                tlb_pgsize, tlb_cpumask, asids, asidcount);

  //
  // Copy in parameters.
  //
  uint32_t base_x, base_y, width, height;
  client_tile_mask client_mask;
  get_client_flushinfo(&base_x, &base_y, &width, &height, &client_mask);
  int num_client_tiles = width * height;

  int cpumask_wds = (num_client_tiles + NBPW - 1) / NBPW;
  client_tile_mask tlbmask;
  client_tile_mask cachemask;
  PA real_pa = 0;

  if (tlb_length == 0)
    tlb_cpumask = NULL;
  if (tlb_cpumask)
  {
    ON_FAULT_RETURN_EFAULT(tlb_cpumask, cpumask_wds * sizeof (long));
    memcpy(&tlbmask, tlb_cpumask, cpumask_wds * sizeof (long));
    FAULT_END();
  }
  else
  {
    memset(&tlbmask, 0, sizeof (tlbmask));
  }

  if (cache_control == 0)
    cache_cpumask = NULL;

  if (cache_cpumask == (unsigned long *) -1)
  {
    get_client_home_mask(&cachemask);
  }
  else
  if (cache_cpumask)
  {
    ON_FAULT_RETURN_EFAULT(cache_cpumask, cpumask_wds * sizeof (long));
    memcpy(&cachemask, cache_cpumask, cpumask_wds * sizeof (long));
    FAULT_END();
  }
  else
  {
    memset(&cachemask, 0, sizeof (cachemask));
  }

  unsigned long cache_length = cache_control & HV_FLUSH_MAX_CACHE_LEN;
  if (cache_cpumask && cache_length && !(cache_control & HV_FLUSH_EVICT_L2) &&
      c2r_pa(cache_pa, cache_length, &real_pa))
    return (HV_EFAULT);

  if (asidcount > num_client_tiles || asidcount < 0)
    return (HV_EINVAL);

  HV_Remote_ASID asids_copy[HV_TILES];

  if (asidcount > 0)
  {
    ON_FAULT_RETURN_EFAULT(asids, asidcount * sizeof (HV_Remote_ASID));
    memcpy(asids_copy, asids, asidcount * sizeof (HV_Remote_ASID));
    FAULT_END();
  }

  //
  // Clip the masks to the actual legal client tiles.
  //
  int tlb_pgsize_zero_ok = !(cache_control & HV_FLUSH_ASID_VA_RANGE);
  for (int i = 0;
       i < (sizeof (client_mask.mask) / sizeof (client_mask.mask[0]));
       i++)
  {
    tlbmask.mask[i] &= client_mask.mask[i];
    if (tlbmask.mask[i])
      tlb_pgsize_zero_ok = 0;
    cachemask.mask[i] &= client_mask.mask[i];
  }

  //
  // Validate tlb_pgsize.  It can be zero if we're not using tlbmask,
  // but if we are it must be a valid page size.
  //
  if (!(tlb_pgsize == 0 && tlb_pgsize_zero_ok) &&
      tlb_pgsize != page_size_jumbo && tlb_pgsize != page_size_large &&
      tlb_pgsize != page_size_small &&
      tlb_pgsize != (page_size_jumbo << pte_super_shift[PTE_SHIFT_JUMBO]) &&
      tlb_pgsize != (page_size_large << pte_super_shift[PTE_SHIFT_LARGE]) &&
      tlb_pgsize != (page_size_small << pte_super_shift[PTE_SHIFT_SMALL]))
    return (HV_EINVAL);

  //
  // Validate tlb_va and tlb_length.
  //
  if (!tlb_pgsize_zero_ok && c_va_invalid(tlb_va, tlb_length))
    return (HV_EFAULT);

  //
  // First create a list of tiles and what we'll ask them to do.
  //
  memset(tile_info, 0, sizeof (tile_info));

  unsigned long mask = 1;
  int wordidx = 0;

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      if (tlbmask.mask[wordidx] & mask)
        tile_info[x][y].flags |= TILE_INFO_DO_TLB;

      if (cachemask.mask[wordidx] & mask)
        tile_info[x][y].flags |= TILE_INFO_DO_CACHE;

      mask <<= 1;
      if (mask == 0)
      {
        mask = 1;
        wordidx++;
      }
    }
  }

  for (int i = 0; i < asidcount; i++)
  {
    Asid real_asid;
    int x = asids_copy[i].x;
    int y = asids_copy[i].y;

    if (x < 0 || x >= width || y < 0 || y >= height ||
        c2r_asid(asids_copy[i].asid, &real_asid))
      return (HV_EINVAL);

    tile_info[x][y].asid = asids_copy[i].asid;
    tile_info[x][y].flags |= TILE_INFO_DO_ASID;
  }

  //
  // Go through our list and send all of the messages.
  //
  struct hv_msg_flush_remote msg =
  {
    .cache_pa = real_pa,
    .cache_control = cache_control,
    .tlb_va = tlb_va,
    .tlb_len = tlb_length,
    .page_shift = __builtin_ctzl(tlb_pgsize),
  };

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      if (!tile_info[x][y].flags)
        continue;

      msg.flush_tlb = (tile_info[x][y].flags & TILE_INFO_DO_TLB) != 0;
      msg.flush_cache = (tile_info[x][y].flags & TILE_INFO_DO_CACHE) != 0;
      msg.flush_asid = (tile_info[x][y].flags & TILE_INFO_DO_ASID) != 0;
      msg.asid = tile_info[x][y].asid;

      pos_t dest = { .bits.x = x + base_x, .bits.y = y + base_y };
      if (dest.word != my_pos.word)
      {
        send_var(dest, HV_TAG_FLUSH_REMOTE, &msg, sizeof (msg), NULL, 0,
                 &tile_info[x][y].channel, &tile_info[x][y].retval,
                 sizeof (tile_info[x][y].retval), 0);
      }
      else
      {
        handle_flush_remote(real_pa, cache_control, tlb_va, tlb_length,
                            msg.asid, msg.flush_tlb, msg.page_shift,
                            msg.flush_cache, msg.flush_asid);
        // We don't want to wait for ourselves below, so mark this info
        // struct as unused
        tile_info[x][y].flags = 0;
      }
    }
  }

  //
  // Now go through again and wait for each message.
  //
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      if (!tile_info[x][y].flags)
        continue;

      size_t rcv_replylen;

      uint32_t rcv_type = getreply(tile_info[x][y].channel, &rcv_replylen, 0);

      if (rcv_type != HV_TAG_FLUSH_REMOTE)
        panic("message type mismatch: sent %#x, got %#x", HV_TAG_FLUSH_REMOTE,
              rcv_type);

      if (rcv_replylen != sizeof (tile_info[x][y].retval))
        panic("message length error for HV_TAG_FLUSH_REMOTE reply");

      if (tile_info[x][y].retval && !retval)
        retval = tile_info[x][y].retval;
    }
  }

  return (retval);
}


/** Invalidate the entire data cache (both the L1 and L2).
 */
int
inv_whole_l2()
{
  //
  // What we want to do here is to just read in an L2-cache-size's worth of
  // data in order to evict other stuff out of the cache.  However, in
  // order to make sure we flush the whole cache, without relying on the
  // cache LRU bits (which are a performance optimization but aren't
  // necessarily functionally verified), we read in an L2 way's worth of
  // data at a time, forcing each way in turn to evict the data, using the
  // CACHE_PINNED_WAYS SPR.  In order to flush the L1 at the same time
  // without doing a lot of extra work, we're also going to use different
  // offsets within the L2 lines during various phases of the flush.  All
  // of this means that this will have to be re-done when the cache
  // organization is changed.
  //
  // The choice of which data to read is important here.  Obviously we
  // can't use the client's memory, since that might be what the client is
  // trying to flush.  It turns out, though, that we can't just use random
  // hypervisor memory, either.  If we were to pick something that happened
  // to already be in the L1, then when reading it, we'd hit in the L1, and
  // thus not evict whatever was in the L2.  (Similarly, we could hit in
  // one way of the L2 and not evict what we were trying to evict in the
  // other way.)  So, to make sure this doesn't happen, we reserve an area
  // of memory just for this purpose.  We actually alternate between two
  // separate L2-sized regions; that way we know that we won't hit
  // something that was left in the L1 from the last time we called this
  // routine.
  //

  static VA flush_offset = 0;

#if CHIP_L2_LOG_LINE_SIZE() < CHIP_L1D_LOG_LINE_SIZE()
#error "L2 cache line smaller than L1D line; rework this routine"
#endif

#if CHIP_L2_CACHE_SIZE() < CHIP_L1D_CACHE_SIZE()
#error "L2 cache smaller than L1D; rework this routine"
#endif

  flush_offset ^= CHIP_L2_CACHE_SIZE();

  // What address will we load from?
  VA base_flush_va = HV_FLUSH_VA + flush_offset;

  // Notify the --grind-coherence mechanism of what we are doing.
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_GRINDER_CLEAR);

  //
  // We need to temporarily stuff a new entry into the TLB to do our
  // flushing.  We pick the first unwired entry, and wire it down
  // to be safe.  We don't restore it when we are done, since it's
  // more robust to require that it be faulted back in from the TSB,
  // page table, etc., in case there was some kind of asynchronous
  // update to those sources during the flush.  However, we do save
  // and restore CACHE_PINNED_WAYS.
  //
  int wired_idx = __insn_mfspr(SPR_WIRED_DTLB);
  __insn_mtspr(SPR_WIRED_DTLB, wired_idx + 1);
  
  tte_t tte = {
    .w0 = 
    {{
      .ps = TTE_SHIFT_TO_PS(HV_FLUSH_PAGE_SHIFT),
      .g = 1,
      .asid = 0,
      .v = 1,
      .w = 1,
      .pin = 1,
      .mpl = HV_PL,
      .memory_attribute = SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
      .cache_home_mapping = SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
      .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
      .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
    }},
    .w1 = { .word = HV_FLUSH_VA },
    .w2 = { .word = l2_flush_pa }
  };

  validate_tte(&tte, TSB_D);
  WRITE_TLB_AT(D, wired_idx, tte);




  int old_cpw = __insn_mfspr(SPR_CACHE_PINNED_WAYS);

  int old_dstream_pf = __insn_mfspr(SPR_DSTREAM_PF);
  __insn_mtspr(SPR_DSTREAM_PF, 0);
  __insn_mf();
  VA flush_va = base_flush_va;

  for (int way = 0; way < CHIP_L2_ASSOC(); way++)
  {
    // Select which way to clear
    __insn_mf();




    SPR_CACHE_PINNED_WAYS_t new_cpw = {{ .mp_enable = 1 << way }};
    __insn_mtspr(SPR_CACHE_PINNED_WAYS, new_cpw.word);


    //
    // We use wh64 to create a new dirty line in the cache (evicting
    // whatever was there before), then load from that line to evict a line
    // in the L1 (installing a line with garbage data).  We expect to evict
    // all of the lines in the L1 as we make our way through the entire L2,
    // since the L2 is larger than the L1; this relies on the L1's LRU
    // algorithm working.
    //
    for (int i = 0; i < L2_WAY_SIZE/64; ++i, flush_va += 64)
    {
      __insn_wh64((void*) flush_va);
      *(volatile char*) flush_va;
    }
  }

  //
  // We now invalidate all of the lines we wh64'ed above, to flush
  // them out of the L1 and L2.
  //
  flush_va = base_flush_va;
  for (int i = 0; i < CHIP_L2_CACHE_SIZE()/64; ++i, flush_va += 64)
    __insn_inv((void*) flush_va);

  // Wait for the loads to finish and evict all the old data, and then wait
  // for the old data to reach memory.
  mf_incoherent();




  __insn_mtspr(SPR_CACHE_PINNED_WAYS, old_cpw);

  __insn_mtspr(SPR_DSTREAM_PF, old_dstream_pf);
  __insn_mtspr(SPR_WIRED_DTLB, wired_idx);

  return (0);
}


/** Invalidate part of the L2 cache.
 * @param pa Physical address of the first byte to invalidate.
 * @param len Number of bytes to invalidate.
 */
static int
inv_partial_l2(PA pa, unsigned long len)
{
  //
  // We need to temporarily stuff a new entry into the TLB to do our
  // flushing.  We pick the first unwired entry, and wire it down
  // to be safe.  We don't restore it when we are done, since it's
  // more robust to require that it be faulted back in from the TSB,
  // page table, etc., in case there was some kind of asynchronous
  // update to those sources during the flush.  Finally, we disable the
  // D$ prefetcher while we're flushing.
  //
  int wired_idx = __insn_mfspr(SPR_WIRED_DTLB);
  __insn_mtspr(SPR_WIRED_DTLB, wired_idx + 1);
  int old_dstream_pf = __insn_mfspr(SPR_DSTREAM_PF);
  __insn_mtspr(SPR_DSTREAM_PF, 0);
  __insn_mf();

  while (len > 0)
  {
    PA page_pa = ROUND_DN(pa, HV_FLUSH_PAGE_SIZE);
    PA offset = pa - page_pa;
    PA bytes2flush = (page_pa + HV_FLUSH_PAGE_SIZE) - pa;
    if (bytes2flush > len)
      bytes2flush = len;

    tte_t tte = {
      .w0 =
      {{
        .ps = TTE_SHIFT_TO_PS(HV_FLUSH_PAGE_SHIFT),
        .g = 1,
        .asid = 0,
        .v = 1,
        .w = 1,
        .mpl = HV_PL,
        .memory_attribute =
          SPR_DTLB_CURRENT_ATTR__MEMORY_ATTRIBUTE_VAL_COHERENT,
        .cache_home_mapping =
          SPR_DTLB_CURRENT_ATTR__CACHE_HOME_MAPPING_VAL_TILE,
        .location_x_or_page_mask = HV_LOTAR_X(my_lotar),
        .location_y_or_page_offset = HV_LOTAR_Y(my_lotar),
      }},
      .w1 = { .word = HV_FLUSH_VA },
      .w2 = { .word = page_pa }
    };

    validate_tte(&tte, TSB_D);
    WRITE_TLB_AT(D, wired_idx, tte);

    finv_range(HV_FLUSH_VA + offset, bytes2flush);

    len -= bytes2flush;
    pa += bytes2flush;
  }

  //
  // Restore the old TLB and prefetcher state.
  //
  __insn_mtspr(SPR_WIRED_DTLB, wired_idx);
  __insn_mtspr(SPR_DSTREAM_PF, old_dstream_pf);

  return (0);
}


/** Process a flush_remote message.
 * @param cache_pa Physical address to flush.
 * @param cache_control Size of cache to flush and/or HV_FLUSH_xxx flags.
 * @param tlb_va Virtual address to flush.
 * @param tlb_len Number of TLB bytes to flush.
 * @param asid ASID to flush.
 * @param flush_tlb If true, flush the TLB.
 * @param page_shift Log2 of page size for TLB flush.
 * @param flush_cache If true, flush the cache.
 * @param flush_asid If true, flush the given ASID.
 * @return Zero on success, or an error code.
 */
int
handle_flush_remote(PA cache_pa, unsigned long cache_control, VA tlb_va,
                    unsigned long tlb_len, Asid asid, int flush_tlb,
                    int page_shift, int flush_cache, int flush_asid)
{
  // Wait for istream prefetch (unlikely to be a problem, but
  // we do so to be completely correct).
  asm("icoh zero; drain");

  int retval = 0;
  int phase_retval = 0;
  int asid_va_range = cache_control & HV_FLUSH_ASID_VA_RANGE;

  if (flush_cache)
  {
    //
    // For right now, if we're going to invalidate the whole cache, we're
    // just going to flush it.  There's probably a range of sizes beyond
    // this where it's still cheaper to do the temporary mapping and the
    // finv instructions; it's unclear what that is, exactly.
    //
    unsigned int cache_len = cache_control & HV_FLUSH_MAX_CACHE_LEN;
    if (cache_control & HV_FLUSH_EVICT_L1I)
      flush_icache();
    if ((cache_control & HV_FLUSH_EVICT_L2) ||
        cache_len >= CHIP_L2_CACHE_SIZE())
      phase_retval = inv_whole_l2();
    else if (cache_len)
      phase_retval = inv_partial_l2(cache_pa, cache_len);

    if (!retval)
      retval = phase_retval;
  }

  if (flush_tlb || (flush_asid && asid_va_range))
  {
    Asid real_asid;
    int is_m32;
    if (flush_asid)
    {
      is_m32 = cache_control & HV_FLUSH_TLB_32BIT;
      c2r_asid(asid, &real_asid);
    }
    else
    {
      is_m32 = current_pt.flags & HV_CTX_32BIT;
      real_asid = current_pt.asid;
    }
    phase_retval = flush_pages_asid(tlb_va, 1UL << page_shift, tlb_len,
                                    real_asid, is_m32);
    if (!retval)
      retval = phase_retval;
  }

  if (flush_asid && !asid_va_range)
  {
    phase_retval = syscall_flush_asid(asid);
    if (!retval)
      retval = phase_retval;
  }

  return (retval);
}


/** Update the state structure so we cause a downcall on return to the client.
 * @param state State pointer passed to the original TSB miss or access
 *        violation handler.
 * @param vector Interrupt vector number to put in the state structure.
 * @param fault_addr Fault address.
 */
void
raw_tsb_downcall(struct tsb_downcall_state* state, int vector, VA fault_addr)
{
  state->vector = vector;
  state->faultaddr = fault_addr;
}


/** Cause a TSB miss or access violation downcall to the user.
 * @param state State pointer passed to the original TSB miss or access
 *        violation handler.
 * @param tlb_type TLB type (TSB_xxx \#define).
 * @param flags Flags (TSB_DOWNCALL_xxx).
 * @param fault_addr Fault address.
 * @param miss_reason Miss reason bit (1 if a write miss, 0 if a read miss).
 *        Must be zero for ITLB/SNITLB misses; not used for access violations.
 */
void
tsb_downcall(struct tsb_downcall_state* state, int tlb_type, int flags,
             VA fault_addr, uint32_t miss_reason)
{
  int is_accvio = flags & TSB_DOWNCALL_ACCVIO;

  // Truncate the address if the m32 process address is in the range of top 2G.
  if ((current_pt.flags & HV_CTX_32BIT) && (fault_addr == (int) fault_addr))
    fault_addr = (unsigned int) fault_addr;

  if ((state->regs.ex_context_1 & 3) == HV_PL)
  {
    // We're in the hypervisor; if this is an expected fault, then cause
    // FAULT_BEGIN() to return nonzero, otherwise, panic.
    if (likely(fault_expected()))
    {
      fault_end();
      //
      // Since we're omitting the iret we would have taken for the TLB
      // miss interrupt, we want to reset the interrupt masks to their
      // pre-miss values before we return to the previous bit of code.
      //
      __insn_mtspr(INTERRUPT_MASK_HV, state->regs.intmask);



      longjmp(fault_jmp_buf, 1);
    }
    else
      tsb_fatal(state, tlb_type, is_accvio, fault_addr, miss_reason);
  }
  else
  {
    //
    // We got a fault from the client.  We set up the saved state to cause a
    // downcall to happen when we return to the interrupt dispatch code.
    //
    int vflags = 2;

    //
    // Downcalls go to the guest when we are running a virtualization
    // context, unless we are passed the TSB_DOWNCALL_VIRT flag.
    //
    if (is_guest_fault(flags))
      vflags |= (1UL << 11);

    //
    // Figure out what vector we're going to.
    //
    int vector;
    if (is_accvio)
      vector = access_vectors[tlb_type];
    else
    {
      vflags |= miss_reason;  /* set bit 0 if a write miss */
      vector = miss_vectors[tlb_type];
    }

    //
    // When the client (or guest) is in a critical section, we set a
    // flag that causes us to pass the faulting PC in the argument we
    // pass to the client, instead of putting it in the client's ex_context.
    //
    if ((state->regs.ex_context_1 & SPR_EX_CONTEXT_3_1__ICS_MASK) &&
        ((state->regs.ex_context_1 & SPR_EX_CONTEXT_3_1__PL_MASK) != 0))
    {
      vflags |= (1UL << 10);

      //
      // If we're downcalling back to the same vector we came from, we're
      // probably stuck in an infinite loop, most likely caused by the
      // client having a bad stack pointer.  In that case, do a register
      // dump, then set up to instead downcall to the client's double fault
      // handler.  Note that we only do this for the client, not the guest;
      // in the guest's case it's really the client's job to notice this.
      //
      if ((state->regs.ex_context_1 & SPR_EX_CONTEXT_3_1__PL_MASK) ==
          CLIENT_PL &&
          state->regs.ex_context_0 >> 8 ==
          ((__insn_mfspr(INTERRUPT_VECTOR_BASE_CL) >> 8) | vector))
      {
        static int recursive_tsb_miss = 0;

        if (recursive_tsb_miss)
          panic("recursive supervisor TSB miss\n");
        recursive_tsb_miss = 1;

        tprintf("%sTLB miss in client %sTLB miss handler, "
                "treating as a double fault\n",
                tlb_names[tlb_type], tlb_names[tlb_type]);
        tprintf("Fault address: 0x%lx\n", fault_addr);
        dump_saved_regs(&state->regs);
        vector = INT_DOUBLE_FAULT;
        vflags = 2;
      }
    }

    raw_tsb_downcall(state, (vector << 2) | vflags, fault_addr);
  }
}


/** Terminate the client due to a fatal TSB miss or access violation.
 * @param state State pointer passed to the original TSB miss or access
 *        violation handler.
 * @param tlb_type TLB type (TSB_xxx \#define).
 * @param flags Flags (TSB_DOWNCALL_xxx).
 * @param fault_addr Fault address.
 * @param miss_reason Miss reason bit (1 if a write miss, 0 if a read miss).
 *        Must be zero for ITLB/SNITLB misses; not used for access violations.
 */
void
tsb_fatal(struct tsb_downcall_state* state, int tlb_type, int flags,
          VA fault_addr, uint32_t miss_reason)
{
  int is_accvio = flags & TSB_DOWNCALL_ACCVIO;

  if (is_guest_fault(flags))
  {
    tsb_downcall(state, TSB_GUEST, flags | TSB_DOWNCALL_VIRT,
                 fault_addr, miss_reason);
    return;
  }

  panic_start("fatal %sTSB %s %s, fault addr %#lX",
              tlb_names[tlb_type], (miss_reason | is_accvio) ? "write" : "read",
              is_accvio ? "access violation" : "miss", fault_addr);
  tprintf("%s", tsb_fatal_reason);
  dump_saved_regs(&state->regs);
  panic_end();
}


/** Initialize the TSB, and the TSB acceleration registers.  Note that, unlike
 *  all of the other flush code, this does nothing to the TLBs.
 */
void
init_tsb()
{
  memset(&tsb, 0, sizeof (tsb));

  SPR_DTLB_TSB_BASE_ADDR_0_t base;

#if TSB_S_IDX_WIDTH < 9 || TSB_L_IDX_WIDTH < 9 || CTTE_SHIFT < 4
#error TSB structure too small for accleration hardware
#endif

  // Ensure small page TSB is self-size-aligned
  assert ((((uintptr_t) &tsb[TSB_S_START]) &
           RMASK(TSB_S_IDX_WIDTH + CTTE_SHIFT)) == 0);

  base.word = (VA) &tsb;
  base.entries = TSB_S_IDX_WIDTH - 9;
  base.ps = TTE_SHIFT_TO_PS(page_shift_small);
  base.size = CTTE_SHIFT - 4;

  __insn_mtspr(SPR_DTLB_TSB_BASE_ADDR_0, base.word);
  __insn_mtspr(SPR_ITLB_TSB_BASE_ADDR_0, base.word);

  // Ensure large page TSB is self-size-aligned
  assert ((((uintptr_t) &tsb[TSB_L_START]) &
            RMASK(TSB_L_IDX_WIDTH + CTTE_SHIFT)) == 0);

  base.word = (VA) &tsb[TSB_S_ENTRIES];
  base.entries = TSB_L_IDX_WIDTH - 9;
  base.ps = TTE_SHIFT_TO_PS(page_shift_large);
  base.size = CTTE_SHIFT - 4;

  __insn_mtspr(SPR_DTLB_TSB_BASE_ADDR_1, base.word);
  __insn_mtspr(SPR_ITLB_TSB_BASE_ADDR_1, base.word);
}


/** Dump out TSB-related debugging information to the console.
 * @param dump_invalid If nonzero, dump even invalid entries.
 */
void
dump_tsb(int dump_invalid)
{
  int i;

  tprintf("Currently installed page table: %#llx (%#llx)\n",
          current_pt.base, current_pt.real_base);
  tprintf("Currently installed ASID, ppg bitmask, AAR : %#x, %#x, %#llx\n",
          current_pt.asid, current_caching_bitmask, current_pt.aar);

  tprintf("TSB dump\n");
  for (i = 0; i < TSB_ALL_ENTRIES; i++)
    if (dump_invalid || tsb[i].attr.v)
    {
      tte_t tmp_tte;
      ctte2tte(&tsb[i], &tmp_tte);
      dump_tte(tmp_tte, i, 0);
    }
}
