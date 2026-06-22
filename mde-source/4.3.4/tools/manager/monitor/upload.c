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

// General "upload" support.

#include "tools/handy/handy.h"

#include <fcntl.h>

#include <dirent.h>
#include <sys/stat.h>


//! Pending paths (host/tile) for upload.
static StringArray upload_paths;

//! While uploading a file, the fd.
static int upload_fd = -1;

//! While uploading a file, the desired mode.
// ISSUE: Moving this argument from "upload_finish" to "upload_start"
// would simplify the monitor, but complicate the shepherd.
static mode_t upload_mode;

//! HACK: This is called (if non-null) whenever a file upload succeeds.
static void (*upload_callback)(const char* host_path, const char* tile_path);


//! The number of bytes we try to send via "query_upload_append",
//! including the header, path, number of bytes, and actual bytes.
static const size_t upload_want = 128 * 1024;


#ifdef UPLOAD_TIME

static struct timeval upload_time_start;
static uint upload_time_best;
static uint upload_time_worst;
static uint upload_time_total;

#endif


static void
handle_upload_advance(void);


// NOTE: For simplicity, this does not affect the "current" upload.
//
static void
handle_upload_cancel(void)
{
  if (upload_paths.size > 2)
  {
    warn("Aborting partial upload.");
    for (int i = 2; i < upload_paths.size; i++)
      free(upload_paths.data[i]);
    StringArray_excise(&upload_paths, 2, upload_paths.size - 2);
  }
}


static void
handle_upload_finish(bool success)
{
  if (upload_fd >= 0)
  {
    close_or_die(upload_fd);
    upload_fd = -1;
    upload_mode = 0;
  }

  if (success)
  {
    char* host_path = upload_paths.data[0];
    char* tile_path = upload_paths.data[1];

    // ISSUE: For directories, this should actually be done AFTER
    // the directory contents have actually been uploaded, but the
    // actual "upload_callback" pointer ignores directories anyway.
    if (upload_callback != NULL)
      upload_callback(host_path, tile_path);

    StringArray_excise(&upload_paths, 0, 2);

    free(host_path);
    free(tile_path);

    handle_upload_advance();
  }
  else
  {
    if (upload_paths.size > 2)
      warn("Aborting partial upload.");

    // Free the current upload and cancel all pending uploads.
    StringArray_free_and_clear(&upload_paths);
  }
}


static void
handle_upload_reply_file_mkdir(void* info)
{
  char* host_path = upload_paths.data[0];
  char* tile_path = upload_paths.data[1];

  DIR* dir = opendir(host_path);
  if (dir == NULL)
  {
    warn_with_errno("Cannot upload directory '%s'", host_path);
    handle_upload_finish(false);
    return;
  }

  StringArray paths;
  StringArray_init(&paths);

  struct dirent* val;
  while ((val = readdir(dir)) != NULL)
  {
    const char* name = val->d_name;

    if (!strcmp(name, ".") || !strcmp(name, ".."))
      continue;

    StringArray_append(&paths, strfmt_or_die("%s/%s", host_path, name));
    StringArray_append(&paths, strfmt_or_die("%s/%s", tile_path, name));
  }

  if (closedir(dir) != 0)
    punt_with_errno("Failure in 'closedir()'");

  StringArray_splice(&upload_paths, 2, paths.data, paths.size);

  StringArray_destroy(&paths);

  handle_upload_finish(true);
}


static void
handle_upload_reply_upload_start(void* info)
{

#ifdef UPLOAD_TIME

  upload_time_best = 0xFFFFFFFF;
  upload_time_worst = 0;
  upload_time_total = 0;

#endif

  handle_upload_advance();
}


static void
handle_upload_reply_upload_append(void* info)
{

#ifdef UPLOAD_TIME

  struct timeval td;
  timeval_elapsed(&td, &upload_time_start);

  uint elapsed = 1000000 * td.tv_sec + td.tv_usec;

  if (upload_time_best > elapsed)
    upload_time_best = elapsed;

  if (upload_time_worst < elapsed)
    upload_time_worst = elapsed;

  upload_time_total += elapsed;

#endif

  handle_upload_advance();
}


