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
 * Header for device drivers
 * @file
 */

#ifndef _SYS_DEVICES_H
#define _SYS_DEVICES_H

#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include "files.h"   // fops

// device stuff here
extern const struct fops cons_fops;


/** Initialize devices. */
void init_devices(void);


/** What devices do we have? */
const struct fops *
find_device(const char *file, int flags, int mode);

// Non-syscall access.
bool is_hugetlb_file(int fd);
ssize_t chr_open(struct fd* fd, const char* name);
ssize_t cons_write(struct fd* fd, const char* buf_va, ssize_t bytes);
ssize_t null_write(struct fd* fd, const char* buf_va, ssize_t bytes);
void null_close(struct fd* fd);
int chr_ioctl(struct fd* fd, int cmd, void* ptr);


#endif
