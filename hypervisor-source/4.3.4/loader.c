/*
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
 */

/**
 * @file
 * Load an ELF executable into client space.
 */

#include <string.h>

#include <arch/chip.h>

#include <elf.h>

#include "sys/libc/include/util.h"
#include <hv/hypervisor.h>

/** Use the non-standard-I/O version of the compression library */
#define BZ_NO_STDIO
#include <bzlib.h>

#include "bits.h"
#include "client_obj.h"
#include "debug.h"
#include "filesys.h"
#include "loader.h"
#include "mapping.h"
#include "misc.h"
#include "physacc.h"

//
// State values for client decompression.
//
/** Are we decompressing (vs. just reading a file)? */
static int is_decomp = 0;
/** Lowest-addressed byte being used by the decompressor's malloc() */
static char* lowest_malloced_byte;
/** State structure for decompression. */
static bz_stream strm;
/** Our temporary buffer for input data. */
static char* tmp_inbuf;
/** Our temporary buffer for to-be-discarded output data. */
static char* tmp_outbuf;
/** Size of tmp_inbuf and tmp_outbuf. */
#define TMP_BUFSIZ (1024 * 1024)


/** Provide a very dumb allocator for use by the bzip2 library.  We set
 *  a pointer to the end of the user's available address space, then
 *  decrement it as space is allocated.
 * @param opaque Opaque pointer (unused).
 * @param m Number of items to allocate.
 * @param n Size of items to allocate.
 * @return Pointer to the allocated space.
 */
static void*
my_bzalloc(void* opaque, int m, int n)
{
  //
  // We round up to a multiple of 8 to ensure alignment for any data type.
  //
  int total = ROUND_UP(m * n, 8);
  lowest_malloced_byte -= total;
  return lowest_malloced_byte;
}


/** Provide a null free routine for use by the bzip2 library.  We don't
 *  actually worry about freeing anything; since we're just using the
 *  client's memory, as soon as we start the client we'll give it all up.
 * @param opaque Opaque pointer (unused).
 * @param p Pointer to free.
 */
static void
my_bzfree(void* opaque, void* p)
{
  return;
}


/** Initialize the decompressor and set up do_read to return decompressed
 *  data.
 */
static void
init_decomp()
{
  // Discover how much memory we can use for the allocator from shim 0.
  // In some cases -- for instance, if we're striping -- we may have more
  // memory on shim 0 than is addressable via the client's address space.
  // This means that the memory at the end of the shim isn't accessible in
  // fake P=V mode.  When that happens, we initialize the allocator with
  // less memory than is available, so that we'll be able to address all of
  // the memory which comes from it.
  //
  // (Arguably we should be clipping to the VA space which is available to
  // the client, but it's easier to use a fixed value here, and the client
  // can't really assume it will have physical memory equal to such a large
  // VA space, so we just clip to 2 GB.)
  //
  CPA size = client_mshim_size(0);
  if (size > 0x80000000UL)
    size = 0x80000000UL;
  lowest_malloced_byte = (char*) (long) ROUND_DN(size, 8);

  //
  // Now that the allocator is set up, just use it for our temporary buffers.
  //
  tmp_inbuf = my_bzalloc(0, 1, TMP_BUFSIZ);
  tmp_outbuf = my_bzalloc(0, 1, TMP_BUFSIZ);

  //
  // Set up the decompressor's state structure.
  //
  strm.bzalloc = my_bzalloc;
  strm.bzfree = my_bzfree;
  strm.opaque = 0;

  //
  // Initialize the decompressor.
  //
  int err;
  if ((err = BZ2_bzDecompressInit(&strm, 0, 0)) != BZ_OK)
    panic("error %d initializing decompressor", err);

  //
  // Note that we're decompressing, not reading directly from a file.
  //
  is_decomp = 1;
}


/** Shut down the decompressor.
 */
static void
reset_decomp()
{
  is_decomp = 0;

  //
  // Theoretically we should call BZ2_bzDecompressEnd() here, but since we
  // don't actually care about freeing the memory it allocated, we don't
  // bother; that saves it from being linked in to the hypervisor image.
  //
}


/** Read and decompress one segment of data.
 * @param inode Inode number of the source file.
 * @param buf Destination for the data.
 * @param length Number of bytes to produce.
 */
