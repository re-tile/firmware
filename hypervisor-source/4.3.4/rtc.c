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
 * Routines to read and write the RTC chip.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <util.h>

#include <arch/cycle.h>
#include <sys/time.h>

#include <hv/hypervisor.h>

#include "board_info.h"
#include "debug.h"
#include "devices.h"
#include "i2c_acc.h"
#include "hv.h"

#include "rtc.h"

rtc_read_time_func* rtc_read_time;   /**< Pointer to rtc read function */
rtc_write_time_func* rtc_write_time; /**< Pointer to rtc write function */

/** Helper function for converting two-digit binary-coded decimal to decimal */
static inline uint8_t
bcd2dec(uint8_t val)
{
  return ((val & 0xf) + ((val >> 4) * 10));
}

/** Helper function for converting decimal to two-digit binary-coded decimal */
static inline uint8_t
dec2bcd(uint8_t val)
{
  return (((val / 10) << 4) + (val % 10));
}


/** Default function for reading the date/time from the RTC.
 */
static int
default_read_time(HV_RTCTime* tm)
{
  tm->tm_sec = 0;
  tm->tm_min = 0;
  tm->tm_hour = 0;
  tm->tm_mday = 1;
  tm->tm_mon = 0;
  tm->tm_year = 1970 - 1900;
  tm->flags = HV_RTC_NO_CHIP;
  
  return 0;
}


/** Default function for writing the date/time to the RTC.
 */
static int
default_write_time(HV_RTCTime tm)
{
  return -1;
}


//
// PCF8563 static data.
//
/** I2C bus that the RTC is connected to. */
static int pcf8563_bus;
static int pcf8563_switch_inst;
static int pcf8563_switch_chan;
/** I2C address of the RTC. */
static uint32_t pcf8563_i2c_addr;


/** Swing the I2C switch to our chip, if needed. */
static void
switch_swing()
{
  i2c_switch_swing(pcf8563_bus, pcf8563_switch_inst,
                   pcf8563_switch_chan);
}


/** Release the I2C switch, if needed. */
static void
switch_release()
{
  i2c_switch_release(pcf8563_bus, pcf8563_switch_inst);
}


/** Setup function for PCF8563 RTC.
 */
static void
pcf8563_setup(bi_ptr_t bi)
{
  struct bi_rtc_cfg* bir = bi;
  pcf8563_bus = bir->u.pcf8563.addr.bus;
  pcf8563_i2c_addr = bir->u.pcf8563.addr.dev_addr << 1;
  pcf8563_switch_inst = bir->u.pcf8563.addr.switch_inst;
  pcf8563_switch_chan = bir->u.pcf8563.addr.switch_chan;

  if (i2cm_info[pcf8563_bus])
  {
    uint8_t byte_val = 0;

    switch_swing();
    
    // Make sure that the clock is running.
    //
    // Check for voltage-low condition (indicating that the battery died) and
    // print a warning if the condition is present.  The condition will be
    // cleared the next time the time is written.
    if ((i2c_wr(i2cm_info[pcf8563_bus]->idn_ports[0],
                i2cm_info[pcf8563_bus]->channel, pcf8563_i2c_addr,
                I2C_PCF8563_STAT1, 1, &byte_val) != 1) ||
        (i2c_rd(i2cm_info[pcf8563_bus]->idn_ports[0],
                i2cm_info[pcf8563_bus]->channel, pcf8563_i2c_addr,
                I2C_PCF8563_VL_SEC, 1, &byte_val) != 1))
    {
      printf("hv_warning: can't access RTC chip\n");
      switch_release();
      return;
    }

    switch_release();

    if (byte_val & I2C_PCF8563_VL_MASK)
      printf("hv_warning: rtc not programmed or battery low, "
             "date/time not reliable\n");

    return;
  }
}

/** PCF8563-specific function for reading the date/time from the RTC.
 */
