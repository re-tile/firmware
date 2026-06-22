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
 * Handle device drivers
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
#include "interrupt.h"
#include "mem_layout.h"
#include "mman.h"
#include "rand.h"
#include "stat.h"
#include "syscall.h"

#ifndef TCGETS
// newlib doesn't define this; should be just #include <sys/ioctl.h>
#define TCGETS 0x5401
#endif

#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE          0x89F0
#endif

/** Compute the CRC32 of a buffer. */
static uint32_t
crc(const char *str, int len, uint32_t crc)
{
  crc = ~crc;

  while (len--)
    crc = __insn_crc32_8(crc, *str++);

  return (~crc);
}

static bool ts_filesums_enabled _TILESTATE;

/** Turn on "output file checksums instead of file data" mode. */
void
do_filesums(bool enabled)
{
  ts_filesums_enabled = enabled;
}




/** Set mode to character. */
ssize_t
chr_open(struct fd* fd, const char* name)
{
  fd->mode = 0600 | S_IFCHR;
  return 0;
}

/** console open */
static ssize_t
cons_open(struct fd* fd, const char* name)
{
  fd->crc = 0;
  fd->crc_valid = false;
  fd->crc_enabled = ts_filesums_enabled;
  return chr_open(fd, name);
}

/** console read */
static ssize_t
cons_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  int byte = hv_console_read_if_ready();
  // FIXME: really only if opened O_NONBLOCK, but it's convenient :-)
  // We have no mechanism to yield the processor at the moment.
  // We could just spin slowly here until data becomes available.
  if (byte < 0)
    return -EAGAIN;
  char* p = buf_va;
  do {
    *p++ = byte;
    if (p - buf_va >= bytes)
      break;
    byte = hv_console_read_if_ready();
  } while (byte >= 0);
  return p - buf_va;
}

/** console write */
ssize_t
cons_write(struct fd* fd, const char* buf_va, ssize_t bytes)
{
  int index = fd - ts_descriptors;
  bool last_byte_newline = true;

  if (fd->crc_enabled)
  {
    fd->crc = crc(buf_va, bytes, fd->crc);
    fd->crc_valid = true;
    return bytes;
  }

  // Use quoted-printable encoding for file output to the console.
  char enc_buf[256];
  int enc_len = 0;

  for (int i = 0; i < bytes; ++i)
  {
    unsigned char c = buf_va[i];
    if (c == '\n' || c == '\t' || (c >= ' ' && c < 126 && c != '='))
    {
      enc_buf[enc_len++] = c;
    }
    else
    {
      static const char hex[] = "0123456789abcdef";
      enc_buf[enc_len++] = '=';
      enc_buf[enc_len++] = hex[c >> 4];
      enc_buf[enc_len++] = hex[c & 0xf];
    }

    // If we don't have room for another encoded character, flush the buffer.
    if (enc_len + 3 > sizeof (enc_buf))
    {
      last_byte_newline = enc_buf[enc_len - 1] == '\n';
      console_write(index, enc_buf, enc_len);
      enc_len = 0;
    }
  }

  // If there's anything remaining in the buffer, flush it.
  if (enc_len)
  {
    last_byte_newline = enc_buf[enc_len - 1] == '\n';
    console_write(index, enc_buf, enc_len);
  }

  // Make sure we write a final newline to flush output to the console.
  if (!last_byte_newline)
    console_write(index, "=\n", 2);

  return bytes;
}

/** console close */
static void
cons_close(struct fd* fd)
{
  int index = fd - ts_descriptors;

  if (fd->crc_valid)
  {
    if (index < 3)
      printf("%s CRC: 0x%x\n", console_names[index], fd->crc);
    else
      printf("%d CRC: 0x%x\n", index, fd->crc);
  }
}

