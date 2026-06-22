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
// XMLUtils.h -- XML output utility class
// ==========================================================================

// inclusion guard
#ifndef XMLUTILS_H
#define XMLUTILS_H

// custom includes
#include "io.h"            // IO streams
#include "string_utils.h"  // C/C++ string utilities
#include "foreach.h"       // FOR_EACH macro
#include "XMLAttribute.h"  // XMLAttribute class


// -------------------------------------------------------------------------
// global state
// -------------------------------------------------------------------------

/** Whether to minimize white space by not indenting,
    and by not wrapping attributes */
extern bool g_minimize_white_space;


// -------------------------------------------------------------------------
// xml_format(string) stream manipulator
// -------------------------------------------------------------------------

/** Manipulator class: xml_format(string)
 *  Writes string to stream, formatting any
 *  XML special characters as entities.
 */
class XMLFormatManipulator
{
private:
  /* string to write out */
  const string& m_string;

public:
  /** Constructor */
  XMLFormatManipulator(const string& string) : m_string(string) {}

  /** Output stream manipulation operator */
  template<typename charT, typename traits>
    void operator() (std::basic_ostream<charT, traits>& stream) const
    {
      FOR_EACH(const_iterator, i, string, m_string) {
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
          if ((uc <= 0x1F) || uc >= 0x7F) {
            stream << "&#" << (int)uc << ";";
          } else {
            stream << c;
          }
          break;
        }
      }
    }
};

/** xml_format(string) manipulator -- function declaration */
inline XMLFormatManipulator xml_format(const string& string)
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


// -------------------------------------------------------------------------
// XMLUtils
// -------------------------------------------------------------------------

/** XML output utility class */
class XMLUtils
{
  // --- processing instructions ---
public:

  /** Writes '<?xml version="{version}" encoding="{encoding}" ?>'
      and a newline */
  static void write_xml_declaration_line(
	ostream&       out,
	const string&  version  = "1.0",
	const string&  encoding = "UTF-8"
  ) 
  {
    out << "<?xml version=\"" << version << "\" encoding=\""
        << encoding << "\" ?>" << endl;
  }

  // --- attributes ---
public:

  /** Writes 'name="value"' */
  template <typename ValueType>
  static void write_attribute (
          ostream&        out,
          const string&   name,
          const ValueType value
  )
  {
    out << name << "=\"" << xml_format(to_string(value)) << "\"";
  }

  /**
   * Writes white-space between attributes.
   * If "wrap_attributes" is false, then just a space is written.
   * Otherwise and newline is written, followed by enough spaces
   * to align the next attribute underneath the previous one,
   * as shown:
   * 
   *     <element_name attr_1="value_1"
   *                   attr_2="value_2"
   *                       :
   */
  static void write_attribute_separator (
          ostream&       out,
          bool           wrap_attributes,
          unsigned int   indentation,
          const string&  element_name
  ) {
    if ( ( ! g_minimize_white_space ) && wrap_attributes) {
      out << endl << leading_spaces(indentation + element_name.length() + 2);
    } else {
      out << ' ';
    }
  }

  /** Return the IndentSpacesManipulator to use for the given
      indentation count (and MINIMIZE_WHITE_SPACE flag) */
  static IndentSpacesManipulator leading_spaces(unsigned int indentation)
  {
    return IndentSpacesManipulator(g_minimize_white_space ? 0 : indentation);
  }


