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

// header file
#include "Frame.h"

// system headers
#include <math.h>

// application includes
#include "global_options.h"      // global flag variables


// --------------------------------------------------------------------------
// Frame
// --------------------------------------------------------------------------


// --- constructors/destructors ---

/** Constructor */
Frame::Frame(CallGraphNode* node, Frame* parent)
  : m_parent(parent), m_call_graph_node(node), m_children(),
    m_depth(0), m_self_counts()
{
  if (m_parent != NULL) {
    m_parent->m_children.add(this);
    m_depth = m_parent->m_depth + 1;
  }
}

/** Destroys this Frame and all its children */
Frame::~Frame()
{
  m_call_graph_node = NULL;

  // "delete" each child since each is allocated from the heap using "new":
  int count = m_children.size();
  for (int i=0; i<count; ++i) {
    delete m_children[i];
  }
  m_children.clear();

  m_self_counts.clear();
}


// --- to_string() function ---

/** Returns a string representation of this frame */
const string Frame::to_string() const {
  std::stringstream ss;
  for (int i=0; i<m_depth; i++) ss << " ";
  ss << "Frame[";

  ss << m_call_graph_node->to_string();

  ss << ", ";

  ss << "events(";
  FOR_EACH(const_iterator, it, EventCountMap, m_self_counts) {
    const string& event_name   = it->first;
    unsigned long self_count = it->second;
    ss << " " << event_name << "=" << self_count;
  }
  ss << " )";

  ss << "]";
  return ss.str();
}


// --- "self" count member functions ---

/** Clears "self" event counts on this node */
void Frame::clear_self_counts() { m_self_counts.clear(); }

/** Adds "self" sample count for specified event */
void Frame::add_self_count(const string& event_name, 
                           unsigned long self_count)
{
  unsigned long value = m_self_counts.get(event_name, 0);
  value += self_count;
  m_self_counts.put(event_name, value);
}

/** Sets "self" sample count for specified event */
void Frame::set_self_count(const string& event_name, 
                           unsigned long self_count)
{
  m_self_counts.put(event_name, self_count);
}

/** Removes "self" sample count for specified event */
void Frame::remove_self_count(const string& event_name,
                              unsigned long self_count)
{
  unsigned long value = m_self_counts.get(event_name, 0);
  value = (self_count > value) ? 0 : value - self_count;
  if (value <= 0)
    m_self_counts.remove(event_name);
  else
    m_self_counts.put(event_name, value);
}

/** Gets total "self" sample count for specified event.
 *  Returns 0 if no samples for the event have been added.
 */
unsigned long Frame::get_self_count(const string& event_name) {
  unsigned long value = m_self_counts.get(event_name, 0);
  return value;
}

/** Returns true if this node has a non-zero "self" sample count
    for any event name. */
bool Frame::has_self_counts() { return m_self_counts.size() > 0; }


// --- member functions ---

