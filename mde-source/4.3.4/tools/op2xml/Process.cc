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
// Process -- Represents a binary: executable, dll, etc.
// ==========================================================================

// header file
#include "Process.h"

// custom includes
#include "Map.h"
#include "foreach.h"

// typedefs

// Maps from "exe_path:PID" key strings to Process pointers
typedef Map <string, Process*> ProcessMap;  


// --------------------------------------------------------------------------
// Process
// --------------------------------------------------------------------------


// --- static members ---

int Process::s_next_id = 1;


// --- member functions ---

/*
  To construct the callgraph for a given process, we need to
  iterate over the SampleFiles we collected for this process
  and construct callgraph by storing self values and
  creating call graph edges.

  The sample files will include files for non-cg (self) samples,
  and may include files for callgraph (from-to) samples

  The non-cg files record samples taken by OProfile at a given
  location (function/line) and the cg files from OProfile walking the
  stack upwards from that location to record who called it and how
  often. The latter are only included if the user specified
  "--callgraph=n" in OProfile's options (with a non-zero n, obviously)

  The non-cg samples basically give us "self" counts for each sampled
  location.  These are stored as a value on the callee node, under the
  sample's event name.  Hence, there can be more than one such "self"
  value for a given node, one for each event.  (Nodes which have no
  samples for a given event report a 0 "self" value for that event.)

  The cg samples give us call-tree structure, and also indicate roughly
  how the "self" samples came from each location's caller(s).
  These are stored as edge between different nodes, with the event name
  and callgraph value on each edge. Again, there can be more than one edge
  between any "from" and "to" pair of nodes, one for each event name.

  This representation of callgraph information is far from perfect.
  Callgraph backtracing is bounded by the --callgraph=n limit, and a
  given node might happen to be the nth one in a backtrace, in which
  case we record who it called, but not who called it.  One should not
  expect the callgraph edge values to add up to the self value for a
  node. At best, when present they can be used as a heuristic for
  apportioning the "self" value amongst multiple callers for a given
  function.

  We'll basically have to try to "extract" one or more call tree(s)
  from the resulting hairball of "from"/"to" calls, and hope that
  these represent something reasonably close to the actual program
  call structure.  For simple, tree-like programs this is likely the
  case; for complex, heavily-recursive code it likely will not be.
  Also, if the user has given us little or no callgraph information,
  it may not even be possible; the goal is to do the best we can.

  This is discussed in more detail in a lengthy comment in Frame.cc.
*/

/** Adds self values and call graph edges for this process */
void Process::construct_call_graph() const
{
  // Iterate over the sample files for this process
  FOR_EACH(const_iterator, it, SampleFilePtrVector, m_sample_files) {
    const SampleFile* sample_file_ptr = *it;
    const string& event_name = sample_file_ptr->event_name();
    bool is_callgraph = sample_file_ptr->is_callgraph_file();

    // Iterate over the Samples in each file:
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    FOR_EACH(const_iterator, it2, SampleFile::SampleList, samples) {
      const Sample& sample = *it2;
      CallGraphNode* caller_node = sample.from_call_graph_node();
      CallGraphNode* callee_node = sample.to_call_graph_node();
      odb_value_t value = sample.value();

      // for non-cg sample files, "caller" and "callee" nodes are the same
      if (! is_callgraph) {
        // we store the sample count as a "self" value on this node
        caller_node->add_self_count(event_name, value);

        if (g_show_process_call_graph) {
          cout << this->to_string() << ": "
               << "Self count: "
               << caller_node->to_string()
               << ", event " << event_name << " = " << value
               << endl;
        }
      }

      // for callgraph sample files, the "caller" and "callee" are different
      else { // is_callgraph
        
        // get/create edge from caller to callee
        CallGraphEdge* edge = caller_node->get_edge_to_callee(callee_node);
        if (edge == NULL) {
          edge = caller_node->add_edge_to_callee(callee_node);
        }
        // add event count
        edge->add_sample_count(event_name, value);

        if (g_show_process_call_graph) {
          cout << this->to_string() << ": "
               << "Edge count: "
               << "from " << caller_node->to_string()
               << ", to " << callee_node->to_string()
               << ", event " << event_name << " = " << value
               << endl;
        }
      }
    }
  }
}