  // --- general element functions ---

public:
  /**
   * Writes (indented) '<name>' and a newline,
   * though if "include_slash" is true, "/>"
   * is written at the end instead of just ">"
   */
  static void write_element (
          ostream&        out,
          unsigned int    indentation,
          const string&   name,
          bool            include_slash
  ) {
    out << leading_spaces(indentation) << '<' << name;
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name="attr_value">' and a newline,
   * though if "include_slash" is true, "/>" is written
   * at the end instead of just ">"
   */
  template <typename ValueType>
  static void write_element (
          ostream&        out,
          unsigned int    indentation,
          const string&   name,
          bool            include_slash,
          const string&   attr_name,
          const ValueType attr_value
  ) {
    out << leading_spaces(indentation) << '<' << name << ' ';
    write_attribute(out, attr_name, attr_value);
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name1="attr_value1"
   *                          attr_name2="attr_value2">' and a newline,
   * though if "include_slash" is true, "/>" is written at the end
   * instead of just ">"
   */
  template <typename TypeOfValue1, typename TypeOfValue2>
  static void write_element (
          ostream&           out,
          unsigned int       indentation,
          const string&      name,
          bool               include_slash,
          const string&      attr_name1,
          const TypeOfValue1 attr_value1,
          const string&      attr_name2,
          const TypeOfValue2 attr_value2,
          bool               wrap_attributes = false
  ) {
    out << leading_spaces(indentation) << '<' << name << ' ';
    write_attribute(out, attr_name1, attr_value1);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name2, attr_value2);
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name1="attr_value1"
   *                          attr_name2="attr_value2"
   *                          attr_name3="attr_value3">' and a newline,
   * though if "include_slash" is true, "/>" is written at the end
   * instead of just ">"
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3>
  static void write_element (
          ostream&           out,
          unsigned int       indentation,
          const string&      name,
          bool               include_slash,
          const string&      attr_name1,
          const TypeOfValue1 attr_value1,
          const string&      attr_name2,
          const TypeOfValue2 attr_value2,
          const string&      attr_name3,
          const TypeOfValue3 attr_value3,
          bool               wrap_attributes = false
  ) {
    out << leading_spaces(indentation) << '<' << name << ' ';
    write_attribute(out, attr_name1, attr_value1);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name2, attr_value2);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name3, attr_value3);
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name1="attr_value1"
   *                          attr_name2="attr_value2"
   *                          attr_name3="attr_value3"
   *                          attr_name4="attr_value4" ...>' and a newline,
   * though if "include_slash" is true, "/>" is written at the end
   * instead of just ">"
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4>
  static void write_element (
          ostream&           out,
          unsigned int       indentation,
          const string&      name,
          bool               include_slash,
          const string&      attr_name1,
          const TypeOfValue1 attr_value1,
          const string&      attr_name2,
          const TypeOfValue2 attr_value2,
          const string&      attr_name3,
          const TypeOfValue3 attr_value3,
          const string&      attr_name4,
          const TypeOfValue4 attr_value4,
          bool               wrap_attributes = false
  ) {
    out << leading_spaces(indentation) << '<' << name << ' ';
    write_attribute(out, attr_name1, attr_value1);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name2, attr_value2);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name3, attr_value3);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name4, attr_value4);
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name1="attr_value1"
   *                          attr_name2="attr_value2"
   *                          attr_name3="attr_value3"
   *                          attr_name4="attr_value4"
   *                          attr_name5="attr_value5" ...>' and a newline,
   * though if "include_slash" is true, "/>" is written at the end
   * instead of just ">"
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4,
            typename TypeOfValue5>
  static void write_element (
          ostream&           out,
          unsigned int       indentation,
          const string&      name,
          bool               include_slash,
          const string&      attr_name1,
          const TypeOfValue1 attr_value1,
          const string&      attr_name2,
          const TypeOfValue2 attr_value2,
          const string&      attr_name3,
          const TypeOfValue3 attr_value3,
          const string&      attr_name4,
          const TypeOfValue4 attr_value4,
          const string&      attr_name5,
          const TypeOfValue5 attr_value5,
          bool               wrap_attributes = false
  ) {
    out << leading_spaces(indentation) << '<' << name << ' ';
    write_attribute(out, attr_name1, attr_value1);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name2, attr_value2);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name3, attr_value3);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name4, attr_value4);
    write_attribute_separator (out, wrap_attributes, indentation, name);
    write_attribute(out, attr_name5, attr_value5);
    write_terminator(out, include_slash);
  }

  /**
   * Writes (indented) '<name attr_name1="attr_value1"
   *                          attr_name2="attr_value2" ...>' and a newline,
   * though if "include_slash" is true, "/>" is written at the end
   * instead of just ">"
   */
  static void write_element (
          ostream&                   out,
          unsigned int               indentation,
          const string&              name,
          bool                       include_slash,
          const XMLAttributeVector&  attributes,
          bool                       wrap_attributes = false
  ) {
    int attr_count = attributes.size();
    if (attr_count == 0) {
      write_element (out, indentation, name, include_slash);
    }
    else {
      out << leading_spaces(indentation) << '<' << name << ' ';
      out << attributes[0];
      for (int i=1; i<attr_count; ++i) {
        write_attribute_separator (out, wrap_attributes, indentation, name);
        out << attributes[i];
      }
      write_terminator(out, include_slash);
    }
  }

  /** If "include_slash" is true, "/>" and a newline are written to "out";
      otherwise ">"/newline is written */
  static void write_terminator (ostream& out, bool include_slash)
  {
    out << (include_slash ? "/>" : ">") << endl;
  }


  /** Writes '</name>' */
  static void write_element_end (
    ostream&       out,
    unsigned int   indentation,
    const string&  name
  ) {
    out << leading_spaces(indentation) << "</" << name << '>' << endl;
  }


  // --- "write_element_start" functions ---

