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
// XMLPrinter.h -- XMLPrinter header
// =========================================================================

// multiple-inclusion guard
#ifndef XMLPRINTER_H
#define XMLPRINTER_H

// custom includes
#include "io.h"            // IO streams
#include "string_utils.h"  // C/C++ strings
#include "foreach.h"       // FOR_EACH macro

// XML includes
#include "XMLUtils.h"
#include "XMLDocument.h"
#include "XMLElement.h"


// -------------------------------------------------------------------------
// XMLPrinter
// -------------------------------------------------------------------------

/** XMLDocument printing utility class */
// @TODO:  Unify with XMLUtils.h
class XMLPrinter
{
  // --- members ---
protected:
  /** Whether to format (i.e. indent) the output. */
  bool m_formatted;

  /** Initial indent, in spaces */
  int m_margin;

  /** Indent step */
  int m_step;

  
  // --- constructors ---
public:
  /** Constructor */
  XMLPrinter(bool formatted = true, int margin = 0, int step = 2) :
    m_formatted(formatted), m_margin(margin), m_step(step)
  {}

  /** Destructor */
  ~XMLPrinter()
  {}


  // --- accessors ---
public:
  /** Gets whether output is formatted (i.e. indented) */
  const bool& is_formatted()
  {
    return m_formatted;
  }
  /** Sets whether output is formatted (i.e. indented) */
  void set_formatted(bool formatted)
  {
    m_formatted = true;
  }


  // --- XML document/element output methods ---
public:
  /** Prints XML document to stream. */
  template<typename charT, typename traits> \
    void print(std::basic_ostream<charT, traits>& out,
	       const XMLDocument& document,
	       int indent = -1) const
  {
    if (indent == -1) indent = m_margin;
    print_xml_header(out, indent);
    const XMLElement* root = document.root();
    print(out, *root, indent);
  }

  /** Prints XML element to stream. */
  template<typename charT, typename traits> \
    void print(std::basic_ostream<charT, traits>& out,
	       const XMLElement& element,
	       int indent = -1) const
  {
    if (indent == -1) indent = m_margin;
    if (m_formatted) out << indent_spaces(indent);
    out << "<" << element.name();
    bool first_attribute = true;
    int attribute_indent = indent + 1 + element.name().length() + 1;
    FOR_EACH(const_iterator, attr_pair, XMLAttributeMap,
             element.attributes())
    {
      if (first_attribute) {
        first_attribute = false;
        out << " ";
      }
      else {
        if (m_formatted)
          out << endl << indent_spaces(attribute_indent);
        else
          out << " ";
      }
      out << attr_pair->first << "=\"" <<
        xml_format(attr_pair->second) << "\" ";
    }
    if (! element.has_children()) {
      out << "/>" << endl;
    }
    else {
      out << ">" << endl;
      FOR_EACH(const_iterator, child, XMLElementVector, element.children())
      {
	print(out, **child, indent + m_step);
      }
      if (m_formatted) out << indent_spaces(indent);
      out << "</" << element.name() << ">" << endl;
    }
  }


  // --- XML format output methods ---
public:

  /** Prints XML <?xml ...?> header to stream. */
  template<typename charT, typename traits> \
    void print_xml_header(std::basic_ostream<charT, traits>& out,
			  int indent = -1) const
  {
    if (indent == -1) indent = m_margin;
    if (m_formatted) out << indent_spaces(indent);
    XMLUtils::write_xml_declaration_line(out);
  }
};


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR(out, XMLDocument, x)
{
  XMLPrinter printer;
  printer.print(out, x);
  return out;
}

OUTPUT_STREAM_OPERATOR(out, XMLDocument*, x)
{
  XMLPrinter printer;
  printer.print(out, *x);
  return out;
}

OUTPUT_STREAM_OPERATOR(out, XMLElement, x)
{
  XMLPrinter printer;
  printer.print(out, x);
  return out;
}

OUTPUT_STREAM_OPERATOR(out, XMLElement*, x)
{
  XMLPrinter printer;
  printer.print(out, *x);
  return out;
}

// multiple-inclusion guard
#endif
