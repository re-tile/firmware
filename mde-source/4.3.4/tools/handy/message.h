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

#ifndef TOOLS_HANDY_MESSAGE_H
#define TOOLS_HANDY_MESSAGE_H

#include "common.h"

BEGIN_EXTERN_C


//! Determines whether a given call to @ref spew actually does anything.
extern int message_verbosity;

//! A static prefix used by @ref message (and various related macros).
extern const char* message_prefix;

//! An optional hook which should place a dynamic prefix into "prefix", of
//! maximum size "size" (1024), to be used instead of @ref message_prefix.
extern void (*message_prefix_hook)(char* prefix, size_t size);

//! An optional hook which should emit the given string, in some way,
//! followed by a newline, to be used instead of "fprintf(stderr, ...)".
extern void (*message_output_hook)(char* str);


//! List of uppercase hex characters to make hex output easier.
extern const char hex_chars_upper[16];

//! List of lowercase hex characters to make hex output easier.
extern const char hex_chars_lower[16];

//! Convert a hex digit to a number, or -1 if unknown.
extern int
hex_char_to_int(char ch);


//! Emit a message, with a prefix of "prefix", and a suffix of "suffix".
//! See also @ref message_prefix and @ref message_prefix_hook.
//! By default, the result is emitted to stderr, plus a final newline,
//! and then stderr is flushed, but see @message_output_hook.
extern void
vmessage_aux(const char* prefix, const char* suffix,
             const char* format, va_list args);


//! Call @ref vmessage_aux, with an empty "prefix" and "suffix".
extern void
vmessage(const char* format, va_list args);


//! Call @ref vmessage_aux, with an empty "prefix" and "suffix".
extern void
message(const char* format, ...)
  __attribute__((format(printf, 1, 2)));


//! Call @ref message, with a prefix of "ERROR: ", and then die.
extern void
punt(const char* format, ...)
  __attribute__((format(printf, 1, 2), noreturn));

//! Call @ref message, with a prefix of "ERROR: ", and a suffix of
//! ": (<ERRNO>) <ERRSTR>", and then die.
extern void
punt_with_errno(const char* format, ...)
  __attribute__((format(printf, 1, 2), noreturn));


//! Call @ref message, with a prefix of "WARNING: ".
extern void
warn(const char* format, ...)
  __attribute__((format(printf, 1, 2)));

//! Call @ref message, with a prefix of "WARNING: ", and a suffix of
//! ": (<ERRNO>) <ERRSTR>".
extern void
warn_with_errno(const char* format, ...)
  __attribute__((format(printf, 1, 2)));


//! Basically, call "note(format, ...)", but appending the first "size"
//! bytes of data, escaping certain bytes with backslash.
extern void
note_bytes(const void* data, size_t size, const char* format, ...)
  __attribute__((format(printf, 3, 4)));


//! NOTE: Deprecated, just use @ref message.
#define note(...) \
  message(__VA_ARGS__)


//! Emit a message (using @ref message).
//! Do nothing if @ref message_verbosity < @param V.
#define spew(V, ...) \
  do { \
    if (message_verbosity >= (V)) \
      message(__VA_ARGS__); \
  } while (0)


//! Call "note_bytes(D, S, ...)", if @ref message_verbosity >= "V".
#define spew_bytes(V, D, S, ...) \
  do { \
    if (message_verbosity >= (V)) \
      note_bytes(D, S, __VA_ARGS__); \
  } while (0)


END_EXTERN_C

#endif /* !TOOLS_HANDY_MESSAGE_H */