/**
  A brief overview of the thinking behind the algorithm used below
  and in OutputWriter.write_frame_element(), which calls this method:

  OProfile gathers data by performing sample interrupts in response to
  hardware events. Each sample interrupt essentially notes the current
  program location, and then traverses the current call stack for at most
  a certain number of frames (defined by the --call-graph opcontrol option)
  to capture "from->to" relationships between the callers of the current
  function.

  This data is stored as a non-callgraph "self" value for each sampled
  location and callgraph ({cg}) sample counts for the from/to pairs.

  When tile-op2xml loads this data for a given process, we represent it
  as a call graph, consisting of:
  - nodes that represent program locations (essentially,
    source file/line numbers) and the "self" value for that location
    for each event name
  - edges that represent traversals from a "caller" location
    to a "callee" location, with callgraph sample values for
    each event name

  What we need to do is reconstruct the original call tree of the
  process, so we can generate corresponding call-tree <frame> nodes
  in the output profile.xml file, each with the self count for the
  frame for each event.

  We can't expect to do this perfectly, because the OProfile data is
  potentially gappy: the --call-graph depth may be small or zero,
  the sampling interval may be too high to provide complete
  coverage of the call graph, and rarely traveled paths may
  simply not be sampled at all. The goal is to do the best job
  that we can given the potential gappiness of the data.

  We assume that overlapping of separate call stack walks provides
  sufficient coverage to be able to reconstruct a usable call tree.
  If not, then we should try to lose gracefully, producing a bunch
  of limited call tree "fragments". In the worst case, where the user
  runs OProfile with --call-graph 0, there is no call graph edge data,
  and we basically generate a flat list of "self" counts for individual
  program locations. This can still be aggregated to obtain 
  per-process and per-function totals, but will lack call-tree info.

  We assume that the program's call tree starts from one or more "root"
  locations (such as _start, main, or the handler functions of
  interrupts), and assume that we can reconstruct call trees by
  starting from root nodes and walking the caller/callee relationships
  that were captured by OProfile.

  In generating trees from the call graph data, we'll need to consider
  and handle a number of potential cases, illustrated below:

  1) the easiest case: call graph is already a
     singly-rooted tree with no fork-joins:

                (1) 
               /   \
              a     b
             v       v
           (2)       (3)
           / \       / \
          c   d     e   f
         v     v   v     v
       (4)    (5) (6)    (7)

     We "discover" root node (1) by the fact it has no caller edges.

     We create one profile frame per node. The frame just copies the
     "self" counts for all events found on the node. Edge counts are
     essentially ignored, as we merely use the edges to discover
     the call tree structure.


  2) Slightly more complicated is a singly-rooted tree
     that has fork-joins:

                (1)
               /   \
              a     b
             v       v
           (2)       (3)
           / \       / \
          c   d     e   f
         v     v   v     v
       (4)      (5)      (6)
                 |
                 g
                 |
                (7)

     As above, we discover root node (1) because it has no callers.

     We process the tree as we did above, but here some nodes may be
     traversed more than once. We need to divide the "self" values for
     each event on such nodes in proportion to the callgraph value(s)
     for that even on the incoming caller edges.

     For example, for node (5), we need to divvy its "self" values
     between when it's called from (2) and when it's called from (3),
     based on the relative values found on the edges "d" and "e".

     One might think that we would then need to do the same for node (7)
     and for any other children of node (5), that is, carry the
     same relative proportion down the tree while capturing self values
     for these nodes. This appears not to be the case, however. 
     We're able to obtain values from tile-op2xml that approximate the
     results of tile-opreport by the simpler method of just dividing
     a given node's values based solely on its caller edges.

     Remember that counts for multiple event names are stored on
     each node and each edge; we need to do the divvying of "self"
     counts independently for each event, based on the event(s)
     reported on the caller edges. 

     Events may not appear on all incoming edges; for a given event
     there might be only one incoming edge or many with a sample count.
     We need to consider the actual set of edges with counts for
     each event separately, and handle them accordingly.

     Once we figure out how much of the "self" count to consume
     from each node, we decrement that from the value on the node
     to ensure that we do not "reuse" self counts. We also clear
     the event counts from edges as we process them, so that these
     will not be reused either. This has the benefit of simplifying
     the calculations for later passes: the second time we encounter
     the node (5) in the above graph, we can treat it as if it's a
     single-caller node: we can just consume all the remaining
     self counts on it and its callee nodes.


   3) The next step in complexity is a muliply-rooted tree:

                (1)      (7)      (10)
               /   \     / \
              a     b   g   h
             v       v v     v
           (2)       (3)     (8)
           / \       / \       \
          c   d     e   f       i
         v     v   v     v       v
       (4)      (5)      (6)     (9)
                 |
                 j
                 |
                (11)

     Here we discover root nodes (1), (7), and also (10),
     which has no callers but may have "self" counts,
     so we treat it as a "degenerate" root node.

     We process each root node in turn, as above.
     After we've processed all the children of root node (1),
     we need to process the children of root node (7).
     This gives us a forest of call-tree frames, one for each
     of the root nodes. (If we're processing a profile directory
     with no callgraph data, then _all_ the nodes will be like (10),
     and we'll generate a profile that's basically a forest
     of single leaves.)

     A problem is what to do about "overlapping" portions of call trees.
     Consider nodes (5), (6), and (11) -- what part of their self counts
     belongs to the call tree rooted at (1), and what part belongs to
     the call tree rooted at (7)? We treat this as we do with fork-joins:
     we simply apportion samples based on the relative weight of
     the node's callers. This appears to produce results comparable with
     the output of tile-opreport.

     If there's a lot of overlap, we may not be assigning "self" values to
     the correct call-tree frames, but we choose to accept that as a
     hazard.  The aggregate process/function counts should still be
     reasonable, based on how we consume the sample values on the
     nodes and edges.

     Again, we decrement node/edge counts as we use them,
     so that when we've processed root (1) and move on to
     root (7), it's as if (7) was always a singly-rooted tree.


  4) Finally, consider a muliply-rooted tree with fork-joins
     that has cycles in the graph:

               (1)      (7)     (11)    (13)
              /   \     / ^     ^  \
             a     b   g   h   l    m
            v       v v     \ /      v
          (2)       (3)     (8)     (12)
          / \       ^ \       ^      /
         c   d     e   f       i    n
        v     v   /     v       \  v
      (4)      (5)      (6)     (9)
                 ^      /
                  k    j
                   \  v
                   (10)

     This is possible because of the overlapping of call trees
     from different root nodes, and/or recursion in the program itself.

     In this case, we discover the root nodes (1) and (13),
     and process each in turn.

     We deal with cycles in the graph by incrementing a "visited" count
     on each edge as we walk the graph, and by ignoring edges that are
     marked as already visited.

     Once we're done processing all the root nodes we can, for
     efficiency's sake we go through and remove any visited edges
     that have zero samples, i.e. everything in the graph we know
     that we've already walked.

     After processing all the root nodes we can, it may be that
     some nodes with samples and/or unvisited edges are left over.
     In these, there will either be one or more root nodes, having
     no callers, or else all nodes/edges will form one or more cycles.

     If we can find any roots, we process these as above, and then
     again look for roots or cycles.

     If we're left with only cycles, then we pick an arbitrary node
     that has at least one callee edge, and treat it as a root node,
     consuming as much of the cycle it's in, and any nodes
     touched by it, as we can. Then we look at the remaining graph,
     if any, for roots or a cycle again.

     Ultimately, we must use up the entire graph this way.
     We may not get the original call tree(s) this way,
     but we'll at least capture samples for all frames _somewhere_
     in the resulting profile data, so while the call-tree display
     may be somewhat screwy, the aggregate per-function counts
     should be reasonably correct.

     Again, if there's a lot of overlap and/or an abundance of cycles,
     this algorithm can potentially assign samples to the wrong frames.
     We choose to accept this, because there really isn't much else
     we can do without a lot more hair, and the current strategy
     provides a reasonable match with the output of tile-opreport.
*/

