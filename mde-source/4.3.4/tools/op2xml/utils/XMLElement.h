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

// =========================================================================
// XMLElement.h -- XMLElement header
// =========================================================================

// multiple-inclusion guard
#ifndef XMLELEMENT_H
#define XMLELEMENT_H

// custom includes
#include "io.h"            // IO streams
#include "string_utils.h"  // C/C++ string utilities
#include "foreach.h"       // FOR_EACH macro
#include "Vector.h"        // vectors
#include "Map.h"           // hashtables


// -------------------------------------------------------------------------
// typedefs
// -------------------------------------------------------------------------

/** Attribute name->value mapping */
typedef Map<string, string> XMLAttributeMap;

/** Attribute name->value mapping */
typedef Vector<class XMLElement *> XMLElementVector;


// -------------------------------------------------------------------------
// XMLElement
// -------------------------------------------------------------------------

/** XML element (aka "tag") */
class XMLElement
{
  // --- members ---
protected:
  /** Parent of this element, if any */
  XMLElement* m_parent;

  /** name of this element */
  string m_name;

  /** attributes of this element */
  XMLAttributeMap m_attributes;

  /** children of this element */
  XMLElementVector m_children;

  /** Nested text content, if any */
  string m_text;


  // --- constructors ---
public:
  /** Constructor */
  XMLElement(const char* name) : m_parent(NULL), m_name(name)
  {}
  /** Constructor */
  XMLElement(const string& name) : m_parent(NULL), m_name(name)
  {}

  /** Constructor */
  XMLElement(const char* name,
             const XMLAttributeMap& attributes) :
    m_parent(NULL), m_name(name), m_attributes(attributes)
  {}
  /** Constructor */
  XMLElement(const string& name,
             const XMLAttributeMap& attributes) :
    m_parent(NULL), m_name(name), m_attributes(attributes)
  {}

  /** Copy constructor
      Note: this overload creates a "detached" copy --
      it does not copy the element's parent. */
  XMLElement(const XMLElement* element) {
    m_parent = NULL;
    m_name = element->m_name;
    m_attributes = element->m_attributes;
    m_text = element->m_text;
    FOR_EACH(const_iterator, i, XMLElementVector, element->m_children) {
      m_children.add(new XMLElement(this, *i));
    }
  }

  /** Copy constructor */
  XMLElement(XMLElement* parent, const XMLElement* element) {
    m_parent = parent;
    m_name = element->m_name;
    m_attributes = element->m_attributes;
    m_text = element->m_text;
    FOR_EACH(const_iterator, i, XMLElementVector, element->m_children) {
      m_children.add(new XMLElement(this, *i));
    }
  }

  /** Constructor */
  XMLElement(XMLElement* parent,
             const char* name) : m_parent(parent), m_name(name)
  {}
  /** Constructor */
  XMLElement(XMLElement* parent,
             const string& name) : m_parent(parent), m_name(name)
  {}

  /** Constructor */
  XMLElement(XMLElement* parent,
             const char* name, const XMLAttributeMap& attributes) :
    m_parent(parent), m_name(name), m_attributes(attributes)
  {}
  /** Constructor */
  XMLElement(XMLElement* parent,
             const string& name, const XMLAttributeMap& attributes) :
    m_parent(parent), m_name(name), m_attributes(attributes)
  {}

  /** Destructor */
  ~XMLElement()
  {
    m_parent = NULL;
    m_name.clear();
    m_attributes.clear();
    FOR_EACH(iterator, i, XMLElementVector, m_children) {
      delete *i;
    }
    m_children.clear();
    m_text.clear();
  }


  // --- to_string methods ---
public:
  /** Returns human-readable string representation of object */
  const string to_string() const;

  /** Returns string representation as C string */
  const char* c_str() const;


  // --- accessors ---
public:
  /** Gets name of this element */
  const string& name() const
  {
    return m_name;
  }

  /** Sets attribute of this element */
  void set_attribute(const string& name, const string& value)
  {
    m_attributes.put(name, value);
  }
  /** Gets attribute of this element */
  string attribute(const string& name,
                   const string& default_value = "") const
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
  const XMLAttributeMap& attributes() const
  {
    return m_attributes;
  }
  /** Clears attributes of this element */
  void clear_attributes()
  {
    m_attributes.clear();
  }

  /** Gets boolean-valued attribute of this element */
  bool bool_attribute(const string& name, const bool& default_value) const
  {
    return to_bool (attribute(name), default_value);
  }
  /** Gets integer-valued attribute of this element */
  int int_attribute(const string& name, const int& default_value) const
  {
    return to_int (attribute(name), default_value);
  }
  /** Gets long-valued attribute of this element */
  long long_attribute(const string& name, const long& default_value) const
  {
    return to_long (attribute(name), default_value);
  }
  /** Gets float-valued attribute of this element */
  float float_attribute(const string& name, const float& default_value) const
  {
    return to_float (attribute(name), default_value);
  }
  /** Gets double-valued attribute of this element */
  double double_attribute(const string& name,
                          const double& default_value) const
  {
    return to_double (attribute(name), default_value);
  }


  /** Sets nested text of element */
  void set_text(const string& text)
  {
    m_text = text;
  }
  /** Appends to nested text of element */
  void append_text(const string& text)
  {
    m_text += text;
  }
  /** Gets nested text of element */
  const string& text() const
  {
    return m_text;
  }


  // --- parent/child management ---
public:
  /** Gets parent of element */
  XMLElement* parent() const
  {
    return m_parent;
  }

  /** Adds child element */
  void add_child(XMLElement* child) {
    if (child->parent() != this) child->set_parent(this);
  }
  /** Removes child element */
  void remove_child(XMLElement* child) {
    if (child->parent() == this) child->set_parent(NULL);
  }

  /** Sets parent of element */
  void set_parent(XMLElement* parent) {
    if (m_parent != NULL) parent->m_children.remove(this);
    m_parent = parent;
    if (m_parent != NULL) parent->m_children.add(this);
  }

  /** Returns true if this element has children */
  bool has_children() const
  {
    return ! m_children.empty();
  }
  /** Returns list of children */
  const XMLElementVector& children() const
  {
    return m_children;
  }


  // --- child lookup methods ---
public:

  /** Returns child, if any, with specified name. */
  XMLElement* child_named(const char* name) const;
  /** Returns child, if any, with specified name. */
  XMLElement* child_named(const string& name) const;

  /** Returns children, if any, with specified name. */
  void children_named(const char* name, XMLElementVector& result) const;
  /** Returns children, if any, with specified name. */
  void children_named(const string& name, XMLElementVector& result) const;

  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* child_with_attribute(const char* attribute_name,
                                   const char* value) const;
  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* child_with_attribute(const string& attribute_name,
                                   const string& value) const;

  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* child_with_name_and_attribute(
     const char* name, const char* attribute_name, const char* value) const;
  /** Returns child, if any, with specified value for named attribute. */
  XMLElement* child_with_name_and_attribute(
     const string& name, const string& attribute_name,
     const string& value) const;

};

// multiple-inclusion guard
#endif
