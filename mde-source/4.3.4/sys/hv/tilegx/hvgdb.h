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
 * Routines to support gdb stub.
 */

/* FIXME: GX: this is really just a stub, since we haven't ported hvgdb yet. */

#ifndef _SYS_HV_HVGDB_H
#define _SYS_HV_HVGDB_H

#include "param.h"
#include "types.h"
#include "tte.h"

#ifndef __ASSEMBLER__

/** Second phase of hvgdb initialization, called from hv().  Allocates
 * physical memory for the hvgdb stack if called on the master tile and
 * passes address of allocated memory to slave tiles.  If EARLY_HVGDB is
 * enabled, also pins DTLB for stack.
 * @param pa If NULL, means allocate memory, else pass address to slave.
 * @return Address of allocated memory.
 */
extern PA hvgdb_init2(PA pa);

/** Writes mapping info to debug stub memory for Level 0 access by host.
 */
extern int hvgdb_store_mapping(VA va, uint32_t len, PA pa);

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_HVGDB_H */
