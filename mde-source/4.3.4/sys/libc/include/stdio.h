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
 *
 * A subset of the standard I/O library.
 */

/**
 * @addtogroup libc_stdio
 * @{
 *
 * The standard I/O library used by the BME runtime (as well as by
 * other Tilera system software components, such as the Hypervisor).
 * This library is based upon the standard C I/O library but has
 * limited functionality in some areas in order to meet space and
 * implementation cost goals.
 */

#ifndef _SYS_LIBC_STDIO_H
#define _SYS_LIBC_STDIO_H

#include <stdarg.h>

#include <sys/types.h>

#ifdef __BME__
#include <tmc/spin.h>
#endif

/** Forward reference so we can use this in file_ops. */
struct _file;

#ifdef __DOXYGEN_SHIP__

/** File structure.  Represents an input or output stream. */
typedef struct _file FILE;

#else /* __DOXYGEN_SHIP__ */

/** File operations vector. */
struct _file_ops
{
  /// Write data
  int (*write)(char* data, int len, unsigned int offset, void* private);
  /// Read data
  int (*read)(char* data, int len, unsigned int offset, void* private);
  /// Sync data
  int (*sync)(void* private);
  /// Initialize
  void (*init)(struct _file* f);
};

/** File structure. */
typedef struct _file
{
  int               wrem; /**< Number of empty slots in buffer */
  int               rrem; /**< Number of full slots in buffer */
  char*             ptr;  /**< Pointer to first full/empty slot in buffer */
  char*             buf;  /**< Buffer */
  int               len;  /**< Length of buffer */
  unsigned int      pos;  /**< # of chars read/written before current buffer */
  unsigned int      flg;  /**< Mode flags */
  struct _file_ops* ops;  /**< Operations vector for this file */
  void*             pvt;  /**< Private state for this file (fd, etc.) */
#ifdef __BME__
  tmc_spin_queued_mutex_t lck; /**< Lock word for flockfile()/funlockfile() */
#endif
} FILE;

/** Flags for struct _file -> flg. */
#define _FLG_R 0x1      /**< File is readable */
#define _FLG_W 0x2      /**< File is writable */

#endif /* __DOXYGEN_SHIP__ */

#define EOF (-1)        /**< End of file */

#ifdef __BME__

/** Null output file. */
extern FILE bme_stdout_null;

/** Simulator output file. */
extern FILE bme_stdout_sim;

/** Hypervisor console output file. */
extern FILE bme_stdout_hvcons;

#endif

/** Print to a file.
 * @param f File to which output will be directed.
 * @param fmt Format string; see @ref printf for details.
 * @return Number of characters printed.
 */
int fprintf(FILE* f, const char* fmt, ...)
  __attribute__((__format__(__printf__,2,3)));

/** Print to stdout.
 * @param fmt Format string.  This printf supports the %d, %i, %u, %p, %x,
 *        %X, %o, %c, and %s descriptors, and the "#", "0", "-", " ", "+",
 *        and "'" flags.  Descriptors can take a width, but specifying a
 *        precision is not supported, except that the .* precision specifier
 *        is supported for %s only.  Floating-point output is not supported.  
 * @return Number of characters printed.
 */
int printf(const char* fmt, ...)
  __attribute__((__format__(__printf__,1,2)));

/** Print to stdout, prepending tile coordinates ("(x,y) ").
 * @param fmt Format string; see @ref printf for details.
 * @return Number of characters printed.
 */
int tprintf(const char* fmt, ...)
  __attribute__((__format__(__printf__,1,2)));

/** Print to stdout, prepending tile coordinates ("(x,y) ").
 * This is exactly the same as @ref tprintf except its arguments
 * are not checked at compile time. This allows using the "%'x"
 * extension that gcc would normally warn about. Because it stifles
 * useful warnings, this function should only be used when absolutely
 * necessary.
 * @param fmt Format string; see @ref printf for details.
 * @return Number of characters printed.
 */
int utprintf(const char* fmt, ...);

#ifdef __TILECC__
/* This function is primarily used for debug tracing, so clue in the
 * compiler to assume that an "if" containing a call to this function
 * will probably not be executed.
 */
#pragma frequency_hint NEVER tprintf
#endif

/** Print to a file.
 * @param stream File to which output will be directed.
 * @param fmt Format string; see @ref printf for details.
 * @param ap Items to be formatted.
 * @return Number of characters printed.
 */
int vfprintf(FILE* stream, const char* fmt, va_list ap)
  __attribute__((__format__(__printf__,2,0)));

/** Print to stdout.
 * @param fmt Format string; see @ref printf for details.
 * @param ap Items to be formatted.
 * @return Number of characters printed.
 */
int vprintf(const char* fmt, va_list ap)
  __attribute__((__format__(__printf__,1,0)));

/** Print to a limited-length string.
 * @param str String to which output will be directed.
 * @param size String length.
 * @param fmt Format string; see @ref printf for details.
 * @return Number of characters, not including the trailing NULL, which would
 *          have been printed if the string were long enough to hold them.  If
 *          the string actually was long enough, this is the number printed.
 */
