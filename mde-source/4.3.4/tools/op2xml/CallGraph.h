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
// CallGraph -- Represents nodes and edges in a call-graph
// ==========================================================================

// multiple-inclusion guard
#ifndef CALL_GRAPH_H
#define CALL_GRAPH_H

// custom includes
#include "string_utils.h"
#include "SampleLocation.h"
#include "Vector.h"
#include "Map.h"
#include "Set.h"
#include "foreach.h"


// -------------------------------------------------------------------------
// definitions
// -------------------------------------------------------------------------

// forward defs
class CallGraphNode;
class CallGraphEdge;

/** Vector of CallGraphNode pointers */
typedef Vector<CallGraphNode*> CallGraphNodePtrVector;

/** Vector of CallGraphEdge pointers */
typedef Vector<CallGraphEdge*> CallGraphEdgePtrVector;

/** Map for storing "self" values for events on a node */
typedef Map<string, unsigned long> EventCountMap;


// --------------------------------------------------------------------------
// CallGraphNode
// --------------------------------------------------------------------------

/**
 * Represents a node in a call-graph
 */
class CallGraphNode {

  // --- members ---
 private:

  friend class CallGraphNodeFactory;

  /** Represents the code location for this node */
  const SampleLocation m_sample_location;

  /** All of the "caller" call graph nodes which call into this node */
  CallGraphEdgePtrVector m_callers;

  /** All of the "callee" call graph nodes which are called by this node */
  CallGraphEdgePtrVector m_callees;

  /** "self" sample count for event(s) stored on this node */
  EventCountMap m_self_counts;


  // --- constructors/destructors ---
 private:
  /** Constructor */
  CallGraphNode(const SampleLocation& sample_location);


  // --- accessors ---
 public:
  /** Returns a string representation of this callgraph node */
  const string to_string() const;

  /** Returns this nodes SampleLocation */
  const SampleLocation& sample_location() { return m_sample_location; }


  // --- "self" count member functions ---
 public:
  /** Clears "self" event counts on this node */
  void clear_self_counts();

  /** Adds "self" sample count for specified event */
  void add_self_count(const string& event_name, unsigned long count);

  /** Removes "self" sample count for specified event */
  void remove_self_count(const string& event_name, unsigned long count);
  
  /** Gets remaining "self" sample count for specified event. */
  unsigned long get_self_count(const string& event_name);

  /** Gets remaining "self" sample counts for all events. */
  EventCountMap& get_self_counts();

  /** Returns true if this has non-zero "self" value for any event name. */
  bool has_self_counts();


  // --- edge member functions ---
 public:

  /** Returns this node's caller edges */
  CallGraphEdgePtrVector& caller_edges() { return m_callers; }

  /** Returns this node's callee edges */
  CallGraphEdgePtrVector& callee_edges() { return m_callees; }


  /** Returns the number of "caller" edges for this node */
  unsigned int caller_count() const { return m_callers.size(); }

  /** Returns the number of "callee" edges for this node */
  unsigned int callee_count() const { return m_callees.size(); }


  /** Adds an edge to the given caller */
  CallGraphEdge* add_edge_to_caller(CallGraphNode *callee);

  /** Adds an edge to the given callee */
  CallGraphEdge* add_edge_to_callee(CallGraphNode *callee);

  /** Finds an edge to the given caller and returns it,
      or returns NULL if no such edge is found */
  CallGraphEdge* get_edge_to_caller(CallGraphNode *caller);

  /** Finds an edge to the given callee and returns it,
      or returns NULL if no such edge is found */
  CallGraphEdge* get_edge_to_callee(CallGraphNode *callee);


  /** Returns the number of "caller" edges with non-zero sample counts */
  unsigned int non_zero_caller_count();

  /** Returns the number of "callee" edges with non-zero sample counts */
  unsigned int non_zero_callee_count();


  /** Returns the total sample count over caller edges labelled
      with the given "event_name" */
  unsigned long total_sample_count_over_caller_edges_with_event(
    const string& event_name);

  /** Returns the total sample count over callee edges labelled
      with the given "event_name" */
  unsigned long total_sample_count_over_callee_edges_with_event(
    const string& event_name);


  /** Returns set of names of event counts on caller edges of this node */
  void get_caller_edge_event_names(Set<string>& event_names);

  /** Returns set of names of event counts on caller edges of this node */
  void get_caller_edge_counts_for_event(const string& event_name,
                                        unsigned int& edge_count,
                                        unsigned long& sample_total);

  /** Removes all edges to callers */
  void remove_edges_to_callers();

  /** Removes all edges to callees */
  void remove_edges_to_callees();

  /** Removes all edges to callees which have no event counts */
  void remove_zero_edges_to_callees();
};


// --------------------------------------------------------------------------
// CallGraphEdge
// --------------------------------------------------------------------------

/**
 * Represents an edge in a call-graph.
 * Each edge connects a caller CallGraphNode to a callee node.
 */
class CallGraphEdge {

  // --- members ---
 private:
  /** The calling node */
  CallGraphNode* m_caller;

  /** The called node */
  CallGraphNode* m_callee;

  /** sample "edge" counts for event(s) stored on this edge */
  EventCountMap m_sample_counts;

  /** "visited" count used while walking the graph */
  unsigned int m_visited;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  CallGraphEdge (CallGraphNode* caller, CallGraphNode* callee);


  // --- accessors ---
 public:
  /** Returns a string representation of this callgraph node */
  const string to_string() const;


  // --- "visited" count members functions ---
 public:
  /** Gets visited count */
  unsigned int get_visited() {
    return m_visited;
  }

  /** Increments visited count */
  void visit() {
    ++m_visited;
  }

  /** Decrements visited count */
  void unvisit() {
    if (m_visited > 0) --m_visited;
  }

  /** Clears visited count */
  void clear_visited() {
    m_visited = 0;
  }


  // --- edge member functions ---
 public:
  /** Returns this edge's caller node */
  CallGraphNode* caller() const { return m_caller; }

  /** Returns this edge's callee node */
  CallGraphNode* callee() const { return m_callee; }


  // --- sample count member functions ---
 public:
  /** Clears sample counts on this edge */
  void clear_sample_counts();

  /** Sets sample count on this edge for specified event */
  void set_sample_count(const string& event_name, unsigned long count);

  /** Adds sample count to this edge for specified event */
  void add_sample_count(const string& event_name, unsigned long count);

  /** Removes sample count from this edge for specified event */
  void remove_sample_count(const string& event_name, unsigned long count);
  
  /** Gets remaining "sample" sample count for specified event. */
  unsigned long get_sample_count(const string& event_name);

  /** Gets remaining "sample" sample counts for all events. */
  EventCountMap& get_sample_counts();

  /** Returns true if this has non-zero "sample" value for any event name. */
  bool has_sample_counts();

};

// multiple-inclusion guard
#endif
