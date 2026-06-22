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
 *
 * Bare Metal Environment heap memory allocation support.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_BME_MALLOC_H
#define _SYS_BME_BME_MALLOC_H

#include <sys/types.h>

__BEGIN_DECLS

/** Allocate memory from the heap.
 * @param bytes Number of bytes to allocate.
 * @returns Pointer to allocated memory, 0 if none.
 */
void* bme_malloc(size_t bytes);


/** Return memory refererenced by mem to the heap.
 * @param mem Pointer to memory to free.
 */
void bme_free(void* mem);

__END_DECLS

#endif /* _SYS_BME_BME_MALLOC_H */

/** @} */
