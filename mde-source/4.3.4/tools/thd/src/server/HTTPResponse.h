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
// HTTPResponse.h -- HTTP response data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

// C includes
#include <stdio.h>       // printf, etc.
#include <stdlib.h>      
#include <stdarg.h>      // va_arg() (variable-length argument lists)

// C++ includes
#include <string>        // string

// custom includes
#include "collections.h" // Array
#include "Parameter.h"   // Parameter


// -----------------------------------------------------------------------------
// HTTP_VERSION
// -----------------------------------------------------------------------------

/** Current HTTP version. */
#define HTTP_VERSION "HTTP/1.1"


// -----------------------------------------------------------------------------
// HTTPStatus
// -----------------------------------------------------------------------------

/** HTTP response status values. */
enum HTTPStatus
{
  HTTP_STATUS_UNDEFINED     = 0,   // Not set yet
  HTTP_STATUS_OK            = 200, // Resource found
  HTTP_STATUS_REDIRECT      = 302, // Resource located at different URL
  HTTP_STATUS_NOT_AVAILABLE = 404, // Resource not found/accessible
};


// -----------------------------------------------------------------------------
// HTTPResponse
// -----------------------------------------------------------------------------

/** HTTP response data structure */
class HTTPResponse
{
  // --- members ---
protected:
  /** Time at which request was sent. */
  time_t m_timestamp;

  /** HTTP version */
  std::string m_version;

  /** response status */
  HTTPStatus m_status;

  /** response reason */
  std::string m_reason;
  
  /** HTTP headers */
  Array<Parameter> m_headers;

  /** Whether this is a headers-only response. */
  bool m_headers_only;

  /** Body content, if any. */
  std::string m_content;

  /** Pathname of content file to append, if any. */
  std::string m_path;

  /** Explicit content length of file. */
  int m_content_length;


  // --- constructors/destructors ---
public:
  /** Constructor */
  HTTPResponse();

  /** Destructor */
  ~HTTPResponse();


  // --- init methods ---
protected:
  /** Initializes members. */
  void
  initialize();


  // --- accessors ---
public:

  /** Gets response status. */
  HTTPStatus
  get_status();

  /** Sets response status. */
  void
  set_status(HTTPStatus  status);

  /** Gets response reason. */
  const std::string&
  get_reason();

  /** Sets response reason. */
  void
  set_reason(const std::string& reason);

  /** Sets response status and reason. */
  void
  set_status(HTTPStatus status, const std::string& reason);

  /** Returns true if response has HTTP header with specified name. */
  bool
  has_http_header(const std::string& name);

  /** Gets value of named HTTP header.
      Returns true if header has a value, and sets value argument accordingly.
      Returns false otherwise, and clears value argument.
   */
  bool
  get_http_header(const std::string& name, std::string& value,
		  std::string default_value = "") const;

  /** Gets integer value of named HTTP header.
      Returns true if header has a value, and sets value argument accordingly.
      Returns false otherwise, and returns default in the value.
   */
  bool
  get_http_header(const std::string& name, int& value,
		  int default_value = 0) const;

  /** Adds HTTP header. Allows duplicate headers with same name. */
  void
  add_http_header(const std::string& name,
		  const std::string& value);

  /** Sets string HTTP header value. Replaces value of first instance,
      otherwise adds new instance. */
  void
  set_http_header(const std::string& name,
		  const std::string& value);

  /** Sets integer HTTP header value. Replaces value of first instance,
      otherwise adds new instance. */
  void
  set_http_header(const std::string& name,
		  const int& value);

  /** Sets whether this is a "headers only" response. */
  void
  set_headers_only(bool headers_only);

  /** Sets body content to specified text. */
  void
  set_content(const std::string& content);

  /** Appends specified text to body content. */
  void
  add_content(const std::string& content);

  /** Appends content of named file to body content.
      Null characters in text ('\0') are replaced with specified character. */
  void
  add_file_content(const std::string& pathname, char nullchar = '\n');

  /** Appends formatted text to body content. */
  void
  printf(const std::string& format, ...);

  /** Sets pathname of file to append.
      (Empty string means don't append a file.) */
  void
  set_content_path(const std::string& path);


  // --- methods ---
public:

  /** Writes an HTTP response to the specified connection fd. */
  int
  send(int fd);


  // --- debugging methods ---
public:

  /** Prints request content to stream */
  void
  fprint(FILE* fp, bool include_body = false);

};

// Multiple-inclusion guard.
#endif
