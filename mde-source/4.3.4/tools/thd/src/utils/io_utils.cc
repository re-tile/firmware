// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

// ============================================================================
// io_utils.cc -- C/C++ IO Utilities
// ============================================================================

#include "io_utils.h"

// C/C++ includes


// ----------------------------------------------------------------------------
// fopen(int fd, char* mode)
// ----------------------------------------------------------------------------

/** Overload of fopen() that creates a character stream associated with the
    specified file descriptor.
    Creates an internal dup of the file descriptor, so the stream can be
    fclosed as usual without thereby closing the original file descriptor.
 */
FILE*
fopen(const int fd, const char* mode)
{
  return fdopen(dup(fd), mode);
}

// ----------------------------------------------------------------------------
// fopen(const std::string& name, char* mode)
// ----------------------------------------------------------------------------

/** Overload of fopen() that takes an std::string. */
FILE*
fopen(const std::string& name, const char* mode)
{
  return fopen(name.c_str(), mode);
}

// ----------------------------------------------------------------------------
// readline()
// ----------------------------------------------------------------------------

/** Reads from stream until until {\r}\n or EOF is seen,
    and stores character(s) read to specified string argument.
    Does not add the newline character(s), if any, to the string.
    Returns the number of characters appended to the string (can be zero)
    or -1 if EOF is encountered before any characters were read from stream. */
int
readline(FILE *stream, std::string& buffer)
{
  int i=0, c=0;
  buffer.clear();
  while ((c = getc(stream)) != EOF)
  {
    if (c == '\n') break;
    if (c != '\r')
    {
      buffer += c;
      ++i;
    }
  }
  if (i == 0 && c == EOF) i=-1;
  return i;
}

