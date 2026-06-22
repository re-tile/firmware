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
// Frame -- Data needed to write an output <frame> node and its children
// ==========================================================================

// multiple-inclusion guard
#ifndef FRAME_H
#define FRAME_H

// custom includes
#include "CallGraph.h"
#include "Vector.h"
#include "foreach.h"


// --------------------------------------------------------------------------
// Frame
// --------------------------------------------------------------------------

/**
 * Data needed to write an output <frame> node and its children
 */
class Frame {

  // --- members ---
 private:
  /** The parent frame of this frame */
  Frame* m_parent;

  /** The CallGraphNode associated with this Frame */
  CallGraphNode* m_call_graph_node;

  /** The child Frames of this Frame */
  Vector<Frame*> m_children;
  
  /** Depth of this frame */
  int m_depth;

  /** The "self" counts of OProfile events involving this Frame */
  EventCountMap m_self_counts;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  Frame(CallGraphNode* node, Frame* parent = NULL);

  /** Destructor, which destroys this Frame and all its children */
  ~Frame();


  // --- accessor functions ---
 public:
  /** Returns this frame's call graph node */
  CallGraphNode* call_graph_node() { return m_call_graph_node; }

  /** Returns this frame's Vector of children */
  Vector<Frame*>& children() { return m_children; }

  /** Returns frame's stack depth */
  int depth() { return m_depth; }


  // --- to_string() function ---
 public:
  /** Returns a string representation of this frame */
  const string to_string() const;


  // --- "self" count member functions ---
 public:
  /** Clears "self" event counts on this node */
  void clear_self_counts();

  /** Adds "self" sample count for specified event */
  void add_self_count(const string& event_name, unsigned long self_count);

  /** Sets "self" sample count for specified event */
  void set_self_count(const string& event_name, unsigned long self_count);

  /** Removes "self" sample count for specified event */
  void remove_self_count(const string& event_name, unsigned long self_count);

  /** Gets total "self" sample count for specified event.
   *  Returns 0 if no samples for the event have been added.
   */
  unsigned long get_self_count(const string& event_name);

  /** Returns true if this node has a non-zero "self" sample count
      for any event name. */
  bool has_self_counts();


  // --- call-graph member functions ---
 public:
  /**
   * Walks the call graph, starting at the node used when this frame 
   * was constructed, and builds this frame's child frames,
   * while also determining event names and "self" sample counts.
   * The numerator/denominator ratio is a proportionality value used to scale
   * samples that are under node(s) with more than one caller.
   */
  void build_call_tree();

};

// multiple-inclusion guard
#endif
