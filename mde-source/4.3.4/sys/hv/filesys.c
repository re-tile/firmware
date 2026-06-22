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
 * The hypervisor filesystem.
 */

#include <stdio.h>
#include <string.h>
#include <util.h>

#include <arch/sim.h>
#include <arch/rsh.h>
#include <arch/udn.h>

#include <hv/hypervisor.h>
#include "sys/libc/include/util.h"

#include "bits.h"
#include "boot_error.h"
#include "cfg.h"
#include "client_obj.h"
#include "debug.h"
#include "fault.h"
#include "filesys.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "mapping.h"
#include "misc.h"
#include "page.h"
#include "post/post_ram.h"
#include "srom_acc.h"
#include "sromboot.h"
#include "tlb.h"

/** Pointer to the start of hypervisor filesystem. */
static PA fs_pa;

/** Filesystem reader function, used in init_fs() only. */
typedef uint32_t (*fs_reader)(void* buf, int nwds, uint32_t old_crc);

/** Convert bytes to HVFS words */
#define FS_B2W_UP(val)         (((val) + 3) >> 2)
/** Convert HVFS words to bytes */
#define FS_W2B(val)            ((val) << 2)

/** Shim which will be targeted by srom_fs_reader. */
static pos_t fs_reader_rshim;

/** Read and CRC words from the static network.
 * @param buf Destination buffer for the words.
 * @param nwds Number of words to read.
 * @param old_crc Previous CRC to update.
 * @return New CRC value.
 */
static uint32_t
bootstream_fs_reader(void* buf, int nwds, uint32_t old_crc)
{
  //
  // Note that this routine functions in terms of 4-byte words, since
  // that's what the HVFS uses.  We might consider changing this in the
  // future.
  //
  uint32_t* ptr = buf;
  static int lookahead_valid = 0;
  static uint32_t lookahead;
  old_crc = ~old_crc;

  if (nwds && lookahead_valid)
  {
    *ptr = lookahead;
    old_crc = __insn_crc32_32(old_crc, *ptr++);
    lookahead_valid = 0;
    nwds--;
  }





  while (nwds)
  {






    uint64_t data = udn0_receive();


    *ptr = (uint32_t) data;
    old_crc = __insn_crc32_32(old_crc, *ptr++);

    if (--nwds == 0)
    {
      lookahead = (uint32_t) (data >> 32);
      lookahead_valid = 1;
      break;
    }

    *ptr = (uint32_t) (data >> 32);
    old_crc = __insn_crc32_32(old_crc, *ptr++);
    nwds--;
  }

  return (~old_crc);
}

/** SROM address at which srom_fs_reader should begin reading; this is
    incremented by a call to srom_fs_reader, so successive calls read
    successive blocks of words from the SROM. */
static int fs_reader_srom_addr;

/** srom_dev value for reading from SROM. */
static int fs_reader_srom_dev;

/** Read and CRC words from the SROM.
 * @param buf Destination buffer for the words.
 * @param nwds Number of words to read.
 * @param old_crc Previous CRC to update.
 * @return New CRC value.
 */
static uint32_t
srom_fs_reader(void* buf, int nwds, uint32_t old_crc)
{
  int rv = srom_rd(fs_reader_rshim, SROM_CHAN, fs_reader_srom_dev,
                   fs_reader_srom_addr, FS_W2B(nwds), buf);
  if (rv != FS_W2B(nwds))
    panic("unexpected return value from srom_rd (%d) while reading hvfs", rv);

  fs_reader_srom_addr += FS_W2B(nwds);

  old_crc = ~old_crc;
  uint32_t* wordp = (uint32_t*) buf;
  for (int i = 0; i < nwds; i++)
    old_crc = __insn_crc32_32(old_crc, *wordp++);
  return (~old_crc);
}

/** Map a page of the HVFS into virtual space.
 * @param offset Offset within the HVFS to be accessed.
 * @param len Pointer to the returned valid length (i.e., the number of
 *   bytes, beginning at the returned VA, which can be accessed without
 *   moving the mapping window).  May be NULL if no length need be
 *   returned.
 * @return The VA of the requested byte of the HVFS.
 */
