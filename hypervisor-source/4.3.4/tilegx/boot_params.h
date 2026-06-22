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
 * Definitions of parameters passed to the booter.
 */

#ifndef _SYS_HV_TILEGX_BOOT_PARAMS_H
#define _SYS_HV_TILEGX_BOOT_PARAMS_H

#include <stdint.h>

/** Extra configuration data. */
union boot_params
{
  /** Configuration data. */
  struct
  {
    /** Memory speed settings, in MT/s.  0 means to use the default speed,
     *  and a negative value means that controller should be disabled. */
    int16_t mem_speed[4];

    //
    // Device speed settings.  Values are in MHz.  A value of SPEED_DEFAULT
    // means that the device is present but no frequency was requested, so
    // the default should be used; a value of 0 means the device is not
    // present.
    //

    /** Core clock (tile) frequency. */
    uint64_t speed_core: 12;

    /** mPIPE 0 core clock frequency. */
    uint64_t speed_mpipe_0_core: 12;

    /** mPIPE 0 classifier clock frequency. */
    uint64_t speed_mpipe_0_cls: 12;

    /** TRIO 0 clock frequency. */
    uint64_t speed_trio_0: 12;

    /** Crypto 0 clock frequency. */
    uint64_t speed_crypto_0: 12;

    /** If nonzero, take POST defaults from post_query/post_thorough,
     *  below; if zero, those are ignored, and the booter default used. */
    uint64_t post_override: 1;

    /** If nonzero, query the user for the POST mode at boot time.  If
     *  no entry is made, run quick or thorough post depending upon
     *  the value of post_thorough. */
    uint64_t post_query: 1;

    /** If nonzero, run thorough POST.  Used if post_override is set and
     *  post_query is not, or if both are set but the user doesn't make
     *  a selection when prompted. */
    uint64_t post_thorough: 1;

    /** Filler.  Without this, x86 and Gx have different alignment for
     *  subsequent members. */
    uint64_t : 1;

    /** Crypto 1 clock frequency. */
    uint64_t speed_crypto_1: 12;

    /** Compression 0 clock frequency. */
    uint64_t speed_comp_0: 12;

    /** Compression 1 clock frequency. */
    uint64_t speed_comp_1: 12;

    /** Filler (formerly USB host 0 clock frequency). */
    uint64_t : 12;

    /** Filler (formerly USB host 1 clock frequency). */
    uint64_t : 12;

    /** Filler. */
    uint64_t : 4;

    /** mPIPE 1 core clock frequency. */
    uint64_t speed_mpipe_1_core: 12;

    /** mPIPE 1 classifier clock frequency. */
    uint64_t speed_mpipe_1_cls: 12;

    /** TRIO 1 clock frequency. */
    uint64_t speed_trio_1: 12;

    /** Filler. */
    uint64_t : 28;
  }
  cfg;
  /** Word-oriented format. */
  uint64_t words[0];
};

/** Value the parameters are initialized to in the booter. */
#define BOOTPARAMS_INIT \
{ \
  { \
    .mem_speed = { 0, 0, 0, 0 },         \
    .speed_core = SPEED_DEFAULT,         \
    .speed_mpipe_0_core = SPEED_DEFAULT, \
    .speed_mpipe_0_cls = SPEED_DEFAULT,  \
    .speed_trio_0 = SPEED_DEFAULT,       \
    .speed_crypto_0 = SPEED_DEFAULT,     \
    .post_override = 0,                  \
    .post_query = 0,                     \
    .post_thorough = 0,                  \
    .speed_crypto_1 = SPEED_DEFAULT,     \
    .speed_comp_0 = SPEED_DEFAULT,       \
    .speed_comp_1 = SPEED_DEFAULT,       \
    .speed_mpipe_1_core = SPEED_DEFAULT, \
    .speed_mpipe_1_cls = SPEED_DEFAULT,  \
    .speed_trio_1 = SPEED_DEFAULT,       \
  } \
}

/** Device is present but has no requested speed. */
#define SPEED_DEFAULT 0xFFF

/** Maximum representable speed. */
#define SPEED_MAX 0xFFF

#endif // _SYS_HV_TILEGX_BOOT_PARAMS_H
