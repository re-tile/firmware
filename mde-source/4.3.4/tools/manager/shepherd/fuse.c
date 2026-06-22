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

#include "tools/handy/handy.h"

// The "fuse" RPC calls.
#include "tools/manager/shepherd/gen_rpc_fuse.h"


#define FUSE_USE_VERSION 26

#include <fuse.h>

#include <limits.h>

#include <tmc/task.h>


//! The socket to the watchdog.
static Pollable g_socket;


//! The host directory, with NO trailing slash (except for "/").  This
//! is prepended by all "sock_xxx" functions below to "path", which is
//! relative to the mounted tile directory, and always starts with a
//! slash, and has no trailing slash (except for "/").
//!
static const char* g_host_dir;


//! Combine "g_host_dir" and "path", treating "/" specially.
//!
static char*
hostify(const char* path)
{
  return strfmt_or_die("%s%s", g_host_dir, strcmp(path, "/") ? path : "");
}


static void
handle_packet(RPC rpc)
{
  if (!dispatch_fuse_packet(rpc))
  {
    warn("Ignoring unexpected packet code 0x%04x.", rpc.code);
  }
}


static void
await_result(int* resultp)
{
  Pollable_flush_fully(&g_socket);

  spew(3, "Awaiting result...");

  while (*resultp > 0)
  {
    if (handle_packets_slowly(&g_socket, handle_packet) < 0)
      punt_with_errno("Lost connection to watchdog");
  }
}



// NOTE: All "info" structures must start with "int result", so that
// they can use "handle_error()" and "handle_reply()".

// TODO: Add a simple "querify()" mechanism for simple queries.


static void
handle_error(void* info, char* msg)
{
  int* resultp = (int*)info;

  *resultp = -ENODEV;

  if (has_prefix(msg, "-"))
  {
    int tmp = atoi(msg);
    if (tmp < 0)
      *resultp = tmp;
  }
}


static void
handle_reply(void* info)
{
  int* resultp = (int*)info;

  *resultp = 0;
}



static void*
sock_init(struct fuse_conn_info* fci)
{
  spew(2, "Greeting watchdog.");

  // Notify the watchdog that we are ready.
  // ISSUE: Use a synchronous query?
  do_fuse_init(&g_socket);

  // Fully flush the query, since we have no event loop.
  Pollable_flush_fully(&g_socket);

  return NULL;
}



struct sock_getattr_info {
  int result;
  struct stat* stbuf;
};


static void
sock_getattr_reply(void* info,
                   uint64_t dev,
                   uint64_t ino,
                   uint64_t mode,
                   uint64_t nlink,
                   uint64_t uid,
                   uint64_t gid,
                   uint64_t rdev,
                   uint64_t size,
                   uint64_t blksize,
                   uint64_t blocks,
                   uint64_t atime,
                   uint64_t atimensec,
                   uint64_t mtime,
                   uint64_t mtimensec,
                   uint64_t ctime,
                   uint64_t ctimensec)
{
  struct sock_getattr_info* infop = (struct sock_getattr_info*)info;

  struct stat* stbuf = infop->stbuf;

  stbuf->st_dev = dev;
  stbuf->st_ino = ino;
  stbuf->st_mode = mode;
  stbuf->st_nlink = nlink;
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_rdev = rdev;
  stbuf->st_size = size;
  stbuf->st_blksize = blksize;
  stbuf->st_blocks = blocks;
  stbuf->st_atim.tv_sec = atime;
  stbuf->st_atim.tv_nsec = atimensec;
  stbuf->st_mtim.tv_sec = mtime;
  stbuf->st_mtim.tv_nsec = mtimensec;
  stbuf->st_ctim.tv_sec = ctime;
  stbuf->st_ctim.tv_nsec = ctimensec;

  infop->result = 0;
}


