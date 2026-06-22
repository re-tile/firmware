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
// main.cc -- perf2xml utility -- perf_events raw data to XML formatter
// ============================================================================

// custom includes
#include "Application.h"
#include "bfd_api.h"


// ----------------------------------------------------------------------------
// main()
// ----------------------------------------------------------------------------

/** main function */
int
main(int argc, char **argv)
{
  int status = 0;
  
  Application app;
  status = app.run(argc, argv);

  return status;
}
