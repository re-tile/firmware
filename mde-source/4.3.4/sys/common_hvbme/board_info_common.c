/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

/**
 * @file
 * Non chip-dependent board information access routines, shared between
 * the hypervisor and BME.
 */

#include <stdint.h>

#include "board_info.h"

uint32_t
bi_find(uint32_t* blockbuf, int blocklen, int type, int instance,
        uint32_t** resbuf, int* offset)
{
  //
  // Start our search in the right place.  WARNING: the code in
  // sys/hv/board_info.c which does AIB splicing knows the interpretation
  // of *offset, since it needs to back up the search when it replaces an
  // AIB item with the actual AIB.  If you change the way this works, that
  // code will also need to be modified.
  //
  int position;
  if (!offset)
    position = 0;
  else
    position = *offset;

  while (position < (blocklen / 4))
  {
    int desc = blockbuf[position];
    int newpos = position + 1 + BI_WDS(desc);

    //
    // In addition to checking that this it the type we're looking for,
    // we make sure the data area doesn't go off the end of the buffer.
    //
    if ((type == -1 || type == BI_TYPE(desc)) &&
        (instance == -1 || instance == BI_INST(desc)) &&
        newpos <= (blocklen / 4))
    {
      *resbuf = &blockbuf[position + 1];
      if (offset)
        *offset = newpos;

      return (desc);
    }

    position = newpos;
  }

  return (BI_NULL);
}
