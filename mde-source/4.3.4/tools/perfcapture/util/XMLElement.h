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
// XMLElement.h -- XMLElement header
// ============================================================================

// multiple-inclusion guard
#ifndef XMLELEMENT_H
#define XMLELEMENT_H

// custom includes
#include "io_utils.h"       // IO streams
#include "string_utils.h"   // C/C++ string utilities
#include "collections.h"    // collections, FOR_EACH


// ----------------------------------------------------------------------------
// typedefs
// ----------------------------------------------------------------------------

/** Attribute name->value mapping */
typedef Map<std::string, std::string> XMLAttributeMap;

/** Attribute name->value mapping */
typedef Array<class XMLElement *> XMLElementArray;


// ----------------------------------------------------------------------------
// XMLElement
// ----------------------------------------------------------------------------

/** XML element (aka "tag") */
class XMLElement
{
  // --- members ---
protected:
  /** Parent of this element, if any */
  XMLElement* m_parent;

  /** name of this element */
  std::string m_name;

  /** attributes of this element */
  XMLAttributeMap m_attributes;

  /** children of this element */
  XMLElementArray m_children;

  /** Nested text content, if any */
  std::string m_text;


  // --- constructors ---
public:
  /** Constructor */
  XMLElement(const std::string& name,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             const std::string& attr5, const std::string& value5,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
    set_attribute(attr5, value5);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             const std::string& attr5, const std::string& value5,
             const std::string& attr6, const std::string& value6,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
    set_attribute(attr5, value5);
    set_attribute(attr6, value6);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             const std::string& attr5, const std::string& value5,
             const std::string& attr6, const std::string& value6,
             const std::string& attr7, const std::string& value7,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
    set_attribute(attr5, value5);
    set_attribute(attr6, value6);
    set_attribute(attr7, value7);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             const std::string& attr5, const std::string& value5,
             const std::string& attr6, const std::string& value6,
             const std::string& attr7, const std::string& value7,
             const std::string& attr8, const std::string& value8,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
    set_attribute(attr5, value5);
    set_attribute(attr6, value6);
    set_attribute(attr7, value7);
    set_attribute(attr8, value8);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const std::string& attr1, const std::string& value1,
             const std::string& attr2, const std::string& value2,
             const std::string& attr3, const std::string& value3,
             const std::string& attr4, const std::string& value4,
             const std::string& attr5, const std::string& value5,
             const std::string& attr6, const std::string& value6,
             const std::string& attr7, const std::string& value7,
             const std::string& attr8, const std::string& value8,
             const std::string& attr9, const std::string& value9,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
    set_attribute(attr1, value1);
    set_attribute(attr2, value2);
    set_attribute(attr3, value3);
    set_attribute(attr4, value4);
    set_attribute(attr5, value5);
    set_attribute(attr6, value6);
    set_attribute(attr7, value7);
    set_attribute(attr8, value8);
    set_attribute(attr9, value9);
  }

  /** Constructor */
  XMLElement(const std::string& name,
             const XMLAttributeMap& attributes,
             XMLElement* parent = NULL) :
    m_parent(parent), m_name(name), m_attributes(attributes)
  {
    if (m_parent != NULL) m_parent->m_children.add(this);
  }

  /** Copy constructor
      Note: this creates a "detached" copy of the element
      and its children. It does not copy the element's parent.
      The specified parent element, if any, is used instead.
      This allows "cloning" one part of an XML hierarchy into another.
  */
  XMLElement(const XMLElement* element, XMLElement* parent = NULL)
  {
    m_name = element->m_name;
    m_attributes = element->m_attributes;
    m_parent = parent;
    if (m_parent != NULL) m_parent->m_children.add(this);
    m_text = element->m_text;
    FOR_EACH(const_iterator, it, XMLElementArray, element->m_children)
    {
      new XMLElement(*it, this);
    }
  }

  /** Destructor */
  ~XMLElement()
  {
    m_name.clear();
    m_attributes.clear();
    m_text.clear();
    m_parent = NULL;
    FOR_EACH(iterator, it, XMLElementArray, m_children)
    {
      XMLElement* e = *it;
      delete e;
    }
    m_children.clear();
  }


  // --- to_string methods ---
public:
  /** Returns human-readable string representation of object */
  const std::string to_string() const;

  /** Returns string representation as C string */
  const char* c_str() const;


  // --- accessors ---
public:
  /** Gets name of this element */
  const std::string& get_name() const
  {
    return m_name;
  }

  /** Sets attribute of this element */
  void set_attribute(const std::string& name, const std::string& value)
  {
    m_attributes.put(name, value);
  }
  /** Gets attribute of this element */
  std::string get_attribute(const std::string& name,
                            const std::string& default_value = "") const
  {
    return m_attributes.get(name, default_value);
  }

