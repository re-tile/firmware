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
// XMLReader.h -- XMLReader header
// =========================================================================

// multiple-inclusion guard
#ifndef XMLREADER_H
#define XMLREADER_H

// custom includes
#include "io.h"            // IO streams
#include "string_utils.h"  // C/C++ strings
#include "Pathname.h"      // Unix/Linux pathnames
#include <errno.h>         // errno
#include <string.h>

// XML includes
#include "XMLElement.h"
#include "XMLDocument.h"

// -------------------------------------------------------------------------
// xml_encode()
// -------------------------------------------------------------------------

/** Encodes XML special characters in text as XML entities.
 *  (i.e. "2<3" -> "2&lt;3")
 */
string xml_encode(const string& text);

// -------------------------------------------------------------------------
// xml_decode()
// -------------------------------------------------------------------------

/** Decodes XML special character entities in text
 *  (i.e. "2&lt;3" -> "2<3")
 */
string xml_decode(const string& text);


// -------------------------------------------------------------------------
// XMLTag
// -------------------------------------------------------------------------

/** Representation of an XML tag, used by XMLReader */
class XMLTag
{
  // --- constants ---
public:
  /** Tag type constants */
  enum Type {
    UNKNOWN = 0,
    OPEN_TAG = 1,
    CLOSE_TAG = 2,
    EMPTY_TAG = 3,
    PROCESSING_INSTRUCTION = 4,
    COMMENT = 5
  };

  // --- members ---
protected:
  /** tag type */
  Type m_type;

  /** tag name */
  string m_name;

  /** attributes of this element */
  XMLAttributeMap m_attributes;

  /** Nested text content, if any.
   *  For COMMENT tags, comment text is stored here.
   */
  string m_text;

  
  // --- constructors ---
public:
  /** Constructor */
  XMLTag() :
    m_type(UNKNOWN), m_name("NO_NAME_TAG")
  {}

  /** Constructor */
  XMLTag(Type type, const string& name) :
    m_type(type), m_name(name)
  {}

  /** Constructor */
  XMLTag(Type type, const string& name, const XMLAttributeMap& attributes) :
    m_type(type), m_name(name), m_attributes(attributes)
  {}

  /** Destructor */
  ~XMLTag()
  {
    m_name.clear();
    m_attributes.clear();
  }


  // --- accessors ---
public:
  /** Returns tag type (open, close, empty, etc.) */
  Type type() const
  {
    return m_type;
  }
  /** Sets tag type (open, close, empty, etc.) */
  void set_type(Type type)
  {
    m_type = type;
  }

  /** Returns tag name */
  const string& name() const
  {
    return m_name;
  }
  /** Sets tag name */
  void name(string& name) {
    m_name = name;
  }

  /** Sets attribute of this tag */
  void set_attribute(const string& name, const string& value)
  {
    m_attributes.put(name, value);
  }
  /** Gets attribute of this tag */
  string get_attribute(const string& name, const string& default_value = "")
  {
    return m_attributes.get(name, default_value);
  }
  /** Sets attribute name/value map of this tag */
  void set_attributes(const XMLAttributeMap& attributes)
  {
    m_attributes.clear();
    m_attributes = attributes;
  }
  /** Gets attribute name/value map of this tag */
  const XMLAttributeMap& attributes() const
  {
    return m_attributes;
  }
  /** Clears attributes of this tag */
  void clear_attributes()
  {
    m_attributes.clear();
  }

  /** Sets nested text of element */
  void set_text(const string& text)
  {
    m_text = text;
  }
  /** Appends to nested text of element */
  void append_text(const string& text)
  {
    m_text += text;
  }
  /** Gets nested text of element */
  const string& text() const
  {
    return m_text;
  }
};


// -------------------------------------------------------------------------
// XMLReader
// -------------------------------------------------------------------------

/** XMLDocument reading utility class */
template<typename charT = char, typename traits = std::char_traits<charT> >
class XMLReaderBase
{
  // --- static methods ---
public:
  static XMLDocument* read(const Pathname& pathname, string& errors)
  {
    XMLDocument* result = NULL;
    errors = "";

    // open file
    ifstream fin(pathname.c_str());
    if (! fin) {
      errors += "Could not open file '" + pathname + "'.\n";
      errors += "Reason: (" + to_string(errno) + ") " + to_string(strerror(errno)) + "\n";
    }
    else {
      // load XML document
      result = XMLReaderBase<charT, traits>(fin).read(errors);
      if (result == NULL) {
	errors = "XML parse error(s) while reading statistic metadata.\n"
	  + errors;
      }
    }
    return result;
  }  


