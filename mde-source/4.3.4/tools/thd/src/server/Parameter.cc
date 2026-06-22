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
// Parameter.cc -- name/value pair
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "Parameter.h"

// -----------------------------------------------------------------------------
// Parameter
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
Parameter::Parameter() :
  m_name(""),
  m_value("")
{
}

/** Constructor. */
Parameter::Parameter(const char* name,
         const char* value) :
  m_name(name),
  m_value(value)
{
}

/** Constructor. */
Parameter::Parameter(const std::string& name,
         const std::string& value) :
  m_name(name),
  m_value(value)
{
}

/** Copy constructor. */
Parameter::Parameter(const Parameter& parameter) :
  m_name(parameter.m_name),
  m_value(parameter.m_value)
{
}

/** Assignment operator. */
const Parameter&
Parameter::operator=(const Parameter& parameter)
{
  if (this != &parameter) // handle self-assignment
  {
    m_name  = parameter.m_name;
    m_value = parameter.m_value;
  }
  return *this;
}

/** Destructor. */
Parameter::~Parameter()
{
}


// --- object methods ---

/** Equality comparison. */
bool
Parameter::operator==(const Parameter& parameter) const
{
  return (
    m_name == parameter.m_name &&
    m_value == parameter.m_value
  );
}


// --- methods ---

/** Gets parameter name. */
const std::string&
Parameter::get_name() const
{
  return m_name;
}

/** Sets parameter name. */
void
Parameter::set_name(const std::string& name)
{
  m_name = name;
}

/** Returns true if parameter has non-empty value. */
bool
Parameter::has_value() const
{
  return ! m_value.empty();
}

/** Gets parameter value. */
const std::string&
Parameter::get_value() const
{
  return m_value;
}

/** Sets parameter value. */
void
Parameter::set_value(const std::string& value)
{
  m_value = value;
}


