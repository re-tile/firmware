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
 * Handle file descriptor-related stuff
 * @file
 */

#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <hv/hypervisor.h>
#include <hv/pagesize.h>

#include "bogux.h"
#include "debug.h"
#include "devices.h"
#include "errno.h"
#include "files.h"
#include "mem_layout.h"
#include "mman.h"
#include "rand.h"
#include "stat.h"
#include "syscall.h"

struct fd ts_descriptors[MAX_DESCRIPTORS] _TILESTATE;



/** Hypervisor filesystem open */
static ssize_t
hvfs_open(struct fd* fd, const char* name)
{
  fd->inode = hv_fs_findfile((HV_VirtAddr) name);
  if (fd->inode < 0)
  {
    assert(fd->inode == HV_ENOENT);
    if ((fd->flags & O_ACCMODE) == O_RDONLY)
      return -ENOENT;
    fd->size = 0;
    fd->mode = 0600 | S_IFREG;
    // Switch this file descriptor to use the pure console fops, since it
    // won't be able to read anything from the hv fs anyway.
    fd->fops = &cons_fops;
  }
  else
  {
    HV_FS_StatInfo statinfo = hv_fs_fstat(fd->inode);
    assert(statinfo.size >= 0);   // we don't expect HV_EBADF here
    fd->size = statinfo.size;
    if (statinfo.flags & HV_FS_ISDIR)
      fd->mode = 0700 | S_IFDIR;
    else
      fd->mode = 0600 | S_IFREG;
  }

  // Register it with a printf for the console front-end to see,
  if ((fd->flags & O_ACCMODE) != O_RDONLY)
    printf("open(): new fd %ld: %s\n", (long)(fd - ts_descriptors), name);

  return 0;
}

/** Hypervisor filesystem read */
static ssize_t
hvfs_read(struct fd* fd, char* va, ssize_t bytes)
{
  int retval = hv_fs_pread(fd->inode, (HV_VirtAddr) va, bytes, fd->offset);

  /* This is documented to return HV_EBADF or HV_EFAULT, but
     neither should be possible since we've prevalidated our arguments. */
  assert(retval >= 0);

  fd->offset += retval;

  return retval;
}

static const struct fops hvfs_fops = {
  .open = hvfs_open,
  .read = hvfs_read,
  .write = cons_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};

/** Array of /proc and /sys file contents, initialized on demand.
 * For now we just do these per-tile, since all the other read/write
 * data managed here is tile-state specific anyway.
 */
#define PROC_TILE_GRID 0
#define SYS_CHIP_WIDTH 1
#define SYS_CHIP_HEIGHT 2
#define PROC_VERSION 3
#define PROC_MEMINFO 4
#define _PROC_ENTRIES 5
static char ts_proc[_PROC_ENTRIES][32] _TILESTATE;

/** Identify Bogux. FIXME: this should be generated dynamically during
 * compilation. I don't have time to do that right now.
 */
static const char version_str[] = "Tilera Bogux v0.9.5\n";

