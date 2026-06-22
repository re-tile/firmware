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
 *
 */

/**
 * @file
 * Definition of the board information block, and accessor routines.
 */

#ifndef _SYS_COMMON_HVBME_BOARD_INFO_H
#define _SYS_COMMON_HVBME_BOARD_INFO_H

#include <stdint.h>

#ifdef __HV__
#include "hvbme/board_info_common.h"
#else
#include "board_info_common.h"
#endif

/**
 * @name Descriptor manipulation macros
 * @{
 */

/** Construct a descriptor.
 * @param type Descriptor type (BI_TYPE_xxx).
 * @param instance Instance number (use depends upon type).
 * @param wds Number of words of data which follow descriptor.
 */
#define BI_MKDESC(type, instance, wds) \
                                  (((type) << 16) | ((instance) << 8) | (wds))
/** Extract type from a descriptor */
#define BI_TYPE(desc) ((uint32_t) (desc) >> 16)
/** Extract instance from a descriptor */
#define BI_INST(desc) (((desc) >> 8) & 0xFF)
/** Instance type */
typedef unsigned char bi_inst_t;
/** Extract word count from a descriptor */
#define BI_WDS(desc)  ((desc) & 0xFF)
/** Extract byte count from a descriptor */
#define BI_BYTES(desc)  (((desc) & 0xFF) << 2)
/** @} */

/**
 * @name Item descriptor types
 * @{
 */

//
// CODE BELOW THIS POINT IS AUTOMATICALLY GENERATED -- DO NOT EDIT
//

/** I2C Address.  This structure describes an I2C device attached to a Tile
 *  Processor, and is used in several of the BIB item types. */
struct bi_i2c_addr
{
  /** I2C address of the device.  In the binary representation, this is the
   *  7-bit address (the low read/write bit is omitted). */
  uint8_t dev_addr: 7;

  /** Number of the I2C slave bus to which the device is attached (0-2). */
  uint8_t bus: 3;

  /** I2C switch instance.  Some devices are only reachable via I2C
   *  switches; the switch must be activated and the proper switch channel
   *  selected before the device may be read or written.  For such devices,
   *  this field will be nonzero, and will be part of the instance number of
   *  an item of type I2C_SWITCH which describes the relevant switch.  (The
   *  switch item's instance number also uses bits from the bus number; see
   *  the description of that item type for details.)  If this field is
   *  NONE, no I2C switch need be configured to read or write this device.
   */
  uint8_t switch_inst: 3;

  /** No I2C switch need be configured to read or write this device. */
#define BI_I2C_ADDR_SWITCH_INST__VAL_NONE 7

  /** I2C switch channel.  This field is only relevant if the switch field
   *  is not NONE; in that case, it denotes the channel on the switch which
   *  must be activated in order to communicate with this device.
   *  Otherwise, this field must be zero. */
  uint8_t switch_chan: 3;

} __attribute__((packed, aligned(1)));


/** GPIO-specific information in a signal descriptor. */
struct bi_signal_gpio
{
  /** GPIO pin number. */
  uint8_t pin: 6;

  /** GPIO controller number.  This is for future expansion and must be zero
   *  on TILE-Gx chips. */
  uint8_t bank: 2;

  /** If true, the GPIO pin is configured as an open-drain pin.  This bit is
   *  ignored on signal descriptors which describe inputs. */
  uint8_t open_drain: 1;

  /** If true, signal is asserted when the pin is low. */
  uint8_t inverted: 1;

} __attribute__((packed, aligned(1)));


/** I2C-specific information in a signal descriptor. */
struct bi_signal_i2c
{
  /** GPIO expander type. */
  uint8_t type: 2;

  /** PCA9555 or equivalent. */
#define BI_SIGNAL_I2C_TYPE__VAL_PCA9555 0

  /** Reserved: must be zero. */
  long : 1;

  /** GPIO expander pin number. */
  uint8_t pin: 4;

  /** If true, signal is asserted when the pin is low. */
  uint8_t inverted: 1;

  /** I2C address of the GPIO port expander. */
  struct bi_i2c_addr addr;

} __attribute__((packed, aligned(1)));


/** Reset-specific information in a signal descriptor. */
struct bi_signal_reset
{
  /** If true, reset is asserted when the pin is low. */
  uint8_t inverted: 1;

} __attribute__((packed, aligned(1)));


/** Fixed-specific information in a signal descriptor. */
struct bi_signal_fixed
{
  /** If true, the pin is tied high; otherwise, it is tied low. */
  uint8_t value: 1;

} __attribute__((packed, aligned(1)));


/** Ethernet interrupt-specific information in a signal descriptor. */
struct bi_signal_enet_int
{
  /** ENET_INT pin number. */
  uint8_t pin: 2;

  /** Reserved: must be zero. */
  long : 1;

  /** If true, signal is asserted when the pin is low.  NOTE: this item is
   *  included for future expansion.  On current TILE-Gx processors, only
   *  active high interrupts are supported on ENET_INT pins; 10 Gbps
   *  Ethernet links whose PHYS have active low interrupts should wire those
   *  interrupts to an SGMII_INT pin instead, and then use the GPIO signal
   *  type. */
  uint8_t inverted: 1;

} __attribute__((packed, aligned(1)));


/** Many items contain descriptions of how a particular board signal is wired
 *  to the Tile Processor. These descriptions share a common format, which is
 *  called a signal descriptor and defined by this structure. */
struct bi_signal
{
  /** Type of signal. */
  uint8_t type;

  /** This signal is not connected. */
#define BI_SIGNAL_TYPE__VAL_NONE 0

  /** The signal is connected to a processor GPIO pin. */
#define BI_SIGNAL_TYPE__VAL_GPIO 1

  /** The signal is connected to an I2C GPIO expander pin. */
#define BI_SIGNAL_TYPE__VAL_I2C 2

  /** The signal is connected to the TILE Processor's reset pin; this is
   *  generally used only with the output signal for watchdog timers. */
#define BI_SIGNAL_TYPE__VAL_RESET 3

  /** The signal is tied to a fixed value. */
#define BI_SIGNAL_TYPE__VAL_FIXED 4

  /** The signal is tied to one of the dedicated 10 Gbps Ethernet interrupt
   *  pins (ENET_INT[0:3]).  Note that some pins are designated as both GPIO
   *  pins and SGMII Ethernet interrupt pins; these should use the GPIO
   *  signal type, not this type. */
#define BI_SIGNAL_TYPE__VAL_ENET_INT 5

  /** Information for specific types of signals. */
  union
  {
    struct bi_signal_gpio gpio;

    struct bi_signal_i2c i2c;

    struct bi_signal_reset reset;

    struct bi_signal_fixed fixed;

    struct bi_signal_enet_int enet_int;

  } u;

} __attribute__((packed, aligned(1)));


/** Nominal tile clock in Hz.  Typically this item is not included in the BIB;
 *  in that case, the maximum frequency, based on the CPU's speed grade and
 *  the board's voltage capability, is used.  System software can override the
 *  processor frequency within limits set by the processor. */
struct bi_nom_tile_freq
{
  /** Nominal tile clock in Hz. */
  uint32_t clock;

} __attribute__((packed, aligned(4)));

/** Type code for bi_nom_tile_freq. */
#define BI_TYPE_NOM_TILE_FREQ 3


/** Board part number.  Tilera's board part numbers are of the following
 *  format:  402-ddddd-dd  where d is a digit from 0 to 9. Although it is not
 *  a desired practice, some parts of the current system software stack may
 *  make assumptions about the board's properties based on this number. Thus,
 *  you are advised to avoid part numbers that might be confused with
 *  Tilera's.  This item's value may be retrieved from the
 *  /sys/hypervisor/board/board_part file under Tile Linux. Its presence is
 *  highly recommended, but not required. */
