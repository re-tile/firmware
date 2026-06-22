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
 * Definitions related to translation table entries.
 */

#ifndef _SYS_HV_TILEGX_TTE_H
#define _SYS_HV_TILEGX_TTE_H

#include <arch/spr.h>

#include "bits.h"
#include "types.h"

//
// For Gx, we have two different ways of representing a translation
// table entry.  In most places, we use a tte_t, like we do on T64/Pro.
// However, in the TSB itself, we use a compressed TTE, or ctte_t.
// This stuffs the PA and VA into one word, so a ctte_t is just two
// words, vs. three for a tte_t.  This speeds up the TLB miss handler,
// both by eliminating the need for a load instruction, and by
// increasing the likelihood of cache hits on the TSB.
//
#define TTE_PS_4K       0  /**< 4K page size code */
#define TTE_PS_16K      1  /**< 16K page size code */
#define TTE_PS_64K      2  /**< 64K page size code */
#define TTE_PS_256K     3  /**< 256K page size code */
#define TTE_PS_1M       4  /**< 1M page size code */
#define TTE_PS_4M       5  /**< 4M page size code */
#define TTE_PS_16M      6  /**< 16M page size code */
#define TTE_PS_64M      7  /**< 64M page size code */
#define TTE_PS_256M     8  /**< 256M page size code */
#define TTE_PS_1G       9  /**< 1G page size code */
#define TTE_PS_4G      10  /**< 4G page size code */
#define TTE_PS_16G     11  /**< 16G page size code */
#define TTE_PS_64G     12  /**< 64G page size code */

#define TTE_PS_MIN      TTE_PS_4K   /**< Smallest page size */
#define TTE_PS_MAX      TTE_PS_64G  /**< Largest page size */

/** Convert log2 of a page size to its page size code */
#define TTE_SHIFT_TO_PS(x)      (((x) - 12) / 2)
/** Covert a page size code to log2 of the size of the page */
#define TTE_PS_TO_SHIFT(x)      ((x) * 2 + 12)

/** Log2 of size of a CTTE */
#define CTTE_SHIFT      4
/** Size of a CTTE */
#define CTTE_SIZE       (1 << CTTE_SHIFT)

/** Offset to the VA and PA within the CTTE */
#define CTTE_VAPA_OFFSET        0

/** Offset to the attributes within the CTTE */
#define CTTE_ATTR_OFFSET        8

#ifndef __ASSEMBLER__

/* Definition of a TTE, for C code. */

/** Word 0 of a TTE */
typedef SPR_DTLB_TSB_FILL_CURRENT_ATTR_t tte_w0_t;

/** Word 1 of a TTE */
typedef SPR_DTLB_CURRENT_VA_t tte_w1_t;

/** Word 2 of a TTE */



typedef SPR_DTLB_CURRENT_PA_t tte_w2_t;


/** The full translation table entry. */
typedef struct
{
    tte_w0_t w0;            /**< Word 0 of the TTE */
    tte_w1_t w1;            /**< Word 1 of the TTE */
    tte_w2_t w2;            /**< Word 2 of the TTE */
} tte_t;

/** A zero-valued TTE. */
#define TTE_ZERO ((tte_t) {{{ 0 }}})

/* Definition of a CTTE, for C code. */

/** Word 0 of a CTTE */
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__










    /** Top part of the virtual page frame number. */
    unsigned long va_41_36 :  6;
    /** Unused bits. */
    unsigned long reserved :  6;
    /** Physical page frame number. */
    unsigned long pa_39_12 : 28;
    /** Bottom part of the virtual page frame number. */
    unsigned long va_35_12 : 24;

#else  // __BIG_ENDIAN__






    unsigned long va_35_12 : 24;
    unsigned long pa_39_12 : 28;
    unsigned long reserved :  6;
    unsigned long va_41_36 :  6;

#endif // __BIG_ENDIAN__
  };

  /** Access to full word-size value. */
  uint_reg_t word;
} ctte_vapa_t;

/** Word 1 of a CTTE */
typedef SPR_DTLB_TSB_FILL_CURRENT_ATTR_t ctte_attr_t;

/** The full compressed translation table entry.  If this is modified, fix the
 *  CTTE_xxx_OFFSET values above. */
typedef struct
{
    ctte_vapa_t vapa;           /**< Word 0 of the CTTE */
    ctte_attr_t attr;           /**< Word 1 of the CTTE */
} ctte_t;


/** Insert a PFN into a CTTE. */
static inline void
ctte_set_pfn(ctte_t* c, PA pfn)
{



  c->vapa.pa_39_12 = pfn;

}

/** Extract a PFN from a CTTE. */
static inline PA
ctte_get_pfn(ctte_t* c)
{



  return (c->vapa.pa_39_12);

}

/** Insert a VPFN into a CTTE. */
static inline void
ctte_set_vpfn(ctte_t* c, PA vpfn)
{




  c->vapa.va_41_36 = vpfn >> 24;
  c->vapa.va_35_12 = vpfn & 0xFFFFFF;

}

/** Extract a VPFN from a CTTE. */
static inline PA
ctte_get_vpfn(ctte_t* c)
{



  return ((VA) c->vapa.va_41_36 << 24) | c->vapa.va_35_12;

}

/** Convert a TTE to a CTTE. */
static inline void
tte2ctte(tte_t* t, ctte_t* c)
{
  c->attr = t->w0;
  ctte_set_vpfn(c, t->w1.vpn);
  ctte_set_pfn(c, t->w2.pfn);
}

/** Get a virtual address from a CTTE. */
static inline VA
ctte_get_va(ctte_t* c)
{
  int sign_extend_bits = (8 * sizeof(intptr_t)) - CHIP_VA_WIDTH();
  VA va = ctte_get_vpfn(c) << 12;
  return ((intptr_t)va << sign_extend_bits) >> sign_extend_bits;
}

/** Convert a CTTE to a TTE. */
static inline void
ctte2tte(ctte_t* c, tte_t* t)
{
  t->w0 = c->attr;
  t->w1.word = ctte_get_va(c);
  t->w2.word = 0;
  t->w2.pfn = ctte_get_pfn(c);
}


/** Extract a virtual page frame number from a VA */
static inline VA VPFN(VA va)
{
  tte_w1_t tmp = { .word = va };
  return (tmp.vpn);
}

/** Extract a physical page frame number from a PA */
static inline PA PFN(PA pa)
{
  tte_w2_t tmp = { .word = pa };
  return (tmp.pfn);
}

#endif /* !__ASSEMBLER__ */

/** This is the start and end bit position which allows a bfexts
 *  instruction to extract a VA out of a CTTE, aligned for insertion into
 *  the CURRENT_VA register.  (Some of the low 12 VA bits will be from the
 *  PA, but that's okay as they are ignored by hardware.)  The CTTE has the
 *  PFN properly aligned for insertion into the CURRENT_PA register.
 */



#define CTTE_VAPA_VA_FIELD  28,5


/** Define if we want to allow 4KB pages using VA/PA alignment. */
#undef TTE_VA_PA_ALIGNMENT

/** TILE-Gx's TTEs must always have the VA and PA aligned in the
 *  low 14 bits, even for 4KB page size.
 */
#define TTE_VA_PA_ALIGN_SHIFT PG_SHIFT_16K

/** The size of the smallest VA/PA alignable page in bytes. */
#define TTE_VA_PA_ALIGN_SIZE (1 << TTE_VA_PA_ALIGN_SHIFT)

#endif /* _SYS_HV_TILEGX_TTE_H */
