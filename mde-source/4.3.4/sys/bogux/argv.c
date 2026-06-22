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
 * Copy argv-type strings
 * @file
 */

#include <string.h>

#include "argv.h"
#include "bogux.h"
#include "mman.h"

ArrayInfo
strings_size(char** src, bool validate)
{
  ArrayInfo a;
  a.valid = false;
  a.size = a.count = 0;
  int count = 0;
  int char_space = 0;
  if (src)
  {
    for (;; ++count)
    {
      if (validate &&
          !is_valid_user_buf(&src[count], sizeof(char*), PROT_READ))
        return a;
      if (src[count] == NULL)
        break;
      if (!is_valid_user_string(src[count], PROT_READ))
        return a;
      char_space += strlen(src[count]) + 1;
    }
  }
  char_space = ROUND_UP(char_space, sizeof(int));

  a.valid = true;
  a.size = char_space;
  a.count = count;
  return a;
}

void
strings_copy(ArrayInfo a, char** dest, char* char_ptr,
             char** src, long va_delta)
{
  for (int i = 0; i < a.count; ++i)
  {
    int len = strlen(src[i]) + 1;
    memcpy(char_ptr, src[i], len);
    dest[i] = char_ptr + va_delta;
    char_ptr += len;
  }
  dest[a.count] = NULL;
}
