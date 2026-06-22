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
// StatisticDescriptorIndex.h -- profiling statistic descriptor index
// ==========================================================================

// multiple-inclusion guard
#ifndef STATISTICDESCRIPTORINDEX_H
#define STATISTICDESCRIPTORINDEX_H

// custom includes
#include "Pathname.h"            // Unix pathnames
#include "Vector.h"              // Vector class
#include "Set.h"                 // Set class
#include "Map.h"                 // Map class
#include "xml.h"                 // XML support

#include "StatisticDescriptor.h" // profiling statistic descriptor


// -------------------------------------------------------------------------
// StatisticDescriptorIndex
// -------------------------------------------------------------------------

/** List of event name strings, used by remove_unused_event_names() method */
typedef StringVector EventNameVector;

/** Index of statistic descriptors */
class StatisticDescriptorIndex
{
  // --- members ---
protected:

  /** List of top-level statistics */
  StatisticDescriptorVector m_statistics;

  /** Mapping from statistic name to statistic descriptor. */
  StatisticDescriptorMap m_statistics_by_name;

  /** Mapping from OProfile EVENT_NAME to statistic descriptor. */
  StatisticDescriptorMap m_statistics_by_event_name;

  /** Value statistics to check for <frame> node */
  StatisticDescriptorVector m_frame_statistics;

  /** Value statistics to check for <frame><processor> node */
  StatisticDescriptorVector m_frame_processor_statistics;

  /** Value statistics to check for <frame><processor><stalls> node */
  StatisticDescriptorVector m_frame_stall_statistics;

  /** Value statistics to check for <frame><processor><user_events> node */
  StatisticDescriptorVector m_frame_user_event_statistics;

  /** Value statistics to check for <frame><cache> node */
  StatisticDescriptorVector m_frame_cache_statistics;

  /** Value statistics to check for <frame><imesh> node */
  StatisticDescriptorVector m_frame_imesh_statistics;


  // --- constructors/destructors ---
public:
  
  /** Constructor */
  StatisticDescriptorIndex()
  {
  }
  
  /** Copy constructor */
  StatisticDescriptorIndex(const StatisticDescriptorIndex& index)
  {
    FOR_EACH(const_iterator, i, StatisticDescriptorVector,
             index.m_statistics)
    {
      StatisticDescriptor* stat = (*i);

      // copy the top-level statistic and all its children
      StatisticDescriptor* stat_copy = new StatisticDescriptor(stat);

      // add copied stat tree to this index, and updates the name->stat maps
      add(NULL, stat_copy, false); // DON'T update parent->child links
    }
  }
  
  /** Destructor */
  ~StatisticDescriptorIndex();


  // --- accessors ---
public:

  /** Returns list of top-level statistics */
  const StatisticDescriptorVector& statistics() const
  {
    return m_statistics;
  }

  /** Returns map from name to statistic descriptor */
  const StatisticDescriptorMap& statistics_by_name() const
  {
    return m_statistics_by_name;
  }

  /** Returns map from OProfile EVENT_NAME to statistic descriptor. */
  const StatisticDescriptorMap& statistics_by_event_name() const
  {
    return m_statistics_by_event_name;
  }


  // --- statistic storage/retrieval ---
public:

  /** Adds a single statistic descriptor */
  void add(StatisticDescriptor* parent, StatisticDescriptor* stat,
           bool update_parent = true);

  /** Deletes a statistic descriptor, plus its children if any */
  void remove(StatisticDescriptor* stat);

  /** Looks up statistic by name */
  const StatisticDescriptor* statistic(const string& name) const;
  /** Looks up statistic by name */
  StatisticDescriptor* statistic(const string& name);

  /** Looks up percent statistic by base statistic's name
   *  E.g. given "stat_name" as an argument,
   *  returns "stat_name%" statistic descriptor, if found.
   */
  const StatisticDescriptor* percent_statistic(const string& name) const;
  /** Looks up percent statistic by base statistic's name
   *  E.g. given "stat_name" as an argument,
   *  returns "stat_name%" statistic descriptor, if found.
   */
  StatisticDescriptor* percent_statistic(const string& name);

  /** Looks up event statistic by event name */
  const StatisticDescriptor* event_statistic(const string& event_name) const;
  /** Looks up event statistic by event name */
  StatisticDescriptor* event_statistic(const string& event_name);

protected:
  /** Processes a single statistic description from XML element. */
  StatisticDescriptor* process_statistic_description(
    StatisticDescriptor* parent, XMLElement* statElement);

  /** Loads tree of statistic descriptors from XML elements */
  void process_statistic_descriptions(
    StatisticDescriptor* parent, XMLElement* statElement);

  /** Loads tree of statistic descriptors from XML elements */
  void process_statistic_descriptions(XMLElement* statElement);

public:

  /** Loads specified statistic metadata file, populates index. */
  bool load_statistics(const Pathname& path, string& errors);

  /** Loads specified statistic metadata file, populates index. */
  bool load_statistics(const string& path, string& errors);

  /** Deletes event stats from the index whose event names
      are not in the specified list. */
  void remove_unused_event_stats(const EventNameVector& event_names);

protected:
  /** Internal -- Deletes event stats from the index whose event names
      are not in the specified list. */
  void remove_unused_event_stats(const EventNameVector& event_names,
                                 StringSet& removed,
				 StatisticDescriptor* statistic);

public:
  /** Value statistics to check for <frame> node */
  const StatisticDescriptorVector& get_frame_statistics() const {
    return m_frame_statistics;
  }

  /** Value statistics to check for <frame><processor> node */
  const StatisticDescriptorVector& get_frame_processor_statistics() const {
    return m_frame_processor_statistics;
  }

  /** Value statistics to check for <frame><processor><stalls> node */
  const StatisticDescriptorVector& get_frame_stall_statistics() const {
    return m_frame_stall_statistics;
  }

  /** Value statistics to check for <frame><processor><user_events> node */
  const StatisticDescriptorVector& get_frame_user_event_statistics() const {
    return m_frame_user_event_statistics;
  }

  /** Value statistics to check for <frame><cache> node */
  const StatisticDescriptorVector& get_frame_cache_statistics() const {
    return m_frame_cache_statistics;
  }

  /** Value statistics to check for <frame><imesh> node */
  const StatisticDescriptorVector& get_frame_imesh_statistics() const {
    return m_frame_imesh_statistics;
  }


  // --- display methods ---
public:

  /** Writes XML for statistic descriptor index hierarchy
      to specified stream. */
  template<typename charT, typename traits> \
    void print(std::basic_ostream<charT, traits>& out,
	       int indent = -1, int step = 2) const
  {
    FOR_EACH(const_iterator, i, StatisticDescriptorVector, m_statistics)
    {
      (*i)->print(out, indent, step);
    }
  }
};

/** Internal -- utility method for displaying content of statistics index */
void debug_print_stat_tree(const StatisticDescriptor& statistic, 
                           int indent = 0, int step = 2);

/** Internal -- utility method for displaying content of statistics index */
void debug_print_stat_tree(const StatisticDescriptorIndex& index,
                           int indent = 0, int step = 2);

// multiple-inclusion guard
#endif
