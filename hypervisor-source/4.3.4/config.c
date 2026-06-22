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
 * The configuration file parser.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

#include <arch/chip.h>

#include "config.h"
#include "cons_uart.h"
#include "console.h"
#include "debug.h"
#include "devices.h"
#include "drvchan.h"
#include "filesys.h"
#include "hv.h"
#include "mapping.h"
#include "misc.h"
#include "mshim_acc.h"
#include "types.h"

/** We fill this in as a result of our config file parsing, and other routines
 *  read it to get their information.  (We also fill in data in other places,
 *  like the device table.)
 */
struct config config;

/** Which client in config.clients[] are we? */
int my_client;

///
/// @name Status bits for parse_xxx() helper functions
/// @{
#define STAT_TOK     0x1   /**< Token found */
#define STAT_INDENT  0x2   /**< Line on which the token was found was indented;
                                only valid for the first token on the line */
#define STAT_EOF     0x4   /**< End of file encountered before token found */
#define STAT_ERR     0x8   /**< Token was too long for buffer provided */
/// @}

/** Get the next token from the input stream.
 * @param f File to read.
 * @param tok Buffer into which the token is written.  The token will always
 *        be null-terminated.
 * @param len Length of tok in bytes.
 * @param cross_eol If true, parse_tok will continue parsing over newlines to
 *        try to find a token.
 * @param lineno Pointer to current line number, updated by routine if it
 *        processes more than one line.
 * @return STAT_xxx flags (see above).
 */
static int
parse_tok(FILE* f, char* tok, int len, int cross_eol, int* lineno)
{
  char* ptr = tok;
  int indent = 0;   // Set to STAT_INDENT if this token is idented

  while (1)
  {
    // Note that more getc() calls are below, so we may read more than one
    // character per loop iteration.
    int c = getc(f);

    // If we hit EOF here, we hadn't gotten a token, so just return EOF.
    if (c == EOF)
      return (STAT_EOF);

    // If we hit a newline, and we aren't crossing them, we won't find a token
    // so we return 0; we don't consume the newline.  Otherwise, we say we
    // haven't seen an indent yet, and go back around.
    if (c == '\n')
    {
      if (!cross_eol)
      {
        ungetc(c, f);
        return (0);
      }

      (*lineno)++;

      indent = 0;
      continue;
    }

    // If we hit a space, the next token on this line is indented.
    if (isspace(c))
    {
      indent = STAT_INDENT;
      continue;
    }

    // We must have hit a non-blank character, so this is the start of
    // our token.  Slurp it in; if it's too long, return an error;
    // otherwise terminate it and return it.  Note that if our token
    // ends in a newline we never consume it; our caller may want to
    // look for subsequent tokens on the same line.
    do
    {
      if (--len < 1)
        return (STAT_ERR);
      *ptr++ = c;
      c = getc(f);
    } while (c != EOF && !isspace(c));

    ungetc(c, f);

    *ptr = '\0';
    return (STAT_TOK | indent);
  }
}

/** Parse a number from an input stream which will be used as part of a tile
 *  identifier (either an x or y coordinate, a rectangle width or height,
 *  or a Linux CPU number).  The parsing ends when the first non-digit
 *  character is retrieved from the file; this character will be pushed
 *  back on the input stream so that the caller can retrieve it.
 * @param f File to read.
 * @param c First ASCII character of the number, which the caller has already
 *   retrieved from the input stream.
 * @return The parsed value.  If the value calculated from the input digits
 *   would be greater than 2048, 2048 will be returned.
 */
uint
parse_tile_number(FILE* f, int c)
{
  uint accum = 0;
  do
  {
    accum = accum * 10 + c - '0';
    if (accum > 2047)
      return (2048);
    c = getc(f);
  } while (isdigit(c));
  ungetc(c, f);

  return (accum);
}


/** Get the next token of the form <number>,<number> or <number>x<number>
 *  from the input stream; if it's an <x,y> position, add chip_logical_ulhc to
 *  it (so the user sees a (0,0)-based coordinate system); return the result
 *  as a pos_t.
 * @param f File to read.
 * @param pos Position structure to fill in; only modified if STAT_TOK is
 *            set in the return value.
 * @param want_comma If nonzero, require a comma as the separator; otherwise,
 *        require an x or an X.
 * @return STAT_xxx flags (see above).
 */
static int
parse_pos(FILE* f, pos_t* pos, int want_comma)
{
  int c = getc(f);
  int x = 0;
  int y = 0;

  // Skip leading whitespace.
  while (c != '\n' && isspace(c))
    c = getc(f);

  // If no text left, say we didn't get anything.
  if (c == '\n')
  {
    ungetc(c, f);
    return (0);
  }

  // We have to start with a digit.
  if (!isdigit(c))
  {
    ungetc(c, f);
    return (STAT_ERR);
  }

  // Parse the X value.
  x = parse_tile_number(f, c);
  if (x > 2047)
    return (STAT_ERR);
  c = getc(f);
  // It's OK to have spaces around the central , or x character.
  while (c != '\n' && isspace(c))
    c = getc(f);

  // Is the separator what we expect?
  if (want_comma ? (c != ',') : (c != 'x' && c != 'X'))
  {
    ungetc(c, f);
    return (STAT_ERR);
  }

  // Skip to the Y value, make sure it's numeric.
  do
  {
    c = getc(f);
  } while (c != '\n' && isspace(c));

  if (!isdigit(c))
  {
    ungetc(c, f);
    return (STAT_ERR);
  }

  // Parse the Y value.
  y = parse_tile_number(f, c);
  if (y > 2047)
    return (STAT_ERR);

  pos->word = 0;
  if (want_comma)
  {
    pos->bits.x = x + chip_logical_ulhc.bits.x;
    pos->bits.y = y + chip_logical_ulhc.bits.y;
  }
  else
  {
    pos->bits.x = x;
    pos->bits.y = y;
  }
  return (STAT_TOK);
}


/** Skip to the end of the current line; if we pass any non-whitespace
 *  characters on the way there, return an error.
 * @param f File to read.
 * @return STAT_xxx flags (see above).
 */
static int
nextline(FILE* f)
{
  int retval = 0;

  while (1)
  {
    int c = getc(f);

    if (c == EOF)
      return (retval | STAT_EOF);

    if (c == '\n')
    {
      ungetc(c, f);
      return (retval);
    }

    if (!isspace(c))
    {
      // Not a space -> extra gunk we want to complain about.
      retval = STAT_ERR;
    }
  }
}


/** Read in up to a line of text, not including the trailing newline.
 * @param f File to read.
 * @param buf Pointer to buffer to fill with the text.
 * @param buflen Length of buf in bytes.
 * @return Number of bytes read.
 */
static int
readline(FILE* f, char* buf, int buflen)
{
  int retval = 0;

  //
  // Make sure we have space for a trailing null.
  //
  if (buflen <= 0)
    return 0;
  else
    buflen--;

  while (buflen)
  {
    int c = getc(f);

    if (c == EOF)
      break;

    if (c == '\n')
    {
      ungetc(c, f);
      break;
    }

    *buf++ = c;
    retval++;
    buflen--;
  }

  *buf = '\0';

  return (retval);
}

/** Compute 10^x.
 * @param exp Exponent.
 */
static int64_t inline
intpow10(unsigned int exp)
{
  //
  // Yes, there are ways to do this with fewer multiplications, but this
  // is rarely called, and when it is, exp is highly likely to be less
  // than 3, so lots of optimization isn't really called for.
  //
  int64_t rv = 1;

  while (exp--)
    rv *= 10;

  return (rv);
}


/** Turn a string into a number.  If there's a leading 0x or 0X, the string
 *  is interpreted as hexadecimal; a leading 0, octal; otherwise, decimal.
 *  If a trailing "k", "m", or "g" is present, the value is multiplied by
 *  2^10, 2^20, or 2^30, respectively.  Decimal values may include a decimal
 *  point and a fractional part, although this is only really useful when a
 *  k/m/g suffix is supplied, or when the scale parameter is specified,
 *  since the fractional part is truncated (not rounded) before return.
 *  Negative values are not permitted and will result in a parsing error.
 * @param string String to parse.  @param val_p Pointer to returned value.
 * @param ll_flag If nonzero, values which cannot be represented in 32 bits
 *        are permitted; otherwise they produce a parsing error.  (The returned
 *        value is always 64-bit, no matter what this flag is set to.)
 * @param scale If nonzero, returned values are multiplied by 10^scale.
 *        This occurs before the value is truncated.  So, if scale was set
 *        to 2, then an input string of "1" would result in a return value
 *        of 100, "1.25k" would result in a return value of 128000, and "10.2"
 *        would result in a return value of 1020.
 * @return Nonzero if there was a parsing error, else zero.
 */
static int
str2number(char* string, int64_t* val_p, int ll_flag, unsigned int scale)
{
  int64_t whole;       // Whole number part of input
  int64_t frac = 0;    // Fractional part of input
  int fracplaces = 0;  // Number of places in fractional part
  char* stopcharp;

  //
  // Get the whole and fractional parts of the string.
  //
  int err = str2ll(string, &stopcharp, 0, &whole);

  if (err || whole < 0)
    return (1);

  if (*stopcharp == '.')
  {
    if (*string == '0' && stopcharp > string + 1)
    {
      // This is hex or octal; we don't allow a decimal point.
      return (2);
    }
    else
    {
      char* startfrac = stopcharp + 1;
      err = str2ll(startfrac, &stopcharp, 10, &frac);
      if (err || frac < 0)
        return (3);
      fracplaces = stopcharp - startfrac;
    }
  }

  //
  // Multiply by the scale factor if specified.
  //
  if (scale)
  {
    whole *= intpow10(scale);
    if (scale >= fracplaces)
    {
      whole += frac * intpow10(scale - fracplaces);
      frac = 0;
    }
    else
    {
      fracplaces -= scale;
      whole += frac / intpow10(fracplaces);
      frac %= intpow10(fracplaces);
    }
  }

  //
  // Handle any suffixes.
  //
  int suf_shift = 0;

  if (tolower(*stopcharp) == 'k')
  {
    suf_shift = 10;
    stopcharp++;
  }
  else if (tolower(*stopcharp) == 'm')
  {
    suf_shift = 20;
    stopcharp++;
  }
  else if (tolower(*stopcharp) == 'g')
  {
    suf_shift = 30;
    stopcharp++;
  }

  if (suf_shift)
  {
    whole <<= suf_shift;
    if (frac)
      whole += (frac << suf_shift) / intpow10(fracplaces);
  }

  //
  // Make sure there isn't any trailing garbage, verify we fit into the
  // requested size, return.
  //
  if (*stopcharp != '\0')
    return (4);

  if (!ll_flag && (whole < LONG_MIN || whole > LONG_MAX))
    return (5);

  *val_p = whole;
  return (0);
}


