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
// OutputWriter -- Writes the "tile-op2xml" output
// ==========================================================================

// header
#include "OutputWriter.h"

// application headers
#include "MonitorDataFile.h"
#include "global_options.h"


// --------------------------------------------------------------------------
// OutputWriter
// --------------------------------------------------------------------------

// --- constants ---
const string OutputWriter::OUTPUT_FORMAT_VERSION = "2.0.0";


// --- constructors/destructors ---

/** Constructor */
OutputWriter::OutputWriter(ostream& out,
                           const StatisticDescriptorIndex& statistics_index,
                           const XMLDocument* target_spec,
                           const SampleFileVector& sample_files,
                           const BinaryPtrVector& binaries,
                           const ProcessPtrVector& processes)
  : m_out(out), m_statistics_index(statistics_index),
    m_target_spec(target_spec), m_tile_width(8),
    m_sample_files(sample_files),
    m_binaries(binaries), m_processes(processes)
{ }


// --- member functions ---

/** Writes the "tile-op2xml" XML output */
void OutputWriter::write()
{
  m_current_indentation = 0;

  // <?xml version="1.0" ?>
  XMLUtils::write_xml_declaration_line(m_out);

  // <statistics>
  const string STATISTICS = "statistics";
  XMLUtils::write_element_start(m_out, indentation(), STATISTICS,
                                "version", OUTPUT_FORMAT_VERSION);
  indent();

  // statistic metadata (<properties>)
  write_properties(m_sample_files, m_statistics_index);

  // target description (<chip>)
  write_target_spec();

  // list of binaries and function symbols referenced by this profile
  write_binaries();

  // list of processes, with call-graph for each one
  write_processes();

  // </statistics>
  outdent();
  XMLUtils::write_element_end(m_out, indentation(), STATISTICS);
}


// --------------------------------------------------------------------------
// "<properties>" functions
// --------------------------------------------------------------------------

/** Map from OProfile event names to intervals */
typedef Map<string, long> EventNameIntervalMap;

/** Writes the "&lt;properties&gt;" XML element */
void OutputWriter::write_properties(
  const SampleFileVector& sample_files,
  const StatisticDescriptorIndex& statistics_index)
{
  // create a copy of loaded metadata index,
  // so we can delete unnecessary properties
  StatisticDescriptorIndex index(statistics_index);

  // collect list of OProfile EVENT_NAMES from sample files we've seen;
  // also, collect event interval for ONE samples
  // (this becomes the multiplier
  // for the estimated "cycles" statistic)
  // TODO: decide what to do if we find ONE event intervals that don't match;
  // for now the first one we find wins
  EventNameVector      event_names;
  EventNameIntervalMap event_intervals;
  FOR_EACH(const_iterator, i, SampleFileVector, sample_files) {
    const SampleFile& sample_file = *i;
    const string event_name = sample_file.pathname().event_name();
    long event_interval = sample_file.pathname().event_interval();
    if (! event_names.contains(event_name)) {
      event_names.add(event_name);
      event_intervals.add(event_name, event_interval);
    }
  }

  // now prune statistic properties we don't need, that is,
  // remove any event-based statistic property that has an EVENT_NAME
  // that is not in the list we've just collected
  index.remove_unused_event_stats(event_names);

  // update the interval counts of any event statistics
  FOR_EACH(const_iterator, i, EventNameIntervalMap, event_intervals) {
    string name     = i->first;
    long   interval = i->second;
    StatisticDescriptor* event_stat = index.event_statistic(name);
    if (event_stat != NULL) {
      event_stat->set_count(interval);
    }
  }

  // if we found a ONE interval, use it to set the multiplier
  // on the "Cycles (Estimated)" stat;
  // this estimates the number of cycles by multiplying the number
  // of samples by this interval
  long one_event_interval = event_intervals.get("ONE", 0);
  if (one_event_interval > 0) {
    cout << "Found 'ONE' event, with event interval: " 
         << one_event_interval << endl;
    StatisticDescriptor* cycles_stat = index.statistic("cycles");
    if (cycles_stat != NULL) {
      string method = string("mul:") + to_string(one_event_interval)
        + ",samples";
      cycles_stat->set_method(method);
      cout << "Set multiplier for '" << cycles_stat->display_name()
           << "' statistic " << "to: " << one_event_interval << endl;
    }
  }

  // now generate statistic properties XML from the pruned list
  const string PROPERTIES = "properties";
  XMLUtils::write_element_start(m_out, indentation(), PROPERTIES);

  indent();
  index.print(m_out, indentation(), indentation_delta());
  outdent();

  XMLUtils::write_element_end(m_out, indentation(), PROPERTIES);
}


// --------------------------------------------------------------------------
// "<chip>" (target description) functions
// --------------------------------------------------------------------------

