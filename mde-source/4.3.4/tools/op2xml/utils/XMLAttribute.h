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
// XMLAttribute.h -- XMLAttribute and XMLAttributeVector classes
// ==========================================================================

// multiple-inclusion guard
#ifndef XML_ATTRIBUTE_H
#define XML_ATTRIBUTE_H

// custom headers
#include "XMLUtils.h"
#include "string_utils.h"
#include "Vector.h"


// -------------------------------------------------------------------------
// XMLAttribute
// -------------------------------------------------------------------------

class XMLAttribute
{
  // --- members ---
protected:
  /** Attribute's name */
  string m_name;

  /** Attribute's value, converted to a string by the constructor template */
  string m_value;


  // --- constructors ---
public:
  /** Constructor */
  XMLAttribute (const string& name, const string& value)
    : m_name(name), m_value(value)
  { }

  /** Constructor */
  template<typename ValueType>
  XMLAttribute (const string& name, const ValueType& value)
    : m_name(name), m_value(to_string(value))
  { }


  // --- accessor functions ---
public:
  /** Returns this attribute's name */
  const string& name() const { return m_name; }

  /** Returns this attribute's value */
  const string& value() const { return m_value; }
};


// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, XMLAttribute, attr)
{
  out << attr.name() << "=\"" << attr.value() << '"';
  return out;
}


// -------------------------------------------------------------------------
// XMLAttributeVector
// -------------------------------------------------------------------------

class XMLAttributeVector : public Vector<XMLAttribute>
{
  // --- constructors ---
public:
  /** Constructor */
  XMLAttributeVector() : Vector<XMLAttribute>()
  {}


  // --- member functions ---
public:
  /** Constructs an XMLAttribute using the given name and value,
      and adds it to this Vector */
  template<typename ValueType>
  void add (const string& name, const ValueType& value)
  {
    ((Vector<XMLAttribute>*) this)->add(XMLAttribute(name, value));
  }
};


// multiple-inclusion guard
#endif
