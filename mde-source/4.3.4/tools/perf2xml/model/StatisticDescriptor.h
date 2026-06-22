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
// StatisticDescriptor.h -- profiling statistic descriptor
// ==========================================================================

// multiple-inclusion guard
#ifndef STATISTICDESCRIPTOR_H
#define STATISTICDESCRIPTOR_H

// C/C++ includes

// custom includes
#include "io_utils.h"       // IO streams
#include "Pathname.h"       // Unix pathnames
#include "collections.h"    // collections, FOR_EACH macro
#include "xml.h"            // XML support


// -------------------------------------------------------------------------
// StatisticDescriptor
// -------------------------------------------------------------------------

// list of decriptors
typedef Array<class StatisticDescriptor*> StatisticDescriptorArray;

// mapping from name to descriptor
typedef Map<std::string, class StatisticDescriptor*> StatisticDescriptorMap;


/** Description of a profiling statistic. */
class StatisticDescriptor
{
  // --- members ---
private:  
  /** internal (non-localized) name */
  std::string m_name;

  /** display (localized) name */
  std::string m_display_name;

  /** tooltip (localized) description string */
  std::string m_description;

  /** data type of stat value
   *  (boolean, int, long, double, float, string)
   */
  std::string m_type;

  /** method used to read/calculate the statistic */
  std::string m_method;


  /** OProfile EVENT_NAME (if any) associated with this statistic */
  std::string m_event;

  /** Event ID, if known. */
  int m_event_id;

  /** for EVENT_NAME, user-specified "count" value, if known */
  int m_count;

  /** for EVENT_NAME, default "count" value to use */
  int m_default_count;

  /** for EVENT_NAME statistics,
      minimum recommended "count" value for statistic */
  int m_minimum_count;


  /** parent statistic, if any */
  StatisticDescriptor* m_parent;

  /** nested StatisticDescriptors, if any */
  StatisticDescriptorArray m_children;


  // --- constructors/destructors ---
public:
  
  /** Constructor */
  StatisticDescriptor(const std::string& name,
                      const std::string& display_name,
                      const std::string& description,
                      const std::string& type,
                      const std::string& method) :
    m_name(name),
    m_display_name(display_name),
    m_description(description),
    m_type(type),
    m_method(method),
    m_parent(NULL)
  {}

  /** Constructor */
  StatisticDescriptor(const std::string& name,
                      const std::string& display_name,
                      const std::string& description,
                      const std::string& type,
                      const std::string& method,
                      const std::string& event,
                              int event_id = 0,
                      int minimum_count = 0,
                      int default_count = 0) :
    m_name(name),
    m_display_name(display_name),
    m_description(description),
    m_type(type),
    m_method(method),
    m_event(event),
    m_event_id(event_id),
    m_count(default_count), // note: count defaults to default_count
    m_default_count(default_count),
    m_minimum_count(minimum_count),
    m_parent(NULL)
  {}

  /** Copy constructor */
  StatisticDescriptor(const StatisticDescriptor& desc) :
    m_name(desc.m_name),
    m_display_name(desc.m_display_name),
    m_description(desc.m_description),
    m_type(desc.m_type),
    m_method(desc.m_method),
    m_event(desc.m_event),
    m_event_id(desc.m_event_id),
    m_count(desc.m_count),
    m_default_count(desc.m_default_count),
    m_minimum_count(desc.m_minimum_count),
    m_parent(NULL)
  {
    copy_children(desc);
  }
  
  /** Copy constructor */
  StatisticDescriptor(const StatisticDescriptor* desc) :
    m_name(desc->m_name),
    m_display_name(desc->m_display_name),
    m_description(desc->m_description),
    m_type(desc->m_type),
    m_method(desc->m_method),
    m_event(desc->m_event),
    m_event_id(desc->m_event_id),
    m_count(desc->m_count),
    m_default_count(desc->m_default_count),
    m_minimum_count(desc->m_minimum_count),
    m_parent(NULL)
  {
    copy_children(*desc);
  }

protected:
  /** Copies children from specified descriptor */
  void copy_children(const StatisticDescriptor& desc)
  {
    FOR_EACH(const_iterator, i, StatisticDescriptorArray, desc.m_children)
    {
      StatisticDescriptor* child_copy = new StatisticDescriptor(*i);
      child_copy->set_parent(this);
    }
  }

public:  
  /** Destructor */
  ~StatisticDescriptor();


  // --- accessors ---
public:

