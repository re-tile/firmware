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
// options.h -- command-line option-processing utilities
// ==========================================================================

// inclusion guard
#ifndef OPTIONS_H
#define OPTIONS_H

// C/C++ includes
#include <unistd.h>              // getopt() argument processing utilities
#include <getopt.h>              // getopt_long/_only() GNU extensions

// custom includes
#include "io.h"           // IO streams
#include "string_utils.h" // C/C++ strings
#include "foreach.h"      // FOR_EACH() macro
#include "Vector.h"       // Vector class
#include "Map.h"          // MultiMap class


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

/** Option instance description */
struct Option {
  // --- members ---
  string name;
  string value;

  // --- constructors/destructors ---
  Option(string name, string value) : name(name), value(value) {}
};


/** List of option descriptors */
typedef Vector<Option> OptionList;

/** List of pointers to options*/
typedef Vector<const Option*> OptionPtrList;


// -------------------------------------------------------------------------
// constants
// -------------------------------------------------------------------------

/** Option character code for non-option arguments.

    If the initial char in the "short_options" string is "-",
    then the argument array is not permuted during the search;
    instead, non-option arguments are treated as the value of
    a pseudo-option with character code 1. */
extern const int NON_OPTION_ARGUMENT_CODE;

/** Name reported for non-option arguments.

    If the initial char in the "short_options" string is "-",
    then the argument array is not permuted during the search;
    instead, non-option arguments are treated as the value of
    a pseudo-option with character code 1. */
extern const char* NON_OPTION_ARGUMENT_NAME;


// -------------------------------------------------------------------------
// collect_options()
// -------------------------------------------------------------------------

/** The collect_options() function extracts "-x" and "--name" options from
    the command line arguments array.

    The argc and argv arguments are the variables passed into main().
    On return, argc is set to the number of non-option arguments,
    plus one for argv[0],
    and argv[] is resorted to collect the non-option arguments in
    elements 1 through argc of the array, so these arguments can be
    processed as usual.

    The collect_options() function internally uses getopt_long()
    to collect options,
    and takes the same short-option and long-option spec formats,
    summarized below.

    short_options is a string defining the accepted short option characters
    (-x)
      "x"   -->  no value (-x), can be grouped in one string e.g. -xyzpdq
      "x:"  -->  required value (-xVALUE)
      "x::" -->  optional value (-x or -xVALUE)
     note:
      The argument "--" is consumed, and forcibly terminates search
      The argument "-"  is NOT consumed, and is preserved as literal argument

    long_options is an array of option structs defining the accepted
    long option names (--name)

    The format for each option is:
      { "option_name", required/optional/no_argument, flag_var, 'x' }

        "option_name" is the option name
           (unique prefix substrings are automatically accepted)

        required_argument means accept
           either --option=argument  or  --option argument

        optional_argument means accept
           either --option=argument  or  --option

        no_argument       means accept
           only   --option

        'x' is the int value returned by getopt_long...() for the option
        if flag_var is NULL.

        if flag_var is non-NULL, 0 is returned by getopt_long...()
        and the specified flag_var is set to the value 'x'.

    Note: we picked getopt_long() since:
      getopt() only handles short options (-x)
      getopt_long() also accepts long options with a
        double dash (--option_name)
      getopt_long_only() also accepts long options
        with a single dash (-option_name)
        but it doesn't set the optopt variable correctly
        for unrecognized short options,
        so we can't collect the rejected options properly.
*/
bool collect_options(int &argc,   char** &argv,
                     const char*          short_options,
                     const struct option* long_options,
                     OptionList &options,
                     OptionList &rejects);

/** Runs collect_options with specified arguments,
    displays collected and rejected options and remaining arguments. */
bool collect_options_display(int &argc,   char** &argv,
			     const char*          short_options,
			     const struct option* long_options);


// -------------------------------------------------------------------------
// find_option(), find_options()
// -------------------------------------------------------------------------

/** Returns true if there is at least one option with the specified name. */
bool find_option(OptionList& options, string name);

/** Returns true if there is at least one option with the specified name.
    Modifies option argument to point to first option found,
    or NULL if not found. */
bool find_option(OptionList& options, string name, const Option* &option);

/** Returns number of options with the specified name. */
int find_options(OptionList& options, string name);

/** Returns number of options with the specified name.
    Populates specified vector with pointer(s) to found options, if any. */
int find_options(OptionList& options, string name,
                 Vector<const Option*> &optionlist);

// inclusion guard
#endif