static void*
fs_map_win(unsigned long offset, int* len)
{
  const int ps = TTE_SHIFT_TO_PS(HV_FS_PAGE_SHIFT);

  unsigned long page = offset & ~((unsigned long) HV_FS_PAGE_SIZE - 1);
  unsigned long page_offset = offset & ((unsigned long) HV_FS_PAGE_SIZE - 1);

  assert(!install_wired_mapping(HV_FS_VA, fs_pa + page, ps,
                                HV_PTE_MODE_CACHE_NO_L3, 0));

  if (len)
    *len = HV_FS_PAGE_SIZE - page_offset;

  return ((void*) (HV_FS_VA + page_offset));
}


/** Unmap the last-mapped window onto the HVFS. */
static void
fs_unmap_win()
{
  assert(!remove_wired_tte_va(HV_FS_VA));
}


/** Unmap the last-mapped window onto the HVFS, if there is such a window. */
static void
fs_unmap_win_if_needed()
{
  remove_wired_tte_va(HV_FS_VA);
}


/** Initialize the hypervisor filesystem.
 * @param sts Slave tile state.
 * @param srom_addr If nonzero, the filesystem comes from the SROM, and
 *   this is the SROM address at which it starts.  Otherwise, we read it
 *   from the static network.
 * @param rshim Address of the rshim; only needed if srom_addr != 0.
 */
