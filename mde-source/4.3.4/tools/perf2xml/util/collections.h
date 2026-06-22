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
// collections.h -- STL-based collection classes
// ============================================================================

// multiple-inclusion guard
#ifndef COLLECTIONS_H
#define COLLECTIONS_H

// useful macro for when we're iterating over template types
// with multiple arguments, e.g.: Map<String COMMA String>
#ifndef COMMA
#define COMMA ,
#endif


// ----------------------------------------------------------------------------
// FOR_EACH iteration macros
// ----------------------------------------------------------------------------

/** Iterates ITER_VAR over values in collection.
    Collection values can be accessed as follows:
    value = *ITER
*/
#define FOR_EACH(ITER_TYPE, ITER_VAR, COLLECTION_TYPE, COLLECTION) \
  for(COLLECTION_TYPE::ITER_TYPE \
  ITER_VAR = (COLLECTION).begin(); \
  ITER_VAR != (COLLECTION).end(); \
  ++ITER_VAR)

/** Iterates ITER_VAR over values in collection in reverse order.
    Collection values can be accessed as follows:
    value = *ITER
*/
#define FOR_EACH_REVERSE(ITER_TYPE, ITER_VAR, COLLECTION_TYPE, COLLECTION) \
  for (COLLECTION_TYPE::reverse_##ITER_TYPE \
  ITER_VAR = (COLLECTION).rbegin() ; \
  ITER_VAR != (COLLECTION).rend() ; \
  ++ITER_VAR)

/** Iterates ITER_VAR over key/value pairs in map.
    Map values can be accessed as follows:
    key   = ITER_VAR->first()
    value = ITER_VAR->second()
*/
#define FOR_EACH_PAIR(ITER_TYPE, ITER_VAR, MAP_TYPE, MAP) \
  for(MAP_TYPE::ITER_TYPE ITER_VAR = (MAP).begin(); \
  ITER_VAR != (MAP).end(); \
  ++ITER_VAR)


// ----------------------------------------------------------------------------
// Array
// ----------------------------------------------------------------------------

#include <vector>

/** Auto-resizing array class.
    Operations:
     clear      -- removes all items
     add, +=    -- add item to end of array
     remove, -= -- remove first instance of item from array
     size       -- current array size
     a[index]   -- access an item
     contains(x) -- tests whether x is in the array
 */
template<typename T>
class Array : public std::vector<T>
{
  // --- members ---

  // --- constructors/destructors ---
public:
  /** Constructor */
  Array() : std::vector<T>() {}

  /** Copy constructor */
  Array(const std::vector<T>& v) :
    std::vector<T>(v) {}

  /** Assignment operator */
  Array& operator=(const std::vector<T>& v)
  {
    return std::vector<T>::operator=(v);
  }

  /** Destructor */
  virtual
  ~Array() {}

  // --- operators --
public:
  /** Operator overload for add() */
  virtual void
  operator+=(const T& value)
  {
    add(value);
  }

  /** Operator overload for remove() */
  virtual void
  operator-=(const T& value)
  {
    remove(value);
  }

  // --- methods ---
public:
  /** Clears all elements in the collection.
      Note: creator is responsible for deleting any pointers
      in collection first. */
  // void clear(); // defined by superclass

  /** Adds an element at the end of the collection */
  virtual void
  add(const T& value) { push_back(value); }

  /** Removes value from the collection. */
  virtual void
  remove(const T& value)
  {
    FOR_EACH(iterator, it, typename Array<T>, *this)
    {
      if (*it == value)
      {
        std::vector<T>::erase(it);
        break;
      }
    }
  }

  /** Gets nth element from the array.
      NOTE: operator[] is also defined, on base class. */
  T& get(size_t n)
  {
    return std::vector<T>::operator[](n);
  }

  /** Gets nth element from the array.
      NOTE: operator[] is also defined, on base class. */
  const T& get(size_t n) const
  {
    return std::vector<T>::operator[](n);
  }

  /** Returns iterator pointing to element, if found,
      or Array::end(), if not. */
  typename std::vector<T>::iterator find(const T& item)
  {
    typename std::vector<T>::iterator result = std::vector<T>::end();
    FOR_EACH(iterator, it, typename Array<T>, *this)
    {
      if (*it == item)
      {
        result = it;
        break;
      }
    }
    return result;
  }

  /** Returns iterator pointing to element, if found,
      or Array::end(), if not. */
  typename std::vector<T>::const_iterator find(const T& item) const
  {
    typename std::vector<T>::const_iterator result = std::vector<T>::end();
    FOR_EACH(const_iterator, it, typename Array<T>, *this)
    {
      if (*it == item)
      {
        result = it;
        break;
      }
    }
    return result;
  }

  /** Returns true if the array contains the specified item. */
  bool
  contains(const T& item) const
  {
    return find(item) != Array<T>::end();
  }
};


// ----------------------------------------------------------------------------
// Set
// ----------------------------------------------------------------------------

#include <set>

