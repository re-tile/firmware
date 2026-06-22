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
 * Definitions for boot error handling functions.
 */

#ifndef _SYS_HV_BOOT_ERROR_H
#define _SYS_HV_BOOT_ERROR_H

#include <arch/chip.h>

#include "types.h"

// Error codes used in both assembly and C code.

/** Boot code was detected coming in an invalid static network port. */
#define BOOT_ERR_INPUT_INVALID_DIRECTION 0x5
/** During boot, no working mshim was found. */
#define BOOT_ERR_NO_MSHIM 0x6
/** Boot message received by slave tile exceeded the maximum length. */
#define BOOT_ERR_MSG_TOO_BIG 0x7
/** Boot message received by slave was not recognized. */
#define BOOT_ERR_UNRECOG_MSG 0x8
/** Ack message received by boot master had wrong tag. */
#define BOOT_ERR_ACK_BAD_TAG 0x9
/** Boot master didn't receive enough acks given detected rectangle size. */
#define BOOT_ERR_LOST_ACK 0xa
/** Bad CRC was detected on the hypervisor image. */
#define BOOT_ERR_HV_IMAGE_BAD_CRC 0xb
/** Bad CRC was detected on booter configuration data. */
#define BOOT_ERR_CONFIG_BAD_CRC 0xc
/** Bad CRC was detected on the level 1 booter image. */
#define BOOT_ERR_L1_IMAGE_BAD_CRC 0xd

/** During boot, received a bad IODN response packet to a config write. */
#define BOOT_ERR_CFG_WRITE_BAD_RESP 0x10
/** During boot, received a bad IODN response packet to a config read. */
#define BOOT_ERR_CFG_READ_BAD_RESP 0x11
/** During boot, received a bad IODN response packet to a probe config read. */
#define BOOT_ERR_CFG_PROBE_BAD_RESP 0x12

/** During boot, function set_cbox_mmap_spr passed bad index value. */
#define BOOT_ERR_BAD_CBOX_MMAP_INDEX 0x20

/** When booting from SROM, no valid image was found.  */
#define BOOT_ERR_SROM_NO_VALID_IMAGE 0x30
/** When booting from SROM, detected a bad trailer.  */
#define BOOT_ERR_SROM_BAD_TRAILER 0x31
/** When booting from SROM, detected bad CRC.  */
#define BOOT_ERR_SROM_BAD_CRC 0x32

/** POST detected error on first pass through L1 Data cache. */
#define POST_ERR_CACHE_L1D_PASS1 0x41
/** POST detected error on second pass through L1 Data cache. */
#define POST_ERR_CACHE_L1D_PASS2 0x42
/** POST detected error on third pass through L1 Data cache. */
#define POST_ERR_CACHE_L1D_PASS3 0x43

/** POST detected error on first pass through L2 cache, Way 0. */
#define POST_ERR_CACHE_L2_W0_PASS1 0x44
/** POST detected error on second pass through L2 cache, Way 0. */
#define POST_ERR_CACHE_L2_W0_PASS2 0x45
/** POST detected error on first pass through L2 cache, Way 1. */
#define POST_ERR_CACHE_L2_W1_PASS1 0x46
/** POST detected error on second pass through L2 cache, Way 1. */
#define POST_ERR_CACHE_L2_W1_PASS2 0x47
/** POST detected error on first pass through L2 cache, Way 2. */
#define POST_ERR_CACHE_L2_W2_PASS1 0x48
/** POST detected error on second pass through L2 cache, Way 2. */
#define POST_ERR_CACHE_L2_W2_PASS2 0x49
/** POST detected error on first pass through L2 cache, Way 3. */
#define POST_ERR_CACHE_L2_W3_PASS1 0x4a
/** POST detected error on second pass through L2 cache, Way 3. */
#define POST_ERR_CACHE_L2_W3_PASS2 0x4b

/** POST found a parity error logged in the L2 cache. */
#define POST_ERR_CACHE_L2_PARITY 0x4c
/** POST found a parity error logged in the L1D cache. */
#define POST_ERR_CACHE_L1D_PARITY 0x4d

/** POST error detected in DRAM during quick initial test. */
#define POST_ERR_QUICK_DRAM 0x50
/** POST error detected in DRAM during the exhaustive test of memory that
    could be occupied by the hypervisor. */
#define POST_ERR_HV_RAM 0x51
/** POST error detected in DRAM during exhaustive HV filesystem RAM test. */
#define POST_ERR_HV_RAM_FS 0x52

/** Failure to translate a frequency to a voltage, or vice versa. */
#define BOOT_ERR_BAD_F2V_TABLE 0x60
/** Failure to translate a frequency to a set of PLL settings. */
#define BOOT_ERR_BAD_F2PLL 0x61
/** Attempt to set the chip voltage to a dangerous value. */
#define BOOT_ERR_VOLTAGE_OUT_OF_RANGE 0x62

/** No board information block found (or compiled in). */
#define BOOT_ERR_NO_BIB 0x70

/** External memory BIST operation did not complete. */
#define BOOT_ERR_MSH_BIST_HANG 0x80
/** Error during memory configuration. */
#define BOOT_ERR_MSH_CFG 0x81

#ifndef __ASSEMBLER__

void punt(uint32_t code);
void boot_error(uint32_t code);

#else

#define PUNT(x)  { moveli r0, (x); j punt_asm }
#define PUNTR(r) { move   r0, r;   j punt_asm }

#ifdef OVERRIDE_PUNT

#include "override_punt.h"

#else

        .macro PUNT_ROUTINE
        .local punt_drain

        .pushsection .text.punt_asm, "ax"
        .global punt_asm
        .type punt_asm,@function

punt_asm:
        mtspr   FAIL, r0
        mtspr   DONE, r0



punt_drain:
        {
         move   zero, udn0
         j      punt_drain
        }


        .size punt_asm,.-punt_asm
        .popsection
        .endm

#endif /* !OVERRIDE_PUNT */

#endif /* !__ASSEMBLER__ */

#endif /* _SYS_HV_BOOT_ERROR_H */
