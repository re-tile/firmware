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


// -------------------------------------------------------------------------
// StatisticDescriptorIndex
// -------------------------------------------------------------------------

// --- constructors/destructors ---

/** Destructor */
StatisticDescriptorIndex::~StatisticDescriptorIndex()
{
  m_statistics_by_name.clear();
  m_statistics_by_event_name.clear();
  FOR_EACH(const_iterator, i, StatisticDescriptorArray, m_statistics)
  {
    delete (*i);
  }
  m_statistics.clear();
}


// --- statistic storage/retrieval ---

/** Adds a single statistic descriptor */
void
StatisticDescriptorIndex::add(StatisticDescriptor* parent,
                              StatisticDescriptor* stat,
                              bool update_parent)
{
  // add stat to main tree of statistics
  if (parent == NULL)
  {
    m_statistics.add(stat);
  }
  else
  {
    if (update_parent) stat->set_parent(parent);
  }

  // hash it by the stat name
  m_statistics_by_name.add(stat->get_name(), stat);

  // hash hardware events by the hardware EVENT name
  if (stat->is_event())
  {
    m_statistics_by_event_name.add(stat->get_event_name(), stat);

    // create lists of value stats we'll need to check
    // while generating various elements in the XML output
    const std::string& method = stat->get_method();
    if (starts_with(method, "value:frame@"))
    {
      m_frame_statistics.add(stat);
    }
    else if (starts_with(method, "value:frame/processor@"))
    {
      m_frame_processor_statistics.add(stat);
    }
    else if (starts_with(method, "value:frame/processor/stalls@"))
    {
      m_frame_stall_statistics.add(stat);
    }
    else if (starts_with(method, "value:frame/processor/user_events@"))
    {
      m_frame_user_event_statistics.add(stat);
    }
    else if (starts_with(method, "value:frame/cache@"))
    {
      m_frame_cache_statistics.add(stat);
    }
    else if (starts_with(method, "value:frame/imesh@"))
    {
      m_frame_imesh_statistics.add(stat);
    }
  }

  // if it has any children, add them too
  FOR_EACH(const_iterator, i, StatisticDescriptorArray, stat->get_children())
  {
    add(stat, *i, update_parent);
  }
}

/** Deletes a statistic descriptor, plus its children if any */
void
StatisticDescriptorIndex::remove(StatisticDescriptor* stat)
{
  if (stat == NULL) return;

  // if it has any children, delete them first
  FOR_EACH(const_iterator, i, StatisticDescriptorArray, stat->get_children())
  {
    delete(*i);
  }

  // remove it from hierarchy
  const StatisticDescriptor* parent = stat->get_parent();
  if (parent == NULL)
  {
    m_statistics.remove(stat);
  }
  else
  {
    stat->set_parent(NULL);
  }

  // remove it from mappings
  m_statistics_by_name.remove(stat->get_name());
  if (stat->is_event())
  {
    m_statistics_by_event_name.remove(stat->get_event_name());
    const std::string& method = stat->get_method();
    if (starts_with(method, "value:frame@"))
    {
      m_frame_statistics.remove(stat);
    }
    else if (starts_with(method, "value:frame/processor@"))
    {
      m_frame_processor_statistics.remove(stat);
    }
    else if (starts_with(method, "value:frame/processor/stalls@"))
    {
      m_frame_stall_statistics.remove(stat);
    }
    else if (starts_with(method, "value:frame/processor/user_events@"))
    {
      m_frame_user_event_statistics.remove(stat);
    }
    else if (starts_with(method, "value:frame/cache@"))
    {
      m_frame_cache_statistics.remove(stat);
    }
    else if (starts_with(method, "value:frame/imesh@"))
    {
      m_frame_imesh_statistics.remove(stat);
    }
  }

  // delete stat object itself
  delete stat;
}

/** Looks up statistic by name */
const StatisticDescriptor*
StatisticDescriptorIndex::statistic(const std::string& name) const
{
  return m_statistics_by_name.get(name, NULL);
}

/** Looks up statistic by name */
StatisticDescriptor*
StatisticDescriptorIndex::statistic(const std::string& name)
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
const StatisticDescriptor*
StatisticDescriptorIndex::percent_statistic(const std::string& name) const
{
  return m_statistics_by_name.get(name + "%", NULL);
}

