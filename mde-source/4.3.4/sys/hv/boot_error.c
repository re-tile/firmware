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
 * Error handling functions for boot.
 */

#include <arch/chip.h>
#include <arch/spr.h>

#include <arch/udn.h>


#include "types.h"
#include "boot_error.h"

#ifndef OVERRIDE_PUNT

/** Very minimal panic(), without any string output.  In an attempt to prevent
 *  the host machine hanging when we're booting over PCI-E, we drain the boot
 *  network while we spin.
 * @param code Error code to record.
 */
void
punt(uint32_t code)
{
  __insn_mtspr(SPR_PASS, code);
  __insn_mtspr(SPR_FAIL, code);
  __insn_mtspr(SPR_DONE, code);
  while (1)



    (void) udn0_receive();

}

#endif /* !OVERRIDE_PUNT */


#ifndef OVERRIDE_BOOT_ERROR

/** Error handling function.  Punt or continue based on error code.
 * @param code Error code.
 */
void
boot_error(uint32_t code)
{
  switch (code)
  {
  // Fatal errors: punt.
  case BOOT_ERR_INPUT_INVALID_DIRECTION:
  case BOOT_ERR_NO_MSHIM:
  case BOOT_ERR_MSG_TOO_BIG:
  case BOOT_ERR_UNRECOG_MSG:
  case BOOT_ERR_ACK_BAD_TAG:
  case BOOT_ERR_LOST_ACK:
  case BOOT_ERR_HV_IMAGE_BAD_CRC:
  case BOOT_ERR_CONFIG_BAD_CRC:
  case BOOT_ERR_CFG_WRITE_BAD_RESP:
  case BOOT_ERR_CFG_READ_BAD_RESP:
  case BOOT_ERR_CFG_PROBE_BAD_RESP:
  case BOOT_ERR_BAD_CBOX_MMAP_INDEX:
  case POST_ERR_HV_RAM_FS:
    punt(code);
    break;

  // Non-fatal errors, just continue.
  // An error found in DRAM is not fatal by default.  We can disable
  // the memory controller associated with the failing location and continue.
  case POST_ERR_QUICK_DRAM:
  case POST_ERR_HV_RAM:
    break;

  // Default: consider fatal.
  default:
    punt(code);
    break;
  }
}

#endif /* !OVERRIDE_BOOT_ERROR */
