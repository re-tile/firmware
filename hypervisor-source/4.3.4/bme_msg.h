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
 * Special messages sent by the BME runtime.
 */

#ifndef _SYS_HV_BME_MSG_H
#define _SYS_HV_BME_MSG_H

#include <stdint.h>

#include "msgtag.h"

//
// Note that all BME request messages have an extra word, similar to and
// directly following the implicit sending tile ID, which specifies an
// explicit tag to use on the reply message.
//

/** Request to write to the console.
 */
struct bme_msg_write_console
{
  long len;       /**< Number of bytes to write; these follow this message. */
};

/** Console write tag */
#define BME_TAG_WRITE_CONSOLE       HV_MKTAG(0xB0, HV_MSG_PRI_CONSOLE)

#endif /* _SYS_HV_BME_MSG_H */
