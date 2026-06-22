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
// string_utils.cc -- C/C++ String Utilities
// ==========================================================================

// header file
#include "string_utils.h" // C/C++ string utils
#include <string.h>
#include <stdio.h>


// -------------------------------------------------------------------------
// char* string utilities
// -------------------------------------------------------------------------

/** String equality test */
bool streql(const char* s1, const char* s2)
{
  return (strcmp(s1, s2) == 0);
}


// -------------------------------------------------------------------------
// std::string utilities
// -------------------------------------------------------------------------

/** Tests whether string starts with specified prefix string. */
bool starts_with(const std::string& string, const std::string& prefix)
{
  return string.find(prefix, 0) == 0;
}

/** Tests whether string ends with specified string. */
bool ends_with(const std::string& string, const std::string& suffix)
{
  int stringlen = string.size();
  int suffixlen = suffix.size();
  string::size_type suffixstart = stringlen - suffixlen;
  return (suffixstart >= 0) &&
    (suffixstart == string.rfind(suffix, stringlen));
}

/** Tests whether string contains specified substring. */
bool contains(const std::string& string, const std::string& substring)
{
  return string.find(substring, 0) != string::npos;
}

/** Splits string at first instance of specified character.
 *  Returns true if character is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if character is not found, and leaves
 *  before/after unchanged.
 */
bool split_string(const std::string& s, const char& c,
                  std::string& before, std::string& after)
{
  bool found = false;
  size_t start = s.find(c);
  if (start != string::npos) {
    found = true;
    size_t size = s.size();
    size_t end = start + 1;
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  return found;
}

/** Splits string s at first instance of specified substring.
 *  Returns true if substring is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if substring is not found, and leaves
 *  before/after unchanged.
 */
bool split_string(const std::string& s, const std::string& sub,
                  std::string& before, std::string& after)
{
  bool found = false;
  size_t start = s.find(sub);
  if (start != string::npos) {
    found = true;
    size_t size = s.size();
    size_t end = start + sub.size();
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  return found;
}

/** Splits string s at every instance of specified character.
 *  Returns true if substring is found,
 *  and populates split vector with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split vector unchanged.
 */
bool split_string(const std::string& s, const char& c,
                  Vector<std::string>& split)
{
  bool found = false;
  size_t pos = 0;
  while (true) {
    size_t start = s.find(c, pos);
    // if there are no more separators
    if (start == string::npos) {
      // store remaining substring in result vector
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else {
      // note that we saw the separator
      found = true;
      // store substring before it in result vector
      split.add(s.substr(pos,start-pos));
      // keep going to look for more separators after it
      size_t end = start + 1;
      pos = end;
    }
  }
  return found;
}

/** Splits string s at every instance of specified substring.
 *  Returns true if substring is found,
 *  and populates split vector with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split vector unchanged.
 */
bool split_string(const std::string& s, const std::string& sub,
                  Vector<std::string>& split)
{
  bool found = false;
  size_t pos = 0;
  while (true) {
    size_t start = s.find(sub, pos);
    // if there are no more separators
    if (start == string::npos) {
      // store remaining substring in result vector
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else {
      // note that we saw the separator
      found = true;
      // store substring before it in result vector
      split.add(s.substr(pos,start-pos));
      // keep going to look for more separators after it
      size_t end = start + sub.size();
      pos = end;
    }
  }
  return found;
}



/** Converts the given string to a bool */
bool to_bool (const std::string& str, const bool default_value)
{
  bool result = default_value;
  if (str != "") {
    result = (str != "false" &&
              str != "FALSE" &&
              str != "no" &&
              str != "0");
  }
  return result;
}

/** Converts the given string to an int */
int to_int (const std::string& str, const int default_value)
{
  int result = default_value;
  if (str.size() > 0) {
    int match_count = sscanf (str.c_str(), "%i", &result);
    if (match_count != 1) result = default_value;
  }
  return result;
}

/** Converts the given string to a long */
long to_long (const std::string& str, const long default_value)
{
  long result = default_value;
  if (str.size() > 0) {
    int match_count = sscanf (str.c_str(), "%li", &result);
    if (match_count != 1) result = default_value;
  }
  return result;
}

/** Converts the given string to a float */
float to_float (const std::string& str, const float default_value)
{
  float result = default_value;
  if (str.size() > 0) {
    int match_count = sscanf (str.c_str(), "%f", &result);
    if (match_count != 1) result = default_value;
  }
  return result;
}

/** Converts the given string to a double */
double to_double (const std::string& str, const double default_value)
{
  double result = default_value;
  if (str.size() > 0) {
    int match_count = sscanf (str.c_str(), "%lf", &result);
    if (match_count != 1) result = default_value;
  }
  return result;
}
