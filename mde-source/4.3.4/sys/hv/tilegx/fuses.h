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
 * Definitions for software-used data in the processor's fuses.
 */

#ifndef _SYS_HV_TILEGX_FUSES_H
#define _SYS_HV_TILEGX_FUSES_H

#include "types.h"

/** Entry in a frequency-to-voltage table.  This matches the format used
 *  in the processor's fuses. */
struct f2v_entry
{
  /** Scaled target frequency.  The actual target frequency in Hertz is twice
   *  this value, plus 200 MHz. */
  uint16_t freq : 10;

  /** VID bits representing the voltage necessary to sustain the actual
   *  target frequency. */
  uint16_t vid : 6;
};


/** Word offset of union fuses within the actual fuse structure, for GX36.
 *  There are some words before this offset which are used by hardware and
 *  which we ignore. */
#define FUSE_FIRST_WORD_GX36  4

/** Word offset of union fuses within the actual fuse structure, for GX72.
 *  There are some words before this offset which are used by hardware and
 *  which we ignore. */
#define FUSE_FIRST_WORD_GX72  6

/** Index of core frequency to voltage entries. */
#define FUSE_F2V_CORE        0
/** Index of memory shim frequency to voltage entries. */
#define FUSE_F2V_MSH         3
/** Index of MiCA crypto shim frequency to voltage entries. */
#define FUSE_F2V_CRYPTO      6
/** Index of MiCA compress/decompress shim frequency to voltage entries. */
#define FUSE_F2V_COMPRESS    9
/** Index of mPIPE shim core frequency to voltage entries. */
#define FUSE_F2V_MPIPE_CORE 12
/** Index of mPIPE shim classifier core frequency to voltage entries. */
#define FUSE_F2V_MPIPE_CLS  15
/** Index of TRIO shim frequency to voltage entries. */
#define FUSE_F2V_TRIO       18
/** Index of USB shim frequency to voltage entries. */
#define FUSE_F2V_USB        21

/** F2V entries for each clock. */
#define FUSE_NUM_F2V_ENTRIES 3

/** Layout of the processor fuses. */
union fuses
{
  /** Layout of the processor fuses. */
  struct
  {
    /** Frequency to voltage entries. */
    struct f2v_entry f2v[24];

    /** Duty cycle adjustment. */
    int8_t duty_cycle_adjust:5;

    /** Is duty cycle adjustment valid? */
    uint8_t duty_cycle_adjust_valid:1;
  } data;
  /** Word access to the fuses, for filling the structure. */
  uint64_t words[0];
};

/** Number of words of fuses which we read. */
#define FUSE_NUM_WORDS \
  ((sizeof (union fuses) + sizeof (uint64_t) - 1) / sizeof (uint64_t))

#endif /* _SYS_HV_TILEGX_FUSES_H */
