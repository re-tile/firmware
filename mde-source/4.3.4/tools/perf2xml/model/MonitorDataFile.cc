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
// MonitorDataFile.cc -- MonitorDataFile class
// ==========================================================================

// C/C++ includes

// custom includes
#include "string_utils.h"    // C/C++ strings
#include "collections.h"     // FOR_EACH macro
#include "MonitorDataFile.h" // header file


// --------------------------------------------------------------------------
// MonitorDataFile
// --------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const std::string&
MonitorDataFile::to_string() const
{
  return m_pathname.to_string();
}

/** Returns string representation as C string */
const char*
MonitorDataFile::c_str() const
{
  return to_string().c_str();
}


// --- methods ---

/** Gets target spec (<chip>) subnode of XML metadata */
XMLElement*
MonitorDataFile::get_target_spec()
{
  XMLElement* result = NULL;
  if (m_document != NULL)
  {
    XMLElement* chip_element =
      m_document->get_element_by_path("target", "chip");
    if (chip_element != NULL)
    {
      result = new XMLElement(chip_element);
    }
  }
  return result;
}

/** Gets binary filename mapping from XML metadata */
bool
MonitorDataFile::get_binary_path_map(PathnameMap& map)
{
  bool result = false;
  if (m_document != NULL)
  {
    XMLElement* binariesElement =
      m_document->get_element_by_path("target", "binaries");
    if (binariesElement != NULL)
    {
      XMLElementArray binaryElements;
      binariesElement->get_children_named("binary", binaryElements);
      FOR_EACH(const_iterator, i, XMLElementArray, binaryElements)
      {
        XMLElement* binary = (*i);
        std::string local  = binary->get_attribute("local");
        std::string remote = binary->get_attribute("remote");
        map.add(Pathname(remote), Pathname(local));
      }
      result = true;
    }
  }
  return result;
}
