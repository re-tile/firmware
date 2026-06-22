/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

/**
 * @file
 *
 * Implementation of GPIO gxio calls.
 */


#include "gxio/gpio.h"
#include "gpio_rpc_call.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/mman.h>
#include <sys/stat.h>



int
gxio_gpio_init(gxio_gpio_context_t* context, int gpio_index)
{
  char file[32];
  int fd;


  snprintf(file, sizeof (file), "/dev/iorpc/gpio%d", gpio_index);

  fd = open(file, O_RDWR);

  if (fd < 0)
  {
    return -errno;
  }

  context->fd = fd;

  // Map in our MMIO space.
  context->mmio_base =
    mmap(NULL, HV_GPIO_MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (context->mmio_base == MAP_FAILED)
  {
    close(fd);
    return -errno;
  }


  return 0;
}


int
gxio_gpio_destroy(gxio_gpio_context_t* context)
{
  munmap(context->mmio_base, HV_GPIO_MMIO_SIZE);
  close(context->fd);

  context->mmio_base = NULL;
  context->fd = -1;

  return 0;
}


uint64_t
gxio_gpio_get(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_STATE);
}


void
gxio_gpio_set(gxio_gpio_context_t* context, uint64_t pins, uint64_t mask)
{
  if (pins & mask)
    __gxio_mmio_write(context->mmio_base + GPIO_PIN_SET, pins & mask);
  if (~pins & mask)
    __gxio_mmio_write(context->mmio_base + GPIO_PIN_CLR, ~pins & mask);
}


void
gxio_gpio_toggle(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_OUTPUT_TGL, pins);
}


void
gxio_gpio_pulse_assert(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_PULSE_SET, pins);
}


void
gxio_gpio_pulse_deassert(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_PULSE_CLR, pins);
}


void
gxio_gpio_release(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_RELEASE, pins);
}


uint64_t
gxio_gpio_get_out_inv(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_OUTPUT_INV);
}


void
gxio_gpio_set_out_inv(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_OUTPUT_INV, pins);
}


uint64_t
gxio_gpio_get_in_inv(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_INPUT_INV);
}


void
gxio_gpio_set_in_inv(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_INPUT_INV, pins);
}


uint64_t
gxio_gpio_get_out_mask(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_OUTPUT_MSK);
}


void
gxio_gpio_set_out_mask(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_OUTPUT_MSK, pins);
}


uint64_t
gxio_gpio_get_in_mask(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_INPUT_MSK);
}


void
gxio_gpio_set_in_mask(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_INPUT_MSK, pins);
}


uint64_t
gxio_gpio_get_in_sync(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_INPUT_SYNC);
}


void
gxio_gpio_set_in_sync(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_INPUT_SYNC, pins);
}


uint64_t
gxio_gpio_get_in_cnd(gxio_gpio_context_t* context)
{
  return __gxio_mmio_read(context->mmio_base + GPIO_PIN_INPUT_CND);
}


void
gxio_gpio_set_in_cnd(gxio_gpio_context_t* context, uint64_t pins)
{
  __gxio_mmio_write(context->mmio_base + GPIO_PIN_INPUT_CND, pins);
}


void
gxio_gpio_report_interrupt(gxio_gpio_context_t* context,
                           uint64_t* asserted, uint64_t* deasserted)
{
  if (asserted)
    *asserted = __gxio_mmio_read(context->mmio_base + GPIO_INT_VEC0_W1TC);
  if (deasserted)
    *deasserted = __gxio_mmio_read(context->mmio_base + GPIO_INT_VEC1_W1TC);
}


void
gxio_gpio_report_reset_interrupt(gxio_gpio_context_t* context,
                                 uint64_t* asserted, uint64_t* deasserted)
{
  if (asserted)
    *asserted = __gxio_mmio_read(context->mmio_base + GPIO_INT_VEC0_RTC);
  if (deasserted)
    *deasserted = __gxio_mmio_read(context->mmio_base + GPIO_INT_VEC1_RTC);
}


void
gxio_gpio_reset_interrupt(gxio_gpio_context_t* context,
                          uint64_t en_assert, uint64_t en_deassert)
{
  if (en_assert)
    __gxio_mmio_write(context->mmio_base + GPIO_INT_VEC0_W1TC, en_assert);
  if (en_deassert)
    __gxio_mmio_write(context->mmio_base + GPIO_INT_VEC1_W1TC, en_deassert);
}



int
gxio_gpio_get_pollfd(gxio_gpio_context_t* context,
                     uint64_t on_assert, uint64_t on_deassert)
{
  int fd = open("/dev/iorpc/pollfd", O_RDWR);

  if (fd < 0)
    return -errno;

  int err = gxio_gpio_cfg_pollfd(context, fd, on_assert, on_deassert);

  if (err < 0)
  {
    close(fd);
    return err;
  }

  return fd;
}


int
gxio_gpio_get_pinset(gxio_gpio_context_t* context, const char* name,
                     uint64_t* input_pins, uint64_t* output_pins,
                     uint64_t* output_od_pins, uint64_t* inverted_pins)
{
  int idx = gxio_gpio_get_pinset_aux(context, name, strlen(name));
  if (idx < 0)
    return idx;

  return gxio_gpio_enumerate_pinset_aux(context, idx, input_pins,
                                        output_pins, output_od_pins,
                                        inverted_pins, NULL, 0);
}

int
gxio_gpio_enumerate_pinset(gxio_gpio_context_t* context, int idx,
                           char* name, uint64_t* input_pins,
                           uint64_t* output_pins, uint64_t* output_od_pins,
                           uint64_t* inverted_pins)
{
  return gxio_gpio_enumerate_pinset_aux(context, idx, input_pins,
                                        output_pins, output_od_pins,
                                        inverted_pins, name,
                                        GXIO_GPIO_PINSET_NAME_LEN);
}
