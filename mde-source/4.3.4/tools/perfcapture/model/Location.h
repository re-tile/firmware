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
// Location.h -- Location class
// ============================================================================

// multiple-inclusion guard
#ifndef LOCATION_H
#define LOCATION_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class Location;

/** Array of location instances. */
typedef Array<Location> LocationArray;


// ----------------------------------------------------------------------------
// Location
// ----------------------------------------------------------------------------

/** Location class. */
class Location
{
  // --- members ---
protected:
  /** Location id. */
  int m_id;

  /** Binary id. */
  int m_binary_id;
  
  /** Address. */
  std::string m_address;

  /** Symbol id. */
  int m_symbol_id;

  /** Source file. */
  std::string m_source_file;

  /** Source line. */
  int m_source_line;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Location(int binary_id,
           const std::string& address,
           int symbol_id,
           const std::string& source_file,
           int line);

  /** Copy constructor. */
  Location(const Location& location);

  /** Copy constructor. */
  Location(const Location* location);

  /** Assignment operator. */
  const Location& operator=(const Location& location);

  /** Assignment operator. */
  const Location& operator=(const Location* location);

  /** Destructor. */
  ~Location();


  // --- object methods ---
public:

  /** Equality test. */
  bool equals(const Location& location) const;

  /** Equality operator. */
  bool operator==(const Location& location) const;


  // --- accessors ---
public:
  /** Gets id. */
  int
  get_id() const
  {
    return m_id;
  }
  /** Sets id. */
  void
  set_id(int id)
  {
    m_id = id;
  }

  /** Gets binary id. */
  int
  get_binary_id() const
  {
    return m_binary_id;
  }

  /** Gets address. */
  const std::string&
  get_address() const
  {
    return m_address;
  }

  /** Gets symbol id. */
  int
  get_symbol_id() const
  {
    return m_symbol_id;
  }

  /** Gets source file. */
  const std::string&
  get_source_file() const
  {
    return m_source_file;
  }

  /** Gets source line. */
  int
  get_source_line() const
  {
    return m_source_line;
  }


  // --- methods ---
public:


};

// multiple-inclusion guard
#endif
