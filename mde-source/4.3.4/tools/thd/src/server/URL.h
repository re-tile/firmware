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
// URL.h -- URL data structure
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef URL_H
#define URL_H

// C++ includes.
#include <string>        // string

// Custom includes.
#include "collections.h" // Array
#include "Pathname.h"    // Pathname
#include "Parameter.h"   // Parameter


// -----------------------------------------------------------------------------
// URL
// -----------------------------------------------------------------------------

/** URL data structure.
    The result of processing a URL string like
    http://host:port/path/name?params...
    Note: this implementation mainly just keeps track of the
    /path/name and parameters, if any.
    */
class URL
{
  // --- members ---
protected:

  /** Original URL string. */
  std::string m_url;

  /** URL path (http://hostname/path). */
  // NOTE: this is an std::string, not a Pathname, 
  // because we want to keep a trailing slash, if any, on the path.
  std::string m_pathname;

  /** List of URL paramters (?arg1&arg2...), if any. */
  Array<Parameter> m_parameters;


  // --- constructors/destructors ---
public:
  /** Constructor, */
  URL();

  /** Constructor. */
  URL(std::string url);

  /** Copy constructor. */
  URL(const URL& url);

  /** Assignment operator. */
  const URL&
  operator=(const URL& url);

  /** Destructor. */
  ~URL();


  // --- object methods ---
public:

  /** Returns original URL text as string. */
  const std::string&
  to_string() const;


  // --- methods ---
public:
  /** Gets full URL string. (http://hostname/path?arg1&arg2...). */
  const std::string&
  get_url() const;

  /** Sets full URL string. */
  void
  set_url(std::string url);

  /** Gets URL path component (http://hostname/path). */
  const std::string&
  get_path() const;

  /** Gets URL parameter substring (?arg1&arg2...), if any. */
  const std::string
  get_parameter_string() const;

  /** Gets list of URL parameters (?arg1&arg2...), if any. */
  const Array<Parameter>&
  get_parameters() const;

  /** Returns true if URL has any parameters. */
  bool
  has_parameters() const;

  /** Gets count of URL parameters, if any. */
  int
  get_parameter_count() const;

  /** Gets n'th parameter. */
  const Parameter&
  get_parameter(int n) const;

};

// Multiple-inclusion guard.
#endif