/** Writes description of target, obtained from monitor metadata */
void OutputWriter::write_target_spec() {

  if (m_target_spec == NULL) return;

  XMLElement* chip_element = m_target_spec->element_by_path("chip");
  if (chip_element == NULL) {
    cerr << "Could not get 'chip' element from monitor metadata." << endl;
    return;
  }

  // <chip>...</chip>
  XMLPrinter(indentation(), indentation_delta()).print(m_out, *chip_element);

  // While we're here, cache the chip width we'll use later
  // in calculating tile coordinates.
  m_tile_width = chip_element->int_attribute("chip_width", 8);

}


// --------------------------------------------------------------------------
// "<binaries>" functions
// --------------------------------------------------------------------------


/** Writes the "&lt;binaries&gt;" XML element */
void OutputWriter::write_binaries()
{
  const string BINARIES = "binaries";
  XMLUtils::write_element_start(m_out, indentation(), BINARIES);
  indent();

  // Iterate over the "Binary"s.
  FOR_EACH(const_iterator, it, BinaryPtrVector, m_binaries) {
    write_binary(**it);
  }

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), BINARIES);
}

/** Writes a "&lt;binary&gt;" XML element */
void OutputWriter::write_binary(const Binary& binary)
{
  const string BINARY = "binary";
  XMLUtils::write_element_start(m_out, indentation(), BINARY,
                                "label",      binary.remote_path(),
                                "path",       binary.remote_path(),
                                "local_path", binary.local_path(),
                                "type",       binary.type(), true);
  indent();

  write_functions(binary);

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), BINARY);
}

/** Writes the "&lt;functions&gt;" XML element */
void OutputWriter::write_functions(const Binary& binary)
{
  const string FUNCTIONS = "functions";
  XMLUtils::write_element_start(m_out, indentation(), FUNCTIONS);
  indent();

  // Iterate over SampleFiles that have this binary as the "from" binary.
  const SampleFilePtrVector& from_sample_files = binary.from_sample_files();
  int count = from_sample_files.size();
  for (int i = 0; i < count; ++i) {
    SampleFilePtr sample_file_ptr = from_sample_files[i];
    // Iterate over the "from" SampleLocations in this SampleFile.
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    int sampleCount = samples.size();
    for (int j = 0; j < sampleCount; ++j) {
      write_function_for_location_if_necessary(samples[j].from_location());
    }
  }

  // Iterate over the SampleFiles which have this binary as the "to" binary.
  const SampleFilePtrVector& to_sample_files = binary.to_sample_files();
  count = to_sample_files.size();
  for (int i = 0; i < count; ++i) {
    SampleFilePtr sample_file_ptr = to_sample_files[i];
    // Iterate over the "to" SampleLocations in this SampleFile.
    const SampleFile::SampleList& samples = sample_file_ptr->samples();
    int sampleCount = samples.size();
    for (int j = 0; j < sampleCount; ++j) {
      write_function_for_location_if_necessary(samples[j].to_location());
    }
  }

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), FUNCTIONS);
}

/**
 * Writes the "&lt;function&gt;" XML element for the given SampleLocation,
 * if the function is known for the SampleLocation and if the XML element
 * for the function hasn't already been written
 */
void OutputWriter::write_function_for_location_if_necessary(
  const SampleLocation& loc)
{
  if (loc.symbol_found()) {
    SymbolPtr symbol = loc.symbol();
    if ( ! symbol->was_written_to_xml() ) {
      write_function(symbol);
      symbol->set_written_to_xml();
    }
  }
}

/** Writes a "&lt;function&gt;" XML element */
void OutputWriter::write_function(SymbolPtr symbol)
{
  XMLUtils::write_complete_element(m_out, indentation(), "function",
                                   "id",   symbol->unique_id(),
                                   "name", symbol->name(),
                                   "size", symbol->size(),
                                   false);
}


// --------------------------------------------------------------------------
// "<processes>" functions
// --------------------------------------------------------------------------

/** Writes the "&lt;processes&gt;" XML element */
void OutputWriter::write_processes()
{
  const string PROCESSES = "processes";
  XMLUtils::write_element_start(m_out, indentation(), PROCESSES);
  indent();

  // Iterate over the "Process"s.
  int count = m_processes.size();
  for (int i = 0; i < count; ++i) {
    write_process(*m_processes[i]);
  }

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), PROCESSES);
}

/** Writes a "&lt;process&gt;" XML element */
void OutputWriter::write_process(Process& process)
{
  const string PROCESS = "process";
  XMLUtils::write_element_start(m_out, indentation(), PROCESS,
                                "id", process.id(),
                                "linux_process_id", 
                                process.linux_process_id(),
                                "linux_thread_id",
                                process.linux_thread_id(),
                                "session", process.session_number(),
                                "path", process.executable_path(), true);
  indent();

  // write_arguments();
  write_tiles(process);

  // write call tree (i.e. <frame> nodes)
  write_call_tree(process);

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), PROCESS);
}

