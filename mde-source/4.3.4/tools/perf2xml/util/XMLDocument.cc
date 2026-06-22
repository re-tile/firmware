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
// XMLDocument.cc -- XMLDocument class
// ============================================================================

// custom includes
#include "XMLDocument.h"   // header file
#include "string_utils.h"  // C/C++ strings
#include "collections.h"   // FOR_EACH macro

// ----------------------------------------------------------------------------
// XMLDocument
// ----------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const std::string XMLDocument::to_string() const
{
  std::ostringstream stream;
  stream << this;
  return stream.str();
}

/** Returns string representation as C string */
const char* XMLDocument::c_str() const
{
  return to_string().c_str();
}


// --- accessors ---

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1)
{
  XMLElement* root = create_element(name,
                                    attr1, value1);
  return root;
}

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1,
                         const std::string& attr2, const std::string& value2)
{
  XMLElement* root = create_element(name,
                                    attr1, value1,
                                    attr2, value2);
  return root;
}

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1,
                         const std::string& attr2, const std::string& value2,
                         const std::string& attr3, const std::string& value3)
{
  XMLElement* root = create_element(name,
                                    attr1, value1,
                                    attr2, value2,
                                    attr3, value3);
  return root;
}

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1,
                         const std::string& attr2, const std::string& value2,
                         const std::string& attr3, const std::string& value3,
                         const std::string& attr4, const std::string& value4)
{
  XMLElement* root = create_element(name,
                                    attr1, value1,
                                    attr2, value2,
                                    attr3, value3,
                                    attr4, value4);
  return root;
}

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1,
                         const std::string& attr2, const std::string& value2,
                         const std::string& attr3, const std::string& value3,
                         const std::string& attr4, const std::string& value4,
                         const std::string& attr5, const std::string& value5)
{
  XMLElement* root = create_element(name,
                                    attr1, value1,
                                    attr2, value2,
                                    attr3, value3,
                                    attr4, value4,
                                    attr5, value5);
  return root;
}

/** Adds a new root element to the document, with the specified name.
 *  If document already had a root element, it is replaced by the new one.
 */
XMLElement*
XMLDocument::create_root(const std::string& name,
                         const std::string& attr1, const std::string& value1,
                         const std::string& attr2, const std::string& value2,
                         const std::string& attr3, const std::string& value3,
                         const std::string& attr4, const std::string& value4,
                         const std::string& attr5, const std::string& value5,
                         const std::string& attr6, const std::string& value6)
{
  XMLElement* root = create_element(name,
                                    attr1, value1,
                                    attr2, value2,
                                    attr3, value3,
                                    attr4, value4,
                                    attr5, value5,
                                    attr6, value6);
  return root;
}


// --- element management methods ---

/** Adds a new root element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(const std::string& name,
                                        XMLElement* parent)
{
  XMLElement* element = new XMLElement(name);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  const std::string& attr2, const std::string& value2,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1,
                                       attr2, value2);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  const std::string& attr2, const std::string& value2,
  const std::string& attr3, const std::string& value3,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1,
                                       attr2, value2,
                                       attr3, value3);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  const std::string& attr2, const std::string& value2,
  const std::string& attr3, const std::string& value3,
  const std::string& attr4, const std::string& value4,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1,
                                       attr2, value2,
                                       attr3, value3,
                                       attr4, value4);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  const std::string& attr2, const std::string& value2,
  const std::string& attr3, const std::string& value3,
  const std::string& attr4, const std::string& value4,
  const std::string& attr5, const std::string& value5,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1,
                                       attr2, value2,
                                       attr3, value3,
                                       attr4, value4,
                                       attr5, value5);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(
  const std::string& name,
  const std::string& attr1, const std::string& value1,
  const std::string& attr2, const std::string& value2,
  const std::string& attr3, const std::string& value3,
  const std::string& attr4, const std::string& value4,
  const std::string& attr5, const std::string& value5,
  const std::string& attr6, const std::string& value6,
  XMLElement* parent)
{
  XMLElement* element = new XMLElement(name,
                                       attr1, value1,
                                       attr2, value2,
                                       attr3, value3,
                                       attr4, value4,
                                       attr5, value5,
                                       attr6, value6);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}

/** Adds a new root element to the document, with the specified
 *  name and attributes.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, it is replaced by the new one.
 */
