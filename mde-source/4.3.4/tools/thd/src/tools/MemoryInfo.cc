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

// =============================================================================
// MemoryInfo.c -- Memory region info object
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "MemoryInfo.h"

// C/C++ includes
#include <stdio.h> // fprintf


// -----------------------------------------------------------------------------
// MemoryInfo
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
MemoryInfo::MemoryInfo(long long address,
                       long long size,
                       const std::string& access,
                       const Pathname& pathname,
                       long long offset) :
  m_address(address),
  m_size(size),
  m_access(access),
  m_pathname(pathname),
  m_offset(offset)
{
}

/** Destructor */
MemoryInfo::~MemoryInfo()
{
}


// --- accessors ---

/** Gets start address. */
long long
MemoryInfo::get_address() const
{
  return m_address;
}

/** Gets size in bytes. */
long long
MemoryInfo::get_size() const
{
  return m_size;
}

/** Gets access permissions. */
const std::string&
MemoryInfo::get_access() const
{
  return m_access;
}

/** Gets pathname of binary file, if any. */
const Pathname&
MemoryInfo::get_pathname() const
{
  return m_pathname;
}

/** Gets offset into binary file, if any. */
long long
MemoryInfo::get_offset() const
{
  return m_offset;
}


// --- methods ---

/** Prints memory info object to stream */
void
MemoryInfo::print(FILE* stream)
{
  fprintf(stream, "%llx-%llx %s",
	  m_address,
          m_address + m_size - 1,
          (m_access.empty()) ? "----" : m_access.c_str());
  if (m_pathname.empty())
    fprintf(stream, " (unknown)");
  else
    fprintf(stream, " %s %llx", m_pathname.c_str(), m_offset);
}
