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
// CallGraphNodeFactory -- Finds/constructs nodes in a call-graph
// ==========================================================================

// header file
#include "CallGraphNodeFactory.h"


// --------------------------------------------------------------------------
// CallGraphNodeFactory
// --------------------------------------------------------------------------

// --- member functions ---

/**
 * Finds an existing CallGraphNode with the given
 * SampleLocation's "to_string()" representation,
 * or creates a new one if necessary.
 */
CallGraphNode* CallGraphNodeFactory::findOrCreateNodeFor(
  const SampleLocation& sample_location)
{
  string location_string = sample_location.to_string();
  CallGraphNode* result = m_node_map.get (location_string, NULL);
  if (result == NULL) {
    result = new CallGraphNode (sample_location);
    m_node_map.add (location_string, result);
  }
  return result;
}
