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

#include "common.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

// ISSUE: If we uses a hardcoded suffix for the temp files, and assumed
// that no conflicting uploads will occur, we would not need any "state".

// The uploads currently in progress.  For each, we track the original
// "path" and a "temp" path and an actual "fd".
static StringArray g_upload_path_array;
static StringArray g_upload_temp_array;
static Array g_upload_fd_array;


static int
upload_lookup(char* path)
{
  return StringArray_lookup(&g_upload_path_array, path);
}


static void
upload_excise(int i)
{
  free(StringArray_get(&g_upload_path_array, i));
  free(StringArray_get(&g_upload_temp_array, i));

  StringArray_excise(&g_upload_path_array, i, 1);
  StringArray_excise(&g_upload_temp_array, i, 1);
  Array_excise(&g_upload_fd_array, i, 1);
}


static void
upload_cancel(int i)
{
  char* temp = StringArray_get(&g_upload_temp_array, i);
  int fd = (int)(long)Array_get(&g_upload_fd_array, i);

  close_or_die(fd);

  if (unlink(temp) != 0)
    warn("Failed to unlink '%s'.", temp);

  upload_excise(i);
}


void
perform_upload_start(RPC rpc, char* path)
{
  if (upload_lookup(path) >= 0)
  {
    rpc_error(rpc, "Invalid query 'upload_start(\"%s\")'.", path);
    return;
  }

  if (create_ancestors(path) != 0)
  {
    rpc_error_with_errno(rpc, "Could not create ancestors of '%s'", path);
    return;
  }

  char* copy = strdup_or_die(path);
  char* temp = strfmt_or_die("%s.tmp-XXXXXX", path);

  int fd = mkstemp_boldly(temp);
  if (fd < 0)
  {
    rpc_error_with_errno(rpc, "Could not create temp file '%s'", temp);
    return;
  }

  StringArray_append(&g_upload_path_array, copy);
  StringArray_append(&g_upload_temp_array, temp);
  Array_append(&g_upload_fd_array, (void*)(intptr_t)fd);

  reply_upload_start(rpc);
}


void
perform_upload_append(RPC rpc, char* path, uint8_t* bytes, size_t bytes_size)
{
  int i = upload_lookup(path);
  if (i < 0)
  {
    rpc_error(rpc, "Invalid query 'upload_append(\"%s\")'.", path);
    return;
  }

  int fd = (int)(intptr_t)Array_get(&g_upload_fd_array, i);

  size_t total = write_all_bytes(fd, bytes, bytes_size);
  if (total != bytes_size)
  {
    int err = errno;

    upload_cancel(i);

    rpc_error(rpc, "Only wrote %zu/%zu bytes: %s",
              total, bytes_size, strerror(err));
    return;
  }

  reply_upload_append(rpc);
}


void
perform_upload_finish(RPC rpc, char* path, uint mode)
{
  int i = upload_lookup(path);
  if (i < 0)
  {
    rpc_error(rpc, "Invalid query 'upload_finish(\"%s\")'.", path);
    return;
  }

  char* temp = StringArray_get(&g_upload_temp_array, i);
  int fd = (int)(long)Array_get(&g_upload_fd_array, i);

  bool failure = false;

  if (fchmod(fd, mode) != 0)
  {
    failure = true;
    rpc_error_with_errno(rpc, "Failed to chmod '%s'", temp);
  }

  close_or_die(fd);

  struct stat stats;
  if (!failure && lstat(path, &stats) == 0)
  {
    spew(2, "Replacing '%s'.", path);
    if (unlink(path) != 0)
    {
      failure = true;
      rpc_error_with_errno(rpc, "Failed to replace '%s'", path);
    }
  }

  if (!failure && rename(temp, path) != 0)
  {
    failure = true;
    rpc_error_with_errno(rpc, "Failed to rename '%s' to '%s'", temp, path);
  }

  if (failure)
    (void)unlink(temp);

  upload_excise(i);

  if (failure)
    return;

  reply_upload_finish(rpc);
}


void
perform_upload_cancel(RPC rpc, char* path)
{
  int i = upload_lookup(path);
  if (i < 0)
  {
    rpc_error(rpc, "Invalid query 'upload_cancel(\"%s\")'.", path);
    return;
  }

  upload_cancel(i);

  reply_upload_cancel(rpc);
}