public:
  /** Writes "indentation" number of spaces, then '<name>' and a newline */
  static void write_element_start (
          ostream&       out,
          unsigned int   indentation,
          const string&  name
  ) {
    write_element (out, indentation, name, false);
  }

  /** Writes (indented) '<name attr_name1="attr_value1">' and a newline.
   *  The"attr_value" string is escaped as needed.
   */
  template <typename ValueType>
  static void write_element_start (
          ostream&        out,
          unsigned int    indentation,
          const string&   name,
          const string&   attr_name,
          const ValueType attr_value
  ) {
    write_element (out, indentation, name, false, attr_name, attr_value);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
   *                           attr_name2="attr_value2">' and a newline.
   *  The"attr_value" string is escaped as needed.
   */
  template <typename TypeOfValue1, typename TypeOfValue2>
  static void write_element_start (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, false,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2, wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
   *                           attr_name2="attr_value2"
   *                           attr_name3="attr_value3">' and a newline.
   *  The"attr_value" string is escaped as needed.
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3>
  static void write_element_start (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, false,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
   *                           attr_name2="attr_value2"
   *                           attr_name3="attr_value3"
   *                           attr_name4="attr_value4">' and a newline.
   *  The"attr_value" string is escaped as needed.
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4>
  static void write_element_start (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          const string&       attr_name4,
          const TypeOfValue4  attr_value4,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, false,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   attr_name4, attr_value4,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
   *                           attr_name2="attr_value2"
   *                           attr_name3="attr_value3"
   *                           attr_name4="attr_value4"
   *                           attr_name5="attr_value5">' and a newline.
   *  The"attr_value" string is escaped as needed.
   */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4,
            typename TypeOfValue5>
  static void write_element_start (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          const string&       attr_name4,
          const TypeOfValue4  attr_value4,
          const string&       attr_name5,
          const TypeOfValue5  attr_value5,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, false,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   attr_name4, attr_value4,
                   attr_name5, attr_value5,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2" ...>' and newline */
  static void write_element_start (
          ostream&                   out,
          unsigned int               indentation,
          const string&              name,
          const XMLAttributeVector&  attributes,
          bool                       wrap_attributes = false
  ) {
    write_element (out, indentation, name, false, attributes,
                   wrap_attributes);
  }


  // --- "write_complete_element" functions ---

public:
  /** Writes "indentation" number of spaces, then '<name/>' and a newline */
  static void write_complete_element (
          ostream&       out,
          unsigned int   indentation,
          const string&  name
  ) {
    write_element (out, indentation, name, true);
  }

  /**
   * Writes "indentation" number of spaces,
   * then '<name attr_name="attr_value"/>' and a newline;
   * "attr_value" is escaped as needed
   */
  template <typename ValueType>
  static void write_complete_element (
          ostream&        out,
          unsigned int    indentation,
          const string&   name,
          const string&   attr_name,
          const ValueType attr_value
  ) {
    write_element (out, indentation, name, true, attr_name, attr_value);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2"/>' and a newline */
  template <typename TypeOfValue1, typename TypeOfValue2>
  static void write_complete_element (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, true,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2" .../>' and newline */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3>
  static void write_complete_element (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, true,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2" .../>' and newline */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4>
  static void write_complete_element (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          const string&       attr_name4,
          const TypeOfValue4  attr_value4,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, true,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   attr_name4, attr_value4,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2" .../>' and newline */
  template <typename TypeOfValue1, typename TypeOfValue2,
            typename TypeOfValue3, typename TypeOfValue4,
            typename TypeOfValue5>
  static void write_complete_element (
          ostream&            out,
          unsigned int        indentation,
          const string&       name,
          const string&       attr_name1,
          const TypeOfValue1  attr_value1,
          const string&       attr_name2,
          const TypeOfValue2  attr_value2,
          const string&       attr_name3,
          const TypeOfValue3  attr_value3,
          const string&       attr_name4,
          const TypeOfValue4  attr_value4,
          const string&       attr_name5,
          const TypeOfValue5  attr_value5,
          bool                wrap_attributes = true
  ) {
    write_element (out, indentation, name, true,
                   attr_name1, attr_value1,
                   attr_name2, attr_value2,
                   attr_name3, attr_value3,
                   attr_name4, attr_value4,
                   attr_name5, attr_value5,
                   wrap_attributes);
  }

  /** Writes (indented) '<name attr_name1="attr_value1"
                               attr_name2="attr_value2" .../>' and newline */
  static void write_complete_element (
          ostream&                   out,
          unsigned int               indentation,
          const string&              name,
          const XMLAttributeVector&  attributes,
          bool                       wrap_attributes = false
  ) {
    write_element (out, indentation, name, true, attributes,
                   wrap_attributes);
  }
};

// inclusion guard
#endif