/** Looks up percent statistic by base statistic's name
 *  E.g. given "stat_name" as an argument,
 *  returns "stat_name%" statistic descriptor, if found.
 */
StatisticDescriptor*
StatisticDescriptorIndex::percent_statistic(const std::string& name)
{
  // non-const overload calls the const member
  // and casts to remove the const-ness
  return const_cast<StatisticDescriptor*>(
    static_cast<const StatisticDescriptorIndex&>(*this).
      percent_statistic(name));
}

/** Looks up event statistic by event name */
const StatisticDescriptor*
StatisticDescriptorIndex::event_statistic(const std::string& event_name) const
{
  return m_statistics_by_event_name.get(event_name, NULL);
}

/** Looks up event statistic by event name */
StatisticDescriptor*
StatisticDescriptorIndex::event_statistic(const std::string& event_name)
{
  // non-const overload calls the const member
  // and casts to remove the const-ness
  return const_cast<StatisticDescriptor*>(
    static_cast<const StatisticDescriptorIndex&>(*this).
      event_statistic(event_name));
}

/** Processes a single statistic description from XML element. */
StatisticDescriptor*
StatisticDescriptorIndex::process_statistic_description(
  StatisticDescriptor* parent, XMLElement* statElement)
{
  std::string name          = statElement->get_attribute("name");
  std::string display_name  = statElement->get_attribute("display");
  std::string description   = statElement->get_attribute("description");
  std::string type          = statElement->get_attribute("type");
  std::string method        = statElement->get_attribute("method");
  std::string event         = statElement->get_attribute("event");
  int min_count             = statElement->get_int_attribute("min_count");
  int default_count         = statElement->get_int_attribute("default_count");

  StatisticDescriptor* stat =
    new StatisticDescriptor(name, display_name, description,
                            type, method,
                            event, min_count, default_count);
  add(parent, stat);

  return stat;
}

/** Loads tree of statistic descriptors from XML elements */
void
StatisticDescriptorIndex::process_statistic_descriptions(
  StatisticDescriptor* parent, XMLElement* statElement)
{
  StatisticDescriptor* stat =
    process_statistic_description(parent, statElement);

  XMLElementArray children;
  statElement->get_children_named("statistic", children);
  FOR_EACH(const_iterator, i, XMLElementArray, children)
  {
    XMLElement* childElement = *i;
    process_statistic_descriptions(stat, childElement);
  }
}

/** Loads tree of statistic descriptors from XML elements */
void
StatisticDescriptorIndex::process_statistic_descriptions(
  XMLElement* statElement)
{
  process_statistic_descriptions(NULL, statElement);
}

/** Loads specified statistic metadata file, populates index. */
bool
StatisticDescriptorIndex::load_statistics(
  const Pathname& path, std::string& errors)
{
  return load_statistics(path.to_string(), errors);
}

/** Loads specified statistic metadata file, populates index. */
bool
StatisticDescriptorIndex::load_statistics(
  const std::string& path, std::string& errors)
{
  bool result = true;
  errors = "";

  // load XML document
  XMLDocument* profileMetadataXML = XMLReader::read(path, errors);
  if (profileMetadataXML == NULL)
  {
    errors = "Could not read statistic metadata from '" + path + "'.\n"
             + errors;
    result = false;
  }
  else
  {
    // process its elements
    result = load_statistics(profileMetadataXML, errors);
  }

  return result;
}

/** Loads statistic metadata from XML document, populates index. */
bool
StatisticDescriptorIndex::load_statistics(
  XMLDocument* document, std::string& errors)
{
  bool result = true;
  // get properties element
  const XMLElement* propertiesElement =
    document->get_element_by_path("properties");
  if (propertiesElement == NULL)
  {
    errors += "Could not find <properties> root element "
              "in statistics metadata.\n";
    result = false;
  }
  else
  {
    // walk its children, which are the top-level statistics
    XMLElementArray children;
    propertiesElement->get_children_named("statistic", children);
    FOR_EACH(const_iterator, i, XMLElementArray, children)
    {
      // process each one, and its children
      XMLElement* statElement = *i;
      process_statistic_descriptions(statElement);
    }
  }

  return result;
}

/** Deletes event stats from the index whose event names are not
    in the specified list. */
