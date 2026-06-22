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
 * Load and exec interface.
 * @file
 */

#ifndef _BOGUX_LOADER_H
#define _BOGUX_LOADER_H

#include <stdbool.h>
#include <sys/types.h>

#include <hv/hypervisor.h>

/** Communicate information on a desired exec within or across tiles */
typedef struct ExecData
{
  HV_PhysAddr base;   /**< base of physical address area for top of stack */
  int size;           /**< size of PA area for the top of stack */
  int file;           /**< offset of file to run in base */
  int phnum;          /**< offset from base of AT_PHNUM value */
  int phdr;           /**< offset from base of AT_PHDR value */
} __attribute__((packed)) ExecData;

/** Fill in an ExecData structure.
 * @param msg ExecData structure to fill in
 * @param user Whether the pointers should be validated for user access
 * @param flush Whether to flush and mf the written data; this is
 *   required if another tile will use the page that is created
 * @param filename File name to exec
 * @param argv Argument vector to use
 * @param envp Environment to use
 */
int build_exec_data(ExecData* msg, bool user, bool flush,
                    const char* filename, char **argv, char** envp);

/** Perform an execve() based on the ExecData */
int do_execve(ExecData* msg);

/** Is the given tile idle? */
bool is_tile_idle(uint32_t tile_id);

/** Mark the given tile as no longer idle, and atomically return whether
 * it was idle before the call.
 */
bool unidle_tile(uint32_t tile_id);

/** Translate a tile ID to a pid; if the tile is idle, return -1. */
pid_t tile_to_pid(uint32_t tile_id);

/* Is the given pid active? */
bool pid_active(pid_t pid);

/** Put a user into an infinite nap loop.
 * Once all tiles are here, the OS exits.
 */
void nap_user(void) __attribute__((noreturn));

/** Cycle on which we entered user code. */
extern uint64_t ts_user_start_cycle;

/** Executable running on this tile (points to user stack). */
extern const char* ts_exec_path;

/** Number of times left to rerun the current executable after it exits */
extern int ts_rerun;

#endif  /* !_BOGUX_LOADER_H */
