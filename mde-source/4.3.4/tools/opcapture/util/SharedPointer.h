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
// SharedPointer.h -- reference-counting pointer class
// ==========================================================================
// Credit: derived from Yonat's "counted pointer" example
//         (http://ootips.org/yonat/4dev/counted_ptr.h)
//
// NOTE: we define this class rather than use Boost's shared_ptr so we
// don't have to pull in the entire Boost library for this one feature.
// ==========================================================================

// multiple-inclusion guard
#ifndef SHARED_POINTER_H
#define SHARED_POINTER_H

// C/C++ includes
#include <cstdio>      // NULL

// custom includes
#include "io_utils.h"  // IO streams


// --------------------------------------------------------------------------
// SharedPointer
// --------------------------------------------------------------------------

/** Simple reference-counting pointer class */
template<typename T>
class SharedPointer
{
private:

  // -----------------------------------------------------------------------
  // SharedPointer.Reference
  // -----------------------------------------------------------------------

  /** Internal class used to manage counting of references */
  class Reference
  {

    // --- members ---
  public:
    /** Current reference count */
    unsigned long m_count;

    /** Actual pointer for which we're counting references */
    T* m_pointer;


    // --- constructors/destructors ---
  public:    

    /** Constructor */
    Reference(T* pointer = NULL, unsigned long count = 1) :
      m_count(count), m_pointer(pointer)
    {}

    /** Destructor */
    ~Reference()
    {
      // when last referencing SharedPointer deletes us,
      // we can delete the pointer
      delete m_pointer;
    }


    // --- reference-counting methods ---
  public:
    /** Called by SharedPointer to increment reference count. */
    void increment() { ++m_count; }

    /** Called by SharedPointer to decrement reference count.
     *  Returns false when count reaches 0 and the Reference
     *  can be deleted by the calling SharedPointer object.
     */
    bool decrement()
    {
      return (--m_count > 0);
    }


    // --- pointer access members ---
  public:
    /** Pointer dereference operator */
    T& operator*()
    {
      return *m_pointer;
    }

    /** Pointer to structure dereference operator */
    T* operator->()
    {
      return m_pointer;
    }

    /** Pointer comparison operator*/
    bool operator==(const Reference& reference) const
    {
      return reference.m_pointer == m_pointer;
    }

    /** Pointer comparison operator*/
    bool operator!=(const Reference& reference) const
    {
      return reference.m_pointer != m_pointer;
    }

    /** Pointer comparison operator
        (special case to allow comparison with NULL) */
    bool operator==(T* pointer) const
    {
      return m_pointer == pointer;
    }

    /** Pointer comparison operator
        (special case to allow comparison with NULL) */
    bool operator!=(T* pointer) const
    {
      return m_pointer != pointer;
    }

  };


  // -----------------------------------------------------------------------
  // SharedPointer
  // -----------------------------------------------------------------------

  // --- members ---
private:  
  /** Reference counter object */
  Reference *m_reference;


  // --- constructors/destructors ---
public:
  /** Constructor */
  SharedPointer(T* pointer = NULL)
    : m_reference(NULL)
  {
    // shared pointer that first receives a pointer allocates
    // a reference object
    if (pointer != NULL)
      m_reference = new Reference(pointer);
  }

  /** Copy constructor */
  SharedPointer(const SharedPointer& sharedpointer) throw()
  {
    // get shared reference object and increment its reference count
    acquire(sharedpointer);
  }

  /** Assignment operator */
  const SharedPointer& operator=(const SharedPointer& sharedpointer)
  {
    // ignore self-assignments
    if (this != &sharedpointer)
    {
      // assignment constructor releases any reference we currently have,
      // then gets shared reference object and increments its reference count
      release();
      acquire(sharedpointer);
    }
    return *this;
  }

  /** Assignment operator */
  const SharedPointer& operator=(T* pointer)
  {
    // ignore self-assignments
    if (*this != pointer)
    {
      // assignment constructor releases any reference we currently have,
      // then allocates a reference object for the pointer
      release();
      if (pointer != NULL)
        m_reference = new Reference(pointer);
    }
    return *this;
  }

  /** Destructor */
  ~SharedPointer()
  {
    // when shared pointer is deleted or goes out of scope,
    // it removes reference from reference; this may or may not delete
    // the reference as well,
    // depending on whether any other shared pointer has copied it
    release();
  }


