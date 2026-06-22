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
// Function.h -- function class
// ============================================================================

// multiple-inclusion guard
#ifndef FUNCTION_H
#define FUNCTION_H

// C/C++ includes
#include <string>     // std::string

// custom includes
class Binary; // avoid circular include
#include "Pathname.h" // Unix/Linux pathnames


// ----------------------------------------------------------------------------
// Function
// ----------------------------------------------------------------------------

/** Represents a function. */
class Function
{
  // --- members ---
protected:
  /** Binary. */
  Binary* m_binary;

  /** Function ID. */
  int m_id;

  /** Function name. */
  std::string m_name;

  /** Source file pathname, if known. */
  Pathname m_source_file;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Function(Binary* binary, int id, const std::string& name,
           const Pathname& source_file);

  /** Copy constructor */
  Function(const Function& obj);

  /** Assignment operator */
  Function& operator=(const Function& obj);

  ~Function();


  // --- accessors ---
public:
  /** Gets containing binary for this function. */
  Binary*
  get_binary()
  {
    return m_binary;
  }

  /** Gets function id. */
  int
  get_id() const
  {
    return m_id;
  }

  /** Gets function name. */
  const std::string&
  get_name() const
  {
    return m_name;
  }

  /** Gets source file pathname. */
  const Pathname&
  get_source_file() const
  {
    return m_source_file;
  }


  // --- methods ---
public:

};

// multiple-inclusion guard
#endif
