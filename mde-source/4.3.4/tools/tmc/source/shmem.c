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
//
// Provide a process-shared memory.  The model is to used mmap'ed files
// as the basis for sharing.  Using mmap with MAP_NORESERVE, we can allocate
// a large chunk of address space ahead of time, and by mapping that same
// chunk in every participating process, we can share memory.  Then, by
// initially truncating the file to zero, no memory is valid in the shared
// range.  (Note that hugetlbfs files initially grow to the full size of
// the mapping, so the truncation is necessary in that case.)
//
// As we need to allocate memory into the arenas, we can simply "truncate"
// the file to a larger size, then make a syscall that writes the new memory
// to ensure that is is available (returning EFAULT rather than generating
// a signal if it is not available due to out-of-memory or whatever).
// If we give back memory by truncating the file smaller, the OS will
// properly do TLB shootdowns on all participating processes to remove
// the page, so we preserve a coherent view.
//
// Similarly, if we want to change the homecache of a page, we can simply
// create a temporary mapping of a single page with the desired home cache,
// and the OS will also properly do TLB shootdowns to ensure that all
// participating processes see the home cache change.
//
// There are no other kinds of changes we can make to the mapped memory
// that are automatically propagated to all participating processes by the OS,
// so we limit ourselves to these types only, rather than looking to other
// models (signals and the like) to notify processes of changes.

//! Ensure we can handle large files.
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/times.h>

#include <tmc/shmem.h>

#include "shmem_internal.h"

//! Rounds up "N" to the next multiple of "A" (a power of two).
#define ROUND_UP(N, A) (((N) + (A) - 1) & -(A))

// Newlib support.
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif

// Older kernel support.
#ifndef MAP_HUGETLB
# define MAP_HUGETLB 0
#endif

#ifdef _LP64
# ifdef __LITTLE_ENDIAN__
#  define TMC_SHMEM_COOKIE 0x234af810
# else
#  define TMC_SHMEM_COOKIE 0x234af811
# endif
#else
# ifdef __LITTLE_ENDIAN__
#  define TMC_SHMEM_COOKIE 0x234af812
# else
#  define TMC_SHMEM_COOKIE 0x234af813
# endif
#endif
#define TMC_SHMEM_VERSION 1
#define TMC_SHMEM_HDRLEN (64 * 1024)

// mmap the passed file descriptor as described by alloc, addr, and maxsize,
// and return the address of the resulting mapping, or MAP_FAILED on failure.
static void*
tmc_shmem_setup(int fd, const tmc_alloc_t* alloc,
                void* addr, size_t maxsize)
{
  // Set up mmap flags.  We force SHARED on and PRIVATE off, and also clear
  // POPULATE, since we'll be mapping the file into a much larger range of
  // address space than the size of the actual file.
  // Note that we use MAP_POPULATE by hand to decide whether to explicitly
  // try to bring pages into the page table here as we allocate them.
  // And note that we use MAP_HUGETLB early for tmc_alloc_get_pagesize().
  // We also force off all the inappropriate mmap_flags.
  int mmap_flags = alloc->mmap_flags;
  mmap_flags &=
    ~(MAP_PRIVATE | MAP_POPULATE | MAP_FIXED | MAP_ANONYMOUS | MAP_HUGETLB |
      MAP_GROWSDOWN | MAP_EXECUTABLE | MAP_DENYWRITE);
  mmap_flags |= MAP_SHARED;

  // Force NORESERVE since we are intentionally making a mapping much
  // larger than we necessarily expect to be able to use.  This is
  // particularly important for huge pages, since if we don't do this,
  // the system will try to reserve enough huge pages to cover the
  // entire mmapped region.
  mmap_flags |= MAP_NORESERVE;

  void* rc = tmc_alloc_mmap_mbind(addr, maxsize, alloc->mmap_prot,
                                  mmap_flags, fd, 0, alloc->mbind_policy,
                                  alloc->mbind_nodemask);

  // If the mapping was meant to be at a particular address and the
  // OS couldn't put it there, unmap it and return an error.
  if (addr && rc != addr)
  {
    munmap(rc, maxsize);
    errno = EINVAL;
    return MAP_FAILED;
  }
    
  return rc;
}