struct bi_board_part_num
{
  /** Part number. */
  char part_num[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_board_part_num. */
#define BI_TYPE_BOARD_PART_NUM 5


/** Board serial number.  This item's value may be retrieved from the
 *  /sys/hypervisor/board/board_serial file under Tile Linux. Its presence is
 *  not required. */
struct bi_board_serial_num
{
  /** Serial number. */
  char serial_num[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_board_serial_num. */
#define BI_TYPE_BOARD_SERIAL_NUM 6


/** Chip serial number.  This item's value may be retrieved from the
 *  /sys/devices/system/cpu/chip_serial file under Tile Linux. Its presence is
 *  not required. */
struct bi_chip_num
{
  /** Chip number. */
  char chip_num[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_chip_num. */
#define BI_TYPE_CHIP_NUM 7


/** This entry limits the speed at which a memory shim's memory is run. The
 *  entries in the speed array are indexed by the number of DIMMs installed;
 *  if one is installed, the first array entry is used, if two are installed,
 *  the second array entry is used, etc.  If there are fewer array entries
 *  than installed DIMMs, the last entry in the array will be used.  Note that
 *  this is a board property.  The actual speed will be the lowest of this
 *  value; the maximum speed supported by the processor; and the maximum speed
 *  supported by the installed memory, as described by the SPD for a DIMM or a
 *  virtual SPD for an onboard memory bank.  Other factors evaluated by the
 *  memory controller configuration software may also result in a lower memory
 *  speed. */
struct bi_max_mem_speed
{
  /** Maximum memory speed in millions of transactions per second. */
  uint16_t speed[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_max_mem_speed. */
#define BI_TYPE_MAX_MEM_SPEED 8


/** Board revision.  This item's value may be retrieved from the
 *  /sys/hypervisor/board/board_revision file under Tile Linux. Its presence
 *  is strongly recommended but not required. */
struct bi_board_rev
{
  /** Board revision. */
  char board_rev[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_board_rev. */
#define BI_TYPE_BOARD_REV 14


/** Entry in a DIMM map. */
struct bi_dimm_map_entry
{
  /** If nonzero, this entry describes onboard memory, not a DIMM slot. */
  uint8_t onboard: 1;

  /** Reserved: must be zero. */
  long : 15;

  /** Per-DIMM information. */
  union
  {
    /** I2C address information for the SPD ROM on this DIMM. */
    struct bi_i2c_addr i2c;

    /** Instance number of the SPD_DATA item which contains the SPD bytes
     *  for this onboard memory bank. */
    uint16_t onboard_inst;

  } addr;

} __attribute__((packed, aligned(1)));


/** The DIMM map describes how memory DIMMs or onboard memories are wired to a
 *  memory shim, as well as how software can access data about those memories.
 *  Note that this item describes the availability of sockets for DIMMs, not
 *  the presence of DIMMs themselves; the latter is determined by means of
 *  probing for the relevant SPD PROM. This item does not need to be modified
 *  if DIMMs are added to or removed from sockets on the board.  One instance
 *  of this item is required for each memory shim, unless the shim is to be
 *  unused. */
struct bi_dimm_map
{
  /** Number of chip select lines connected to each DIMM slot or onboard
   *  memory bank on this mshim; must be 1, 2, or 4.  Chip select lines are
   *  assumed to be used in order.  Thus, line 0 is always assigned to CS0
   *  on DIMM slot/onboard bank 0; if N lines are connected to each slot,
   *  then slot M gets chip select lines (M * N) to (M * N) + N - 1. */
  uint8_t cs_per_slot: 3;

  /** Reserved: must be zero. */
  long : 5;

  /** Length of the DQS (data strobe) traces as compared to the DQ (data)
   *  traces, in picoseconds. */
  int8_t dqs_offset;

  /** Reserved: must be zero. */
  long : 16;

  /** DIMM map entries, 1 per DIMM. */
  struct bi_dimm_map_entry map[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_dimm_map. */
#define BI_TYPE_DIMM_MAP 15


/** This entry contains Serial Presence Detect data for onboard memory. We
 *  recommend constructing this virtual SPD using the latest available version
 *  of the JEDEC DDR3 SPD specification. */
struct bi_spd_data
{
  /** SPD data. */
  uint8_t spd[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_spd_data. */
#define BI_TYPE_SPD_DATA 16


/** Instance number for DIMM-based items, like the DIMM label. */
struct bi_dimm_inst
{
  /** DIMM number within a shim. */
  uint8_t dimm: 3;

  /** Memory shim instance number. */
  uint8_t shim: 3;

} __attribute__((packed, aligned(1)));


/** Board silkscreen label or other human-readable identifier for a DIMM.
 *  This is intended to be used in error reporting by automated diagnosis
 *  routines. This item's presence is not required, but its omission will
 *  potentially lead to less useful error messages when memory errors are
 *  detected. */
struct bi_dimm_label
{
  /** DIMM label. */
  char label[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_dimm_label. */
#define BI_TYPE_DIMM_LABEL 17


/** This structure, known as a fan descriptor, describes a fan attached to a
 *  fan controller. */
struct bi_fan_info
{
  /** Maximum fan speed, in hundreds of revolutions per minute.  If zero, no
   *  fan is connected. */
  uint8_t max_speed;

  /** Tachometer pulses per revolution, minus 1.  Thus, a value of 0 in this
   *  field denotes a tachometer which produces one pulse per fan
   *  revolution. */
  uint8_t tach_ppr: 2;

  /** Four wire fan flag.  If true, this is a four-wire fan, with tachometer
   *  speed sending and direct PWM control.  If false, this is a 3-wire fan,
   *  with tachometer speed sensing only. */
  uint8_t four_wire: 1;

  /** PWM active low flag.  If true, the PWM output is active low; i.e., it
   *  should be low all the time when the fan is running at a 100% duty
   *  cycle. */
  uint8_t pwm_act_low: 1;

  /** Reserved: must be zero. */
  long : 3;

  /** Temperature channel valid flag.  If true, the temperature channel is
   *  actually connected to an external diode; if false, it is not
   *  connected. */
  uint8_t temp_valid: 1;

} __attribute__((packed, aligned(1)));


/** Temperature configuration specialized for MAX6639 controller.  Note that
 *  if this sensor has instance zero, and thus monitors processor temperature,
 *  that measurement must be done via temperature channel 1. Also, in that
 *  case, if fan 1 is marked as being present, it is assumed to be part of an
 *  active heatsink for the processor. */
struct bi_temp_cfg_max6639
{
  /** Fan descriptors. */
  struct bi_fan_info fans[2];

  /** Signal descriptors.  If present, the described signals will be
   *  asserted when the controller is initialized. This is intended to
   *  support systems where the selection of 3-wire vs. 4-wire fans is made
   *  via a GPIO signal, although it could be used for other purposes, like
   *  modifying system overtemperature behavior. */
  struct bi_signal sigs[0];

} __attribute__((packed, aligned(1)));


/** Temperature configuration specialized for ADT7467 controller.  Note that
 *  if this sensor has instance zero, and thus monitors processor temperature,
 *  that measurement must be done via temperature channel 1. Also, in that
 *  case, if fan 1 is marked as being present, it is assumed to be part of an
 *  active heatsink for the processor. */
struct bi_temp_cfg_adt7467
{
  /** Fan descriptors.  As recommended by ON Semi, if there are 4 fans, fans
   *  3 and 4 have individual tachometer sensing inputs (TACH3/TACH4) but
   *  are both driven from the fan 3 output (PWM3). */
  struct bi_fan_info fans[4];

  /** Signal descriptors.  If present, the described signals will be
   *  asserted when the controller is initialized. This is intended to
   *  support systems where the selection of 3-wire vs. 4-wire fans is made
   *  via a GPIO signal, although it could be used for other purposes, like
   *  modifying system overtemperature behavior. */
  struct bi_signal sigs[0];

} __attribute__((packed, aligned(1)));


/** Temperature sensor configuration. */
struct bi_temp_cfg
{
  /** Sensor type. */
  uint16_t type;

  /** Unknown sensor. */
#define BI_TEMP_CFG_TYPE__VAL_UNKNOWN 0

  /** Sensor is a National Semiconductor LM84 or equivalent. */
#define BI_TEMP_CFG_TYPE__VAL_LM84 1

  /** Sensor is a National Semiconductor LM95235 or equivalent. */
#define BI_TEMP_CFG_TYPE__VAL_LM95235 2

  /** Sensor is a Maxim MAX6639 or equivalent. */
#define BI_TEMP_CFG_TYPE__VAL_MAX6639 3

  /** Sensor is an ON Semiconductor ADT7467 or equivalent. */
#define BI_TEMP_CFG_TYPE__VAL_ADT7467 4

  /** I2C address of the sensor. */
  struct bi_i2c_addr addr;

  /** Information for specific types of controllers. */
  union
  {
    struct bi_temp_cfg_max6639 max6639;

    struct bi_temp_cfg_adt7467 adt7467;

  } u;

} __attribute__((packed, aligned(4)));

/** Type code for bi_temp_cfg. */
#define BI_TYPE_TEMP_CFG 25


/** Board description.  This is intended to be a human-readable description of
 *  the board's properties, and is explicitly not designed to be parsed by
 *  software; its format is therefore undefined. */
struct bi_board_description
{
  /** Board description. */
  char desc[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_board_description. */
#define BI_TYPE_BOARD_DESCRIPTION 26


/** Wiring of the board failure LED.  If present, the described signal will be
 *  deasserted during the boot process if there have been no POST failures.
 *  The intent is that the LED is lit at reset and stays lit until that
 *  happens; thus, if the LED is lit, either there has been a POST failure, or
 *  the system did not make sufficient progress through the boot process to
 *  douse the LED. */
struct bi_fail_led
{
  /** Board failure LED signal. */
  struct bi_signal signal;

} __attribute__((packed, aligned(4)));

/** Type code for bi_fail_led. */
#define BI_TYPE_FAIL_LED 27


/** Revision level of low-level board firmware (e.g., CPLD or FPGA
 *  configuration data).  This value is used during manufacturing.  Its
 *  presence is not required. */
struct bi_firmware_rev
{
  /** Firmware revision. */
  char rev[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_firmware_rev. */
#define BI_TYPE_FIRMWARE_REV 29


/** Board bill of materials revision level.  This value is used during
 *  manufacturing.  Its presence is not required. */
struct bi_bom_rev
{
  /** BOM revision. */
  char rev[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_bom_rev. */
#define BI_TYPE_BOM_REV 30


/** PCF8563-specific RTC information. */
struct bi_rtc_pcf8563
{
  /** I2C address of the device. */
  struct bi_i2c_addr addr;

} __attribute__((packed, aligned(1)));


/** Describes the interface to a real-time clock chip. */
struct bi_rtc_cfg
{
  /** Clock type. */
  uint8_t type;

  /** No clock. */
#define BI_RTC_CFG_TYPE__VAL_NONE 0

  /** Clock is an NXP PCF8563 or equivalent. */
#define BI_RTC_CFG_TYPE__VAL_PCF8563 1

  /** Information for specific types of clocks. */
  union
  {
    struct bi_rtc_pcf8563 pcf8563;

  } u;

} __attribute__((packed, aligned(4)));

/** Type code for bi_rtc_cfg. */
#define BI_TYPE_RTC_CFG 34


/** I2C-specific AIB information. */
struct bi_aib_i2c
{
  /** I2C address of the device which contains the AIB. */
  struct bi_i2c_addr addr;

} __attribute__((packed, aligned(1)));


/** Points to a potential location for an additional information block. */
struct bi_aib
{
  /** AIB type. */
  uint8_t type;

  /** No AIB. */
#define BI_AIB_TYPE__VAL_NONE 0

  /** AIB in an I2C EEPROM with 16-bit addressing. */
#define BI_AIB_TYPE__VAL_I2C 1

  /** Information for specific types of AIBs. */
  union
  {
    struct bi_aib_i2c i2c;

  } u;

} __attribute__((packed, aligned(4)));

/** Type code for bi_aib. */
#define BI_TYPE_AIB 36


/** MAX6369-specific watchdog information. */
struct bi_watch_max6369
{
  /** I2C address of the device. */
  struct bi_i2c_addr addr;

  /** Wiring for the SET0 pin. */
  struct bi_signal set0;

  /** Wiring for the SET1 pin. */
  struct bi_signal set1;

  /** Wiring for the SET0 pin. */
  struct bi_signal set2;

  /** Wiring for the WDI pin. */
  struct bi_signal wdi;

  /** Wiring for the WDO pin. */
  struct bi_signal wdo;

} __attribute__((packed, aligned(1)));


/** PCF8563-specific watchdog information. */
struct bi_watch_pcf8563
{
  /** I2C address of the device.  Note that this may be the same device
   *  which is described by an RTC_CFG item elsewhere in the BIB. */
  struct bi_i2c_addr addr;

  /** Descriptor for a timer enable signal.  If present, this signal will be
   *  asserted when the watchdog is enabled.  It is typically ANDed with the
   *  reset output of the watchdog chip, and is used to protect against
   *  inadvertent reset when the watchdog is not enabled. */
  struct bi_signal enable;

} __attribute__((packed, aligned(1)));


/** Describes the interface to a watchdog timer. */
struct bi_watch_cfg
{
  /** Watchdog type. */
  uint8_t type;

  /** No watchdog. */
#define BI_WATCH_CFG_TYPE__VAL_NONE 0

  /** Watchdog is a Maxim MAX6369 or equivalent. */
#define BI_WATCH_CFG_TYPE__VAL_MAX6369 1

  /** Watchdog is an NXP PCF8563 or equivalent. */
#define BI_WATCH_CFG_TYPE__VAL_PCF8563 1

  /** Information for specific types of watchdog timers. */
  union
  {
    struct bi_watch_max6369 max6369;

    struct bi_watch_pcf8563 pcf8563;

  } u;

} __attribute__((packed, aligned(4)));

/** Type code for bi_watch_cfg. */
#define BI_TYPE_WATCH_CFG 37


/** This item describes the wiring of a board power-off signal. If defined,
 *  this signal will be asserted when a system power-off is requested (for
 *  instance, via the Linux shutdown -h command). */
struct bi_poweroff
{
  /** Poweroff signal. */
  struct bi_signal signal;

} __attribute__((packed, aligned(4)));

/** Type code for bi_poweroff. */
#define BI_TYPE_POWEROFF 41


/** This item describes customer-specific devices that are intended to be
 *  accessible from the client OS. */
struct bi_i2c_dev_cfg
{
  /** I2C address of the device. */
  struct bi_i2c_addr addr;

  /** If true, the device uses 8-bit memory addresses.  If neither
   *  mem_addr_8bit nor mem_addr_0bit are set, the device is treated as
   *  16-bit. */
  uint8_t mem_addr_8bit: 1;

  /** If true, the device does not use memory addresses.  If neither
   *  mem_addr_8bit nor mem_addr_0bit are set, the device is treated as
   *  16-bit. */
  uint8_t mem_addr_0bit: 1;

  /** log base 2 of the page size to use when accessing this device.  The
   *  page size affects I2C writes: the controller hardware inserts a delay
   *  (controlled by the write_cycle member of this item) when a page's
   *  worth of bytes have been written.  This is intended to accommodate
   *  devices like EEPROMs which require such delays for correct operation.
   *  Many devices have more relaxed requirements, and may thus perform
   *  better with larger page sizes or smaller delays.  The I2C master
   *  hardware only supports page sizes of up to 64 bytes (log2 page size of
   *  6).  If a value larger than that is specified here, reads or writes
   *  longer than 64 bytes will be done as a sequence of 64-byte I2C
   *  transactions without intermediate STOP conditions; instead, each
   *  transaction after the first is initiated with a repeated START. This
   *  is intended for use with devices that support multiple writes, all of
   *  which take effect atomically when a STOP is seen.  However, it may not
   *  be appropriate for all devices.  For instance, some I2C EEPROMs
   *  support a page size larger than 64 bytes, but abort a write in
   *  progress when a repeated START condition is seen.  Thus, setting this
   *  value to such a device's native page size will result in data
   *  corruption on writes, and should be avoided. */
  uint8_t page_size: 4;

  /** Hardware default (8 bytes). */
#define BI_I2C_DEV_CFG_PAGE_SIZE__VAL_DEFAULT 0

  /** Delay between writes of I2C pages (see the page_size member of this
   *  item). This may be 1 to 8 milliseconds, or one of the defined settings
   *  (default, max, min).  Note that due to backwards compatibility
   *  requirements, the default is much larger than required for most
   *  devices. */
  uint8_t write_cycle: 4;

  /** Hardware default (5 ms). */
#define BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_DEFAULT 0

  /** Hardware maximum (8.4 ms). */
#define BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_MAX 9

  /** Hardware minimum (80 ns). */
#define BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_MIN 10

  /** Reserved: must be zero. */
  long : 6;

  /** Name of the device.  This field is optional.  Names longer than 19
   *  characters will be silently truncated. */
  char name[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_i2c_dev_cfg. */
#define BI_TYPE_I2C_DEV_CFG 43


/** Instance number for items describing I2C switches. */
struct bi_i2c_switch_inst
{
  /** I2C master shim within the chip.  Starts at 0, and increments by 1 for
   *  each shim. */
  uint8_t shim: 4;

  /** I2C switch instance within I2C bus hanging off the given shim.  Must
   *  be unique for each switch within a bus, since other items use this
   *  value to identify a specific switch. */
  uint8_t switch_inst: 4;

} __attribute__((packed, aligned(1)));


/** Description of an I2C switch. */
struct bi_i2c_switch
{
  /** Switch type. */
  uint8_t type;

  /** No switch. */
#define BI_I2C_SWITCH_TYPE__VAL_NONE 0

  /** NXP PCA9543, PCA9545, PCA9546, or PCA9548 switch or equivalent. */
#define BI_I2C_SWITCH_TYPE__VAL_PCA954X_SWITCH 1

  /** NXP PCA9540, PCA9542, or PCA9544 multiplexer or equivalent. */
#define BI_I2C_SWITCH_TYPE__VAL_PCA954X_MUX 2

  /** NXP PCA9547 multiplexer or equivalent. */
#define BI_I2C_SWITCH_TYPE__VAL_PCA9547 3

  /** I2C address of the switch.  In the binary representation, this is the
   *  7-bit address (the low read/write bit is omitted). */
  uint8_t dev_addr: 7;

  /** Bitmap of downstream switch ports that will be disabled immediately
   *  after use; to reduce the cost of I2C transactions, ports not specfied
   *  in this mask are typically left enabled until a different downstream
   *  port is selected on that switch.  This setting is useful in cases
   *  where multiple switches on a bus have downstream devices with
   *  identical I2C addresses.  For instance, consider a system with
   *  switches A and B on a bus, each with four downstream legs, where leg 0
   *  on each switch contains an EEPROM at I2C address 0xA0. Assume the
   *  EEPROM behind switch A is accessed, which would require enabling its
   *  downstream leg 0.  In the absence of this member, a subsequent access
   *  to the EEPROM behind switch B might fail, since once switch B's
   *  downstream leg 0 was enabled, two devices at 0xA0 would be visible to
   *  the I2C master. If this member were set to 0x1 for both switches,
   *  their leg 0s would be disabled after accessing the respective EEPROM,
   *  and no conflict would occur. */
  uint8_t conflict_ports: 8;

  /** Reserved: must be zero. */
  long : 9;

  /** Reset signal. */
  struct bi_signal reset;

} __attribute__((packed, aligned(4)));

/** Type code for bi_i2c_switch. */
#define BI_TYPE_I2C_SWITCH 46


/** This item describes reset signals which need to be asserted as part of the
 *  hypervisor boot process; these signals normally reset a variety of devices
 *  on the board.  Note that other, more specific reset items should be used
 *  if possible; for instance, the PHY_LINK_CFG item describes the reset
 *  signal for PHYs.  During boot, the hypervisor will process the items of
 *  this type in the order that they appear in the BIB.  When each item is
 *  processed, the hypervisor will assert each reset signal in resets[]; delay
 *  for the number of microseconds in assert_time; and then deassert each
 *  reset signal. */
struct bi_misc_reset
{
  /** Reset assertion time in microseconds. */
  uint32_t assert_time;

  /** Reset signals to be asserted at startup. */
  struct bi_signal resets[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_misc_reset. */
#define BI_TYPE_MISC_RESET 49


/** This item allows I/O devices to be disabled at boot time.  Disabled
 *  devices will not show up when software enumerates on-chip hardware and
 *  will thus be unusable.  This is the best way to ensure that a device which
 *  is always unused in a particular system will consume the lowest amount of
 *  power. Note that in some chip series, some devices are always disabled.
 *  This entry cannot enable those devices; it can only disable additional
 *  devices. */
struct bi_io_disable
{
  /** Values to be ORed into the rshim's IO_DISABLE registers; the first
   *  element in the array corresponds to IO_DISABLE0, the second to
   *  IO_DISABLE1, etc.  For an explanation of which devices this affects,
   *  see the rshim documentation. */
  uint64_t disable[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_io_disable. */
#define BI_TYPE_IO_DISABLE 51


/** This item specifies the available range of values for the CPU core
 *  voltage, in uV.  If the system does not provide a variable CPU voltage,
 *  both the vmin and vmax values should be set to the fixed voltage which is
 *  provided. If this entry is not present, software will assume that the full
 *  range of voltage values expressible by the CPU's 6-bit subset of the VR11
 *  VID code is available (0.8818V to 1.2125V). */
struct bi_cpu_volt_range
{
  /** Minimum CPU voltage, in uV. */
  uint32_t vmin;

  /** Maximum CPU voltage, in uV. */
  uint32_t vmax;

} __attribute__((packed, aligned(4)));

/** Type code for bi_cpu_volt_range. */
#define BI_TYPE_CPU_VOLT_RANGE 52


/** This item specifies the available range of values for the DDR3 voltage, in
 *  uV.  If the system does not provide a variable memory voltage, both the
 *  vmin and vmax values should be set to the fixed voltage which is provided.
 *  If this entry is not present, software will assume that the full range of
 *  voltage values expressible by the CPU's 4-bit subset of the VR11 VID code
 *  is available (1.2V to 1.6V). */
struct bi_mem_volt_range
{
  /** Minimum DDR3 voltage, in uV. */
  uint32_t vmin;

  /** Maximum DDR3 voltage, in uV. */
  uint32_t vmax;

} __attribute__((packed, aligned(4)));

/** Type code for bi_mem_volt_range. */
#define BI_TYPE_MEM_VOLT_RANGE 53


/** Instance number for items which relate to shim clocks. */
struct bi_clock_inst
{
  /** Shim type. */
  uint8_t type: 4;

  /** MiCA cryptographic shim. */
#define BI_CLOCK_INST_TYPE__VAL_MICA_CRYPTO 0

  /** MiCA compression shim. */
#define BI_CLOCK_INST_TYPE__VAL_MICA_COMPRESS 1

  /** TRIO PCIe shim. */
#define BI_CLOCK_INST_TYPE__VAL_TRIO 2

  /** USB shim. */
#define BI_CLOCK_INST_TYPE__VAL_USB 3

  /** mPIPE main. */
#define BI_CLOCK_INST_TYPE__VAL_MPIPE_MAIN 4

  /** mPIPE classifier. */
#define BI_CLOCK_INST_TYPE__VAL_MPIPE_CLASSIFIER 5

  /** Shim instance within the specified type. */
  uint8_t shim: 3;

} __attribute__((packed, aligned(1)));


/** This item specifies the clock frequency for I/O shim clocks.  Typically
 *  this frequency is set automatically based on the chip's speed grade and
 *  the board's voltage capability.  This item is only useful if a particular
 *  system desires to run a shim's clock slower than the chip's default value,
 *  perhaps to save power.  Note that the best power saving can be achieved by
 *  turning off a shim entirely; see the IO_DISABLE item. */
struct bi_shim_clock
{
  /** Shim frequency, in hertz. */
  uint32_t freq;

} __attribute__((packed, aligned(4)));

/** Type code for bi_shim_clock. */
#define BI_TYPE_SHIM_CLOCK 54


/** This structure describes how a LED signal on an Ethernet PHY should be
 *  configured. */
struct bi_enet_led
{
  /** Describe the behavior desired for the LED.  The behavior which is
   *  actually achievable depends on the capabilities of the installed PHY;
   *  if it cannot support the requested behavior the results are undefined.
   *  In some cases the setting for one LED may limit the choices available
   *  for other LEDs. Also, note that some configurations supported by the
   *  PHY hardware may not necessarily be supported by the hypervisor PHY
   *  drivers. */
  uint8_t cfg: 5;

  /** LED does not exist. */
#define BI_ENET_LED_CFG__VAL_NONE 0

  /** LED is always off. */
#define BI_ENET_LED_CFG__VAL_OFF 1

  /** LED is always on. */
#define BI_ENET_LED_CFG__VAL_ON 2

  /** LED shows link up/down status (i.e., is on if the link is up). */
#define BI_ENET_LED_CFG__VAL_LINK 3

  /** LED shows link status and activity (i.e., is on if the link is up, off
   *  if not, and blinks if there is transmit or receive activity). */
#define BI_ENET_LED_CFG__VAL_LINK_ACT 4

  /** LED shows link status and transmit activity (i.e., is on if the link
   *  is up, off if not, and blinks if there is transmit activity). */
#define BI_ENET_LED_CFG__VAL_LINK_ACT_TX 5

  /** LED shows link status and receive activity (i.e., is on if the link is
   *  up, off if not, and blinks if there is receive activity). */
#define BI_ENET_LED_CFG__VAL_LINK_ACT_RX 6

  /** LED shows Tx/Rx activity (i.e., blinks if packets are sent or
   *  received). */
#define BI_ENET_LED_CFG__VAL_ACT 7

  /** LED shows transmit activity (i.e., blinks if packets are sent). */
#define BI_ENET_LED_CFG__VAL_ACT_TX 8

  /** LED shows receive activity (i.e., blinks if packets are received). */
#define BI_ENET_LED_CFG__VAL_ACT_RX 9

  /** LED is on if link speed is 10 Mbps. */
#define BI_ENET_LED_CFG__VAL_SPEED_10M 10

  /** LED is on if link speed is 100 Mbps. */
#define BI_ENET_LED_CFG__VAL_SPEED_100M 11

  /** LED is on if link speed is 1 Gbps. */
#define BI_ENET_LED_CFG__VAL_SPEED_1G 12

  /** LED is on if link is full-duplex. */
#define BI_ENET_LED_CFG__VAL_FDX 13

  /** Signal is not connected to an LED, but is used as the PHY interrupt
   *  line. */
#define BI_ENET_LED_CFG__VAL_INTR 30

  /** LED should get its default meaning.  This is obviously very PHY-
   *  dependent, and may be affected by the settings of other LEDs. */
#define BI_ENET_LED_CFG__VAL_DEFAULT 31

} __attribute__((packed, aligned(1)));


/** This item specifies the properties of a communication path connected to an
 *  mPIPE shim: this is generally either an Ethernet PHY, a set of SERDES
 *  lanes run directly to a connector (like CX4 for XAUI), or a set of SERDES
 *  lanes connected via circuit board traces to another chip.  SERDES lanes
 *  not described in at least one PHY_LINK_CFG item are assumed to not be
 *  connected, and will not be accessible by software.  Dual-mode PHYs (for
 *  instance, 4-SERDES-lane, 10 Gbps PHYs which also support a 1 Gbps mode
 *  using 1 SERDES lane) must be described by only one PHY_LINK_CFG item,
 *  which specifies all of the PHY's possible speeds. */
struct bi_phy_link_cfg
{
  /** If true, the PHY/link can support 10 Mbps operation (Ethernet). */
  uint8_t speed_10m: 1;

  /** If true, the PHY/link can support 100 Mbps operation (Fast Ethernet).
   */
  uint8_t speed_100m: 1;

  /** If true, the PHY/link can support 1 Gbps operation (Gigabit Ethernet).
   */
  uint8_t speed_1g: 1;

  /** If true, the PHY/link can support 10 Gbps operation (10 Gigabit
   *  Ethernet or 3-lane Interlaken). */
  uint8_t speed_10g: 1;

  /** If true, the PHY/link can support 20 Gbps operation (20 Gigabit
   *  Ethernet/Dual-speed XAUI). */
  uint8_t speed_20g: 1;

  /** If true, the PHY/link can support 25 Gbps operation (5-lane
   *  Interlaken). */
  uint8_t speed_25g: 1;

  /** If true, the PHY/link can support 50 Gbps operation (10-lane
   *  Interlaken). */
  uint8_t speed_50g: 1;

  /** Reserved: must be zero. */
  long : 3;

  /** If true, the connection between the PHY and the SFP/SFP+  socket
   *  reverses the transmit out signals (PHY + wired to SFP -). */
  uint8_t sfp_txout_inv: 1;

  /** If true, the PHY on this port will automatically configure itself. The
   *  precise effect of this flag depends upon the PHY used. For instance,
   *  on the AMCC QT2025, it means that we will assume that the PHY is
   *  configured with both a firmware EEPROM and a configuration EEPROM, and
   *  that none of our normal configuration register settings need be
   *  performed.  (Note that a firmware EEPROM is always required for PHYs
   *  which need firmware; it's just the configuration EEPROM which is
   *  optional.) */
  uint8_t phy_auto_cfg: 1;

  /** If true, there is no PHY connected to this link which can be managed
   *  by this processor via MDIO; the SERDES lanes are wired to an external
   *  connector or directly to another chip. */
  uint8_t no_phy: 1;

  /** If true, the PHY reset line for this link resets other PHYs as well.
   *  This is commonly the case with a multiport PHY. */
  uint8_t shared_reset: 1;

  /** If true, the PHY interrupt line for this link is shared with other
   *  links. This is reserved for future use; currently the hypervisor will
   *  treat such PHYs as non-interrupting. */
  uint8_t shared_intr: 1;

  /** Reserved: must be zero. */
  long : 1;

  /** Desired LED behavior. */
  struct bi_enet_led leds[6];

  /** If true, the PHY's MDIO connection is via the XGBE_MDIO/XGBE_MDC pins;
   *  otherwise, it is via the GBE_MDIO/GBE_MDC pins. */
  uint8_t mdio_bus_xgbe: 1;

  /** The 5-bit address of the PHY on the MDIO bus. */
  uint8_t mdio_addr: 5;

  /** Reserved: must be zero. */
  long : 2;

  /** Number of sequential MAC addresses assigned to this link.  This value
   *  may be zero; in that case, mac_addr is ignored. */
  uint8_t num_mac_addrs: 5;

  /** Reserved: must be zero. */
  long : 3;

  /** MAC address assigned to this link (or the first address of a
   *  contiguous sequence if num_mac_addrs is greater than one; such values
   *  are reserved for future use). */
  uint8_t mac_addr[6];

  /** Code identifying which SERDES lanes make up this link.  Note that some
   *  link modes may not use all available lanes. */
  uint8_t lanes: 5;

  /** Lane 0. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE0 0

  /** Lane 1. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE1 1

  /** Lane 2. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE2 2

  /** Lane 3. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE3 3

  /** Lane 4. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE4 4

  /** Lane 5. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE5 5

  /** Lane 6. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE6 6

  /** Lane 7. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE7 7

  /** Lane 8. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE8 8

  /** Lane 9. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE9 9

  /** Lane 10. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE10 10

  /** Lane 11. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE11 11

  /** Lane 12. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE12 12

  /** Lane 13. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE13 13

  /** Lane 14. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE14 14

  /** Lane 15. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE15 15

  /** Lanes 0 through 3. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE0_3 16

  /** Lanes 4 through 7. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE4_7 17

  /** Lanes 8 through 11. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE8_11 18

  /** Lanes 12 through 15. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE12_15 19

  /** Lanes 13 through 15. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE13_15 20

  /** Lanes 11 through 15. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE11_15 21

  /** Lanes 6 through 15. */
#define BI_PHY_LINK_CFG_LANES__VAL_LANE6_15 22

  /** Reserved: must be zero. */
  long : 3;

  /** Human-readable link number, to be used in error messages, software-
   *  visible names, and so forth.  This should match any physical labels
   *  used on the platform.  The numbers assigned to all 10/20 Gbps links,
   *  on all mPIPE shims, must be distinct; similarly, the numbers assigned
   *  to all 10 Mbps/100 Mbps/1 Gbps links must be distinct.  If this is not
   *  the case, software may use different link names, or may make some
   *  links inaccessible.  If set to DEFAULT, software will choose a number
   *  for this link. */
  uint8_t link_name_num: 6;

  /** Default value. */
#define BI_PHY_LINK_CFG_LINK_NAME_NUM__VAL_DEFAULT 0x3F

  /** Reserved: must be zero. */
  long : 18;

  /** Descriptor for a signal which, when asserted, resets the PHY.  Note
   *  that in MDE versions prior to MDE 4.1, the hypervisor did not use this
   *  signal to do PHY reset.  Board designs which need to support pre-4.1
   *  MDEs are advised to reset PHYs via signals described in a MISC_RESET
   *  item. */
  struct bi_signal reset_sig;

  /** Descriptor for a signal which, when asserted, means that the PHY has
   *  interrupted.  If this signal is of type NONE, this link's PHY cannot
   *  interrupt the processor.  Such PHYs will not be able to fully
   *  implement all software features, so this configuration is not
   *  advisable.  For instance, such a PHY may not react properly to link
   *  connect/disconnect during use. */
  struct bi_signal intr_sig;

} __attribute__((packed, aligned(4)));

/** Type code for bi_phy_link_cfg. */
#define BI_TYPE_PHY_LINK_CFG 55


/** Instance number for items which relate to shim ports. */
struct bi_port_inst
{
  /** Shim instance. */
  uint8_t shim: 3;

  /** Port within the specified shim. */
  uint8_t port: 4;

} __attribute__((packed, aligned(1)));


/** Identifying information for a PCIe port, supplied to the host via
 *  registers in PCIe configuration space.  Note that the Tilera PCIe drivers
 *  use the high 8 bits of the subsystem device ID for use in communication
 *  between the tile-side driver and the host-side driver, so any value set
 *  there will be overwritten. */
struct bi_pcie_id
{
  /** Revision ID. */
  uint8_t rev_id;

  /** Programming interface. */
  uint8_t prog_intf;

  /** Subclass code. */
  uint8_t subclass;

  /** Base class code. */
  uint8_t baseclass;

  /** Vendor ID. */
  uint16_t vendor;

  /** Device ID. */
  uint16_t device;

  /** Subsystem vendor ID. */
  uint16_t subsys_vendor;

  /** Subsystem device ID. */
  uint16_t subsys_device;

} __attribute__((packed, aligned(1)));


/** This item specifies the properties of a PCIe/StreamIO port connected to a
 *  TRIO shim: this is generally either a PCIe card socket, or a set of SERDES
 *  lanes connected via circuit board traces to another chip.  Ports not
 *  described in a PCIE_PORT_CFG item are assumed to not be connected, and
 *  will not be accessible by software. */
struct bi_pcie_port_cfg
{
  /** If true, the link can be configured in PCIe root complex mode. */
  uint8_t allow_rc: 1;

  /** If true, the link can be configured in PCIe endpoint mode. */
  uint8_t allow_ep: 1;

  /** If true, the link can be configured in StreamIO mode. */
  uint8_t allow_sio: 1;

  /** If true, the link is allowed to support 1-lane operation. Software
   *  will not consider it an error if the link comes up as a x1 link. */
  uint8_t allow_x1: 1;

  /** If true, the link is allowed to support 2-lane operation. Software
   *  will not consider it an error if the link comes up as a x2 link. */
  uint8_t allow_x2: 1;

  /** If true, the link is allowed to support 4-lane operation. Software
   *  will not consider it an error if the link comes up as a x4 link. */
  uint8_t allow_x4: 1;

  /** If true, the link is allowed to support 8-lane operation. Software
   *  will not consider it an error if the link comes up as a x8 link. */
  uint8_t allow_x8: 1;

  /** If true, software will override various PCIe identifying information
   *  with the values in u_id.  Customizing this information is only
   *  possible when booting from an interface other than PCIe. This is
   *  because when booting from PCIe, the interface will be identified
   *  before any code could run on the chip to change the identification
   *  information.  If this flag is false, the hardware defaults, which
   *  provide Tilera's PCI-SIG assigned values, are used. */
  uint8_t override_id: 1;

  /** If true, this link is connected to a device which may or may not be
   *  present; for instance, a slot which may or may not contain a PCIe
   *  card.  If false, this link is connected to a device which will always
   *  be present (e.g., a direct connection to another chip on the same
   *  board).  This information may be used by software to modify certain
   *  configuration behavior, like the error recovery strategy. */
  uint8_t removable: 1;

  /** Reserved: must be zero. */
  long : 23;

  /** Descriptor for a signal which, when asserted, resets the PCIe bus. */
  struct bi_signal perst_sig;

  /** PCIe identification information. */
  union
  {
    /** PCIe identification information. */
    struct bi_pcie_id id;

  } u;

} __attribute__((packed, aligned(4)));

/** Type code for bi_pcie_port_cfg. */
#define BI_TYPE_PCIE_PORT_CFG 56


/** This item specifies which GPIO pins are intended to be made available to
 *  application software, and the legal modes for those pins.  It is legal for
 *  pins to appear in more than one mask.  Pins specified elsehere in the BIB
 *  (in signal descriptors) need not, and generally should not, be included in
 *  these masks.  Pins not included in any pin mask are assumed to not be
 *  connected, and will not be accessible by application software. */
struct bi_gpio_pin_cfg
{
  /** Pins which may be configured as inputs. */
  uint64_t input;

  /** Pins which may be configured as outputs. */
  uint64_t output;

  /** Pins which may be configured as open-drain outputs. */
  uint64_t output_od;

} __attribute__((packed, aligned(4)));

/** Type code for bi_gpio_pin_cfg. */
#define BI_TYPE_GPIO_PIN_CFG 57


/** This item specifies the properties of a USB shim port. */
struct bi_usb_port_cfg
{
  /** If true, the port can be configured in device mode. */
  uint8_t allow_device: 1;

  /** If true, the port can be configured in host mode. */
  uint8_t allow_host: 1;

  /** Reserved: must be zero. */
  long : 30;

  /** Descriptor for a signal which, when asserted, resets the port's USB
   *  PHY. */
  struct bi_signal phy_reset_sig;

} __attribute__((packed, aligned(4)));

/** Type code for bi_usb_port_cfg. */
#define BI_TYPE_USB_PORT_CFG 58


/** This item specifies the properties of an SFP module directly connected to
 *  an mPIPE shim without an intervening PHY.  Note that most link properties
 *  still come from the PHY_LINK_CFG item; this item just adds extra
 *  properties which are only relevant in an SFP-only configuration.  As a
 *  special case, this item can be on used on both GbE and 10 GbE links even
 *  when a PHY is employed to specify a link status LED (although it's always
 *  a better idea to attach such an LED to the PHY directly).  In that case,
 *  only the link_led_sig member will be used; all others should be set to
 *  zero. */
struct bi_sfp_cfg
{
  /** Reserved: must be zero. */
  long : 16;

  /** I2C address information for the EEPROM on the SFP. */
  struct bi_i2c_addr i2c;

  /** Descriptor for a signal which, when asserted, means that the receive
   *  signal has been lost (connected to RX_LOS on the SFP). */
  struct bi_signal rx_los_sig;

  /** Descriptor for a signal which, when asserted, means that there has
   *  been a transmission fault (connected to TX_FAULT on the SFP). */
  struct bi_signal tx_fault_sig;

  /** Descriptor for a signal which, when asserted, disables transmission
   *  (connected to TX_DISABLE on the SFP). */
  struct bi_signal tx_disable_sig;

  /** Descriptor for a signal which, when asserted, means that the SFP
   *  module is absent (connected to MOD_ABS on the SFP). */
  struct bi_signal mod_abs_sig;

  /** Descriptor for a signal which will be asserted whenever the link is
   *  up. */
  struct bi_signal link_led_sig;

} __attribute__((packed, aligned(4)));

/** Type code for bi_sfp_cfg. */
#define BI_TYPE_SFP_CFG 59


/** This item specifies the properties of the UART console.  If it is not
 *  present, the UART console is on port 0, running at 115200 baud, no parity,
 *  8 data bits, 1 stop bit. */
struct bi_console_cfg
{
  /** Baud rate. */
  uint32_t baud_rate;

  /** UART port to use. */
  uint8_t port: 1;

  /** Parity. */
  uint8_t parity: 3;

  /** No parity bit. */
#define BI_CONSOLE_CFG_PARITY__VAL_NONE 0

  /** Mark parity (parity bit always on). */
#define BI_CONSOLE_CFG_PARITY__VAL_MARK 1

  /** Space parity (parity bit always off). */
#define BI_CONSOLE_CFG_PARITY__VAL_SPACE 2

  /** Even parity (even number of bits in data + parity). */
#define BI_CONSOLE_CFG_PARITY__VAL_EVEN 3

  /** Odd parity (odd number of bits in data + parity). */
#define BI_CONSOLE_CFG_PARITY__VAL_ODD 4

  /** Number of data bits to be used, not including any parity bits. */
  uint8_t data_bits: 1;

  /** 8 data bits. */
#define BI_CONSOLE_CFG_DATA_BITS__VAL_EIGHT 0

  /** 7 data bits. */
#define BI_CONSOLE_CFG_DATA_BITS__VAL_SEVEN 1

  /** Number of stop bits to be used. */
  uint8_t stop_bits: 1;

  /** 1 stop bit. */
#define BI_CONSOLE_CFG_STOP_BITS__VAL_ONE 0

  /** 2 stop bits. */
#define BI_CONSOLE_CFG_STOP_BITS__VAL_TWO 1

  /** Reserved: must be zero. */
  long : 2;

  /** Seconds to delay waiting for the rshim early console to be enabled,
   *  when booting from SROM.  Typically used on systems which do not have a
   *  serial connection, and thus must use the early console.  The delay
   *  gives the host a chance to enable the early console after chip reset.
   *  If the timeout expires before the early console is enabled, the UART
   *  described by this entry will be used instead. */
  uint8_t early_console_delay: 5;

  /** If the delay is set to this value, the booter will not proceed until
   *  the early console is enabled. */
#define BI_CONSOLE_CFG_EARLY_CONSOLE_DELAY__VAL_FOREVER 31

} __attribute__((packed, aligned(4)));

/** Type code for bi_console_cfg. */
#define BI_TYPE_CONSOLE_CFG 60


/** Description of an N-to-1 I2C switch used to allow access from multiple
 *  masters to a shared I2C bus, as well as signals which implement an
 *  arbitration scheme for the switch; together these are called an arbiter.
 *  This allows one or more I2C devices to be shared between processors in a
 *  multiprocessor system.  If this item is associated with an I2C bus, then
 *  all I2C devices on that bus are assumed to be on the other side of the
 *  arbiter. */
struct bi_i2c_arbiter
{
  /** Switch type. */
  uint8_t type;

  /** No arbiter. */
#define BI_I2C_ARBITER_TYPE__VAL_NONE 0

  /** NXP PCA9541 or equivalent. */
#define BI_I2C_ARBITER_TYPE__VAL_PCA9541 1

  /** I2C address of the switch.  In the binary representation, this is the
   *  7-bit address (the low read/write bit is omitted). */
  uint8_t dev_addr: 7;

  /** Reserved: must be zero. */
  long : 1;

  /** Switch port number which is connected to the given master shim. */
  uint8_t switch_chan: 2;

  /** Reserved: must be zero. */
  long : 14;

  /** Reset signal for the switch. */
  struct bi_signal reset;

  /** Request signal for the arbiter.  This signal is asserted by a
   *  processor whenever it needs access to a shared device, and is
   *  deasserted once access is no longer required.  The requesting
   *  processor must not reconfigure the switch or access any shared devices
   *  until the grant signal is asserted. */
  struct bi_signal req;

  /** Grant signal for the arbiter.  This signal is asserted by the arbiter
   *  when it is safe for the processor requesting access to configure the
   *  switch and access shared devices. */
  struct bi_signal grant;

} __attribute__((packed, aligned(4)));

/** Type code for bi_i2c_arbiter. */
#define BI_TYPE_I2C_ARBITER 61


/** This item provides for mPIPE and TRIO shim instance numbers to be
 *  translated to different virtual instance numbers, which are then used when
 *  looking up certain BIB items; specifically, if this item exists for a
 *  particular shim, then the virtual instance number within it is used when
 *  looking up mPIPE PHY_LINK_CFG and SFP_CFG items, and the TRIO
 *  PCIE_PORT_CFG item.  The purpose of this translation is to allow plugin
 *  modules which contain connections to multiple ports to be used with
 *  different processor configurations.  For instance, a pluggable I/O module
 *  might have two PCIe connections, described by PCIE_PORT_CFG items in an
 *  onboard AIB. This module might be capable of connecting to a CPU module
 *  with one Gx processor, or with two Gx processors.  When used with multiple
 *  Gx processors, both will have access to the I/O module's AIB, but the
 *  second processor needs to know that its shim 0 is supposed to connect to
 *  the I/O module connection designated as shim 1 in the AIB.  Adding an
 *  appropriate SHIM_VIRT_INST item in the second processor's BIB will
 *  accomplish this. */
struct bi_shim_virt_inst
{
  /** Virtual instance number to be used with the BIB items described above.
   */
  uint8_t virt_inst;

} __attribute__((packed, aligned(4)));

/** Type code for bi_shim_virt_inst. */
#define BI_TYPE_SHIM_VIRT_INST 62


/** This item specifies additional CPU voltage characteristics.  This item
 *  type is experimental; it may change or even disappear in the future.
 *  Customers are advised against including it in their BIBs without specific
 *  instructions from Tilera technical support. */
struct bi_cpu_volt_char
{
  /** If true, do additional load line processing of the chip operating
   *  voltage. What this means is that before translating an operating
   *  voltage to a VID code to be passed to the power supply, it will first
   *  be translated to a VID voltage; the VID voltage is then translated to
   *  a VID code. This may be helpful when using power supplies which do not
   *  always supply the voltage requested by the VID pins, due to load line
   *  correction.  The translation is done using the load_line_factor and
   *  load_line_offset members of this item.  load_line_factor is a fraction
   *  of the operating voltage to add to that voltage, in millionths, and
   *  load_line_offset is an offset to add to that voltage, in VID steps
   *  (6.25 mV).  If load_line is True, at least one of load_line_factor and
   *  load_line_offset must be nonzero. The precise calculation can be
   *  expressed as:  vid_voltage = (1 + (load_line_factor / 1000000)) *
   *  operating_voltage +               load_line_offset * .00625  Note that
   *  the values in the CPU_VOLT_RANGE item are also processed through the
   *  inverse of this formula.  In other words, CPU_VOLT_RANGE specifies the
   *  range of VID voltages, not achievable operating voltages. */
  uint8_t load_line: 1;

  /** Load line factor.  See the load_line description for how this value is
   *  used. */
  int32_t load_line_factor: 21;

  /** Load line offset.  See the load_line description for how this value is
   *  used. */
  int16_t load_line_offset: 9;

  /** Reserved: must be zero. */
  long : 32;

  /** This member is reserved for future use and must be zero. */
  uint8_t reserved: 1;

} __attribute__((packed, aligned(4)));

/** Type code for bi_cpu_volt_char. */
#define BI_TYPE_CPU_VOLT_CHAR 63


/** Entry in an MSH_REG item. */
struct bi_msh_reg_entry
{
  /** Minimum memory speed matched by this entry, in MT/s. */
  uint16_t min_speed: 12;

  /** Minimum number of DIMMs matched by this entry.  The value encoded in
   *  the BIB is one less than the number of DIMMs; the presentation used in
   *  the BTK is the true value. */
  uint8_t min_dimm: 2;

  /** Minimum number of ranks per DIMM matched by this entry.  The value
   *  encoded in the BIB is one less than the number of ranks; the
   *  presentation used in the BTK is the true value. */
  uint8_t min_rank: 2;

  /** Minimum core voltage matched by this entry.  The value encoded in the
   *  BIB is a VID code; the presentation used in the BTK is uV. */
  uint8_t min_voltage: 6;

  /** Reserved: must be zero. */
  long : 10;

  /** Which parameter to modify. */
  uint8_t parameter;

  /** Read latency setting in DDR3_PHY_DELAY. */
#define BI_MSH_REG_ENTRY_PARAMETER__VAL_RDLAT 1

  /** Address mirroring setting in DDR3_DIMM_CFG.  Note that overriding this
   *  value is generally only useful in unusual soldered-down memory
   *  configurations. */
#define BI_MSH_REG_ENTRY_PARAMETER__VAL_ADDR_MIRROR 2

  /** Nominal on-die termination value, in ohms.  The eventual DRAM MR1
   *  register settings are calculated assuming that Rzq is 240 ohms, so the
   *  only legal values for this setting are 0, 20, 30, 40, 60, and 120.
   *  Note that default values are calculated based on DIMM characteristics;
   *  thus, overriding this value is generally only useful in soldered-down
   *  memory configurations. */
#define BI_MSH_REG_ENTRY_PARAMETER__VAL_ODT_RTT_NOM 3

  /** Write on-die termination value, in ohms.  The eventual DRAM MR2
   *  register settings are calculated assuming that Rzq is 240 ohms, so the
   *  only legal values for this setting are 0, 60, and 120.  Note that
   *  default values are calculated based on DIMM characteristics; thus,
   *  overriding this value is generally only useful in soldered-down memory
   *  configurations. */
#define BI_MSH_REG_ENTRY_PARAMETER__VAL_ODT_RTT_WR 4

  /** Controller termination value, in ohms. */
#define BI_MSH_REG_ENTRY_PARAMETER__VAL_CTRL_TERM 5

  /** Reserved: must be zero. */
  long : 8;

  /** Value for the specific parameter. */
  uint16_t value;

} __attribute__((packed, aligned(1)));


/** This item allows one or more memory shim settings to be overridden, based
 *  on specific configuration conditions (number of DIMMs per shim, number of
 *  ranks per DIMM, memory frequency, processor core voltage). For each
 *  overridable parameter, the memory configuration first computes a value
 *  that it will use in the absence of any override information; it then walks
 *  the list of overrides for that shim, looking for entries which refer to
 *  that parameter.  The last such entry which matches all of the conditions
 *  for that shim will be used to provide the overriden parameter value.  If
 *  no such entry exists, the parameter is not overridden. */
struct bi_msh_reg
{
  /** MSH_REG entries. */
  struct bi_msh_reg_entry entries[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_msh_reg. */
#define BI_TYPE_MSH_REG 64


/** Entry in a SERDES_LANE_CHAR item. */
struct bi_serdes_lane_char_entry
{
  /** Length of this SERDES lane in the receive direction (i.e., data
   *  flowing towards the processor).  This length is somewhat notional.
   *  Ideally, it's the length of the trace in millimeters.  However, in
   *  some cases it might be larger or smaller than that, to simulate the
   *  effect of channels which are more lossy (e.g., incorporating more
   *  connectors) or less lossy (e.g., containing redrivers). */
  int16_t rx_len: 12;

  /** Length of this SERDES lane in the transmit direction (i.e., data
   *  flowing away from the processor).  This length is somewhat notional.
   *  Ideally, it's the length of the trace in millimeters.  However, in
   *  some cases it might be larger or smaller than that, to simulate the
   *  effect of channels which are more lossy (e.g., incorporating more
   *  connectors) or less lossy (e.g., containing redrivers). */
  int16_t tx_len: 12;

  /** This member is reserved for future use and must be zero. */
  uint8_t reserved;

} __attribute__((packed, aligned(1)));


/** This item specifies additional SERDES lane characteristics.  This item
 *  type is experimental; it may change or even disappear in the future.
 *  Customers are advised against including it in their BIBs without first
 *  consulting with Tilera technical support. */
struct bi_serdes_lane_char
{
  /** SERDES_LANE_CHAR entries. */
  struct bi_serdes_lane_char_entry entries[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_serdes_lane_char. */
#define BI_TYPE_SERDES_LANE_CHAR 65


/** This entry defines the speed of the XAUI reference clock for an mPIPE
 *  shim. The typical reference clock speed is 156.25 Mhz, which allows
 *  operation of the XAUI at either 10 Gbps or 20 Gbps; when that speed is
 *  used, this entry is not required.  The only other supported reference
 *  clock speed is 125 MHz, which allows operation of the XAUI at 12 Gbps. */
struct bi_xaui_refclk
{
  /** XAUI reference clock speed, in Hertz. */
  uint32_t speed: 28;

} __attribute__((packed, aligned(4)));

/** Type code for bi_xaui_refclk. */
#define BI_TYPE_XAUI_REFCLK 66


/** This item specifies a name for one or more GPIO pins.  The name is
 *  intended to allow application or operating system software to use GPIO
 *  functions without having to be customized for each board's specific pin
 *  wiring. Pins specified in this item should also be included in a
 *  GPIO_PIN_CFG item since otherwise they will be inaccessible. */
struct bi_gpio_name
{
  /** Pins which may be used as inputs. */
  uint64_t input;

  /** Pins which may be used as outputs. */
  uint64_t output;

  /** Pins which may be used as open-drain outputs. */
  uint64_t output_od;

  /** Pins which should be inverted. */
  uint64_t invert;

  /** Name of the pin or pins.  Names longer than 63 characters will be
   *  silently truncated. */
  char name[0];

} __attribute__((packed, aligned(4)));

/** Type code for bi_gpio_name. */
#define BI_TYPE_GPIO_NAME 67


/** This item modifies the configuration of an I2C master controller.  Changes
 *  made due to the presence of this item occur late in the hypervisor boot
 *  process, upon initialization of the i2cm device driver, and thus do not
 *  apply to I2C accesses made early in the boot process, particularly those
 *  used to find and read the BIB itself. */
struct bi_i2cm_ctl_cfg
{
  /** Frequency of the I2C bus clock, in kilohertz. */
  uint16_t freq_khz: 9;

  /** Reserved: must be zero. */
  long : 3;

  /** Glitch mask, as specified by the I2CM_GLITCH_MASK register in the
   *  shim. */
  uint8_t glitch: 6;

  /** Electrical control settings, as specified by the
   *  I2CM_ELECTRICAL_CONTROL register in the shim.  Note that the value
   *  specified here as the default, 0x36, is not identical to the hardware
   *  default value (the drive strength is increased), and is what is used
   *  by the booter and hypervisor before this item is processed. */
  uint16_t elec: 10;

} __attribute__((packed, aligned(4)));

/** Type code for bi_i2cm_ctl_cfg. */
#define BI_TYPE_I2CM_CTL_CFG 68



/** @} */

/**
 * @name Special instance number values
 * @{
 */

/** Instance value for per-AIB items, which will be translated to a new value
 *  when the items are imported into the main BIB. */
#define BI_INST_AIB             0xFF

/** @} */

#endif /* _SYS_COMMON_HVBME_BOARD_INFO_H */
