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
 * I/O to the null device (i.e., no output).
 */

#include <stdio.h>


/** Write to the null console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @return Number of characters written.
 */
static int
_bme_null_write(char* s, int len, unsigned int offset, void* private)
{
  return (len);
}


/** Read from the null console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @return Number of characters read.
 */
static int
_bme_null_read(char* s, int len, unsigned int offset, void* private)
{
  return (0);
}


/** Buffer for null input and output files. */
static char _bme_null_buf[64];

/** Null file operations vector. */
static struct _file_ops _bme_null_fops =
{
  .write = _bme_null_write,
  .read = _bme_null_read
};

/** Null output file. */
FILE bme_stdout_null =
{
  .buf = _bme_null_buf,
  .len = sizeof (_bme_null_buf),
  .ptr = _bme_null_buf,
  .wrem = sizeof (_bme_null_buf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &_bme_null_fops
};

/** Null input file. */
FILE _stdin__bme_null =
{
  .buf = _bme_null_buf,
  .len = sizeof (_bme_null_buf),
  .ptr = _bme_null_buf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &_bme_null_fops
};