/** Clears any call graph edges and self values added for this process. */
void Process::remove_call_graph() const
{
  // iterate over the SampleFiles
  unsigned int count = m_sample_files.size();
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* const sample_file_ptr = m_sample_files[i];
    bool is_callgraph = sample_file_ptr->is_callgraph_file();

    // iterate over the Samples in the file
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    int sampleCount = samples.size();
    for (int j=0; j<sampleCount; ++j) {
      Sample sample = samples[j];
      CallGraphNode* caller_node = sample.from_call_graph_node();

      if (! is_callgraph) {
        // clear callgraph "self" counts for the node
        // note: caller/callee_node point to the same node
        caller_node->clear_self_counts();
      }
      else {
        // remove any callgraph "edges" we added
        // (this implicitly removes visited count on edges)
        caller_node->remove_edges_to_callees();
      }
    }
  }
}

/** Removes all edges with a sample count of zero */
void Process::remove_zero_edges()
{
  // Iterate over the SampleFiles:
  unsigned int count = m_sample_files.size();
  for (unsigned int i = 0; i < count; i += 1) {
    const SampleFile* const sample_file_ptr = m_sample_files[i];
    bool is_callgraph = sample_file_ptr->is_callgraph_file();

    // we only need to look at callgraph (edge) samples
    if (is_callgraph) {
      // Iterate over the Samples in the file:
      const SampleFile::SampleList& samples = sample_file_ptr->samples();
      int sampleCount = samples.size();
      for (int j = 0; j < sampleCount; j += 1) {
        Sample sample = samples[j];
        CallGraphNode* caller_node = sample.from_call_graph_node();
        caller_node->remove_zero_edges_to_callees();
      }
    }
  }
}


/** Finds and collects the "root" nodes of the call graph for this process.
    Basically, this is any node in the process's sample data that has either
    (a) no non-zero "caller" edges, and one or more "callee" edges
    (b) no non-zero caller/callee edges, 
        but a non-zero "self" count for any event name.

    Note: this means that if a given node's edges are all consumed,
    but because of rounding we don't consume the node's entire "self" count,
    then we'll treat the remaining roundoff as a separate "root" frame
    for the same function, so at least the self count won't be lost.

    TODO: ideally, we'd should sort these root(s) so that "important" ones
    like _start(), etc. are processed first when building call trees,
    but we can't tell how "important" a root is until we've built
    its call tree and seen what it touches, and looking for "special" names
    like "_start", etc. feels like a hack. For now, we assume the
    "typical" case of a call tree with one "main" root for the application
    and a few side root(s) corresponding to interrupts, etc. that don't
    impact the main call tree much.
*/
void Process::find_root_nodes(CallGraphNodePtrVector& root_nodes) const
{
  // iterate over the SampleFiles
  unsigned int count = m_sample_files.size();
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* const sample_file_ptr = m_sample_files[i];

    // iterate over Samples in the file
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    int sampleCount = samples.size();
    for (int j=0; j<sampleCount; ++j) {
      const Sample& sample = samples[j];
      bool root = false;

      // for all samples we can just look at the "caller" node:
      // - for non-cg samples, "caller" and "callee" node are the same node
      // - for cg samples, the "callee" always has a caller, by definition!
      CallGraphNode* caller_node = sample.from_call_graph_node();
      int callers = caller_node->non_zero_caller_count();
      int callees = caller_node->non_zero_callee_count();
      bool has_self_counts = caller_node->has_self_counts();
          
      // if there are no caller edges
      // and there are any callee edges,
      // or failing that if there are any "self" counts remaining
      root = (callers == 0) && (callees > 0 || has_self_counts);

      if (g_show_root_selection) {
        cout << this->to_string() << ": "
             << "Root check: "
             << caller_node->to_string()
             << ", callers=" << callers
             << ", callees=" << callees
             << ", has_self_counts=" << has_self_counts
             << ", root=" << root
             << endl;

        if (callers > 0) {
          FOR_EACH(iterator, it, CallGraphEdgePtrVector,
                   caller_node->caller_edges()) {
            cout << "   Caller: " << (*it)->to_string() << endl;
          }
          cout << endl;
        }
        if (callees > 0) {
          FOR_EACH(iterator, it, CallGraphEdgePtrVector,
                   caller_node->callee_edges()) {
            cout << "   Callee: " << (*it)->to_string() << endl;
          }
          cout << endl;
        }
        if (has_self_counts > 0) {
          FOR_EACH(iterator, it, EventCountMap,
                   caller_node->get_self_counts()) {
            cout << "   Self Count: " << it->first << "="
                 << it->second << endl;
          }
        }
      }

      // a given root node may be the "caller" for many samples,
      // so be careful not to add it multiple times
      if (root && ! root_nodes.contains(caller_node))
        root_nodes.add(caller_node);
    }
  }
}


