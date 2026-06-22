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
// io.h -- C/C++ IO Utilities
// ==========================================================================

// inclusion guard
#ifndef IO_H
#define IO_H

// C++ IO includes
#include <iostream>
using std::istream;
using std::ostream;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;

#include <fstream>
using std::ifstream;
using std::ofstream;


// --------------------------------------------------------------------------
// output stream operator boilerplate macros
// --------------------------------------------------------------------------

/** output stream operator boilerplate for a type */
#define OUTPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_ostream<charT, traits>& \
      operator<<(std::basic_ostream<charT, traits>& STREAMNAME, \
        const TYPENAME &VARNAME)

/** output stream operator boilerplate for a type */
#define OUTPUT_STREAM_OPERATOR_TEMPLATE(STREAMNAME, TYPENAME, \
                                        TEMPLATE_VAR, VARNAME) \
  template<typename TEMPLATE_VAR, typename charT, typename traits> \
    std::basic_ostream<charT, traits>& \
      operator<<(std::basic_ostream<charT, traits>& STREAMNAME, \
        const TYPENAME &VARNAME)

/** input stream operator boilerplate for a type */
#define INPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_istream<charT, traits>& \
      operator>>(std::basic_istream<charT, traits>& STREAMNAME, \
        const TYPENAME &VARNAME)

/** input stream operator boilerplate for a type */
#define INPUT_STREAM_OPERATOR_TEMPLATE(STREAMNAME, TYPENAME, \
                                       TEMPLATE_VAR, VARNAME) \
  template<typename TEMPLATE_VAR, typename charT, typename traits> \
    std::basic_istream<charT, traits>& \
      operator>>(std::basic_istream<charT, traits>& STREAMNAME, \
        const TYPENAME &VARNAME)


// --------------------------------------------------------------------------
// in_hex(value) stream manipulator
// --------------------------------------------------------------------------

/** Manipulator class: in_hex(value)
 *  Writes a single value to an ostream in hex,
 *  then restores stream's original base flags.
 */
template<typename T>
class InHexManipulator
{
private:
  /** Value to write out in hex */
  T m_value;
public:
  /** Constructor */
  InHexManipulator(T value) : m_value(value) {}
  /** Output stream manipulation operator */
  template<typename charT, typename traits>
    void operator() (std::basic_ostream<charT, traits>& stream) const
    {
      std::ios_base::fmtflags old = stream.flags();
      hex(stream);
      stream << m_value;
      stream.setf(old);
    }
};

/** in_hex(value) manipulator -- function template */
template<typename T>
InHexManipulator<T> in_hex(T value)
{
  return InHexManipulator<T>(value);
}

/** in_hex(value) manipulator -- ostream operator<< overload */
template<typename charT, typename traits, typename T>
    std::basic_ostream<charT, traits>&
      operator<<(std::basic_ostream<charT, traits>& stream,
                 const InHexManipulator<T>& m)
{
  m(stream);
  return stream;
}


// --------------------------------------------------------------------------
// indent_spaces(spaces) stream manipulator
// --------------------------------------------------------------------------

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

// inclusion guard
#endif
