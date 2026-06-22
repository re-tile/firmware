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

#include "Array.h"

#include "message.h"
#include "various.h"


void*
Array_get(const Array* array, uint index)
{
  if (index >= array->size)
    punt("Illegal index %u (of %u).", index, array->size);
  return array->data[index];
}


void
Array_set(const Array* array, uint index, void* entry)
{
  if (index >= array->size)
    punt("Illegal index %u (of %u).", index, array->size);
  array->data[index] = entry;
}


void
Array_reserve(Array* array, uint growth)
{
  uint need = array->size + growth;
  if (array->limit < need)
  {
    void** data = array->data;
    uint limit = data ? array->limit : 16;
    while (limit < need)
    {
      limit *= 2;
    }
    array->data = (void**)realloc_or_die(data, limit * sizeof(void*));
    array->limit = limit;
  }
}


void
Array_append(Array* array, void* entry)
{
  Array_reserve(array, 1);
  array->data[array->size++] = entry;
}


void
Array_insert(Array* array, uint index, void* entry)
{
  Array_splice(array, index, &entry, 1);
}


void
Array_splice(Array* array, uint index, void** entries, uint num)
{
  assert(index <= array->size);
  Array_reserve(array, num);
  uint n = (array->size - index) * sizeof(void*);
  array->size += num;
  memmove(array->data + index + num, array->data + index, n);
  memcpy(array->data + index, entries, num * sizeof(void*));
}


void
Array_excise(Array* array, uint index, uint length)
{
  assert(index <= array->size);
  assert(index + length <= array->size);
  uint size = array->size - length;
  uint n = (size - index) * sizeof(void*);
  memmove(array->data + index, array->data + index + length, n);
  array->size = size;
}


int
Array_find(Array* array, void* entry)
{
  for (int i = 0; i < array->size; i++)
  {
    if (array->data[i] == entry)
      return i;
  }

  return -1;
}


int
Array_find_or_append(Array* array, void* entry)
{
  int i = Array_find(array, entry);

  if (i < 0)
  {
    i = array->size;
    Array_append(array, entry);
  }

  return i;
}


void
Array_clear(Array* array)
{
  array->size = 0;
}


void
Array_free_and_clear(Array* array)
{
  for (uint i = 0; i < array->size; i++)
  {
    free(array->data[i]);
  }
  Array_clear(array);
}


void
Array_init(Array* array)
{
  array->data = NULL;
  array->limit = 0;
  array->size = 0;
}


void
Array_destroy(Array* array)
{
  free(array->data);
  Array_init(array);
}

