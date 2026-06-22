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
#include "io.h"             // IO streams
#include "Pathname.h"       // Unix pathnames
#include "Vector.h"         // Vector class
#include "Map.h"            // Map class
#include "xml.h"            // XML support
#include "XMLAttribute.h"   // XML attribute support


// -------------------------------------------------------------------------
// StatisticDescriptor
// -------------------------------------------------------------------------

// list of decriptors
typedef Vector<class StatisticDescriptor*> StatisticDescriptorVector;

// mapping from name to descriptor
typedef Map<string, class StatisticDescriptor*> StatisticDescriptorMap;


/** Description of a profiling statistic. */
class StatisticDescriptor
{
  // --- members ---
private:  
  /** internal (non-localized) name */
  string m_name;

  /** display (localized) name */
  string m_display_name;

  /** tooltip (localized) description string */
  string m_description;

  /** data type of stat value
   *  (boolean, int, long, double, float, string)
   */
  string m_type;

  /** method used to read/calculate the statistic */
  string m_method;


  /** OProfile EVENT_NAME (if any) associated with this statistic */
  string m_event;

  /** for EVENT_NAME, user-specified "count" value, if known */
  int m_count;

  /** for EVENT_NAME, default "count" value to use */
  int m_default_count;

  /** for EVENT_NAME statistics, minimum recommended "count" value for statistic */
  int m_minimum_count;


  /** parent statistic, if any */
  StatisticDescriptor* m_parent;

  /** nested StatisticDescriptors, if any */
  StatisticDescriptorVector m_children;


  // --- constructors/destructors ---
public:
  
  /** Constructor */
  StatisticDescriptor(string name, string display_name, string description,
                      string type, string method) :
    m_name(name), m_display_name(display_name), m_description(description),
    m_type(type), m_method(method),
    m_parent(NULL)
  {}

  /** Constructor */
  StatisticDescriptor(string name, string display_name, string description,
                      string type, string method,
                      string event, int minimum_count, int default_count) :
    m_name(name), m_display_name(display_name), m_description(description),
    m_type(type), m_method(method),
    m_event(event), m_count(default_count), // note: count defaults to default_count
    m_default_count(default_count), m_minimum_count(minimum_count),
    m_parent(NULL)
  {}

  /** Copy constructor */
  StatisticDescriptor(const StatisticDescriptor& desc) :
    m_name(desc.m_name), m_display_name(desc.m_display_name), m_description(desc.m_description),
    m_type(desc.m_type), m_method(desc.m_method),
    m_event(desc.m_event), m_count(desc.m_count),
    m_default_count(desc.m_default_count), m_minimum_count(desc.m_minimum_count),
    m_parent(NULL)
  {
    copy_children(desc);
  }
  
  /** Copy constructor */
  StatisticDescriptor(const StatisticDescriptor* desc) :
    m_name(desc->m_name), m_display_name(desc->m_display_name), m_description(desc->m_description),
    m_type(desc->m_type), m_method(desc->m_method),
    m_event(desc->m_event), m_count(desc->m_count),
    m_default_count(desc->m_default_count), m_minimum_count(desc->m_minimum_count),
    m_parent(NULL)
  {
    copy_children(*desc);
  }

protected:
  /** Copies children from specified descriptor */
  void copy_children(const StatisticDescriptor& desc) {
    FOR_EACH(const_iterator, i, StatisticDescriptorVector, desc.m_children) {
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
  const string& name() const
  {
    return m_name;
  }
  /** Sets internal (non-localized) name */
  void set_name(const string& name) 
  {
    m_name = name;
  }

  /** Gets display (localized) name */
  const string& display_name() const
  {
    return m_display_name;
  }
  /** Sets display (localized) name */
  void set_display_name(const string& display_name)
  {
    m_display_name = display_name;
  }

  /** Gets tooltip (localized) description text */
  const string& description() const
  {
    return m_description;
  }
  /** Sets tooltip (localized) description text */
  void set_description(const string& description)
  {
    m_description = description;
  }

  /** Gets data type of statistic value
   *  (boolean, int, long, float, double, string)
   */
  const string& type() const
  {
    return m_type;
  }
  /** Sets data type of statistic value
   *  (boolean, int, long, float, double, string)
   */
  void set_type(const string& type)
  {
    m_type = type;
  }

  /** Gets method used to look up or calculate the statistic */
  const string& method() const
  {
    return m_method;
  }
  /** Sets method used to look up or calculate the statistic */
  void set_method(const string& method)
  {
    m_method = method;
  }

  /** Gets name: prefix of method */
  const string method_name() const;

  /** Gets arguments, if any, of method */
  const StringVector method_arguments() const;

  /** Whether this statistic is associated with an OProfile EVENT_NAME */
  bool is_event() const
  {
    return (! m_event.empty());
  }

  /** OProfile EVENT_NAME, if any, associated with this statistic */
  const string& event() const
  {
    return m_event;
  }

  /** for EVENT_NAME, user-specified "count" value */
  const int& count() const
  {
    return m_count;
  }
  /** for EVENT_NAME, sets user-specified "count" value */
  void set_count(int count)
  {
    m_count = count;
  }

  /** for EVENT_NAME, default "count" value to use */
  const int& default_count() const
  {
    return m_default_count;
  }

  /** for EVENT_NAME statistics, minimum recommended "count" value for statistic */
  const int& minimum_count() const
  {
    return m_minimum_count;
  }


  // --- parent/child management ---
public:
  /** Sets parent of descriptor */
  void set_parent(StatisticDescriptor* parent);

  /** Gets parent of descriptor */
  const StatisticDescriptor* parent() const;

  /** Adds child descriptor */
  void add_child(StatisticDescriptor* child);

  /** Removes child descriptor */
  void remove_child(StatisticDescriptor* child);

  /** Returns true if statistic descriptor has any child stats */
  bool has_children() const;

  /** Returns list of children */
  const StatisticDescriptorVector& children() const;


  // --- display ---
public:
  /** Writes XML for statistic descriptor and its children to specified stream. */
  template<typename charT, typename traits> \
    void print(std::basic_ostream<charT, traits>& out,
	       int indent = -1, int step = 2) const
  {
    const string STATISTIC = "statistic";

    XMLAttributeVector attributes;
    
    attributes.add ("name", m_name);
    attributes.add ("display", m_display_name);
    attributes.add ("description", m_description);

    if (m_type != "") {
      attributes.add ("type", m_type);
      attributes.add ("method", m_method);
    }
    if (is_event()) {
      attributes.add ("event", m_event);
      attributes.add ("count", m_count);
      /*
      attributes.add ("min_count", m_minimum_count);
      attributes.add ("default_count", m_default_count);
      */
    }
    XMLUtils::write_element (out, indent, STATISTIC, m_children.empty(), attributes, true);

    if (! m_children.empty()) {
      FOR_EACH(const_iterator, i, StatisticDescriptorVector, m_children) {
        (*i)->print(out, indent+step, step);
      }
      XMLUtils::write_element_end (out, indent, STATISTIC);
    }
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

