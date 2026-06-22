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
// XMLDocument.cc -- XMLDocument class
// ==========================================================================

// custom includes
#include "XMLDocument.h"   // header file
#include "string_utils.h"  // C/C++ strings
#include "foreach.h"       // FOR_EACH macro

// --------------------------------------------------------------------------
// XMLDocument
// --------------------------------------------------------------------------

// --- to_string methods ---

/** Returns human-readable string representation of object */
const string XMLDocument::to_string() const
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

// --- element management methods ---

/** Adds a new root element to the document, with the specified name.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, the old root element is automatically deleted.
 */
XMLElement* XMLDocument::create_element(const string& name,
                                        XMLElement* parent)
{
  XMLElement* element = new XMLElement(name);
  if (parent == NULL)
    set_root(element);
  else
    element->set_parent(parent);
  return element;
}

/** Adds a new root element to the document, with the specified
 *  name and attributes.
 *  If parent argument is NULL (the default), the newly-created element
 *  becomes the root element of the document. If document already had
 *  a root element, the old root element is automatically deleted.
 */
XMLElement* XMLDocument::create_element(const string& name,
                                        const XMLAttributeMap& attributes,
                                        XMLElement* parent)
{
  XMLElement* element = new XMLElement(name, attributes);
  if (parent == NULL)
    set_root(element);
  else
    element->set_parent(parent);
  return element;
}


// --- element access methods ---

/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(XMLElement* context,
                                       const string& name1,
                                       XMLElementVector& result) const
{
  if (context == NULL) return;
  context->children_named(name1, result);
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(const string& name1,
                                       XMLElementVector& result) const
{
  if (root()->name() == name1) {
    result.add(root());
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(XMLElement* context,
                                   const string& name1,
                                   const string& name2,
                                   XMLElementVector& result) const
{
  if (context == NULL) return;
  XMLElementVector children;
  context->children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementVector, children) {
    XMLElement* child = *i;
    elements_by_path(child, name2, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(const string& name1,
                                   const string& name2,
                                   XMLElementVector& result) const
{
  if (root()->name() == name1) {
    elements_by_path(root(), name2, result);
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(XMLElement* context,
                                   const string& name1,
                                   const string& name2,
                                   const string& name3,
                                   XMLElementVector& result) const
{
  if (context == NULL) return;
  XMLElementVector children;
  context->children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementVector, children) {
    XMLElement* child = *i;
    elements_by_path(child, name2, name3, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(const string& name1,
                                   const string& name2,
                                   const string& name3,
                                   XMLElementVector& result) const
{
  if (root()->name() == name1) {
    elements_by_path(root(), name2, name3, result);
  }
}


/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(XMLElement* context,
                                   const string& name1,
                                   const string& name2,
                                   const string& name3,
                                   const string& name4,
                                   XMLElementVector& result) const
{
  if (context == NULL) return;
  XMLElementVector children;
  context->children_named(name1, children);
  FOR_EACH(const_iterator, i, XMLElementVector, children) {
    XMLElement* child = *i;
    elements_by_path(child, name2, name3, name4, result);
  }
}
/** Returns elements, if any, reachable by specified path,
    starting at specified context element. */
void XMLDocument::elements_by_path(const string& name1,
                                   const string& name2,
                                   const string& name3,
                                   const string& name4,
                                   XMLElementVector& result) const
{
  if (root()->name() == name1) {
    elements_by_path(root(), name2, name3, name4, result);
  }
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(XMLElement* context,
                                         const string& name1) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->child_named(name1);
  return element;
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(const string& name1) const
{
  XMLElement* result = NULL;
  if (root()->name() == name1) result = root();
  return result;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(XMLElement* context,
                                         const string& name1,
                                         const string& name2) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->child_named(name1);
  return element_by_path(element, name2);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(const string& name1,
                                         const string& name2) const
{
  if (root()->name() == name1)
    return element_by_path(root(), name2);
  else
    return NULL;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(XMLElement* context,
                                         const string& name1,
                                         const string& name2,
                                         const string& name3) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->child_named(name1);
  return element_by_path(element, name2, name3);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(const string& name1,
                                         const string& name2,
                                         const string& name3) const
{
  if (root()->name() == name1)
    return element_by_path(root(), name2, name3);
  else
    return NULL;
}


/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(XMLElement* context,
                                         const string& name1,
                                         const string& name2,
                                         const string& name3,
                                         const string& name4) const
{
  if (context == NULL) return NULL;
  XMLElement* element = context->child_named(name1);
  return element_by_path(element, name2, name3, name4);
}
/** Returns element, if any, reachable by specified path,
    starting at specified context element. */
XMLElement* XMLDocument::element_by_path(const string& name1,
                                         const string& name2,
                                         const string& name3,
                                         const string& name4) const
{
  if (root()->name() == name1)
    return element_by_path(root(), name2, name3, name4);
  else
    return NULL;
}