// Forward declarations.
static void*
tmc_shmem_alloc_mmap_mbind(const tmc_alloc_t* alloc,
                           void* start, size_t length);
static int
tmc_shmem_alloc_unmap(const tmc_alloc_t* alloc, void* addr, size_t length);
static void*
tmc_shmem_alloc_remap(const tmc_alloc_t* alloc,
                      void *old_address, size_t old_length,
                      size_t new_length, int flags, void* new_address);

static tmc_shmem_t*
create_handle(struct tmc_shmem_file* file, int fd, int hugefd)
{
  tmc_shmem_t* shmem = malloc(sizeof(tmc_shmem_t));
  if (shmem == NULL)
    return NULL;
  shmem->alloc = file->alloc;
  shmem->alloc.mmap_mbind_func = tmc_shmem_alloc_mmap_mbind;
  shmem->file = file;
  shmem->fd = fd;
  shmem->hugefd = hugefd;

  // Register this handle's address range with tmc_alloc.
  if (tmc_alloc_register_address_range(&shmem->alloc, file->addr, file->maxsize,
                                       tmc_shmem_alloc_unmap,
                                       tmc_shmem_alloc_remap) < 0)
  {
    free(shmem);
    return NULL;
  }

  return shmem;
}

static tmc_shmem_t*
tmc_shmem_create_path_fd(const char* path, int fd, const tmc_alloc_t* alloc,
                         void* addr, size_t maxsize)
{
  struct tmc_shmem_file* file = MAP_FAILED;
  int hugefd = -1;

  // To start with, size the file to a fixed size.
  int rc = ftruncate(fd, TMC_SHMEM_HDRLEN);
  if (rc < 0)
    goto fail;

  // Provide a default tmc_alloc_t if necessary.
  static const tmc_alloc_t default_tmc_alloc_t = TMC_ALLOC_INIT;
  if (alloc == NULL)
    alloc = &default_tmc_alloc_t;

  // Provide a default size if necessary; round up to page size.
  size_t pagesize = tmc_alloc_get_pagesize(alloc);
  if (maxsize == 0)
    maxsize = 1024 * 1024 * 1024UL;
  maxsize = ROUND_UP(maxsize, pagesize);

  // You can't ask for unaligned starting addresses.
  if ((unsigned long) addr & (pagesize - 1))
  {
    errno = EINVAL;
    goto fail;
  }

  if (pagesize != getpagesize())
  {
    // Map the short primary file at a random, final address.
    file = mmap(NULL, TMC_SHMEM_HDRLEN, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
    if (file == MAP_FAILED)
      goto fail;

    // Create the huge page file and map it at the requested address.
    __tmc_get_huge_file_dir(alloc, file->hugepath);
    strcat(file->hugepath, "/tmc_shmem_XXXXXX");
    hugefd = mkstemp(file->hugepath);
    if (hugefd < 0)
      goto fail;

    // Truncate the file back to zero again.  The system automatically
    // grows the file to the size of the mapping, but we don't want that
    // semantic; we'd prefer to get SIGBUS for out-of-range mappings,
    // and explicitly ftruncate() the file bigger as we allocate.
    rc = ftruncate(hugefd, 0);
    if (rc < 0)
      goto fail;

    // Map the hugepage file into memory.
    addr = tmc_shmem_setup(hugefd, alloc, addr, maxsize);
    if (addr == MAP_FAILED)
      goto fail;
  }
  else
  {
    // Map the primary file at the requested address.
    addr = tmc_shmem_setup(fd, alloc, addr, maxsize);
    if (addr == MAP_FAILED)
      goto fail;
    file = addr;
    file->cursize = TMC_SHMEM_HDRLEN;
  }

  // Initialize the file object.
  file->cookie = TMC_SHMEM_COOKIE;
  file->version = TMC_SHMEM_VERSION;
  file->pagesize = pagesize;
  file->alloc = *alloc;
  file->addr = addr;
  file->maxsize = maxsize;
  tmc_sync_mutex_init(&file->lock);

  // Return a handle object.
  tmc_shmem_t* shmem = create_handle(file, fd, hugefd);
  if (shmem)
    return shmem;

  munmap(addr, maxsize);
 fail:
  if (file != MAP_FAILED)
  {
    if (hugefd != -1)
    {
      if (file->hugepath[0])
        unlink(file->hugepath);
      close(hugefd);
    }
    munmap(file, TMC_SHMEM_HDRLEN);
  }
  close(fd);
  unlink(path);
  return NULL;
}

tmc_shmem_t*
tmc_shmem_create(const char* path, const tmc_alloc_t* alloc,
                 void* addr, size_t maxsize)
{
  // Create the file.
  int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    return NULL;
  return tmc_shmem_create_path_fd(path, fd, alloc, addr, maxsize);
}

tmc_shmem_t*
tmc_shmem_create_temp(char* path, const tmc_alloc_t* alloc,
                      void* addr, size_t maxsize)
{
  int fd = mkstemp(path);
  if (fd < 0)
    return NULL;
  if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
  {
    close(fd);
    return NULL;
  }
  return tmc_shmem_create_path_fd(path, fd, alloc, addr, maxsize);
}

// Map the primary file's header at a random address; NULL on failure.
static struct tmc_shmem_file*
tmc_shmem_map_header(int fd)
{
  struct tmc_shmem_file* file =
    mmap(0, TMC_SHMEM_HDRLEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (file == MAP_FAILED)
    return NULL;
  if (file->cookie != TMC_SHMEM_COOKIE ||
      file->version != TMC_SHMEM_VERSION ||
      (file->hugepath[0] == '\0' && file->pagesize != getpagesize()))
  {
    munmap(file, TMC_SHMEM_HDRLEN);
    errno = EINVAL;
    return NULL;
  }
  return file;
}

tmc_shmem_t*
tmc_shmem_open(const char* path)
{
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    return NULL;
  struct tmc_shmem_file* file = tmc_shmem_map_header(fd);
  if (file == NULL)
  {
    close(fd);
    return NULL;
  }

  int hugefd = -1;
  void* addr = MAP_FAILED;
  if (file->hugepath[0] != '\0')
  {
    // Open and map the hugepage file at the desired address.
    hugefd = open(file->hugepath, O_RDWR);
    if (hugefd < 0)
      goto fail;
    addr = tmc_shmem_setup(hugefd, &file->alloc, file->addr, file->maxsize);
    if (addr == MAP_FAILED)
      goto fail;
  }
  else
  {
    // Re-map the file at the desired address and unmap it where it first was.
    addr = tmc_shmem_setup(fd, &file->alloc, file->addr, file->maxsize);
    if (addr == MAP_FAILED)
      goto fail;
    munmap(file, TMC_SHMEM_HDRLEN);
    file = addr;
  }

  // Return a handle object.
  tmc_shmem_t* shmem = create_handle(file, fd, hugefd);
  if (shmem)
    return shmem;

 fail:
  if (addr != MAP_FAILED)
    munmap(addr, file->maxsize);
  if (file != addr)
    munmap(file, TMC_SHMEM_HDRLEN);
  if (hugefd != -1)
    close(hugefd);
  close(fd);
  return NULL;
}

tmc_shmem_t*
tmc_shmem_open_fds(int fd, int hugefd)
{
  struct tmc_shmem_file* file = tmc_shmem_map_header(fd);
  if (file == NULL)
    return NULL;

  void* addr = MAP_FAILED;
  if (hugefd >= 0)
  {
    // Open and map the hugepage file at the desired address.
    addr = tmc_shmem_setup(hugefd, &file->alloc, file->addr, file->maxsize);
    if (addr == MAP_FAILED)
      goto fail;
  }
  else
  {
    // Re-map the file at the desired address and unmap it where it first was.
    addr = tmc_shmem_setup(fd, &file->alloc, file->addr, file->maxsize);
    if (addr == MAP_FAILED)
      goto fail;
    munmap(file, TMC_SHMEM_HDRLEN);
    file = addr;
  }

  // Return a handle object.
  tmc_shmem_t* shmem = create_handle(file, fd, hugefd);
  if (shmem)
    return shmem;

 fail:
  if (addr != MAP_FAILED)
    munmap(addr, file->maxsize);
  if (file != addr)
    munmap(file, TMC_SHMEM_HDRLEN);
  return NULL;
}

tmc_alloc_t*
tmc_shmem_alloc(tmc_shmem_t* shmem)
{
  return &shmem->alloc;
}

int
tmc_shmem_fd(tmc_shmem_t* shmem)
{
  return shmem->fd;
}

int
tmc_shmem_hugefd(tmc_shmem_t* shmem)
{
  return shmem->hugefd;
}

size_t
tmc_shmem_current_size(tmc_shmem_t* shmem)
{
  return shmem->file->cursize;
}

size_t
tmc_shmem_maximum_size(tmc_shmem_t* shmem)
{
  return shmem->file->maxsize;
}

size_t
tmc_shmem_page_size(tmc_shmem_t* shmem)
{
  return shmem->file->pagesize;
}

void*
tmc_shmem_address(tmc_shmem_t* shmem)
{
  return shmem->file->addr;
}

void
tmc_shmem_set_data(tmc_shmem_t* shmem, void* data)
{
  shmem->file->data = data;
}

void*
tmc_shmem_get_data(tmc_shmem_t* shmem)
{
  return shmem->file->data;
}

int
tmc_shmem_unlink(const char* path)
{
  int fd = open(path, O_RDWR);
  if (fd < 0)
    return fd;
  struct tmc_shmem_file* file = tmc_shmem_map_header(fd);
  close(fd);
  if (file == NULL)
    return -1;
  int hugerc = (file->hugepath[0] != '\0') ? unlink(file->hugepath) : 0;
  munmap(file, TMC_SHMEM_HDRLEN);
  int rc = unlink(path);
  return rc ? rc : hugerc;
}

void
tmc_shmem_close(tmc_shmem_t* shmem)
{
  struct tmc_shmem_file* file = shmem->file;
  tmc_alloc_register_address_range(NULL, file->addr, file->maxsize, NULL, NULL);
  close(shmem->fd);
  munmap(file->addr, file->maxsize);
  if (shmem->hugefd >= 0)
  {
    close(shmem->hugefd);
    munmap(file, TMC_SHMEM_HDRLEN);
  }
  free(shmem);
}

// Ensure that the page table entries are written and backed with pages.
// If we just touched the memory in userspace here, we'd take a SIGSEGV
// if the kernel couldn't provide the memory, but using a system call
// allows the kernel to simply return errno set to EFAULT instead.
static int
force_populate(void* base, size_t length, size_t stride)
{
  size_t i;
  for (i = 0; i < length; i += stride)
  {
    if (syscall(SYS_times, base + i) < 0 && errno == EFAULT)
    {
      errno = ENOMEM;
      return -1;
    }
    memset(base + i, 0, sizeof(struct tms));
  }
  return 0;
}

#ifdef __tile__
// Try to set up a mapping for the file with the specified flags,
// and fault in the page with those flags.  Due to how homecaching
// works, this will cause any other page table entries (e.g. from
// a previous map and unmap of the same page) to be discarded silently
// in any other process that has it mapped, then when it is faulted
// in again, it will use this homecache setting.
int
tmc_shmem_set_page_home(tmc_shmem_t* shmem, void* address, size_t length,
                        int home)
{
  struct tmc_shmem_file* file = shmem->file;
  off_t offset = address - file->addr;
  unsigned long mmap_flags = TMC_ALLOC_HOME_TO_FLAGS(home);

  // Validate arguments.
  if (length == 0 ||
      offset + length > file->cursize ||
      (offset & (file->pagesize - 1)) != 0)
  {
    errno = EINVAL;
    return -1;
  }

  // Map requested chunk of file into memory with cache homing flags,
  // and force it to be populated so we actually re-home the pages,
  // then unmap the temporary mapping.
  length = ROUND_UP(length, file->pagesize);
  int fd = shmem->hugefd >= 0 ? shmem->hugefd : shmem->fd;
  char* tmp = mmap(0, length, PROT_READ | PROT_WRITE,
                   MAP_SHARED | mmap_flags, fd, offset);
  if (tmp == MAP_FAILED)
    return -1;
  int rc = force_populate(tmp, length, file->pagesize);
  munmap(tmp, length);

  // If we requested MAP_POPULATE handling, re-touch the pages to make
  // them present in the page table.  Since we know the file pages
  // here exist, we don't worry about getting a signal from the kernel,
  // and just touch the pages to trigger page fault directly.
  if ((shmem->alloc.mmap_flags & MAP_POPULATE) != 0)
  {
    size_t i;
    for (i = 0; i < length; i += file->pagesize)
      ((volatile char*)address)[i];
  }
    
  return rc;
}
#endif

void*
tmc_shmem_grow(tmc_shmem_t* shmem, size_t length)
{
  struct tmc_shmem_file* file = shmem->file;

  // Round up length to page size.
  length = ROUND_UP(length, file->pagesize);

  // Lock and see if we have enough address space to satisfy the request.
  tmc_sync_mutex_lock(&file->lock);
  off_t oldsize = file->cursize;
  off_t newsize = file->cursize + length;
  if (newsize > file->maxsize)
  {
    tmc_sync_mutex_unlock(&file->lock);
    errno = ENOMEM;
    return NULL;
  }

  // Actually grow the backing file by the requested amount.
  int fd = shmem->hugefd >= 0 ? shmem->hugefd : shmem->fd;
  if (ftruncate(fd, newsize) < 0)
  {
    tmc_sync_mutex_unlock(&file->lock);
    return NULL;
  }

  void* result = file->addr + oldsize;

  // See if the kernel is willing to actually fault in memory at the
  // new size.  If not, we revert the file size and return failure.
  if ((shmem->alloc.mmap_flags & MAP_POPULATE) != 0 &&
      force_populate(result, length, file->pagesize) < 0)
    goto fail;

  // Update the tracked file size and unlock.
  file->cursize = newsize;
  tmc_sync_mutex_unlock(&file->lock);

  // Return the start of the new chunk.
  return result;

 fail:
  ftruncate(fd, file->cursize);
  tmc_sync_mutex_unlock(&file->lock);
  return NULL;
}

int
tmc_shmem_shrink(tmc_shmem_t* shmem, off_t newsize, size_t length)
{
  struct tmc_shmem_file* file = shmem->file;

  if ((newsize & (file->pagesize - 1)) != 0)
  {
    errno = EINVAL;
    return -1;
  }

  // Round up length to page size.
  length = ROUND_UP(length, file->pagesize);

  // Validate that the request is for the end of the file.
  off_t low_bound = (shmem->hugefd < 0) ? TMC_SHMEM_HDRLEN : 0;
  tmc_sync_mutex_lock(&file->lock);
  if (newsize < low_bound || file->cursize != newsize + length)
  {
    tmc_sync_mutex_unlock(&file->lock);
    errno = EINVAL;
    return -1;
  }

  // Truncate the file down.
  int fd = shmem->hugefd >= 0 ? shmem->hugefd : shmem->fd;
  if (ftruncate(fd, newsize) < 0)
  {
    tmc_sync_mutex_unlock(&file->lock);
    return -1;
  }

  file->cursize = newsize;
  tmc_sync_mutex_unlock(&file->lock);
  return 0;
}


// Note that we require the "alloc" object at the start of the "shmem" object.
void*
tmc_shmem_alloc_mmap_mbind(const tmc_alloc_t* alloc, void* start, size_t length)
{
  if (start != NULL)
  {
    errno = EINVAL;
    return NULL;
  }
  return tmc_shmem_grow((tmc_shmem_t*) alloc, length);
}

static int
tmc_shmem_alloc_unmap(const tmc_alloc_t* alloc, void* addr, size_t length)
{
  // Do one easy check to see if we are freeing at the very end of the
  // arena, and if so, shrink the arena.  We could also layer some fancier
  // stuff and keep a freelist of pages and reallocate them on demand.
  // But in principle we prefer to leave that kind of work to the mspace layer.
  // Note that we require the "alloc" object at the start of the "shmem" object.
  tmc_shmem_t* shmem = (tmc_shmem_t*) alloc;
  struct tmc_shmem_file* file = shmem->file;
  off_t new_cursize = addr - file->addr;

  if (new_cursize + length == file->cursize)
    return tmc_shmem_shrink(shmem, new_cursize, length);

  errno = EINVAL;
  return -1;
}

static void*
tmc_shmem_alloc_remap(const tmc_alloc_t* alloc,
                      void *old_address, size_t old_length,
                      size_t new_length, int flags, void* new_address)
{
  errno = EINVAL;
  return NULL;
}
