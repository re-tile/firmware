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
 * Load an ELF executable into client space.
 */

#ifndef _SYS_HV_LOADER_H
#define _SYS_HV_LOADER_H

#include <arch/chip.h>

#include "types.h"

#ifndef __DOXYGEN__
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
#endif

CPA load(int inode);
PA bme_load(int inode);
CPA load_null_client(void);

#endif /* _SYS_HV_LOADER_H */
