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
 * Protection level information for the BME.
 */

#ifndef _SYS_BME_BME_PL_H
#define _SYS_BME_BME_PL_H

#include "param.h"

/** Catenate strings. */
#define _CAT(a, b, c) a ## b ## c
/** Catenate strings. */
#define CAT(a, b, c)  _CAT(a, b, c)

/** Section name for BME's interrupt section */
#define DOT_INTRPT_BME               CAT(.intrpt, BME_PL, )
// FIXME: we need to figure out BME's VA region for GX (also shift here will probably not be 24)
/** First address in BME's VA region */
#define BME_VA_BASE                  (0xfc000000 | (BME_PL << 24))
/** First address after BME's VA region */
#define BME_VA_LIMIT                 (BME_VA_BASE + (1 << 24))
/** BME system save register zero */
#define SYSTEM_SAVE_BME_0            CAT(SPR_SYSTEM_SAVE_, BME_PL, _0)
/** BME system save register one */
#define SYSTEM_SAVE_BME_1            CAT(SPR_SYSTEM_SAVE_, BME_PL, _1)
/** BME system save register two */
#define SYSTEM_SAVE_BME_2            CAT(SPR_SYSTEM_SAVE_, BME_PL, _2)
/** BME system save register three */
#define SYSTEM_SAVE_BME_3            CAT(SPR_SYSTEM_SAVE_, BME_PL, _3)
/** BME exceptional context register zero */
#define EX_CONTEXT_BME_0             CAT(SPR_EX_CONTEXT_, BME_PL, _0)
/** BME exceptional context register one */
#define EX_CONTEXT_BME_1             CAT(SPR_EX_CONTEXT_, BME_PL, _1)
/** BME interrupt mask register */
#define INTERRUPT_MASK_BME         CAT(SPR_INTERRUPT_MASK_, BME_PL, )
/** BME set interrupt mask register */
#define INTERRUPT_MASK_SET_BME     CAT(SPR_INTERRUPT_MASK_SET_, BME_PL, )
/** BME reset interrupt mask register */
#define INTERRUPT_MASK_RESET_BME   CAT(SPR_INTERRUPT_MASK_RESET_, BME_PL, )

#endif /* _SYS_BME_BME_PL_H */