/** /proc open */
static ssize_t
proc_open(struct fd* fd, const char* name)
{
  if (fd->flags != O_RDONLY)
    return -EPERM;
  if (strcmp(name, "/proc/tile/grid") == 0)
  {
    fd->inode = PROC_TILE_GRID;
    if (ts_proc[PROC_TILE_GRID][0] == '\0')
    {
      assert(width < 10 && height < 10);
      ts_proc[PROC_TILE_GRID][0] = '0' + width;
      ts_proc[PROC_TILE_GRID][1] = '\t';
      ts_proc[PROC_TILE_GRID][2] = '0' + height;
      ts_proc[PROC_TILE_GRID][3] = '\n';
      ts_proc[PROC_TILE_GRID][4] = '\0';  // unnecessary, but tidy
    }
  }
  else if (strcmp(name, "/sys/devices/system/cpu/chip_width") == 0)
  {
    fd->inode = SYS_CHIP_WIDTH;
    if (ts_proc[SYS_CHIP_WIDTH][0] == '\0')
    {
      assert(width < 10);
      ts_proc[SYS_CHIP_WIDTH][0] = '0' + width;
      ts_proc[SYS_CHIP_WIDTH][1] = '\n';
      ts_proc[SYS_CHIP_WIDTH][2] = '\0';  // unnecessary, but tidy
    }
  }
  else if (strcmp(name, "/sys/devices/system/cpu/chip_height") == 0)
  {
    fd->inode = SYS_CHIP_HEIGHT;
    if (ts_proc[SYS_CHIP_HEIGHT][0] == '\0')
    {
      assert(height < 10);
      ts_proc[SYS_CHIP_HEIGHT][0] = '0' + height;
      ts_proc[SYS_CHIP_HEIGHT][1] = '\n';
      ts_proc[SYS_CHIP_HEIGHT][2] = '\0';  // unnecessary, but tidy
    }
  }
  else if (strcmp(name, "/proc/tile/grid") == 0)
  {
    fd->inode = PROC_TILE_GRID;
    if (ts_proc[PROC_TILE_GRID][0] == '\0')
    {
      assert(width < 10 && height < 10);
      ts_proc[PROC_TILE_GRID][0] = '0' + width;
      ts_proc[PROC_TILE_GRID][1] = '\t';
      ts_proc[PROC_TILE_GRID][2] = '0' + height;
      ts_proc[PROC_TILE_GRID][3] = '\n';
      ts_proc[PROC_TILE_GRID][4] = '\0';  // unnecessary, but tidy
    }
  }
  else if (strcmp(name, "/proc/version") == 0)
  {
    fd->inode = PROC_VERSION;
    if (ts_proc[PROC_VERSION][0] == '\0')
    {
      strncpy(ts_proc[PROC_VERSION], version_str,
              sizeof(ts_proc[PROC_VERSION]));
    }
  }
  else if (strcmp(name, "/proc/meminfo") == 0)
  {
    fd->inode = PROC_MEMINFO;
    if (ts_proc[PROC_MEMINFO][0] == '\0')
    {
      snprintf(ts_proc[PROC_MEMINFO], sizeof(ts_proc[PROC_MEMINFO]),
               "Hugepagesize:   %8lu kB\n", HV_PAGE_SIZE_LARGE / 1024);
    }
  }
  else
    return -ENOENT;

  fd->size = strlen(ts_proc[fd->inode]);
  fd->mode = 0400 | S_IFREG;
  return 0;
}

/** /proc read */
static ssize_t
proc_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  int max = (fd->offset >= fd->size) ? 0 : (fd->size - fd->offset);
  if (bytes > max)
    bytes = max;
  memcpy(buf_va, &ts_proc[fd->inode][fd->offset], bytes);
  fd->offset += bytes;
  return bytes;
}

