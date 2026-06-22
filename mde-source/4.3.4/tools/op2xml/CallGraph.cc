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

// header file
#include "CallGraph.h"

#define INITIAL_CALLER_RESERVE_COUNT (1)
#define INITIAL_CALLEE_RESERVE_COUNT (2)

// --------------------------------------------------------------------------
// CallGraphNode
// --------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
CallGraphNode::CallGraphNode(const SampleLocation& sample_location)
  : m_sample_location(sample_location),
    m_callers(), m_callees(), m_self_counts()
{
  m_callers.reserve(INITIAL_CALLER_RESERVE_COUNT);
  m_callees.reserve(INITIAL_CALLEE_RESERVE_COUNT);
}


// --- accessors ---

/** Returns a string representation of this callgraph node */
const string CallGraphNode::to_string() const {
  std::stringstream ss;
  ss << "("
     << m_sample_location.to_string();
  FOR_EACH(const_iterator, it, EventCountMap, m_self_counts) {
    const string& event_name = it->first;
    unsigned long self_count = it->second;
    ss << ", " << event_name
       << "="  << self_count;
  }
  ss << ")";
  return ss.str();
}


// --- "self" count member functions ---

/** Clears "self" event counts on this node */
void CallGraphNode::clear_self_counts() {
  m_self_counts.clear();
}

/** Adds "self" sample count for specified event */
void CallGraphNode::add_self_count(const string& event_name,
                                   unsigned long count)
{
  unsigned long value = m_self_counts.get(event_name, 0);
  value += count;
  m_self_counts.put(event_name, value);
}

/** Removes "self" sample count for specified event */
void CallGraphNode::remove_self_count(const string& event_name,
                                      unsigned long count)
{
  unsigned long value = m_self_counts.get(event_name, 0);
  value = (count > value) ? 0 : value - count;
  if (value == 0) {
    m_self_counts.remove(event_name);
  }
  else {
    m_self_counts.put(event_name, value);
  }
}

/** Gets remaining "self" sample count for specified event. */
unsigned long CallGraphNode::get_self_count(const string& event_name) {
  return m_self_counts.get(event_name, 0);
}

/** Gets remaining "self" sample counts for all events. */
EventCountMap& CallGraphNode::get_self_counts() {
  return m_self_counts;
}

/** Returns true if this has non-zero "self" value for any event name. */
bool CallGraphNode::has_self_counts() {
  return m_self_counts.size() > 0;
}


// --- member functions ---

/** Adds a caller to this node's list of caller nodes */
CallGraphEdge* CallGraphNode::add_edge_to_caller(CallGraphNode* caller)
{
  // Create a new edge:
  CallGraphEdge* edge = new CallGraphEdge (caller, this);
  
  // Add the edge to the caller's "callee" list
  caller->m_callees.add(edge);

  // Add the edge to our "caller" list
  m_callers.add(edge);

  return edge;
}

/** Adds a callee to this node's list of callee nodes */
CallGraphEdge* CallGraphNode::add_edge_to_callee(CallGraphNode* callee)
{
  // Create a new edge:
  CallGraphEdge* edge = new CallGraphEdge (this, callee);

  // Add the edge to our "callee" list
  m_callees.add(edge);
  
  // Add the edge to the target's "callers" list
  callee->m_callers.add(edge);

  return edge;
}

/** Finds an edge to the given caller and returns it, or returns NULL
    if no such edge is found */
CallGraphEdge* CallGraphNode::get_edge_to_caller(CallGraphNode *caller)
{
  CallGraphEdge* result = NULL;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callers) {
    CallGraphEdge* edge = *it;
    if (edge->caller() == caller) {
      result = edge;
      break;
    }
  }
  return result;
}

/** Finds an edge to the given callee and returns it, or returns NULL
    if no such edge is found */
CallGraphEdge* CallGraphNode::get_edge_to_callee(CallGraphNode* callee)
{
  CallGraphEdge* result = NULL;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callees) {
    CallGraphEdge* edge = *it;
    if (edge->callee() == callee) {
      result = edge;
      break;
    }
  }
  return result;
}


/** Returns the number of "caller" edges with non-zero sample counts */
unsigned int CallGraphNode::non_zero_caller_count()
{
  unsigned int result = 0;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callers) {
    CallGraphEdge* edge = *it;
    if (edge->has_sample_counts()) {
      ++result;
    }
  }
  return result;
}

/** Returns the number of "callee" edges with non-zero sample counts */
unsigned int CallGraphNode::non_zero_callee_count()
{
  unsigned int result = 0;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callees) {
    CallGraphEdge* edge = *it;
    if (edge->has_sample_counts()) {
      ++result;
    }
  }
  return result;
}

/** Returns the total sample count over caller edges labelled with
    the given "event_name" */
unsigned long CallGraphNode::total_sample_count_over_caller_edges_with_event(
  const string& event_name)
{
  unsigned long result = 0;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callers) {
    CallGraphEdge* edge = *it;
    result += edge->get_sample_count(event_name);
  }
  return result;
}

/** Returns the total sample count over callee edges labelled with the
    given "event_name" */
