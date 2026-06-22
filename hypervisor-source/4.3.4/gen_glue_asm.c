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
 * Generate the header file for the hypervisor glue.
 */

#include "asmhdr.h"

#include "client_msg_shared.h"

ASMHDR_BEGIN()

ASMHDR_TYPE(struct client_msg, CLIENT_MSG)
ASMHDR_MEMBER(len, )
ASMHDR_MEMBER(source, )
ASMHDR_MEMBER(msg, )
ASMHDR_ENDTYPE()

ASMHDR_TYPE(struct client_msg_area, CLIENT_MSG_AREA)
ASMHDR_MEMBER(head, )
ASMHDR_MEMBER(tail, )
ASMHDR_MEMBER(msgs[0], MSGS)
ASMHDR_ENDTYPE()

ASMHDR_END()
