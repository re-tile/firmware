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
 * Header for stat() return type.
 * @file
 */

#ifndef __SYS_BOGUX_STAT_H__
#define __SYS_BOGUX_STAT_H__

struct kernel_stat {
	unsigned long long st_dev;
	unsigned long long st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int 	st_uid;
	unsigned int 	st_gid;
	unsigned long long st_rdev;
	unsigned long long __pad1;
	long long	st_size;
	int		st_blksize;
	int		__pad2;
	long long	st_blocks;
	int		st_atime;
	unsigned int	st_atime_nsec;
	int		st_mtime;
	unsigned int	st_mtime_nsec;
	int		st_ctime;
	unsigned int	st_ctime_nsec;
	unsigned int	__unused4;
	unsigned int	__unused5;
};

#endif
