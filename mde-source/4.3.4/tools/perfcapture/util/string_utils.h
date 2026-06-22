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
// string_utils.h -- C/C++ String Utilities
// ============================================================================

// inclusion guard
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

// C/C++ includes
#include <cstdio>           // stdio
#include <string.h>         // strlen()
#include <string>           // std::string
#include <sstream>          // string stream, io manipulators

// custom includes
#include "collections.h"    // collections, FOR_EACH


// ----------------------------------------------------------------------------
// char* string utilities
// ----------------------------------------------------------------------------

/** String equality test */
bool streql(const char* s1, const char* s2);

/** character type tests (isalpha(), isalnum(), etc.) */
#include <cctype>

inline bool is_letter_or_digit(int c) { return isalnum(c); }
inline bool is_letter(int c)          { return isalpha(c); }
inline bool is_digit(int c)           { return isdigit(c); }
inline bool is_whitespace(int c)      { return isspace(c); }

/** casts integer to char */
inline char to_char(int c)
{
  return static_cast<char>(c);
}

/** Tests whether string starts with specified prefix string. */
bool starts_with(const char* string, const char* prefix);

/** Tests whether string contains specified substring. */
bool string_contains(const char* string, const char* substring);

/** Tests whether string ends with specified suffix string. */
bool ends_with(const char* string, const char* suffix);


// ----------------------------------------------------------------------------
// StringArray
// ----------------------------------------------------------------------------

/** Array of strings. */
typedef Array<std::string> StringArray;


// ----------------------------------------------------------------------------
// StringSet
// ----------------------------------------------------------------------------

/** Set of strings. */
typedef Set<std::string> StringSet;


// ----------------------------------------------------------------------------
// std::string utilities
// ----------------------------------------------------------------------------

/** Tests whether string starts with specified prefix string. */
bool starts_with(const std::string& string, const std::string& prefix);

/** Tests whether string contains specified substring. */
bool string_contains(const std::string& string, const std::string& substring);

/** Tests whether string ends with specified string. */
bool ends_with(const std::string& string, const std::string& suffix);


// ----------------------------------------------------------------------------
// string conversions
// ----------------------------------------------------------------------------

/** Converts value to string. */
template<typename T>
std::string to_string(const T& value)
{
  std::ostringstream stm;
  stm << value;
  return stm.str();
}

/** Converts numeric value to hex string. */
template<typename T>
std::string to_hex_string(const T& value)
{
  std::ostringstream stm;
  stm << "0x" << std::hex << value << std::dec;
  return stm.str();
}

/** Converts string to specified type. */
template<typename T>
T to(const std::string& str, T default_value)
{
  if (str.empty()) return default_value;
  std::stringstream ss(str);
  T value;
  return (ss >> value) ? value : default_value;
}

/** Converts the given string to a bool */
inline bool to_bool(const std::string& str, const bool default_value = false)
{
  return to<bool>(str, default_value);
}

/** Converts the given string to an int */
inline int to_int(const std::string& str, const int default_value = 0)
{
  return to<int>(str, default_value);
}

/** Converts the given string to a long */
inline long to_long(const std::string& str, const long default_value = 0)
{
  return to<long>(str, default_value);
}


/** Converts the given string to a float */
inline float to_float(const std::string& str, const float default_value = 0.0)
{
  return to<float>(str, default_value);
}

/** Converts the given string to a double */
inline double to_double(const std::string& str, const double default_value = 0.0)
{
  return to<double>(str, default_value);
}


// ----------------------------------------------------------------------------
// split(), split_any_of()
// ----------------------------------------------------------------------------

/** Splits string at first instance of specified character.
 *  Returns true if character is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if character is not found, and leaves
 *  before/after unchanged.
 */
bool split(const std::string& s, const char& c,
           std::string& before, std::string& after);

/** Splits string at first instance of specified substring.
 *  Returns true if substring is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if substring is not found, and leaves
 *  before/after unchanged.
 */
bool split(const std::string& s, const std::string& sub,
           std::string& before, std::string& after);

/** Splits string at first instance of any of the specified characters.
 *  Returns true if character is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if character is not found, and leaves
 *  before/after unchanged.
 */
bool split_any_of(const std::string& s, const std::string& chars,
                  std::string& before, std::string& after);

/** Splits string at every instance of specified character.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split(const std::string& s, const char& c,
           StringArray& split);

/** Splits string at every instance of specified substring.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split(const std::string& s, const std::string& sub,
           StringArray& split);

/** Splits string at every instance of one of the specified chars.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split_any_of(const std::string& s, const std::string& chars,
                  StringArray& split);

// ----------------------------------------------------------------------------
// find_end()
// ----------------------------------------------------------------------------

/** Searches for first appearance of the specified sub-string.
 *  If found, returns index one greater than last character of match.
 *  If not found, returns npos.
 */
std::string::size_type find_end(const std::string& str, const std::string& substr);


// ----------------------------------------------------------------------------
// hash_value()
// ----------------------------------------------------------------------------

/** Hash value type. */
typedef unsigned long hash_key_t;

/** Returns hash value for string. */
hash_key_t
hash_value(const char* str);

/** Returns hash value for string. */
hash_key_t
hash_value(const std::string& str);


// inclusion guard
#endif
