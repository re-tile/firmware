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
// XMLPrinter.h -- XMLPrinter header
// ============================================================================

// multiple-inclusion guard
#ifndef XMLPRINTER_H
#define XMLPRINTER_H

// custom includes
#include "XMLDocument.h"    // XMLDocument, XMLElement
#include "io_utils.h"       // IO streams
#include "string_utils.h"   // C/C++ strings
#include "collections.h"    // collections, FOR_EACH
#include "Pathname.h"       // UNIX/Linux pathnames


// ----------------------------------------------------------------------------
// indent_spaces(spaces) stream manipulator
// ----------------------------------------------------------------------------

/** Manipulator class: indent_spaces(spaces)
 *  Writes a specified number of spaces to an output stream.
 */
class IndentSpacesManipulator
{
private:
  /** number of spaces to write out */
  int m_spaces;
public:
  /** Constructor */
  IndentSpacesManipulator(int spaces) : m_spaces(spaces) {}
  /** Output stream manipulation operator */
  template<typename charT, typename traits>
    void operator() (std::basic_ostream<charT, traits>& stream) const
    {
      for (int i=0; i<m_spaces; i++) stream << ' ';
    }
};

/** indent_spaces(spaces) manipulator -- function declaration */
inline IndentSpacesManipulator indent_spaces(int spaces)
{
  return IndentSpacesManipulator(spaces);
}

/** indent_spaces(spaces) manipulator -- ostream operator<< overload */
template<typename charT, typename traits>
    std::basic_ostream<charT, traits>&
       operator<<(std::basic_ostream<charT, traits>& stream,
                  const IndentSpacesManipulator& m)
{
  m(stream);
  return stream;
}


// ----------------------------------------------------------------------------
// xml_format(string) stream manipulator
// ----------------------------------------------------------------------------

/** Manipulator class: xml_format(string)
 *  Writes string to stream, formatting any
 *  XML special characters as entities.
 */
class XMLFormatManipulator
{
private:
  /* string to write out */
  const std::string& m_string;

public:
  /** Constructor */
  XMLFormatManipulator(const std::string& string) : m_string(string) {}

  /** Output stream manipulation operator */
  template<typename charT, typename traits>
    void operator() (std::basic_ostream<charT, traits>& stream) const
    {
      FOR_EACH(const_iterator, i, std::string, m_string)
      {
        char c = *i;
        switch (c)
        {
        case '\"': stream << "&quot;"; break;
        case '\'': stream << "&apos;"; break;
        case '&':  stream << "&amp;";  break;
        case '<':  stream << "&lt;";   break;
        case '>':  stream << "&gt;";   break;
        default:
          // handle characters outside printable ASCII range
          unsigned char uc = (unsigned char)c;
          if ((uc <= 0x1F) || uc >= 0x7F)
          {
            stream << "&#" << (int)uc << ";";
          } else
          {
            stream << c;
          }
          break;
        }
      }
    }
};

/** xml_format(string) manipulator -- function declaration */
inline XMLFormatManipulator xml_format(const std::string& string)
{
  return XMLFormatManipulator(string);
}

/** xml_format(string) manipulator -- ostream operator<< overload */
template<typename charT, typename traits>
  std::basic_ostream<charT, traits>& operator<<(
    std::basic_ostream<charT, traits>& stream, const XMLFormatManipulator& m)
{
  m(stream);
  return stream;
}


// ----------------------------------------------------------------------------
// XMLPrinter
// ----------------------------------------------------------------------------

/** XMLDocument printing utility class */
class XMLPrinter
{
  // --- static members ---
public:
  /** Prints XMLDocument to file. */
  static bool
  print(const XMLDocument& document, const Pathname& pathname, bool pretty_print = false)
  {
    bool result = false;
    std::ofstream output;
    output.open(pathname.c_str());
    if (! output.fail())
    {
      XMLPrinter printer(pretty_print);
      printer.print(output, document);
      output.close();
      result = true;
    }
    return result;
  }

  /** Prints XMLDocument to standard out. */
  static void
  print(const XMLDocument& document, bool pretty_print = false)
  {
    XMLPrinter printer(pretty_print);
    printer.print(std::cout, document);
  }

  /** Prints XMLElement to file. */
  static bool
  print(const XMLElement& element, const Pathname& pathname, bool pretty_print = false)
  {
    bool result = false;
    std::ofstream output;
    output.open(pathname.c_str());
    if (! output.fail())
    {
      XMLPrinter printer(pretty_print);
      printer.print(output, element);
      output.close();
      result = true;
    }
    return result;
  }

  /** Prints XMLElement to standard out. */
  static void
  print(const XMLElement element, bool pretty_print = false)
  {
    XMLPrinter printer(pretty_print);
    printer.print(std::cout, element);
  }

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
    const XMLElement* root = document.get_root();
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
    const std::string& element_name = element.get_name();
    out << "<" << element_name;
    bool first_attribute = true;
    int attribute_indent = indent + 1 + element_name.length() + 1;
    FOR_EACH(const_iterator, attr_pair, XMLAttributeMap,
             element.get_attributes())
    {
      if (first_attribute)
      {
        first_attribute = false;
        out << " ";
      }
      else
      {
        if (m_formatted)
          out << std::endl << indent_spaces(attribute_indent);
        else
          out << " ";
      }
      out << attr_pair->first
          << "=\"" << xml_format(attr_pair->second) << "\"";
    }
    if (! first_attribute) out << " "; // extra space after last attribute

    if (! element.has_children())
    {
      out << "/>" << std::endl;
    }
    else
    {
      out << ">" << std::endl;
      FOR_EACH(const_iterator, child, XMLElementArray, element.get_children())
      {
        print(out, **child, indent + m_step);
      }
      if (m_formatted) out << indent_spaces(indent);
      out << "</" << element.get_name() << ">" << std::endl;
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
    write_xml_declaration_line(out);
  }

  /** Writes '<?xml version="{version}" encoding="{encoding}" ?>'
      and a newline */
  static void write_xml_declaration_line(
        std::ostream&       out,
        const std::string&  version  = "1.0",
        const std::string&  encoding = "UTF-8"
  ) 
  {
    out << "<?xml version=\"" << version << "\" encoding=\""
        << encoding << "\" ?>" << std::endl;
  }
};


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR(out, const XMLDocument&, x)
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

OUTPUT_STREAM_OPERATOR(out, const XMLElement&, x)
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