  // --- members ---
protected:
  /** Stream to read from */
    std::basic_istream<charT, traits>& m_stream;

  /** Current line */
  int m_lineNumber;

  /** Current char */
  int m_column;

  
  // --- constructors ---
public:
  /** Constructor */
  XMLReaderBase(std::basic_istream<charT, traits>& stream) :
    m_stream(stream), m_lineNumber(1), m_column(1)
  {}

  /** Destructor */
  ~XMLReaderBase()
  {}


  // --- XML reading utility methods ---
protected:

  /** Gets character from stream.
   *  Returns -1 when end of stream is reached.
   */
  int get()
  {
    int c = m_stream.get();
    if (c == traits::eof()) c = -1;
    if (c == '\n') {
      m_lineNumber++;
      m_column = 1;
    }
    else {
      m_column++;
    }
    return c;
  }

  /** Pushes back character into stream. */
  void push(int c)
  {
    m_stream.putback(to_char(c));
  }

  /** Peeks at next character without consuming it.
   *  Returns -1 when end of stream is reached.
   */
  int peek()
  {
    int c = m_stream.peek();
    if (c == traits::eof()) c = -1;
    return c;
  }

  /** Returns true if this is a legal name start character. */
  bool is_name_start_char(int c) {
    return (is_letter(c) || c == '_' || c == ':');
  }

  /** Returns true if this is a legal name part character.
   *  (That is, a character that can appear after the starting char
   *  in a name.)
   */
  bool is_name_part_char(int c) {
    return (is_letter_or_digit(c) || c == '_' || c =='-'
            || c == '.' || c == ':');
  }

  /** Skips any whitespace chars in stream. */
  void skip_whitespace()
  {
    int c;
    while ((c = peek()) >=0) {
      if (is_whitespace(c)) {
	// consume whitespace char, and keep going
	get();
      }
      else {
	// exit loop at first non-whitespace char
	break;
      }
    }
  }
 
  /** Trims leadind and trailing whitespace from string */
  void trim(string& text) {
    int left = 0;
    int right = text.size();
    while (left < right && is_whitespace(text[left])) left++;
    while (right > left && is_whitespace(text[right-1])) right--;
    int len = right - left;
    if (len == 0) {
      text.clear();
    }
    else {
      text = text.substr(left, len);
    }
  }

  /** Appends error to error list, adding current line/char info. */
  void add_error(string& errors, const string& error) const {
    errors += "Line " + to_string(m_lineNumber) + ", column "
      + to_string(m_column) + ": ";
    errors += error + "\n";
  }

  /** Appends error to error list, adding current line/char info. */
  void add_error(string& errors, const char* error) const {
    errors += "Line " + to_string(m_lineNumber) + ", column "
      + to_string(m_column) + ": ";
    errors += string(error) + "\n";
  }


  // --- XML document/element output methods ---
public:

  /** Reads text up to next '<' (i.e. text within/between tags).
   *  Trims leading/trailing whitespace before returning it.
   */
  void read_xml_content_text(string& result)
  {
    result.clear();
    int c;
    while ((c = peek()) >= 0) {
      if (c == '<') {
        break;
      }
      else {
	get();
	result += to_char(c);
      }
    }
  }

  /** Reads character text for a single element tag ("<...>") from
   *  the document.
   *  Returns true if read was successful, false if there were any errors.
   */
  bool read_xml_element_text(string& result, string& errors)
  {
    bool okay = true;
    result.clear();
    skip_whitespace();
    int c = peek();
    if (c < 0) {
      add_error(errors, "Expected XML tag, found end of input stream.");
      okay = false;
    }
    else if (c != '<') {
      // whoops, random text found when we were expecting an element
      string junk;
      read_xml_content_text(junk);
      trim(junk);
      add_error(errors, "Expected XML tag, found: '" + junk + "'");
      okay = false;
    }
    else {
      // consume the '<' character
      get();
      result += to_char(c);
      // consume remaining text until we hit a '>' or run out of chars
      while ((c = peek()) >= 0) {
        // detect missing close '>' which allows us to run into following '<'
        if (c == '<') {
          add_error(errors, "While reading XML tag, found unexpected '<' -- preceding tag was not properly closed.");
          okay = false;
          break;
        }
        else {
          // consume the character, stopping when we hit a '>'
          get();
          result += to_char(c);
          if (c == '>') break;
        }
      }
    }
    return okay;
  }

