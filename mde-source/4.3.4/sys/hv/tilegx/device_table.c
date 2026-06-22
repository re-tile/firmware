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
 * The device table.
 */

#include <arch/mica.h>
#include <arch/gpio.h>
#include <arch/i2cm.h>
#include <arch/i2cs.h>
#include <arch/msh.h>
#include <arch/mpipe.h>
#include <arch/rsh.h>
#include <arch/srom.h>
#include <arch/trio.h>
#include <arch/uart.h>
#include <arch/usb_device.h>
#include <arch/usb_host.h>

#include "device_table.h"
#include "fuses.h"

static const __DEVICE_ATTR_BASE device_t base_devices[] =
{
  {
    .shim_type = RSH_DEV_INFO__TYPE_VAL_RSHIM,
    .name      = "rshim/0",
    .instance  = 0,
    .desc      = "Miscellaneous I/O Controller",
  },
  {
    .shim_type = MICA_DEV_INFO__TYPE_VAL_COMPRESSION,
    .name      = "comp/0",
    .instance  = 0,
    .desc      = "Compression Accelerator",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_COMPRESS },
  },
  {
    .shim_type = MICA_DEV_INFO__TYPE_VAL_COMPRESSION,
    .name      = "comp/1",
    .instance  = 1,
    .desc      = "Compression Accelerator",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_COMPRESS },
  },
  {
    .shim_type = MICA_DEV_INFO__TYPE_VAL_CRYPTO,
    .name      = "crypto/0",
    .instance  = 2,
    .desc      = "Crypto Accelerator",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_CRYPTO },
  },
  {
    .shim_type = MICA_DEV_INFO__TYPE_VAL_CRYPTO,
    .name      = "crypto/1",
    .instance  = 3,
    .desc      = "Crypto Accelerator",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_CRYPTO },
  },
  {
    .shim_type = MPIPE_DEV_INFO__TYPE_VAL_TRIO,
    .name      = "trio/0",
    .instance  = 0,
    .desc      = "PCI Express Controller",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_TRIO },
  },
  {
    .shim_type = MPIPE_DEV_INFO__TYPE_VAL_TRIO,
    .name      = "trio/1",
    .instance  = 1,
    .desc      = "PCI Express Controller",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_TRIO },
  },
  {
    .shim_type = MPIPE_DEV_INFO__TYPE_VAL_MPIPE,
    .name      = "mpipe/0",
    .instance  = 0,
    .desc      = "Ethernet Controller",
    .n_clocks  = 2,
    .f2v_index = { FUSE_F2V_MPIPE_CORE, FUSE_F2V_MPIPE_CLS },
  },
  {
    .shim_type = MPIPE_DEV_INFO__TYPE_VAL_MPIPE,
    .name      = "mpipe/1",
    .instance  = 1,
    .desc      = "Ethernet Controller",
    .n_clocks  = 2,
    .f2v_index = { FUSE_F2V_MPIPE_CORE, FUSE_F2V_MPIPE_CLS },
  },
  {
    .shim_type = GPIO_DEV_INFO__TYPE_VAL_GPIO,
    .name      = "gpio/0",
    .instance  = 0,
    .desc      = "General Purpose I/O Controller",
  },
  {
    .shim_type = SROM_DEV_INFO__TYPE_VAL_SROM,
    .name      = "srom/0",
    .instance  = 0,
    .desc      = "SPI Flash ROM Controller",
  },
  {
    .shim_type = I2CM_DEV_INFO__TYPE_VAL_I2CM,
    .name      = "i2cm/0",
    .instance  = 0,
    .desc      = "I2C Interface Master Controller",
  },
  {
    .shim_type = I2CM_DEV_INFO__TYPE_VAL_I2CM,
    .name      = "i2cm/1",
    .instance  = 1,
    .desc      = "I2C Interface Master Controller",
  },
  {
    .shim_type = I2CM_DEV_INFO__TYPE_VAL_I2CM,
    .name      = "i2cm/2",
    .instance  = 2,
    .desc      = "I2C Interface Master Controller",
  },
  {
    .shim_type = I2CS_DEV_INFO__TYPE_VAL_I2CS,
    .name      = "i2cs/0",
    .instance  = 0,
    .desc      = "I2C Interface Slave Controller",
  },
  {
    .shim_type = UART_DEV_INFO__TYPE_VAL_UART,
    .name      = "uart/0",
    .instance  = 0,
    .desc      = "Asynchronous Serial Port Controller",
  },
  {
    .shim_type = UART_DEV_INFO__TYPE_VAL_UART,
    .name      = "uart/1",
    .instance  = 1,
    .desc      = "Asynchronous Serial Port Controller",
  },
  {
    .shim_type = MSH_DEV_INFO__TYPE_VAL_DDR3,
    .name      = "mshim/0",
    .instance  = 0,
    .desc      = "Memory Controller",
    .flags     = DEV_FLG_NO_MDN_CFG,
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_MSH },
  },
  {
    .shim_type = MSH_DEV_INFO__TYPE_VAL_DDR3,
    .name      = "mshim/1",
    .instance  = 1,
    .desc      = "Memory Controller",
    .flags     = DEV_FLG_NO_MDN_CFG,
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_MSH },
  },
  {
    .shim_type = MSH_DEV_INFO__TYPE_VAL_DDR3,
    .name      = "mshim/2",
    .instance  = 2,
    .desc      = "Memory Controller",
    .flags     = DEV_FLG_NO_MDN_CFG,
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_MSH },
  },
  {
    .shim_type = MSH_DEV_INFO__TYPE_VAL_DDR3,
    .name      = "mshim/3",
    .instance  = 3,
    .desc      = "Memory Controller",
    .flags     = DEV_FLG_NO_MDN_CFG,
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_MSH },
  },
  //
  // Note that it's important that the USB device shim precede the USB host
  // shim in this list, since we must put the device MAC and PHY in reset
  // before we spin up the shim's single PLL.
  //
  {
    .shim_type = USB_DEVICE_DEV_INFO__TYPE_VAL_USBS,
    .name      = "usb_dev/0",
    .instance  = 0,
    .desc      = "USB Device Controller",
  },
  {
    .shim_type = USB_HOST_DEV_INFO__TYPE_VAL_USBH,
    .name      = "usb_host/0",
    .instance  = 0,
    .desc      = "USB Host Controller",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_USB },
  },
  {
    .shim_type = USB_HOST_DEV_INFO__TYPE_VAL_USBH,
    .name      = "usb_host/1",
    .instance  = 1,
    .desc      = "USB Host Controller",
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_USB },
  },
  {
    .shim_type = 0,
    .name      = "test/0",
    .instance  = 0,
    .desc      = "Phony Test Device for Test Driver",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = 0,
    .name      = "test/1",
    .instance  = 1,
    .desc      = "Phony Test Device for Test Driver",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = 0,
    .name      = "memprof",
    .instance  = 0,
    .desc      = "Phony Device for Memory Profiling",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = 0,
    .name      = "eeprom",
    .instance  = 0,
    .desc      = "I2C EEPROM Device",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = DEV_PSEUDO_WATCHDOG,
    .name      = "watchdog",
    .instance  = 0,
    .desc      = "Watchdog Device",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = DEV_PSEUDO_TMFIFO,
    .name      = "tmfifo",
    .instance  = 0,
    .desc      = "Tile-monitor FIFO Device",
    .flags     = DEV_FLG_PSEUDO,
  },
  {
    .shim_type = DEV_PSEUDO_COREPLL,
    .name      = "corepll",
    .instance  = 0,
    .desc      = "Tile Processor Core PLL",
    .flags     = DEV_FLG_PSEUDO,
    .n_clocks  = 1,
    .f2v_index = { FUSE_F2V_CORE },
  },
  {
    .shim_type = DEV_PSEUDO_SENSOR,
    .name      = "sensor",
    .instance  = 0,
    .desc      = "Temperature Sensor and Fan Control Device",
    .flags     = DEV_FLG_PSEUDO,
  },
};

static const __DEVICE_ATTR_END device_t end_devices[] =
{
  //
  // Name of NULL denotes end of the list
  //
  {
    .name      = NULL,
  },
};