static int
sock_getattr(const char* path, struct stat* stbuf)
{
  char* host_path = hostify(path);

  spew(3, "Handling getattr of '%s'...", host_path);

  struct sock_getattr_info info = {
    .result = 1,
    .stbuf = stbuf
  };

  memset(stbuf, 0, sizeof(*stbuf));

  query_fuse_getattr(&g_socket,
                     sock_getattr_reply, &info,
                     handle_error, &info,
                     host_path);

  await_result(&info.result);

  spew(3, "Got result %d (mode 0x%x).", info.result, stbuf->st_mode);

  free(host_path);

  return info.result;
}



static int
sock_access(const char* path, int mask)
{
  char* host_path = hostify(path);

  spew(2, "Handling access of '%s' (mask %d)...", host_path, mask);

  int result = 1;

  query_fuse_access(&g_socket,
                    handle_reply, &result,
                    handle_error, &result,
                    host_path, mask);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}



struct sock_readdir_info
{
  int result;
  void* buf;
  fuse_fill_dir_t filler;
};


static void
sock_readdir_reply(void* info, StringArray* children)
{
  struct sock_readdir_info* infop = (struct sock_readdir_info*)info;

  for (uint i = 0; i < children->size; i++)
  {
    char* child = StringArray_get(children, i);
    spew(4, "Handling child '%s'...", child);
    (void)infop->filler(infop->buf, child, NULL, 0);
    spew(4, "Handling child '%s'... done.", child);
  }

  infop->result = 0;
}


static int
sock_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  spew(2, "Handling readdir of '%s'...", host_path);

  struct sock_readdir_info info = {
    .result = 1,
    .buf = buf,
    .filler = filler
  };

  query_fuse_readdir(&g_socket,
                     sock_readdir_reply, &info,
                     handle_error, &info,
                     host_path);

  await_result(&info.result);

  spew(2, "Got result %d.", info.result);

  free(host_path);

  return info.result;
}



struct sock_readlink_info {
  int result;
  char* buf;
  size_t size;
};


static void
sock_readlink_reply(void* info, char* target)
{
  struct sock_readlink_info* infop = (struct sock_readlink_info*)info;

  // ISSUE: HACK: Don't worry about truncation.
  snprintf(infop->buf, infop->size, "%s", target);
  infop->buf[infop->size] = '\0';

  infop->result = 0;
}


static int
sock_readlink(const char* path, char* buf, size_t size)
{
  char* host_path = hostify(path);

  spew(2, "Handling readlink of '%s'...", host_path);

  struct sock_readlink_info info = {
    .result = 1,
    .buf = buf,
    .size = size
  };

  // Paranoia?
  buf[0] = '\0';

  query_fuse_readlink(&g_socket,
                      sock_readlink_reply, &info,
                      handle_error, &info,
                      host_path);

  await_result(&info.result);

  spew(2, "Got result %d (%s).", info.result, buf);

  free(host_path);

  return info.result;
}


