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

#ifndef TOOLS_HANDY_ARRAY_H
#define TOOLS_HANDY_ARRAY_H

#include "common.h"

BEGIN_EXTERN_C

//! An array of "void*" which resizes as needed.
typedef struct _Array Array;

//! Actual contents of @ref Array.
struct _Array
{
  void** data;
  uint limit;
  uint size;
};


//! Get the entry at index @index in @param array.
extern void*
Array_get(const Array* array, uint index);

//! Set the entry at index @index in @param array to @param entry.
extern void
Array_set(const Array* array, uint index, void* entry);

//! Make sure @param array can hold at least @param growth more entries.
extern void
Array_reserve(Array* array, uint growth);

//! Append @param entry to @param array, expanding if needed.
//! NOTE: Same as "Array_insert(array, array->size, entry)".
extern void
Array_append(Array* array, void* entry);

//! Insert @param entry at index @index in @param array, expanding if needed.
//! NOTE: Same as "Array_splice(array, index, &entry, 1)".
extern void
Array_insert(Array* array, uint index, void* entry);

//! Splice @param entries, of length @param num, into @param array,
//! at index @index, expanding if needed.
extern void
Array_splice(Array* array, uint index, void** entries, uint num);

//! Excise @param length entries starting at @param index from @param array.
extern void
Array_excise(Array* array, uint index, uint length);

//! Find @param entry in @param array, and return its index, or return -1.
extern int
Array_find(Array* array, void* entry);

//! Find @param entry in @param array, or append it, and return its index.
extern int
Array_find_or_append(Array* array, void* entry);

//! Clear @param array, without freeing any entries.
extern void
Array_clear(Array* array);

//! Clear @param array, after freeing all entries.
extern void
Array_free_and_clear(Array* array);

//! Initialize @param array.
//! NOTE: The result is the same as setting all bits to zero.
extern void
Array_init(Array* array);

//! Destroy @param array, without freeing any entries.
//! NOTE: The final result is similar to "Array_init(array)".
extern void
Array_destroy(Array* array);

END_EXTERN_C

#endif /* !TOOLS_HANDY_ARRAY_H */
