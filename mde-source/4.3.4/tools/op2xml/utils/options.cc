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
// options.cc -- command-line option-processing utilities
// ==========================================================================

// header file
#include "options.h" // command-line option processing utilities


// -------------------------------------------------------------------------
// constants
// -------------------------------------------------------------------------

/** Option character code for non-option arguments.

    If the initial char in the "short_options" string is "-",
    then the argument array is not permuted during the search;
    instead, non-option arguments are treated as the value of
    a pseudo-option with character code 1. */
const int NON_OPTION_ARGUMENT_CODE = 1;

/** Name reported for non-option arguments.

    If the initial char in the "short_options" string is "-",
    then the argument array is not permuted during the search;
    instead, non-option arguments are treated as the value of
    a pseudo-option with character code 1. */
const char* NON_OPTION_ARGUMENT_NAME = "_ARGUMENT_";


// -------------------------------------------------------------------------
// collect_options()
// -------------------------------------------------------------------------

/** The collect_options() function extracts "-x" and "--name" options from
    the command line arguments array.

    See the description of this function in the header file.
*/
bool collect_options(int &argc,   char** &argv,
                     const char*          short_options,
                     const struct option* long_options,
                     OptionList &options,
                     OptionList &rejects)
{
  bool result = true;

  // set opterr to 0 to suppress getopt()'s builtin error messages
  // about unrecognized options
  opterr = 0;

  // process options until we don't find any more
  int option  = 0;
  int longopt = -1;
  optind = 1; // preset this so we can set lastarg properly
  while (true) {
    char* lastarg = argv[optind];
    option = getopt_long(argc, argv, short_options, long_options, &longopt);
    if (option < 0) {
      //cerr << "collect_options: end of options" << endl;
      // no more options detected
      break;
    }
    else if (option == '?') {
      // unreconized option -- actual char is stored in "optopt" by getopt()
      // and getopt_long()
      if (optopt != 0) {
        string name(1, static_cast<const char>(optopt));
        rejects.add(Option(name, lastarg));
        // cerr << "collect_options: unrecognized option '"
        // << name << "'" << endl;
      }
      else {
        rejects.add(Option(lastarg, lastarg));
        // cerr << "collect_options: unrecognized option '"
        // << lastarg << "'" << endl;
      }
      result = false;
    }
    else if (option == NON_OPTION_ARGUMENT_CODE) {
      // special case, when first char of short-options is "-"
      string name  = NON_OPTION_ARGUMENT_NAME;
      string value = (optarg != NULL) ? optarg : "";
      options.add(Option(name, value));
      //cerr << "collect_options: argument '" << name << "'" << endl;
    }
    else if (longopt >= 0) {
      // long option -- index is stored in "longopt",
      // value if any in "optarg"
      string name = long_options[longopt].name;
      string value = (optarg != NULL) ? optarg : "";
      longopt = -1; // important: reset longopt flag for next iteration
      options.add(Option(name, value));
      //cerr << "collect_options: long option '" << name << "'" << endl;
    }
    else {
      // short option -- actual char is returned in "option",
      // value if any is stored in "optarg"
      string name(1, static_cast<const char>(option));
      string value = (optarg != NULL) ? optarg : "";
      options.add(Option(name, value));
      //cerr << "collect_options: short option '" << name << "'" << endl;
    }
  }

  // argv has been reordered by getopt() processing so that element range
  // argv[optind] - argv[argc-1] contains remaining arguments, if any;
  // let's rearrange things so it looks like those were the only arguments
  char* args[argc];
  args[0] = argv[0];
  for (int i=0; i<optind; i++) {
    args[argc-i] = argv[i];
  }
  for (int i=optind; i<argc; i++) {
    args[i-optind+1] = argv[i];
  }
  argc = argc - optind;
  for (int i=0; i<=argc; i++) {
    argv[i] = args[i];
  }

  return result;  
}


/** Runs collect_options with specified arguments,
    displays collected (or rejected) options. */
bool collect_options_display(int &argc,   char** &argv,
                             const char*          short_options,
                             const struct option* long_options)
{
  OptionList options, rejects;
  bool result = collect_options(argc, argv, short_options, 
                                long_options, options, rejects);
  if (! result) {
    // display rejected options, if any
    FOR_EACH(const_iterator, i, OptionList, rejects) {
      cout << "Unrecognized Option: '" << i->name << "'";
      if (i->name != i->value) {
        cout << " in argument '" << i->value << "'";
        }
      cout << endl;
    }
  }

  // display the options we collected
  FOR_EACH(const_iterator, i, OptionList, options) {
    cout << "Option: '" << i->name << "'";
    if (! (i->value).empty()) {
      cout << " Value='" << i->value << "'";
    }
    cout << endl;
  }

  // display the remaining arguments
  for (int i=1; i<=argc; i++) {
    cout << "Arg: '" << argv[i] << "'" << endl;
  }

  return result;
}


// -------------------------------------------------------------------------
// find_option(), find_options()
// -------------------------------------------------------------------------

/** Returns true if there is at least one option with the specified name.
    Modifies option argument to point to first option found,
    or NULL if not found. */
bool find_option(OptionList& options, string name)
{
  bool result = false;
  FOR_EACH(const_iterator, i, OptionList, options) {
    if ((i->name.compare(name)) == 0) {
      result = true;
      break;
    }
  }
  return result;
}

/** Returns true if there is at least one option with the specified name.
    Modifies option argument to point to first option found,
    or NULL if not found. */
bool find_option(OptionList& options, string name, const Option* &option)
{
  option = NULL;
  FOR_EACH(const_iterator, i, OptionList, options) {
    if ((i->name.compare(name)) == 0) {
      option = &(*i);
      break;
    }
  }
  return (option != NULL);
}

/** Returns true if there is at least one option with the specified name.
    Populates specified vector with pointer(s) to found options, if any. */
int find_options(OptionList& options, string name)
{
  int result = 0;
  FOR_EACH(const_iterator, i, OptionList, options) {
    if ((i->name.compare(name)) == 0) {
      result++;
    }
  }
  return result;
}

/** Returns true if there is at least one option with the specified name.
    Populates specified vector with pointer(s) to found options, if any. */
int find_options(OptionList& options, string name,
                 Vector<const Option*> &optionlist)
{
  optionlist.clear();
  FOR_EACH(const_iterator, i, OptionList, options) {
    if ((i->name.compare(name)) == 0) {
      optionlist.add(&(*i));
    }
  }
  return optionlist.size();
}
