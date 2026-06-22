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
// bfd_api.h -- BFD C/C++ Bridge API
// ============================================================================

// multiple-inclusion guard
#ifndef BFD_API_H
#define BFD_API_H

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// CREDIT: These BFD tools, in particular bfd_addr2line, are derived from
// the addr2line.c example found here:
//   http://linuxgazette.net/151/misc/melinte/addr2line.c

// No specific licensing terms were given with the example.
// In accordance with the Linux Gazette's overall "Open Publication License":
// (1) this code is derived from the addr2line.c example
// (2) it was created by William R. Swanson on 10/21/2011
// (3) the example article's author is Aurelian Melinte
//     article:      http://linuxgazette.net/151/melinte.html
//     contact info: http://linuxgazette.net/authors/melinte.html)
// (4) the original document location is the URL given above
// (5) no endorsement by the original author is asserted or implied.

// The C api code is defined in: bfd_api_c.c

#ifdef __cplusplus
extern "C"
{
#endif

#include <bfd.h>

#ifndef DMGL_PARAMS
#define DMGL_PARAMS      (1 << 0)       /* Include function args */
#define DMGL_ANSI        (1 << 1)       /* Include const, volatile, etc */
#endif

// ----------------------------------------------------------------------------
// assorted bfd-related defs
// ----------------------------------------------------------------------------

#define BFD_FILE_MAX FILENAME_MAX // defined in stdio.h
#define BFD_NAME_MAX FILENAME_MAX // for want of a better limit constant


// ----------------------------------------------------------------------------
// typdef struct bfd_file
// ----------------------------------------------------------------------------

typedef struct _bfd_file
{
  char*        pathname;
  char*        target;
  asymbol**    symbol_table;
  long         symbol_count;
  unsigned int symbol_size;
  int          symbols_dynamic;
  bfd*         abfd;
} bfd_file;


// ----------------------------------------------------------------------------
// typdef struct symbol_data
// ----------------------------------------------------------------------------

typedef struct _symbol_data
{
  asymbol**    symbol_table;
  bfd_vma      vma;
  bfd_vma      offset;
  bfd_vma      size;
  const char*  functionname;
  const char*  filename;
  unsigned int line;
  bfd_boolean  found;
} symbol_data;


// ----------------------------------------------------------------------------
// typdef symbol_function
// ----------------------------------------------------------------------------

typedef int (*symbol_function)(symbol_data* symbol);


// ----------------------------------------------------------------------------
// bfd_open_object_file
// ----------------------------------------------------------------------------

/** Opens object file, allocates and returns bfd_file object. */
bfd_file*
bfd_open_object_file(const char* pathname);


// ----------------------------------------------------------------------------
// bfd_close_object_file
// ----------------------------------------------------------------------------

/** Closes object file and deallocates bfd_file object. */
void
bfd_close_object_file(bfd_file* objfile);


// ----------------------------------------------------------------------------
// bfd_addr2line
// ----------------------------------------------------------------------------

/** Returns function, source file, and source line information
    for specified address. */
int
bfd_addr2line(bfd_file*     objfile,
              void*         address,
              int           demangle,
              char*         function_name,
              char*         source_file,
              unsigned int* source_line);


#ifdef __cplusplus
}
#endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// End of C API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// The C++ api code is defined in: bfd_api_cc.cc

#ifdef __cplusplus

// C/C++ includes
#include <stdint.h>   // uint64_t

// custom includes
#include "Pathname.h"


// ----------------------------------------------------------------------------
// BFDFile
// ----------------------------------------------------------------------------

/** BFD file instance, used to look up source files and line numbers. */
class BFDFile
{
  // --- members ---
protected:  
  /** Pathname of binary file. */
  Pathname m_pathname;

  /** BFD instance for file. */
  bfd_file* m_bfd_file;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  BFDFile(const Pathname& pathname);

protected:
  /** Copy constructor */
  BFDFile(const BFDFile& obj);

  /** Assignment operator */
  BFDFile&
  operator=(const BFDFile& obj);

public:
  /** Destructor. */
  ~BFDFile();


  // --- methods ---

  /** Returns whether the file is opened and ready for access. */
  bool
  is_ready();

  /** Gets source file / line number for given address. */
  bool
  addr2line(uint64_t address,
            std::string& function_name,
            Pathname& source_file,
            unsigned int& source_line,
            bool demangle = true);

};


// ----------------------------------------------------------------------------
// bfd_test()
// ----------------------------------------------------------------------------

/** Test of BFD C/C++ API. */
int
bfd_test();



// ifdef __cplusplus
#endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// End of C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// multiple-inclusion guard
#endif
