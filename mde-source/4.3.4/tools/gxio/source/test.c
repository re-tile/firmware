/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

/**
 * @file
 * IO RPC framework test driver.
 */
#include "gxio/test.h"
#include "test_rpc_call.h"
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/** Initialize the test driver. */
int gxio_test_init(gxio_test_context_t* context)
{
  context->fd = open("/dev/iorpc/test", O_RDWR);
  if (context->fd < 0)
    return -errno;

  return 0;
}

int gxio_test_release(gxio_test_context_t* context)
{
  return close(context->fd);
}