static const struct fops proc_fops = {
  .open = proc_open,
  .read = proc_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


/** Map a name to a struct fd. */
int
do_open(const char *file, int flags, int mode)
{
  // We don't support non-blocking I/O
  if (flags & O_NONBLOCK)
    return -EINVAL;

  // See if we have a spare file descriptor
  int index = 0;
  while (ts_descriptors[index].fops != NULL)
  {
    ++index;
    if (index >= MAX_DESCRIPTORS)
      return -ENFILE;
  }

  // Save away info in the fd
  struct fd* fd = &ts_descriptors[index];
  fd->flags = flags;
  fd->mode = mode;
  fd->inode = 0;
  fd->offset = 0;

  // Figure out which file operations to use.
  if (strncmp(file, "/dev/", 5) == 0)
  {
    fd->fops = find_device(file, flags, mode);
    if (fd->fops == NULL)
    {
      // Back out our use of this file descriptor
      sys_close(index);
      return -ENOENT;
    }
  }
  else if (strncmp(file, "/proc/", 6) == 0 || strncmp(file, "/sys/", 5) == 0)
    fd->fops = &proc_fops;
  else
    fd->fops = &hvfs_fops;

  // Do any extra open processing
  ssize_t retval = fd->fops->open(fd, file);
  if (retval < 0)
  {
    // Back out our use of this file descriptor
    sys_close(index);
    return retval;
  }

  return index;
}

int
sys_openat(int dfd, const char *file, int flags, int mode)
{
  // Validate the path to the file
  if (!is_valid_user_string(file, PROT_READ))
    return -EFAULT;

  SYSCALL_TRACE("open: filename is %s\n", file);

  return do_open(file, flags, mode);
}


/** Return pointer to struct fd, if allocated. */
struct fd*
get_fd(int index)
{
  if (index < 0 || index >= MAX_DESCRIPTORS)
    return NULL;
  struct fd* fd = &ts_descriptors[index];
  return fd->fops ? fd : NULL;
}


int
sys_faccessat(int dfd, const char *file, int mode)
{
  // Validate the path to the file
  if (!is_valid_user_string(file, PROT_READ))
    return -EFAULT;

  SYSCALL_TRACE("access: filename is %s\n", file);

  // We treat X_OK as R_OK since we don't have any true execute bit
  // support in any of our filesystems.
  if (mode & X_OK)
    mode |= R_OK;
  mode &= (R_OK|W_OK);

  // Do a simple-minded mapping of access mode to open flags.
  // Since our underlying filesystem switch only supports open as
  // an accessor, for now this is all we can do, and is probably
  // good enough anyway.  For F_OK (i.e. no bits set) we want
  // to try both with O_RDONLY and O_WRONLY and see if either works.
  int fd = -1;
  if (mode == (R_OK|W_OK))
    fd = do_open(file, O_RDWR, 0);
  else
  {
    if (!(mode & W_OK))
      fd = do_open(file, O_RDONLY, 0);
    if (fd < 0 && !(mode & R_OK))
      fd = do_open(file, O_WRONLY, 0);
  }

  if (fd < 0)
    return fd;

  sys_close(fd);
  return 0;
}


/** Read from a file descriptor. */
int
do_read(int index, void* buf, size_t count)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  if ((fd->flags & O_ACCMODE) == O_WRONLY)
    return -EBADF;

  return fd->fops->read(fd, buf, count);
}

int
sys_read(int index, void* buf, size_t count)
{
  if (!is_valid_user_buf(buf, count, PROT_WRITE))
    return -EFAULT;

  return do_read(index, buf, count);
}

int
do_pread(int index, void* buf, size_t count, loff_t offset)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  if ((fd->flags & O_ACCMODE) == O_WRONLY)
    return -EBADF;

  loff_t prevoffset = fd->offset;
  fd->offset = offset;
  int retval = fd->fops->read(fd, buf, count);
  fd->offset = prevoffset;
  return retval;
}

/** Read from a file descriptor at a given offset. */
int
sys_pread64(int index, void* buf, size_t count, loff_t offset)
{
  if (!is_valid_user_buf(buf, count, PROT_WRITE))
    return -EFAULT;

  return do_pread(index, buf, count, offset);
}


/** Write to a file descriptor. */
int
sys_write(int index, const void* buf, size_t count)
{
  if (!is_valid_user_buf(buf, count, PROT_READ))
    return -EFAULT;

  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  if ((fd->flags & O_ACCMODE) == O_RDONLY)
    return -EBADF;

  return fd->fops->write(fd, buf, count);
}


int
do_pwrite(int index, const void* buf, size_t count, loff_t offset)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  if ((fd->flags & O_ACCMODE) == O_RDONLY)
    return -EBADF;

  loff_t prevoffset = fd->offset;
  fd->offset = offset;
  int retval = fd->fops->write(fd, buf, count);
  fd->offset = prevoffset;
  return retval;
}


/** Write to a file descriptor at a given offset. */
int
sys_pwrite64(int index, const void* buf, size_t count, loff_t offset)
{
  if (!is_valid_user_buf(buf, count, PROT_READ))
    return -EFAULT;

  return do_pwrite(index, buf, count, offset);
}


