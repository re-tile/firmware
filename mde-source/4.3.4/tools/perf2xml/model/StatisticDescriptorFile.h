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

// ==========================================================================
// StatisticDescriptorFile.h -- Statistic metadata file
// ==========================================================================

// multiple-inclusion guard
#ifndef STATISTIC_DESCRIPTOR_FILE_H
#define STATISTIC_DESCRIPTOR_FILE_H

// custom includes
#include "io_utils.h"      // IO streams
#include "string_utils.h"  // C/C++ string utils
#include "Pathname.h"      // UNIX/Linux pathnames
#include "xml.h"           // XML support

#include "StatisticDescriptorIndex.h"


// --------------------------------------------------------------------------
// StatisticDescriptorFile
// --------------------------------------------------------------------------

/** StatisticDescriptorFile header file */
class StatisticDescriptorFile
{
  // --- members ---
protected:
  /** Pathname of statistic metadata file */
  Pathname m_pathname;

  /** XML data content */
  XMLDocument *m_document;


  // --- constructors ---
public:
  /** Constructor */
  StatisticDescriptorFile(const Pathname& pathname, std::string& errors) :
    m_pathname(pathname)
  {
    // load XML document
    m_document = XMLReader::read(pathname, errors);
    if (m_document == NULL)
    {
      errors = "Could not read statistic metadata from '" + pathname + "'.\n"
               + errors;
    }
  }

  /** Destructor */
  ~StatisticDescriptorFile()
  {
    if (m_document != NULL)
    {
      delete m_document;
    }
  }


  // --- to_string methods ---
public:
  /** Returns human-readable string representation of object */
  const std::string&
  to_string() const;

  /** Returns string representation as C string */
  const char*
  c_str() const;


  // --- accessors ---
public:
  /** Returns pathname of this file */
  const Pathname&
  pathname()
  {
    return m_pathname;
  }


  // --- methods ---
public:

  /** Gets statistic metadata index from XML data. */
  StatisticDescriptorIndex*
  get_statistic_index();

};


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR(out, StatisticDescriptorFile, x)
{
  out << x.pathname().to_string();
  return out;
}

// multiple-inclusion guard
#endif
