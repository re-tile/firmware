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

#ifndef TOOLS_HANDY_BUFFER_H
#define TOOLS_HANDY_BUFFER_H

#include "common.h"

BEGIN_EXTERN_C


//! A buffer of bytes which resizes as needed.
typedef struct _Buffer Buffer;

//! Actual contents of @ref Buffer.
struct _Buffer
{
  uint8_t* data;
  uint limit;
  uint size;
  uint head;
};


//! Make sure @param buffer can hold at least @param growth more entries.
extern void
Buffer_reserve(Buffer* buffer, uint growth);

//! Append @param byte to @param buffer, expanding if needed.
extern void
Buffer_append(Buffer* buffer, uint8_t byte);

//! Append @param count bytes, starting at @param bytes, to @param buffer,
//! expanding if needed.
extern void
Buffer_write(Buffer* buffer, const void* bytes, size_t count);

//! A varargs interface to @ref Buffer_printf.
extern void
Buffer_vprintf(Buffer* buffer, const char* format, va_list args);

//! Like "printf(format, ...)" into @param buffer, expanding as needed.
//! A final nul will be written, but will not be included in "size".
//! Certain bad format strings will cause fatal errors.
extern void
Buffer_printf(Buffer* buffer, const char* format, ...)
  __attribute__((format(printf, 2, 3)));

//! Like "printf("%s", str)" into @param buffer, expanding as needed.
//! A final nul will be written, but will not be included in "size".
extern void
Buffer_print(Buffer* buffer, const char* str);

//! Returns offset of first instance of char in buffer.
//! Returns -1 if char is not found.
extern uint
Buffer_find(Buffer* buffer, int c);

//! Excise @param length entries starting at @param index from @param buffer.
extern void
Buffer_excise(Buffer* buffer, uint index, uint length);

//! Clear @param buffer completely.
extern void
Buffer_clear(Buffer* buffer);

//! HACK: Initialize @param buffer, using @param data and @param limit.
//! It is assumed that "data" is non-NULL, and "limit" is non-zero.
extern void
Buffer_init_hack(Buffer* buffer, uint8_t* data, uint limit);

//! Initialize @param buffer.
//! NOTE: The result is the same as setting all bits to zero.
extern void
Buffer_init(Buffer* buffer);

//! Destroy @param buffer.
//! NOTE: The final result is similar to "Buffer_init(buffer)".
extern void
Buffer_destroy(Buffer* buffer);


//! Call strdup(), or die.
extern char*
strdup_or_die(const char* str);


//! A varargs interface to @ref strfmt_or_die.
extern char*
vstrfmt_or_die(const char* format, va_list args);

//! Allocate a string containing "printf(format, ...)", or die.
extern char*
strfmt_or_die(const char* format, ...)
  __attribute__((format(printf, 1, 2)));


END_EXTERN_C

#endif /* !TOOLS_HANDY_BUFFER_H */