int snprintf(char* str, size_t size, const char* fmt, ...)
  __attribute__((__format__(__printf__,3,4)));

/** Print to a limited-length string.
 * @param str String to which output will be directed.
 * @param size String length.
 * @param fmt Format string; see @ref printf for details.
 * @param ap Items to be formatted.
 * @return Number of characters, not including the trailing NULL, which would
 *          have been printed if the string were long enough to hold them.  If
 *          the string actually was long enough, this is the number printed.
 */
int vsnprintf(char* str, size_t size, const char* fmt, va_list ap)
  __attribute__((__format__(__printf__,3,0)));

/** Write a string, followed by a newline, to stdout.
 * @param s String to write. 
 */
int puts(const char* s);

/** Write a string to the named file.
 * @param s String to write. 
 * @param f File to which output will be directed.
 */
int fputs(const char* s, FILE* f);

/** Read a string from the named file.
 * @param s Buffer to fill. 
 * @param len Length of buffer.
 * @param f File from which input will be taken.
 */
char* fgets(char* s, int len, FILE* f);

/** Flush any data in the file's buffer to its eventual destination.
 * @param f File to flush.
 */
int fflush(FILE* f);

/** Block until any data written to this file has been emitted to the
 *  outside world.  Different from fflush() in that the former may return
 *  when data has been committed to a device's output buffer but not
 *  actually transmitted.  Different from the Unix fsync() system call
 *  since it takes a FILE*, not a file descriptor.
 * @param f File to flush.
 */
int ffsync(FILE* f);

/** Return the current file position.
 * @param f File to inspect.
 * @return Number of bytes read or written since the file was initialized.
 */
long ftell(FILE* f);

#ifndef __DOXYGEN_SHIP__

long _ftell_unlocked(FILE* f);

//
// These aren't intended to be called directly by any hypervisor or
// supervisor code, but we need to make them visible because the putc and
// getc macros call them.
//
void _flush_unlocked(FILE* f);
int _flush_and_put_unlocked(FILE* f, char c);
int _fill_and_get_unlocked(FILE* f);


/** Write a character to a stream, without locking */
static inline char
_putc_unlocked(char c, FILE* f)
{
  if (f->wrem)
  {
    f->wrem--;
    *f->ptr++ = c;
    if (c == '\n')
      _flush_unlocked(f);
    return c;
  }
  else
    return _flush_and_put_unlocked(f, c);
}

/** Get a character from a stream, without locking */
static inline int
_getc_unlocked(FILE* f)
{
  if (f->rrem)
  {
    f->rrem--;
    return *f->ptr++;
  }
  else
    return _fill_and_get_unlocked(f);
}

#endif /* __DOXYGEN_SHIP__ */

/** Push a character back onto a file.
 * @param c Character to push back.
 * @param f File upon which the character will be pushed back.
 * @return The character pushed back, or EOF on error.
 */
int ungetc(int c, FILE* f);

#ifdef __BME__

//
// We do locking under BME.
//

/** Lock a file. */
static inline void flockfile(FILE *f)
{
  tmc_spin_queued_mutex_lock(&f->lck);
}

/** Unlock a file. */
static inline void funlockfile(FILE *f)
{
  tmc_spin_queued_mutex_unlock(&f->lck);
}

/** Write a character to a stream */
static inline int
putc(int c, FILE* f)
{
  flockfile(f);
  int r = _putc_unlocked(c, f);
  funlockfile(f);
  return (r);
}

/** Get a character from a stream */
static inline int
getc(FILE* f)
{
  flockfile(f);
  int r = _getc_unlocked(f);
  funlockfile(f);
  return (r);
}

#else /* __BME__ */

//
// We do no locking in the hypervisor or Bogux.
//
static inline void flockfile(FILE *f) {}
static inline void funlockfile(FILE *f) {}

/** Write a character to a stream */
#define putc _putc_unlocked
/** Get a character from a stream */
#define getc _getc_unlocked

#endif /* __BME__ */

/** Default standard output file.  Under the BME, this is set by default to
 *  bme_stdout_hvcons, which sends output to the hypervisor console. */
extern FILE* stdout;

/** Default standard input file.  Under the BME, this set by default to
 *  bme_stdin_null, which always returns EOF; no other input streams are
 *  currently supported. */
extern FILE* stdin;

/** Write a character to stdout */
int putchar(int c);

/** Get a character from stdout */
#define getchar() getc(stdin)

/** Write a character to a file */
int fputc(int c, FILE* f);

/* This stdio supports non-standard functions for writing string data to
 * a file, or to stdout.
 */
#ifndef __DOXYGEN_SHIP__
void _putstring_unlocked(const char* s, FILE* f);
#endif
/** Write a string to a file.
 * @param s String to write.
 * @param f File to which output will be directed.
 */
void putstring(const char* s, FILE* f);

/** Write a string to stdout */
#define putstr(s) putstring((s), stdout)

#endif /* _SYS_LIBC_STDIO_H */

/** @} */