/** Writes a "&lt;tiles&gt;" XML element */
void OutputWriter::write_tiles(Process& process)
{
  const string TILES = "tiles";
  XMLUtils::write_element_start(m_out, indentation(), TILES);
  indent();

  CpuNumberVector cpu_numbers = process.cpu_numbers();
  int count = cpu_numbers.size();

  if ( ! g_minimize_white_space ) {
    m_out << indent_spaces(indentation()) << "<!-- CPU numbers: ";
    for (int i = 0; i < count; ++i) {
      int cpu_number = cpu_numbers[i];
      m_out << cpu_number << ' ';
    }
    m_out << "-->" << endl;
  }

  for (int i = 0; i < count; ++i) {
    write_tile(cpu_numbers[i]);
  }

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), TILES);
}

/** Writes a "&lt;tile&gt;" XML element */
void OutputWriter::write_tile(int cpu_number)
{
  // NOTE: We got the m_tile_width value while
  // writing out the target description from
  // the monitor.xml file.

  int x = cpu_number % m_tile_width;
  int y = cpu_number / m_tile_width;
  XMLUtils::write_complete_element(m_out, indentation(),
                                   "tile", "x", x, "y", y, false);
}

/** Writes a "&lt;call_tree&gt;" XML element */
void OutputWriter::write_call_tree(Process& process)
{
  const string CALL_TREE = "call_tree";
  XMLUtils::write_element_start(m_out, indentation(), CALL_TREE);
  indent();
  
  // Temporarily construct the call graph for this process
  // by adding self values and edges to call graph nodes
  process.construct_call_graph();
  
  // Write the "frame" elements, starting with the root nodes
  write_frame_elements(process);
  
  // Remove the process's call graph information
  process.remove_call_graph();

  outdent();
  XMLUtils::write_element_end(m_out, indentation(), CALL_TREE);
}

/**
 * Writes the top-level "&lt;frame&gt;" XML elements (and their children)
 * for the given process' root nodes in the call graph
 */
void OutputWriter::write_frame_elements(Process& process)
{
  // current root nodes left to process
  CallGraphNodePtrVector root_nodes;

  // find root nodes, or failing that, break a cycle to create a root node
  process.find_root_nodes_or_cycle(root_nodes);

  while (!root_nodes.empty()) {
    // For each root node in the call graph, build a Frame-based tree,
    // write that to the XML as nested <frame> elements.
    int root_node_count = root_nodes.size();
    for (int i = 0; i < root_node_count; ++i) {

      // Build a nested Frame tree, rooted at this iteration's root node.
      Frame root_frame(root_nodes[i]);
      root_frame.build_call_tree();

      // Write that Frame tree to the XML output.
      write_frame(root_frame);
    }
    
    // Clear "root_nodes" since we've processed all its nodes.
    root_nodes.clear();

    // To make things more efficient, we can go through
    // and discard any sample edges for which the sample count
    // has been totally "consumed" (set to zero) by the above
    process.remove_zero_edges();

    // again find root nodes, or failing that, break a cycle
    // to create a root node, and repeat
    process.find_root_nodes_or_cycle(root_nodes);
  }
}

