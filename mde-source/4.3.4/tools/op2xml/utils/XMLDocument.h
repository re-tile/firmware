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

// =========================================================================
// XMLDocument.h -- XMLDocument header
// =========================================================================

// multiple-inclusion guard
#ifndef XMLDOCUMENT_H
#define XMLDOCUMENT_H

// XML includes
#include "XMLElement.h"


// -------------------------------------------------------------------------
// XMLDocument
// -------------------------------------------------------------------------

/** XMLDocument header file */
class XMLDocument
{
  // --- members ---
protected:
  /** Root element of this document */
  XMLElement* m_root;


  // --- constructors ---
public:
  /** Constructor */
  XMLDocument() : m_root(NULL)
  {}

  /** Constructor */
  XMLDocument(const XMLElement* root) :
    m_root(new XMLElement(root))
  {}

  /** Destructor */
  ~XMLDocument()
  {
    if (m_root != NULL) {
      delete m_root;
      m_root = NULL;
    }
  }


  // --- to_string methods ---
public:
  /** Returns human-readable string representation of object */
  const string to_string() const;

  /** Returns string representation as C string */
  const char* c_str() const;


  // --- accessors ---
public:
  /** Sets root element of this document */
  void set_root(XMLElement* root)
  {
    if (m_root != NULL) delete m_root;
    m_root = root;
  }
  /** Gets root element of this document */
  XMLElement* root() const
  {
    return m_root;
  }


  // --- element management methods ---
public:
  /** Adds a new root element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(const string& name,
                             XMLElement* parent = NULL);

  /** Adds a new root element to the document, with the specified
   *  name and attributes.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(const string& name,
                             const XMLAttributeMap& attributes,
                             XMLElement* parent = NULL);


  // --- element access methods ---
public:

  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(XMLElement* context,
                        const string& name1,
                        XMLElementVector& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(const string& name1,
                        XMLElementVector& result) const;

  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(XMLElement* context,
                        const string& name1, const string& name2,
                        XMLElementVector& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(const string& name1, const string& name2,
                        XMLElementVector& result) const;

  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(XMLElement* context,
                        const string& name1, const string& name2,
                        const string& name3,
                        XMLElementVector& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(const string& name1, const string& name2,
                        const string& name3,
                        XMLElementVector& result) const;

  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(XMLElement* context,
                        const string& name1, const string& name2,
                        const string& name3, const string& name4,
                        XMLElementVector& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void elements_by_path(const string& name1, const string& name2,
                        const string& name3, const string& name4,
                        XMLElementVector& result) const;


  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(XMLElement* context,
                              const string& name1) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(const string& name1) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(XMLElement* context,
                              const string& name1,
                              const string& name2) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(const string& name1,
                              const string& name2) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(XMLElement* context,
                              const string& name1,
                              const string& name2,
                              const string& name3) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(const string& name1,
                              const string& name2,
                              const string& name3) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(XMLElement* context,
                              const string& name1,
                              const string& name2,
                              const string& name3,
                              const string& name4) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* element_by_path(const string& name1,
                              const string& name2,
                              const string& name3,
                              const string& name4) const;

};

// multiple-inclusion guard
#endif
