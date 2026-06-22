/**
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
 *
 * Header for inter-tile messages
 * @file
 */

#ifndef _SYS_MESSAGING_H
#define _SYS_MESSAGING_H

#include "loader.h"   // ExecData

typedef enum SupervisorMessageTag
{
  /* We don't use this tag */
  SMSG_INVAL = 0,

  /* Exec a user process: see SMsg_Exec, below */
  SMSG_EXEC,

  /* Flush TLB and optionally page tables (for munmap, mprotect) */
  SMSG_FLUSH_MAPPINGS,

  /* Update caching mode for hypervisor (for MAP_COMMON|MAP_CACHE_PRIORITY) */
  SMSG_UPDATE_CACHING,

} SupervisorMessageTag;

typedef struct SMsg_Exec
{
  int tag;            /**< tag for message (must be SMSG_EXEC) */
  ExecData data;      /**< the actual data for the remote exec() */
} SMsg_Exec;

void init_messaging(void);

#endif
