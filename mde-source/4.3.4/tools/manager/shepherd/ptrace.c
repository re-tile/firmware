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

// This file defines some useful "ptrace()" wrappers.


// Includes "tools/handy/handy.h" and thus "common/include/tilera.h".
#include "common.h"


static bool
ptrace_read_ulong(pid_t tid, ulong addr, ulong* valp)
{
  errno = 0;
  ulong val = ptrace(PTRACE_PEEKDATA, tid, addr, NULL);
  if (errno != 0)
    return false;

  spew(5, "Read 0x%08lx from addr 0x%08lx.", val, addr);
  *valp = val;
  return true;
}


bool
ptrace_read_byte(pid_t tid, ulong addr, uint8_t* bytep)
{
  ulong val;
  if (!ptrace_read_ulong(tid, addr & -sizeof(ulong), &val))
    return false;

  uint shift = (addr % sizeof(ulong)) * 8;
  *bytep = val >> shift;
  return true;
}


size_t
ptrace_read_bytes(pid_t tid, ulong addr, void* bytes, size_t num)
{
  uint8_t* bp = (uint8_t*)bytes;

  for (size_t i = 0; i < num; )
  {
    if ((addr + i) % sizeof(ulong) == 0 && i + sizeof(ulong) <= num)
    {
      ulong val = 0;
      if (!ptrace_read_ulong(tid, addr + i, &val))
        return i;
      // NOTE: This assumes host/tile are little endian.
      if (sizeof(ulong) == 4)
        write_uint(bp + i, val);
      else
        write_uint64(bp + i, val);
      i += sizeof(ulong);
    }
    else
    {
      if (!ptrace_read_byte(tid, addr + i, bp + i))
        return i;
      i++;
    }
  }
  return num;
}



static bool
ptrace_write_ulong(pid_t tid, ulong addr, ulong val)
{
  if (ptrace(PTRACE_POKEDATA, tid, addr, val) != 0)
    return false;

  spew(5, "Wrote 0x%08lx at addr 0x%08lx.", val, addr);

  return true;
}


bool
ptrace_write_byte(pid_t tid, ulong addr, uint8_t byte)
{
  errno = 0;
  ulong val1 = ptrace(PTRACE_PEEKDATA, tid, addr & -sizeof(ulong), NULL);
  if (errno != 0)
    return false;

  uint shift = (addr % sizeof(ulong)) * 8;
  ulong val2 = (val1 & ~(0xFFL << shift)) | ((ulong)byte << shift);
  return ptrace_write_ulong(tid, addr & -sizeof(ulong), val2);
}


size_t
ptrace_write_bytes(pid_t tid, ulong addr, const void* bytes, size_t num)
{
  const uint8_t* bp = (const uint8_t*)bytes;

  for (size_t i = 0; i < num; )
  {
    if ((addr + i) % sizeof(ulong) == 0 && i + sizeof(ulong) <= num)
    {
      // NOTE: This assumes host/tile are little endian.
      ulong val =
        (sizeof(ulong) == 4) ? read_uint(bytes + i) : read_uint64(bytes + i);
      if (!ptrace_write_ulong(tid, addr + i, val))
        return i;
      i += sizeof(ulong);
    }
    else
    {
      uint8_t byte = bp[i];
      if (!ptrace_write_byte(tid, addr + i, byte))
        return i;
      i++;
    }
  }
  return num;
}