  /** Parses XML tag from string returned by read_xml_element_text(). */
  XMLTag* parse_xml_element_text(string& tag, string& errors)
  {
    XMLTag* result = NULL;
    XMLTag::Type type;
    type = XMLTag::UNKNOWN;
    string name;
    XMLAttributeMap attributes;
    string text;

    int x = 1; // current read point in tag string
    int len = tag.size();
    bool found_tag_end = false; // whether we've seen the end of the tag

    // check overall tag type
    if (starts_with(tag, "</") && ends_with(tag, ">")) {
      type = XMLTag::CLOSE_TAG;
      x = 2; // skip tag start chars
    }
    else if (starts_with(tag, "<?") && ends_with(tag, "?>")) {
      type = XMLTag::PROCESSING_INSTRUCTION;
      x = 2; // skip tag start chars
    }
    else if (starts_with(tag, "<") && ends_with(tag, "/>")) {
      type = XMLTag::EMPTY_TAG;
    }
    else if (starts_with(tag, "<!--") && ends_with(tag, "-->")) {
      // special case -- read comment body and store comment in text of tag
      x = 4; // skip tag start chars
      type = XMLTag::COMMENT;
      text = tag.substr(x, len - 7);
      trim(text);
      found_tag_end = true;
    }
    // note: this test must come last, since it matches anything
    // with '<' '>' on the ends
    else if (starts_with(tag, "<") && ends_with(tag, ">")) {
      type = XMLTag::OPEN_TAG;
    }
    else {
      add_error(errors, "Unrecognized tag type: '" + tag + "'");
      found_tag_end = true;
    }

    // note: use while to provide a context we can break out of
    while (! found_tag_end) {

      bool okay = true;
      bool first_char;
      int c;

      // skip whitespace, if any, before name
      while ((x < len) && is_whitespace(tag[x])) x++;
      if (x >= len) break;

      // look for name token
      first_char = true;
      while (! found_tag_end && (x < len)) {
        c = tag[x];
        if (first_char && is_name_start_char(c)) {
          first_char = false;
          name += to_char(c);
        }
        else if (! first_char && is_name_part_char(c)) {
          name += to_char(c);
        }
        else if (is_whitespace(c)) {
          break;
        }
        else if (c == '?' || c == '/' || c == '>') {
          found_tag_end = true;
          break;
        }
        else {
          // unexpected random garbage
          add_error(errors, "While reading tag name, found character '"
                    + to_string((char) c) + "'");
          okay = false;
          break;
        }
        x++;
      }
      if (x >= len || ! okay) break;

      // look for attributes, if any
      while (! found_tag_end && (x < len)) {

        string attribute_name, attribute_value;

        // skip whitespace, if any, before next attribute
        while ((x < len) && is_whitespace(tag[x])) x++;
        if (x >= len) break;

        // look for name token,
        // or end of tag if we've run out of attributes
        first_char = true;
        while (! found_tag_end && (x < len)) {
          c = tag[x];
          if (first_char && is_name_start_char(c)) {
            first_char = false;
            attribute_name += to_char(c);
          }
          else if (! first_char && is_name_part_char(c)) {
            attribute_name += to_char(c);
          }
          else if (is_whitespace(c) || c == '=') {
            break;
          }
          else if (c == '?' || c == '/' || c == '>') {
            found_tag_end = true;
            break;
          }
          else {
            // unexpected random garbage
            add_error(errors, "While reading tag attribute name, found character '" + to_string((char) c) + "'");
            okay = false;
            break;
          }
          x++;
        }
        if (found_tag_end || x >= len || ! okay) break;

        // skip whitespace, if any, before equal sign
        while ((x < len) && is_whitespace(tag[x])) x++;

        // check for equal sign
        c = tag[x]; if (c != '=') break;
        x++; // skip equal sign

        // skip whitespace, if any, before value
        while ((x < len) && is_whitespace(tag[x])) x++;

        // check for value quoting
        int quote = 0;
        if (x >= len) break;
        c = tag[x];
        if (c == '\'' || c == '"') {
          quote = c;
          x++; // skip the quote char
        }

        // collect value
        while (! found_tag_end && (x < len)) {
          c = tag[x];
          if (quote > 0) {
            // if we're quoted, closing quote or end of stream ends value
            if (c == quote) {
              x++; // skip the quote char
              break;
            }
            else { 
              attribute_value += to_char(c);
            }
          }
          else { // quote == 0
            // if we're not quoted, end of tag or whitespace
            // ends attribute value
            if (is_whitespace(c)) {
              break;
            }
            else if (c == '?' || c == '/' || c == '>') {
              found_tag_end = true;
              break;
            }
            else {
              attribute_value += to_char(c);
            }
          }
          x++;
        }

        if (! attribute_name.empty()) {
          attribute_value = xml_decode(attribute_value);
          attributes.put(attribute_name, attribute_value);
        }
      }
    }

    if (type != XMLTag::UNKNOWN) {
      result = new XMLTag(type, name, attributes);
    }

    return result;
  }

