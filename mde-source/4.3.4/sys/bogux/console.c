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
 * Output of characters to the console.
 * @file
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <util.h>
#include <tnslock.h>

#include <hv/hypervisor.h>

#include <arch/chip.h>

#include "bogux.h"

/** Most sources are file descriptors, but there are some special ones. */
#define CONSOLE -1

/** Names for certain file descriptors. */
const char* const console_names[] = { "stdin", "stdout", "stderr" };

static int cons_lock _LOCKS;

union ConsData {
  struct {
    bool is_newline;
    int last_source;
    HV_Coord last_coord;
    const char* name;
  };
  char pad[CHIP_L2_LINE_SIZE()];
} console _L2ALIGNED_INIT = {{ true, CONSOLE, {0,0}, "console" }};

#define console_lock() tnslock_lock(&console, sizeof(console), &cons_lock)
#define console_unlock() tnslock_unlock(&console, sizeof(console), &cons_lock)

static void
console_write_internal(int source, char* s, int len)
{
  if (len <= 0)
    return;
  HV_Topology topology = hv_inquire_topology();
  if (console.last_source != source ||
      console.last_coord.x != topology.coord.x ||
      console.last_coord.y != topology.coord.y)
  {
    switch (source)
    {
    case CONSOLE:
      console.name = "console";
      break;
    case 0:
    case 1:
    case 2:
      console.name = console_names[source];
      break;
    default:  /* some file descriptor */
      assert(source < 100);
      static char buf[3];
      char* b = buf;
      if (source >= 10)
        *b++ = '0' + source / 10;
      *b++ = '0' + source % 10;
      *b++ = '\0';
      console.name = buf;
      break;
    }
    console.last_source = source;
    console.last_coord = topology.coord;
    if (!console.is_newline)
    {
      console.is_newline = true;
      // display quoted-printable line-continuation character
      hv_console_write((HV_VirtAddr) "=\n", 2);
    }
  }
  if (console.is_newline)
  {
    char line_header[80];
    snprintf(line_header, sizeof (line_header), "(%d,%d) %s: ",
             topology.coord.x, topology.coord.y, console.name);
    hv_console_write((HV_VirtAddr) line_header, strlen(line_header));
  }
  console.is_newline = (s[len - 1] == '\n');
  if (console.is_newline)
  {
    if (len > 1)
      hv_console_write((HV_VirtAddr) s, len - 1);
    hv_console_write((HV_VirtAddr) "\r\n", 2);
  }
  else
    hv_console_write((HV_VirtAddr) s, len);
}


static void
console_write_unlocked(int fd, char* s, int len)
{
  int i = 0;
  while (i < len)
  {
    int j;
    for (j = i; j < len; ++j)
    {
      if (s[j] == '\n')
      {
        ++j;
        break;
      }
    }
    console_write_internal(fd, &s[i], j-i);
    i = j;
  }
}


void
console_write(int fd, char* s, int len)
{
  console_lock();
  console_write_unlocked(fd, s, len);
  console_unlock();
}


// Definitions for the standard output file used by printf() et al.

static int
hv_cons_write(char* s, int len, unsigned int offset, void* private)
{
  console_write(CONSOLE, s, len);
  return (len);
}

static int
hv_cons_read(char* s, int len, unsigned int offset, void* private)
{
  // Bogux doesn't use console input, so we don't implement it
  return (0);
}

static char ts_cons_buf[256] _TILESTATE;

static struct _file_ops cons_fops =
{
  .write = hv_cons_write,
  .read = hv_cons_read
};

static FILE ts_cons_out _TILESTATE;

// During early boot, we have a simple "struct _file" that works before
// _TILESTATE and _LOCKS memory has been set up in the page table.

static int
hv_cons_write_unlocked(char* s, int len, unsigned int offset, void* private)
{
  console_write_unlocked(CONSOLE, s, len);
  return (len);
}

static struct _file_ops boot_fops =
{
  .write = hv_cons_write_unlocked,
  .read = hv_cons_read
};
static char boot_buf[256];
static FILE boot_out = {
  .buf = boot_buf,
  .ptr = boot_buf,
  .len = sizeof(boot_buf),
  .wrem = sizeof(boot_buf),
  .rrem = 0,
  .flg = _FLG_W,
  .ops = &boot_fops
};

FILE* stdout = &boot_out;

void
init_per_tile_stdout()
{
  ts_cons_out.buf = ts_cons_out.ptr = ts_cons_buf;
  ts_cons_out.len = ts_cons_out.wrem = sizeof(ts_cons_buf);
  ts_cons_out.rrem = 0;
  ts_cons_out.flg = _FLG_W;
  ts_cons_out.ops = &cons_fops;
}

void
reset_stdout()
{
  stdout = &ts_cons_out;
  __insn_flush(&stdout);
  __insn_mf();
}
