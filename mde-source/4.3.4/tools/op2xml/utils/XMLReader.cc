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
// XMLReader.cc -- XMLReader class
// ==========================================================================

// custom includes
#include "XMLReader.h"     // header file
#include "foreach.h"       // FOR_EACH macro

// --------------------------------------------------------------------------
// xml_encode()
// --------------------------------------------------------------------------

/** Encodes XML special characters in text as XML entities.
 *  (i.e. "2<3" -> "2&lt;3")
 */
string xml_encode(const string& text) {
  string result;
  FOR_EACH(const_iterator, i, string, text) {
    char c = *i;
    switch (c)
    {
     case '\"': result += "&quot;"; break;
     case '\'': result += "&apos;"; break;
     case '&':  result += "&amp;";  break;
     case '<':  result += "&lt;";   break;
     case '>':  result += "&gt;";   break;
     default:
       // handle characters outside printable ASCII range
       unsigned char uc = (unsigned char)c;
       if ((uc <= 0x1F) || uc >= 0x7F) {
         result += "&#";
         result += (int)uc;
         result += ";";
       } else {
         result += c;
       }
       break;
    }
  }
  return result;
}

// --------------------------------------------------------------------------
// xml_decode()
// --------------------------------------------------------------------------

/** Decodes XML special character entities in text
 *  (i.e. "2&lt;3" -> "2<3")
 */
string xml_decode(const string& text) {
  string result;
  size_t len = text.size();
  size_t x=0, amp, semi;
  while (x < len) {
    // look for start of next entity, if any
    amp = text.find('&', x);
    if (amp == string::npos) {
      // if no more entities, append rest of string to result
      result += text.substr(x);
      x = len;
    }
    else {
      // append everything up to the ampersand
      if (amp - x > 0) {
        result += text.substr(x, amp-x);
        x = amp;
      }
      // is this a quoted ampersand?
      if (amp < len && text[amp+1] == '&') {
        // append a single ampersand
        result += '&';
        x = amp+2;
      }
      else {
        // look for a closing semicolon
        semi = text.find(';', amp+1);
        if (semi == string::npos) {
          // whoops, ampersand with no closing semi
          // for now, we'll treat this as a quoted semi
          result += '&';
          x = amp+2;
        }
        else {
          // read the entity declaration and substitute it
          string entity = text.substr(amp, semi-amp+1);
          if (entity == "&quot;") {
            result += "\"";
          }
          else if (entity == "&apos;") {
            result += "\'";
          }
          else if (entity == "&apos;") {
            result += "\'";
          }
          else if (entity == "&amp;") {
            result += "&";
          }
          else if (entity == "&lt;") {
            result += "<";
          }
          else if (entity == "&gt;") {
            result += ">";
          }
          else if (starts_with(entity,"&#")) {
            string char_code = entity.substr(2,entity.size()-3);
            int code = atoi(char_code.c_str());
            char c = to_char(code);
            result += c;
          }
          else {
            // we give up, leave it as is and let the user figure it out
            result += entity;
          }
          x += entity.size();
        }
      }
    }
  }
  return result;
}


// --------------------------------------------------------------------------
// XMLTag
// --------------------------------------------------------------------------

// class is simple enough to be all inlined for now


// --------------------------------------------------------------------------
// XMLReader
// --------------------------------------------------------------------------

// class is a template, so no sources here for now