static void
do_read_step(int inode, char* buf, int length)
{
  int err;

  strm.next_out = buf;
  strm.avail_out = length;

  //
  // We loop until we produce the amount of data requested, or until we get an
  // error.
  //
  while (1)
  {
    //
    // If we have everything we need, we're done.
    //
    if (strm.avail_out == 0)
      break;

    //
    // If we're out of input, supply some more.
    //
    if (strm.avail_in == 0)
    {
      int inlen = fs_pread(inode, tmp_inbuf, TMP_BUFSIZ, strm.total_in_lo32);
      if (inlen <= 0)
        panic("ran out of input data during decompression of client");

      strm.next_in = tmp_inbuf;
      strm.avail_in = inlen;
    }

    //
    // Do some decompression.  If it all goes well, continue; if we hit EOF,
    // and got all of the data we wanted, exit; otherwise, die.
    //
    err = BZ2_bzDecompress(&strm);

    if (err == BZ_OK)
      continue;

    if (err == BZ_STREAM_END && strm.avail_out == 0)
      break;

    panic("error %d during decompression of client", err);
  }
}


/** Read data from a file in the hypervisor filesystem, potentially
 *  uncompressing it if needed.
 * @param inode Inode number of the source file.
 * @param buf Destination for the data.
 * @param length Number of bytes to produce.
 * @param offset Where in the decompressed stream the data should come from.
 * @return Number of bytes decompressed.  (This will always be equal to
 *   length, since we panic on all errors.)
 */
static int
do_read(int inode, char* buf, int length, int offset)
{
  //
  // If we aren't decompressing, just read the data.
  //
  if (!is_decomp)
    return (fs_pread(inode, buf, length, offset));

  //
  // We can't go backward in the output stream without re-decompressing
  // the whole file, so don't.
  //
  if (offset < strm.total_out_lo32)
    panic("attempt to seek backward on compressed file");

  //
  // If the reader wants data beyond the current output point, we need to
  // decompress some data and discard it.
  //
  if (offset > strm.total_out_lo32)
  {
    int bytes2discard;

    while (1)
    {
      bytes2discard = offset - strm.total_out_lo32;

      if (bytes2discard == 0)
        break;

      if (bytes2discard > TMP_BUFSIZ)
        bytes2discard = TMP_BUFSIZ;

      do_read_step(inode, tmp_outbuf, bytes2discard);
    }
  }

  //
  // Now the file offset is right; read the data the user wanted.
  //
  do_read_step(inode, buf, length);

  return (length);
}


/** Internal error handling routine called by the bzip2 library.
 * @param errcode Error code.
 */
void
bz_internal_error(int errcode)
{
  panic("error %d in decompression library during client load", errcode);
}


/** ELF magic number. */
#ifdef __BIG_ENDIAN__
#define ELF_MAGIC (ELFMAG3 | (ELFMAG2 << 8) | (ELFMAG1 << 16) | (ELFMAG0 << 24))
#else
#define ELF_MAGIC (ELFMAG0 | (ELFMAG1 << 8) | (ELFMAG2 << 16) | (ELFMAG3 << 24))
#endif

/** Bzip2 magic number. */
#define BZ2_MAGIC ('B' | ('Z' << 8) | ('h' << 16))
/** Length of Bzip2 magic number. */
#define BZ2_MAGIC_LEN 3

/** Load an ELF executable into client physical memory.
 * @param inode Inode of the file to load from the hypervisor filesystem.
 * @return Client physical address of the executable entry point.
 */
