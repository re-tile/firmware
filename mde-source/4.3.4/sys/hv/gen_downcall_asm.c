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
 * Generate the header file for the downcall assembly code.
 */

#include "asmhdr.h"

#include "downcall.h"
#include "drvchan.h"

ASMHDR_BEGIN()

ASMHDR_DEF(DOWNCALL_MESSAGE_RCV)
ASMHDR_DEF(DOWNCALL_DMATLB_MISS)
ASMHDR_DEF(DOWNCALL_DMATLB_ACCESS)
ASMHDR_DEF(DOWNCALL_SNITLB_MISS)
ASMHDR_DEF(DOWNCALL_DEV_INTR)

ASMHDR_TYPE(struct downcall_info, DOWNCALL)
ASMHDR_MEMBER(pending_downcalls, )
ASMHDR_ENDTYPE()

ASMHDR_END()
