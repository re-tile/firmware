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
 * Implementation of shared methods.
 */
#include <string.h>
#include <hv/iorpc.h>
#include "gxio/common.h"


const char*
gxio_strerror(int error)
{
  switch (error)
  {
  case GXIO_ERR_OPCODE:
    return "Bad RPC opcode - possible version incompatibility";
  case GXIO_ERR_INVAL:
    return "Invalid parameter";
  case GXIO_ERR_ALIGNMENT:
    return "Memory buffer did not meet alignment requirements";
  case GXIO_ERR_COHERENCE:
    return "Memory buffers must be coherent and cacheable";
  case GXIO_ERR_ALREADY_INIT:
    return "Resource already initialized";
  case GXIO_ERR_NO_SVC_DOM:
    return "No service domains available";
  case GXIO_ERR_INVAL_SVC_DOM:
    return "Illegal service domain number";
  case GXIO_ERR_MMIO_ADDRESS:
    return "Illegal MMIO address";
  case GXIO_ERR_INTERRUPT:
    return "Illegal interrupt binding.";
  case GXIO_ERR_CLIENT_MEMORY:
    return "Unreasonable client memory.";
  case GXIO_ERR_IOTLB_ENTRY:
    return "No more IOTLB entries";
  case GXIO_ERR_INVAL_MEMORY_SIZE:
    return "Invalid memory size";
  case GXIO_ERR_UNSUPPORTED_OP:
    return "Unsupported operation";
  case GXIO_ERR_DMA_CREDITS:
    return "Insufficient DMA credits";
  case GXIO_ERR_TIMEOUT:
    return "Operation timed out";
  case GXIO_ERR_NO_DEVICE:
    return "No such device or link";
  case GXIO_ERR_BUSY:
    return "Device or resource busy";
  case GXIO_ERR_IO:
    return "I/O error";
  case GXIO_ERR_PERM:
    return "Permissions error";

  case GXIO_TEST_ERR_REG_NUMBER:
    return "Illegal register number";
  case GXIO_TEST_ERR_BUFFER_SLOT:
    return "Illegal buffer slot";

  case GXIO_MPIPE_ERR_INVAL_BUFFER_SIZE:
    return "Invalid buffer size";

  case GXIO_MPIPE_ERR_NO_BUFFER_STACK:
    return "Cannot allocate buffer stack";
  case GXIO_MPIPE_ERR_BAD_BUFFER_STACK:
    return "Invalid buffer stack number";
  case GXIO_MPIPE_ERR_NO_NOTIF_RING:
    return "Cannot allocate NotifRing";
  case GXIO_MPIPE_ERR_BAD_NOTIF_RING:
    return "Invalid NotifRing number";
  case GXIO_MPIPE_ERR_NO_NOTIF_GROUP:
    return "Cannot allocate NotifGroup";
  case GXIO_MPIPE_ERR_BAD_NOTIF_GROUP:
    return "Invalid NotifGroup number";
  case GXIO_MPIPE_ERR_NO_BUCKET:
    return "Cannot allocate bucket";
  case GXIO_MPIPE_ERR_BAD_BUCKET:
    return "Invalid bucket number";
  case GXIO_MPIPE_ERR_NO_EDMA_RING:
    return "Cannot allocate eDMA ring";
  case GXIO_MPIPE_ERR_BAD_EDMA_RING:
    return "Invalid eDMA ring number";
  case GXIO_MPIPE_ERR_BAD_CHANNEL:
    return "Invalid channel number";
  case GXIO_MPIPE_ERR_BAD_CONFIG:
    return "Invalid configuration";

  case GXIO_MPIPE_ERR_IQUEUE_EMPTY:
    return "Empty iqueue";

  case GXIO_MPIPE_ERR_RULES_EMPTY:
    return "Empty rules";
  case GXIO_MPIPE_ERR_RULES_FULL:
    return "Full rules";
  case GXIO_MPIPE_ERR_RULES_CORRUPT:
    return "Corrupt rules";
  case GXIO_MPIPE_ERR_RULES_INVALID:
    return "Invalid rules";

  case GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG:
    return "Classifier is too big";
  case GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX:
    return "Classifier is too complex";
  case GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER:
    return "Classifier has bad header";
  case GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS:
    return "Classifier has bad contents";
  case GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL:
    return "Classifier encountered invalid symbol";
  case GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS:
    return "Classifier encountered invalid bounds";
  case GXIO_MPIPE_ERR_CLASSIFIER_INVAL_RELOCATION:
    return "Classifier encountered invalid relocation";
  case GXIO_MPIPE_ERR_CLASSIFIER_UNDEF_SYMBOL:
    return "Classifier encountered undefined symbol";


  case GXIO_TRIO_ERR_NO_MEMORY_MAP:
    return "Cannot allocate memory map region";
  case GXIO_TRIO_ERR_BAD_MEMORY_MAP:
    return "Invalid memory map region number";
  case GXIO_TRIO_ERR_NO_SCATTER_QUEUE:
    return "Cannot allocate scatter queue";
  case GXIO_TRIO_ERR_BAD_SCATTER_QUEUE:
    return "Invalid scatter queue number";
  case GXIO_TRIO_ERR_NO_PUSH_DMA_RING:
    return "Cannot allocate push DMA ring";
  case GXIO_TRIO_ERR_BAD_PUSH_DMA_RING:
    return "Invalid push DMA ring index";
  case GXIO_TRIO_ERR_NO_PULL_DMA_RING:
    return "Cannot allocate pull DMA ring";
  case GXIO_TRIO_ERR_BAD_PULL_DMA_RING:
    return "Invalid pull DMA ring index";
  case GXIO_TRIO_ERR_NO_PIO:
    return "Cannot allocate PIO region";
  case GXIO_TRIO_ERR_BAD_PIO:
    return "Invalid PIO region index";
  case GXIO_TRIO_ERR_NO_ASID:
    return "Cannot allocate ASID";
  case GXIO_TRIO_ERR_BAD_ASID:
    return "Invalid ASID";

  case GXIO_MICA_ERR_BAD_ACCEL_TYPE:
    return "No such accelerator type";
  case GXIO_MICA_ERR_NO_CONTEXT:
    return "Cannot allocate context";
  case GXIO_MICA_ERR_OPERATION_FAILED:
    return "MiCA operation failed";

  case GXIO_MICA_ERR_PKA_CMD_QUEUE_FULL:
    return "PKA command queue is full";
  case GXIO_MICA_ERR_PKA_RESULT_QUEUE_EMPTY:
    return "PKA result queue is empty";

  case GXIO_GPIO_ERR_PIN_UNAVAILABLE:
    return "Pin unavailable";
  case GXIO_GPIO_ERR_PIN_BUSY:
    return "Pin busy";
  case GXIO_GPIO_ERR_PIN_UNATTACHED:
    return "Cannot access unattached pin";
  case GXIO_GPIO_ERR_PIN_INVALID_MODE:
    return "Invalid I/O mode for pin";

  case 0:
    return "Success";

  default:
    return strerror(-error);
  }
}