CPA
load(int inode)
{
  // See if this is a compressed file, and if so set up for decompression.
  uint32_t bz2_magic = 0;

  if (fs_pread(inode, (char*) &bz2_magic, BZ2_MAGIC_LEN, 0) == BZ2_MAGIC_LEN &&
      bz2_magic == BZ2_MAGIC)
    init_decomp();

  // Do header checks.

  ElfW(Ehdr) elf_header;

  if (do_read(inode, (char*) &elf_header, sizeof (elf_header), 0) !=
      sizeof (elf_header) || elf_header.e_ehsize < sizeof (elf_header))
    panic("can't load client; ELF header too short");

  uint32_t* elf_magic = (uint32_t*) &elf_header;
  if (*elf_magic != ELF_MAGIC)
    panic("can't load client; bad magic number");

  if (elf_header.e_machine != CHIP_ELF_TYPE() &&
      elf_header.e_machine != CHIP_COMPAT_ELF_TYPE())
    panic("can't load client; ELF file wrong architecture");






  if (elf_header.e_flags & (1 << EF_TILEGX_ISE1))
    panic("can't load client; ISE1 not supported");


  if (elf_header.e_type != ET_EXEC)
    panic("can't load client; file is not executable");

  // This limit is completely arbitrary, and is just here to keep us from
  // blowing out the stack if we get some really wacky client binary.
  // Normally this value is about 3.
  if (elf_header.e_phnum > 32)
    panic("can't load client; too many program headers");

  ElfW(Phdr) phdrs[elf_header.e_phnum];

  if (elf_header.e_phentsize < sizeof(phdrs[0]))
    panic("can't load client; no program headers");

  // Save the start virtual address; we'll translate this to physical below.
  unsigned long start_va = elf_header.e_entry;
  CPA start_pa = ~0ULL;

  //
  // Read in the program header table, then loop through it and copy
  // sections to memory.  We read it all at once, and then do one pass
  // through it to load the actual segments, rather than reading them
  // one at a time and loading each one as we see it.  This is because
  // if we're dealing with a compressed input file we can't go backwards
  // from a loaded segment to get the next phdr.
  //
  // Theoretically we should sort the program header table by file offset,
  // since we can't seek backward.  As it turns out, for the supervisors
  // we build, the headers are already ordered properly, so we don't bother.
  //
  if (do_read(inode, (char*) phdrs, sizeof (phdrs), elf_header.e_phoff) !=
      sizeof (phdrs))
    panic("can't load client; ELF program headers too short");

  for (int i = 0; i < elf_header.e_phnum; i++)
  {
    // If it's not readable, we don't load it.
    if ((phdrs[i].p_flags & PF_R) == 0)
      continue;

    // If the start address is in this section, compute its physical address.
    if (start_va >= phdrs[i].p_vaddr &&
        start_va < phdrs[i].p_vaddr + phdrs[i].p_memsz)
      start_pa = (start_va - phdrs[i].p_vaddr) + phdrs[i].p_paddr;

    // Copy this section's data to physical memory.  (We're actually in fake
    // P=V mode, so we just copy right to virtual memory; however, we still do
    // appropriate c2r_pa calls to verify that the addresses used are within
    // range.)

    PA section_pa;

    if (c2r_pa(phdrs[i].p_paddr, phdrs[i].p_memsz, &section_pa))
      panic("can't load client; section at PA %#llX, length %#llx out of range",
            (unsigned long long)phdrs[i].p_paddr,
            (unsigned long long)phdrs[i].p_memsz);

    if (phdrs[i].p_paddr < HV_GLUE_START_CPA + HV_GLUE_RESERVED_SIZE &&
        phdrs[i].p_paddr + phdrs[i].p_memsz >= HV_GLUE_START_CPA)
      panic("can't load client; would be overwritten by hypervisor glue layer");

    if (do_read(inode, (char*) phdrs[i].p_paddr,
        phdrs[i].p_filesz, phdrs[i].p_offset) !=
        phdrs[i].p_filesz)
      panic("can't load client; unexpected error or end of file");

    if (phdrs[i].p_filesz < phdrs[i].p_memsz)
      memset((void*) (phdrs[i].p_paddr + phdrs[i].p_filesz), 0,
             phdrs[i].p_memsz - phdrs[i].p_filesz);

    flush_range(phdrs[i].p_paddr, phdrs[i].p_memsz);
  }

  if (start_pa == ~0ULL)
    panic("can't load client; bogus entry point %#lx", start_va);

  reset_decomp();

  return (start_pa);
}


/** Load a null client executable into client physical memory.
 * @return Client physical address of the executable entry point.
 */
CPA
load_null_client()
{
  extern char null_client, null_client_end;
  VA dest = HV_GLUE_START_CPA + HV_GLUE_RESERVED_SIZE;
  int len = &null_client_end - &null_client;

  memcpy((void*) dest, &null_client, len);
  flush_range(dest, len);

  return (dest);
}
