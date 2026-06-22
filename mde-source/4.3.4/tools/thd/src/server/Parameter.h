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
// Parameter.h -- name/value pair
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef PARAMETER_H
#define PARAMETER_H

// C++ includes.
#include <string>       // string

// -----------------------------------------------------------------------------
// Parameter
// -----------------------------------------------------------------------------

/** Name/value pair. */
class Parameter
{
  // --- members ---
protected:
  /** Parameter name. */
  std::string m_name;

  /** Parameter value. */
  std::string m_value;

  // --- constructors/destructors ---
public:
  /** Constructor. */
  Parameter();

  /** Constructor. */
  Parameter(const char* name,
           const char* value);

  /** Constructor. */
  Parameter(const std::string& name,
           const std::string& value);

  /** Copy constructor. */
  Parameter(const Parameter& parameter);

  /** Assignment operator. */
  const Parameter&
  operator=(const Parameter& parameter);

  /** Destructor. */
  ~Parameter();


  // --- object methods ---
public:
  /** Equality comparison. */
  bool
  operator==(const Parameter& parameter) const;


  // --- methods ---
public:

  /** Gets parameter name. */
  const std::string&
  get_name() const;

  /** Sets parameter name. */
  void
  set_name(const std::string& name);

  /** Returns true if parameter has non-empty value. */
  bool
  has_value() const;

  /** Gets parameter value. */
  const std::string&
  get_value() const;

  /** Sets parameter value. */
  void
  set_value(const std::string& value);

};

// Multiple-inclusion guard.
#endif

