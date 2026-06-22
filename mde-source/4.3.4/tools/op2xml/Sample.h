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
// Sample.h -- OProfile sample
// ==========================================================================

// multiple-inclusion guard
#ifndef SAMPLE_H
#define SAMPLE_H

// custom includes
#include "SampleLocation.h"
#include "SymbolFile.h"
#include "CallGraph.h"
#include "CallGraphNodeFactory.h"

// OProfile includes
#include <op_bfd.h>             // op_bfd, bfd_vma, etc.
#include <odb.h>                // odb_open(), odb_value, etc.


// --------------------------------------------------------------------------
// Sample
// --------------------------------------------------------------------------

/**
 * A Sample represents a callstack-style OProfile sample,
 * which includes a pair of SampleLocations, a sample key and
 * accumulated value.
 * The pair of SampleLocations includes the "from" (caller) location,
 * and "to" (callee) location.
 */
class Sample {

  // --- members ---
 private:
  static CallGraphNodeFactory s_call_graph_node_factory;

  /** sample value (i.e. event count for this sample) */
  odb_value_t m_value;

  /** whether this is a callgraph sample
      (i.e. both "from" and "to" are defined) */
  bool m_is_callgraph_sample;

  /** "from" CallGraphNode */
  CallGraphNode* m_from_call_graph_node;

  /** "to" CallGraphNode */
  CallGraphNode* m_to_call_graph_node;

  /** hashmap key */
  string m_hash_key;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  Sample (
    const odb_key_t& key, const odb_value_t& value, bool is_callgraph_sample,
    const SymbolFilePtr& from_symbol_file,
    const bool from_kernel, const bfd_vma& from_start_offset,
    const SymbolFilePtr& to_symbol_file,  
    const bool to_kernel, const bfd_vma& to_start_offset
  );


  // --- accessors ---
 public:

  /** Sample value. */
  odb_value_t value() const { return m_value; }

  /** Increments this sample's value by the given amount */
  void increment_value_by (odb_value_t inc) { m_value += inc; }

  /** Gets whether this is a callgraph sample */
  bool is_callgraph_sample() { return m_is_callgraph_sample; }

  /** Returns the "from" CallGraphNode */
  CallGraphNode* from_call_graph_node() const { 
    return m_from_call_graph_node; }

  /** Returns the "from" SampleLocation. */
  const SampleLocation& from_location() const {
    return m_from_call_graph_node->sample_location(); }

  /** Returns the "to" CallGraphNode */
  CallGraphNode* to_call_graph_node() const {
    return m_to_call_graph_node; }

  /** Returns the "to" SampleLocation. */
  const SampleLocation& to_location() const {
    return m_to_call_graph_node->sample_location(); }

  /** Returns hashmap key for this sample. */
  const string& get_hash_key();
};

// multiple-inclusion guard
#endif