/** Turn a string into a pos_t.  The input is in the user-visible (0,0)-based
 *  coordinate system; the returned value is in the real (1,1)-based system.
 * @param string String to parse.
 * @param val_p Pointer to returned value.
 * @return Nonzero if there was a parsing error, else zero.
 */
static int
str2pos(char* string, pos_t* val_p)
{
  long x, y;
  char* stopcharp;

  int err = str2l(string, &stopcharp, 10, &x);

  if (err || *stopcharp != ',' || x < 0)
    return (1);

  err = str2l(stopcharp + 1, &stopcharp, 10, &y);

  if (err || *stopcharp != '\0' || y < 0)
    return (1);

  *val_p = (pos_t)
  {
    .bits.x = x + chip_logical_ulhc.bits.x,
    .bits.y = y + chip_logical_ulhc.bits.y,
  };

  return (0);
}

/** Get the next option (of the form "foo", or "foo=bar") from the input
 *  stream.
 * @param f File to read.
 * @param opt Buffer into which the option name is written.  The name will
 *        always be null-terminated.
 * @param optlen Length of opt in bytes.
 * @param val Buffer into which the option value is written.  The name will
 *        always be null-terminated.  If no value is present, *val will be
 *        '\0'.
 * @param vallen Length of val in bytes.
 * @return STAT_xxx flags (see above).
 */
static int
parse_option(FILE* f, char* opt, int optlen, char* val, int vallen)
{
  while (1)
  {
    // Note that more getc() calls are below, so we may read more than one
    // character per loop iteration.
    int c = getc(f);

    // If we hit EOF here, we haven't seen an option, so just return EOF.
    if (c == EOF)
      return (STAT_EOF);

    // If we hit a newline, we won't find an option so we return 0; we don't
    // consume the newline.
    if (c == '\n')
    {
      ungetc(c, f);
      return (0);
    }

    // If we hit a space, we skip it.
    if (isspace(c))
      continue;

    // We must have hit a non-blank character, so this is the start of
    // our option.  Slurp it in; if it's too long, return an error;
    // otherwise terminate it and then go to look for the value.
    do
    {
      if (--optlen < 1)
        return (STAT_ERR);
      *opt++ = c;
      c = getc(f);
    } while (c != EOF && c != '=' && !isspace(c));

    *opt = '\0';

    // Now see whether we have an option value.  If so, process it like the
    // name.

    if (c == '=')
    {
      c = getc(f);
      while (c != EOF && !isspace(c))
      {
        if (--vallen < 1)
          return (STAT_ERR);
        *val++ = c;
        c = getc(f);
      }
    }

    *val = '\0';

    // Note that if our option or value ends in a newline we don't consume it.
    ungetc(c, f);
    return (STAT_TOK);
  }
}


/** Examine two rectangles and determine whether they overlap.
 * @param ulhc_a Upper left hand corner of the first rectangle.
 * @param lrhc_a Lower right hand corner of the first rectangle.
 * @param ulhc_b Upper left hand corner of the other rectangle.
 * @param lrhc_b Lower right hand corner of the other rectangle.
 * @return Nonzero if the two rectangles overlap, zero otherwise.
 */
static int
overlap(pos_t ulhc_a, pos_t lrhc_a, pos_t ulhc_b, pos_t lrhc_b)
{
  return (!(lrhc_b.bits.x < ulhc_a.bits.x || ulhc_b.bits.x > lrhc_a.bits.x ||
            lrhc_b.bits.y < ulhc_a.bits.y || ulhc_b.bits.y > lrhc_a.bits.y));
}


/** Examine two rectangles and determine whether the second is contained in
 *  the first.
 * @param ulhc_a Upper left hand corner of the enclosing rectangle.
 * @param lrhc_a Lower right hand corner of the enclosing rectangle.
 * @param ulhc_b Upper left hand corner of the enclosed rectangle.
 * @param lrhc_b Lower right hand corner of the enclosed rectangle.
 * @return Nonzero if a contains b, zero otherwise.
 */
static int
within(pos_t ulhc_a, pos_t lrhc_a, pos_t ulhc_b, pos_t lrhc_b)
{
  return (ulhc_b.bits.x >= ulhc_a.bits.x && ulhc_b.bits.y >= ulhc_a.bits.y &&
          lrhc_b.bits.x <= lrhc_a.bits.x && lrhc_b.bits.y <= lrhc_a.bits.y);
}


/** Parse a tile mask specification, as used in, e.g., the bme subcommand.
 *  Note that although the input tile specs are in client space, the returned
 *  mask is chip-relative.
 * @param f File to read.
 * @param mask Mask to be filled in.  This should be set to its default
 *   value before calling this routine; it will not be cleared if no tile
 *   specification is present in the input file, and it will be subtracted
 *   from if the first tilespec begins with ^.
 * @param client_ulhc Coordinates of client upper-left-hand-corner.
 * @param client_lrhc Coordinates of client lower-right-hand-corner.
 * @return STAT_xxx flags (see above).
 */
static int
parse_tilemask(FILE* f, tile_mask* mask, pos_t client_ulhc,
               pos_t client_lrhc)
{
  uint accum = 0;       // Number being accumulated
  int subtracting = 0;  // Nonzero if we'll subtract these tiles from the mask
  uint x = 0;           // X coordinate of ULHC of rectangle to be added
  uint y = 0;           // Y coordinate of ULHC of rectangle to be added
  uint w = 0;           // Width of rectangle to be added
  uint h = 0;           // Height of rectangle to be added
  uint first_cpu = 0;   // First CPU in range to be added
  uint last_cpu = 0;    // Last CPU in range to be added
  const int client_w =  // Width of client rectangle
    client_lrhc.bits.x - client_ulhc.bits.x + 1;
  const int client_h =  // Height of client rectangle
    client_lrhc.bits.y - client_ulhc.bits.y + 1;

  enum
  {
    START, IDLE, HAVE_CARET, HAVE_NUMBER,
    GET_2NDCPU, GET_Y, GET_H, GET_AT, GET_X,
    ADD_CPUS2MASK, ADD_RECT2MASK
  }
  state = START;

  while (1)
  {
    // Note that more getc() calls are below, so we may read more than one
    // character per loop iteration.
    int c = getc(f);

    switch (state)
    {
    //
    // We haven't see anything non-blank yet.
    //
    case START:
      if (isdigit(c))
      {
        ungetc(c, f);
        clear_tile_mask(mask);
        state = IDLE;
      }
      else if (c == '^')
      {
        state = HAVE_CARET;
      }
      else if (isspace(c))
      {
        // Do nothing, discard
      }
      else
      {
        ungetc(c, f);
        state = IDLE;
      }
      break;

    //
    // We've seen something, but we aren't currently in the middle of a
    // tile specifier.
    //
    case IDLE:
      subtracting = 0;

      if (isdigit(c))
      {
        accum = parse_tile_number(f, c);
        state = HAVE_NUMBER;
      }
      else if (c == '^')
      {
        state = HAVE_CARET;
      }
      else if (c == '\n')
      {
        ungetc(c, f);
        return (STAT_TOK);
      }
      else if (isspace(c))
      {
        // Do nothing, discard
      }
      else
        return (STAT_ERR);
      break;

    //
    // We just saw an ^.
    //
    case HAVE_CARET:
      subtracting = 1;

      if (isdigit(c))
      {
        accum = parse_tile_number(f, c);
        state = HAVE_NUMBER;
      }
      else
      {
        ungetc(c, f);
        return (STAT_ERR);
      }
      break;

    //
    // We've just finished parsing a number.
    //
    case HAVE_NUMBER:
      if (isspace(c))
      {
        ungetc(c, f);
        first_cpu = last_cpu = accum;
        state = ADD_CPUS2MASK;
      }
      else if (c == '-')
      {
        first_cpu = accum;
        state = GET_2NDCPU;
      }
      else if (c == ',')
      {
        w = h = 1;
        x = accum;
        state = GET_Y;
      }
      else if (c == 'x')
      {
        w = accum;
        state = GET_H;
      }
      else
        return (STAT_ERR);
      break;

    //
    // We've just parsed a number and a minus sign.
    //
    case GET_2NDCPU:
      if (isdigit(c))
      {
        last_cpu = parse_tile_number(f, c);
        state = ADD_CPUS2MASK;
      }
      else
      {
        ungetc(c, f);
        return (STAT_ERR);
      }
      break;

    //
    // We've just parsed a number and a comma.  (They may have been preceded
    // by <w>x<h>@.)
    //
    case GET_Y:
      if (isdigit(c))
      {
        y = parse_tile_number(f, c);
        state = ADD_RECT2MASK;
      }
      else
      {
        ungetc(c, f);
        return (STAT_ERR);
      }
      break;

    //
    // We've just parsed a number and an x.
    //
    case GET_H:
      if (isdigit(c))
      {
        h = parse_tile_number(f, c);
        state = GET_AT;
      }
      else
      {
        ungetc(c, f);
        return (STAT_ERR);
      }
      break;

    //
    // We've just parsed a number, an x, and another number.
    //
    case GET_AT:
      if (c == '@')
        state = GET_X;
      else
      {
        ungetc(c, f);
        x = y = 0;
        state = ADD_RECT2MASK;
      }
      break;

    //
    // We've just parsed a number, an x, another number, and an @.
    //
    case GET_X:
      if (isdigit(c))
      {
        x = parse_tile_number(f, c);
        c = getc(f);
        if (c == ',')
          state = GET_Y;
        else
        {
          ungetc(c, f);
          return (STAT_ERR);
        }
      }
      else
      {
        ungetc(c, f);
        return (STAT_ERR);
      }
      break;

    //
    // We've parsed a complete specification and now want to add a rectangle
    // of tiles to the mask.
    //
    case ADD_RECT2MASK:
      ungetc(c, f);

      if (x + w > client_w || y + h > client_h)
        return (STAT_ERR);

      for (int nx = x; nx < x + w; nx++)
        for (int ny = y; ny < y + h; ny++)
          if (subtracting)
            del_tile_mask(mask, (pos_t) { .bits.x = nx + client_ulhc.bits.x,
                                          .bits.y = ny + client_ulhc.bits.y });
          else
            add_tile_mask(mask, (pos_t) { .bits.x = nx + client_ulhc.bits.x,
                                          .bits.y = ny + client_ulhc.bits.y });

      state = IDLE;
      break;

    //
    // We've parsed a complete specification and now want to add a range
    // of CPUs to the mask.
    //
    case ADD_CPUS2MASK:
      ungetc(c, f);

      if (first_cpu > last_cpu || last_cpu >= client_w * client_h)
        return (STAT_ERR);

      for (int cpu = first_cpu; cpu <= last_cpu; cpu++)
      {
        x = cpu % client_w;
        y = cpu / client_w;
        if (subtracting)
          del_tile_mask(mask, (pos_t) { .bits.x = x + client_ulhc.bits.x,
                                        .bits.y = y + client_ulhc.bits.y });
        else
          add_tile_mask(mask, (pos_t) { .bits.x = x + client_ulhc.bits.x,
                                        .bits.y = y + client_ulhc.bits.y });
      }

      state = IDLE;
      break;

    default:
      panic("bad state %d in parse_tilemask", state);
    }
  }
}


