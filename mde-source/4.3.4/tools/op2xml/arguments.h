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
// arguments.h -- argument processing
// ==========================================================================

// multiple-inclusion guard
#ifndef ARGUMENTS_H
#define ARGUMENTS_H

// custom includes
#include "options.h"           // command-line option processing
#include "Map.h"               // Map class
#include "Pathname.h"          // UNIX pathnames
#include "SymbolFileManager.h" // symbol file management


// -------------------------------------------------------------------------
// process_arguments()
// -------------------------------------------------------------------------

/** Processes command-line arguments.
    Returns true if application should continue, false if it should exit.
    Returns exit status value in status.
    Stores parsed results in other arguments.
*/
bool process_arguments(int argc, char** argv, int &status,
		       Pathname&       profile_output_file,
		       Pathname&       monitor_xml_file,
		       Pathname&       statistics_xml_file,
		       PathnameVector& sample_directory_paths);

// multiple-inclusion guard
#endif