void
init_fs(struct slave_tile_state* sts, int srom_addr, pos_t rshim)
{
  fs_hdr_t tmp_fs_hdr;
  fs_crc_t tmp_fs_crc;

  //
  // Pick a function to get us filesystem words depending on where they're
  // coming from.
  //
  fs_reader reader_func;
  fs_reader_rshim = rshim;

  if (srom_addr != 0)
  {
    reader_func = srom_fs_reader;
    fs_reader_srom_dev = srom_get_dev(fs_reader_rshim, SROM_CHAN);
    fs_reader_srom_addr = srom_addr;
  }
  else
    reader_func = bootstream_fs_reader;

  if (is_master)
  {
    // Read in the filesystem header.

    uint32_t crc = reader_func(&tmp_fs_hdr, FS_B2W_UP(sizeof (tmp_fs_hdr)), 0);

    // Make sure it really looks like a filesystem.

    if (tmp_fs_hdr.magic != HV_FS_MAGIC)
    {
      if (board_flags & BOARD_BADCRC_REBOOT)
      {
        tprintf("hv_warning: bad magic number %#x in hypervisor filesystem\n",
                tmp_fs_hdr.magic);
        tprintf("Rebooting to try alternate image.\n");
        reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
      }
      else
        panic("bad magic number %#x in hypervisor filesystem",
              tmp_fs_hdr.magic);
    }

    // Read in the CRC.

    reader_func(&tmp_fs_crc, FS_B2W_UP(sizeof (tmp_fs_crc)), 0);

    // Check the CRC.

    if (crc != tmp_fs_crc.crc)
    {
      if (board_flags & BOARD_BADCRC_REBOOT)
      {
        tprintf("corrupt hvfs header: computed CRC %#x, trailer CRC %#x\n",
                crc, tmp_fs_crc.crc);
        tprintf("Rebooting to try alternate image.\n");
        reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
      }
      else
        panic("corrupt hvfs header: computed CRC %#x, trailer CRC %#x",
              crc, tmp_fs_crc.crc);
    }

    // Reserve physical memory for the filesystem.

    fs_pa = get_phys(tmp_fs_hdr.fs_len, HV_FS_PAGE_SIZE);
    if (fs_pa == ~(PA) 0)
      panic("not enough memory for hypervisor file system");

    int post_failed = 0;
    {
      PA post_pa = fs_pa;
      unsigned long offset = 0;
      int length;

      // If running on tsim, test a small amount of memory, but enough
      // that it doesn't all just fit in the cache.
      if (sim_is_simulator())
        length = CHIP_L2_CACHE_SIZE() * 2;
      else
        length = tmp_fs_hdr.fs_len;

      while (length && !post_failed)
      {
        int bytesthispass;
        void* va = fs_map_win(offset, &bytesthispass);
   
        if (bytesthispass > length)
          bytesthispass = length;

        post_failed = post_ram_hv((VA) va, post_pa, bytesthispass, 0);
   
        fs_unmap_win();
   
        offset += bytesthispass;
        post_pa += bytesthispass;
        length -= bytesthispass;
      }
    }

    if (post_failed)
    {
      printf("POST of memory for HV filesystem failed.\n");
      boot_error(POST_ERR_HV_RAM_FS);
    }

    // Load filesystem into memory.

    int length = tmp_fs_hdr.fs_len - sizeof (tmp_fs_hdr);
    unsigned long offset = sizeof (tmp_fs_hdr);

#if defined(DEBUG)
    //
    // Tracing while we read in the hypervisor filesystem generally fills up
    // tsim's log files, and greatly slows down initialization, for very
    // little value.  Disable tracing while we do the copy and flush unless
    // we've been told not to.
    //
    uint32_t trace_flags = 0;
    if ((debug_flags & DEBUG_INIT_FS) == 0)
    {
      trace_flags = sim_get_tracing();
      sim_set_tracing(SIM_TRACE_NONE);
    }
#endif /* DEBUG */

    crc = 0;

    while (length)
    {
      int bytesthispass;
      char* dst_ptr = fs_map_win(offset, &bytesthispass);
 
      if (bytesthispass > length)
        bytesthispass = length;
 
      crc = reader_func(dst_ptr, FS_B2W_UP(bytesthispass), crc);

#ifdef __BIG_ENDIAN__
      // Byte swap the file system.
      // The inodes are actually OK.  We swap them back at the end.
      uint32_t* p = (uint32_t*) dst_ptr;

      for (int i = 0; i < FS_B2W_UP(bytesthispass); ++i, ++p)
        *p = __builtin_bswap32(*p);
#endif
      flush_range((VA) dst_ptr, bytesthispass);
 
      fs_unmap_win();
 
      offset += bytesthispass;
      length -= bytesthispass;
    }

    // Read in the CRC.

    reader_func(&tmp_fs_crc, FS_B2W_UP(sizeof (tmp_fs_crc)), 0);

    // Check the CRC.

    if (crc != tmp_fs_crc.crc)
    {
      if (board_flags & BOARD_BADCRC_REBOOT)
      {
        tprintf("corrupt hvfs: computed CRC %#x, trailer CRC %#x\n",
                crc, tmp_fs_crc.crc);
        tprintf("Rebooting to try alternate image.\n");
        reset_chip(SROMBOOT_SOFTREBOOT_ACT_BADCRC);
      }
      else
        panic("corrupt hvfs: computed CRC %#x, trailer CRC %#x",
              crc, tmp_fs_crc.crc);
    }

    //
    // Validate here that the full set of inodes will fit in one window.
    // We're pessimistic here, in that we assume every file has the
    // longest possible name, but generally the HVFS only has a couple
    // of files in it.
    //
    assert(sizeof (fs_hdr_t) +
           tmp_fs_hdr.ninode * (sizeof (inode_t) + HV_PATH_MAX + 1) <=
           HV_FS_PAGE_SIZE);

    // Save our temporary header in the actual filesystem.

    fs_hdr_t* fs_hdr = fs_map_win(0, NULL);
    *fs_hdr = tmp_fs_hdr;

#ifdef __BIG_ENDIAN__
    // Byte swap the inodes back the way they started.
    uint32_t* p = (uint32_t*) &fs_hdr[1];
    int isize = fs_hdr->fs_desc_off - sizeof (fs_hdr_t);

    for (int i = 0; i < FS_B2W_UP(isize); ++i, ++p)
      *p = __builtin_bswap32(*p);
#endif

    // Flush fs_hdr and inodes.
    flush_range((VA) fs_hdr, fs_hdr->fs_desc_off);
    fs_unmap_win();

    // Save relevant data in the state we'll send to slave tiles.

    sts->fs_pa = fs_pa;
    sts->fs_len = tmp_fs_hdr.fs_len;

#if defined(DEBUG)
    if (trace_flags)
      sim_set_tracing(trace_flags);
#endif /* DEBUG */
  }
  else
  {
    fs_pa = sts->fs_pa;
  }
}