  /** Gets internal (non-localized) name */
  const std::string& get_name() const
  {
    return m_name;
  }
  /** Sets internal (non-localized) name */
  void set_name(const std::string& name) 
  {
    m_name = name;
  }

  /** Gets display (localized) name */
  const std::string& get_display_name() const
  {
    return m_display_name;
  }
  /** Sets display (localized) name */
  void set_display_name(const std::string& display_name)
  {
    m_display_name = display_name;
  }

  /** Gets tooltip (localized) description text */
  const std::string& get_description() const
  {
    return m_description;
  }
  /** Sets tooltip (localized) description text */
  void set_description(const std::string& description)
  {
    m_description = description;
  }

  /** Gets data type of statistic value
   *  (boolean, int, long, float, double, string)
   */
  const std::string& get_type() const
  {
    return m_type;
  }
  /** Sets data type of statistic value
   *  (boolean, int, long, float, double, string)
   */
  void set_type(const std::string& type)
  {
    m_type = type;
  }

  /** Gets method used to look up or calculate the statistic */
  const std::string& get_method() const
  {
    return m_method;
  }
  /** Sets method used to look up or calculate the statistic */
  void set_method(const std::string& method)
  {
    m_method = method;
  }

  /** Gets name: prefix of method */
  const std::string get_method_name() const;

  /** Gets arguments, if any, of method */
  const StringArray get_method_arguments() const;

  /** Whether this statistic is associated with an OProfile EVENT_NAME */
  bool is_event() const
  {
    return (! m_event.empty());
  }

  /** OProfile EVENT_NAME, if any, associated with this statistic */
  const std::string& get_event_name() const
  {
    return m_event;
  }

  /** Event ID, if known. */
  int get_event_id() const
  {
    return m_event_id;
  }

  /** Set Event ID. */
  void set_event_id(int event_id)
  {
    m_event_id = event_id;
  }

  /** for EVENT_NAME, user-specified "count" value */
  const int& get_count() const
  {
    return m_count;
  }
  /** for EVENT_NAME, sets user-specified "count" value */
  void set_count(int count)
  {
    m_count = count;
  }

  /** for EVENT_NAME, default "count" value to use */
  const int& get_default_count() const
  {
    return m_default_count;
  }

  /** for EVENT_NAME statistics,
      minimum recommended "count" value for statistic */
  const int& get_minimum_count() const
  {
    return m_minimum_count;
  }


  // --- parent/child management ---
public:
  /** Sets parent of descriptor */
  void set_parent(StatisticDescriptor* parent);

  /** Gets parent of descriptor */
  const StatisticDescriptor* get_parent() const;

  /** Adds child descriptor */
  void add_child(StatisticDescriptor* child);

  /** Removes child descriptor */
  void remove_child(StatisticDescriptor* child);

  /** Returns true if statistic descriptor has any child stats */
  bool has_children() const;

  /** Returns list of children */
  StatisticDescriptorArray& get_children();

  /** Returns list of children */
  const StatisticDescriptorArray& get_children() const;


  // --- display ---
public:
  /** Writes XML for statistic descriptor and its children
      to specified stream. */
  template<typename charT, typename traits> \
    void print(std::basic_ostream<charT, traits>& out,
               int indent = -1, int step = 2) const
  {
    XMLElement* element = to_xml();
    out << element;
    delete element;
  }

  /** Converts statistic descriptor and its children to XML. */
  XMLElement* to_xml(XMLElement* parent = NULL) const
  {
    const std::string STATISTIC = "statistic";
    XMLElement* element = new XMLElement(STATISTIC, parent);

    element->set_attribute("name", m_name);
    element->set_attribute("display", m_display_name);
    element->set_attribute("description", m_description);

    if (m_type != "")
    {
      element->set_attribute("type", m_type);
      element->set_attribute("method", m_method);
    }

    if (is_event())
    {
      element->set_attribute("event", m_event);
      element->set_int_attribute("count", m_count);
      /*
      element->set_attribute("min_count", m_minimum_count);
      element->set_attribute("default_count", m_default_count);
      */
    }

    if (! m_children.empty())
    {
      FOR_EACH(const_iterator, it, StatisticDescriptorArray, m_children)
      {
        StatisticDescriptor* sd = *it;
        sd->to_xml(element);
      }
    }

    return element;
  }
};

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, StatisticDescriptor, s)
{
  s.print(out);
  return out;
}

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, StatisticDescriptor*, s)
{
  s->print(out);
  return out;
}

// multiple-inclusion guard
#endif