unsigned long CallGraphNode::total_sample_count_over_callee_edges_with_event(
  const string& event_name)
{
  unsigned long result = 0;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callees) {
    CallGraphEdge* edge = *it;
    result += edge->get_sample_count(event_name);
  }
  return result;
}

/** Returns set of names of event counts on caller edges of this node */
void CallGraphNode::get_caller_edge_event_names(Set<string>& event_names)
{
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callers) {
    CallGraphEdge* edge = *it;
    FOR_EACH(iterator, it2, EventCountMap, edge->get_sample_counts()) {
      const string& event_name = it2->first;
      event_names.add(event_name);
    }
  }
}

/** Returns set of names of event counts on caller edges of this node */
void CallGraphNode::get_caller_edge_counts_for_event(
  const string& event_name, unsigned int& edge_count,
  unsigned long& total_sample_count)
{
  edge_count = 0;
  total_sample_count = 0;
  FOR_EACH(iterator, it, CallGraphEdgePtrVector, m_callers) {
    CallGraphEdge* edge = *it;
    unsigned long sample_count = edge->get_sample_count(event_name);
    total_sample_count += sample_count;
    if (sample_count > 0) ++edge_count;
  }
}


/** Removes all edges to all callers of this node */
void CallGraphNode::remove_edges_to_callers()
{
  int caller_count = m_callers.size();
  for (int i = caller_count - 1; i >= 0; i -= 1) {
    CallGraphEdge* edge = m_callers[i];

    // Remove the edge from the target's callees list
    edge->caller()->m_callees.remove(edge);

    // Remove the edge from our callers list
    m_callers.remove_at(i);

    delete edge;
  }
  
  // Reset the edge list to its default reserve size
  m_callers.reserve(INITIAL_CALLER_RESERVE_COUNT);
}

/** Removes all edges to all callees of this node */
void CallGraphNode::remove_edges_to_callees()
{
  int callee_count = m_callees.size();
  for (int i = callee_count - 1; i >= 0; i -= 1) {
    CallGraphEdge* edge = m_callees[i];

    // Remove the edge from the target's callers list
    edge->callee()->m_callers.remove(edge);

    // Remove the edge to the callee from our callees list
    m_callees.remove_at(i);

    delete edge;
  }
  
  // Reset the edge list to its default reserve size
  m_callees.reserve(INITIAL_CALLEE_RESERVE_COUNT);
}

/** Removes all edges to all callees of this node */
void CallGraphNode::remove_zero_edges_to_callees()
{
  int callee_count = m_callees.size();
  for (int i = callee_count - 1; i >= 0; i -= 1) {
    CallGraphEdge* edge = m_callees[i];
    if (! edge->has_sample_counts()) {
      // Remove the edge from the target's callers list
      edge->callee()->m_callers.remove(edge);

      // Remove the edge to the callee from our callees list
      m_callees.remove_at(i);

      delete edge;
    }
  }
  
  // Reset the edge list to its default reserve size
  m_callees.reserve(INITIAL_CALLEE_RESERVE_COUNT);
}


// --------------------------------------------------------------------------
// CallGraphEdge
// --------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
CallGraphEdge::CallGraphEdge(CallGraphNode* caller, CallGraphNode* callee)
  : m_caller(caller), m_callee(callee), m_sample_counts(), m_visited(0)
{ }


// --- accessors ---

/** Returns a string representation of this callgraph node */
const string CallGraphEdge::to_string() const {
  std::stringstream ss;
  ss << "("
     << m_caller->to_string()
     << " -> "
     << m_callee->to_string();
  FOR_EACH(const_iterator, it, EventCountMap, m_sample_counts) {
    const string& event_name   = it->first;
    unsigned long sample_count = it->second;
    ss << ", " << event_name
       << "="  << sample_count;
  }
  ss << ")";
  return ss.str();
}


// --- sample count member functions ---

/** Clears sample counts on this edge */
void CallGraphEdge::clear_sample_counts() {
  m_sample_counts.clear();
}

/** Sets sample count on this edge for specified event */
void CallGraphEdge::set_sample_count(const string& event_name, 
                                     unsigned long count)
{
  if (count <= 0) {
    m_sample_counts.remove(event_name);
  }
  else {
    m_sample_counts.put(event_name, count);
  }
}

/** Adds sample count to this edge for specified event */
void CallGraphEdge::add_sample_count(const string& event_name,
                                     unsigned long count)
{
  unsigned long value = m_sample_counts.get(event_name, 0);
  value += count;
  set_sample_count(event_name, value);
}

/** Removes sample count from this edge for specified event */
void CallGraphEdge::remove_sample_count(const string& event_name,
                                        unsigned long count)
{
  unsigned long value = m_sample_counts.get(event_name, 0);
  value = (count > value) ? 0 : value - count;
  set_sample_count(event_name, value);
}

/** Gets remaining "sample" sample count for specified event. */
unsigned long CallGraphEdge::get_sample_count(const string& event_name)
{
  return m_sample_counts.get(event_name, 0);
}

/** Gets remaining "sample" sample counts for all events. */
EventCountMap& CallGraphEdge::get_sample_counts() {
  return m_sample_counts;
}

/** Returns true if this has non-zero "sample" value for any event name. */
bool CallGraphEdge::has_sample_counts() {
  return m_sample_counts.size() > 0;
}
