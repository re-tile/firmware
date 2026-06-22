//!
// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.
//
// Definitions for system call handlers.
//
// Note: while this file is a compilable C header file, it is also parsed
// by mktrace.py, which is limited in what it can accept.  Don't put anything
// in here except prototypes for syscall handlers, and make sure they're
// handled by mktrace.py; see comments in that script for details.
//
// @file
//

#ifndef _SYS_BOGUX_SYSCALL_H
#define _SYS_BOGUX_SYSCALL_H

#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <stdint.h>
#include <unistd.h>

#include "bogux.h"
#include "files.h"
#include "mman.h"
#include "stat.h"

// Implemented in syscall.c

int sys_exit(int status);
int sys_gettimeofday(struct timeval* tv, struct timezone* tz);
int sys_getpid(void);
int sys_getcpu(int* cpu, int* node);
int sys_clone(void);
int sys_sched_setaffinity(int pid, unsigned int len, unsigned long* mask);
int sys_sched_getaffinity(int pid, unsigned int len, unsigned long* mask);
int sys_execve(const char* filename, char** argv, char** envp);
int sys_readlinkat(int fd, const char* path, char* buf, size_t buflen);
int sys_times(struct tms* t);
int sys_kill(pid_t pid /* %d */, int sig);
time_t sys_time(time_t* p);
int sys_ni_syscall(int nr);
int sys_dummy_syscall(void);
int sys_atomic(int* mem, int oldval, int newval, int which);
int sys_uname(void* uname);

// Implemented in files.c

int sys_openat(int dfd, const char* file, int flags /* %#x */,
               int mode /* %#o */);
int sys_faccessat(int dfd, const char* path, int mode /* %#x */);
int sys_read(int fd, void* buf, size_t count);
int sys_pread64(int fd, void* buf, size_t count, loff_t offset);
int sys_write(int fd, const void* buf, size_t count);
int sys_pwrite64(int fd, const void* buf, size_t count, loff_t offset);
int sys_lseek (int fd, off_t offset, int whence);
int sys_llseek (int fd, off_t offset_hi, off_t offset_lo,
                loff_t* result, int whence);
int sys_close(int fd);
int sys_fstat(int fd, struct kernel_stat* buf);
int sys_fstatat(int dfd, const char* file, struct kernel_stat* buf, int flag);
int sys_fcntl(int fd, int cmd, void* ptr);
int sys_ioctl(int fd, int cmd, void* ptr);
int sys_errno_rofs(void);

// Implemented in mmap.c

VirtualAddress sys_brk(VirtualAddress new_brk);
int sys_munmap(VirtualAddress address, uint32_t size);
int sys_mprotect(VirtualAddress address, uint32_t size, int32_t protection);
int sys_mbind(VirtualAddress address, uint32_t size, int policy,
              unsigned long* nodemask, unsigned long maxnode, uint32_t flags);
int sys_set_mempolicy(int policy, unsigned long* nodemask,
                      unsigned long maxnode);
int sys_get_mempolicy(int* policy, unsigned long* nodemask,
                      unsigned long maxnode, unsigned long addr,
                      unsigned long flags);
VirtualAddress sys_mmap(VirtualAddress start, size_t length,
                        int prot /* %#x */, int flags /* %#x */, int fd,
                        off_t offset);

#endif /* _SYS_BOGUX_SYSCALL_H */