XMLElement* XMLDocument::create_element(const std::string& name,
                                        const XMLAttributeMap& attributes,
                                        XMLElement* parent)
{
  XMLElement* element = new XMLElement(name, attributes);
  if (parent == NULL) set_root(element);
  else element->set_parent(parent);
  return element;
}


// --- element access methods ---

/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(XMLElement* context,
                                       const std::string& name1,
                                       XMLElementArray& result) const
{
  if (context == NULL) return;
  context->get_children_named(name1, result);
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(const std::string& name1,
                                       XMLElementArray& result) const
{
  if (get_root()->get_name() == name1)
  {
    result.add(get_root());
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(XMLElement* context,
                                       const std::string& name1,
                                       const std::string& name2,
                                       XMLElementArray& result) const
{
  if (context == NULL) return;
  XMLElementArray children;
  context->get_children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementArray, children)
  {
    XMLElement* child = *i;
    get_elements_by_path(child, name2, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(const std::string& name1,
                                       const std::string& name2,
                                       XMLElementArray& result) const
{
  if (get_root()->get_name() == name1)
  {
    get_elements_by_path(get_root(), name2, result);
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(XMLElement* context,
                                       const std::string& name1,
                                       const std::string& name2,
                                       const std::string& name3,
                                       XMLElementArray& result) const
{
  if (context == NULL) return;
  XMLElementArray children;
  context->get_children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementArray, children)
  {
    XMLElement* child = *i;
    get_elements_by_path(child, name2, name3, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(const std::string& name1,
                                       const std::string& name2,
                                       const std::string& name3,
                                       XMLElementArray& result) const
{
  if (get_root()->get_name() == name1)
  {
    get_elements_by_path(get_root(), name2, name3, result);
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(XMLElement* context,
                                       const std::string& name1,
                                       const std::string& name2,
                                       const std::string& name3,
                                       const std::string& name4,
                                       XMLElementArray& result) const
{
  if (context == NULL) return;
  XMLElementArray children;
  context->get_children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementArray, children)
  {
    XMLElement* child = *i;
    get_elements_by_path(child, name2, name3, name4, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::get_elements_by_path(const std::string& name1,
                                       const std::string& name2,
                                       const std::string& name3,
                                       const std::string& name4,
                                       XMLElementArray& result) const
{
  if (get_root()->get_name() == name1)
  {
    get_elements_by_path(get_root(), name2, name3, name4, result);
  }
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(XMLElement* context,
                                             const std::string& name1) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->get_child_named(name1);
  return element;
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(const std::string& name1) const
{
  XMLElement* result = NULL;
  if (get_root()->get_name() == name1)
  {
    result = get_root();
  }
  return result;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(XMLElement* context,
                                             const std::string& name1,
                                             const std::string& name2) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->get_child_named(name1);
  return get_element_by_path(element, name2);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(const std::string& name1,
                                             const std::string& name2) const
{
  XMLElement* result = NULL;
  if (get_root()->get_name() == name1)
  {
    result = get_element_by_path(get_root(), name2);
  }
  return result;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(XMLElement* context,
                                             const std::string& name1,
                                             const std::string& name2,
                                             const std::string& name3) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->get_child_named(name1);
  return get_element_by_path(element, name2, name3);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(const std::string& name1,
                                             const std::string& name2,
                                             const std::string& name3) const
{
  XMLElement* result = NULL;
  if (get_root()->get_name() == name1)
  {
    result = get_element_by_path(get_root(), name2, name3);
  }
  return result;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(XMLElement* context,
                                             const std::string& name1,
                                             const std::string& name2,
                                             const std::string& name3,
                                             const std::string& name4) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->get_child_named(name1);
  return get_element_by_path(element, name2, name3, name4);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::get_element_by_path(const std::string& name1,
                                             const std::string& name2,
                                             const std::string& name3,
                                             const std::string& name4) const
{
  XMLElement* result = NULL;
  if (get_root()->get_name() == name1)
  {
    result = get_element_by_path(get_root(), name2, name3, name4);
  }
  return result;
}