/** Handle a client or bme "memory" subcommand.
 * @param f File to read.
 * @param lineno_ptr Pointer to the line number.
 */
static void
handle_memory(FILE* f, int* lineno_ptr)
{
  //
  // Client memory subcommand.
  //
  char token[HV_PATH_MAX + 1];

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    int stat2 = parse_tok(f, token, sizeof (token), 0, lineno_ptr);
    if (stat2 & STAT_TOK)
    {
      int64_t val;
      if (!strcmp(token, "default"))
        val = CLIENT_MEM_DEFAULT;
       else
       {
        if ((stat2 & STAT_ERR) || str2number(token, &val, 1, 0))
        {
          tprintf("config: malformed memory size at line %d, "
                  "ignoring\n", *lineno_ptr);
          nextline(f);
          break;
        }
      }
      config.clients[config.nclients - 1].req_mem_len[i] = val;
    }
    else if (i == 0)
    {
      tprintf("config: missing memory sizes at line %d, ignoring\n",
              *lineno_ptr);
      nextline(f);
      break;
    }
  }

  if (nextline(f) & STAT_ERR)
    tprintf("config: trailing characters ignored on line %d\n", *lineno_ptr);
}


/** Handle a subcommand which saves its arguments in an hvfs_str.  We save
 *  in this form so that we don't have to allocate a large buffer for every
 *  possible string; when the client asks for the string we'll go back to
 *  the filesystem and read it at that time.
 * @param f File to read.
 * @param lineno_ptr Pointer to the line number.
 * @param str Structure to be filled in with information on the argument
 *        string.
 */
static void
handle_hvfs_str(FILE* f, int* lineno_ptr, struct hvfs_str* str)
{
  int c = getc(f);
  while (c != '\n' && isspace(c))
    c = getc(f);

  if (c == '\n' || c == EOF)
  {
    str->len = 0;
    return;
  }

  //
  // Find out where we are in the config file; then skip to the end
  // of the line/file and use that to figure out what the length of
  // the arguments is.
  //
  str->ino = fs_findfile(CONFIG_NAME);
  str->off = ftell(f) - 1;

  while (c != '\n' && c != '\r' && c != EOF)
    c = getc(f);

  ungetc(c, f);

  str->len = ftell(f) - str->off;
}



/** Handle a client or bme "hfh_tiles" subcommand.
 * @param f File to read.
 * @param lineno_ptr Pointer to the line number.
 */
static void
handle_hfh_tiles(FILE* f, int* lineno_ptr)
{
  //
  // We pass a temporary copy of the tile mask to the parsing routine,
  // so that if it fails we don't mess up the already-set default.
  //
  tile_mask tmp_mask =
    config.clients[config.nclients - 1].home_map_tiles;

  int stat =
    parse_tilemask(f, &tmp_mask,
                   config.clients[config.nclients - 1].ulhc,
                   config.clients[config.nclients - 1].lrhc);

  if (stat & STAT_ERR)
  {
    tprintf("config: syntax or tile range error at "
            "line %d, subcommand ignored\n", *lineno_ptr);
    nextline(f);
  }
  else
    config.clients[config.nclients - 1].home_map_tiles = tmp_mask;
}



//
// Codes for BME memory placement subcommands.
//
#define BME_CMD_TEXT        0  ///< "text".
#define BME_CMD_RODATA      1  ///< "rodata".
#define BME_CMD_RWDATA      2  ///< "rwdata".
#define BME_CMD_PERTILE     3  ///< "pertile".
#define BME_CMD_EXTRA       4  ///< "extra".

/** Handle one of the BME memory placement commands (text, rodata, rwdata,
 *  pertile).
 * @param f File to read.
 * @param lineno_ptr Pointer to the line number.
 * @param command Type of command (BME_CMD_xxx)
 * @param mpg Current memory placement group.
 * @param md Appropriate memory descriptor within the placement group (i.e.,
 *   the one corresponding to the command being parsed).
 */
static void
handle_bme_mpg_command(FILE* f, int* lineno_ptr, int command_type,
                       struct bme_mem_placement_group* mpg,
                       struct bme_mem_desc* md)
{
  char optname[128];
  char optval[128];
  char *stopchar;
  int64_t val;

  while (parse_option(f, optname, sizeof (optname), optval,
         sizeof (optval)) & STAT_TOK)
  {
    if (!strcmp(optname, "cache"))
    {
      pos_t cache_home;

      if (command_type == BME_CMD_EXTRA)
        tprintf("config: cache= not legal for extra files "
                "at line %d, ignoring\n", *lineno_ptr);
      else if (!strcmp(optval, "none"))
        md->cache_mode = BME_CACHE_MODE_NONE;
      else if (!strcmp(optval, "local"))
        md->cache_mode = BME_CACHE_MODE_LOCAL;
      else if (!strcmp(optval, "hash"))
        md->cache_mode = BME_CACHE_MODE_HASH;
      else if (!str2pos(optval, &cache_home))
      {
        md->cache_mode = BME_CACHE_MODE_COORDS;
        md->cache_coords = cache_home;
      }
      else
        tprintf("config: unrecognized cache option value %s on line %d, "
                "ignoring\n", optval, *lineno_ptr);
    }
    else if (!strcmp(optname, "ctl"))
    {
      if (!strcmp(optval, "nearest"))
        md->mem_ctl_num = BME_CTL_NUM_NEAREST;
      else if (str2number(optval, &val, 0, 0) || val >= MAX_MSHIMS ||
               !mshims[val])
        tprintf("config: ignoring illegal value %s for controller "
                "at line %d\n", optval, *lineno_ptr);
      else
        md->mem_ctl_num = val;
    }
    else if (!strcmp(optname, "dtlb"))
    {
      if (command_type != BME_CMD_TEXT)
        tprintf("config: dtlb= only legal for text mappings "
                "at line %d, ignoring\n", *lineno_ptr);
      else if (!strcmp(optval, "none"))
        mpg->text.mapped_in_dtlb = BME_DTLB_MAP_MODE_NONE;
      else if (!strcmp(optval, "read"))
        mpg->text.mapped_in_dtlb = BME_DTLB_MAP_MODE_READ;
      else if (!strcmp(optval, "write"))
        mpg->text.mapped_in_dtlb = BME_DTLB_MAP_MODE_RW;
      else
        tprintf("config: unrecognized dtlb option value %s on line %d, "
                "ignoring\n", optval, *lineno_ptr);
    }
    else if (!strcmp(optname, "heap"))
    {
      if (command_type != BME_CMD_PERTILE)
        tprintf("config: heap= only legal for per-tile mappings "
                "at line %d, ignoring\n", *lineno_ptr);
      else if (str2number(optval, &val, 0, 0) || val >= (1ULL << 32))
        tprintf("config: ignoring illegal value %s for heapsize "
                "at line %d\n", optval, *lineno_ptr);
      else
        mpg->pertile.heapsize = val;
    }
    else if (!strcmp(optname, "pa"))
    {
      if (!strcmp(optval, "bottom"))
        md->pa_mode = BME_CTL_PLACE_BOTTOM;
      else if (!strcmp(optval, "top"))
        md->pa_mode = BME_CTL_PLACE_TOP;
      else if (!strcmp(optval, "exe"))
      {
        if (command_type == BME_CMD_PERTILE)
          tprintf("config: pa=exe not legal for per-tile mappings "
                  "at line %d, ignoring\n", *lineno_ptr);
        else
          md->pa_mode = BME_CTL_PLACE_EXE;
      }
      else if (str2ll(optval, &stopchar, 0, &val) || *stopchar || val < 0 ||
               val >= (1ULL << MSH_MAX_SIZE_SHIFT))
        tprintf("config: ignoring illegal value %s for PA "
                "at line %d\n", optval, *lineno_ptr);
      else
      {
        md->ctl_pa = val;
        md->pa_mode = BME_CTL_PLACE_CONFIG;
      }
    }
    else if (!strcmp(optname, "sharemap"))
    {
      int sharemap = -1;

      if (!strcmp(optval, "none"))
        sharemap = 0;
      else if (!strcmp(optval, "rwdata"))
        sharemap = 1;
      else
        tprintf("config: unrecognized sharemap option value %s on line %d, "
                "ignoring\n", optval, *lineno_ptr);

      if (sharemap != -1)
      {
        if (command_type == BME_CMD_RODATA)
          mpg->rodata.sharemap = sharemap;
        else if (command_type == BME_CMD_PERTILE)
          mpg->pertile.sharemap = sharemap;
        else
          tprintf("config: sharemap= only legal for rodata or per-tile "
                  "mappings at line %d, ignoring\n", *lineno_ptr);
      }
    }
    else if (!strcmp(optname, "stack"))
    {
      if (command_type != BME_CMD_PERTILE)
        tprintf("config: stack= only legal for per-tile mappings "
                "at line %d, ignoring\n", *lineno_ptr);
      else if (str2number(optval, &val, 0, 0) || val >= (1ULL << 32))
        tprintf("config: ignoring illegal value %s for stacksize "
                "at line %d\n", optval, *lineno_ptr);
      else
        mpg->pertile.stacksize = val;
    }
    else if (!strcmp(optname, "va"))
    {
      if (command_type != BME_CMD_PERTILE)
        tprintf("config: va= only legal for per-tile mappings "
                "at line %d, ignoring\n", *lineno_ptr);
      else if (str2ll(optval, &stopchar, 0, &val) || *stopchar || val < 0 ||
               val >= (1ULL << 32))
        tprintf("config: ignoring out-of-range value %s for va "
                "at line %d\n", optval, *lineno_ptr);
      else
        mpg->pertile.va = val;
    }
    else
      tprintf("config: unrecognized option name %s on line %d, "
              "ignoring\n", optname, *lineno_ptr);
  }