/**
 * Adds the root nodes of this process' call graph to "nodes".
 * If no such root nodes are found, this function then looks cycles
 * If a cycle is found, one of its nodes is added to "nodes".
 * 
 * The returned string is "Root Nodes" or "Cycle", depending on
 * what was found.  If neither are found, "nodes" is not touched
 * and "" is returned.
 */
string Process::find_root_nodes_or_cycle(CallGraphNodePtrVector& nodes) const
{
  // first, see if we have any new roots to process
  find_root_nodes(nodes);
  int root_count = nodes.size();
  if (root_count > 0) {
    return "New Roots";
  }

  // if there are no roots, then if there are any nodes left
  // in the call graph, there's at least one cycle;
  // we'll pick an arbitrary node in the cycle and call that a "root",
  // and process it to see if that cleans up the remaining nodes
  CallGraphNode* node_in_cycle = find_node_with_at_least_one_callee();
  if (node_in_cycle != NULL) {
    nodes.add(node_in_cycle);
    return "Node in a Cycle";
  }

  // nothing left to process, we're done
  return "";
}


/** Looks for any node of this process' call graph which
    has at least one non-zero edge to a callee node
 */
CallGraphNode* Process::find_node_with_at_least_one_callee() const
{
  // iterate over the SampleFiles
  unsigned int count = m_sample_files.size();
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* const sample_file_ptr = m_sample_files[i];

    // iterate over Samples in the file
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    int sampleCount = samples.size();
    for (int j=0; j<sampleCount; ++j) {
      Sample sample = samples[j];

      // for all samples we can just look at the "caller" node
      // (for non-cg samples, this is the same as the "callee" node)
      CallGraphNode* caller_node = sample.from_call_graph_node();

      // if there's at least one non-zero edge to a callee
      if (caller_node->non_zero_callee_count() > 0) {
        // we'll take it
        return caller_node;
      }
    }
  }

  return NULL;
}


// utility function called by find_process_in_sample_files:

/** Adds the given SampleFilePtr to this Process' list of SampleFiles */
void Process::add_sample_file(const SampleFile* sample_file_ptr)
{
  m_sample_files.add(sample_file_ptr);
    
  // Add the CPU number from the sample file to
  // this Process' CPU number list,
  // if that CPU number isn't already in the list:
  int cpu_number = to_int(sample_file_ptr->pathname().cpu_id());
  if ( ! m_cpu_numbers.contains(cpu_number) ) {
    m_cpu_numbers.add(cpu_number);
  }
}


// -------------------------------------------------------------------------
// friend functions
// -------------------------------------------------------------------------

/**
 * Constructs Process objects for all processes/threads in the
 * given sample files and adds them to "processes"
 */
void find_processes_in_sample_files(
  const SampleFileVector&  sample_file_vector,
  ProcessPtrVector&        processes
) {
  // This maps local paths to the index in "processes" of the Process
  // with that local path:
  ProcessMap process_map;

  // Iterate over the SampleFiles:
  unsigned int count = sample_file_vector.size();
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* sample_file_ptr = &sample_file_vector[i];

    // Get the SampleFile's exe path, PID, TID, session number, and cpu
    // to use as a key:
    string exec_path = sample_file_ptr->executable_pathname();
    string PID_string = sample_file_ptr->process_id();
    int PID = to_int(PID_string);
    string TID_string = sample_file_ptr->thread_id();
    int TID = to_int(TID_string);
    int session_number = sample_file_ptr->session_number();
    string CPU_string = sample_file_ptr->cpu_id();

    string map_key = exec_path + ":" + PID_string + ":"
      + TID_string + ":" + to_string(session_number) +
      ((g_split_across_cpus) ? CPU_string : "");

    Process* matched_process = process_map.get(map_key, NULL);
    if (matched_process == NULL) {
      // Not found: create a new Process for this path:
      matched_process = new Process(PID, TID, session_number, exec_path);
      processes.add(matched_process);
      process_map.add(map_key, matched_process);
    }
    matched_process->add_sample_file(sample_file_ptr);
  }
}