/** Set (resizable unique array) class
    Operations:
     clear       -- removes all items
     add, +=     -- add item to set (uniquely)
     remove, -=  -- remove item from set
     size        -- current set size
     a[index]    -- access an item
     contains(x) -- tests whether x is a member of the set
 */
template<typename T>
class Set : public std::set<T>
{
  // --- members ---

  // --- constructors/destructors ---
public:
  /** Constructor */
  Set() : std::set<T>() {}

  /** Copy constructor */
  Set(const std::set<T>& v) :
    std::set<T>(v) {}

  /** Assignment operator */
  Set& operator=(const std::set<T>& v)
  {
    return std::set<T>::operator=(v);
  }

  /** Destructor */
  virtual
  ~Set() {}

  // --- operators --
public:
  /** Operator overload for add() */
  virtual void
  operator+=(const T& value)
  {
    add(value);
  }

  /** Operator overload for remove() */
  virtual void
  operator-=(const T& value)
  {
    remove(value);
  }

  // --- methods ---
public:
  /** Clears all elements in the collection.
      Note: creator is responsible for deleting any pointers
      in collection first. */
  // void clear(); // defined by superclass

  /** Adds an element at the end of the collection. */
  virtual void
  add(const T& value)
  {
    insert(std::set<T>::end(), value);
  }

  /** Removes value from the collection. */
  virtual void
  remove(const T& value)
  {
    FOR_EACH(iterator, it, typename Set<T>, *this)
    {
      if (*it == value)
      {
        std::set<T>::erase(it);
        break;
      }
    }
  }

  /** Returns true if the set contains the specified item. */
  bool
  contains(const T& item) const
  {
    return find(item) != Set<T>::end();
  }
};


// ----------------------------------------------------------------------------
// Map
// ----------------------------------------------------------------------------

#include <map>

/** Associative map class, with possibly non-unique keys.
    Operations:
     clear            -- removes all mappings
     add(key,v)       -- add binding to set (allows duplicate keys)
     put(key,v)       -- add binding to set (replaces existing binding, if any)
     get(key,default) -- gets binding for key, if any, else returns default
     remove(key)      -- remove any binding(s) for key from set
     size             -- current map size
     contains(key)    -- tests whether there is at least one binding for key
 */
template<typename KEY, typename VALUE>
class Map : public std::map<KEY, VALUE>
{
  // --- members ---

  // --- constructors/destructors ---
public:
  /** Constructor */
  Map() : std::map<KEY, VALUE>() {}

  /** Copy constructor */
  Map(const std::map<KEY, VALUE>& m) :
    std::map<KEY, VALUE>(m) {}

  /** Assignment operator */
  Map& operator=(const std::map<KEY, VALUE>& m)
  {
    return std::map<KEY, VALUE>::operator=(m);
  }

  /** Destructor */
  ~Map() {}

  // --- methods ---
public:
  /** Clears all elements in the collection.
      Note: creator is responsible for deleting any pointers
      in collection first. */
  // void clear(); // defined by superclass

  /** Adds key/value pair to map.
  Allows multiple binds from key to value.
  */
  void
  add(const KEY& key, const VALUE& value)
  {
    (*this)[key] = value;
  }

  /** Adds key/value pair to map.
  Replaces any existing binding(s) for same key.
  */
  void
  put(const KEY& key, const VALUE& value)
  {
    remove(key);
    (*this)[key] = value;
  }

  /** Gets value, if any, for key */
  const VALUE&
  get(const KEY& key, const VALUE& defaultvalue) const
  {
    typename Map::const_iterator found = find(key);
    if (found != Map<KEY,VALUE>::end())
      return found->second;
    else
      return defaultvalue;
  }

  /** Removes all value mappings for key */
  void
  remove(const KEY& key)
  {
    typename Map::iterator found;
    while ((found = find(key)) != Map<KEY,VALUE>::end())
      erase(found);
  }

  /** Returns true if there is at least one binding for key in the map */
  bool
  contains(const KEY& key) const
  {
    return find(key) != Map<KEY,VALUE>::end();
  }

  /** Returns array of keys from the map. */
  Array<KEY>
  get_keys()
  {
    Array<KEY> result;
    return get_keys(result);
  }

  /** Returns array of keys from the map. */
  Array<KEY>&
  get_keys(Array<KEY>& keys)
  {
    keys.clear();
    FOR_EACH_PAIR(const_iterator, it, typename Map<KEY COMMA VALUE>, *this)
    {
      keys.add(it->first);
    }
    return keys;
  }

  /** Returns array of values from the map. */
  Array<VALUE>
  get_values()
  {
    Array<VALUE> result;
    return get_values(result);
  }

  /** Returns array of values from the map. */
  Array<VALUE>&
  get_values(Array<VALUE>& values)
  {
    values.clear();
    FOR_EACH_PAIR(const_iterator, it, typename Map<KEY COMMA VALUE>, *this)
    {
      values.add(it->second);
    }
    return values;
  }

};


// multiple-inclusion guard
#endif
