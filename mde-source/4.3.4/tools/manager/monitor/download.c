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

// General "download" support.

// NOTE: Assumes "upload.c" has already been included (for header files).


//! Pending paths (tile/host) for download.
static StringArray download_paths;

//! While downloading a file, the temp file path.
static char* download_temp;

//! While downloading a file, the temp file fd.
static int download_fd = -1;

//! While downloading a file, the desired mode.
static mode_t download_mode;

//! While downloading a file, the offset into that file.
// FIXME: This should be "uint64_t".
static uint download_offset;


//! The number of bytes we try to get via "file_get_bytes",
//! including the header, number of bytes, and actual bytes.
static const size_t download_want = 128 * 1024;


static void
handle_download_advance(void);


static void
handle_download_finish(bool success)
{
  char* tile_path = download_paths.data[0];
  char* host_path = download_paths.data[1];

  if (download_fd >= 0)
  {
    if (success)
    {
      if (fchmod(download_fd, download_mode) != 0)
      {
        warn_with_errno("Failed to chmod '%s'", download_temp);
        success = false;
      }
    }

    close_or_die(download_fd);

    // ISSUE: Do we need to delete any existing "host_path"?

    if (success && rename(download_temp, host_path) != 0)
    {
      warn_with_errno("Failed to rename '%s' to '%s'",
                      download_temp, host_path);
      success = false;
    }

    if (!success)
      (void)unlink(download_temp);

    download_fd = -1;
    download_mode = 0;
    download_offset = 0;
  }

  if (success)
  {
    StringArray_excise(&download_paths, 0, 2);

    free(tile_path);
    free(host_path);

    handle_download_advance();
  }
  else
  {
    if (download_paths.size > 2)
      warn("Aborting partial download.");

    // Free the current download and cancel all pending downloads.
    StringArray_free_and_clear(&download_paths);
  }
}


static void
handle_download_error(void* info, char* msg)
{
  warn("Download error: %s", msg);

  handle_download_finish(false);
}


static void
handle_download_reply_file_get_children(void* info, StringArray* children)
{
  char* tile_path = download_paths.data[0];
  char* host_path = download_paths.data[1];

  spew(3, "Processing children of '%s'.", tile_path);

  for (uint i = 0; i < children->size; i++)
  {
    char* child = children->data[i];
    spew(3, "Processing child '%s'.", child);

    StringArray_append(&download_paths,
                       strfmt_or_die("%s/%s", tile_path, child));
    StringArray_append(&download_paths,
                       strfmt_or_die("%s/%s", host_path, child));
  }
 
  handle_download_finish(true);
}


static void
handle_download_reply_file_get_bytes(void* info,
                                     uint8_t* bytes, size_t bytes_size)
{
  if (bytes_size != 0)
  {
    write_all_bytes_or_die(download_fd, bytes, bytes_size);
    download_offset += bytes_size;
  }

  // HACK: A partial reply implies end-of-file.
  unsigned int want = download_want - (RPC_HEADER_SIZE + 4);
  if (bytes_size >= want)
  {
    handle_download_advance();
    return;
  }

  handle_download_finish(true);
}


static void
handle_download_reply_file_get_target(void* info, char* target)
{
  //--char* tile_path = download_paths.data[0];
  char* host_path = download_paths.data[1];

  bool failure = false;

  struct stat stats;
  if (lstat(host_path, &stats) == 0)
  {
    spew(2, "Replacing path '%s'.", host_path);
    failure = (unlink(host_path) != 0);
  }

  if (failure || symlink(target, host_path) != 0)
  {
    warn_with_errno("Could not download symlink '%s'", host_path);
    handle_download_finish(false);
    return;
  }

  handle_download_finish(true);
}


static void
handle_download_reply_file_get_mode(void* info, mode_t mode)
{
  char* tile_path = download_paths.data[0];
  char* host_path = download_paths.data[1];

  if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode))
  {
    // Just simply ignore the file.
    download_fd = -1;
    handle_download_finish(true);
    return;
  }

  if (create_ancestors(host_path) != 0)
  {
    warn_with_errno("Could not download '%s'", host_path);
    handle_download_finish(false);
    return;
  }

  if (S_ISDIR(mode))
  {
    if (create_directory(host_path) != 0)
    {
      warn_with_errno("Could not download directory '%s'", host_path);
      handle_download_finish(false);
      return;
    }

    spew(2, "Downloading directory '%s' to '%s'.", tile_path, host_path);

    query_file_get_children(&g_shepherd_socket,
                            handle_download_reply_file_get_children, NULL,
                            handle_download_error, NULL,
                            tile_path);

    return;
  }

  if (S_ISREG(mode))
  {
    spew(2, "Downloading file '%s' to '%s'.", tile_path, host_path);

    download_temp = strfmt_or_die("%s.tmp-XXXXXX", host_path);
    download_fd = mkstemp_boldly(download_temp);
    if (download_fd < 0)
    {
      warn_with_errno("Failure in 'mkstemp(\"%s\")'", download_temp);
      handle_download_finish(false);
      return;
    }
    download_mode = mode;
    handle_download_advance();
  }

  else // if (S_ISLNK(mode))
  {
    spew(2, "Downloading symlink '%s' to '%s'.", tile_path, host_path);

    query_file_get_target(&g_shepherd_socket,
                          handle_download_reply_file_get_target, NULL,
                          handle_download_error, NULL,
                          tile_path);
  }
}


static void
handle_download_advance(void)
{
  // Stop when done.
  if (download_paths.size == 0)
  {
    spew(1, "Downloading complete.");
    return;
  }

  char* tile_path = download_paths.data[0];
  char* host_path = download_paths.data[1];

  if (download_fd < 0)
  {
    // Start download.
    spew(3, "Downloading '%s' to '%s'.", tile_path, host_path);
    query_file_get_mode(&g_shepherd_socket,
                        handle_download_reply_file_get_mode, NULL,
                        handle_download_error, NULL,
                        tile_path);
  }
  else
  {
    unsigned int want = download_want - (RPC_HEADER_SIZE + 4);

    // Continue download.
    query_file_get_bytes(&g_shepherd_socket,
                         handle_download_reply_file_get_bytes, NULL,
                         handle_download_error, NULL,
                         tile_path, download_offset, want);
  }
}


// Start downloading a directory or file or symlink.
//
static void
handle_download_start(const char* tile_path, const char* host_path)
{
  spew(3, "Preparing to download '%s' to '%s'.", tile_path, host_path);

  bool idle = (download_paths.size == 0);

  // Schedule the download.
  StringArray_append(&download_paths, strdup_or_die(tile_path));
  StringArray_append(&download_paths, strdup_or_die(host_path));

  // Start the download, if needed.
  if (idle)
  {
    spew(1, "Downloading...");
    handle_download_advance();
  }
}
