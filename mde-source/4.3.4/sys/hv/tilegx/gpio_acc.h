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
 * Routines to maninpulate GPIO pins.
 */

#ifndef _SYS_HV_TILEGX_GPIO_ACC_H
#define _SYS_HV_TILEGX_GPIO_ACC_H

#include <stdint.h>

void gpio_raw_drive_pins(unsigned int bank, uint64_t value, uint64_t mask);
uint64_t gpio_raw_sense_pins(unsigned int bank);
void gpio_raw_invert_input(unsigned int bank, uint64_t inv_map, uint64_t mask);
void gpio_raw_set_dir(unsigned int bank, uint64_t disabled_pins,
                      uint64_t input_pins, uint64_t output_pins,
                      uint64_t output_od_pins);
void gpio_raw_cfg_interrupt(unsigned int bank, int x, int y, int inter_ipi,
                            int inter_event, int pin, int invert);
int gpio_raw_get_clear_interrupt(unsigned int bank, int pin, int invert);
void gpio_raw_mask_interrupt(unsigned int bank, int pin, int invert);
void gpio_raw_unmask_interrupt(unsigned int bank, int pin, int invert);

#endif /* _SYS_HV_TILEGX_GPIO_ACC_H */
