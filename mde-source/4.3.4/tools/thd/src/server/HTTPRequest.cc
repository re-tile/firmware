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
// HTTPRequest.cc -- HTTP request data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "HTTPRequest.h"

// custom includes
#include "URL.h"
#include "io_utils.h"      // fopen(int fd, char*)
#include "string_utils.h"


// -----------------------------------------------------------------------------
// HTTP mode constants
// -----------------------------------------------------------------------------

// HTTP request mode values.

const char* HTTP_MODE_HEAD = "HEAD";
const char* HTTP_MODE_GET  = "GET";
const char* HTTP_MODE_POST = "POST";


// -----------------------------------------------------------------------------
// HTTPRequest
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
HTTPRequest::HTTPRequest()
{
  initialize();
}

/** Destructor */
HTTPRequest::~HTTPRequest()
{
}


// --- init methods ---

/** Initializes members. */
void
HTTPRequest::initialize()
{
}


// --- accessors ---

/** Gets timestamp. */
const time_t&
HTTPRequest::get_timestamp() const
{
  return m_timestamp;
}

/** Gets source of the request.
    This is the value of the Host HTTP header, if any. */
const std::string
HTTPRequest::get_source() const
{
  std::string result;
  get_http_header("Host", result, "Unidentified");
  return result;
}

/** Gets request method. */
const std::string&
HTTPRequest::get_method() const
{
  return m_method;
}

/** Gets request URL. */
const URL&
HTTPRequest::get_url() const
{
  return m_url;
}

/** Gets request path. */
const std::string&
HTTPRequest::get_path() const
{
  return m_url.get_path();
}

/** Returns true if request has HTTP header with specified name. */
bool
HTTPRequest::has_http_header(const std::string& name)
{
  bool result = false;
  FOR_EACH(const_iterator, it, Array<Parameter>, m_headers)
  {
    const Parameter& header = *it;
    if (header.get_name() == name)
    {
      result = true;
      break;
    }
  }
  return result;
}

/** Gets string value of named HTTP header.
    Returns true if header has a value, and sets value argument accordingly.
    Returns false otherwise, and sets value argument to default_value.
 */
bool
HTTPRequest::get_http_header(const std::string& name, std::string& value,
                             const std::string& default_value) const
{
  bool result = false;
  value = default_value;
  FOR_EACH(const_iterator, it, Array<Parameter>, m_headers)
  {
    const Parameter& header = *it;
    if (header.get_name() == name)
    {
      value = header.get_value();
      result = true;
      break;
    }
  }
  return result;
}

/** Gets integer value of named HTTP header.
    Returns true if header has a value, and sets value argument accordingly.
    Returns false otherwise, and sets value argument to default_value.
 */
bool
HTTPRequest::get_http_header(const std::string& name, int& value,
                             int default_value) const
{
  bool result = false;
  std::string v;
  value = default_value;
  if (get_http_header(name, v))
  {
    sscanf(v.c_str(), "%i", &value);
    result = true;
  }
  return result;
}


// --- methods ---

/** Receives an HTTP request from the specified connection fd. */
int
HTTPRequest::receive(int fd)
{
  int result = -1;

  // Capture time at which this request was received.
  m_timestamp = time(NULL);

  // Create input stream associated with file descriptor.
  FILE* input = fopen(fd, "r");
  if (input == NULL) return result;

  std::string line;
  Array<std::string> tokens;

  // Create context we can break out of.
  do { 

    // Read GET URL HTTP/1.1 header.
    if (readline(input, line) <= 0)
    {
      result = -2;
      break;
    }
    split_string(line, " ", tokens);
    if (tokens.size() < 3)
    {
      result = -3;
      break;
    }
    m_version = tokens[2];
    m_method  = tokens[0];

    // Parse URL, including parameters, if any.
    m_url.set_url(tokens[1]); 

    // Read HTTP header lines ("Name: value"), up to first blank line.
    m_headers.clear();
    while (readline(input, line) > 0)
    {
      std::string name;
      std::string value;
      if (split_string(line, ":", name, value))
      {
        trim(name);
        trim(value);
      }
      m_headers.add(Parameter(name, value));
    }

    // Collect body content (for POST requests), if any,
    // based on size specified in Content-Length header, if any.
    int content_length = 0;
    get_http_header("Content-Length", content_length, 0);
    m_content.resize(content_length);
    int i=0, c=0;
    while (i<content_length && (c=getc(input)) != EOF)
      m_content[i++] = c;

    // Flush/close input stream.
    // (Note: need to flush since we may later be opening a write
    //  stream on the same file descriptor to send the response.)
    fflush(input);
    fclose(input);

    result = 0;

  } while(false);

  return result;
}


// --- debugging methods ---

/** Prints request content to stream */
void
HTTPRequest::fprint(FILE* fp, bool include_body)
{
  fprintf(fp, "HTTP Request:\n");
  fprintf(fp, "Timestamp: %i\n", (int) m_timestamp);
  fprintf(fp, "Version:  %s\n", m_version.c_str());
  fprintf(fp, "Method:   %s\n", m_method.c_str());
  fprintf(fp, "URL path: %s\n", m_url.get_path().c_str());
  if (m_url.has_parameters())
  {
    fprintf(fp, "URL parameters:\n");
    int i=-1;
    FOR_EACH(const_iterator, it, Array<Parameter>, m_url.get_parameters())
    {
      const Parameter& param = *it;
      fprintf(fp, "[%i] %s = %s\n", ++i,
              param.get_name().c_str(),
              param.get_value().c_str());
    }
  }

  fprintf(fp, "HTTP headers:\n");
  if (m_headers.empty())
  {
    fprintf(fp, "(no headers)\n");
  }
  else
  {
    int i=-1;
    FOR_EACH(const_iterator, it, Array<Parameter>, m_headers)
    {
      const Parameter& header = *it;
      fprintf(fp,"[%i] %s: %s\n", ++i,
              header.get_name().c_str(),
              header.get_value().c_str());
    }
  }

  if (include_body)
  {
    fprintf(fp, "Request Content:\n");
    if (m_content.empty())
    {
      fprintf(fp, "(none)\n");
    }
    else
    {
      fprintf(fp, "%s\n", m_content.c_str());
    }
  }
}
