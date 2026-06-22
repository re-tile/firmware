// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

#include <sys/types.h>
#include <tmc/alloc.h>
#include <tmc/sync.h>

struct list_head
{
  struct list_head *next;
  struct list_head *prev;
};

// Struct tagged onto each free page.
struct shmem_free
{
  // MAGIC_1 value.
  unsigned long magic1;

  // Offset in the underlying file.
  off_t offset;

  // Size of this chunk (multiple of pages).
  // If zero, then list.next points to the head shmem_free
  // with info for a multipage chunk.
  size_t size;

  // Linked list of free chunks.
  struct list_head list;

  // MAGIC_2 value.
  unsigned long magic2;
};

#define SHMEM_FREE_MAGIC_1 0x3987ae46
#define SHMEM_FREE_MAGIC_2 0x7ae46398

// Description of first page of primary file for tmc_shmem arenas.
struct tmc_shmem_file {

  // Identify file as tmc_shmem and flag endianness and wordlength.
  int cookie;

  // Version of file data.
  int version;

  // Page size that is being allocated in this arena.
  size_t pagesize;

  // tmc_alloc object to specify how arena file is mapped.
  tmc_alloc_t alloc;

  // Address at which this object should be mapped.
  void* addr;

  // Maximum size usable by this tmc_shmem_t.
  size_t maxsize;

  // Current size of this shmem_t (equivalent to stat() length of file).
  size_t cursize;

  // Lock for looking at cursize and calling ftruncate().
  tmc_sync_mutex_t lock;

  // List head for list of free pages.
  struct list_head freelist;

  // For huge pages only: path to hugepage file holding the data.
  char hugepath[PATH_MAX];

  // For tmc_cmem's mspace APIs, non-NULL in the generic small page shmem only.
  void* mspace;

  // For tmc_cmem, if we have persisted the shmems of which this is the first.
  int persisted;

  // Generic pointer value for applications.
  void* data;
};

// Per-process handle for an open tmc_shmem.
struct tmc_shmem {

  // Copy of tmc_alloc object from tmc_shmem_file.
  // Necessary to allow tmc_shmem to work as a tmc_alloc_t allocator.
  // NOTE: We currently require this to be the first thing in the tmc_shmem
  // object for ease of implementation in tmc_shmem_alloc_*().
  tmc_alloc_t alloc;

  // Pointer to mapped primary file.
  struct tmc_shmem_file* file;

  // File descriptor for mapped primary file.
  int fd;

  // If using huge pages, file descriptor for huge page file,
  // mapped at file->addr.  Otherwise -1.
  int hugefd;

  // Link for next tmc_shmem when used with tmc_cmem.
  struct tmc_shmem* next;
};

// Internal API for use by cmem.
tmc_shmem_t* tmc_shmem_open_fds(int fd, int hugefd);
