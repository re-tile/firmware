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
// StatisticDescriptorFile.cc -- StatisticDescriptorFile class
// ==========================================================================

// C/C++ includes

// custom includes
#include "string_utils.h"    // C/C++ strings
#include "collections.h"     // FOR_EACH macro
#include "StatisticDescriptorFile.h" // header file


// --------------------------------------------------------------------------
// StatisticDescriptorFile
// --------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const std::string&
StatisticDescriptorFile::to_string() const
{
  return m_pathname.to_string();
}

/** Returns string representation as C string */
const char*
StatisticDescriptorFile::c_str() const
{
  return to_string().c_str();
}


// --- methods ---

/** Gets statistic metadata index from XML data. */
StatisticDescriptorIndex*
StatisticDescriptorFile::get_statistic_index()
{
  StatisticDescriptorIndex* result = new StatisticDescriptorIndex();
  if (m_document != NULL)
  {
    std::string errors;
    result->load_statistics(m_document, errors);
    if (! errors.empty())
    {
      printf("Errors when loading statistic index from file %s\n%s",
             m_pathname.c_str(), errors.c_str());
    }
  }
  return result;
}