void
perform_file_write(RPC rpc, char* path, uint8_t* bytes, size_t bytes_size)
{
  if (create_ancestors(path) != 0)
  {
    rpc_error_with_errno(rpc, "Could not create ancestors of '%s'", path);
    return;
  }

  int fd = open_or_die(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  write_all_bytes_or_die(fd, bytes, bytes_size);
  close_or_die(fd);

  reply_file_write(rpc);
}


void
perform_file_rmdir(RPC rpc, char* path)
{
  if (rmdir(path) < 0)
  {
    rpc_error_with_errno(rpc, "Could not rmdir '%s'", path);
    return;
  }

  reply_file_rmdir(rpc);
}


void
perform_file_unlink(RPC rpc, char* path)
{
  if (unlink(path) < 0)
  {
    rpc_error_with_errno(rpc, "Could not unlink '%s'", path);
    return;
  }

  reply_file_unlink(rpc);
}


void
perform_file_mkdir(RPC rpc, char* path)
{
  if (create_ancestors(path) != 0 || create_directory(path) != 0)
  {
    rpc_error_with_errno(rpc, "Could not mkdir '%s'", path);
    return;
  }

  reply_file_mkdir(rpc);
}


void
perform_file_symlink(RPC rpc, char* path, char* target)
{
  if (create_ancestors(path) != 0)
  {
    rpc_error_with_errno(rpc, "Could not create ancestors of '%s'", path);
    return;
  }

  struct stat stats;
  if (lstat(path, &stats) == 0)
  {
    spew(2, "Replacing '%s'.", path);
    if (unlink(path) != 0)
    {
      rpc_error_with_errno(rpc, "Failed to replace '%s'", path);
      return;
    }
  }

  if (symlink(target, path) != 0)
  {
    rpc_error_with_errno(rpc, "Could not symlink '%s' to '%s'", path, target);
    return;
  }

  reply_file_symlink(rpc);
}


void
perform_file_rename(RPC rpc, char* path, char* newpath)
{
  if (create_ancestors(newpath) != 0)
  {
    rpc_error_with_errno(rpc, "Could not create ancestors of '%s'", newpath);
    return;
  }

  if (rename(path, newpath) != 0)
  {
    rpc_error_with_errno(rpc, "Could not rename '%s' to '%s'", path, newpath);
    return;
  }

  reply_file_rename(rpc);
}


void
perform_file_get_mode(RPC rpc, char* path)
{
  struct stat stats;
  if (lstat(path, &stats) != 0)
  {
    rpc_error_with_errno(rpc, "Failure in 'lstat(\"%s\")'", path);
    return;
  }

  mode_t mode = stats.st_mode;

  reply_file_get_mode(rpc, mode);
}


void
perform_file_get_children(RPC rpc, char* path)
{
  DIR* dir = opendir(path);
  if (dir == NULL)
  {
    rpc_error_with_errno(rpc, "Could not get children of '%s'", path);
    return;
  }

  StringArray children;
  StringArray_init(&children);

  struct dirent* val;
  while ((val = readdir(dir)) != NULL)
  {
    if (!strcmp(val->d_name, ".") || !strcmp(val->d_name, ".."))
      continue;

    StringArray_append(&children, strdup_or_die(val->d_name));
  }

  if (closedir(dir) != 0)
  {
    rpc_error_with_errno(rpc, "Could not get children of '%s'", path);
  }
  else
  {
    reply_file_get_children(rpc, &children);
  }

  StringArray_free_and_clear(&children);
  StringArray_destroy(&children);
}


void
perform_file_get_size(RPC rpc, char* path)
{
  struct stat stats;
  if (stat(path, &stats) != 0)
  {
    rpc_error_with_errno(rpc, "Failure in 'stat(\"%s\")'", path);
    return;
  }

  reply_file_get_size(rpc, stats.st_size);
}


static char* g_file_get_bytes_path;
static int g_file_get_bytes_fd;
static uint64_t g_file_get_bytes_offset;


static void
file_get_bytes_close(void)
{
  if (g_file_get_bytes_path == NULL)
    return;

  free(g_file_get_bytes_path);
  g_file_get_bytes_path = NULL;

  close_or_die(g_file_get_bytes_fd);
  g_file_get_bytes_fd = -1;

  g_file_get_bytes_offset = 0;
}


void
perform_file_get_bytes(RPC rpc, char* path, uint64_t offset, uint length)
{
  int fd = g_file_get_bytes_fd;

  if (g_file_get_bytes_path == NULL ||
      strcmp(g_file_get_bytes_path, path) != 0)
  {
    file_get_bytes_close();

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
      rpc_error_with_errno(rpc, "Failure in 'open()'");
      return;
    }

    g_file_get_bytes_path = strdup_or_die(path);
    g_file_get_bytes_fd = fd;
  }

  if (g_file_get_bytes_offset != offset)
  {
    if (lseek(fd, SEEK_SET, offset) != offset)
    {
      rpc_error_with_errno(rpc, "Failure in 'seek()'");
      return;
    }

    g_file_get_bytes_offset = offset;
  }

  uint8_t buf[length];
  ssize_t n = read_some_bytes_or_die(fd, buf, length);
  if (n < 0)
    n = 0;

  g_file_get_bytes_offset += n;

  // Close now if monitor will assume EOF due to short read.
  if (n < length)
    file_get_bytes_close();

  reply_file_get_bytes(rpc, buf, n);
}


void
perform_file_get_target(RPC rpc, char* path)
{
  char target[PATH_MAX];

  if (readlink_aux(path, target, sizeof(target)) < 0)
  {
    rpc_error_with_errno(rpc, "Failure in 'readlink_aux(\"%s\")'", path);
    return;
  }

  reply_file_get_target(rpc, target);
}
