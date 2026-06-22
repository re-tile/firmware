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
// io_utils.h -- C/C++ IO Utilities
// ============================================================================

// inclusion guard
#ifndef IO_H
#define IO_H

// C++ IO includes
#include <iostream>
using std::istream;
using std::ostream;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;

#include <fstream>
using std::ifstream;
using std::ofstream;


// ----------------------------------------------------------------------------
// output stream operator boilerplate macros
// ----------------------------------------------------------------------------

/** Output stream operator boilerplate for a type. */
#define OUTPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_ostream<charT, traits>& \
      operator<<(std::basic_ostream<charT, traits>& STREAMNAME, \
        TYPENAME VARNAME)

/** Input stream operator boilerplate for a type. */
#define INPUT_STREAM_OPERATOR(STREAMNAME, TYPENAME, VARNAME) \
  template<typename charT, typename traits> \
    std::basic_istream<charT, traits>& \
      operator>>(std::basic_istream<charT, traits>& STREAMNAME, \
        TYPENAME VARNAME)


// inclusion guard
#endif