/* Only used on 64-bit, but always provide it, to simplify mktrace logic. */
int
sys_lseek (int index, off_t offset, int whence)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;
  off_t base;
  switch (whence)
  {
  case SEEK_SET:
    base = 0;
    break;
  case SEEK_CUR:
    base = fd->offset;
    break;
  case SEEK_END:
    base = fd->size;
    break;
  default:
    return -EINVAL;
  }

  if (base + offset < 0)
    return -EINVAL;
  fd->offset = base + offset;
  return fd->offset;
}


/* Only used on 32-bit, but always provide it, to simplify mktrace logic. */
int
sys_llseek(int index, off_t offset_hi, off_t offset_lo,
           loff_t* result, int whence)
{
  loff_t offset = ((loff_t) offset_hi << 32) | offset_lo;

  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  loff_t base;
  switch (whence)
  {
  case SEEK_SET:
    base = 0;
    break;
  case SEEK_CUR:
    base = fd->offset;
    break;
  case SEEK_END:
    base = fd->size;
    break;
  default:
    return -EINVAL;
  }

  if (base + offset < 0)
    return -EINVAL;

  fd->offset = base + offset;

  if (!is_valid_user_buf(result, sizeof(*result), PROT_WRITE))
    return -EFAULT;

  *result = fd->offset;
  return 0;
}


/** Return information on a file descriptor */
int
do_fstat(int index, struct kernel_stat* buf)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  struct kernel_stat* sb = buf;

  // Mostly zeros, but we have some data.
  sb->st_dev = 0;
  sb->st_ino = 0;
  sb->st_mode = fd->mode;
  sb->st_nlink = 1;
  sb->st_uid = 0;
  sb->st_gid = 0;
  sb->st_rdev = 0;
  sb->st_size = fd->size;
  sb->st_blksize = HV_PAGE_SIZE_SMALL;
  sb->st_blocks = 0;
  sb->st_atime = sb->st_mtime = sb->st_ctime = 0;
  sb->st_atime_nsec = sb->st_mtime_nsec = sb->st_ctime_nsec = 0;
  sb->__unused4 = sb->__unused5 = 0;

  return 0;
}


/** Return information on a file descriptor */
int
sys_fstat(int index, struct kernel_stat* buf)
{
  if (!is_valid_user_buf(buf, sizeof(*buf), PROT_WRITE))
    return -EFAULT;

  return do_fstat(index, buf);
}


/** Return information on a file name */
int
sys_fstatat(int dfd, const char* file, struct kernel_stat* buf, int flag)
{
  // Validate the user pointers
  if (!is_valid_user_string(file, PROT_READ) ||
      !is_valid_user_buf(buf, sizeof(*buf), PROT_WRITE))
    return -EFAULT;

  SYSCALL_TRACE("stat: filename is %s\n", file);

  int fd = do_open(file, O_RDONLY, 0);
  if (fd < 0)
    return fd;

  int rc = do_fstat(fd, buf);
  sys_close(fd);
  return rc;
}


/** Perform a misc file operation */
int
sys_fcntl(int index, int cmd, void* arg)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  switch (cmd)
  {
  case 1:  // F_GETFD
    return 0;        // we don't close on exec right now
  }

  return -EINVAL;
}

/** Perform a misc I/O operation */
int
sys_ioctl(int index, int cmd, void* ptr)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;
  return fd->fops->ioctl(fd, cmd, ptr);
}


/** Close a file descriptor. */
int
sys_close(int index)
{
  struct fd* fd = get_fd(index);
  if (fd == NULL)
    return -EBADF;

  // Do any file-dependent close operations
  if (fd->fops != NULL)
    fd->fops->close(fd);

  // Clear fd structure
  fd->fops = NULL;
  fd->inode = fd->mode = 0;
  fd->size = fd->offset = 0;

  return 0;
}


/** Close all file descriptors. */
void
do_closeall()
{
  for (int index = 0; index < MAX_DESCRIPTORS; ++index)
    if (ts_descriptors[index].fops != NULL)
      sys_close(index);
}


/** Return error for any routine that tries to alter directory-type info.
 * None of our filesystems support updating like this.
 */
int
sys_errno_rofs()
{
  return -EROFS;
}
