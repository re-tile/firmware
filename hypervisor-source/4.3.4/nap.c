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
 * Napping routines.
 */

#include "misc.h"

void
wake_idn_ca_msg(struct saved_regs* sr)
{
  extern char* nap_idn_ca_msg;
  extern char* nap_idn_ca_msg_complete;

  if (sr->ex_context_0 >= (uintptr_t) &nap_idn_ca_msg &&
      sr->ex_context_0 < (uintptr_t) &nap_idn_ca_msg_complete)
    sr->ex_context_0 = (uintptr_t) &nap_idn_ca_msg_complete;
}
