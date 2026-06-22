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
 * Header for argv copier
 * @file
 */

#ifndef _SYS_BOGUX_ARGV_H
#define _SYS_BOGUX_ARGV_H

#include <stdbool.h>

/** Structure holding info about an argv-type array */
typedef struct ArrayInfo
{
  bool valid;  /* is the data OK? */
  int count;   /* number of strings in the array */
  int size;    /* total number of bytes to hold character data */
} ArrayInfo;

/** Get information on strings.
 * If validate is true, the input will be taken to be user data,
 * and checked for valid user readability.  If it is not readable,
 * "valid" will be false in the returned ArrayInfo.
 */
ArrayInfo strings_size(char** src, bool validate);

/** Copy strings using info from strings_size().
 * @param info The information collected from strings_size()
 * @param dest The address to write the copied string pointers to
 * @param dest_charptr The address to write the copied string characters to
 * @param src The address originally passed to strings_size()
 * @param va_delta The amount to add to any pointer written to dest[].
 *    This lets you plan to remap the dest data at a different VA later.
 */
void strings_copy(ArrayInfo info, char** dest, char* dest_charptr,
                  char** src, long va_delta);

#endif
