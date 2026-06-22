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
// Map.h -- wrapper for STL map class
// ==========================================================================

// inclusion guard
#ifndef MAP_H
#define MAP_H

// STL includes
#include <map>
using std::map;
using std::multimap;


// --------------------------------------------------------------------------
// Map
// --------------------------------------------------------------------------

/** Wrapper for STL map class that cleans up the interface a bit. */
template<typename KEY, typename VALUE>
class Map : public map<KEY, VALUE>
{
  // --- constructors/destructors ---
public:
  /** Constructor */
  Map() : map<KEY, VALUE>()
  {}

  // --- methods ---
public:
  /** Adds key/value pair to map (convenient alias for put()) */
  void add(const KEY& key, const VALUE& value)
  {
    put(key, value);
  }
  
  /** Removes binding, if any, for specified key */
  void remove(const KEY& key)
  {
    erase(key);
  }
  
  /** Adds key/value pair to map.
   *  If map already has a binding for key,
   *  this binding replaces it.
   */
  void put(const KEY& key, const VALUE& value)
  {
    // remove existing binding, if any
    if (contains(key)) erase(key);
    typename Map::value_type pair(key, value);
    insert(pair);
  }
  
  /** Tests whether set contains specified key */
  bool contains(const KEY& key) const
  {
    return (Map::find(key) != Map::end());
  }

  /** Gets value of key from map, or default if key is not found */
  VALUE get(const KEY& key, const VALUE default_value) const
  {
    typename Map::const_iterator iter = Map::find(key);
    if (iter != Map::end()) {
      return iter->second;
    }
    else {
      return default_value;
    }
  }
};


// --------------------------------------------------------------------------
// MultiMap
// --------------------------------------------------------------------------

/** Wrapper for STL multimap class that cleans up the interface a bit. */
template<typename KEY, typename VALUE>
class MultiMap : public multimap<KEY, VALUE>
{
  // --- constructors/destructors ---
public:
  /** Constructor */
  MultiMap() : multimap<KEY, VALUE>()
  {}

  // --- methods ---
public:
  /** Adds key/value pair to map (alias for put()) */
  void add(const KEY& key, const VALUE& value)
  {
    typename MultiMap::value_type pair(key, value);
    insert(pair);
  }
  
  /** Removes binding(s), if any, for specified key */
  void remove(const KEY& key)
  {
    erase(key);
  }
  
  /** Adds key/value pair to map.
   *  Note that this is a multimap,
   *  so more than one pair with the same key is allowed.
   */
  void put(const KEY& key, const VALUE& value)
  {
    typename MultiMap::value_type pair(key, value);
    insert(pair);
  }
  
  /** Tests whether set contains specified key */
  bool has(const KEY& key)
  {
    return (MultiMap::find(key) != MultiMap::end());
  }

  /** Gets value of key from map, or default if key is not found */
  VALUE get(const KEY& key, const VALUE default_value) const
  {
    typename MultiMap::const_iterator iter = MultiMap::find(key);
    if (iter != MultiMap::end()) {
      return (*iter).second;
    }
    else {
      return default_value;
    }
  }
};


// inclusion guard
#endif
