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
// XMLElement.cc -- XMLElement class
// ============================================================================

// custom includes
#include "XMLElement.h"    // header file

// ----------------------------------------------------------------------------
// XMLElement
// ----------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const std::string XMLElement::to_string() const
{
  std::ostringstream result;
  result << "<";
  result << m_name;
  FOR_EACH(const_iterator, pair, XMLAttributeMap, m_attributes)
  {
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


// --- parent/child management ---

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4,
                           const std::string& attr5, const std::string& value5)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4,
                   attr5, value5);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4,
                           const std::string& attr5, const std::string& value5,
                           const std::string& attr6, const std::string& value6)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4,
                   attr5, value5,
                   attr6, value6);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4,
                           const std::string& attr5, const std::string& value5,
                           const std::string& attr6, const std::string& value6,
                           const std::string& attr7, const std::string& value7)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4,
                   attr5, value5,
                   attr6, value6,
                   attr7, value7);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4,
                           const std::string& attr5, const std::string& value5,
                           const std::string& attr6, const std::string& value6,
                           const std::string& attr7, const std::string& value7,
                           const std::string& attr8, const std::string& value8)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4,
                   attr5, value5,
                   attr6, value6,
                   attr7, value7,
                   attr8, value8);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const std::string& attr1, const std::string& value1,
                           const std::string& attr2, const std::string& value2,
                           const std::string& attr3, const std::string& value3,
                           const std::string& attr4, const std::string& value4,
                           const std::string& attr5, const std::string& value5,
                           const std::string& attr6, const std::string& value6,
                           const std::string& attr7, const std::string& value7,
                           const std::string& attr8, const std::string& value8,
                           const std::string& attr9, const std::string& value9)
{
  XMLElement* element =
    new XMLElement(name,
                   attr1, value1,
                   attr2, value2,
                   attr3, value3,
                   attr4, value4,
                   attr5, value5,
                   attr6, value6,
                   attr7, value7,
                   attr8, value8,
                   attr9, value9);
  return add_element(element);
}

/** Creates and adds child element with specified name
    and attributes. */
XMLElement*
XMLElement::create_element(const std::string& name,
                           const XMLAttributeMap& attributes)
{
  return add_element(new XMLElement(name, attributes));
}

/** Adds child element */
XMLElement*
XMLElement::add_element(XMLElement* child)
{
  if (child->get_parent() != this) child->set_parent(this);
  return child;
}
/** Removes child element */
XMLElement*
XMLElement::remove_element(XMLElement* child)
{
  if (child->get_parent() == this) child->set_parent(NULL);
  return child;
}

/** Sets parent of element */
void
XMLElement::set_parent(XMLElement* parent)
{
  if (m_parent != NULL) parent->m_children.remove(this);
  m_parent = parent;
  if (m_parent != NULL) parent->m_children.add(this);
}


// --- child lookup methods ---

/** Returns child, if any, with specified name. */
XMLElement* XMLElement::get_child_named(const char* name) const
{
  return get_child_named(::to_string(name));
}

/** Returns child, if any, with specified name. */
XMLElement* XMLElement::get_child_named(const std::string& name) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementArray, m_children)
  {
    XMLElement* child = *i;
    std::string child_name = child->get_name();
    if (name == child_name)
    {
      result = child;
      break;
    }
  }
  return result;
}

/** Returns children, if any, with specified name. */
void XMLElement::get_children_named(const char* name,
                                    XMLElementArray& result) const
{
  get_children_named(::to_string(name), result);
}
/** Returns children, if any, with specified name. */
void XMLElement::get_children_named(const std::string& name,
                                    XMLElementArray& result) const
{
  FOR_EACH(const_iterator, i, XMLElementArray, m_children)
  {
    XMLElement* child = *i;
    std::string child_name = child->get_name();
    if (name == child_name) result.add(child);
  }
}

/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::get_child_with_attribute(
  const char* attribute_name,
  const char* value) const
{
  return get_child_with_attribute(::to_string(attribute_name),
                                  ::to_string(value));
}
/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::get_child_with_attribute(
  const std::string& attribute_name,
  const std::string& value) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementArray, m_children)
  {
    XMLElement* child = *i;
    std::string child_value = child->get_attribute(attribute_name);
    if (value == child_value)
    {
      result = child;
      break;
    }
  }
  return result;
}

/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::get_child_with_name_and_attribute(
  const char* name,
  const char* attribute_name,
  const char* value) const
{
  return get_child_with_name_and_attribute(::to_string(name),
                                           attribute_name, value);
}
/** Returns child, if any, with specified value for named attribute. */
XMLElement* XMLElement::get_child_with_name_and_attribute(
  const std::string& name,
  const std::string& attribute_name,
  const std::string& value) const
{
  XMLElement* result = NULL;
  FOR_EACH(const_iterator, i, XMLElementArray, m_children)
  {
    XMLElement* child = *i;
    std::string child_name = child->get_name();
    if (name == child_name)
    {
      std::string child_value = child->get_attribute(attribute_name);
      if (value == child_value)
      {
        result = child;
        break;
      }
    }
  }
  return result;
}
