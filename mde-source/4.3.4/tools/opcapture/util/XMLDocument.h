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
// XMLDocument.h -- XMLDocument header
// ============================================================================

// multiple-inclusion guard
#ifndef XMLDOCUMENT_H
#define XMLDOCUMENT_H

// XML includes
#include "XMLElement.h"    // XMLElement
#include "io_utils.h"      // IO streams and utilities


// ----------------------------------------------------------------------------
// XMLDocument
// ----------------------------------------------------------------------------

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
  XMLDocument(const std::string root_name) : m_root(NULL)
  {
    create_root(root_name);
  }

  /** Constructor */
  XMLDocument(const XMLElement* root) :
    m_root(new XMLElement(root))
  {}

  /** Destructor */
  ~XMLDocument()
  {
    if (m_root != NULL)
    {
      delete m_root;
      m_root = NULL;
    }
  }


  // --- to_string methods ---
public:
  /** Returns human-readable string representation of object */
  const std::string to_string() const;

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
  XMLElement* get_root() const
  {
    return m_root;
  }

  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name)
  {
    return create_element(name);
  }
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1);
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2);
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3);
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4);
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5);
  
  /** Adds a new root element to the document, with the specified name.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6);

  
  /** Adds a new root element to the document, with the specified name
   *  and attributes.
   *  If document already had a root element,
   *  the old root element is automatically deleted.
   */
  XMLElement* create_root(
    const std::string& name,
    const XMLAttributeMap& attributes)
  {
    return create_element(name, attributes);
  }
  

  // --- element management methods ---
public:
  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const std::string& attr1, const std::string& value1,
    const std::string& attr2, const std::string& value2,
    const std::string& attr3, const std::string& value3,
    const std::string& attr4, const std::string& value4,
    const std::string& attr5, const std::string& value5,
    const std::string& attr6, const std::string& value6,
    XMLElement* parent = NULL);

  /** Adds a new element to the document, with the specified name
   *  and attributes.
   *  If parent argument is NULL (the default), the newly-created element
   *  becomes the root element of the document. If document already had
   *  a root element, the old root element is automatically deleted.
   */
  XMLElement* create_element(
    const std::string& name,
    const XMLAttributeMap& attributes,
    XMLElement* parent = NULL);


  // --- element access methods ---
public:

  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(XMLElement* context,
                            const std::string& name1,
                            XMLElementArray& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(const std::string& name1,
                            XMLElementArray& result) const;


  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(XMLElement* context,
                            const std::string& name1,
                            const std::string& name2,
                            XMLElementArray& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(const std::string& name1,
                            const std::string& name2,
                            XMLElementArray& result) const;


  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(XMLElement* context,
                            const std::string& name1,
                            const std::string& name2,
                            const std::string& name3,
                            XMLElementArray& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(const std::string& name1,
                            const std::string& name2,
                            const std::string& name3,
                            XMLElementArray& result) const;


  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(XMLElement* context,
                            const std::string& name1,
                            const std::string& name2,
                            const std::string& name3, 
                            const std::string& name4,
                            XMLElementArray& result) const;
  /** Returns elements, if any, reachable by specified path,
      starting at specified context element. */
  void get_elements_by_path(const std::string& name1,
                            const std::string& name2,
                            const std::string& name3,
                            const std::string& name4,
                            XMLElementArray& result) const;


  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(XMLElement* context,
                                  const std::string& name1) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(const std::string& name1) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(XMLElement* context,
                                  const std::string& name1,
                                  const std::string& name2) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(const std::string& name1,
                                  const std::string& name2) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(XMLElement* context,
                                  const std::string& name1,
                                  const std::string& name2,
                                  const std::string& name3) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(const std::string& name1,
                                  const std::string& name2,
                                  const std::string& name3) const;

  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(XMLElement* context,
                                  const std::string& name1,
                                  const std::string& name2,
                                  const std::string& name3,
                                  const std::string& name4) const;
  /** Returns element, if any, reachable by specified path,
      starting at specified context element. */
  XMLElement* get_element_by_path(const std::string& name1,
                                  const std::string& name2,
                                  const std::string& name3,
                                  const std::string& name4) const;

};

// multiple-inclusion guard
#endif
