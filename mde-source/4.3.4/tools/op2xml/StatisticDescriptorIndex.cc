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
// StatisticDescriptorIndex.cc -- profiling statistic descriptor index
// ==========================================================================

// custom includes
#include "StatisticDescriptorIndex.h"  // header file
#include "Vector.h"                    // Vector collection

// -------------------------------------------------------------------------
// StatisticDescriptorIndex
// -------------------------------------------------------------------------

// --- constructors/destructors ---

/** Destructor */
StatisticDescriptorIndex::~StatisticDescriptorIndex()
{
  m_statistics_by_name.clear();
  m_statistics_by_event_name.clear();
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, m_statistics)
  {
    delete (*i);
  }
  m_statistics.clear();
}


// --- statistic storage/retrieval ---

/** Adds a single statistic descriptor */
void StatisticDescriptorIndex::add(StatisticDescriptor* parent,
                                   StatisticDescriptor* stat,
                                   bool update_parent)
{
  // add stat to main tree of statistics
  if (parent == NULL)
  {
    m_statistics.add(stat);
  }
  else {
    if (update_parent) stat->set_parent(parent);
  }

  // hash it by the stat name
  m_statistics_by_name.add(stat->name(), stat);

  // hash hardware events by the hardware EVENT name
  if (stat->is_event())
  {
    m_statistics_by_event_name.add(stat->event(), stat);

    // create lists of value stats we'll need to check
    // while generating various elements in the XML output
    if (starts_with(stat->method(), "value:frame@"))
    {
      m_frame_statistics.add(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor@"))
    {
      m_frame_processor_statistics.add(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor/stalls@"))
    {
      m_frame_stall_statistics.add(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor/user_events@"))
    {
      m_frame_user_event_statistics.add(stat);
    }
    else if (starts_with(stat->method(), "value:frame/cache@"))
    {
      m_frame_cache_statistics.add(stat);
    }
    else if (starts_with(stat->method(), "value:frame/imesh@"))
    {
      m_frame_imesh_statistics.add(stat);
    }
  }

  // if it has any children, add them too
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, stat->children())
  {
    add(stat, *i, update_parent);
  }
}

/** Deletes a statistic descriptor, plus its children if any */
void StatisticDescriptorIndex::remove(StatisticDescriptor* stat)
{
  if (stat == NULL) return;

  // if it has any children, delete them first
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, stat->children())
  {
    delete(*i);
  }

  // remove it from hierarchy
  const StatisticDescriptor* parent = stat->parent();
  if (parent == NULL) {
    m_statistics.remove(stat);
  }
  else {
    stat->set_parent(NULL);
  }

  // remove it from mappings
  m_statistics_by_name.remove(stat->name());
  if (stat->is_event()) {
    m_statistics_by_event_name.remove(stat->event());
    if (starts_with(stat->method(), "value:frame@"))
    {
      m_frame_statistics.remove(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor@"))
    {
      m_frame_processor_statistics.remove(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor/stalls@"))
    {
      m_frame_stall_statistics.remove(stat);
    }
    else if (starts_with(stat->method(), "value:frame/processor/user_events@"))
    {
      m_frame_user_event_statistics.remove(stat);
    }
    else if (starts_with(stat->method(), "value:frame/cache@"))
    {
      m_frame_cache_statistics.remove(stat);
    }
    else if (starts_with(stat->method(), "value:frame/imesh@"))
    {
      m_frame_imesh_statistics.remove(stat);
    }
  }

  // delete stat object itself
  delete stat;
}

/** Looks up statistic by name */
const StatisticDescriptor* StatisticDescriptorIndex::statistic(
  const string& name) const
{
  return m_statistics_by_name.get(name, NULL);
}

/** Looks up statistic by name */
StatisticDescriptor* StatisticDescriptorIndex::statistic(const string& name)
{
  // non-const overload calls the const member
  // and casts to remove the const-ness
  return const_cast<StatisticDescriptor*>(
    static_cast<const StatisticDescriptorIndex&>(*this).statistic(name));
}

/** Looks up percent statistic by base statistic's name
 *  E.g. given "stat_name" as an argument,
 *  returns "stat_name%" statistic descriptor, if found.
 */
const StatisticDescriptor* StatisticDescriptorIndex::percent_statistic(
  const string& name) const
{
  return m_statistics_by_name.get(name + "%", NULL);
}

/** Looks up percent statistic by base statistic's name
 *  E.g. given "stat_name" as an argument,
 *  returns "stat_name%" statistic descriptor, if found.
 */
StatisticDescriptor* StatisticDescriptorIndex::percent_statistic(
  const string& name)
{
  // non-const overload calls the const member
  // and casts to remove the const-ness
  return const_cast<StatisticDescriptor*>(
    static_cast<const StatisticDescriptorIndex&>(*this).
      percent_statistic(name));
}

/** Looks up event statistic by event name */
const StatisticDescriptor* StatisticDescriptorIndex::event_statistic(
  const string& event_name) const
{
  return m_statistics_by_event_name.get(event_name, NULL);
}

/** Looks up event statistic by event name */
StatisticDescriptor* StatisticDescriptorIndex::event_statistic(
  const string& event_name)
{
  // non-const overload calls the const member
  // and casts to remove the const-ness
  return const_cast<StatisticDescriptor*>(
    static_cast<const StatisticDescriptorIndex&>(*this).
      event_statistic(event_name));
}

/** Processes a single statistic description from XML element. */
StatisticDescriptor* StatisticDescriptorIndex::process_statistic_description(
  StatisticDescriptor* parent, XMLElement* statElement)
{
  string name          = statElement->attribute("name");
  string display_name  = statElement->attribute("display");
  string description   = statElement->attribute("description");
  string type          = statElement->attribute("type");
  string method        = statElement->attribute("method");
  string event         = statElement->attribute("event");
  int min_count        = to_int(statElement->attribute("min_count"));
  int default_count    = to_int(statElement->attribute("default_count"));

  StatisticDescriptor* stat =
    new StatisticDescriptor(name, display_name, description,
                            type, method,
                            event, min_count, default_count);
  add(parent, stat);

  return stat;
}

/** Loads tree of statistic descriptors from XML elements */
void StatisticDescriptorIndex::process_statistic_descriptions(
  StatisticDescriptor* parent, XMLElement* statElement)
{
  StatisticDescriptor* stat =
    process_statistic_description(parent, statElement);

  XMLElementVector children;
  statElement->children_named("statistic", children);
  FOR_EACH(const_iterator, i, XMLElementVector, children)
  {
    XMLElement* childElement = *i;
    process_statistic_descriptions(stat, childElement);
  }
}

/** Loads tree of statistic descriptors from XML elements */
void StatisticDescriptorIndex::process_statistic_descriptions(
  XMLElement* statElement)
{
  process_statistic_descriptions(NULL, statElement);
}

/** Loads specified statistic metadata file, populates index. */
bool StatisticDescriptorIndex::load_statistics(
  const Pathname& path, string& errors)
{
  return load_statistics(path.to_string(), errors);
}

/** Loads specified statistic metadata file, populates index. */
bool StatisticDescriptorIndex::load_statistics(
  const string& path, string& errors)
{
  bool result = true;
  errors = "";

  do {

    // load XML document
    XMLDocument* profileMetadataXML = XMLReader::read(path, errors);
    if (profileMetadataXML == NULL) {
      errors = "Could not read statistic metadata from '" + path + "'.\n"
               + errors;
      result = false;
      break;
    }

    // get properties element
    const XMLElement* propertiesElement =
      profileMetadataXML->element_by_path("properties");
    if (propertiesElement == NULL) {
      errors += "Could not find <properties> element in XML data.\n";
      result = false;
      break;
    }

    // walk its children, which are the top-level statistics
    XMLElementVector children;
    propertiesElement->children_named("statistic", children);
    FOR_EACH(const_iterator, i, XMLElementVector, children)
    {
      // process each one, and its children
      XMLElement* statElement = *i;
      process_statistic_descriptions(statElement);
    }

  }
  while (false);

  return result;
}

/** Deletes event stats from the index whose event names are not
    in the specified list. */
void StatisticDescriptorIndex::remove_unused_event_stats(
  const EventNameVector& event_names)
{
  // NOTE: can't remove items from list we're iterating over,
  // so need to create a separate copy of the list,
  // iterate over that, and remove items from the original
  StatisticDescriptorVector list = m_statistics;

  // keep track of names of stats we remove,
  // so we can remove any stats that depend on them
  StringSet removed;

  // iterate over top-level stats in the index
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, list) {
    StatisticDescriptor* child = (*i);
    remove_unused_event_stats(event_names, removed, child);
  }
}

/** Internal -- Deletes event stats from the index whose event names
    are not in the specified list. */
void StatisticDescriptorIndex::remove_unused_event_stats(
  const EventNameVector& event_names,
  StringSet& removed,
  StatisticDescriptor* statistic)
{
  bool remove_it = false;

  // remove event stats that aren't on the guest list
  if (statistic->is_event() && ! event_names.contains(statistic->event())) {
    remove_it = true;
  }

  // for non-event stats, we need to
  // (a) check children for event stats
  // (b) remove stats that depend on stats we've removed
  // (c) clean out any container stats that have no children left
  else {
    
    // get statistic method (used to determine which stats to prune)
    string method_name = statistic->method_name();

    // NOTE: can't remove items we're iterating over, so need to create a
    // separate copy of the list, iterate over that,
    // and remove items from the original
    StatisticDescriptorVector list = statistic->children();

    // if there are no children...
    if (list.empty()) {
      if (method_name == "" || method_name == "none") {
        remove_it = true;
      }
    }
    
    // if there _are_ children...
    else {
      // recurse over children to clean them out
      FOR_EACH(const_iterator, i, StatisticDescriptorVector, list) {
        StatisticDescriptor* child = (*i);
        remove_unused_event_stats(event_names, removed, child);
      }

      // if we managed to clean out all the children,
      // remove the containing stat too
      if (! statistic->has_children()) {
        remove_it = true;
      }
    }

    // finally, check whether this statistic is a computed value that
    // depends on any stats we've removed so far -- if so, drop it.
    if (method_name != "" && 
        method_name != "none" &&
        method_name != "value")
    {
      StringVector method_args = statistic->method_arguments();
      FOR_EACH(const_iterator, i, StringVector, method_args) {
        const string& arg = *i;
        if (removed.contains(arg)) {
          // whoops, need to remove this statistic because
          // it depends on something that's been removed
          remove_it = true;
          break;
        }
      }
    }
  }

  // if this stat didn't make the cut, remove it
  if (remove_it) {
    // keep track of names of stats we remove,
    // so we can remove those that depend on them
    removed.add(statistic->name());
    remove(statistic);
  }
}

/** Internal -- utility method for displaying content of statistics index */
void debug_print_stat_tree(const StatisticDescriptor& statistic,
                           int indent, int step)
{
  for (int s=0; s<indent; s++) cout << " ";
  cout << "- " << statistic.name() << endl;
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, 
           statistic.children())
  {
    debug_print_stat_tree(*i, indent + step, step);
  }
}

/** Internal -- utility method for displaying content of statistics index */
void debug_print_stat_tree(const StatisticDescriptorIndex& index,
                           int indent, int step) {
  FOR_EACH(const_iterator, i, StatisticDescriptorVector, index.statistics())
  {
    debug_print_stat_tree(*i, indent, step);
  }
}
