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

// Routines to manipulate the flash ROM.

#ifndef TOOLS_MBOOTCLI_FLASH_H
#define TOOLS_MBOOTCLI_FLASH_H

#define USER_PARAM          1
#define BOOT_PARAM          0

#ifndef BLOCKS_IN_FLASH
#define BLOCKS_IN_FLASH     2
#endif

#define DEVICE_NUM          2

void write_flash_data(int device, const char* datap, int numtowrite);
int read_flash_data(int device, int* length, int* read_len, char* ret);

#endif /* TOOLS_MBOOTCLI_FLASH_H */