  /** Sets attribute name/value map of this element */
  void set_attributes(const XMLAttributeMap& attributes)
  {
    m_attributes.clear();
    m_attributes = attributes;
  }
  /** Gets attribute name/value map of this element */
  const XMLAttributeMap& get_attributes() const
  {
    return m_attributes;
  }
  /** Clears attributes of this element */
  void clear_attributes()
  {
    m_attributes.clear();
  }

  /** Sets boolean-valued attribute of this element */
  void
  set_bool_attribute(const std::string& name,
                     const bool& value = true)
  {
    set_attribute(name, ::to_string(value));
  }
  /** Gets boolean-valued attribute of this element */
  bool
  get_bool_attribute(const std::string& name,
                     const bool& default_value = false) const
  {
    return to_bool(get_attribute(name), default_value);
  }

  /** Sets integer-valued attribute of this element */
  void
  set_int_attribute(const std::string& name,
                    const int& value = 0)
  {
    set_attribute(name, ::to_string(value));
  }
  /** Gets integer-valued attribute of this element */
  int
  get_int_attribute(const std::string& name,
                    const int& default_value = 0) const
  {
    return to_int(get_attribute(name), default_value);
  }

  /** Sets long-valued attribute of this element */
  void
  set_long_attribute(const std::string& name,
                     const long& value = 0L)
  {
    set_attribute(name, ::to_string(value));
  }
  /** Gets long-valued attribute of this element */
  long
  get_long_attribute(const std::string& name,
                     const long& default_value = 0L) const
  {
    return to_long(get_attribute(name), default_value);
  }

  /** Sets float-valued attribute of this element */
  void
  set_float_attribute(const std::string& name,
                      const float& value = 0.0f)
  {
    set_attribute(name, ::to_string(value));
  }
  /** Gets float-valued attribute of this element */
  float
  get_float_attribute(const std::string& name,
                      const float& default_value = 0.0f) const
  {
    return to_float(get_attribute(name), default_value);
  }

  /** Sets double-valued attribute of this element */
  void
  set_double_attribute(const std::string& name,
                       const double& value = 0.0)
  {
    set_attribute(name, ::to_string(value));
  }
  /** Gets double-valued attribute of this element */
  double
  get_double_attribute(const std::string& name,
                       const double& default_value = 0.0f) const
  {
    return to_double(get_attribute(name), default_value);
  }


  /** Sets nested text of element */
  void set_text(const std::string& text)
  {
    m_text = text;
  }
  /** Appends to nested text of element */
  void append_text(const std::string& text)
  {
    m_text += text;
  }
  /** Gets nested text of element */
  const std::string& get_text() const
  {
    return m_text;
  }


  // --- parent/child management ---
public:
  /** Gets parent of element */
  XMLElement* get_parent() const
  {
    return m_parent;
  }

  /** Creates and adds child element with specified name. */
  XMLElement* create_element(const std::string& name)
  {
    return add_element(new XMLElement(name));
  }

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6,
    const std::string& attr7, const std::string& value7);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6,
    const std::string& attr7, const std::string& value7,
    const std::string& attr8, const std::string& value8);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6,
    const std::string& attr7, const std::string& value7,
    const std::string& attr8, const std::string& value8,
    const std::string& attr9, const std::string& value9);

  /** Creates and adds child element with specified name
      and attributes. */
  XMLElement* create_element(const std::string& name,
                             const XMLAttributeMap& attributes);


  /** Adds child element */
  XMLElement* add_element(XMLElement* child);

  /** Removes child element */
  XMLElement* remove_element(XMLElement* child);

  /** Sets parent of element */
  void set_parent(XMLElement* parent);


  /** Returns true if this element has children */
  bool has_children() const
  {
    return ! m_children.empty();
  }
  /** Returns list of children */
  const XMLElementArray& get_children() const
  {
    return m_children;
  }


  // --- child lookup methods ---
public:

  /** Returns child, if any, with specified name. */
  XMLElement* get_child_named(const char* name) const;
  /** Returns child, if any, with specified name. */
  XMLElement* get_child_named(const std::string& name) const;

  /** Returns children, if any, with specified name. */
  void get_children_named(const char* name,
                          XMLElementArray& result) const;
  /** Returns children, if any, with specified name. */
  void get_children_named(const std::string& name,
                          XMLElementArray& result) const;

  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* get_child_with_attribute(const char* attribute_name,
                                       const char* value) const;
  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* get_child_with_attribute(const std::string& attribute_name,
                                       const std::string& value) const;

  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* get_child_with_name_and_attribute(
    const char* name,
    const char* attribute_name,
    const char* value) const;
  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* get_child_with_name_and_attribute(
    const std::string& name,
    const std::string& attribute_name,
    const std::string& value) const;

};

// multiple-inclusion guard
#endif
