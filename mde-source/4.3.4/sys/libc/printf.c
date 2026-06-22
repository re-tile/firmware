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
 * Formatted output to the console, or to strings.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

#include <arch/chip.h>

#ifdef __BME__
#include "../bme/bme_state.h"
#endif

// Note that we intentionally do not implement sprintf() or vsprintf() here,
// as they're a common source of security bugs and we have no preexisting
// hypervisor or supervisor code which requires them; use snprintf() or
// vsnprintf() instead.


/** Set to nonzero to use underscore separators on "%p" formats. */
int _xprintf_p_sep = 0;

/** Set to nonzero to use underscore separators on "%#X" formats. */
int _xprintf_pound_X_sep = 1;


/** Private state structure used for writes to strings. */
struct string_pvt
{
  /** Pointer to unwritten space in target string. */
  char* string;
  /** Remaining space in target string. */
  size_t remlen;
};

/** Superset of FILE used to encapsulate all data used in a string
 *  write operation. */
struct string_file
{
  /** The FILE to be used. */
  FILE f;
  /** Extra data to track writes to the real string. */
  struct string_pvt p;
  /** Temporary file buffer. */
  char buf[80];
};

/** Write to a string.
 * @param data Data to write.
 * @param len Number of bytes to write.
 * @return Number of bytes written.
 */
static int
str_write(char* data, int len, unsigned int offset, void* private)
{
  struct string_pvt* sp = private;

  size_t bytes_to_copy = min(len, sp->remlen);
  memcpy(sp->string, data, bytes_to_copy);
  sp->string += bytes_to_copy;
  sp->remlen -= bytes_to_copy;
  
  return (len);
}


/** File operations vector for strings. */
struct _file_ops strops = { str_write };


/** Initialize a file structure to write to a string.
 * @param str String to write to.
 * @param sf string_file structure to initialize.
 * @param len Length of the string; the returned file will just discard
 *        any bytes after this many have been written.
 * @return FILE pointer for the string.
 */
static FILE*
string_to_file(char* str, struct string_file* sf, int len)
{
  sf->f.buf = sf->buf;
  sf->f.len = sizeof (sf->buf);
  sf->f.ptr = sf->buf;
  sf->f.wrem = sizeof (sf->buf);
  sf->f.pos = sf->f.rrem = 0;
  sf->f.flg = _FLG_W;
  sf->f.ops = &strops;
  sf->f.pvt = &sf->p;
#ifdef __BME__
  tmc_spin_queued_mutex_init(&sf->f.lck);
#endif
  sf->p.string = str;
  sf->p.remlen = len;

  return &sf->f;
}


/** Lower-case hexadecimal digits. */
static const char hexdigits_lc[] = "0123456789abcdefx";
/** Upper-case hexadecimal digits. */
static const char hexdigits_uc[] = "0123456789ABCDEFX";

// Formatting flags

#define FMT_SPACE    0x01	/**< Always print minus sign or space */
#define FMT_ALT      0x02	/**< Alternate output (e.g., initial '0x') */
#define FMT_ZERO     0x04	/**< Fill with leading zeroes */
#define FMT_LEFT     0x08	/**< Left-justify in field */
#define FMT_PLUS     0x10	/**< Always print sign bit */
#define FMT_WIDTH    0x20	/**< Minimum width specified */
#define FMT_SEP      0x40	/**< Print separators (, for d/u, _ for x/o) */
#define FMT_PREC_ST  0x80	/**< Precision specifier of .* given */


/** Print to a file.
 * @param f File to print to.
 * @param fmt Format string.
 * @param ap Items to be formatted.
 * @return Number of characters printed.
 */