/** Writes the given "&lt;frame&gt;" XML element and its children, if any */
void OutputWriter::write_frame(Frame& frame)
{
  const string FRAME       = "frame";
  const string PROCESSOR   = "processor";
  const string CACHE       = "cache";
  const string IMESH       = "imesh";
  const string LOCATION    = "location";
  const string FUNCTION_ID = "function_id";
  const string PC          = "pc";
  const string FILE        = "file";
  const string LINE        = "line";

  // See how many children there are.
  Vector<Frame*>& children = frame.children();
  int child_count = children.size();
  bool has_children = (child_count != 0);
  
  // Get the frame's SampleLocation.
  const SampleLocation& loc = frame.call_graph_node()->sample_location();
  
  // Create a vector to hold this <frame>s attributes.
  XMLAttributeVector attributes;
  const int MAX_ATTRIBUTE_COUNT = 5;
  attributes.reserve(MAX_ATTRIBUTE_COUNT);
  
  if (g_show_location_attributes_on_frame_elements) {
    attributes.add(LOCATION, loc.to_debug_string());
  }
  
  if (loc.known_source_file_and_line()) {
    attributes.add(FUNCTION_ID, loc.symbol()->unique_id());
    attributes.add(FILE, loc.path().to_string());
    attributes.add(LINE, loc.line());
  }
  else if (loc.symbol_found()) {
    attributes.add(FUNCTION_ID, loc.symbol()->unique_id());
  }

  // Add "pc" (as a hex string).
  std::ostringstream pc_oss;
  pc_oss << std::showbase << std::hex << loc.vma();
  attributes.add(PC, pc_oss.str());

  // Add attributes for any hardware events on the <frame> element.
  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0)
      attributes.add(stat->name(), count);
  }

  // Check which elements we have statistics for.
  bool processor_samples = false;
  bool processor_stall_samples = false;
  bool processor_user_event_samples = false;
  bool cache_samples = false;
  bool imesh_samples = false;

  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_processor_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0) {
      processor_samples = true;
      break;
    }
  }

  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_stall_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0) {
      processor_stall_samples = true;
      break;
    }
  }

  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_user_event_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0) {
      processor_user_event_samples = true;
      break;
    }
  }

  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_cache_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0) {
      cache_samples = true;
      break;
    }
  }

  FOR_EACH(const_iterator, i, StatisticDescriptorVector,
           m_statistics_index.get_frame_imesh_statistics())
  {
    StatisticDescriptor* stat = (*i);
    unsigned long count = frame.get_self_count(stat->event());
    if (count > 0) {
      imesh_samples = true;
      break;
    }
  }

  bool any_processor_samples =
    processor_samples || processor_stall_samples || processor_user_event_samples;

  bool frame_element_has_children =
    has_children || any_processor_samples || cache_samples || imesh_samples;


  // Write the <frame> element.
  XMLUtils::write_element(m_out, indentation(), FRAME,
                          ! frame_element_has_children, attributes, false);

  // Write the <processor> samples, if any.
  if (any_processor_samples) {
    indent();

    if (processor_samples) {
      indent();
      attributes.clear();

      FOR_EACH(const_iterator, i, StatisticDescriptorVector,
               m_statistics_index.get_frame_processor_statistics())
      {
        StatisticDescriptor* stat = (*i);
        unsigned long count = frame.get_self_count(stat->event());
        if (count > 0)
          attributes.add(stat->name(), count);
      }

      XMLUtils::write_element_start(m_out, indentation(), PROCESSOR,
                                    attributes, true);
      outdent();
    }
    else
    {
      XMLUtils::write_element_start(m_out, indentation(), PROCESSOR);
    }

    // Write the <stalls> samples, if any.
    if (processor_stall_samples) {
      indent();
      attributes.clear();

      FOR_EACH(const_iterator, i, StatisticDescriptorVector,
               m_statistics_index.get_frame_stall_statistics())
      {
        StatisticDescriptor* stat = (*i);
        unsigned long count = frame.get_self_count(stat->event());
        if (count > 0)
          attributes.add(stat->name(), count);
      }

      XMLUtils::write_complete_element(m_out, indentation(),
                                       "stalls", attributes, true);
      outdent();
    }

    // Write the <user_events> samples, if any.
    if (processor_user_event_samples) {
      indent();
      attributes.clear();

      FOR_EACH(const_iterator, i, StatisticDescriptorVector,
               m_statistics_index.get_frame_user_event_statistics())
      {
        StatisticDescriptor* stat = (*i);
        unsigned long count = frame.get_self_count(stat->event());
        if (count > 0)
          attributes.add(stat->name(), count);
      }

      XMLUtils::write_complete_element(m_out, indentation(),
                                       "user_events", attributes, true);
      outdent();
    }

    XMLUtils::write_element_end(m_out, indentation(), PROCESSOR);
    outdent();
  }

  // Write the <cache> samples, if any.
  if (cache_samples) {
    indent();
    attributes.clear();

    FOR_EACH(const_iterator, i, StatisticDescriptorVector,
             m_statistics_index.get_frame_cache_statistics())
    {
      StatisticDescriptor* stat = (*i);
      unsigned long count = frame.get_self_count(stat->event());
      if (count > 0)
        attributes.add(stat->name(), count);
    }

    XMLUtils::write_complete_element(m_out, indentation(),
                                     CACHE, attributes, true);
    outdent();
  }

  // Write the <imesh> samples, if any.
  if (imesh_samples) {
    indent();
    attributes.clear();

    FOR_EACH(const_iterator, i, StatisticDescriptorVector,
             m_statistics_index.get_frame_imesh_statistics())
    {
      StatisticDescriptor* stat = (*i);
      unsigned long count = frame.get_self_count(stat->event());
      if (count > 0)
        attributes.add(stat->name(), count);
    }

    XMLUtils::write_complete_element(m_out, indentation(),
                                     IMESH, attributes, true);
    outdent();
  }

  if (has_children) {
    indent();
    for (int i = 0; i < child_count; ++i)
      write_frame(*children[i]);
    outdent();
  }

  if (frame_element_has_children)
    XMLUtils::write_element_end(m_out, indentation(), FRAME);
}
