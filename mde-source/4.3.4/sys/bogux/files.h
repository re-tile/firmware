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
 * Definitions for file access routines.
 * @file
 */

#ifndef _SYS_BOGUX_FILES_H
#define _SYS_BOGUX_FILES_H

#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include "stat.h"

typedef long long loff_t;

/** The maximum number of file descriptors per tile. */
#define MAX_DESCRIPTORS 64

/** Describe a single file descriptor. */
struct fd
{
  /** How do we actually do things with this fd? */
  const struct fops* fops;

  /** If necessary, the inode for this fd. */
  int inode;

  /** The flags used to open this file */
  int flags;

  /** The mode of the underlying file */
  int mode;

  /** The size of the file */
  loff_t size;

  /** The current file position, if relevant. */
  loff_t offset;

  /** The current accumulated file CRC, if relevant. */
  uint32_t crc;

  /** true if we're CRCing this file. */
  bool crc_enabled;

  /** true if the CRC has ever been written. */
  bool crc_valid;
};

/** Describe simple I/O.
 * All the function pointers here return negative errno on error.
 */
struct fops
{
  /** Open file from name; return zero on success. */
  ssize_t (*open)(struct fd*, const char* name);

  /** Read bytes into a buffer; return bytes read on success. */
  ssize_t (*read)(struct fd*, char* buf, ssize_t bytes);

  /** Write bytes from a buffer; return bytes written on success. */
  ssize_t (*write)(struct fd*, const char* buf, ssize_t bytes);

  /** Perform miscellaneous I/O control operations. */
  int (*ioctl)(struct fd*, int cmd, void* ptr);

  /** Close the file. */
  void (*close)(struct fd*);

  /** Driver flags (FOPS_FLG_xxx). */
  int flags;
};

/** This driver is the hvtest driver. */
#define FOPS_FLG_HVTEST     0x1

// Non-syscall access to file I/O functionality
int do_open(const char* path, int flags, int mode);
int do_read(int fd, void *buf, size_t count);
int do_pread(int fd, void *buf, size_t count, loff_t offset);
int do_fstat(int fd, struct kernel_stat* buf);
void do_filesums(bool enable);
void do_closeall(void);
struct fd* get_fd(int index);

/** Seed for this tile's random number generator */
extern uint32_t ts_rand_seed;

/** The array of file descriptors */
extern struct fd ts_descriptors[MAX_DESCRIPTORS];

#endif /* _SYS_BOGUX_FILES_H */