/** console ioctl */
int
chr_ioctl(struct fd* fd, int cmd, void* ptr)
{
  switch (cmd)
  {
  case TCGETS:
  {
    if (!S_ISCHR(fd->mode))
      return -ENOTTY;

    /* FIXME - this is just a placeholder for uclibc, so we
     * don't provide any actual information via the termios pointer
     */
    return 0;
  }

  case SIOCDEVPRIVATE:           // Toggle CRC mode bit for this file
    {
      bool oldval = fd->crc_enabled;
      if (ptr)
        fd->crc_enabled = true;
      else
        fd->crc_enabled = false;
      return oldval;
    }

  default:
    return -EINVAL;
  }
}

const struct fops cons_fops = {
  .open = cons_open,
  .read = cons_read,
  .write = cons_write,
  .ioctl = chr_ioctl,
  .close = cons_close,
};


/** null close */
void
null_close(struct fd* fd)
{
}



/** /dev/null read */
static ssize_t
null_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  return 0;
}

/** /dev/null write */
ssize_t
null_write(struct fd* fd, const char* buf_va, ssize_t bytes)
{
  return bytes;
}

static const struct fops null_fops = {
  .open = chr_open,
  .read = null_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


/** /dev/zero read */
static ssize_t
zero_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  memset(buf_va, 0, bytes);
  return bytes;
}

