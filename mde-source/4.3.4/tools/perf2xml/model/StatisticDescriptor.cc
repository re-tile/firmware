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
// StatisticDescriptor.cc -- profiling statistic descriptor
// ==========================================================================

// custom includes
#include "StatisticDescriptor.h"  // header file


// -------------------------------------------------------------------------
// StatisticDescriptor
// -------------------------------------------------------------------------

// --- constructors/destructors ---

/** Destructor */
StatisticDescriptor::~StatisticDescriptor()
{
  FOR_EACH(iterator, i, StatisticDescriptorArray, m_children)
  {
    delete *i;
  }
  m_children.clear();
  m_parent = NULL;
}


// --- accessors ---

/** Gets name: prefix of method */
const std::string StatisticDescriptor::get_method_name() const
{
  std::string name, args;
  split_string(m_method, ':', name, args);
  return name;
}

/** Gets arguments, if any, of method */
const StringArray StatisticDescriptor::get_method_arguments() const
{
  std::string name, args;
  split_string(m_method, ':', name, args);
  StringArray result;
  split_string(args, ',', result);
  return result;
}


// --- parent/child management ---

/** Sets parent of descriptor */
void StatisticDescriptor::set_parent(StatisticDescriptor* parent)
{
  if (m_parent != NULL) m_parent->m_children.remove(this);
  m_parent = parent;
  if (m_parent != NULL) m_parent->m_children.add(this);
}

/** Gets parent of descriptor */
const StatisticDescriptor* StatisticDescriptor::get_parent() const
{
  return m_parent;
}

/** Adds child descriptor */
void StatisticDescriptor::add_child(StatisticDescriptor* child)
{
  if (child->get_parent() != this) child->set_parent(this);
}

/** Removes child descriptor */
void StatisticDescriptor::remove_child(StatisticDescriptor* child)
{
  if (child->get_parent() == this) child->set_parent(NULL);
}

/** Returns true if statistic descriptor has any child stats */
bool StatisticDescriptor::has_children() const
{
  return (! m_children.empty());
}

/** Returns list of children */
StatisticDescriptorArray& StatisticDescriptor::get_children()
{
  return m_children;
}

/** Returns list of children */
const StatisticDescriptorArray& StatisticDescriptor::get_children() const
{
  return m_children;
}

