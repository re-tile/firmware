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
 *
 * Implementation of mica gxio calls.
 */

#include "gxio/mica.h"
#include "mica_rpc_call.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

int
gxio_mica_init(gxio_mica_context_t* context, gxio_mica_accelerator_type_t type,
               int mica_index)
{
  char file[32];


  if (type == GXIO_MICA_ACCEL_CRYPTO)
    snprintf(file, sizeof(file), "/dev/iorpc/crypto%d", mica_index);
  else if (type == GXIO_MICA_ACCEL_COMP)
    snprintf(file, sizeof(file), "/dev/iorpc/comp%d", mica_index);
  else
    return GXIO_MICA_ERR_BAD_ACCEL_TYPE;

  context->fd = open(file, O_RDWR);

  if (context->fd < 0)
  {
    return -errno;
  }

  // Map in the Context User MMIO space, for just this one context.
  context->mmio_context_user_base =
    mmap(NULL, HV_MICA_CONTEXT_USER_MMIO_SIZE, PROT_READ | PROT_WRITE,
         MAP_SHARED, context->fd, 0);
  if (context->mmio_context_user_base == MAP_FAILED)
  {
    close(context->fd);
    return -errno;
  }


  return 0;
}


int
gxio_mica_destroy(gxio_mica_context_t* context)
{
  munmap(context->mmio_context_user_base, HV_MICA_CONTEXT_USER_MMIO_SIZE);
  return close(context->fd) ? -errno : 0;
}


int
gxio_mica_register_page(gxio_mica_context_t* context,
                        void* page, size_t page_size, unsigned int page_flags)
{
  unsigned long vpn = (unsigned long)page >> 12;
  return gxio_mica_register_page_aux(context, page, page_size, page_flags,
                                     vpn);
}

int
gxio_mica_unregister_page(gxio_mica_context_t* context, void* page)
{
  return gxio_mica_unregister_page_aux(context, page);
}

void
gxio_mica_memcpy_start(gxio_mica_context_t* context, void* dst, void* src,
                       int length)
{
  MICA_OPCODE_t opcode_oplen = {{ 0 }};

  __gxio_mmio_write(context->mmio_context_user_base + MICA_SRC_DATA,
                    (unsigned long)src);
  __gxio_mmio_write(context->mmio_context_user_base + MICA_DEST_DATA,
                    (unsigned long)dst);
  __gxio_mmio_write(context->mmio_context_user_base + MICA_EXTRA_DATA_PTR, 0);

  opcode_oplen.size = length;
  opcode_oplen.engine_type = MICA_OPCODE__ENGINE_TYPE_VAL_MEM_CPY;
  opcode_oplen.src_mode = MICA_OPCODE__SRC_MODE_VAL_SINGLE_BUFF_DESC;
  opcode_oplen.dest_mode = MICA_OPCODE__DEST_MODE_VAL_SINGLE_BUFF_DESC;

  __insn_mf();
  __gxio_mmio_write(context->mmio_context_user_base + MICA_OPCODE,
                    opcode_oplen.word);
}


void
gxio_mica_start_op(gxio_mica_context_t* context,
                   void* src, void* dst, void* extra_data,
                   gxio_mica_opcode_t opcode)
{
  __gxio_mmio_write(context->mmio_context_user_base + MICA_SRC_DATA,
                    (unsigned long)src);
  __gxio_mmio_write(context->mmio_context_user_base + MICA_DEST_DATA,
                    (unsigned long)dst);
  __gxio_mmio_write(context->mmio_context_user_base + MICA_EXTRA_DATA_PTR,
                    (unsigned long)extra_data);

  __insn_mf();

  __gxio_mmio_write(context->mmio_context_user_base + MICA_OPCODE,
                    opcode.word);
}


int
gxio_mica_is_busy(gxio_mica_context_t* context)
{
  MICA_IN_USE_t inuse;
  inuse.word = __gxio_mmio_read(context->mmio_context_user_base + MICA_IN_USE);
  return inuse.in_use;
}
