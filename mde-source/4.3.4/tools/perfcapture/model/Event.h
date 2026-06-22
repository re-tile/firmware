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
// Event.h -- Event class
// ============================================================================

// multiple-inclusion guard
#ifndef EVENT_H
#define EVENT_H

// C/C++ includes
#include <string>               // std::string

// custom includes
#include "collections.h"        // collections, FOR_EACH


// ----------------------------------------------------------------------------
// type descriptors
// ----------------------------------------------------------------------------

// fref
class Event;

/** Array of event instances. */
typedef Array<Event> EventArray;


// ----------------------------------------------------------------------------
// Event
// ----------------------------------------------------------------------------

/** Event class. */
class Event
{
  // --- members ---
protected:
  /** Event id. */
  int m_id;

  /** Event internal name. */
  std::string m_name;
  
  /** Event display name. */
  std::string m_display_name;

  /** Event description. */
  std::string m_description;

  /** Hardware event name (if any). */
  std::string m_hardware_event_name;

  /** Event calculation method. */
  std::string m_method;

  /** Event data type. */
  std::string m_type;

  /** Event data type. */
  std::string m_categories;

  /** Event sampling interval. */
  long m_interval;

  /** Event mask, if any. */
  int m_mask;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Event(const std::string& name,
        const std::string& display_name,
        const std::string& description,
        const std::string& hardware_event_name,
        const std::string& method = "",
        const std::string& type = "",
        const std::string& categories = "",
        long interval = -1,
        int mask = -1);

  /** Copy constructor. */
  Event(const Event& event);

  /** Copy constructor. */
  Event(const Event* event);

  /** Assignment operator. */
  const Event& operator=(const Event& event);

  /** Assignment operator. */
  const Event& operator=(const Event* event);

  /** Destructor. */
  ~Event();


  // --- object methods ---
public:

  /** Equality test. */
  bool
  equals(const Event& event) const;

  /** Equality operator. */
  bool
  operator==(const Event& event) const;

  /** Comparison operator. */
  bool
  less_than(const Event& event) const;

  /** Comparison operator. */
  bool
  operator<(const Event& event) const;


  // --- accessors ---
public:
  /** Gets id. */
  int
  get_id() const;

  /** Sets id. */
  void
  set_id(int id);


  /** Gets internal name. */
  const std::string
  get_name() const;

  /** Gets display name. */
  const std::string
  get_display_name() const;

  /** Gets description. */
  const std::string
  get_description() const;

  /** Gets hardware event name. */
  const std::string
  get_hardware_event_name() const;


  /** Gets method. */
  const std::string&
  get_method() const;

  /** Gets method name and arguments. */
  Array<std::string>
  get_method_name_and_arguments() const;

  /** Gets method name. */
  std::string
  get_method_name() const;

  /** Gets method arguments. */
  Array<std::string>
  get_method_arguments() const;

  /** Gets type. */
  const std::string&
  get_type() const;

  /** Gets categories. */
  const std::string&
  get_categories() const;


  /** Gets interval. */
  long
  get_interval() const;

  /** Sets interval. */
  void
  set_interval(long interval);

  /** Gets mask. */
  int
  get_mask() const;

  /** Sets mask. */
  void
  set_mask(int mask);


  // --- methods ---
public:


};


// --- comparison functions ---

/** Comparison function for Event* type. (Used for sorting arrays) */
bool Event_pointer_less_than(Event* const& e1, Event*const& e2);


// multiple-inclusion guard
#endif
