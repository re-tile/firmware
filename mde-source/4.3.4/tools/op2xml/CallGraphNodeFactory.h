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

// multiple-inclusion guard
#ifndef CALL_GRAPH_NODE_FACTORY_H
#define CALL_GRAPH_NODE_FACTORY_H

// custom includes
#include "string_utils.h"
#include "CallGraph.h"
#include "Map.h"


// --------------------------------------------------------------------------
// CallGraphNodeFactory
// --------------------------------------------------------------------------

/**
 * A factory which maps the "to_string()" representation of
 * "SampleLocation"s to unique "CallGraphNode"s which correspond
 * to locations with that string representation.
 */
class CallGraphNodeFactory {

  // --- members ---
 private:
 
  /** Maps SampleLocation strings to CallGraphNodes */
  typedef Map<string, CallGraphNode*> NodeMap;

  /** Maps SampleLocation strings to CallGraphNodes */
  NodeMap m_node_map;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  CallGraphNodeFactory ()
    : m_node_map (NodeMap())
  { }


  // --- member functions ---
 public:
  /**
   * Finds an existing CallGraphNode with the given sample location string,
   * or if one isn't found, creates a new one.
   */
  CallGraphNode* findOrCreateNodeFor (const SampleLocation& sample_location);
};

// multiple-inclusion guard
#endif
