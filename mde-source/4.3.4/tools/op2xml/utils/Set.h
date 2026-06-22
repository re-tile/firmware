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

// ==========================================================================
// Set.h -- wrapper for STL set class
// ==========================================================================

// inclusion guard
#ifndef SET_H
#define SET_H

// STL includes
#include <set>
using std::set;


// -------------------------------------------------------------------------
// Set
// -------------------------------------------------------------------------

/** Wrapper for STL set class that cleans up the interface a bit. */
template<typename T>
class Set : public set<T>
{
  // --- constructors/destructors ---
public:
  /** Constructor */
  Set() : set<T>()
  {}

  // --- methods ---
public:
  /** Adds item to set */
  void add(const T& item)
  {
    insert(item);
  }
  
  /** Tests whether set contains specified item */
  bool contains(const T& item)
  {
    return (Set::find(item) != Set::end());
  }
};


// -------------------------------------------------------------------------
// useful typedefs
// -------------------------------------------------------------------------

// set of strings
typedef Set<string> StringSet;


// inclusion guard
#endif
