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
// foreach.h -- vector/list FOR_EACH() macros
// ==========================================================================

// inclusion guard
#ifndef FOREACH_H
#define FOREACH_H

// --------------------------------------------------------------------------
// FOR_EACH() -- vector/list iteration macros
// --------------------------------------------------------------------------
//
// The FOR_EACH() macro expands into a standard C/C++ for loop clause
// that walks the ITER_VAR iterator over each item in a collection.
// It requires that the collection support begin(), end(),
// iterator::operator!=() and ++iterator.
//
// The walked items can be accessed via *(ITER_VAR).
// For map/multimap collections, the item is a pair whose values
// can be accessed via i->first and i->second.
//
// The FOR_EACH_REVERSE() macro does the same thing, but walks the
// collection in reverse order.
// It requires that the collection support the reverse_ iterator types,
// rbegin(), and rend(). Note that you specify the iterator type
// as "iterator" or "const_iterator", and the "reverse_" prefix is added
// by the macro.
//
// Examples:
//
//   vector<int> v;
//   map<int, string> m;
//   FOR_EACH(const_iterator, i, vector<int>, v) {
//     printf("%d\n", *i);
//   }
//   FOR_EACH_REVERSE(const_iterator, i, map<int,string>, m) {
//     printf("%d, %s\n", i->first, i->second);
//   }
//

#define FOR_EACH(ITERATOR_TYPE, ITER_VAR, COLLECTION_TYPE, COLLECTION) \
  for (COLLECTION_TYPE::ITERATOR_TYPE ITER_VAR = (COLLECTION).begin() ; \
       ITER_VAR != (COLLECTION).end() ; \
       ++ITER_VAR)

#define FOR_EACH_REVERSE(ITERATOR_TYPE, ITER_VAR, \
                         COLLECTION_TYPE, COLLECTION) \
  for (COLLECTION_TYPE::reverse_##ITERATOR_TYPE ITER_VAR \
         = (COLLECTION).rbegin() ; \
       ITER_VAR != (COLLECTION).rend() ; \
       ++ITER_VAR)

// inclusion guard
#endif
