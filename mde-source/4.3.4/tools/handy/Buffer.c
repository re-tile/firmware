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

#include "Buffer.h"

#include "message.h"
#include "various.h"

#include <stdio.h>


void
Buffer_reserve(Buffer* buffer, uint growth)
{
  uint need = buffer->size + growth;

  uint old_limit = buffer->limit;

  if (old_limit < need)
  {
    uint limit = 1024;
    while (limit < need)
      limit *= 2;

    if (old_limit % 2 != 0)
    {
      // HACK: Support "Buffer_init_hack()".
      uint8_t* data = (uint8_t*)malloc_or_die(limit);
      memcpy(data, buffer->data, old_limit);
      buffer->data = data;
    }
    else
    {
      buffer->data = (uint8_t*)realloc_or_die(buffer->data, limit);
    }

    buffer->limit = limit;
  }
}


void
Buffer_append(Buffer* buffer, uint8_t byte)
{
  Buffer_reserve(buffer, 1);
  buffer->data[buffer->size++] = byte;
}


void
Buffer_write(Buffer* buffer, const void* bytes, size_t count)
{
  Buffer_reserve(buffer, count);
  memcpy(buffer->data + buffer->size, bytes, count);
  buffer->size += count;
}


void
Buffer_vprintf(Buffer* buffer, const char* format, va_list args)
{
  uint size = buffer->size;

  // HACK: Make room for at least one char, to optimize the common case of
  // "buffer" having no room left, and the result needing at most 1024 chars.
  Buffer_reserve(buffer, 1);

  va_list copy;
  va_copy(copy, args);

  int n = vsnprintf((char*)buffer->data + size, buffer->limit - size,
                    format, args);

  if (n < 0)
  {
    punt("Failure in 'vsnprintf(..., \"%s\", ...)'", format);
  }

  if ((uint)n >= buffer->limit - size)
  {
    Buffer_reserve(buffer, n + 1);

    va_copy(args, copy);
    int m = vsnprintf((char*)buffer->data + size, buffer->limit - size,
                      format, args);
    va_end(args);

    if (m != n)
    {
      punt("Expected %d, but got %d, from 'vsnprintf()'", n, m);
    }
  }

  va_end(copy);

  buffer->size = size + n;
}


void
Buffer_printf(Buffer* buffer, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  Buffer_vprintf(buffer, format, args);
  va_end(args);
}


void
Buffer_print(Buffer* buffer, const char* str)
{
  uint len = strlen(str);
  Buffer_reserve(buffer, len + 1);
  memcpy(buffer->data + buffer->size, (const uint8_t*)str, len + 1);
  buffer->size += len;
}


uint
Buffer_find(Buffer* buffer, int c)
{
  const void* start = buffer->data;
  size_t size = buffer->size;
  void* found = memchr(start, c, size);
  uint result = (found == NULL) ? -1 : (found - start);
  return result;
}

void
Buffer_excise(Buffer* buffer, uint index, uint length)
{
  assert(index <= buffer->size);
  assert(index + length <= buffer->size);
  uint size = buffer->size - length;
  uint n = size - index;
  memmove(buffer->data + index, buffer->data + index + length, n);
  buffer->size = size;
}


inline void
Buffer_clear(Buffer* buffer)
{
  buffer->size = 0;
  buffer->head = 0;
}


void
Buffer_init(Buffer* buffer)
{
  buffer->data = NULL;
  buffer->limit = 0;
  buffer->size = 0;
  buffer->head = 0;
}


void
Buffer_init_hack(Buffer* buffer, uint8_t* data, uint limit)
{
  assert(data != NULL && limit != 0);

  // HACK: Throw away one char (if needed) to allow us to use the parity of
  // "limit" to determine if "data" was supplied by the user.
  if (limit % 2 == 0)
    limit--;

  buffer->data = data;
  buffer->limit = limit;
  buffer->size = 0;
  buffer->head = 0;
}


void
Buffer_destroy(Buffer* buffer)
{
  // HACK: Support "Buffer_init_hack()".
  if (buffer->limit % 2 == 0)
    free(buffer->data);
  Buffer_init(buffer);
}



char*
strdup_or_die(const char* str)
{
  char* cpy = strdup(str);

  if (str == NULL)
    punt_with_errno("Failure in strdup()");

  return cpy;
}


char*
vstrfmt_or_die(const char* format, va_list args)
{
  char* str;
  int n = vasprintf(&str, format, args);
  if (n >= 0)
    return str;
  punt_with_errno("Failure in vasprintf()");
}


char*
strfmt_or_die(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  char* cpy = vstrfmt_or_die(format, args);
  va_end(args);
  return cpy;
}
