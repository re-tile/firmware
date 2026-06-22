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
 * Generate the header file for the SROM access assembly code.
 */

#include "asmhdr.h"

#include <arch/chip.h>



#include "hv_l1boot.h"
#include "srom_acc.h"
#include "srom_format.h"
#include "hw_config.h"

ASMHDR_BEGIN()


ASMHDR_DEF(SROM_CHAN)


ASMHDR_DEF(SROM_TRAILER_MAGIC)
ASMHDR_DEF(SROM_IMAGE_MAGIC)

ASMHDR_DEF(REFCLK)

ASMHDR_TYPE(struct srom_image_header, SIH)
ASMHDR_MEMBER(magic, )
ASMHDR_MEMBER(generation, )
ASMHDR_MEMBER(offset, )
ASMHDR_MEMBER(l0_boot_words, )
ASMHDR_MEMBER(l0_boot_crc, )
ASMHDR_MEMBER(total_words, )
ASMHDR_MEMBER(total_crc, )
ASMHDR_MEMBER(comment, )
ASMHDR_MEMBER(header_crc, )
ASMHDR_ENDTYPE()

ASMHDR_END()
