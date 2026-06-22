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
// HTTPRequest.h -- HTTP request data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

// C includes
#include <stdio.h>      // printf, etc.
#include <stdlib.h>

// C++ includes
#include <string>       // string

// custom includes
#include "URL.h"


// -----------------------------------------------------------------------------
// HTTPMode
// -----------------------------------------------------------------------------

// HTTP request mode values.

extern const char* HTTP_MODE_HEAD;
extern const char* HTTP_MODE_GET;
extern const char* HTTP_MODE_POST;


// -----------------------------------------------------------------------------
// HTTPRequest
// -----------------------------------------------------------------------------

/** HTTP request data structure */
class HTTPRequest
{
  // --- members ---
protected:
  /** Time at which request was received. */
  time_t m_timestamp;

  /** HTTP version of this request. */
  std::string m_version;

  /** HTTP method (GET, POST, etc.) of this request. */
  std::string m_method;

  /** URL of this request. */
  URL m_url;

  /** HTTP headers */
  Array<Parameter> m_headers;

  /** Body content, if any. */
  std::string m_content;


  // --- constructors/destructors ---
public:
  /** Constructor */
  HTTPRequest();

  /** Destructor */
  ~HTTPRequest();


  // --- init methods ---
protected:
  /** Initializes members. */
  void
  initialize();


  // --- accessors ---
public:
  /** Gets timestamp. */
  const time_t&
  get_timestamp() const;

  /** Gets source of the request.
      This is the value of the Host HTTP header, if any. */
  const std::string
  get_source() const;

  /** Gets request method. */
  const std::string&
  get_method() const;

  /** Gets request URL */
  const URL&
  get_url() const;

  /** Gets request path from URL. */
  // NOTE: this is an std::string, not Pathname,
  // because we want to keep any trailing slash on the pathname.
  const std::string&
  get_path() const;

  /** Returns true if request has HTTP header with specified name. */
  bool
  has_http_header(const std::string& name);

  /** Gets string value of named HTTP header.
      Returns true if header has a value, and sets value argument accordingly.
      Returns false otherwise, and sets value argument to default_value.
   */
  bool
  get_http_header(const std::string& name, std::string& value,
		  const std::string& default_value = "") const;

  /** Gets integer value of named HTTP header.
      Returns true if header has a value, and sets value argument accordingly.
      Returns false otherwise, and sets value argument to default_value.
   */
  bool
  get_http_header(const std::string& name, int& value,
		  int default_value = 0) const;


  // --- methods ---
public:

  /** Receives an HTTP request from the specified connection fd. */
  int
  receive(int fd);


  // --- debugging methods ---
public:

  /** Prints request content to stream */
  void
  fprint(FILE* fp, bool include_body = false);

};

// Multiple-inclusion guard.
#endif
