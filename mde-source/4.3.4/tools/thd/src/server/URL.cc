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

// =============================================================================
// URL.cc -- URL data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "URL.h"

// C includes.
#include <stdio.h>       // printf, etc.
#include <stdlib.h>

// Custom includes.
#include "utils.h"
#include "string_utils.h"


// -----------------------------------------------------------------------------
// URL
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
URL::URL()
{
  set_url("");
}

/** Constructor. */
URL::URL(std::string url)
{
  set_url(url);
}

/** Copy constructor. */
URL::URL(const URL& url) :
  m_url(url.m_url),
  m_pathname(url.m_pathname),
  m_parameters(url.m_parameters)
{
}

/** Assignment operator. */
const URL&
URL::operator=(const URL& url)
{
  if (this == &url) return *this; // handle self-assignment
  m_url  = url.m_url;
  m_pathname = url.m_pathname;
  m_parameters = url.m_parameters;
  return *this;
}

/** Destructor. */
URL::~URL()
{
}


// --- object methods ---

/** Returns original URL text as string. */
const std::string&
URL::to_string() const
{
  return get_url();
}


// --- methods ---

/** Gets full URL string. (http://hostname/path?arg1&arg2...). */
const std::string&
URL::get_url() const
{
  return m_url;
}

/** Sets full URL string. */
void
URL::set_url(std::string url)
{
  // save original URL text, in case we want it
  m_url = url;

  // TODO: for now, we just split URL into http://host/path
  // and "?" argument list, if any;
  // we might also want to parse out HTTP method, hostname, etc.

  m_parameters.clear();
  std::string parameters;
  if (split_string(m_url, "?", m_pathname, parameters))
  {
    Array<std::string> paramlist;
    split_string(parameters, "&", paramlist);
    FOR_EACH(const_iterator, it, Array<std::string>, paramlist)
    {
      const std::string& param = *it;
      std::string name;
      std::string value;
      split_string(param, "=", name, value);
      m_parameters.add(Parameter(name, value));
    }
  }
}

/** Gets URL path component (http://hostname/path). */
const std::string&
URL::get_path() const
{
  return m_pathname;
}

/** Gets URL parameter substring (?arg1&arg2...), if any. */
const std::string
URL::get_parameter_string() const
{
  std::string result;
  FOR_EACH(const_iterator, it, Array<Parameter>, m_parameters)
  {
    // TODO: properly quote specials in parameter name/value strings
    const Parameter& param = *it;

    result += result.empty() ? "?" : "&";

    result += param.get_name();

    if (! param.get_value().empty())
    {
      result += "=" + param.get_value();
    }
  }
  return result;
}

/** Gets list of URL parameters (?arg1&arg2...), if any. */
const Array<Parameter>&
URL::get_parameters() const
{
  return m_parameters;
}

/** Returns true if URL has any parameters. */
bool
URL::has_parameters() const
{
  return ! m_parameters.empty();
}

/** Gets count of URL parameters, if any. */
int
URL::get_parameter_count() const
{
  return m_parameters.size();
}

/** Gets n'th parameter. */
const Parameter&
URL::get_parameter(int n) const
{
  return m_parameters.get(n);
}
