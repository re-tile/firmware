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
 * Formatted output to the console.  This version of printf is designed
 * for use in the booter, and thus has fewer features than the standard
 * sys/libc printf.  It omits octal, unsigned, and upper-case hexadecimal
 * support, and the '-', ' ', '+', and ',' modifiers.
 *
 */

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "hv_l1boot.h"

/** Hexadecimal digits. */
static const char hexdigits[] = "0123456789abcdefx";

// Formatting flags

#define FMT_ALT      0x02       /**< Alternate output (e.g., initial '0x') */
#define FMT_ZERO     0x04       /**< Fill with leading zeroes */
#define FMT_WIDTH    0x20       /**< Minimum width specified */
#define FMT_PREC_ST  0x80       /**< Precision specifier of .* given */


/** Write a string to the console.
 * @param s String to write.
 */
static void
boot_putstring(const char *s)
{
  while (*s)
    boot_putchar(*s++);
}


/** Print to the console.
 * @param fmt Format string.
 */
void
boot_printf(const char *fmt, ...)
{
  const char* fmt_p;
  va_list ap;

  long long i_val_ll;
  unsigned long long u_val_ll;
  const char* s_val;
  int s_prec;
  char c_val;

  char numbuf[16];  // 10 characters for 32-bit unsigned, plus slop for 0x, etc.
  char* n_ptr;
  char* n_end;

  char sign_char;
  const char* alt_prefix;

  int ells;
  int width = 0;
  int fmt_flag;
  char c;
  const char* start_desc;

  n_end = &numbuf[sizeof (numbuf) - 1];
  *n_end = '\0';

  va_start(ap, fmt);

  for (fmt_p = fmt; *fmt_p; fmt_p++)
  {
    // Write out text until we hit a descriptor
    while (*fmt_p != '\0' && *fmt_p != '%')
      boot_putchar(*fmt_p++);

    // If at end of string, done
    if (!*fmt_p)
      break;

    // Save our location in case it turns out not to be a descriptor and we
    // have to print it out
    start_desc = fmt_p++;
    fmt_flag = 0;

    // Parse flags
    while ((c = *fmt_p) != '\0')
    {
      if (c == '#')
        fmt_flag |= FMT_ALT;
      else if (c == '0')
        fmt_flag |= FMT_ZERO;
      else
        break;
      fmt_p++;
    }

    // Parse width, if there is one
    if (*fmt_p >= '0' && *fmt_p <= '9')
    {
      fmt_flag |= FMT_WIDTH;
      width = 0;
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

    // See if there are any l modifiers
    ells = 0;
    while (*fmt_p == 'l' && ells < 2)
    {
      ells++;
      fmt_p++;
    }

    // If at end of string, this wasn't a descriptor, print it
    if (!*fmt_p)
    {
      boot_putstring(start_desc);
      va_end(ap);
      return;
    }

    // Hopefully, we now have a conversion specifier
    switch (*fmt_p)
    {
    case 'd':
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
        while (i_val_ll)
        {
          *--n_ptr = hexdigits[i_val_ll % 10];
          i_val_ll /= 10;
          width--;
        }
      }
      else
      {
        *--n_ptr = '0';
        width--;
      }

      if (sign_char)
        width--;

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_ZERO))
      {
        if (sign_char)
          boot_putchar(sign_char);
        while (width-- > 0)
          boot_putchar('0');
        boot_putstring(n_ptr);
      }
      else if (fmt_flag & FMT_WIDTH)
      {
        while (width-- > 0)
          boot_putchar(' ');
        if (sign_char)
          boot_putchar(sign_char);
        boot_putstring(n_ptr);
      }
      else
      {
        if (sign_char)
          boot_putchar(sign_char);
        boot_putstring(n_ptr);
      }

      break;

    //
    // At the moment we think we can get away without %u, so it's #ifdef'ed
    // out.
    //
#ifdef BOOT_PRINTF_NEED_UNSIGNED
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
        while (u_val_ll)
        {
          *--n_ptr = hexdigits[u_val_ll % 10];
          u_val_ll /= 10;
          width--;
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
            boot_putchar('0');
        else
          while (width-- > 0)
            boot_putchar(' ');
      }

      boot_putstring(n_ptr);

      break;
#endif

    case 'x':
      // Hexadecimal

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
          *--n_ptr = hexdigits[u_val_ll & 0xF];
          u_val_ll >>= 4;
          width--;
        }
        if (fmt_flag & FMT_ALT)
        {
          alt_prefix = (*fmt_p == 'X') ? "0X" : "0x";
          width -= 2;
        }
      }
      else
      {
        *--n_ptr = '0';
        width--;
      }

      if ((fmt_flag & FMT_WIDTH) && (fmt_flag & FMT_ZERO))
      {
        boot_putstring(alt_prefix);
        while (width-- > 0)
          boot_putchar('0');
        boot_putstring(n_ptr);
      }
      else if (fmt_flag & FMT_WIDTH)
      {
        while (width-- > 0)
          boot_putchar(' ');
        boot_putstring(alt_prefix);
        boot_putstring(n_ptr);
      }
      else
      {
        boot_putstring(alt_prefix);
        boot_putstring(n_ptr);
      }

      break;

    case 'c':
      // Character.  Note that we use 'int' here rather than 'char'
      // because 'char' gets promoted to 'int' in '...'.
      c_val = (char) va_arg(ap, int);
      boot_putchar(c_val);
      break;

    case 's':
      // String

      if (fmt_flag & FMT_PREC_ST)
        s_prec = va_arg(ap, const int);
      else
        s_prec = INT_MAX;

      s_val = va_arg(ap, const char*);

      if (fmt_flag & FMT_WIDTH)
      {
        int len = strlen(s_val);
        if (len > s_prec)
          len = s_prec;
        width -= len;
        while (width-- > 0)
          boot_putchar(' ');
        while (*s_val && s_prec--)
          boot_putchar(*s_val++);
      }
      else
        while (*s_val && s_prec--)
          boot_putchar(*s_val++);

      break;

    case '%':
      // A percent sign
      boot_putchar('%');
      break;

    default:
      // Not something we support, print out the whole thing
      while (start_desc <= fmt_p)
        boot_putchar(*start_desc++);
      break;
    }
  }

  va_end(ap);
  return;
}