static void
handle_upload_reply_upload_finish(void* info)
{

#ifdef UPLOAD_TIME

  spew(1, "Elapsed best = %d usec worst = %d usec total = %d usec.",
       upload_time_best, upload_time_worst, upload_time_total);

#endif

  handle_upload_finish(true);
}


static void
handle_upload_reply_file_symlink(void* info)
{
  handle_upload_finish(true);
}


static void
handle_upload_error(void* info, char* msg)
{
  warn("Upload error: %s", msg);

  handle_upload_finish(false);
}


static void
handle_upload_advance_start(void)
{
  char* host_path = upload_paths.data[0];
  char* tile_path = upload_paths.data[1];

  struct stat sbuf;
  if (lstat(host_path, &sbuf) != 0)
  {
    warn_with_errno("Cannot upload '%s'", host_path);
    handle_upload_finish(false);
    return;
  }

  mode_t mode = sbuf.st_mode;

  if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode))
  {
    warn("Cannot upload bizarre '%s'.", host_path);
    handle_upload_finish(false);
    return;
  }

  if (S_ISDIR(mode))
  {
    spew(2, "Uploading directory '%s' to '%s'.", host_path, tile_path);

    query_file_mkdir(&g_shepherd_socket,
                     handle_upload_reply_file_mkdir, NULL,
                     handle_upload_error, NULL,
                     tile_path);
  }
  else if (S_ISREG(mode))
  {
    int fd = open(host_path, O_RDONLY);
    if (fd < 0)
    {
      warn_with_errno("Cannot upload file '%s'", host_path);
      handle_upload_finish(false);
      return;
    }

    spew(2, "Uploading file '%s' to '%s'.", host_path, tile_path);

    upload_fd = fd;
    upload_mode = mode;

    query_upload_start(&g_shepherd_socket,
                       handle_upload_reply_upload_start, NULL,
                       handle_upload_error, NULL,
                       tile_path);
  }
  else //--if (S_ISLNK(mode))
  {
    char target[PATH_MAX];
    if (readlink_aux(host_path, target, sizeof(target)) < 0)
    {
      warn_with_errno("Cannot upload symlink '%s'", host_path);
      handle_upload_finish(false);
      return;
    }

    spew(2, "Uploading symlink '%s' to '%s'.", host_path, tile_path);

    query_file_symlink(&g_shepherd_socket,
                       handle_upload_reply_file_symlink, NULL,
                       handle_upload_error, NULL,
                       tile_path, target);
  }
}


static bool
handle_upload_advance_append(void)
{
  char* tile_path = upload_paths.data[1];

  int fd = upload_fd;

  int tile_len = strlen(tile_path) + 1;
  if (tile_len > 1024) tile_len = 1024;
  const size_t want = upload_want - (RPC_HEADER_SIZE + tile_len + 4);

  uint8_t buf[want];

  ssize_t n = read_some_bytes_or_die(fd, buf, want);

  if (n > 0)
  {

#ifdef UPLOAD_TIME

    timeval_now(&upload_time_start);

#endif

    query_upload_append(&g_shepherd_socket,
                        handle_upload_reply_upload_append, NULL,
                        handle_upload_error, NULL,
                        tile_path, buf, n);

    return true;
  }

  return false;
}


static void
handle_upload_advance(void)
{
  // Stop when done.
  if (upload_paths.size == 0)
  {
    spew(1, "Uploading complete.");
    return;
  }

  if (upload_fd < 0)
  {
    // Start the next directory or file or symlink.
    handle_upload_advance_start();
    return;
  }

  // Continue the current file.
  if (handle_upload_advance_append())
    return;

  // Finish the current file.
  char* tile_path = upload_paths.data[1];
  query_upload_finish(&g_shepherd_socket,
                      handle_upload_reply_upload_finish, NULL,
                      handle_upload_error, NULL,
                      tile_path, upload_mode);
}


// Start uploading a directory or file or symlink.
//
static void
handle_upload_start(const char* host_path, const char* tile_path)
{
  spew(3, "Preparing to upload '%s' to '%s'.", host_path, tile_path);

  bool idle = (upload_paths.size == 0);

  StringArray_append(&upload_paths, strdup_or_die(host_path));
  StringArray_append(&upload_paths, strdup_or_die(tile_path));

  // Start the upload, if needed.
  if (idle)
  {
    spew(1, "Uploading...");
    handle_upload_advance();
  }
}