void
StatisticDescriptorIndex::remove_unused_event_stats(
  const EventNameArray& event_names)
{
  // NOTE: can't remove items from list we're iterating over,
  // so need to create a separate copy of the list,
  // iterate over that, and remove items from the original
  StatisticDescriptorArray list = m_statistics;

  // keep track of names of stats we remove,
  // so we can remove any stats that depend on them
  StringSet removed;

  // iterate over top-level stats in the index
  FOR_EACH(const_iterator, i, StatisticDescriptorArray, list)
  {
    StatisticDescriptor* child = (*i);
    remove_unused_event_stats(event_names, removed, child);
  }
}

/** Sets event ids for hardware event names, from specified mapping. */
void
StatisticDescriptorIndex::populate_event_ids(
  const Map<std::string, int>& event_names_and_ids,
  StatisticDescriptor* desc)
{
  if (desc == NULL)
  {
    // iterate over top-level stats in the index
    FOR_EACH(iterator, it, StatisticDescriptorArray, m_statistics)
    {
      StatisticDescriptor* child = *it;
      populate_event_ids(event_names_and_ids, child);
    }
  }
  else
  {
    // Populate hardware event name, if any, on this descriptor.
    if (desc->is_event())
    {
      const std::string& event_name = desc->get_event_name();
      int event_id = event_names_and_ids.get(event_name, 0);
      desc->set_event_id(event_id);
    }

    // Iterate over its children.
    FOR_EACH(iterator, it, StatisticDescriptorArray, desc->get_children())
    {
      StatisticDescriptor* child = *it;
      populate_event_ids(event_names_and_ids, child);
    }
  }
}


/** Internal -- Deletes event stats from the index whose event names
    are not in the specified list. */
void
StatisticDescriptorIndex::remove_unused_event_stats(
  const EventNameArray& event_names,
  StringSet& removed,
  StatisticDescriptor* statistic)
{
  bool remove_it = false;

  // remove event stats that aren't on the guest list
  if (statistic->is_event() &&
      ! event_names.contains(statistic->get_event_name()))
  {
    remove_it = true;
  }

  // for non-event stats, we need to
  // (a) check children for event stats
  // (b) remove stats that depend on stats we've removed
  // (c) clean out any container stats that have no children left
  else
  {
    
    // get statistic method (used to determine which stats to prune)
    std::string method_name = statistic->get_method_name();

    // NOTE: can't remove items we're iterating over, so need to create a
    // separate copy of the list, iterate over that,
    // and remove items from the original
    StatisticDescriptorArray list = statistic->get_children();

    // if there are no children...
    if (list.empty())
    {
      if (method_name == "" || method_name == "none")
      {
        remove_it = true;
      }
    }
    
    // if there _are_ children...
    else
    {
      // recurse over children to clean them out
      FOR_EACH(const_iterator, i, StatisticDescriptorArray, list)
      {
        StatisticDescriptor* child = (*i);
        remove_unused_event_stats(event_names, removed, child);
      }

      // if we managed to clean out all the children,
      // remove the containing stat too
      if (! statistic->has_children())
      {
        remove_it = true;
      }
    }

    // finally, check whether this statistic is a computed value that
    // depends on any stats we've removed so far -- if so, drop it.
    if (method_name != "" && 
        method_name != "none" &&
        method_name != "value")
    {
      StringArray method_args = statistic->get_method_arguments();
      FOR_EACH(const_iterator, i, StringArray, method_args)
      {
        const std::string& arg = *i;
        if (removed.contains(arg))
        {
          // whoops, need to remove this statistic because
          // it depends on something that's been removed
          remove_it = true;
          break;
        }
      }
    }
  }

  // if this stat didn't make the cut, remove it
  if (remove_it)
  {
    // keep track of names of stats we remove,
    // so we can remove those that depend on them
    removed.add(statistic->get_name());
    remove(statistic);
  }
}


// --- display methods ---

/** Returns statistics as XML. */
XMLElement*
StatisticDescriptorIndex::to_xml(XMLElement* parent) const
{
  const std::string PROPERTIES = "properties";
  XMLElement* root = new XMLElement(PROPERTIES, parent);
  FOR_EACH(const_iterator, it, StatisticDescriptorArray, m_statistics)
  {
    const StatisticDescriptor& sd = *it;
    sd.to_xml(root);
  }
  return root;
}
