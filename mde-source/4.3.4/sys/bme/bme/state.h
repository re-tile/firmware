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
 * Bare Metal Environment per-tile state support.  These routines support
 * an application-supplied per-tile state structure, and might be useful for
 * applications running under the BME's shared data model.
 *
 * @addtogroup bme
 * @{
 */

#ifndef _SYS_BME_STATE_H
#define _SYS_BME_STATE_H

#include <features.h>

__BEGIN_DECLS

/** Get this tile's state pointer.
 * @return State pointer pointer.
 */
void* bme_get_pertile_state(void);


/** Set this tile's state pointer.
 * @param state New state pointer pointer.
 */
void bme_get_pertile_state(void* state);

__END_DECLS

#endif /* _SYS_BME_STATE_H */

/** @} */
