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
// utils.cc -- utility macros/functions
// Copyright (C) 2010. Tilera Corporation
// ============================================================================

#include "utils.h"

// C includes.
#include <stdlib.h>    // rand(), srand()
#include <time.h>      // time()
#include <unistd.h>    // getpid()


// ----------------------------------------------------------------------------
// random numbers
// ----------------------------------------------------------------------------

/** Initializes random number generator. */
void randomize()
{
  srand(time(NULL) + getpid());
}

/** Returns random integer between 0 and limit-1, inclusive. */
int random(int limit)
{
  return (rand() % limit);
}


