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
// XMLElement.cc -- XMLElement class
// ==========================================================================

// custom includes
#include "XMLElement.h"    // header file

// --------------------------------------------------------------------------
// XMLElement
// --------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const string XMLElement::to_string() const
{
  std::ostringstream result;
  result << "<";
  result << m_name;
  FOR_EACH(const_iterator, pair, XMLAttributeMap, m_attributes) {
    result << " " << pair->first << "=\"" << pair->second << "\"" << endl;
  }
  result << ">";
  return std::string(result.str());
}

/** Returns string representation as C string */
const char* XMLElement::c_str() const
{
  return to_string().c_str();
}


// --- child lookup methods ---

/** Returns child, if any, with specified name. */
XMLElement* XMLElement::child_named(const char* name) const
{
  return child_named(::to_string(name));
}

/** Returns child, if any, with specified name. */
XMLElement* XMLElement::child_named(const string& name) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementVector, m_children) {
    XMLElement* child = *i;
    string child_name = child->name();
    if (name == child_name) {
      result = child;
      break;
    }
  }
  return result;
}

/** Returns children, if any, with specified name. */
void XMLElement::children_named(const char* name,
                                XMLElementVector& result) const
{
  children_named(::to_string(name), result);
}
/** Returns children, if any, with specified name. */
void XMLElement::children_named(const string& name,
                                XMLElementVector& result) const
{
  FOR_EACH(const_iterator, i, XMLElementVector, m_children) {
    XMLElement* child = *i;
    string child_name = child->name();
    if (name == child_name) {
      result.add(child);
    }
  }
}

/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::child_with_attribute(
  const char* attribute_name, const char* value) const
{
  return child_with_attribute(::to_string(attribute_name),
                              ::to_string(value));
}
/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::child_with_attribute(
  const string& attribute_name, const string& value) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementVector, m_children) {
    XMLElement* child = *i;
    string child_value = child->attribute(attribute_name);
    if (value == child_value) {
      result = child;
      break;
    }
  }
  return result;
}

/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::child_with_name_and_attribute(
  const char* name, const char* attribute_name,
  const char* value) const
{
  return child_with_name_and_attribute(::to_string(name),
                                       attribute_name, value);
}
/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::child_with_name_and_attribute(
  const string& name, const string& attribute_name,
  const string& value) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementVector, m_children) {
    XMLElement* child = *i;
    string child_name = child->name();
    if (name == child_name) {
      string child_value = child->attribute(attribute_name);
      if (value == child_value) {
        result = child;
        break;
      }
    }
  }
  return result;
}
