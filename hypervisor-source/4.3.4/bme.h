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
 * BME support routines.
 */

#ifndef _SYS_HV_BME_H_
#define _SYS_HV_BME_H_

#include "tsb.h"
#include "types.h"

/** PA of the memory containing locks shared between the HV and the BME. */
extern PA shared_lock_page_pa;

/** Lotar for the memory containing locks shared between the HV and the BME. */
extern Lotar shared_lock_page_lotar;

/** Function to initialize memory for shared lock */
void init_shared_lock(PA* page_pa, Lotar* lotar);

/** Load the client program into the client's memory.
 * @return Client physical address of loaded executable's entry point.
 */
CPA load_bme(void);

/** Start the BME. This is run on client tiles.
 * @param mshim_pa List of start PA for the memory controllers.
 * @param mshim_len Length in bytes of available memory on the controllers.
 * @param entrypoint The entrypoint to jump to in order to start the client.
 */
void start_client_bme(const PA mshim_pa[], const PA mshim_len[],
                      CPA entrypoint);

#endif /* _SYS_HV_BME_H_ */
