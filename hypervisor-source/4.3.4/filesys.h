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
 * The hypervisor filesystem.
 */

#ifndef _SYS_HV_FILESYS_H
#define _SYS_HV_FILESYS_H

//
// This file is used by sbim as well as by the main hypervisor.  We modify
// the included files in the non-hypervisor case to pull in only what's
// needed.
//
#ifdef __HV__
#include <stdio.h>

#include <hv/hypervisor.h>

#include "tile.h"
#include "types.h"
#else
#include <stdint.h>
#endif

#define HV_PATH_MAX    256     /**< Maximum length of a file name in the
                                    hypervisor filesystem */

//
// Hypervisor filesystem data format
//
// The hypervisor filesystem is composed of six regions:
//
// 1. The filesystem header.  This contains a magic number for identification,
//    the number of files in the filesystem, its total length, and an offset
//    from the start of the filesystem to a string describing it, which is
//    stored in the filename region.
//
// 2. A CRC of the filesystem header.
//
// 3. The inodes.  There is an inode for each file; each inode contains
//    the offset of the file's name and the file's data from the start of the
//    filesystem as well as the file's length and the file's attribute flags.
//
// 4. The filenames.  These are null-terminated.
//
// 5. File data.  These are not terminated in any way, but extra zeroes are
//    added to make every file start on a word boundary.
//
// 6. A CRC of items 3-5.
//

/** Filesystem header */
typedef struct
{
  /** Filesystem magic number (HV_FS_MAGIC) */
  uint32_t magic;

  /** Number of inodes */
  uint32_t ninode;

  /** Total length of filesystem in bytes */
  uint32_t fs_len;

  /** Offset to filesystem description */
  uint32_t fs_desc_off;
}
fs_hdr_t;

/** Filesystem CRC.  There's an instance of this after the header, which
 *  covers the header, and one after the remainder of the filesystem,
 *  which covers the inode table, filenames, and filesystem data. */
typedef struct
{
  /** CRC of filesystem contents.  This covers the filesystem from the
   *  start of the header to the last byte of data, but not including the
   *  trailer. */
  uint32_t crc;
}
fs_crc_t;

/** Filesystem magic number */
#define HV_FS_MAGIC  0x73467648 // "HvFs"

/** Per-file data */
typedef struct
{
  /** Offset to file name */
  uint32_t       name_off;

  /** Offset to file data */
  uint32_t       data_off;

  /** Length of file */
  uint32_t       len;

  /** File flags */
  uint32_t       flags;
}
inode_t;

#ifdef __HV__

void init_fs(struct slave_tile_state* sts, int srom_addr, pos_t rshim);

int fs_findfile(char* filename);
void fs_stat(int inode, int* len, unsigned int* flags);
int fs_pread(int inode, char* buf, int length, int offset);
int fs_pread_user(int inode, char* buf, int length, int offset);

void fs_sim_notify_exec(int inode);

int fs_open(char* filename, FILE* f, char* buf, int len);

int syscall_fs_findfile(char* filename);
HV_FS_StatInfo syscall_fs_fstat(int inode);
int syscall_fs_pread(int inode, char* buf, int length, int offset);

#endif /* __HV__ */

#endif /* _SYS_HV_FILESYS_H */