static const struct fops zero_fops = {
  .open = chr_open,
  .read = zero_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


/** Seed for the random number generator. */
uint32_t ts_rand_seed _TILESTATE;

/** /dev/random filesystem read */
static ssize_t
rand_read(struct fd* fd, char* va, ssize_t bytes)
{
  for (int i = 0; i < bytes; i++)
    *va++ = rand_step(256, &ts_rand_seed);
  fd->offset += bytes;
  return bytes;
}

static const struct fops rand_fops = {
  .open = chr_open,
  .read = rand_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


static const struct fops hugetlb_fops = {
  .open = chr_open,
  .read = null_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


/** Was this file opened on a hugetlbfs? */
bool
is_hugetlb_file(int index)
{
  struct fd* fd = get_fd(index);
  return fd && fd->fops == &hugetlb_fops;
}


/** Registers returned from last test hypervisor syscall.  Most calls just
 *  return one word, but some have more, so we set up to handle anything.
 */

struct hv_call_retval { unsigned long rv[10]; };

static struct hv_call_retval ts_hvtest_retval _TILESTATE;

struct hv_call_retval do_hv_call(void* func, unsigned long* args);

/** Hypervisor test read; this returns the register values obtained from
 *  the last hvtest_write() call.
 */
static ssize_t
hvtest_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  if (bytes > sizeof(ts_hvtest_retval))
    bytes = sizeof(ts_hvtest_retval);

  memcpy(buf_va, &ts_hvtest_retval, bytes);

  return bytes;
}

/** Hypervisor test write.  The first 4 bytes of the write are the index of
 *  the hypervisor routine to be called, and the remaining bytes are stuffed
 *  into the parameter registers.
 */
static ssize_t
hvtest_write(struct fd* fd, const char* buf_va, ssize_t bytes)
{
  unsigned long args[11];

  if (bytes > sizeof(args) || bytes < sizeof (args[0]) || (bytes & 3) != 0)
    return -EINVAL;

  memcpy(args, buf_va, bytes);

  unsigned long func = MEM_CODE_VA + HV_GLUE_START_CPA +
                       args[0] * HV_DISPATCH_ENTRY_SIZE;

  ts_hvtest_retval = do_hv_call((void*) func, &args[1]);

  return bytes;
}

static const struct fops hvtest_fops = {
  .open = chr_open,
  .read = hvtest_read,
  .write = hvtest_write,
  .ioctl = chr_ioctl,
  .close = null_close,
  .flags = FOPS_FLG_HVTEST,
};


/** Values returned from last v2p hypervisor operation.
 */

struct ts_hvv2p_retval_t
{
  uint64_t pa;
  HV_PTE pte;
  uint32_t pagesize;
};

static struct ts_hvv2p_retval_t ts_hvv2p_retval _TILESTATE;

/** Hypervisor v2p read; this returns the client physical address obtained from
 *  the last hvv2p_write() call.
 */
static ssize_t
hvv2p_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  if (bytes > sizeof(ts_hvv2p_retval))
    bytes = sizeof(ts_hvv2p_retval);

  memcpy(buf_va, &ts_hvv2p_retval, bytes);

  return bytes;
}

/** Hypervisor v2p write.  The 4 bytes of the write are the VA to be
 *  translated to a physical address, which is later retrieved via a read
 *  from the same device.
 */
static ssize_t
hvv2p_write(struct fd* fd, const char* buf_va, ssize_t bytes)
{
  void* arg;

  if (bytes != sizeof(arg))
    return -EINVAL;

  memcpy(&arg, buf_va, bytes);

  ts_hvv2p_retval.pa = va_to_cpa_and_pte(arg, &ts_hvv2p_retval.pte);
  ts_hvv2p_retval.pagesize =
    hv_pte_get_page(ts_hvv2p_retval.pte) ?
      HV_PAGE_SIZE_LARGE : HV_PAGE_SIZE_SMALL;

  return bytes;
}

static const struct fops hvv2p_fops = {
  .open = chr_open,
  .read = hvv2p_read,
  .write = hvv2p_write,
  .close = null_close,
};

/** Hypervisor interrupt test read; this returns the oldest interrupt received,
 *  removing it from the queue.
 */
static ssize_t
hvmsgint_read(struct fd* fd, char* buf_va, ssize_t bytes)
{
  HV_IntrMsg him;

  if (!get_msg_int(&him))
    return 0;

  if (bytes > sizeof(him))
    bytes = sizeof(him);

  memcpy(buf_va, &him, bytes);

  return bytes;
}

static const struct fops hvmsgint_fops = {
  .open = chr_open,
  .read = hvmsgint_read,
  .write = null_write,
  .ioctl = chr_ioctl,
  .close = null_close,
};


static const struct device {
  void (*init)(void);
  const struct fops * fops;
  const char * name;
  bool exact;
} device_table[] = {
  { NULL, &null_fops, "null", true },         // /dev/null
  { NULL, &zero_fops, "zero", true },         // /dev/zero
  { NULL, &cons_fops, "tty",  true },         // /dev/tty
  { NULL, &hugetlb_fops, "hugetlb/", false }, // /dev/hugetlb/*
  { NULL, &hugetlb_fops, "hugepages/", false }, // /dev/hugepages/*
  { NULL, &rand_fops, "random", true },       // /dev/random
  { NULL, &hvmsgint_fops, "hvmsgint", true }, // /dev/hvmsgint
  { NULL, &hvtest_fops, "hvtest", true },     // /dev/hvtest
  { NULL, &hvv2p_fops, "hvv2p", true },       // /dev/hvv2p
#ifndef __tilegx__
  { xaui_init, &xaui_fops, "xgbe/", false },  // /dev/xgbe/*
#endif

  { NULL, NULL, "END", false }
};

#define DEV_PREFIX_LENGTH (5) /* length of "/dev/" */

/** What devices do we have? */
const struct fops *
find_device(const char *file, int flags, int mode)
{
  const size_t sizeof_device_table = sizeof(device_table) /
                                     sizeof(device_table[0]);

  unsigned int i;
  // Look down the device table and see what we have.
  for (i = 0; i < sizeof_device_table; i++)
  {
    if (strncmp(file+DEV_PREFIX_LENGTH, device_table[i].name,
                strlen(device_table[i].name)) == 0)
    {
      // We have a possible match -- does it need to be exact?
      if (!device_table[i].exact ||
          file[strlen(device_table[i].name)+DEV_PREFIX_LENGTH] == '\0')
      {
        return device_table[i].fops;
      }
    }
  }
  return NULL;
}

/** Initialize devices. */
void
init_devices()
{
  const size_t sizeof_device_table = sizeof(device_table) /
                                     sizeof(device_table[0]);

  unsigned int i;
  // Look down the device table and see what we have.
  for (i = 0; i < sizeof_device_table; i++)
  {
    if (device_table[i].init != NULL)
    {
      device_table[i].init();
    }
  }
}
