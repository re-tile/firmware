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

#ifndef TOOLS_HANDY_STRINGARRAY_H
#define TOOLS_HANDY_STRINGARRAY_H

#include "common.h"

BEGIN_EXTERN_C


//! An array of "char*" which resizes as needed.
typedef struct _StringArray StringArray;

//! Actual contents of @ref StringArray.
//
// NOTE: This layout must match that of Array.
//
struct _StringArray
{
  char** data;
  uint limit;
  uint size;
};


//! Return the index of the entry with the same textual contents, or -1.
extern int
StringArray_lookup(const StringArray* array, const char* str);

//! See @ref Array_get.
extern char*
StringArray_get(const StringArray* array, uint index);

//! See @ref Array_set.
extern void
StringArray_set(const StringArray* array, uint index, char* entry);

//! See @ref Array_reserve.
extern void
StringArray_reserve(StringArray* array, uint need);

//! See @ref Array_append.
extern void
StringArray_append(StringArray* array, char* entry);

//! See @ref Array_insert.
extern void
StringArray_insert(StringArray* array, uint index, char* entry);

//! See @ref Array_splice.
extern void
StringArray_splice(StringArray* array, uint index, char** entries, uint num);

//! See @ref Array_excise.
extern void
StringArray_excise(StringArray* array, uint index, uint length);

//! See @ref Array_clear.
extern void
StringArray_clear(StringArray* array);

//! See @ref Array_free_and_clear.
extern void
StringArray_free_and_clear(StringArray* array);

//! See @ref Array_init.
extern void
StringArray_init(StringArray* array);

//! See @ref Array_destroy.
extern void
StringArray_destroy(StringArray* array);


//! Extract tokens from @param str, appending them to @param array.
//!
//! Tokens are separated by whitespace, but backslashes will escape
//! any character (including whitespace), and quotes (single or double)
//! will disable the special properties of whitespace.
//!
//! Returns number of tokens appended, or -1 (setting errno to "EINVAL") if
//! there were any unterminated quotes (which are treated as terminated), or
//! trailing backslashes (which are ignored).
//!
extern int
tokenize(StringArray* array, char* str);


// Takes an array of "key=value" entries.
//
// If called with a new "key=value" entry, replaces the first existing entry
// with the same "key" with the the new entry, and returns the old entry, or
// else appends the new entry, and returns NULL.
//
// If called with "key", deletes the first existing entry with that "key",
// and returns the old entry, or returns NULL.
//
extern char*
modify_envp(StringArray* envp, char* str);


END_EXTERN_C

#endif /* !TOOLS_HANDY_STRINGARRAY_H */