  /** Reads and returns a single element tag ("<...>") from the document. */
  XMLTag* read_tag(string& errors)
  {
    string text;
    bool okay = read_xml_element_text(text, errors);
    XMLTag* result = (okay) ? parse_xml_element_text(text, errors) : NULL;
    return result;
  }


  // --- XML read methods ---
public:

  /** Reads and returns a single XML document or document fragment.
   *  Looks for a top-level tag, ignoring processing instructions
   *  and comments,
   *  reads this tag completely, then returns an XMLDocument object
   *  for the tag and its nested content.
   */
  XMLDocument* read(string& errors)
  {
    errors.clear();

    XMLDocument* document = new XMLDocument();
    XMLElement* currentElement = NULL;

    while (true) {

      // get the next XML tag from the stream
      XMLTag* tag = read_tag(errors);
      if (tag == NULL) {
	add_error(errors, "Expected XML tag but could not read one.");
	break;
      }

      XMLTag::Type tag_type = tag->type();
      string tag_name = tag->name();

      XMLElement* element = NULL;
      // TODO: detect '<?xml ... ?>' processing instruction
      // and check that we only see it once
      if (tag_type == XMLTag::PROCESSING_INSTRUCTION ||
          tag_type == XMLTag::COMMENT)
      {
	// skip these for now
      }
      else {
	// for an empty tag, we don't need to drill into child nodes,
	// we can just hang the new element off of the
        // current element/document, if any
	if (tag_type == XMLTag::EMPTY_TAG) {
	  // create a new element as root of the document or
          // child the current nested element
          element = document->create_element(tag->name(),
                                             tag->attributes(),
                                             currentElement);

	  if (currentElement == NULL) {
	    // we're done, this document contains a single empty tag
            // as its root element
	    break;
	  }
	}

	// for an open tag, we need to make the new element
	// the current element, and read its child elements, if any
	else if (tag_type == XMLTag::OPEN_TAG) {
	  // create a new element as root of the document
          // or child the current nested element
          element = document->create_element(tag->name(),
                                             tag->attributes(),
                                             currentElement);

	  // collect content text if any
	  if (tag_type == XMLTag::OPEN_TAG) {
            element->set_text(tag->text());
	  }

	  currentElement = element;
	}

	// when we hit a close tag, we can bounce back up a level
	// of the element hierarchy
	else if (tag_type == XMLTag::CLOSE_TAG) {
	  if (currentElement == NULL) {
	    add_error(errors, "Encountered close tag '" + tag_name + "' with no corresponding open tag.");
	    break;
	  }
	  else if (currentElement->name() != tag_name) {
	    add_error(errors, "Encountered close tag '" + tag_name + "', but " +
		      "was expecting close tag for '" + currentElement->name() + "'.");
	    break;
	  }

	  // bounce up one level in node hierarchy
	  currentElement = currentElement->parent();
	  if (currentElement == NULL) {
	    // if we've closed the root element, we're done!
	    break;
	  }
	}
      }

      // clean up the tag we read
      delete tag;
    }

    // if we didn't generate any errors, assume document is complete
    // and return it
    if (!(errors.empty() && document != NULL && document->root() != NULL)) {
      if (document != NULL) {
        delete document;
        document = NULL;
      }
    }

    return document;
  }

};

// convenience typedef
typedef XMLReaderBase<> XMLReader;

// multiple-inclusion guard
#endif