/**
 * Walk the call graph, starting at the node used when this frame
 * was constructed, and build this frame's child frames,
 * while also determining event names and sample counts.
 * Decrements edge/self values that it uses, so we know what's left.
 */
void Frame::build_call_tree()
{
  // get the parent node and edge (if any)
  CallGraphNode* parent_node = (m_parent == NULL) ? NULL :
    m_parent->m_call_graph_node;
  CallGraphEdge* parent_edge = (m_parent == NULL) ? NULL :
    m_call_graph_node->get_edge_to_caller(parent_node);

  // mapping from event names to scaling proportions, if any
  Map<string, double> frame_event_proportions;

  // if there's zero or one caller edges, we don't care
  // about the edge sample value(s), if any;
  // we can just consume all the "self" counts we find

  // if there ARE two or more caller edges, however...
  unsigned int caller_count = m_call_graph_node->caller_count();
  if (caller_count > 1) {

    // collect the event names that appear on any caller edge,
    // so we know what events to map over
    Set<string> event_names;
    m_call_graph_node->get_caller_edge_event_names(event_names);

    // for each event name on multiple edges
    FOR_EACH(const_iterator, it, Set<string>, event_names) {
      const string& event_name = (*it);

      // get the number of edges it appears on, and total sample count
      unsigned int edge_count;
      unsigned long total_sample_count;
      m_call_graph_node->get_caller_edge_counts_for_event(
        event_name, edge_count, total_sample_count);

      // get the sample count for the event on the parent edge
      unsigned long parent_sample_count =
        (parent_edge == NULL) ? 0 :
          parent_edge->get_sample_count(event_name);

      // we only care if more than one edge has a sample count,
      // and the parent edge's count is non-zero
      if (edge_count > 1 &&
          parent_sample_count > 0 &&
          total_sample_count >= parent_sample_count)
      {
        // construct ratio
        double frame_event_ratio =
          parent_sample_count * 1.0 / total_sample_count;

        // store the proportion for use by this frame
        frame_event_proportions.put(event_name, frame_event_ratio);
      }
    }
  }

  // if we have a parent edge
  if (parent_edge != NULL) {
    // "Consume" the event sample counts on the parent edge,
    // since we no longer need them
    parent_edge->clear_sample_counts();

    // Increment parent edge's "visited" count
    parent_edge->visit();
  }

  // Now we can forget about the caller edges,
  // and just use the event proportions map to determine
  // the samples to grab from this node

  // get copy of this node's "self" counts for events
  // need a copy so we can iterate over it
  // while updating values in the original map
  EventCountMap event_counts_copy = m_call_graph_node->get_self_counts();

  // for each "self" event name:
  FOR_EACH(iterator, it, EventCountMap, event_counts_copy) {
    const string& event_name           = it->first;
    unsigned long remaining_self_count = it->second;
    unsigned long frame_self_count = 0;

    // if there's no entry for it in the event proportions map
    if (! frame_event_proportions.contains(event_name)) {
      // this frame consumes the entire self count for the event
      frame_self_count = remaining_self_count;
    }
    else {
      // this frame consumes a proportional amount of the self count
      double frame_event_ratio =
        frame_event_proportions.get(event_name, 1.0);
      frame_self_count =
        (unsigned int) (frame_event_ratio * remaining_self_count);
    }

    if (frame_self_count > 0) {
      // store the frame's self count for this event
      set_self_count(event_name, frame_self_count);

      // "Consume" the frame's self count from the call graph node
      m_call_graph_node->remove_self_count(event_name, frame_self_count);
    }
  }

  if (g_show_frame_generation) {
    cout << this->to_string() << endl;
  }

  // don't look at children if we reach the
  // maximum allowed recursion depth
  if (g_max_stack_depth == 0 || m_depth < g_max_stack_depth) {

    // Okay, now we need to iterate over the callee edges
    // of this node to construct frames.
    FOR_EACH(iterator, it, CallGraphEdgePtrVector,
             m_call_graph_node->callee_edges())
    {
      CallGraphEdge* edge = *it;
      CallGraphNode* callee_node = edge->callee();

      // to avoid infinite loops due to cycles, we skip edges
      // we've already visited
      unsigned int visited = edge->get_visited();
      if (visited > 0) continue;

      // construct the call tree for the child frame,
      // passing it any event proportions we received or created
      Frame* child_frame = new Frame(callee_node, this);
      child_frame->build_call_tree();
    }
  }
}