/** Find a file in the hypervisor filesystem.
 * @param filename Name of the file to find.
 * @return Inode number of the found file, or a negative error code.
 */
int
fs_findfile(char* filename)
{
  int retval = HV_ENOENT;

  const char* fs_ptr = fs_map_win(0, NULL);
  const fs_hdr_t* fs_hdr = (fs_hdr_t*) fs_ptr;
  const inode_t* fs_ino = (inode_t*) &fs_hdr[1];

  for (int i = 0; i < fs_hdr->ninode; i++)
    if (!strncmp(filename, fs_ptr + fs_ino[i].name_off, HV_PATH_MAX))
    {
      retval = i;
      break;
    }

  fs_unmap_win();

  return (retval);
}


/** Return metadata for a file in the hypervisor filesystem.
 * @param inode Inode number of the file whose status is requested.
 * @param len Pointer to the returned length of the file, in bytes.
 * @param flags Pointer to the returned flags (this is really for future
 *   expansion; no flags are currently defined).
 */
void
fs_stat(int inode, int* len, unsigned int* flags)
{
  const char* fs_ptr = fs_map_win(0, NULL);
  const fs_hdr_t* fs_hdr = (fs_hdr_t*) fs_ptr;
  const inode_t* fs_ino = (inode_t*) &fs_hdr[1];

  if (inode < 0 || inode >= fs_hdr->ninode)
  {
    *len = HV_EBADF;
    *flags = 0;
  }
  else
  {
    *len = fs_ino[inode].len;
    *flags = fs_ino[inode].flags;
  }

  fs_unmap_win();
}


/** Read data from a file in the hypervisor filesystem.
 * @param inode The hypervisor file to read.
 * @param buf The buffer to read data into.  This must be a hypervisor
 *   virtual address; to read into a client virtual address, use
 *   fs_pread_user().
 * @param length The number of bytes of data to read.
 * @param offset The offset into the file to read the data from.
 * @return The number of bytes successfully read, or a negative error code.
 */
int
fs_pread(int inode, char* buf, int length, int offset)
{
  if (length <= 0)
    return (0);

  int end_xfer = offset + length - 1;

  const fs_hdr_t* fs_hdr = (fs_hdr_t*) fs_map_win(0, NULL);
  const inode_t* fs_ino = (inode_t*) &fs_hdr[1];

  if (inode < 0 || inode >= fs_hdr->ninode)
  {
    fs_unmap_win();
    return (HV_EBADF);
  }
  else if (offset >= fs_ino[inode].len)
  {
    fs_unmap_win();
    return (0);
  }
  else if (end_xfer < offset || end_xfer >= fs_ino[inode].len)
    length = fs_ino[inode].len - offset;

  unsigned long file_base = fs_ino[inode].data_off + offset;

  fs_unmap_win();

  int bytes_to_copy = length;
  while (bytes_to_copy)
  {
    int bytesthispass;
    const char* src_ptr = fs_map_win(file_base, &bytesthispass);
 
    if (bytesthispass > bytes_to_copy)
      bytesthispass = bytes_to_copy;
 
    memcpy(buf, src_ptr, bytesthispass);
 
    fs_unmap_win();
 
    file_base += bytesthispass;
    buf += bytesthispass;
    bytes_to_copy -= bytesthispass;
  }

  return (length);
}


/** Read data from a file in the hypervisor filesystem to a client virtual
 *  address.
 * @param inode The hypervisor file to read.
 * @param buf The buffer to read data into.  This must be a client virtual
 *   address; to read into a hypervisor virtual address, use fs_pread().
 * @param length The number of bytes of data to read.
 * @param offset The offset into the file to read the data from.
 * @return The number of bytes successfully read, or a negative error code,
 *   including HV_EFAULT if the supplied client virtual address range is
 *   invalid.
 */