static int
pcf8563_read_time(HV_RTCTime* tm)
{
  uint8_t byte_val;
  pos_t pos;
  unsigned long chan;

  if (!i2cm_info[pcf8563_bus])
    return -1;

  switch_swing();
    
  pos.word = i2cm_info[pcf8563_bus]->idn_ports[0].word;
  chan = i2cm_info[pcf8563_bus]->channel;

  // read seconds, and low voltage condition
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_VL_SEC, 1,
             &byte_val) != 1)
  {
    switch_release();
    return -1;
  }
  tm->flags = (byte_val & I2C_PCF8563_VL_MASK) ? HV_RTC_LOW_VOLTAGE : 0;
  tm->tm_sec = bcd2dec(byte_val & I2C_PCF8563_SEC_MASK);

  // read minutes
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_MIN, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }
  tm->tm_min = bcd2dec(byte_val & I2C_PCF8563_MIN_MASK);

  // read hours
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_HOUR, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }
  tm->tm_hour = bcd2dec(byte_val & I2C_PCF8563_HOUR_MASK);

  // read day
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_DAY, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }
  tm->tm_mday = bcd2dec(byte_val & I2C_PCF8563_DAY_MASK);

  // read month
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_CENT_MON, 1,
             &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  int century_is_19xx = byte_val & I2C_PCF8563_CENT_MASK;
  tm->tm_mon = bcd2dec(byte_val & I2C_PCF8563_MON_MASK) - 1;

  // read year
  if (i2c_rd(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_YEAR, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  switch_release();

  tm->tm_year = bcd2dec(byte_val & I2C_PCF8563_YEAR_MASK) +
    (century_is_19xx ? 0 : 100);

  return 0;
}


/** PCF8563-specific function for writing the date/time to the RTC.
 */
static int
pcf8563_write_time(HV_RTCTime tm)
{
  uint8_t byte_val;
  pos_t pos;
  unsigned long chan;

  if (!i2cm_info[pcf8563_bus])
    return -1;

  switch_swing();

  pos.word = i2cm_info[pcf8563_bus]->idn_ports[0].word;
  chan = i2cm_info[pcf8563_bus]->channel;

  // write seconds
  byte_val = dec2bcd(tm.tm_sec);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_VL_SEC, 1,
             &byte_val) != 1)
  {
    switch_release();
    return -1;
  }
  
  // write minutes
  byte_val = dec2bcd(tm.tm_min);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_MIN, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  // write hours
  byte_val = dec2bcd(tm.tm_hour);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_HOUR, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  // write day
  byte_val = dec2bcd(tm.tm_mday);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_DAY, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  // write month
  int century_is_19xx = tm.tm_year < 100;
  byte_val = dec2bcd(tm.tm_mon + 1) | 
    (century_is_19xx ? I2C_PCF8563_CENT_MASK : 0);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_CENT_MON, 1,
             &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  // write year
  byte_val = dec2bcd(tm.tm_year % 100);
  if (i2c_wr(pos, chan, pcf8563_i2c_addr, I2C_PCF8563_YEAR, 1, &byte_val) != 1)
  {
    switch_release();
    return -1;
  }

  switch_release();

  return 0;
}


/** Determine from the BIB the type, if any, of RTC chip, and initialize
 * the function pointers for reading and writing the time.
 */
void
init_rtc()
{
    bi_ptr_t rtc_type_p;
    int err = bi_getparam(BI_TYPE_RTC_CFG, 0, &rtc_type_p, NULL);
    if (err == BI_NULL)
    {
      rtc_read_time = default_read_time;
      rtc_write_time = default_write_time;
      return;
    }

    struct bi_rtc_cfg* bir = rtc_type_p;
    uint32_t type = bir->type;

    switch (type)
    {
    case BI_RTC_CFG_TYPE__VAL_PCF8563:
      rtc_read_time = pcf8563_read_time;
      rtc_write_time = pcf8563_write_time;
      pcf8563_setup(rtc_type_p);
      break;

    default:
      rtc_read_time = default_read_time;
      rtc_write_time = default_write_time;
      break;
    }
}
