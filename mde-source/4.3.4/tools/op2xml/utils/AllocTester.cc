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
// AllocTester.cc -- Allocation/copy/destruction test class
// ==========================================================================

// header file
#include "AllocTester.h"


// --------------------------------------------------------------------------
// AllocTester
// --------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
AllocTester::AllocTester(string ID) : ID(ID), m_destructed(false) {
  cerr << to_string() << " constructed" << endl;
}

/** Copy constructor */
AllocTester::AllocTester(AllocTester& original) :
  ID(original.ID + ".c"), m_destructed(false)
{
  cerr << to_string() << " copy-constructed" << endl;
}

/** Assignment operator */
const AllocTester& AllocTester::operator=(AllocTester& original) {
  ID = original.ID + ".a";
  m_destructed = false;
  cerr << to_string() << " assigned" << endl;
  return *this;
}

/** Destructor */
AllocTester::~AllocTester() {
  if (! m_destructed)
    cerr << to_string() << " destroyed" << endl;
  else
    cerr << "Warning -- AllocTester instance destructed more than once."
         << endl;
  m_destructed = true;
}

// --- accessors ---

const string AllocTester::to_string() const {
  return "AllocTester[" + ID + "]";
}

