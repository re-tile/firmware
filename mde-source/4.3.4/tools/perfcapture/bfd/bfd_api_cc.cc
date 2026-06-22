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
// bfd_api_cc.cc -- BFD C/C++ Bridge API
// ============================================================================

#include "bfd_api.h" // C++ API declarations

// custom includes


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// C++ API
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// ----------------------------------------------------------------------------
// BFDFile
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
BFDFile::BFDFile(const Pathname& pathname) :
  m_pathname(pathname),
  m_bfd_file(NULL)
{
  m_bfd_file = bfd_open_object_file(pathname.c_str());
}


/** Copy constructor */
BFDFile::BFDFile(const BFDFile& obj)
{
}

/** Assignment operator */
BFDFile&
BFDFile::operator=(const BFDFile& obj)
{
  if (this != &obj) // handle self-assignment
  {
  }
  return *this;
}

/** Destructor. */
BFDFile::~BFDFile()
{
  if (m_bfd_file != NULL)
  {
    bfd_close_object_file(m_bfd_file);
    m_bfd_file = NULL;
  }
}


// --- methods ---

/** Returns whether the file is opened and ready for access. */
bool
BFDFile::is_ready()
{
  return m_bfd_file != NULL;
}


/** Gets source file / line number for given address. */
bool
BFDFile::addr2line(const uint64_t address,
                   std::string& function_name,
                   Pathname& source_file, 
                   unsigned int& source_line,
                   bool demangle)
{
  bool result = false;
  if (! is_ready()) return result;

  char c_function_name[BFD_NAME_MAX+1] = {0};
  char c_source_file  [BFD_FILE_MAX+1] = {0};
  unsigned int c_source_line = 0;

  int status = bfd_addr2line(m_bfd_file,
                             (void*) address,
                             (demangle) ? 1 : 0,
                             c_function_name,
                             c_source_file,
                             &c_source_line);

  if (status == 0)
  {
    function_name = c_function_name;
    source_file   = c_source_file;
    source_line   = c_source_line;
    result = true;
  }
  else
  {
    function_name = "";
    source_file = "";
    source_line = 0;
  }

  return result;
}


// ----------------------------------------------------------------------------
// bfd_test()
// ----------------------------------------------------------------------------

/** Test of BFD C/C++ API. */
int
bfd_test()
{
  int result = -1;

  Pathname pathname = "/u/bswanson/dev/profile/perf_events/tests/fork";
  BFDFile* bfdfile = new BFDFile(pathname);

  if (! bfdfile->is_ready())
  {
    printf("Could not open object file: %s\n", pathname.c_str());
  }
  else
  {
    uint64_t address = 0x10e50;
    std::string  function_name;
    Pathname     source_file;
    unsigned int source_line;

    if (! bfdfile->addr2line(address, function_name, source_file, source_line))
    {
      printf("Could not get source line info for address: %p\n",
             (void*) address);
    }
    else
    {
      printf("%p = %s:%i\n",
             (void*) address,
             source_file.c_str(),
             source_line);
      result = 0;
    }
  }

  delete bfdfile;

  return result;
}

