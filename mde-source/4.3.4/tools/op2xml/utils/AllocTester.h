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
// AllocTester.h -- Allocation/copy/destruction test class
// ==========================================================================

// multiple-inclusion guard
#ifndef ALLOCTESTER_H
#define ALLOCTESTER_H

// custom includes
#include "io.h"            // IO streams
#include "string_utils.h"  // C/C++ strings


// --------------------------------------------------------------------------
// AllocTester
// --------------------------------------------------------------------------

/** A simple debugging class useful for checking for
 *  proper construction/deletion of shared_ptr objects.
 */
struct AllocTester
{
  // --- members ---

  /** ID of this instance */
  string ID;

  /** Whether this instance has been destroyed yet */
  bool m_destructed;


  // --- constructors/destructors ---

  /** Constructor */
  AllocTester(string ID);

  /** Copy constructor */
  AllocTester(AllocTester& original);

  /** Assignment operator */
  const AllocTester& operator=(AllocTester& original);

  /** Destructor */
  ~AllocTester();


  // --- accessors ---

  const string to_string() const;


};


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR(out, AllocTester, a)
{
  out << a.to_string();
  return out;
}


/*
Example:

#include <vector>

int main(int argc, char **argv)
{
  typedef SafePointer<AllocTester> AllocTesterPtr;

  vector<AllocTesterPtr> v;

  cout << "Adding items" << endl;
  v.push_back(AllocTesterPtr(new AllocTester("A")));
  v.push_back(AllocTesterPtr(new AllocTester("B")));
  v.push_back(AllocTesterPtr(new AllocTester("C")));

  vector<AllocTesterPtr> v2;

  cout << "Removing B" << endl;
  v.remove_at(1);

  cout << "Copying vector" << endl;
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
