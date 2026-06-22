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

// ============================================================================
// Event.cc -- Event class
// ============================================================================

#include "Event.h"

// C/C++ includes

// custom includes
#include "string_utils.h" // C/C++ string utilities


// ----------------------------------------------------------------------------
// Event
// ----------------------------------------------------------------------------

// --- members ---


// --- constructors/destructors ---

/** Constructor. */
Event::Event(const std::string& name,
             const std::string& display_name,
             const std::string& description,
             const std::string& hardware_event_name,
             const std::string& method,
             const std::string& type,
             const std::string& categories,
             long interval,
             int mask) :
  m_id(-1),
  m_name(name),
  m_display_name(display_name),
  m_description(description),
  m_hardware_event_name(hardware_event_name),
  m_method(method),
  m_type(type),
  m_categories(categories),
  m_interval(interval),
  m_mask(mask)
{
}

/** Copy constructor. */
Event::Event(const Event& event) :
  m_id(event.m_id),
  m_name(event.m_name),
  m_display_name(event.m_display_name),
  m_description(event.m_description),
  m_hardware_event_name(event.m_hardware_event_name),
  m_method(event.m_method),
  m_type(event.m_type),
  m_categories(event.m_categories),
  m_interval(event.m_interval),
  m_mask(event.m_mask)
{
}

/** Copy constructor. */
Event::Event(const Event* event) :
  m_id(event->m_id),
  m_name(event->m_name),
  m_display_name(event->m_display_name),
  m_description(event->m_description),
  m_hardware_event_name(event->m_hardware_event_name),
  m_method(event->m_method),
  m_type(event->m_type),
  m_categories(event->m_categories),
  m_interval(event->m_interval),
  m_mask(event->m_mask)
{
}

/** Assignment operator. */
const Event&
Event::operator=(const Event& event)
{
  if (&event != this) // self-assignment guard
  {
    m_id                  = event.m_id;
    m_name                = event.m_name;
    m_display_name        = event.m_display_name;
    m_description         = event.m_description;
    m_hardware_event_name = event.m_hardware_event_name;
    m_method              = event.m_method;
    m_type                = event.m_type;
    m_categories          = event.m_categories;
    m_interval            = event.m_interval;
    m_mask                = event.m_mask;
  }
  return *this;
}

/** Assignment operator. */
const Event&
Event::operator=(const Event* event)
{
  return operator=(*event);
}

/** Destructor. */
Event::~Event()
{}


// --- object methods ---

/** Equality test. */
bool
Event::equals(const Event& event) const
{
  return(
    m_id                   == event.m_id                   &&
    m_name                 == event.m_name                 &&
    m_hardware_event_name  == event.m_hardware_event_name
  );
}

/** Equality operator. */
bool
Event::operator==(const Event& event) const
{
  return equals(event);
}

/** Comparison operator. */
bool
Event::less_than(const Event& event) const
{
  // We only use this for sorting events.
  // Events are sorted by their assigned IDs.
  return m_id < event.m_id;
}

/** Comparison operator. */
bool
Event::operator<(const Event& event) const
{
  return less_than(event);
}


// --- accessors ---

/** Gets id. */
int
Event::get_id() const
{
  return m_id;
}
/** Sets id. */
void
Event::set_id(int id)
{
  m_id = id;
}


/** Gets internal name. */
const std::string
Event::get_name() const
{
  return m_name;
}

/** Gets display name. */
const std::string
Event::get_display_name() const
{
  return m_display_name;
}

/** Gets description. */
const std::string
Event::get_description() const
{
  return m_description;
}

/** Gets hardware event name. */
const std::string
Event::get_hardware_event_name() const
{
  return m_hardware_event_name;
}

/** Gets method. */
const std::string&
Event::get_method() const
{
  return m_method;
}

/** Gets method name and arguments. */
Array<std::string>
Event::get_method_name_and_arguments() const
{
  // m_method may be in either of the following formats:
  // name(arg,...,arg) -- preferred
  // name:arg,...,arg  -- obsolete
  Array<std::string> result;
  Array<std::string> split;
  split_any_of(m_method, ":,()", split);
  FOR_EACH(iterator, it, Array<std::string>, split)
  {
    std::string& s = *it;
    if (! s.empty()) result.add(s);
  }
  return result;
}

/** Gets method name. */
std::string
Event::get_method_name() const
{
  // m_method may be in either of the following formats:
  // name(arg,...,arg) -- preferred
  // name:arg,...,arg  -- obsolete
  std::string result;
  std::string ignore;
  split_any_of(m_method,":(",result,ignore);
  return result;
}

/** Gets method arguments. */
Array<std::string>
Event::get_method_arguments() const
{
  // m_method may be in either of the following formats:
  // name(arg,...,arg) -- preferred
  // name:arg,...,arg  -- obsolete
  Array<std::string> result;
  std::string ignore;
  std::string arguments;
  if (split_any_of(m_method,":(",ignore,arguments))
  {
    Array<std::string> split;
    split_any_of(arguments, ",)", split);
    FOR_EACH(iterator, it, Array<std::string>, split)
    {
      std::string& s = *it;
      if (! s.empty()) result.add(s);
    }
  }
  return result;
}

/** Gets type. */
const std::string&
Event::get_type() const
{
  return m_type;
}

/** Gets categories. */
const std::string&
Event::get_categories() const
{
  return m_categories;
}


/** Gets interval. */
long
Event::get_interval() const
{
  return m_interval;
}
/** Sets interval. */
void
Event::set_interval(long interval)
{
  m_interval = interval;
}


/** Gets mask. */
int
Event::get_mask() const
{
  return m_mask;
}
/** Sets mask. */
void
Event::set_mask(int mask)
{
  m_mask = mask;
}


// --- methods ---



// --- comparison functions ---

/** Comparison function for Event* type. (Used for sorting arrays) */
bool Event_pointer_less_than(Event* const& e1, Event*const& e2)
{
  return (e1->get_id()) < (e2->get_id());
};

