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

// C/C++ includes
#include <cstdio> // sscanf


// ----------------------------------------------------------------------------
// char* string utilities
// ----------------------------------------------------------------------------

/** String equality test */
bool streql(const char* s1, const char* s2)
{
  return (strcmp(s1, s2) == 0);
}

/** Tests whether string contains specified substring. */
bool string_contains(const char* string, const char* substring)
{
  return strstr(string, substring) != NULL;
}

/** Tests whether string starts with specified prefix string. */
bool starts_with(const char* string, const char* prefix)
{
  bool result = true;
  for (int i=0; prefix[i] != '\0'; ++i)
  {
    if (string[i] != prefix[i])
    {
      result = false;
      break;
    }
  }
  return result;
}

/** Tests whether string ends with specified suffix string. */
bool ends_with(const char* string, const char* suffix)
{
  bool result = true;
  int len  = strlen(string);
  int len2 = strlen(suffix);
  for (int i=0; suffix[i] != '\0'; ++i)
  {
    int j = len-len2+i;
    if (j < 0 || string[j] != suffix[i])
    {
      result = false;
      break;
    }
  }
  return result;
}

// ----------------------------------------------------------------------------
// std::string utilities
// ----------------------------------------------------------------------------

/** Tests whether string starts with specified prefix string. */
bool starts_with(const std::string& string, const std::string& prefix)
{
  return string.find(prefix, 0) == 0;
}

/** Tests whether string contains specified substring. */
bool string_contains(const std::string& string, const std::string& substring)
{
  return string.find(substring, 0) != std::string::npos;
}

/** Tests whether string ends with specified string. */
bool ends_with(const std::string& string, const std::string& suffix)
{
  int stringlen = string.size();
  int suffixlen = suffix.size();
  std::string::size_type suffixstart = stringlen - suffixlen;
  return (suffixstart >= 0) &&
    (suffixstart == string.rfind(suffix, stringlen));
}



// ----------------------------------------------------------------------------
// string conversions
// ----------------------------------------------------------------------------



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
           std::string& before, std::string& after)
{
  bool found = false;
  size_t start = s.find(c);
  if (start != std::string::npos)
  {
    found = true;
    size_t size = s.size();
    size_t end = start + 1;
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  return found;
}

/** Splits string at first instance of specified substring.
 *  Returns true if substring is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if substring is not found, and leaves
 *  before/after unchanged.
 */
bool split(const std::string& s, const std::string& sub,
           std::string& before, std::string& after)
{
  bool found = false;
  size_t start = s.find(sub);
  if (start != std::string::npos)
  {
    found = true;
    size_t size = s.size();
    size_t end = start + sub.size();
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  return found;
}

/** Splits string at first instance of any of the specified characters.
 *  Returns true if character is found, and copies content
 *  before and after it to the before and after arguments.
 *  Returns false if character is not found, and leaves
 *  before/after unchanged.
 */
bool split_any_of(const std::string& s, const std::string& chars,
                  std::string& before, std::string& after)
{
  bool found = false;
  size_t start = s.find_first_of(chars);
  if (start != std::string::npos)
  {
    found = true;
    size_t size = s.size();
    size_t end = start + 1;
    before = s.substr(0,start);
    after = s.substr(end, size - end);
  }
  return found;
}

/** Splits string at every instance of specified character.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split(const std::string& s, const char& c,
           StringArray& split)
{
  bool found = false;
  size_t pos = 0;
  while (true)
  {
    size_t start = s.find(c, pos);
    // if there are no more separators
    if (start == std::string::npos)
    {
      // store remaining substring in result array
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else
    {
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

/** Splits string at every instance of specified substring.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split(const std::string& s, const std::string& sub,
           StringArray& split)
{
  bool found = false;
  size_t pos = 0;
  while (true)
  {
    size_t start = s.find(sub, pos);
    // if there are no more separators
    if (start == std::string::npos)
    {
      // store remaining substring in result array
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else
    {
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

/** Splits string at every instance of one of the specified chars.
 *  Returns true if split is found,
 *  and populates split array with substring(s).
 *  Returns false if split is not found,
 *  and leaves split array unchanged.
 */
bool split_any_of(const std::string& s, const std::string& chars,
                  StringArray& split)
{
  bool found = false;
  size_t pos = 0;
  while (true)
  {
    size_t start = s.find_first_of(chars, pos);
    // if there are no more separators
    if (start == std::string::npos)
    {
      // store remaining substring in result array
      split.add(s.substr(pos));
      // stop here
      break;
    }
    else
    {
      // note that we saw the separator
      found = true;
      // store substring before it in result array
      split.add(s.substr(pos,start-pos));
      size_t end = start + 1;
      pos = end;
    }
  }
  return found;
}


// ----------------------------------------------------------------------------
// find_end()
// ----------------------------------------------------------------------------

/** Searches for first appearance of the specified sub-string.
 *  If found, returns index one greater than last character of match.
 *  If not found, returns npos.
 */
std::string::size_type find_end(const std::string& str, const std::string& substr)
{
  std::string::size_type result = str.find(substr);
  if (result != std::string::npos)
  {
    result += substr.size();
  }
  return result;
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

