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

#include "string_utils.h"
#include <stdio.h> // sscanf

// ----------------------------------------------------------------------------
// char* string utilities
// ----------------------------------------------------------------------------

/** String equality test */
bool streql(const char* s1, const char* s2)
{
  return (strcmp(s1, s2) == 0);
}

/** Compares C strings for equality, up to specified length. */
bool
streqln(const char* s1, const char* s2, int len)
{
  return (strncmp(s1, s2, len) == 0) ? true : false;
}

/** Returns pointer to first character following specified string.
    Returns NULL if the string is not found. */
char*
find_substring_after(const char* substring, char* s)
{
  char* result = strstr(s, substring);
  if (result != NULL)
    result += strlen(substring);
  return result;
}

/** Returns pointer to first character following specified string.
    Returns NULL if the string is not found. */
const char*
find_substring_after(const char* substring, const char* s)
{
  const char* result = strstr(s, substring);
  if (result != NULL)
    result += strlen(substring);
  return result;
}



// ----------------------------------------------------------------------------
// std::string utilities
// ----------------------------------------------------------------------------

/** Tests whether string starts with specified prefix string. */
bool starts_with(const std::string& prefix, const std::string& string)
{
  return string.find(prefix, 0) == 0;
}

/** Tests whether string ends with specified string. */
bool ends_with(const std::string& suffix, const std::string& string)
{
  int stringlen = string.size();
  int suffixlen = suffix.size();
  std::string::size_type suffixstart = stringlen - suffixlen;
  return (suffixstart >= 0) &&
    (suffixstart == string.rfind(suffix, stringlen));
}

/** Trims leading/trailing whitespace characters from argument string.
    Returns true if string was modified, false otherwise.
 */
bool
trim(std::string& s)
{
  bool result = false;
  int l  = s.length();
  int c1 = 0;
  int c2 = l;
  while (c1 < c2 && is_whitespace(s[c1]))   c1++;
  while (c2 > c1 && is_whitespace(s[c2-1])) c2--;
  // c1 is the first char (if any) we want to keep
  // c2 is one greater than the last char (if any) we want to keep
  // for an all-whitespace string, c1 == l == c2  (c2 - c1 = 0 chars)
  // for a string with one non-ws char at x, c1 == x, c2 == x+1
  if (c1 > 0 || c2 < l)
  {
    s = s.substr(c1,c2-c1);
    result = true;
  }
  return result;
}

/** Returns index of first instance of substring. 
    Returns -1 if no match was found. */
size_t
index_of_first(const std::string &substring, const std::string& s)
{
  size_t result = s.find(substring);
  if (result == std::string::npos) result = -1;
  return result;
}

/** Returns offset of substring following specified string.
    Returns std::string;:npos if the string is not found. */
size_t
find_substring_after(const std::string &substr, const std::string& s)
{
  size_t result = s.find(substr);
  if (result != std::string::npos)
  {
    result += substr.length();
  }
  return result;
}

/** Returns true if string contains all digits. */
bool
is_all_digits(const std::string& s)
{
  int result = true;
  FOR_EACH(const_iterator, it, std::string, s)
  {
    const char c = *it;
    if (! is_digit(c))
    {
      result = false;
      break;
    }
  }
  return result;
}


// ----------------------------------------------------------------------------
// string conversions
// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------
// split(), split_string()
// ----------------------------------------------------------------------------

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
  if (start != std::string::npos) {
    found = true;
    size_t size = s.size();
    size_t end = start + 1;
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  else {
    before = s;
    after = "";
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
  if (start != std::string::npos) {
    found = true;
    size_t size = s.size();
    size_t end = start + sub.size();
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  else {
    before = s;
    after = "";
  }
  return found;
}

/** Splits string s at every instance of specified character.
 *  Returns true if substring is found,
 *  and populates split array with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split array unchanged.
 */
bool split_string(const std::string& s, const char& c,
                  Array<std::string>& split)
{
  bool found = false;
  size_t pos = 0;
  while (true) {
    size_t start = s.find(c, pos);
    // if there are no more separators
    if (start == std::string::npos) {
      // store remaining substring in result array
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else {
      // note that we saw the separator
      found = true;
      // store substring before it in result array
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
 *  and populates split array with substring(s).
 *  Returns false if substring is not found,
 *  and leaves split array unchanged.
 */
bool split_string(const std::string& s, const std::string& sub,
                  Array<std::string>& split)
{
  bool found = false;
  size_t pos = 0;
  while (true) {
    size_t start = s.find(sub, pos);
    // if there are no more separators
    if (start == std::string::npos) {
      // store remaining substring in result array
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else {
      // note that we saw the separator
      found = true;
      // store substring before it in result array
      split.add(s.substr(pos,start-pos));
      // keep going to look for more separators after it
      size_t end = start + sub.size();
      pos = end;
    }
  }
  return found;
}


// ----------------------------------------------------------------------------
// hash_value()
// ----------------------------------------------------------------------------

/** Returns hash value for string. */
// CREDIT: This is derived from sdbm example at www.cse.yorku.ca/~oz/hash.html
hash_key_t
hash_value(const char* str)
{
  hash_key_t result = 0;
  int c;
  while ((c = *str++) != '\0')
    result = c + (result << 6) + (result << 16) - result;
  return result;
}

/** Returns hash value for string. */
hash_key_t
hash_value(const std::string& str)
{
  return hash_value(str.c_str());
}

