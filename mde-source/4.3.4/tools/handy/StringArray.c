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

#include "StringArray.h"

#include "Array.h"

#include <ctype.h>


int
StringArray_lookup(const StringArray* array, const char* str)
{
  for (uint i = 0; i < array->size; i++)
  {
    if (!strcmp(array->data[i], str))
      return i;
  }

  return -1;
}


char*
StringArray_get(const StringArray* array, uint index)
{
  return (char*)(Array_get((const Array*)array, index));
}


void
StringArray_set(const StringArray* array, uint index, char* entry)
{
  Array_set((Array*)array, index, (void*)entry);
}


void
StringArray_reserve(StringArray* array, uint growth)
{
  Array_reserve((Array*)array, growth);
}


void
StringArray_append(StringArray* array, char* entry)
{
  Array_append((Array*)array, (void*)entry);
}


void
StringArray_insert(StringArray* array, uint index, char* entry)
{
  Array_insert((Array*)array, index, (void*)entry);
}


void
StringArray_splice(StringArray* array, uint index, char** entries, uint num)
{
  Array_splice((Array*)array, index, (void**)entries, num);
}


void
StringArray_excise(StringArray* array, uint index, uint length)
{
  Array_excise((Array*)array, index, length);
}


void
StringArray_clear(StringArray* array)
{
  Array_clear((Array*)array);
}


void
StringArray_free_and_clear(StringArray* array)
{
  Array_free_and_clear((Array*)array);
}


void
StringArray_init(StringArray* array)
{
  Array_init((Array*)array);
}


void
StringArray_destroy(StringArray* array)
{
  Array_destroy((Array*)array);
}



// NOTE: See the similar "tokenize" in "tools/simulator/tsim/Params.cc",
// which supports a bizarre "comma mode" (with optional "colon support").
//
int
tokenize(StringArray* array, char* str)
{
  int count = 0;

  char quote = '\0';

  // Skip leading whitespace.
  while (isspace(*str))
    str++;

  // Tokenize args.
  while (*str != '\0')
  {
    char* tok = str;

    char* dst = tok;

    while (*str != '\0')
    {
      // Normal text.
      if (quote == '\0')
      {
        // Start quoting as needed.
        if ((*str == '\"') || (*str == '\''))
        {
          quote = *str++;
          continue;
        }

        // End token on whitespace.
        if (isspace(*str))
        {
          break;
        }
      }

      // Stop quoting as needed.
      else if (*str == quote)
      {
        quote = '\0';
        str++;
        continue;
      }

      // Handle backslashed literals.
      if (*str == '\\')
      {
        str++;

        // Handle trailing backslash.
        if (*str == '\0')
        {
          quote = '\\';
          break;
        }
      }

      // Copy char.
      *dst++ = *str++;
    }

    // Skip trailing whitespace.
    while (isspace(*str))
      str++;

    // Done.
    *dst = '\0';

    // Collect the token.
    StringArray_append(array, tok);
    count++;
  }

  // Handle unterminated quote and trailing backslash.
  if (quote != '\0')
  {
    errno = EINVAL;
    return -1;
  }

  return count;
}


char*
modify_envp(StringArray* envp, char* str)
{
  // Find the first '=', or the end of 'str'.
  char* equals = str;
  while (*equals != '\0' && *equals != '=')
    equals++;
  uint len = equals - str;

  // Replace/Remove existing entry.
  for (uint i = 0; i < envp->size; i++)
  {
    char* old = envp->data[i];
    if (strncmp(old, str, len) == 0 && old[len] == '=')
    {
      if (*equals != '\0')
        envp->data[i] = str;
      else
        StringArray_excise(envp, i, 1);
      return old;
    }
  }

  // Add new entry, if needed.
  if (*equals != '\0')
    StringArray_append(envp, str);
  return NULL;
}

