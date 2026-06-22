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
// Function.cc -- function class
// ============================================================================

#include "Function.h"


// ----------------------------------------------------------------------------
// Function
// ----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
Function::Function(Binary* binary,
                   int id,
                   const std::string& name,
                   const Pathname& source_file) :
  m_binary(binary),
  m_id(id),
  m_name(name),
  m_source_file(source_file)
{}

/** Copy constructor */
Function::Function(const Function& obj) :
  m_binary(obj.m_binary),
  m_id(obj.m_id),
  m_name(obj.m_name),
  m_source_file(obj.m_source_file)
{}

/** Assignment operator */
Function&
Function::operator=(const Function& obj)
{
  if (this != &obj) // handle self-assignment
  {
    m_binary = obj.m_binary;
    m_id = obj.m_id;
    m_name = obj.m_name;
    m_source_file = obj.m_source_file;
  }
  return *this;
}

Function::~Function()
{
  m_binary = NULL;
}


// --- accessors ---

