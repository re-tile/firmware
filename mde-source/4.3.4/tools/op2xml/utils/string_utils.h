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
// string_utils.h -- C/C++ String Utilities
// ==========================================================================

// inclusion guard
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

// -------------------------------------------------------------------------
// char* string utilities
// -------------------------------------------------------------------------

/** String equality test */
bool streql(const char* s1, const char* s2);

/** character type tests (isalpha(), isalnum(), etc.) */
#include <cctype>

inline bool is_letter_or_digit(int c) { return isalnum(c); }
inline bool is_letter(int c)          { return isalpha(c); }
inline bool is_digit(int c)           { return isdigit(c); }
inline bool is_whitespace(int c)      { return isspace(c); }

/** casts integer to char */
inline char to_char(int c) {
  return static_cast<char>(c);
}


// -------------------------------------------------------------------------
// STL string class
// -------------------------------------------------------------------------

#include <string>
#include <sstream>


// -------------------------------------------------------------------------
// std::string utilities
// -------------------------------------------------------------------------

#include "Vector.h" // Vector collection class

/** Tests whether string starts with specified prefix string. */
bool starts_with(const std::string& string, const std::string& prefix);

/** Tests whether string ends with specified string. */
bool ends_with(const std::string& string, const std::string& suffix);

/** Tests whether string contains specified substring. */
bool contains(const std::string& string, const std::string& substring);

/** Splits string at first instance of specified character.
 *  Returns true if character is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if character is not found, and
 *  leaves before/after unchanged.
 */
bool split_string(const std::string& s, const char& c,
                  std::string& before, std::string& after);

/** Splits string s at first instance of specified substring.
 *  Returns true if substring is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if substring is not found, and leaves
 *  before/after unchanged.
 */
bool split_string(const std::string& s, const std::string& sub,
                  std::string& before, std::string& after);

/** Splits string s at every instance of specified character.
 *  Returns true if substring is found,
 *  and populates split vector with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split vector unchanged.
 */
bool split_string(const std::string& s, const char& c, Vector<string>& split);

/** Splits string s at every instance of specified substring.
 *  Returns true if substring is found,
 *  and populates split vector with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split vector unchanged.
 */
bool split_string(const std::string& s, const std::string& sub,
                  Vector<std::string>& split);

/** Converts value to string */
template<typename T>
std::string to_string(const T& value) {
  std::ostringstream stm;
  stm << value;
  return stm.str();
}

/** Converts the given string to a bool */
bool to_bool (const std::string& str, const bool default_value = false);

/** Converts the given string to an int */
int to_int (const std::string& str, const int default_value = 0);

/** Converts the given string to a long */
long to_long (const std::string& str, const long default_value = 0);

/** Converts the given string to a float */
float to_float (const std::string& str, const float default_value = 0.0);

/** Converts the given string to a double */
double to_double (const std::string& str, const double default_value = 0.0);

// inclusion guard
#endif
