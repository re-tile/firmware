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
// io_utils.h -- C/C++ IO Utilities
// ============================================================================

// inclusion guard
#ifndef IO_H
#define IO_H

// C++ IO includes
#include <iostream>
using std::istream;
using std::ostream;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;

#include <fstream>
using std::ifstream;
using std::ofstream;

#include <dirent.h>  // DIR, dirent
// Work around change in signature for scandir() function.
// Ref: http://stackoverflow.com/questions/146291/manpage-scandir-prototype-weirdness
#define USE_SCANDIR_VOIDPTR
#if defined( __GLIBC_PREREQ )
#if __GLIBC_PREREQ(2,10)
#undef USE_SCANDIR_VOIDPTR
#endif
#endif

// C IO includes
#include <stdio.h>   // FILE, printf, etc.

// custom includes


// ----------------------------------------------------------------------------
// output stream operator boilerplate macros
// ----------------------------------------------------------------------------

/** Output stream operator boilerplate for a type. */
#define OUTPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_ostream<charT, traits>& \
      operator<<(std::basic_ostream<charT, traits>& STREAMNAME, \
        TYPENAME VARNAME)

/** Input stream operator boilerplate for a type. */
#define INPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_istream<charT, traits>& \
      operator>>(std::basic_istream<charT, traits>& STREAMNAME, \
        TYPENAME VARNAME)


// -----------------------------------------------------------------------------
// fopen(int fd, char* mode)
// -----------------------------------------------------------------------------

/** Overload of fopen() that creates a character stream associated with the
    specified file descriptor.
    Creates an internal dup of the file descriptor, so the stream can be
    fclosed as usual without thereby closing the original file descriptor.
 */
FILE*
fopen(const int fd, const char* mode);


// -----------------------------------------------------------------------------
// fopen(const std::string& name, char* mode)
// -----------------------------------------------------------------------------

/** Overload of fopen() that takes an std::string. */
FILE*
fopen(const std::string& name, const char* mode);


// -----------------------------------------------------------------------------
// readline()
// -----------------------------------------------------------------------------

/** Reads from stream until until {\r}\n or EOF is seen,
    and appends character(s) read to specified string argument.
    Does not add the newline character(s), if any, to the string.
    Returns the number of characters appended to the string (can be zero)
    or -1 if EOF is encountered before any characters were read from stream. */
int
readline(FILE *stream, std::string& buffer);


// inclusion guard
#endif
