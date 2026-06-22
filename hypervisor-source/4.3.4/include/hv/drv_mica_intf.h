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
 * Interface definitions for the general mica driver.
 */

#ifndef _SYS_HV_DRV_MICA_INTF_H
#define _SYS_HV_DRV_MICA_INTF_H

#include <arch/mica.h>
#include <arch/mica_def.h>
#include <arch/mica_crypto.h>
#include <arch/mica_crypto_def.h>

#ifndef __DOXYGEN_API_REF__

/** Offset for the Context User register MMIO region. */
#define HV_MICA_CONTEXT_USER_MMIO_OFFSET(context_num)     \
  ((MICA_ADDRESS_SPACE__PARTITION_VAL_CONTEXT_USER << \
   MICA_ADDRESS_SPACE__PARTITION_SHIFT) | \
   ((context_num) << MICA_ADDRESS_SPACE_CTX_USER__CONTEXT_SHIFT))

/** Size of the Context User register MMIO region. */
#define HV_MICA_CONTEXT_USER_MMIO_SIZE \
  ((1 << MICA_ADDRESS_SPACE_CTX_USER__CONTEXT_SHIFT))

/** The number of contexts supported by the MICA shim */
#define HV_MICA_NUM_CONTEXTS 40

#endif /* !__DOXYGEN_API_REF__ */

#endif /* _SYS_HV_DRV_MICA_INTF_H */