static int
xprintf(FILE* f, const char* fmt, va_list ap)
{
  long long i_val_ll;
  unsigned long long u_val_ll;
  const char* s_val;
  int s_prec;
  char c_val;

  char numbuf[32];  // 22 characters for 64-bit octal, plus slop for 0x, +, etc.
  char* n_ptr;

  char sign_char;
  const char* alt_prefix;
  const char* hexdigits;

  char c;
  int digits;

  int init_pos = _ftell_unlocked(f);

  char* n_end = &numbuf[sizeof (numbuf) - 1];
  *n_end = '\0';

  for (const char* fmt_p = fmt; *fmt_p; fmt_p++)
  {
    // Write out text until we hit a descriptor
    while (*fmt_p != '\0' && *fmt_p != '%')
      _putc_unlocked(*fmt_p++, f);

    // If at end of string, done
    if (!*fmt_p)
      break;

    // Save our location in case it turns out not to be a descriptor and we
    // have to print it out
    const char* start_desc = fmt_p++;
    int fmt_flag = 0;

    // Parse flags
    while ((c = *fmt_p) != '\0')
    {
      if (c == '#')
        fmt_flag |= FMT_ALT;
      else if (c == '0')
        fmt_flag |= FMT_ZERO;
      else if (c == '-')
        fmt_flag |= FMT_LEFT;
      else if (c == ' ')
        fmt_flag |= FMT_SPACE;
      else if (c == '+')
        fmt_flag |= FMT_PLUS;
      else if (c == '\'')
        fmt_flag |= FMT_SEP;
      else
        break;
      fmt_p++;
    }

    // Parse width, if there is one
    int width = 0;
    if (*fmt_p >= '0' && *fmt_p <= '9')
    {
      fmt_flag |= FMT_WIDTH;
      while (*fmt_p >= '0' && *fmt_p <= '9')
        width = (width * 10) + (*fmt_p++ - '0');
    }

    // See if there's a .* precision specifier (note that we don't currently
    // support the .n style, and that this is only useful for the %s format)
    if (*fmt_p == '.' && *(fmt_p + 1) == '*')
    {
      fmt_flag |= FMT_PREC_ST;
      fmt_p += 2;
    }

    // If at end of string, this wasn't a descriptor, print it
    if (!*fmt_p)
    {
      _putstring_unlocked(start_desc, f);
      return (_ftell_unlocked(f) - init_pos);
    }

    // See if there are any l modifiers
    int ells = 0;
    while (*fmt_p == 'l' && ells < 2)
    {
      ells++;
      fmt_p++;
    }

    // Check for 'z' (size_t), 'j' (intmax_t), and 't' (ptrdiff_t).
    // We treat them all as 'l', assuming ILP32 or LP64.
    if (ells == 0 && (*fmt_p == 'z' || *fmt_p == 'j' || *fmt_p == 't'))
    {
      ells++;
      fmt_p++;
    }

    // If at end of string, this wasn't a descriptor, print it
    if (!*fmt_p)
    {
      _putstring_unlocked(start_desc, f);
      return (_ftell_unlocked(f) - init_pos);
    }

    // Hopefully, we now have a conversion specifier
    switch (*fmt_p)
    {
    case 'd':
    case 'i':
      if (ells >= 2)
        i_val_ll = va_arg(ap, long long);
      else if (ells == 1)
        i_val_ll = va_arg(ap, long);
      else
        i_val_ll = va_arg(ap, int);
  
      if (i_val_ll < 0)
      {
        sign_char = '-';
        i_val_ll = -i_val_ll;
      }
      else
        sign_char = 0;
  
      n_ptr = n_end;
      if (i_val_ll)
      {
        digits = 0;
        while (i_val_ll)
        {
          *--n_ptr = hexdigits_lc[i_val_ll % 10];
          i_val_ll /= 10;
          width--;
          if (fmt_flag & FMT_SEP)
          {
            digits++;
            if (digits && (digits % 3) == 0 && i_val_ll)
            {
              *--n_ptr = ',';
              width--;
            }
          }
        }
      }
      else
      {
        *--n_ptr = '0';
        width--;
      }

      if (!sign_char)
      {
        if (fmt_flag & FMT_SPACE)
          sign_char = ' ';
        else if (fmt_flag & FMT_PLUS)
          sign_char = '+';
      }

      if (sign_char)
        width--;

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_ZERO))
      {
        if (sign_char)
          _putc_unlocked(sign_char, f);
        while (width-- > 0)
          _putc_unlocked('0', f);
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && !(fmt_flag & FMT_LEFT))
      {
        while (width-- > 0)
          _putc_unlocked(' ', f);
        if (sign_char)
          _putc_unlocked(sign_char, f);
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_LEFT))
      {
        if (sign_char)
          _putc_unlocked(sign_char, f);
        _putstring_unlocked(n_ptr, f);
        while (width-- > 0)
          _putc_unlocked(' ', f);
      }
      else
      {
        if (sign_char)
          _putc_unlocked(sign_char, f);
        _putstring_unlocked(n_ptr, f);
      }

      break;

    case 'u':
      if (ells >= 2)
        u_val_ll = va_arg(ap, unsigned long long);
      else if (ells == 1)
        u_val_ll = va_arg(ap, unsigned long);
      else
        u_val_ll = va_arg(ap, unsigned int);
  
      n_ptr = n_end;
      if (u_val_ll)
      {
        digits = 0;
        while (u_val_ll)
        {
          *--n_ptr = hexdigits_lc[u_val_ll % 10];
          u_val_ll /= 10;
          width--;
          if (fmt_flag & FMT_SEP)
          {
            digits++;
            if (digits && (digits % 3) == 0 && u_val_ll)
            {
              *--n_ptr = ',';
              width--;
            }
          }
        }
      }
      else
      {
        *--n_ptr = '0';
        width--;
      }

      if (fmt_flag & FMT_WIDTH)
      {
        if (fmt_flag & FMT_ZERO)
          while (width-- > 0)
            _putc_unlocked('0', f);
        else if (!(fmt_flag & FMT_LEFT))
          while (width-- > 0)
            _putc_unlocked(' ', f);
      }

      _putstring_unlocked(n_ptr, f);

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_LEFT))
        while (width-- > 0)
          _putc_unlocked(' ', f);

      break;

    case 'p':
      // Void pointer
      fmt_flag |= FMT_ALT;

      if (_xprintf_p_sep)
        fmt_flag |= FMT_SEP;

      ells = 1;

      // Fall through to...

    case 'x':
      // Lowercase hexadecimal
      // Fall through to...

    case 'X':
      // Uppercase hexadecimal

      hexdigits = hexdigits_lc;
      alt_prefix = "0x";
      
      if (*fmt_p == 'X')
      {
        if ((fmt_flag & FMT_ALT) && _xprintf_pound_X_sep)
          fmt_flag |= FMT_SEP;
        else
        {
          hexdigits = hexdigits_uc;
          alt_prefix = "0X";
        }
      }

      if (ells >= 2)
        u_val_ll = va_arg(ap, unsigned long long);
      else if (ells == 1)
        u_val_ll = va_arg(ap, unsigned long);
      else
        u_val_ll = va_arg(ap, unsigned int);

      n_ptr = n_end;
      if (u_val_ll)
      {
        digits = 0;

        while (u_val_ll)
        {
          *--n_ptr = hexdigits[u_val_ll & 0xF];
          u_val_ll >>= 4;
          width--;
          if (fmt_flag & FMT_SEP)
          {
            digits++;
            if (digits && (digits & 3) == 0 && u_val_ll)
            {
              *--n_ptr = '_';
              width--;
            }
          }
        }
        if (fmt_flag & FMT_ALT)
          width -= 2;
        else
          alt_prefix = "";
      }
      else
      {
        digits = 1;
        *--n_ptr = '0';
        width--;
        alt_prefix = "";
      }

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_ZERO))
      {
        _putstring_unlocked(alt_prefix, f);
        if (fmt_flag & FMT_SEP)
        {
          int place = width + digits + (digits - 1) / 4;
          while (width-- > 0)
          {
            if ((place % 5) == 0)
              _putc_unlocked('_', f);
            else
              _putc_unlocked('0', f);
            place--;
          }
        }
        else
        {
          while (width-- > 0)
            _putc_unlocked('0', f);
        }
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && !(fmt_flag & FMT_LEFT))
      {
        while (width-- > 0)
          _putc_unlocked(' ', f);
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_LEFT))
      {
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
        while (width-- > 0)
          _putc_unlocked(' ', f);
      }
      else
      {
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
      }

      break;

    case 'o':
      // Octal
      if (ells >= 2)
        u_val_ll = va_arg(ap, unsigned long long);
      else if (ells == 1)
        u_val_ll = va_arg(ap, unsigned long);
      else
        u_val_ll = va_arg(ap, unsigned int);
  
      n_ptr = n_end;
      alt_prefix = "";
      if (u_val_ll)
      {
        while (u_val_ll)
        {
          *--n_ptr = hexdigits_lc[u_val_ll & 0x7];
          u_val_ll >>= 3;
          width--;
        }
        if (fmt_flag & FMT_ALT)
        {
          alt_prefix = "0";
          width -= 1;
        }
      }
      else
      {
        *--n_ptr = '0';
        width--;
      }

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_ZERO))
      {
        _putstring_unlocked(alt_prefix, f);
        while (width-- > 0)
          _putc_unlocked('0', f);
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && !(fmt_flag & FMT_LEFT))
      {
        while (width-- > 0)
          _putc_unlocked(' ', f);
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
      }
      else if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_LEFT))
      {
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
        while (width-- > 0)
          _putc_unlocked(' ', f);
      }
      else
      {
        _putstring_unlocked(alt_prefix, f);
        _putstring_unlocked(n_ptr, f);
      }

      break;

    case 'c':
      // Character.  Note that we use 'int' here rather than 'char'
      // because 'char' gets promoted to 'int' in '...'.
      c_val = (char) va_arg(ap, int);
      _putc_unlocked(c_val, f);
      break;

    case 's':
      // String

      if (fmt_flag & FMT_PREC_ST)
        s_prec = va_arg(ap, const int);
      else
        s_prec = INT_MAX;

      s_val = va_arg(ap, const char*);

      if (s_val == NULL)
        s_val = "(null)";

      if (fmt_flag & FMT_WIDTH)
      {
        int len = strlen(s_val);
        if (len > s_prec)
          len = s_prec;
        width -= len;
        if (fmt_flag & FMT_LEFT)
        {
          while (*s_val && s_prec--)
            _putc_unlocked(*s_val++, f);
          while (width-- > 0)
            _putc_unlocked(' ', f);
        }
        else
        {
          while (width-- > 0)
            _putc_unlocked(' ', f);
          while (*s_val && s_prec--)
            _putc_unlocked(*s_val++, f);
        }
      }
      else
        while (*s_val && s_prec--)
          _putc_unlocked(*s_val++, f);

      break;

    case '%':
      // A percent sign
      _putc_unlocked('%', f);
      break;

    default:
      // Not something we support, print out the whole thing
      while (start_desc <= fmt_p)
        _putc_unlocked(*start_desc++, f);
      break;
    }
  }

  return (_ftell_unlocked(f) - init_pos);
}