  if (nextline(f) & STAT_ERR)
    tprintf("config: trailing characters ignored on line %d\n", *lineno_ptr);

  nextline(f);
}


/* Get a device name and validate that the device exists.
 * @param f File to read.
 * @param buf Buffer for the device name.
 * @param buflen Length of buffer in bytes.
 * @param optional If nonzero, and the device name is legal but was not
 *  probed on this particular chip, don't print an error message.
 * @param devpp Pointer to device pointer, set if we find the device.
 * @param lineno_ptr Pointer to the line number.
 * @return Zero on success; 1 if there was a parsing error or the device
 *  name is unknown; 2 if the device name is legal but the named device
 *  was not probed on this particular chip.
 */
static int
get_device_name(FILE* f, char* buf, int buflen, int optional,
                struct device** devpp, int* lineno_ptr)
{
  struct device* devp;

  //
  // Get and validate device name.  It has to be in the table, and
  // must have been found by the shim probe, unless it's a pseudo-device.
  //
  int stat2 = parse_tok(f, buf, buflen, 0, lineno_ptr);
  if (stat2 & STAT_ERR)
  {
    tprintf("config: malformed device name at line %d, ignoring\n",
            *lineno_ptr);
    return (1);
  }
  if ((stat2 & STAT_TOK) == 0)
  {
    tprintf("config: missing device name at line %d, ignoring\n", *lineno_ptr);
    return (1);
  }

  for (devp = devices; devp->name; devp++)
    if (!strcmp(devp->name, buf))
      break;

  if (!devp->name)
  {
    tprintf("config: device %s not found at line %d, ignoring\n",
            buf, *lineno_ptr);
    return (1);
  }

  if (!(devp->flags & DEV_FLG_PSEUDO) && !devp->probed)
  {
    if (!optional)
      tprintf("config: device %s not probed at line %d, ignoring\n",
              buf, *lineno_ptr);
    return (2);
  }

  *devpp = devp;
  return (0);
}


//
// What multi-line command (or "stanza") are we currently parsing?
//
#define PARSING_NONE        0  ///< We're not parsing a multi-line command.
#define PARSING_CLIENT      1  ///< We're parsing a client command.
#define PARSING_DEVICE      2  ///< We're parsing a device command.
#define PARSING_BME         3  ///< We're parsing a BME command.


/** Handle any cleanup necessary at the end of a BME stanza.
 * @param default_mpg_ptr Pointer to the default memory placement group.
 */
static void
bme_stanza_finish(struct bme_mem_placement_group* default_mpg_ptr)
{
  //
  // If the default group still has tiles in it, then make a copy of it and
  // link it on to the list of groups for the last BME.
  //
  if (!tile_mask_is_empty(&default_mpg_ptr->tiles))
  {
    struct bme_mem_placement_group* mpg = local_alloc(sizeof (*mpg), 0); 

    if (!mpg)
    {
      tprintf("config: can't allocate memory for default group for BME at "
              "line %d, ignored", config.clients[config.nclients - 1].lineno);
      return;
    }

    *mpg = *default_mpg_ptr;
    mpg->next = config.clients[config.nclients - 1].bme_groups;
    config.clients[config.nclients - 1].bme_groups = mpg;
  }
}

/** Fix up a BME memory descriptor so that it isn't caching on a nonexistent
 *  tile.
 * @param md Pointer to a memory descriptor.
 * @param tiles Mask of valid tiles for this BME.
 */
static void
bme_caching_fixup(struct bme_mem_desc* md, tile_mask* tiles)
{
  if (md->cache_mode != BME_CACHE_MODE_COORDS)
    return;

  //
  // If we picked a default coherent mapping on TILE64, we set the cache
  // coordinates to zero.  That value makes in_tile_mask crash, so just skip
  // that test and fall through to picking a real tile to cache on.
  //
  if (md->cache_coords.word != 0 && in_tile_mask(tiles, md->cache_coords))
    return;

  //
  // This may fail, but if it does the BME has no tiles, so we don't care.
  //
  ffs_tile_mask(tiles, &md->cache_coords);
}