static int
sock_mknod(const char* path, mode_t mode, dev_t rdev)
{
  char* host_path = hostify(path);

  spew(2, "Handling mknod of '%s' (mode 0x%x)...", host_path, mode);

  int result = 1;

  query_fuse_mknod(&g_socket,
                   handle_reply, &result,
                   handle_error, &result,
                   host_path, mode, rdev);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_mkdir(const char* path, mode_t mode)
{
  char* host_path = hostify(path);

  spew(2, "Handling mkdir of '%s' (mode 0x%x)...", host_path, mode);

  int result = 1;

  query_fuse_mkdir(&g_socket,
                   handle_reply, &result,
                   handle_error, &result,
                   host_path, mode);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_rmdir(const char* path)
{
  char* host_path = hostify(path);

  spew(2, "Handling rmdir of '%s'...", host_path);

  int result = 1;

  query_fuse_rmdir(&g_socket,
                   handle_reply, &result,
                   handle_error, &result,
                   host_path);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_unlink(const char* path)
{
  char* host_path = hostify(path);

  spew(2, "Handling unlink of '%s'...", host_path);

  int result = 1;

  query_fuse_unlink(&g_socket,
                    handle_reply, &result,
                    handle_error, &result,
                    host_path);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_rename(const char* path, const char* newpath)
{
  char* host_path = hostify(path);
  char* host_newpath = hostify(newpath);

  // ISSUE: What if one path is remote and the other is not?

  spew(2, "Handling rename of '%s' to '%s'...", host_path, host_newpath);

  int result = 1;

  query_fuse_rename(&g_socket,
                    handle_reply, &result,
                    handle_error, &result,
                    host_path, host_newpath);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);
  free(host_newpath);

  return result;
}


static int
sock_link(const char* path, const char* newpath)
{
  char* host_path = hostify(path);
  char* host_newpath = hostify(newpath);

  // ISSUE: What if one path is remote and the other is not?

  spew(2, "Handling link of '%s' to '%s'...", host_path, host_newpath);

  int result = 1;

  query_fuse_link(&g_socket,
                  handle_reply, &result,
                  handle_error, &result,
                  host_path, host_newpath);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);
  free(host_newpath);

  return result;
}


// NOTE: The FUSE documentation implies the arguments are reversed,
// but experimentation (and code inspection) shows that they are not.
//
static int
sock_symlink(const char* target, const char* path)
{
  char* host_path = hostify(path);

  spew(2, "Handling symlink of '%s' to '%s'...", host_path, target);

  int result = 1;

  query_fuse_symlink(&g_socket,
                     handle_reply, &result,
                     handle_error, &result,
                     target, host_path);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_chmod(const char* path, mode_t mode)
{
  char* host_path = hostify(path);

  spew(2, "Handling chmod of '%s' (mode 0x%x)...", host_path, mode);

  int result = 1;

  query_fuse_chmod(&g_socket,
                   handle_reply, &result,
                   handle_error, &result,
                   host_path, mode);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_chown(const char* path, uid_t uid, gid_t gid)
{
  char* host_path = hostify(path);

  spew(2, "Handling chown of '%s' (uid %u, gid %u)...", host_path, uid, gid);

  int result = 1;

  query_fuse_chown(&g_socket,
                   handle_reply, &result,
                   handle_error, &result,
                   host_path, uid, gid);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_truncate(const char* path, off_t size)
{
  char* host_path = hostify(path);

  spew(2, "Handling truncate of '%s' (size %llu)...", host_path,
       (unsigned long long)size);

  int result = 1;

  query_fuse_truncate(&g_socket,
                      handle_reply, &result,
                      handle_error, &result,
                      host_path, size);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_utimens(const char* path, const struct timespec ts[2])
{
  char* host_path = hostify(path);

  spew(2, "Handling utimens of '%s'...", host_path);

  int result = 1;

  query_fuse_utimens(&g_socket,
                     handle_reply, &result,
                     handle_error, &result,
                     host_path, ts[0].tv_sec, ts[0].tv_nsec,
                     ts[1].tv_sec, ts[1].tv_nsec);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


struct sock_statvfs_info {
  int result;
  struct statvfs* stbuf;
};


static void
sock_statvfs_reply(void* info,
                   uint64_t bsize,
                   uint64_t frsize,
                   uint64_t blocks,
                   uint64_t bfree,
                   uint64_t bavail,
                   uint64_t files,
                   uint64_t ffree,
                   uint64_t favail,
                   uint64_t fsid,
                   uint64_t flag,
                   uint64_t namemax)
{
  struct sock_statvfs_info* infop = (struct sock_statvfs_info*)info;

  struct statvfs* stbuf = infop->stbuf;

  stbuf->f_bsize = bsize;
  stbuf->f_frsize = frsize;
  stbuf->f_blocks = blocks;
  stbuf->f_bfree = bfree;
  stbuf->f_bavail = bavail;
  stbuf->f_files = files;
  stbuf->f_ffree = ffree;
  stbuf->f_favail = favail;
  stbuf->f_fsid = fsid;
  stbuf->f_flag = flag;
  stbuf->f_namemax = namemax;

  infop->result = 0;
}


// NOTE: This actually does "statvfs()", not "statfs()".
//
// ISSUE: Somebody claims that the 'f_frsize', 'f_favail', 'f_fsid'
// and 'f_flag' fields are "ignored".
//
static int
sock_statfs(const char* path, struct statvfs* stbuf)
{
  char* host_path = hostify(path);

  spew(3, "Handling statvfs of '%s'...", host_path);

  struct sock_statvfs_info info = {
    .result = 1,
    .stbuf = stbuf
  };

  memset(stbuf, 0, sizeof(*stbuf));

  query_fuse_statvfs(&g_socket,
                     sock_statvfs_reply, &info,
                     handle_error, &info,
                     host_path);

  await_result(&info.result);

  spew(3, "Got result %d.", info.result);

  free(host_path);

  return info.result;
}



static int
sock_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  spew(2, "Handling create of '%s' (mode 0x%x, flags 0x%x)...",
       host_path, mode, fi->flags);

  int result = 1;

  query_fuse_create(&g_socket,
                    handle_reply, &result,
                    handle_error, &result,
                    host_path, fi->flags, mode);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


static int
sock_open(const char* path, struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  spew(2, "Handling open of '%s' (flags 0x%x)", host_path, fi->flags);

  int result = 1;

  query_fuse_open(&g_socket,
                  handle_reply, &result,
                  handle_error, &result,
                  host_path, fi->flags);

  await_result(&result);

  spew(2, "Got result %d.", result);

  free(host_path);

  return result;
}


struct sock_read_info {
  int result;
  char* buf;
  int size;
};


static void
sock_read_reply(void* info, uint8_t* bytes, size_t bytes_size)
{
  struct sock_read_info* infop = (struct sock_read_info*)info;

  memcpy(infop->buf, bytes, bytes_size);
  infop->size = bytes_size;

  infop->result = 0;
}


static int
sock_read(const char* path, char* buf, size_t size, off_t offset,
          struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  spew(3, "Handling read of '%s' (size 0x%zx, offset 0x%llx)...",
       host_path, size, (unsigned long long)offset);

  struct sock_read_info info = {
    .result = 1,
    .buf = buf,
    .size = 0
  };

  // HACK: Avoid clipping issues with "(uint)size" below, relying on the
  // fact that the underlying RPC code prevents "huge" data transfers.
  if (size > 0x80000000U)
    size = 0x80000000U;

  query_fuse_read(&g_socket,
                  sock_read_reply, &info,
                  handle_error, &info,
                  host_path, offset, (uint)size);

  await_result(&info.result);

  spew(3, "Got result %d and size %d.", info.result, info.size);

  free(host_path);

  if (info.result < 0)
    return info.result;

  return info.size;
}


struct sock_write_info {
  int result;
  int size;
};


static void
sock_write_reply(void* info, uint size)
{
  struct sock_write_info* infop = (struct sock_write_info*)info;

  infop->size = size;

  infop->result = 0;
}


static int
sock_write(const char* path, const char* buf, size_t size, off_t offset,
           struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  spew(3, "Handling write of '%s' (size 0x%zx, offset 0x%llx)...",
       host_path, size, (unsigned long long)offset);

  struct sock_write_info info = {
    .result = 1,
    .size = 0
  };

  // HACK: Avoid clipping issues with "(uint)size" below, relying on the
  // fact that the underlying RPC code prevents "huge" data transfers.
  // ISSUE: Huge sizes will presumably cause out-of-memory errors, so
  // maybe we should chop the data into smaller pieces?
  if (size > 0x80000000U)
    size = 0x80000000U;

  query_fuse_write(&g_socket,
                   sock_write_reply, &info,
                   handle_error, &info,
                   host_path, offset, (const uint8_t*)buf, size);

  await_result(&info.result);

  spew(3, "Got result %d and size %d.", info.result, info.size);

  free(host_path);

  if (info.result < 0)
    return info.result;

  return info.size;
}


#if 0

// ISSUE: Is this needed?

static int
sock_fsync(const char* path, int isdatasync,
           struct fuse_file_info* fi)
{
  char* host_path = hostify(path);

  int res;

#ifndef HAVE_FDATASYNC
  (void)isdatasync;
#else
  if (isdatasync)
    res = fdatasync(fi->fh);
  else
#endif
    res = fsync(fi->fh);

  free(host_path);

  if (res == -1)
    return -errno;

  return 0;
}

#endif


static struct fuse_operations sock_oper = {
  .getattr = sock_getattr,
  .readlink = sock_readlink,
  // .getdir (deprecated by readdir)
  .mknod = sock_mknod,
  .mkdir = sock_mkdir,
  .unlink = sock_unlink,
  .rmdir = sock_rmdir,
  .symlink = sock_symlink,
  .rename = sock_rename,
  .link = sock_link,
  .chmod = sock_chmod,
  .chown = sock_chown,
  .truncate = sock_truncate,
  // .utime (deprecated by utimens)
  .open = sock_open,
  .read = sock_read,
  .write = sock_write,
  .statfs = sock_statfs,
  // .flush (we auto-flush)
  // .release (we are stateless)
  // .fsync (we auto-flush)
  // .setxattr (could implement)
  // .getxattr (could implement)
  // .listxattr (could implement)
  // .removexattr (could implement)
  // .opendir (should implement)
  .readdir = sock_readdir,
  // .releasedir (we are stateless)
  // .fsyncdir (we auto-flush)
  .init = sock_init,
  // .destroy (we are stateless)
  .access = sock_access,
  .create = sock_create,
  // .ftruncate (falls back to truncate)
  // .fgetattr (falls back to getattr)
  // .lock (should maybe implement)
  .utimens = sock_utimens,
  // .bmap (used only for 'blkdev' filesystems)
};



// NOTE: Caller must strip final slash from "host_dir" (except "/").
//
// NOTE: Caller must create "tile_dir" (and its ancestors) if needed.
//
int
main(int argc, char* argv[])
{
  // Use line buffered stderr.
  setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

  message_prefix = "[shepherd fuse] ";

  // Process args.
  int i;
  for (i = 1; i < argc; )
  {
    const char* arg = argv[i++];

    if (arg[0] != '-')
    {
      i--;
      break;
    }

    else if (!strcmp(arg, "--"))
    {
      break;
    }

    else if (!strcmp(arg, "--verbose"))
    {
      message_verbosity++;
    }

    else
    {
      punt("Unknown option '%s'.", arg);
    }
  }

  if (i + 4 != argc)
    punt("Incorrect usage.");

  char* tile_dir = argv[i++];
  char* host_dir = argv[i++];
  int fd = atoi_or_die(argv[i++]);
  int id = atoi_or_die(argv[i++]);

  // Save the host dir.
  g_host_dir = host_dir;

  const char* tail = (strlen(tile_dir) > 32) ? "..." : "/";
  message_prefix = strfmt_or_die("[shepherd fuse %.32s%s] ", tile_dir, tail);

  spew(2, "Mounting '%s' as '%s'.", host_dir, tile_dir);

  // NOTE: This socket is used only for synchronous RPC queries, and
  // so is always flushed fully, and then read using blocking reads,
  // so there is no need for a "handle_readable" hook.
  Pollable_init(&g_socket, "Socket");
  set_close_on_exec_or_die(fd, true);
  set_blocking_or_die(fd, true);
  Pollable_open(&g_socket, fd, NULL);

  // Use exactly one "tag".
  rpc_set_tag_range(0x8000 + id, 0x8000 + id);

  // Become a fuse daemon.  Use "-s" to forbid threads.  Use "-f" to
  // forbid backgrounding.  Use "-o nonempty" to allow mounting on top
  // of an existing directory.  Use "-o allow_other" to allow file
  // access by non-root users after "su" (see bug 8616).
  // ISSUE: Why do we use "strdup_or_die(tile_dir)"?
  char* base = strdup_or_die(tile_dir);
  char* argv2[8+1] = {
    argv[0], "-s", "-f", "-o", "nonempty", "-o", "allow_other", base, NULL
  };
  int result = fuse_main(8, argv2, &sock_oper, NULL);

  spew(2, "Exiting with code %d.", result);

  exit(result);
}
