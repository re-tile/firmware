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

#include "message.h"

#include "Buffer.h"

#include <stdio.h>


int message_verbosity = 0;


const char* message_prefix = "";


void (*message_prefix_hook)(char* prefix, size_t size);


void (*message_output_hook)(char* str);


static bool message_busy;



const char hex_chars_upper[16] =
{
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};


const char hex_chars_lower[16] =
{
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};


int
hex_char_to_int(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}



void
vmessage_aux(const char* prefix, const char* suffix,
             const char* format, va_list args)
{
  // HACK: Prevent various problems involving recursion.
  if (message_busy)
    return;

  message_busy = true;

  const char* special_prefix = message_prefix;

  char message_prefix_buf[1024];
  if (message_prefix_hook != NULL)
  {
    (*message_prefix_hook)(message_prefix_buf, sizeof(message_prefix_buf));
    special_prefix = message_prefix_buf;
  }

  void (*output_hook)(char* str) = message_output_hook;
  if (output_hook != NULL)
  {
    uint8_t buf[1023];
    Buffer buffer;
    Buffer_init_hack(&buffer, buf, sizeof(buf));
    Buffer_print(&buffer, special_prefix);
    Buffer_print(&buffer, prefix);
    Buffer_vprintf(&buffer, format, args);
    Buffer_print(&buffer, suffix);
    (*output_hook)((char*)buffer.data);
    Buffer_destroy(&buffer);
  }
  else
  {
    fprintf(stderr, "%s%s", special_prefix, prefix);
    vfprintf(stderr, format, args);
    fprintf(stderr, "%s\n", suffix);
    fflush(stderr);
  }

  message_busy = false;
}


void
vmessage(const char* format, va_list args)
{
  vmessage_aux("", "", format, args);
}


void
message(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vmessage_aux("", "", format, args);
  va_end(args);
}



void
punt(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vmessage_aux("ERROR: ", "", format, args);
  va_end(args);

  exit(128);
}


void
punt_with_errno(const char* format, ...)
{
  char suffix[1024];
  snprintf(suffix, sizeof(suffix), ": (%d) %s.", errno, strerror(errno));

  va_list args;
  va_start(args, format);
  vmessage_aux("ERROR: ", suffix, format, args);
  va_end(args);

  exit(128);
}



void
warn(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vmessage_aux("WARNING: ", "", format, args);
  va_end(args);
}


void
warn_with_errno(const char* format, ...)
{
  char suffix[1024];
  snprintf(suffix, sizeof(suffix), ": (%d) %s.", errno, strerror(errno));

  va_list args;
  va_start(args, format);
  vmessage_aux("WARNING: ", suffix, format, args);
  va_end(args);
}



void
note_bytes(const void* data, size_t size, const char* format, ...)
{
  uint8_t buf[1023];
  Buffer buffer;
  Buffer_init_hack(&buffer, buf, sizeof(buf));

  const uint8_t* bytes = data;

  for (uint i = 0; i < size; i++)
  {
    uint8_t b = bytes[i];
    if (b >= ' ' && b <= '~')
    {
      if (b == '\\')
        Buffer_append(&buffer, '\\');
      Buffer_append(&buffer, b);
    }
    else
    {
      Buffer_append(&buffer, '\\');
      switch (b)
      {
      case '\n':
        Buffer_append(&buffer, 'n');
        break;
      case '\r':
        Buffer_append(&buffer, 'r');
        break;
      case '\t':
        Buffer_append(&buffer, 't');
        break;
      default:
        Buffer_append(&buffer, 'x');
        // Optimized: Buffer_printf(&buffer, "%02x", byte);
        Buffer_append(&buffer, hex_chars_lower[b / 16]);
        Buffer_append(&buffer, hex_chars_lower[b % 16]);
        break;
      }
    }
  }

  Buffer_append(&buffer, '\0');

  va_list args;
  va_start(args, format);
  vmessage_aux("", (char*)buffer.data, format, args);
  va_end(args);

  Buffer_destroy(&buffer);
}