  // --- reference-management methods ---
private:
  /** Acquire shared reference object, and increment its reference count */
  void acquire(const SharedPointer& sharedpointer)
  {
    m_reference = sharedpointer.m_reference;
    if (m_reference != NULL) m_reference->increment();
  }

  /** Release reference object, and decrement its reference count */
  void release()
  {
    if (m_reference != NULL)
    {
      // shared pointer that releases the last reference also deletes
      // the reference object
      if (! m_reference->decrement()) delete m_reference;
    }
    m_reference = NULL;
  }


  // --- pointer access methods ---
public:
  /** Pointer dereference operator*/
  T& operator*() const
  {
    return m_reference->operator*();
  }

  /** Pointer to structure dereference operator */
  T* operator->() const
  {
    return m_reference->operator->();
  }

  /** Pointer comparison operator */
  bool operator==(const SharedPointer& pointer) const
  {
    return pointer.m_reference == m_reference;
  }

  /** Pointer comparison operator */
  bool operator!=(const SharedPointer& pointer) const
  {
    return pointer.m_reference != m_reference;
  }

  /** Pointer comparison operator
      (special case to allow comparison with NULL) */
  bool operator==(T* pointer) const
  {
    if (m_reference == NULL)
    {
      // if shared pointer is not set, treat it as NULL
      return NULL == pointer;
    }
    else
    {
      return (*m_reference == pointer);
    }
  }

  /** Pointer comparison operator
      (special case to allow comparison with NULL) */
  bool operator!=(T* pointer) const
  {
    if (m_reference == NULL)
    {
      // if shared pointer is not set, treat it as NULL
      return NULL != pointer;
    }
    else
    {
      return (*m_reference != pointer);
    }
  }

  /** Simple "is null" test */
  bool is_null()
  {
    return (m_reference == NULL || (*m_reference) == NULL);
  }
};


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR_TEMPLATE(typename T, out, SharedPointer<T>, p)
{
  out << "SharedPointer(" << (*p) << ")";
  return out;
}


/*
#include "AllocTester.h"     // allocation/copy/deletion test class

void testSharedPointer()
{
  typedef SharedPointer<AllocTester> AllocTesterPtr;

  cout << "Testing shared pointer comparison with NULL" << endl;
  AllocTesterPtr p;
  cout << "p == NULL? "   << to_string(p == NULL) << endl;
  cout << "p != NULL? "   << to_string(p != NULL) << endl;
  cout << "Testing assignment to NULL" << endl;
  p = NULL;
  cout << "p == NULL? "   << to_string(p == NULL) << endl;
  cout << "p != NULL? "   << to_string(p != NULL) << endl;

  cout << "Creating raw pointers to A,B,C" << endl;
  AllocTester* a = new AllocTester("A");
  AllocTester* b = new AllocTester("B");
  AllocTester* c = new AllocTester("C");

  cout << "Adding shared pointers to A,B,C to vector" << endl;
  Vector<AllocTesterPtr> v;
  v.add(AllocTesterPtr(a));
  v.add(AllocTesterPtr(b));
  v.add(AllocTesterPtr(c));
 
  cout << "Displaying vector" << endl;
  cout << "v[0] = " << v[0] << endl;
  cout << "v[1] = " << v[1] << endl;
  cout << "v[2] = " << v[2] << endl;

  cout << "Testing comparison with null" << endl;
  cout << "v[0] == NULL? " << to_string(v[0] == NULL) << endl;
  cout << "v[0] != NULL? " << to_string(v[0] != NULL) << endl;

  cout << "Comparing pointers" << endl;
  cout << "v[0] == A? " << to_string(v[0] == a) << endl;
  cout << "v[0] == B? " << to_string(v[0] == b) << endl;
  cout << "v[1] == B? " << to_string(v[1] == b) << endl;
  cout << "v[1] == C? " << to_string(v[1] == c) << endl;
  cout << "v[2] == C? " << to_string(v[2] == c) << endl;

  cout << "Removing B" << endl;
  v.remove_at(1);

  cout << "Copying vector" << endl;
  Vector<AllocTesterPtr> v2;
  v2.assign(v.begin(), v.end());

  cout << "Removing A from v1" << endl;
  v.remove_at(0);
  
  cout << "Clearing array 1" << endl;
  v.clear();

  cout << "Removing A from v2" << endl;
  v2.remove_at(0);

  cout << "Clearing array 2" << endl;
  v2.clear();
}

*/

// multiple-inclusion guard
#endif
