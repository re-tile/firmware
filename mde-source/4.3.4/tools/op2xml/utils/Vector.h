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
// Vector.h -- wrapper for STL vector class
// ==========================================================================

// inclusion guard
#ifndef VECTOR_H
#define VECTOR_H

// STL includes
#include <vector>
using std::vector;

#include <algorithm>
using std::sort;
using std::stable_sort;

#include <string>
using std::string;


// -------------------------------------------------------------------------
// Vector
// -------------------------------------------------------------------------

/** Wrapper for STL vector class that cleans up the interface a bit. */
template<typename T>
class Vector : public vector<T>
{
  // --- constructors/destructors ---
public:
  /** Constructor */
  Vector() : vector<T>()
  {}


  // --- methods ---
public:
  /** Appends item to end of vector */
  void add(const T& item)
  {
    push_back(item);
  }

  /** Appends all items of specified vector to end of vector */
  void add_all(const Vector<T>& v)
  {
    insert(Vector<T>::end(), v.begin(), v.end());
  }

  /** Gets iterator pointing to item at specified index. */
  typename Vector<T>::iterator item_at(unsigned index)
  {
    typename Vector<T>::iterator result = Vector<T>::begin();
    if (index <= Vector<T>::size()) result += index;
    else result = Vector<T>::end();
    return result;
  }

  /** Gets iterator pointing to first instance of specified item. */
  typename Vector<T>::const_iterator item(const T& item) const
  {
    return find(Vector<T>::begin(), Vector<T>::end(), item);
  }

  /** Gets iterator pointing to first instance of specified item. */
  typename Vector<T>::iterator item(const T& item)
  {
    return find(Vector<T>::begin(), Vector<T>::end(), item);
  }

  /** Returns true iff this Vector contains the given item */
  bool contains(const T& it) const
  {
    typename Vector<T>::const_iterator iter = item(it);
    return iter != Vector<T>::end();
  }

  /** Removes item at specified index */
  void remove_at(unsigned index)
  {
    typename Vector<T>::iterator iter = item_at(index);
    if (iter != Vector<T>::end()) erase(iter);
  }

  /** Removes first instance of specified item */
  void remove(const T& it)
  {
    typename Vector<T>::iterator iter = item(it);
    if (iter != Vector<T>::end()) erase(iter);
  }

  /** Sorts elements of vector using element type's '<' operator */
  void sort(bool stable = false)
  {
    if (stable)
      std::stable_sort(Vector::begin(), Vector::end());
    else
      std::sort(Vector::begin(), Vector::end());
  }

  /** Sorts elements of vector using specified comparator */
  template <typename Compare>
  void sort(Compare comp, bool stable = false)
  {
    if (stable)
      std::stable_sort(Vector::begin(), Vector::end(), comp);
    else
      std::sort(Vector::begin(), Vector::end(), comp);
  }
};


// -------------------------------------------------------------------------
// useful typedefs
// -------------------------------------------------------------------------

// vector of strings
typedef Vector<string> StringVector;


// inclusion guard
#endif