void
parse_config()
{
  char cfg_file_buf[1024];            // Buffer for config file
  char keyword[32];                   // Current keyword we're handling
  char token[HV_PATH_MAX + 1];        // Make sure this can hold a file name
  int lineno = 1;                     // Line in config file, for error messages
  int parsing = PARSING_NONE;         // What multiline command are we parsing?
  struct device* devp;                // Valid iff parsing is PARSING_DEVICE
  tile_mask ded_tile_mask = {{ 0 }};  // Dedicated tiles
  pos_t default_shared =              // Default shared tile
    chip_logical_ulhc;
  FILE cf;                            // Config file
  struct bme_mem_placement_group      // Default BME group attributes
    default_mpg = {{{ 0 }}};
  struct bme_mem_placement_group*     // BME group currently being worked on
    current_mpg = &default_mpg;
  int skip_subcommands = 0;           // Should we skip following subcommands?

  //
  // If there's no config file, we just set our input file to /dev/null,
  // since we want to actually run all of the normal cleanup code below
  // the command parsing loop.
  //
  if (fs_open(CONFIG_NAME, &cf, cfg_file_buf, sizeof (cfg_file_buf)) < 0)
  {
    tprintf("config: warning: hypervisor configuration file \"" CONFIG_NAME
            "\" not found");
    cf = null_in;
  }

  //
  // Initialize the masks of dedicated and shared tiles; some tiles may
  // already be spoken for if they're used by system devices, which have
  // already been initialized by this point.
  //
  clear_tile_mask(&ded_tile_mask);
  clear_tile_mask(&config.shr_tile_mask);

  //
  // Initialize any nonzero defaults.
  //
  config.halt_full_chip_requested = 1;
  config.dfs_core = 1;

  for (devp = devices; devp->name; devp++)
  {
    if (devp->drv)
    {
      for (int i = 0; i < devp->info.num_stiles; i++)
          add_tile_mask(&config.shr_tile_mask, devp->info.stiles[i]);

      for (int i = 0; i < devp->info.num_dtiles; i++)
          add_tile_mask(&ded_tile_mask, devp->info.dtiles[i]);
    }
  }

  //
  // Parse the configuration commands.
  //
  while (1)
  {
    int stat = parse_tok(&cf, keyword, sizeof (keyword), 1, &lineno);
    if (stat & STAT_EOF)
      break;
    if (stat & STAT_ERR)
    {
      tprintf("config: malformed command at line %d, ignoring\n", lineno);
      nextline(&cf);
      continue;
    }

    if ((stat & STAT_INDENT) == 0)
    {
      //
      // If the line isn't indented, this must be a command.  First do any
      // cleanup that might be required for a previous BME command.
      //
      if (parsing == PARSING_BME)
        bme_stanza_finish(&default_mpg);

      //
      // Now that we've seen a new command, we want to pay attention to any
      // following subcommands, too.
      //
      skip_subcommands = 0;

      parsing = PARSING_NONE;
      if (!strcmp(keyword, "debug"))
      {
        //
        // debug command.
        //
        int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if ((stat2 & STAT_TOK) == 0)
        {
          tprintf("config: no argument to debug command on line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }

        int64_t val;
        if (str2number(token, &val, 0, 0))
          tprintf("config: ignoring out of range value %s for debug flags\n",
                  token);
        else
          config.debug = (uint32_t) val;

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "console"))
      {
        //
        // console command.
        //
        int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if ((stat2 & STAT_TOK) == 0)
        {
          tprintf("config: no argument to console command on line %d, "
                  "ignoring\n", lineno);
          nextline(&cf);
          continue;
        }

        // FIXME: need to look up console in device table and mark it there

        tprintf("config: client console subcommand not yet supported on line "
                "%d\n", lineno);

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "options"))
      {
        //
        // options command.
        //
        char optname[128];
        char optval[128];

        while (parse_option(&cf, optname, sizeof (optname), optval,
               sizeof (optval)) & STAT_TOK)
        {
          //
          // CPU speed option.
          //
          if (!strcmp(optname, "cpu_speed"))
          {
            int64_t speed;
            if (str2number(optval, &speed, 0, 6) || speed <= 0)
              tprintf("config: malformed cpu_speed value %s on line %d, "
                      "ignoring\n", optval, lineno);
            else if (config.cpu_speed != 0)
              tprintf("config: repeated cpu_speed option on line %d, "
                      "ignoring\n", lineno);
            else
              config.cpu_speed = speed;
          }
          else if (!strcmp(optname, "default_shared"))
          {
            pos_t tile;
            if (str2pos(optval, &tile))
              tprintf("config: malformed default_shared value %s on line %d, "
                      "ignoring\n", optval, lineno);
            else if (!within(chip_logical_ulhc, chip_logical_lrhc, tile, tile))
              tprintf("config: default shared value %s inconsistent with chip "
                      "at line %d, ignoring\n", optval, lineno);
            else
              default_shared = tile;
          }
          else if (!strcmp(optname, "console_debug"))
          {
            init_uart_debug_string(optval);
          }
          else if (!strcmp(optname, "post"))
          {
            //
            // On Gx, this value has already been processed by the booter,
            // so we ignore it.
            //
          }
          else if (!strcmp(optname, "panic"))
          {
            if (!strcmp(optval, "reboot"))
              config.reboot_on_panic_requested = 1;
            else if (!strcmp(optval, "halt"))
              config.reboot_on_panic_requested = 0;
            else
              tprintf("config: unsupported value %s for panic option "
                      "on line %d, ignoring\n", optval, lineno);
          }
          else if (!strcmp(optname, "halt"))
          {
            if (!strcmp(optval, "tile"))
              config.halt_full_chip_requested = 0;
            else if (!strcmp(optval, "chip"))
              config.halt_full_chip_requested = 1;
            else
              tprintf("config: unsupported value %s for halt option "
                      "on line %d, ignoring\n", optval, lineno);
          }
          else if (!strcmp(optname, "dvfs"))
          {
            if (!strcmp(optval, "off"))
            {
              config.dfs_core = 0;
              config.dvs = 0;
            }
            else if (!strcmp(optval, "core"))
            {
              config.dfs_core = 1;
              config.dvs = 0;
            }
            else if (!strcmp(optval, "core_volt"))
            {
              config.dfs_core = 1;
              config.dvs = 1;
            }
            else
              tprintf("config: unsupported mode %s for dvfs option "
                      "on line %d, ignoring\n", optval, lineno);
          }
          else if (!strcmp(optname, "mem_error"))
          {
            if (!strcmp(optval, "silent"))
            {
              config.mem_error_silent = 1;
              config.mem_error_panic = 0;
            }
            else if (!strcmp(optval, "warn"))
            {
              config.mem_error_silent = 0;
              config.mem_error_panic = 0;
            }
            else if (!strcmp(optval, "panic"))
            {
              config.mem_error_silent = 0;
              config.mem_error_panic = 1;
            }
            else
              tprintf("config: unsupported mode %s for mem_error option "
                      "on line %d, ignoring\n", optval, lineno);
          }
          else if (!strcmp(optname, "stripe_memory"))
          {
            if (optval[0])
            {
              if (!strcmp(optval, "silent") || !strcmp(optval, "never"))
              {
                // The only thing we use striping_requested for is to
                // decide if we should complain if we weren't striped.
                // Since "silent" just means "don't complain", we don't
                // set striping_requested, even though we might be
                // striping.
              }
              else if (!strcmp(optval, "always") || !strcmp(optval, "default"))
              {
                // Note that the "stripe no matter what" aspect of the
                // "always" mode is handled by the booter, so we don't
                // need to do anything for it other than note that striping
                // was explicitly asked for.
                config.striping_requested = 1;
              }
              else
                tprintf("config: unrecognized mode %s for stripe_memory "
                        "option on line %d, ignoring option\n", optval,
                        lineno);
            }
            else
              config.striping_requested = 1;
          }
          else if (!strcmp(optname, "contig_pa"))
          {
            if (optval[0])
              tprintf("config: contig_pa option on line %d takes no "
                      "value, ignoring option\n", lineno);
            else
              config.contig_pa = 1;
          }
          else if (!strcmp(optname, "stats"))
          {
            if (optval[0])
              tprintf("config: stats option on line %d takes no "
                      "value, ignoring option\n", lineno);
            else
              config.stats = 1;
          }
          else if (!strcmp(optname, "mem_speed"))
          {
            //
            // This value has already been processed by the booter, so
            // we ignore it.
            //
          }
          else
            tprintf("config: unrecognized option name %s on line %d, "
                    "ignoring\n", optname, lineno);
        }

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "client"))
      {
        //
        // client command.
        //
        config.clients[config.nclients].lineno = lineno;

        if (config.nregclients >= MAX_CLIENTS)
        {
          tprintf("config: too many client definitions at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }

        //
        // Get and validate executable name.
        //
        int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if (stat2 & STAT_ERR)
        {
          tprintf("config: malformed client binary name at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }
        if ((stat2 & STAT_TOK) == 0)
        {
          tprintf("config: missing client binary name at line %d, ignoring\n",
                  lineno);
          continue;
        }

        int ino = fs_findfile(token);
        if (ino < 0)
        {
          tprintf("config: client binary %s not found at line %d, ignoring\n",
                  token, lineno);
          nextline(&cf);
          continue;
        }

        config.clients[config.nclients].bin_ino = ino;

        //
        // Get and validate optional geometry/position arguments.
        //
        pos_t geom;
        pos_t ulhc = chip_logical_ulhc;
        pos_t lrhc = chip_logical_lrhc;

        stat2 = parse_pos(&cf, &geom, 0);
        if (stat2 & STAT_ERR)
        {
          tprintf("config: malformed client geometry at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }

        if (stat2 & STAT_TOK)
        {
          stat2 = parse_pos(&cf, &ulhc, 1);
          if (stat2 & STAT_ERR)
          {
            tprintf("config: malformed client origin at line %d, ignoring\n",
                    lineno);
            nextline(&cf);
            continue;
          }

          //
          // Set the bottom corner based on the geometry and the top corner.
          //
          lrhc.bits.x = ulhc.bits.x + geom.bits.x - 1;
          lrhc.bits.y = ulhc.bits.y + geom.bits.y - 1;
        }

        //
        // Make sure this client fits on the chip.
        //
        if (!within(chip_logical_ulhc, chip_logical_lrhc, ulhc, lrhc))
        {
          tprintf("config: client origin/geometry inconsistent with chip at "
                  "line %d, ignoring\n", lineno);
          nextline(&cf);
          continue;
        }

        //
        // Make sure this client doesn't overlap with any previously defined
        // clients.
        //
        int found_overlap = 0;
        for (int i = 0; i < config.nclients; i++)
          if (overlap(ulhc, lrhc, config.clients[i].ulhc,
              config.clients[i].lrhc))
          {
            tprintf("config: client origin/geometry at line %d overlaps with "
                    "previous client, ignoring\n", lineno);
            found_overlap = 1;
            break;
          }

        if (found_overlap)
        {
          nextline(&cf);
          continue;
        }

        config.clients[config.nclients].ulhc = ulhc;
        config.clients[config.nclients].lrhc = lrhc;
        //
        // We'll modify the tile mask below to remove dedicated tiles.
        //
        init_tile_mask(&config.clients[config.nclients].tiles, ulhc, lrhc);
        init_tile_mask(&config.clients[config.nclients].home_map_tiles,
                       ulhc, lrhc);

        config.clients[config.nclients].arg.len = 0;
        for (int i = 0; i < MAX_MSHIMS; i++)
          config.clients[config.nclients].req_mem_len[i] = CLIENT_MEM_DEFAULT;

        config.nclients++;
        config.nregclients++;
        parsing = PARSING_CLIENT;

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "bme"))
      {
        config.clients[config.nclients].lineno = lineno;
        int flags = CLIENT_BME;

        //
        // BME command.
        //
        if (config.nbmeclients >= MAX_BME)
        {
          tprintf("config: too many BME definitions at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }

        //
        // Get and validate executable name.
        //
        int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if (stat2 & STAT_ERR)
        {
          tprintf("config: malformed BME binary name at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }
        if ((stat2 & STAT_TOK) == 0)
        {
          tprintf("config: missing BME binary name at line %d, ignoring\n",
                  lineno);
          continue;
        }

        int ino = fs_findfile(token);
        if (ino < 0)
        {
          tprintf("config: BME binary %s not found at line %d, ignoring\n",
                  token, lineno);
          nextline(&cf);
          continue;
        }
        config.clients[config.nclients].bin_ino = ino;

        //
        // Get and validate sharing keyword.
        //
        stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if (stat2 & STAT_ERR)
        {
          tprintf("config: malformed BME binary name at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }
        if ((stat2 & STAT_TOK) == 0)
        {
          tprintf("config: missing BME sharing keyword at line %d, ignoring\n",
                  lineno);
          continue;
        }
        if (!strcmp(token, "private"))
          flags |= CLIENT_BME_PRIVATE;
        else if (strcmp(token, "shared"))
        {
          tprintf("config: unrecognized BME sharing keyword at line %d, "
                  "ignoring\n", lineno);
          nextline(&cf);
          continue;
        }

        //
        // Compute set of tiles to be used.
        //
        tile_mask tiles;
        init_tile_mask(&tiles, chip_ulhc, chip_lrhc);

        int stat =
          parse_tilemask(&cf, &tiles, chip_ulhc, chip_lrhc);

        if (stat & STAT_ERR)
        {
          tprintf("config: syntax or tile range error at "
                  "line %d, BME command ignored\n", lineno);
          nextline(&cf);
          continue;
        }
        else
          config.clients[config.nclients].tiles = tiles;

        config.clients[config.nclients].home_map_tiles =
          config.clients[config.nclients].tiles;

        //
        // Make sure this BME doesn't overlap with any previously defined
        // BMEs.
        //
        int found_overlap = 0;
        for (int i = 0; i < config.nclients; i++)
        {
          if ((config.clients[i].flags & CLIENT_BME) == 0)
            continue;

          if (tile_mask_overlap(&config.clients[i].tiles,
                                &config.clients[config.nclients].tiles))
          {
            tprintf("config: BME at line %d overlaps with "
                    "previous BME, ignoring\n", lineno);
            found_overlap = 1;
            break;
          }
        }

        if (found_overlap)
        {
          nextline(&cf);
          continue;
        }

        //
        // Initialize the default group attributes.  The lifecycle of
        // default_mpg is as follows:
        //
        // 1. It's initially filled in with the full BME set of tiles and the
        //    default values for all of the placement attributes.  We then
        //    point current_mpg at it.
        //
        // 2. If commands modifying the placement attributes are encountered
        //    before any groups are defined, the changes made by those
        //    commands are made to default_mpg (via current_mpg).
        //
        // 3. When a group is created, its attributes are initialized from the
        //    contents of default_mpg, thus propagating the defaults or the
        //    results of top-level attribute modifications to the group, and
        //    the tiles in the group are removed from the tile set in
        //    default_mpg.  In this way, default_mpg holds the tiles that
        //    aren't yet assigned to a group.
        //
        // 4. When the BME command is complete, if there are any tiles left in
        //    default_mpg, it is converted into a new group and added to the
        //    BME.
        //
        default_mpg.tiles = tiles;

        default_mpg.text.mem_desc.mem_ctl_num = BME_CTL_NUM_NEAREST;
        default_mpg.text.mem_desc.pa_mode = BME_CTL_PLACE_BOTTOM;
        default_mpg.text.mem_desc.cache_mode = BME_CACHE_MODE_LOCAL;
        default_mpg.text.mapped_in_dtlb = BME_DTLB_MAP_MODE_NONE;

        default_mpg.rodata.mem_desc.mem_ctl_num = BME_CTL_NUM_NEAREST;
        default_mpg.rodata.mem_desc.pa_mode = BME_CTL_PLACE_BOTTOM;
        default_mpg.rodata.mem_desc.cache_mode = BME_CACHE_MODE_LOCAL;
        default_mpg.rodata.sharemap = 0;

        default_mpg.rwdata.mem_desc.mem_ctl_num = BME_CTL_NUM_NEAREST;
        default_mpg.rwdata.mem_desc.pa_mode = BME_CTL_PLACE_BOTTOM;
        if (flags & CLIENT_BME_PRIVATE)
          default_mpg.rwdata.mem_desc.cache_mode = BME_CACHE_MODE_LOCAL;
        else
        {
          default_mpg.rwdata.mem_desc.cache_mode = BME_CACHE_MODE_HASH;
        }

        default_mpg.pertile.mem_desc.mem_ctl_num = BME_CTL_NUM_NEAREST;
        default_mpg.pertile.mem_desc.pa_mode = BME_CTL_PLACE_BOTTOM;
        if (flags & CLIENT_BME_PRIVATE)
          default_mpg.pertile.mem_desc.cache_mode = BME_CACHE_MODE_LOCAL;
        else
        {
          default_mpg.pertile.mem_desc.cache_mode = BME_CACHE_MODE_HASH;
        }
        default_mpg.pertile.stacksize = 64 * 1024;
        default_mpg.pertile.heapsize = 64 * 1024;
        // We'll compute the actual stack VA later.
        default_mpg.pertile.va = -1;
        default_mpg.pertile.sharemap = (flags & CLIENT_BME_PRIVATE) != 0;

        default_mpg.extra = NULL;
        default_mpg.nearest_ctl = BME_CTL_NUM_NEAREST;

        current_mpg = &default_mpg;

        //
        // These have to be set to _something_; right now it's not critical
        // that this be the smallest rectangle that covers the BME.
        //
        config.clients[config.nclients].ulhc = chip_logical_ulhc;
        config.clients[config.nclients].lrhc = chip_logical_lrhc;

        config.clients[config.nclients].arg.len = 0;
        for (int i = 0; i < MAX_MSHIMS; i++)
          config.clients[config.nclients].req_mem_len[i] = CLIENT_MEM_DEFAULT;

        config.clients[config.nclients].flags = flags;
        config.clients[config.nclients].bme_groups = NULL;
        config.nclients++;
        config.nbmeclients++;
        parsing = PARSING_BME;

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "device") || !strcmp(keyword, "device?"))
      {
        //
        // device command.
        //
        int is_opt = !strcmp(keyword, "device?");

        int dev_err = get_device_name(&cf, token, sizeof (token), is_opt,
                                      &devp, &lineno);
        if (dev_err)
        {
          nextline(&cf);
          if (dev_err == 2 && is_opt)
          {
            //
            // The device was not found, but we don't want to complain
            // about it.  We need to skip any following subcommands, too.
            // (You could just as well do that on any error here, but this
            // keeps us compatible with earlier MDE versions.)
            //
            skip_subcommands = 1;
          }
          continue;
        }

        if (devp->flags & DEV_FLG_SYSDEV)
        {
          //
          // For now, we don't let the user configure a system device; we
          // might eventually want to allow this so arguments can be given
          // to it.
          //
          tprintf("config: device %s at line %d is a system device, ignoring\n",
                  token, lineno);
          nextline(&cf);
          continue;
        }

        //
        // Get driver name.
        //
        int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
        if (stat2 & STAT_ERR)
        {
          tprintf("config: malformed driver name at line %d, ignoring\n",
                  lineno);
          nextline(&cf);
          continue;
        }

        //
        // Choose a driver.
        //
        driver_t* drvp;

        if ((stat2 & STAT_TOK) == 0)
        {
          //
          // The user didn't give us a driver name, so we just use the first
          // appropriate driver in the list.  If we don't have one, we ignore
          // this device.
          //
          for (drvp = driver_table_start; drvp != driver_table_end; drvp++)
            if (drvp->shim_type == devp->shim_type)
              break;

          if (drvp == driver_table_end)
          {
            tprintf("config: can't find driver for device %s at line %d, "
                    "ignoring\n", devp->name, lineno);
            nextline(&cf);
            continue;
          }
        }
        else
        {
          //
          // Find the driver the user asked for and make sure it works with
          // this device.
          //
          for (drvp = driver_table_start; drvp != driver_table_end; drvp++)
            if (!strcmp(token, drvp->name))
              break;

          if (drvp == driver_table_end)
          {
            tprintf("config: can't find driver %s at line %d, ignoring\n",
                    token, lineno);
            nextline(&cf);
            continue;
          }

          if (drvp->shim_type != devp->shim_type)
          {
            tprintf("config: driver %s can't drive device %s at line %d, "
                    "ignoring\n", token, devp->name, lineno);
            nextline(&cf);
            continue;
          }
        }

        //
        // We have a valid device and driver; link them together.
        //
        devp->drv = drvp;
        parsing = PARSING_DEVICE;

        if (nextline(&cf) & STAT_ERR)
          tprintf("config: trailing characters ignored on line %d\n", lineno);
      }
      else if (!strcmp(keyword, "print"))
      {
        //
        // print command.
        //

        char line[128];
        char* lineptr = line;
        int linelen = readline(&cf, line, sizeof (line));
        while (linelen && isspace(*lineptr))
        {
          lineptr++;
          linelen--;
        }
        tprintf("%.*s\n", linelen, lineptr);

        nextline(&cf);
      }
      else if (!strcmp(keyword, "config_version"))
      {
        //
        // config_version command.
        //
        handle_hvfs_str(&cf, &lineno, &config.config_ver);
      }
      else
      {
        tprintf("config: unrecognized command \"%s\" on line %d\n", keyword,
                lineno);
        nextline(&cf);
      }
    }
    else
    {
      //
      // The line was indented, so this is a subcommand.
      //

      if (skip_subcommands)
      {
        //
        // We're still skipping subcommands from a previous ignored
        // command.
        //
        nextline(&cf);
        continue;
      }

      if (parsing == PARSING_CLIENT)
      {
        //
        // Client subcommands.
        //
        if (!strcmp(keyword, "memory"))
          handle_memory(&cf, &lineno);
        else if (!strcmp(keyword, "args"))
          handle_hvfs_str(&cf, &lineno,
                          &config.clients[config.nclients - 1].arg);
        else if (!strcmp(keyword, "hfh_tiles"))
          handle_hfh_tiles(&cf, &lineno);
        else if (!strcmp(keyword, "device"))
        {
          if (get_device_name(&cf, token, sizeof (token), 0, &devp, &lineno))
          {
            nextline(&cf);
            continue;
          }

          //
          // Set the device's owning client, if this is the first one.
          // Otherwise note that there is no one owning client.
          //
          if (tile_mask_is_empty(&devp->tiles))
          {
            devp->client_owner = config.nclients - 1;
          }
          else
          {
            if (!(devp->flags & DEV_FLG_SHAREABLE))
            {
              tprintf("config: unshareable device %s named in multiple "
                      "device subcommands on line %d\n", token, lineno);
              nextline(&cf);
              continue;
            }
            else
            {
              devp->client_owner = -1;
            }
          }

          bis_tile_mask(&devp->tiles,
                        &config.clients[config.nclients - 1].tiles);
        }
        else
        {
          tprintf("config: unrecognized client subcommand \"%s\" on line %d\n",
                  keyword, lineno);
          nextline(&cf);
        }
      }
      else if (parsing == PARSING_BME)
      {
        //
        // BME subcommands.
        //
        if (!strcmp(keyword, "memory"))
          handle_memory(&cf, &lineno);
        else if (!strcmp(keyword, "args"))
          handle_hvfs_str(&cf, &lineno,
                          &config.clients[config.nclients - 1].arg);
        else if (!strcmp(keyword, "hfh_tiles"))
          handle_hfh_tiles(&cf, &lineno);
        else if (!strcmp(keyword, "group"))
        {
          //
          // Compute set of tiles to be used.  We start with the remaining
          // tiles in the default MPG so "^tile" does something semi-useful.
          //
          tile_mask tiles = default_mpg.tiles;

          int stat =
            parse_tilemask(&cf, &tiles,
                           config.clients[config.nclients - 1].ulhc,
                           config.clients[config.nclients - 1].lrhc);

          if (stat & STAT_ERR)
          {
            tprintf("config: syntax or tile range error at "
                    "line %d, group command ignored\n", lineno);
            nextline(&cf);
            continue;
          }

          //
          // Make sure there are some tiles in the group, that they're all
          // in the BME, and that none are in previous groups.
          //
          if (tile_mask_is_empty(&tiles))
          {
            tprintf("config: group at line %d contains no tiles, ignored\n",
                    lineno);
            nextline(&cf);
            continue;
          }

          tile_mask tmp_tilemask = tiles;
          bic_tile_mask(&tmp_tilemask,
                        &config.clients[config.nclients - 1].tiles);

          if (!tile_mask_is_empty(&tmp_tilemask))
          {
            tprintf("config: group at line %d contains tiles not in this "
                    "BME client, ignored\n", lineno);
            nextline(&cf);
            continue;
          }

          tmp_tilemask = tiles;
          bic_tile_mask(&tmp_tilemask, &default_mpg.tiles);

          if (!tile_mask_is_empty(&tmp_tilemask))
          {
            tprintf("config: group at line %d contains tiles used in "
                    "previous groups in this BME client, ignored\n", lineno);
            nextline(&cf);
            continue;
          }

          //
          // Create a new group structure, initialize it with the defaults,
          // link it onto this BME, point the "thing we're currently
          // modifying" group pointer at it, and then take the tiles from this
          // group out of the set of remaining tiles in the default structure.
          //
          struct bme_mem_placement_group* new_mpg =
            local_alloc(sizeof (*new_mpg), 0); 
          memset(new_mpg, 0, sizeof (*new_mpg));

          if (!new_mpg)
          {
            tprintf("config: can't allocate memory for group at line %d, "
                    "ignored", lineno);
            nextline(&cf);
            continue;
          }

          *new_mpg = default_mpg;
          current_mpg = new_mpg;
          current_mpg->tiles = tiles;
          current_mpg->next = config.clients[config.nclients - 1].bme_groups;
          config.clients[config.nclients - 1].bme_groups = current_mpg;
          bic_tile_mask(&default_mpg.tiles, &current_mpg->tiles);
        }
        else if (!strcmp(keyword, "text"))
          handle_bme_mpg_command(&cf, &lineno, BME_CMD_TEXT, current_mpg,
                                 &current_mpg->text.mem_desc);
        else if (!strcmp(keyword, "rodata"))
          handle_bme_mpg_command(&cf, &lineno, BME_CMD_RODATA, current_mpg,
                                 &current_mpg->rodata.mem_desc);
        else if (!strcmp(keyword, "rwdata"))
          handle_bme_mpg_command(&cf, &lineno, BME_CMD_RWDATA, current_mpg,
                                 &current_mpg->rwdata.mem_desc);
        else if (!strcmp(keyword, "pertile"))
          handle_bme_mpg_command(&cf, &lineno, BME_CMD_PERTILE, current_mpg,
                                 &current_mpg->pertile.mem_desc);
        else if (!strcmp(keyword, "extra"))
        {
          int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
          if ((stat2 & STAT_TOK) == 0)
          {
            tprintf("config: no argument to extra command on line %d, "
                    "ignoring\n", lineno);
            nextline(&cf);
            continue;
          }
          //
          // Allocate a new struct and put it at the end of the list.
          //
          struct bme_extrafile_desc* extra = local_alloc(sizeof(*extra), 0);
          if (!extra)
          {
            tprintf("config: can't allocate memory for extra file description "
                    "line %d, ignored",
                    config.clients[config.nclients - 1].lineno);
            nextline(&cf);
            continue;
          }
          if (current_mpg->extra == NULL)
            current_mpg->extra = extra;
          else
          {
            struct bme_extrafile_desc* lp, *prev;
            for (lp = current_mpg->extra, prev = current_mpg->extra;
                 lp; lp = lp->next)
            {
              prev = lp;
            }
            prev->next = extra;
          }
          extra->next = NULL;

          //
          // Fill in file information.
          //
          extra->bin_ino = fs_findfile(token);
          strncpy(extra->name, token, BME_MAX_NAME_SIZE);
          extra->name[BME_MAX_NAME_SIZE - 1] = '\0';

          //
          // Set some defaults, these may be changed later.
          //
          extra->mem_desc.pa_mode = BME_CTL_PLACE_BOTTOM;
          extra->mem_desc.cache_mode = BME_CACHE_MODE_LOCAL;

          //
          // Set up the memory desc based on the config.
          //
          handle_bme_mpg_command(&cf, &lineno, BME_CMD_EXTRA, current_mpg,
                                 &extra->mem_desc);
        }
        else if (!strcmp(keyword, "nearest"))
        {
          int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
          if ((stat2 & STAT_TOK) == 0)
          {
            tprintf("config: no argument to nearest command on line %d, "
                    "ignoring\n", lineno);
            nextline(&cf);
            continue;
          }

          int64_t val;
          if (str2number(token, &val, 0, 0) || val >= MAX_MSHIMS ||
              !mshims[val])
            tprintf("config: ignoring out of range value %s for nearest "
                    "controller at line %d\n", token, lineno);
          else
            current_mpg->nearest_ctl = val;

          if (nextline(&cf) & STAT_ERR)
            tprintf("config: trailing characters ignored on line %d\n", lineno);
        }
        else
        {
          tprintf("config: unrecognized BME subcommand \"%s\" on line %d\n",
                  keyword, lineno);
          nextline(&cf);
        }
      }
      else if (parsing == PARSING_DEVICE)
      {
        //
        // Device subcommands.
        //
        if (!strcmp(keyword, "dedicated"))
        {
          //
          // Dedicated tiles subcommand.
          //
          for (int i = 0; i < devp->drv->dtilereq; i++)
          {
            pos_t tile;

            int stat2 = parse_pos(&cf, &tile, 1);
            if (stat2 & STAT_ERR)
            {
              tprintf("config: malformed tile coordinate at line %d, "
                      "ignoring\n", lineno);
              nextline(&cf);
              continue;
            }

            if (!within(chip_logical_ulhc, chip_logical_lrhc, tile, tile))
            {
              tprintf("config: tile coordinate (%d,%d) inconsistent with chip "
                      "at line %d, ignoring\n", UXY(tile), lineno);
              nextline(&cf);
              continue;
            }

            if (stat2 & STAT_TOK)
            {
              if (in_tile_mask(&ded_tile_mask, tile) ||
                  in_tile_mask(&config.shr_tile_mask, tile))
              {
                tprintf("config: tile (%d,%d) already used at line %d, "
                        "ignoring\n", UXY(tile), lineno);
                nextline(&cf);
                break;
              }

              add_tile_mask(&ded_tile_mask, tile);
              devp->info.dtiles[i] = tile;
              devp->info.num_dtiles++;
            }
            else
            {
              tprintf("config: missing tiles at line %d, ignoring\n", lineno);
              nextline(&cf);
              break;
            }
          }

          if (nextline(&cf) & STAT_ERR)
            tprintf("config: trailing characters ignored on line %d\n", lineno);
        }
        else if (!strcmp(keyword, "shared"))
        {
          //
          // Shared tiles subcommand.
          //
          for (int i = 0; i < devp->drv->stilereq; i++)
          {
            pos_t tile;

            int stat2 = parse_pos(&cf, &tile, 1);
            if (stat2 & STAT_ERR)
            {
              tprintf("config: malformed tile coordinate at line %d, "
                      "ignoring\n", lineno);
              nextline(&cf);
              continue;
            }

            if (!within(chip_logical_ulhc, chip_logical_lrhc, tile, tile))
            {
              tprintf("config: tile coordinate (%d,%d) inconsistent with chip "
                      "at line %d, ignoring\n", UXY(tile), lineno);
              nextline(&cf);
              continue;
            }

            if (stat2 & STAT_TOK)
            {
              if (in_tile_mask(&ded_tile_mask, tile))
              {
                tprintf("config: tile (%d,%d) already dedicated at line %d, "
                        "ignoring\n", UXY(tile), lineno);
                nextline(&cf);
                break;
              }

              add_tile_mask(&config.shr_tile_mask, tile);
              devp->info.stiles[i] = tile;
              devp->info.num_stiles++;
            }
            else
            {
              tprintf("config: missing tiles at line %d, ignoring\n", lineno);
              nextline(&cf);
              break;
            }
          }

          if (nextline(&cf) & STAT_ERR)
            tprintf("config: trailing characters ignored on line %d\n", lineno);
        }
        else if (!strcmp(keyword, "args"))
        {
          //
          // Process the args string.  We have to pass a string to the device
          // init routine, so we have to put an upper bound on the argument
          // length.  (In the client case the supervisor provides the buffer
          // so we don't have to have a length limit.)
          //
          handle_hvfs_str(&cf, &lineno, &devp->arg);
          if (devp->arg.len > DEV_MAX_ARGLEN)
          {
            tprintf("config: device argument line at line %d too long, "
                    "ignoring\n", lineno);
            devp->arg.len = 0;
          }
        }
#if MAX_DEVICE_CLOCKS > 0
        else if (!strcmp(keyword, "speed"))
        {
          //
          // Device speed subcommand.
          //
          char token[HV_PATH_MAX + 1];

          for (int i = 0; i < devp->n_clocks; i++)
          {
            int stat2 = parse_tok(&cf, token, sizeof (token), 0, &lineno);
            if (stat2 & STAT_TOK)
            {
              int64_t val;

              if ((stat2 & STAT_ERR) || str2number(token, &val, 0, 6) ||
                  val < 0)
              {
                tprintf("config: malformed device speed at line %d, "
                        "ignoring\n", lineno);
                nextline(&cf);
                break;
              }
              devp->info.speeds[i] = val;
            }
            else if (i == 0)
            {
              tprintf("config: missing device speeds at line %d, ignoring\n",
                      lineno);
              nextline(&cf);
              break;
            }
          }

          if (nextline(&cf) & STAT_ERR)
            tprintf("config: trailing characters ignored on line %d\n",
                    lineno);
        }
#endif // MAX_DEVICE_CLOCKS > 0
        else
        {
          tprintf("config: unrecognized device subcommand \"%s\" on line %d\n",
                  keyword, lineno);
          nextline(&cf);
        }
      }
      else
      {
        tprintf("config: subcommand \"%s\" with no previous command on line "
                "%d, ignored\n", keyword, lineno);
        nextline(&cf);
      }
    }
  }

  //
  // Do any cleanup required for the last BME command.
  //
  if (parsing == PARSING_BME)
    bme_stanza_finish(&default_mpg);

  //
  // Check to make sure all devices have the right number of tiles assigned.
  // We do this here to catch the case where we got a device command with no
  // tile subcommand; it's easier than trying to check when we think we're
  // done with each device command/subcommand group.
  //
  // Note that when we disable devices, we aren't taking any tiles which were
  // associated with those devices out of the dedicated/shared tiles bitmaps.
  // The theory is that this is an error which the user will fix, so we don't
  // have to provide optimal performance in this case.
  //
  for (devp = devices; devp->name; devp++)
  {
    if (devp->drv)
    {
      //
      // First check the dedicated tiles.
      //
      int needs = devp->drv->dtilereq;
      int has = devp->info.num_dtiles;

      if (needs != has)
      {
        tprintf("config: device %s has wrong number of dedicated tiles "
                "assigned (needs %d, has %d); disabling\n",
                devp->name, needs, has);

        devp->drv = NULL;
        continue;
      }

      //
      // Make sure they aren't trying to use the default shared tile as a
      // dedicated tile.  We already know that they aren't using any tile
      // which is being used as a shared tile by another driver, but there
      // might not be any drivers using the default, and we have to make sure
      // it isn't dedicated since we're going to run the console there.
      //
      for (int i = 0; i < has; i++)
      {
        if (devp->info.dtiles[i].word == default_shared.word)
        {
          tprintf("config: device %s uses default shared tile (%d,%d) as a "
                  "dedicated tile; disabling\n",
                  devp->name, UXY(default_shared));
          devp->drv = NULL;
        }
      }
      if (!devp->drv)
        continue;

      //
      // Now the shared tiles.
      //
      needs = devp->drv->stilereq;
      has = devp->info.num_stiles;

      if (needs == 1 && has == 0)
      {
        //
        // If they just need one shared tile, use the default.
        //
        devp->info.num_stiles = 1;
        devp->info.stiles[0] = default_shared;
        add_tile_mask(&config.shr_tile_mask, default_shared);
      }
      else if (needs != has)
      {
        tprintf("config: device %s has wrong number of shared tiles assigned "
                "(needs %d, has %d); disabling\n", devp->name, needs, has);

        devp->drv = NULL;
      }
    }
  }

  //
  // Check for devices that have an "automatic driver".  The automatic driver
  // is a specially flagged driver that should be run whenever the hvconfig
  // didn't specify something.  Automatic drivers must use a single, shared
  // tile, which will be placed on the default shared tile.
  //
  for (devp = devices; devp->name; devp++)
  {
    if ((devp->probed || (devp->flags & DEV_FLG_PSEUDO)) &&
        devp->drv == NULL)
    {
      driver_t* drvp;
      for (drvp = driver_table_start; drvp != driver_table_end; drvp++)
      {
        if (drvp->shim_type == devp->shim_type &&
            (drvp->flags & DRV_FLG_AUTOMATIC) &&
            drvp->stilereq <= 1 && drvp->dtilereq == 0)
        {
          devp->drv = drvp;
          devp->info.num_stiles = 1;
          devp->info.stiles[0] = default_shared;
          add_tile_mask(&config.shr_tile_mask, default_shared);
          //
          // Note that we could be re-initializing a device which was
          // named in the hvconfig but was then disabled, say due to one of
          // its dedicated tiles overlapping with the default shared tile.
          // Thus, we need to make sure it isn't given any dedicated tiles. As
          // noted above, when we disable a device in this manner we don't try
          // to reclaim the dedicated tiles.
          //
          devp->info.num_dtiles = 0;
          break;
        }
      }
    }
  }

  //
  // If no clients were defined, create a fake one which will run the null
  // client.  This at least will allow us to come up enough to run the PCIe
  // driver so we can reset the card.
  //
  if (config.nclients <= 0)
  {
    tprintf("config: warning: no clients defined, assuming null client\n");
    config.clients[0].bin_ino = -1;
    config.clients[0].ulhc = chip_logical_ulhc;
    config.clients[0].lrhc = chip_logical_lrhc;
    init_tile_mask(&config.clients[0].tiles, chip_logical_ulhc,
                   chip_logical_lrhc);
    config.clients[0].home_map_tiles = config.clients[0].tiles;
    for (int i = 0; i < MAX_MSHIMS; i++)
      config.clients[0].req_mem_len[i] = CLIENT_MEM_DEFAULT;
    config.clients[0].arg.len = 0;
    config.clients[0].flags = 0;
    config.nclients = 1;
  }
  
  //
  // Pick the primary client.
  //
  int primary_client = 0;
  for (int cidx = 0; cidx < config.nclients; cidx++)
    if (!(config.clients[cidx].flags & CLIENT_BME))
    {
      config.clients[cidx].flags |= CLIENT_PRIMARY;
      primary_client = cidx;
      break;
    }

  //
  // Assign a client tile mask to any device that doesn't have one.  Also,
  // if we're a dedicated tile for a particular device, remember which one;
  // we use this later when figuring out which client we're in.
  //
  struct device* ded_devp = NULL;
  for (devp = devices; devp->name; devp++)
    if (devp->drv)
    {
      if (tile_mask_is_empty(&devp->tiles))
      {
        if (devp->flags & DEV_FLG_SHAREABLE)
        {
          for (int cidx = 0; cidx < config.nclients; cidx++)
            if (!(config.clients[cidx].flags & CLIENT_BME))
              bis_tile_mask(&devp->tiles, &config.clients[cidx].tiles);
          devp->client_owner = -1;
        }
        else
        {
          bis_tile_mask(&devp->tiles, &config.clients[primary_client].tiles);
          devp->client_owner = primary_client;
        }
      }

      for (int i = 0; i < devp->info.num_dtiles; i++)
        if (devp->info.dtiles[i].word == my_pos.word)
          ded_devp = devp;
    }

  //
  // Make a tile mask which includes all tiles not available to regular
  // clients (i.e., dedicated driver and BME tiles).
  //
  tile_mask unavail_tiles = ded_tile_mask;

  for (int cidx = 0; cidx < config.nclients; cidx++)
    if (config.clients[cidx].flags & CLIENT_BME)
      bis_tile_mask(&unavail_tiles, &config.clients[cidx].tiles);

  //
  // Set the remote console tile, on the master.  (On the slave, it's set
  // before we enter this routine.)
  //
  if (is_master)
  {
      chip_console = default_shared;
  }

  // 
  // Remove any unavailable tiles from the tile masks for each client, then
  // make sure each client still has some tiles.
  //
  for (int cidx = 0; cidx < config.nclients; cidx++)
  {
    struct client_config* ccp = &config.clients[cidx];

    if (ccp->flags & CLIENT_BME)
    {
      tile_mask bme_unavail_tiles = ded_tile_mask;

      if (in_tile_mask(&ccp->tiles, chip_console))
      {
        tprintf("config: chip console (default_shared) tile removed from BME "
                "at line %d\n", ccp->lineno);
        add_tile_mask(&bme_unavail_tiles, chip_console);
      }

      bic_tile_mask(&ccp->tiles, &bme_unavail_tiles);
      for (struct bme_mem_placement_group* mpg = ccp->bme_groups; mpg;
           mpg = mpg->next)
      {
        //
        // In addition to removing unavailable tiles from the group, we fix up
        // any cases where we're trying to cache on a now-unavailable tile.
        //
        bic_tile_mask(&mpg->tiles, &bme_unavail_tiles);

        bme_caching_fixup(&mpg->text.mem_desc, &ccp->tiles);
        bme_caching_fixup(&mpg->rodata.mem_desc, &ccp->tiles);
        bme_caching_fixup(&mpg->rwdata.mem_desc, &ccp->tiles);
        bme_caching_fixup(&mpg->pertile.mem_desc, &ccp->tiles);
      }
      bic_tile_mask(&ccp->home_map_tiles, &bme_unavail_tiles);
    }
    else
    {
      bic_tile_mask(&ccp->tiles, &unavail_tiles);
      bic_tile_mask(&ccp->home_map_tiles, &unavail_tiles);
    }

    //
    // Warn if the set of tiles is empty.
    //
    if (tile_mask_is_empty(&ccp->tiles))
      tprintf("config: tile set for client/BME at line %d is empty, "
              "will not be instantiated\n", ccp->lineno);
    //
    // Make sure the set of home map tiles isn't empty.
    //
    if (tile_mask_is_empty(&ccp->home_map_tiles))
    {
      tprintf("config: hfh_tiles set for client/BME at line %d is empty, "
              " using all client tiles\n", ccp->lineno);
      ccp->home_map_tiles = ccp->tiles;
    }
    //
    // We just use the first tile in the tile mask as the start tile.
    //
    ffs_tile_mask(&ccp->tiles, &ccp->start_tile);
  }

  //
  // Assign interrupt channel numbers.  Also, fix up the names for
  // pseudo-devices; these get set at probe time for real devices.
  //
  int intchan = DRV_CHAN_MAX + 1;

  for (devp = devices; devp->name; devp++)
    if (devp->drv)
    {
      if (devp->flags & DEV_FLG_PSEUDO)
        devp->info.name = devp->name;

      if (devp->drv->intchanreq > 0)
      {
        devp->info.intchan = intchan;
        devp->info.num_intchan = devp->drv->intchanreq;
        intchan += devp->drv->intchanreq;
      }
      else
        devp->info.intchan = -1000000;
    }

  assert(intchan <= DRV_CHAN_RMASK + 1);
  config.first_dyn_intchan = intchan;

  //
  // Figure out which client we are.
  //
  my_client = 0;

  if (ded_devp && ded_devp->client_owner >= 0)
  {
    //
    // If we're a dedicated driver tile, and our device is owned by a
    // specific client, pick that as our client.  This does the right
    // thing for non-shareable devices; when we have any shareable devices
    // this might need some tweaking.
    //
    my_client = ded_devp->client_owner;
  }
  else
  {
    //
    // If we're not a dedicated tile, we just pick this client if we're
    // actually a part of it.
    //
    for (int cidx = 0; cidx < config.nclients; cidx++)
    {
      struct client_config* ccp = &config.clients[cidx];
  
      //
      // By default we're in the last non-BME client; this only happens on
      // dedicated driver tiles whose devices aren't owned by a specific
      // client, or tiles we aren't using.
      //
      if (!(ccp->flags & CLIENT_BME))
        my_client = cidx;
  
      if (in_tile_mask(&ccp->tiles, my_pos))
      {
        my_client = cidx;
        break;
      }
    }
  }
}


static void
dump_bme_md(struct bme_mem_desc* md)
{
  if (md->mem_ctl_num == BME_CTL_NUM_NEAREST)
    printf("ctl=nearest ");
  else
    printf("ctl=%d ", md->mem_ctl_num);

  static const char* const pa_modes[] = { "bottom", "top", "exe" };

  if (md->pa_mode == BME_CTL_PLACE_CONFIG)
    printf("pa=%#llX ", md->ctl_pa);
  else
    printf("pa=%s ", pa_modes[md->pa_mode]);

  static const char* const cache_modes[] = { "local", "hash", "none" };

  if (md->cache_mode == BME_CACHE_MODE_COORDS)
    printf("cache=%d,%d ", UXY(md->cache_coords));
  else
    printf("cache=%s ", cache_modes[md->cache_mode]);
}


static void
dump_bme_mpg(struct bme_mem_placement_group* mpg)
{
  tprintf("    BME group:\n");
  tprintf("      Tiles:\n");
  dump_tile_mask(&mpg->tiles);

  static const char* const dtlb_modes[] = { "none", "read", "write" };

  tprintf("      Text:     ");
  dump_bme_md(&mpg->text.mem_desc);
  printf("dtlb=%s\n", dtlb_modes[mpg->text.mapped_in_dtlb]);

  tprintf("      RO data:  ");
  dump_bme_md(&mpg->rodata.mem_desc);
  printf("sharemap=%s\n", mpg->rodata.sharemap ? "rwdata" : "none");

  tprintf("      RW data:  ");
  dump_bme_md(&mpg->rwdata.mem_desc);
  printf("\n");

  tprintf("      Per-tile: ");
  dump_bme_md(&mpg->pertile.mem_desc);
  printf("\n                      stack=%d heap=%d va=%#lX sharemap=%s\n",
         mpg->pertile.stacksize, mpg->pertile.heapsize, mpg->pertile.va,
         mpg->pertile.sharemap ? "rwdata" : "none");

  tprintf("      Nearest controller: %d\n", mpg->nearest_ctl);
}


void
dump_client_config()
{
  tprintf("Configuration dump:\n");
  tprintf("  Debug flags: %#x\n", config.debug);
  tprintf("  CPU speed: %d\n", config.cpu_speed);
  tprintf("  Clients: %d regular, %d BME, %d total, my_client is %d\n",
          config.nregclients, config.nbmeclients, config.nclients, my_client);
  for (int i = 0; i < config.nclients; i++)
  {
    tprintf("  Client %d:\n", i);
    tprintf("    Flags:");
    if (config.clients[i].flags & CLIENT_BME)
      printf(" BME");
    if (config.clients[i].flags & CLIENT_BME_PRIVATE)
      printf(" BME_PRIVATE");
    printf("\n");
    tprintf("    Inode of client binary: %d\n", config.clients[i].bin_ino);
    tprintf("    ULHC: %d,%d  LRHC: %d,%d\n", UXY(config.clients[i].ulhc),
            UXY(config.clients[i].lrhc));
    tprintf("    Valid tiles:\n");
    dump_tile_mask(&config.clients[i].tiles);
    tprintf("    Home map tiles:\n");
    dump_tile_mask(&config.clients[i].home_map_tiles);
    tprintf("    Client memory request:\n");
    for (int j = 0; j < MAX_MSHIMS; j++)
      if (config.clients[i].req_mem_len[j] == CLIENT_MEM_DEFAULT)
        tprintf("      shim %d: default\n", j);
      else
        tprintf("      shim %d: %llx\n", j, config.clients[i].req_mem_len[j]);
    tprintf("    Arguments: inode %d offset %d length %d\n",
            config.clients[i].arg.ino, config.clients[i].arg.off,
            config.clients[i].arg.len);

    if (config.clients[i].flags & CLIENT_BME)
      for (struct bme_mem_placement_group* mpg = config.clients[i].bme_groups;
           mpg; mpg = mpg->next)
        dump_bme_mpg(mpg);
  }
}
