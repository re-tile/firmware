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
// HTTPResponse.cc -- HTTP response data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "HTTPResponse.h"

// C/C++ includes
#include <fcntl.h>        // open/close
#include <sys/sendfile.h> // sendfile
#include <errno.h>        // errno

// custom includes
#include "utils.h"       // randomize(), random()
#include "io_utils.h"    // fopen(fd, char*)


// -----------------------------------------------------------------------------
// HTTPResponse
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
HTTPResponse::HTTPResponse()
{
  initialize();
}

/** Destructor */
HTTPResponse::~HTTPResponse()
{
}


// --- init methods ---

/** Initializes members. */
void
HTTPResponse::initialize()
{
  m_timestamp = 0;
  m_version = HTTP_VERSION;
  m_status = HTTP_STATUS_OK;
  m_reason = "OK";
  m_headers_only = false;
  m_content_length = 0;
}


// --- accessors ---

/** Gets response status. */
HTTPStatus
HTTPResponse::get_status()
{
  return m_status;
}

/** Sets response status. */
void
HTTPResponse::set_status(HTTPStatus status)
{
  m_status = status;
}

/** Gets response reason. */
const std::string&
HTTPResponse::get_reason()
{
  return m_reason;
}

/** Sets response reason. */
void
HTTPResponse::set_reason(const std::string& reason)
{
  m_reason = reason;
}

/** Sets response status and reason string. */
void
HTTPResponse::set_status(HTTPStatus status, const std::string& reason)
{
  m_status = status;
  m_reason = reason;
}


// --- accessors ---

/** Returns true if response has HTTP header with specified name. */
bool
HTTPResponse::has_http_header(const std::string& name)
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
    Returns false otherwise, and clears value argument.
 */
bool
HTTPResponse::get_http_header(const std::string& name, std::string& value,
                              const std::string default_value) const
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
    Returns false otherwise, and returns default in the value.
 */
bool
HTTPResponse::get_http_header(const std::string& name, int& value,
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


/** Adds HTTP header. Allows duplicate headers with same name. */
void
HTTPResponse::add_http_header(const std::string& name,
                              const std::string& value)
{
  m_headers.add(Parameter(name, value));
}

/** Sets HTTP header value. Replaces value of first instance,
    otherwise adds new instance. */
void
HTTPResponse::set_http_header(const std::string& name,
                              const std::string& value)
{
  bool found = false;
  FOR_EACH(iterator, it, Array<Parameter>, m_headers)
  {
    Parameter& header = *it;
    if (header.get_name() == name)
    {
      header.set_value(value);
      found = true;
      break;
    }
  }
  if (! found)
  {
    m_headers.add(Parameter(name, value));
  }
}

/** Sets integer HTTP header value. Replaces value of first instance,
    otherwise adds new instance. */
void
HTTPResponse::set_http_header(const std::string& name,
                              const int& value)
{
  char buf[128];
  snprintf(buf, 127, "%i", value);
  set_http_header(name, buf);
}

/** Sets whether this is a "headers only" response. */
void
HTTPResponse::set_headers_only(bool headers_only)
{
  m_headers_only = headers_only;
}

/** Sets body content to specified text. */
void
HTTPResponse::set_content(const std::string& content)
{
  m_content = content;
}

/** Appends specified text to body content. */
void
HTTPResponse::add_content(const std::string& content)
{
  m_content += content;
}

/** Appends content of named file to body content.
    Null characters in text ('\0') are replaced with specified character. */
void
HTTPResponse::add_file_content(const std::string& pathname, char nullchar)
{
  if (! pathname.empty())
  {
    FILE* fp = fopen(pathname, "r");
    if (fp != NULL)
    {
      int len, maxlen = 513;
      char buffer[maxlen];
      while((len = fread(buffer, 1, maxlen, fp)) > 0)
      {
	buffer[len] = '\0';
        for (int i=0; i<len; ++i)
          if (buffer[i] == '\0') buffer[i] = nullchar;
        m_content += buffer;
      }
      fclose(fp);
    }
  }
}

/** Appends formatted text to body content. */
void
HTTPResponse::printf(const std::string& format, ...)
{
  const int maxlen = 512;
  char buf[maxlen];
  va_list varargs;
  va_start(varargs, format);
  vsnprintf(buf, maxlen, format.c_str(), varargs);
  m_content += buf;
  va_end(varargs);
}

/** Sets pathname of file to append.
    (Empty string means don't append a file.) */
void
HTTPResponse::set_content_path(const std::string& path)
{
  m_path = path;
}


// --- methods ---

/** Writes an HTTP response to the specified connection fd. */
int
HTTPResponse::send(int fd)
{
  int result = 0;

  // Capture time at which this response was sent
  m_timestamp = time(NULL);

  // Open a character output stream that writes to the file descriptor.
  FILE* stream = fopen(fd, "w");

  // write status header
  fprintf(stream, "%s %i %s\n", m_version.c_str(), m_status, m_reason.c_str());

  // write other HTTP headers
  FOR_EACH(const_iterator, it, Array<Parameter>, m_headers)
  {
    const Parameter& header = *it;
    fprintf(stream, "%s: %s\n",
            header.get_name().c_str(),
            header.get_value().c_str());
  }
  fprintf(stream, "\n"); // required blank line at end of headers

  if (! m_headers_only)
  {
    if (! m_path.empty())
    {
      // make sure we've flushed output stream,
      // since sendfile will write to raw file descriptor below
      fflush(stream);

      bool fallback = false;

      // for pseudo files with no length, always fallback
      if (m_content_length == 0) fallback = true;

      // first try using sendfile
      if (! fallback)
      {
        int resource = open(m_path.c_str(), O_RDONLY);
        if (resource < 0) {
          fallback = true;
        }
        else {
          ssize_t sent = sendfile(fd, resource, 0, m_content_length);
          close(resource);
          if (sent < 0 && (errno == EINVAL || errno == ENOSYS))
          {
            fallback = true;
          }
        }
      }

      // if sendfile can't send it, fallback to ordinary fread/fwrite
      if (fallback)
      {
        FILE* fp = fopen(m_path, "r");
        if (fp != NULL)
        {
          int len;
          char buffer[512];
          while((len = fread(buffer, 1, 512, fp)) > 0)
          {
            fwrite(buffer, 1, len, stream);
          }
          fclose(fp);
        }
      }
      
    }
    else if (! m_content.empty())
    {
      // write document body content, if any
      fprintf(stream, "%s", m_content.c_str());
    }
  }

  // flush/close stream, so file descriptor stays in sync
  fflush(stream);
  fclose(stream);

  return result;
}


// --- debugging methods ---

/** Prints request content to stream */
void
HTTPResponse::fprint(FILE* fp, bool include_body)
{
  fprintf(fp, "HTTP Response:\n");
  fprintf(fp, "Timestamp: %i\n", (int) m_timestamp);
  fprintf(fp, "Version:  %s\n", m_version.c_str());
  fprintf(fp, "Status:   %i\n", m_status);
  fprintf(fp, "Reason:   %s\n", m_reason.c_str());

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
      fprintf(fp, "[%i] %s: %s\n", ++i,
              header.get_name().c_str(),
              header.get_value().c_str());
    }
  }

  if (! m_path.empty())
  {
    fprintf(fp, "Path: %s\n", m_path.c_str());
  }

  if (include_body)
  {
    fprintf(fp, "Response Content:\n");
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
