/*
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
 */

/**
 * @file
 * Output of characters to the null console (i.e., no output).
 */

#include <stdio.h>

#include "sys/libc/include/util.h"


/** Write to the null console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer, unused for this file.
 * @return Number of characters written.
 */
static int
null_cons_write(char* s, int len, unsigned int offset, void* private)
{
  return (len);
}


/** Read from the null console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param private Private data pointer, unused for this file.
 * @return Number of characters read.
 */
static int
null_cons_read(char* s, int len, unsigned int offset, void* private)
{
  return (0);
}


/** Buffer for null console output file. */
static char null_buf[64];

/** Remote console output file operations vector. */
static struct _file_ops null_fops =
{
  .write = null_cons_write,
  .read = null_cons_read
};

/** UART console output file. */
FILE null_out =
{
  .buf = null_buf,
  .len = sizeof (null_buf),
  .ptr = null_buf,
  .wrem = sizeof (null_buf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &null_fops
};

/** UART console input file. */
FILE null_in =
{
  .buf = null_buf,
  .len = sizeof (null_buf),
  .ptr = null_buf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &null_fops
};