int
fs_pread_user(int inode, char* buf, int length, int offset)
{
  if (FAULT_BEGIN(buf, length))
  {
    //
    // Note that if the passed address is in the hypervisor's VA space,
    // then we'll execute this code without ever calling fs_pread(); thus,
    // we must we use the version of unmap which doesn't panic if there
    // is no window mapped.
    //
    fs_unmap_win_if_needed();
    return (HV_EFAULT);
  }

  int retval = fs_pread(inode, buf, length, offset);

  FAULT_END();

  return (retval);
}


/** Notify the simulator that "exec" was just done on the given file.
 * @param inode The inode of the file being "exec"d.
 */
void
fs_sim_notify_exec(int inode)
{
  if (!sim_is_simulator())
    return;

  const char* fs_ptr = fs_map_win(0, NULL);
  const fs_hdr_t* fs_hdr = (fs_hdr_t*) fs_ptr;
  const inode_t* fs_ino = (inode_t*) &fs_hdr[1];

  if (inode >= fs_hdr->ninode)
  {
    fs_unmap_win();
    return;
  }

  const char* exe = fs_ptr + fs_ino[inode].name_off;

  unsigned char c;
  do {
    c = *exe++;
    __insn_mtspr(SPR_SIM_CONTROL,
                 (SIM_CONTROL_OS_EXEC | (c << _SIM_CONTROL_OPERATOR_BITS)));
  } while (c);

  fs_unmap_win();
}


/** Handle the hv_fs_findfile() syscall.
 * @param filename Name of requested file.
 * @return Inode of requested file, or an error code.
 */
int
syscall_fs_findfile(char* filename)
{
  int retval;

  SYSCALL_TRACE("fs_findfile(filename = %p)\n", filename);

  ON_FAULT_RETURN_EFAULT(filename, HV_PATH_MAX);

  retval = fs_findfile(filename);

  FAULT_END();

  SYSCALL_TRACE("filename = %s\n", filename);

  return (retval);
}


/** Handle the hv_fs_fstat() syscall.
 * @param inode The inode number to query.
 * @return An HV_FS_StatInfo structure.
 */
HV_FS_StatInfo
syscall_fs_fstat(int inode)
{
  HV_FS_StatInfo stat;

  SYSCALL_TRACE("fs_fstat(inode = %d)\n", inode);

  fs_stat(inode, &stat.size, &stat.flags);

  return (stat);
}


/** Handle the hv_fs_pread() syscall.
 * @param inode The hypervisor file to read.
 * @param buf The buffer to read data into.
 * @param length The number of bytes of data to read.
 * @param offset The offset into the file to read the data from.
 * @return Number of bytes successfully read, or an error code.
 */
int
syscall_fs_pread(int inode, char* buf, int length, int offset)
{
  int retval;

  SYSCALL_TRACE("fs_pread(inode = %d, buf = %p, length = %d, offset = %d)\n",
                inode, buf, length, offset);

  retval = fs_pread_user(inode, buf, length, offset);

  return (retval);
}


// Routines used by the file structure initialized by fs_open.

static int
fs_read(char* s, int len, unsigned int offset, void* private)
{
  int ino = (int) (intptr_t) private;
  return (fs_pread(ino, s, len, offset));
}


static int
fs_write(char* s, int len, unsigned int offset, void* private)
{
  return (0);
}


static struct _file_ops fs_fops =
{
  .write = fs_write,
  .read = fs_read
};


/** Set up a stdio data structure so that a file in the hypervisor filesystem
 *  may be read using stdio routines.
 * @param filename Name of the file.
 * @param f File structure pointer.
 * @param buf Pointer to file's data area.
 * @param len Length of file's data.
 * @return 0 for success, or an error code.
 */
int
fs_open(char* filename, FILE* f, char* buf, int len)
{
  int ino = fs_findfile(filename);
  if (ino < 0)
    return (ino);

  f->buf = buf;
  f->len = len;
  f->ptr = buf;
  f->wrem = 0;
  f->rrem = 0;
  f->pos = 0;
  f->flg = _FLG_R;
  f->ops = &fs_fops;
  f->pvt = (void*)(long)ino;

  return (0);
}
