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
// Process -- Represents a process
// ==========================================================================

// multiple-inclusion guard
#ifndef TILERA_PROCESS_H
#define TILERA_PROCESS_H

// C/C++ includes
#include <sstream>

// custom includes
#include "SampleFile.h"
#include "Vector.h"
#include "global_options.h"


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

// forward def
class Process;  

/** Linux CPU number */
typedef unsigned int cpu_number;

/** Vector of CPU numbers on which process ran */
typedef Vector<cpu_number> CpuNumberVector;

/** Vector of Process objects */
typedef Vector<Process*> ProcessPtrVector;


// -------------------------------------------------------------------------
// friend functions
// -------------------------------------------------------------------------

/**
 * Constructs Process objects for all binaries in the given sample files
 * and adds them to "processes".
 * If aggregrate_over_cpus is false, we split any process
 * that ran on different cpus into one process object per unique cpu id,
 * and allocate sample files to the process object based on their cpu id.
 */
void find_processes_in_sample_files (
  const SampleFileVector&  sample_file_vector,
  ProcessPtrVector&        processes
);


// --------------------------------------------------------------------------
// Process
// --------------------------------------------------------------------------

/**
 * Represents a process/thread.
 *
 * Each Process knows its process ID (PID), thread ID(TID), and OProfile
 * session number, and has a vector of the SampleFiles which match that
 * PID/TID and session number.
 */
class Process {

  // --- friends ---
  friend void find_processes_in_sample_files (
    const SampleFileVector&, ProcessPtrVector&);


  // --- members ---
 private:
  /** The next unallocated unique ID for processes */
  static int s_next_id;

  /** The unique ID for this Process.
   *  Note: this is not the Linux PID, it's just an internal
   *  unique ID for our use.
   */
  int m_id;

  /** The Linux PID of this Process */
  int m_linux_process_id;

  /** The Linux TID of this Process */
  int m_linux_thread_id;

  /** The OProfile session number of this Process */
  int m_session_number;

  /** The executable path of this Process */
  string m_executable_path;

  /** The SampleFiles whose PID match the PID of this Process */
  SampleFilePtrVector m_sample_files;

  /** The numbers of the CPUs on which this process ran */
  CpuNumberVector m_cpu_numbers;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  Process (const int linux_process_id, const int linux_thread_id,
           const int session_number, const string executable_path)
    : m_id(s_next_id++),
      m_linux_process_id(linux_process_id),
      m_linux_thread_id(linux_thread_id),
      m_session_number(session_number),
      m_executable_path(executable_path),
      m_sample_files(), m_cpu_numbers()
  { }


  // --- accessors ---
 public:
  /** Returns this Process's unique ID */
  const int id() const { return m_id; }

  /** Returns this Process's Linux process ID */
  const int linux_process_id() const { return m_linux_process_id; }

  /** Returns this Process's Linux thread ID */
  const int linux_thread_id() const { return m_linux_thread_id; }

  /** Returns this Process's OProfile session number */
  const int session_number() const { return m_session_number; }

  /** Returns this Process's path */
  const string executable_path() const { return m_executable_path; }

  /** Returns the list of SampleFiles for this Process */
  const SampleFilePtrVector& sample_files() const { return m_sample_files; }

  /** Returns the numbers of the CPUs on which this process ran */
  const CpuNumberVector& cpu_numbers() const { return m_cpu_numbers; }


  // --- to_string() function ---
 public:
  /** Returns a string representation of this process */
  const string to_string() const {
    std::stringstream ss;
    ss << "Process("
       << "pid=" << m_linux_process_id << ", "
       << "tid=" << m_linux_thread_id << ", "
       << "exe=" << m_executable_path
       << ")";
    return ss.str();
  }


  // --- member functions ---
 public:
  /** Adds self values and call graph edges for this process */
  void construct_call_graph() const;

  /** Clears any call graph edges and self values added for this process. */
  void remove_call_graph() const;

  /** Removes edges for which the sample count has been set to zero */
  void remove_zero_edges();

  /** Finds and collects the "root" nodes of the call graph
      for this process. */
  void find_root_nodes(CallGraphNodePtrVector& root_nodes) const;

  /** Looks for any node of this process' call graph
      which has at least one edge to a callee node */
  CallGraphNode* find_node_with_at_least_one_callee() const;

  /**
   * Adds the root nodes of this process' call graph to "nodes".
   * If no such root nodes are found, this function then looks cycles
   * If a cycle is found, one of its nodes is added to "nodes".
   * 
   * The returned string is "Root Nodes" or "Cycle", depending on
   * what was found.  If neither are found, "nodes" is not touched
   * and "" is returned.
   */
  string find_root_nodes_or_cycle(CallGraphNodePtrVector& nodes) const;


  // --- utility member function ---

  // utility function called by find_process_in_sample_files:
 private:
  /** Adds the given SampleFilePtr to this Process' list of SampleFiles */
  void add_sample_file (const SampleFile* sample_file_ptr);
};

// multiple-inclusion guard
#endif
