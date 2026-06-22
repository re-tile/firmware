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
 * mPIPE driver.
 */
#ifndef _DRIVERS_MPIPE_RULES_H_
#define _DRIVERS_MPIPE_RULES_H_

#include "mpipe.h"

extern int
mpipe_open_aux(mpipe_state_t* ms, int svc_dom);

extern int
mpipe_close_aux(mpipe_state_t* ms, int svc_dom);

#endif
