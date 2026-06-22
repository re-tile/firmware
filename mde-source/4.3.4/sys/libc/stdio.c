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
 * Miscellaneous standard I/O support.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util.h>


#ifdef __BME__
//
// When this module is used in the hypervisor or Bogux, they take care of
// declaring their own versions of stdout, but in the BME, we do it here
// so the application doesn't have to.
//
FILE* stdout = &bme_stdout_hvcons;
#endif


/** Flush any data in the file's buffer to its eventual destination.
 *  This is an internal routine which is not intended to be called by
 *  anything outside this file other than the putc() macro.
 * @param f File to flush.
 */
void
_flush_unlocked(FILE* f)
{
  if (!(f->flg & _FLG_W))
    return;
  int avail = f->len - f->wrem;
  int written = f->ops->write(f->buf, avail, f->pos, f->pvt);
  int unwritten = avail - written;
  f->pos += written;
  f->wrem += written;
  if (written && unwritten)
    memmove(f->buf, f->buf + written, unwritten);
  f->ptr = f->buf + unwritten;
}


int
fflush(FILE* f)
{
  flockfile(f);
  if (f->wrem < f->len)
    _flush_unlocked(f);
  funlockfile(f);
  return (0);
}


int
ffsync(FILE* f)
{
  flockfile(f);
  if (f->wrem < f->len)
    _flush_unlocked(f);
  if (f->ops->sync)
    f->ops->sync(f->pvt);
  funlockfile(f);
  return (0);
}


/** Flush any data in the file's buffer to its eventual destination, then add
 *  a character to it.  (If the buffer isn't full, or if the added character
 *  is a newline, the added character will be flushed as well, otherwise it
 *  will be left in the buffer.)
 * @param f File to flush.
 * @param c Character to add
 * @return The character added.
 */
int
_flush_and_put_unlocked(FILE* f, char c)
{
  if (!(f->flg & _FLG_W))
    return (EOF);
  if (f->wrem)
  {
   f->wrem--;
   *f->ptr++ = c;
   _flush_unlocked(f);
  }
  else
  {
   _flush_unlocked(f);
   if (f->wrem)
   {
     f->wrem--;
     *f->ptr++ = c;
     if (c == '\n')
       _flush_unlocked(f);
   }
  }
  return (c);
}


/** Retrieve more data for this file from its source, then read and return one
 * character from the resulting buffer.
 * @param f File to fill.
 * @return The character read, or EOF if we couldn't read any.
 */
int
_fill_and_get_unlocked(FILE* f)
{
  if (!(f->flg & _FLG_R))
    return (EOF);
  f->pos += f->ptr - f->buf;
  int n_char = f->ops->read(f->buf, f->len, f->pos, f->pvt);
  f->rrem = n_char;
  f->ptr = f->buf;
  if (f->rrem)
  {
    f->rrem--;
    return ((unsigned char) *f->ptr++);
  }
  else
    return (EOF);
}


/** Return the current file position, without locking; intended for internal
 *  use by other stdio routines which already hold a lock.
 * @param f File to inspect.
 * @return Number of bytes read or written since the file was initialized.
 */
long
_ftell_unlocked(FILE* f)
{
  long retval;

  if (f->flg & _FLG_W)
    retval = f->pos + f->len - f->wrem;
  else
    retval = f->pos + f->ptr - f->buf;

  return (retval);
}


long
ftell(FILE* f)
{
  flockfile(f);
  long retval = _ftell_unlocked(f);
  funlockfile(f);
  return (retval);
}


int
ungetc(int c, FILE* f)
{
  flockfile(f);
  if (c == EOF || !(f->flg & _FLG_R))
  {
    funlockfile(f);
    return (EOF);
  }
  f->rrem++;
  f->ptr--;
  *f->ptr = c;
  funlockfile(f);
  return (c);
}


/** Write a string to a file without taking out the lock.
 * @param s String to write.
 * @param f File to write to.
 */
void
_putstring_unlocked(const char* s, FILE* f)
{
  while (*s)
    _putc_unlocked(*s++, f);
}


void
putstring(const char* s, FILE* f)
{
  flockfile(f);
  _putstring_unlocked(s, f);
  funlockfile(f);
}


int
puts(const char* s)
{
  flockfile(stdout);
  _putstring_unlocked(s, stdout);
  _putc_unlocked('\n', stdout);
  funlockfile(stdout);
  return (0);
}


int
fputs(const char* s, FILE* f)
{
  flockfile(f);
  _putstring_unlocked(s, f);
  funlockfile(f);
  return (0);
}


char*
fgets(char* s, int len, FILE* f)
{
  char* ptr = s;
  int c;

  // If we have no space to read anything into, quit right now  
  if (len <= 1)
    return (NULL);

  // Leave space for the trailing null
  len--;

  flockfile(f);

  do
  {
    if ((c = _getc_unlocked(f)) == EOF)
      break;
    *ptr++ = c;
  } while (--len && c != '\n');

  funlockfile(f);

  if (ptr == s)
    return (NULL);

  *ptr = '\0';

  return (s);
}

// NOTE: These are functions, not macros, because gcc will optimize printf
// or fprintf of a single character into a call to these functions, so they
// must be linkable.
int
putchar(int c)
{
  return putc(c, stdout);
}

int
fputc(int c, FILE* f)
{
  return putc(c, f);
}