int
printf(const char *fmt, ...)
{
  va_list ap;
  int retval;

  va_start(ap, fmt);
  flockfile(stdout);
  retval = xprintf(stdout, fmt, ap);
  funlockfile(stdout);
  va_end(ap);

  return (retval);
}


int
tprintf(const char *fmt, ...)
{
  va_list ap;
  int retval;

  va_start(ap, fmt);
  flockfile(stdout);

#ifdef __BME__
  _putstring_unlocked(_bme_get_state()->tprintf_prefix, stdout);
#else
  _putstring_unlocked(tprintf_prefix, stdout);
#endif
  retval = xprintf(stdout, fmt, ap);
  funlockfile(stdout);
  va_end(ap);

  return (retval);
}


#ifndef __DOXYGEN__
int utprintf(const char* fmt, ...) __attribute__((alias("tprintf")));
#endif


/** Prefix for tprintf. */
char* tprintf_prefix = "";


int
fprintf(FILE* f, const char *fmt, ...)
{
  va_list ap;
  int retval;

  va_start(ap, fmt);
  flockfile(f);
  retval = xprintf(f, fmt, ap);
  funlockfile(f);
  va_end(ap);

  return (retval);
}


int
snprintf(char *str, size_t size, const char *fmt, ...)
{
  va_list ap;
  int retval;
  FILE* f;
  struct string_file sf;

  f = string_to_file(str, &sf, size);

  va_start(ap, fmt);
  retval = xprintf(f, fmt, ap);
  va_end(ap);

  fflush(f);

  if (retval < size)
    str[retval] = '\0';
  else if (size > 0)
    str[size - 1] = '\0';

  return (retval);
}


int vprintf(const char *fmt, va_list ap)
{
  int retval;

  flockfile(stdout);
  retval = xprintf(stdout, fmt, ap);
  funlockfile(stdout);

  return (retval);
}


int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
  int retval;

  flockfile(stream);
  retval = xprintf(stream, fmt, ap);
  funlockfile(stream);

  return (retval);
}


int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
  int retval;
  FILE* f;
  struct string_file sf;

  f = string_to_file(str, &sf, size);
  retval = xprintf(f, fmt, ap);

  fflush(f);

  if (retval < size)
    str[retval] = '\0';
  else
    str[size] = '\0';

  return (retval);
}
