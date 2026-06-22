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
 * Plugin for the MAX6639 temperature sensor/fan controller.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

#include <hv/drv_sensor_intf.h>
#include <hv/hypervisor.h>

#include "board_info.h"
#include "debug.h"
#include "devices.h"
#include "drivers/sensor/sensor.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "lock.h"

//
// Note that these conversion values are for the MAX6639F, which is what's
// actually used on TILEmpower-Pro V2 and TILEmpower-Gx.  They would be
// slightly different for the MAX6639, although everything else about the
// chips is the same.  If we needed to support the original 6639, we'd want
// to just read the chip's ID register and adjust the conversions based on
// that, rather than have a separate driver.
//


//
// Early tests suggested that the values from this particular model of
// temperature sensor needed to be adjusted, but further work demonstrated
// that such was not the case; it now appears that the unadjusted values 
// are accurate.
//

/** Convert a value read from the sensor to the real temperature. */
#define MAX6639_REPORTED_TO_REAL(reported) (reported)

/** Convert a real temperature to the value read from the sensor. */
#define MAX6639_REAL_TO_REPORTED(real) (real)


#define MAX6639_TEMP_LIMIT       255 /**< MAX6639 max temp */

// On the TILEmpower-Pro V2, OT is connected to the board's FPGA, which can
// be configured to power off the board when OT is asserted.  We set the
// MAX6639 to assert OT at a very high temperature.

/** MAX6639 critical cpu temperature */
#define MAX6639_TEMP_CRIT_CPU    125

/** MAX6639 manual RPM fan control mode. Fan speed is forced to the value
 * written to the target tach count register. */
#define MAX6639_RPM_MODE         0

/** MAX6639 manual PWM duty-cycle fan control mode. */
#define MAX6639_PWM_MODE         1

/** MAX6639 automatic RPM fan control mode.  In this mode, the chip
 * attempts to keep the measured temperature constant by running
 * the fan faster when the temperature rises, and slower when it falls.
 */
#define MAX6639_AUTO_MODE        2

/** Lock used to make sure that only one tile allocates shared data. */
static spinlock_t max6639_alloc_lock _SHARED = SPINLOCK_INIT;


/** Fan RPM range table */
static const int rpm_ranges[] = {2000, 4000, 8000, 16000};

/** Swing the I2C switch to our chip, if needed.
 * @param sensor The sensor chip instance.
 */
static void
switch_swing(sens_inst_t* sensor)
{
  i2c_switch_swing(sensor->bus, sensor->switch_inst,
                   sensor->switch_chan);
}


/** Release the I2C switch, if needed.
 * @param sensor The sensor chip instance.
 */
static void
switch_release(sens_inst_t* sensor)
{
  i2c_switch_release(sensor->bus, sensor->switch_inst);
}


/** Initialize the temperature sensor plugin.
 * @param sensorpp The buffer of sensor instance pointer.
 * @param desc BIB entry descriptor.
 * @param resptr Pointer to body of BIB entry.
 * @return 0 on success, or negative error code if there is an error.
 */
int
max6639_init_temp_sens(void** sensorpp, uint32_t desc, bi_ptr_t resptr)
{
  sens_inst_t* inst = *sensorpp;
  spin_lock(&max6639_alloc_lock);
  if (inst == NULL)
  {
    inst = drv_shared_state_zalloc(sizeof (sens_inst_t), 0);
    if (inst == NULL)
    {
      spin_unlock(&max6639_alloc_lock);
      return HV_EFAULT;
    }
    *sensorpp = inst;
  }

  struct bi_temp_cfg* bi = resptr;
  inst->i2c_addr = bi->addr.dev_addr << 1;
  inst->bus = bi->addr.bus;
  inst->switch_inst = bi->addr.switch_inst;
  inst->switch_chan = bi->addr.switch_chan;

  inst->fan_descs[0] = bi->u.max6639.fans[0];
  inst->fan_descs[1] = bi->u.max6639.fans[1];

  inst->bib_instance = BI_INST(desc);

  for (int i = 0; i < 2; i++)
    if (BI_WDS(desc) > 2 + i)
        inst->extra_sigs[i] = bi->u.max6639.sigs[i];
    else
        inst->extra_sigs[i] =
          (struct bi_signal) { .type = BI_SIGNAL_TYPE__VAL_NONE };

  spin_unlock(&max6639_alloc_lock);
  return HV_OK;
}


/** Convert speed value to rpm range value.
 * @param speed speed value in RPM.
 * @return rpm range index from 0 to 3.
 */
static uint8_t
rpm_range_select(int speed)
{
  for (int i = 0; i < sizeof (rpm_ranges) / sizeof (rpm_ranges[0]); i++)
    if (speed <= rpm_ranges[i])
      return i;
  return 3;
}


/** Get the max6639 fan control mode.
 * @param sensor The sensor chip instance.
 * @param chan fan control channel.
 * @return 0 manual RPM mode, 1 manual PWM mode, 2 auto PWM mode,
 *         or -1 if there is an error.
 */
static int
max6639_get_mode(sens_inst_t* sensor, int chan)
{
  uint8_t fan_conf_1;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_FAN_CONF1: I2C_MAX6639_CH2_FAN_CONF1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  int mode;
  if (fan_conf_1 & 0x80)
    mode = MAX6639_PWM_MODE;
  else if (fan_conf_1 & 0x0C)
    mode = MAX6639_AUTO_MODE;
  else
    mode = MAX6639_RPM_MODE;

  return mode;
}


/** Set the max6639 fan control mode, the caller should have swung the
 * I2C switch to our chip already.
 * @param sensor The sensor chip instance.
 * @param chan Fan control channel.
 * @param mode Fan control mode.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
_max6639_set_mode(sens_inst_t* sensor, int chan, int mode)
{
  uint8_t fan_conf_1;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_FAN_CONF1: I2C_MAX6639_CH2_FAN_CONF1;

  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
    return -1;

  if (mode == MAX6639_RPM_MODE)
    fan_conf_1 &= ~((1 << 7) | (1 << 3) | (1 << 2));
  else if (mode == MAX6639_PWM_MODE)
    fan_conf_1 |= (1 << 7);
  if (mode == MAX6639_AUTO_MODE)
  {
    if ((fan_conf_1 & 0x0C) == 0)
      return -1;

    fan_conf_1 &= ~(1 << 7);
  }

  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
    return -1;

  return 0;
}


/** Set the max6639 fan control mode.
 * @param sensor The sensor chip instance.
 * @param chan Fan control channel.
 * @param mode Fan control mode.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
max6639_set_mode(sens_inst_t* sensor, int chan, int mode)
{
  if (mode < 0 || mode > 2)
    return HV_EINVAL;

  switch_swing(sensor);
  int rv = _max6639_set_mode(sensor, chan, mode);
  switch_release(sensor);
  return rv;
}


/** Get the temperature sensor label string.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return the label string.
 */
static char*
max6639_get_temp_label(sens_inst_t* sensor, int chan)
{
  char* label;

  // TODO: check diode connected or not as per temp_valid, and read the label
  // string from BIB, it requires add such label info to BIB.

  if (chan < 0 || chan > 1)
    label = "not exist";
  else if (chan == 0 && sensor->bib_instance == 0)
    label = "cpu temp";
  else
    label = "board temp";

  return label;
}


/** Configure the critical temperature thresholds in the MAX6639.  The
 *  sensor is hard-wired to the board's power, and will shut the board down
 *  if these temperatures are exceeded.  Also, crank the fans up to
 *  maximum, since by default they're at 50%.  We expect the client OS to
 *  request more intelligent fan control mode later.
 * @param sensor The sensor chip instance.
 */
void
max6639_config_temp_sens(void* sensor)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  switch_swing(inst);

  //
  // First do a soft reset to ensure the chip has a known configuration.
  //
  uint8_t gcr = 1 << 6;

  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_MAX6639_GLOBAL_CONFIG, 1, &gcr) != 1)
  {
    printf("hv_warning: can't configure max6639 temperature sensor at "
           "%d/%02x (1)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }

  //
  // Now configure local/remote sensing channel 2, and the fan PWM
  // output frequency range.
  //

  //
  // We always disable the SMBus timeout.
  //
  gcr = 1 << 5;

  //
  // If there's a diode connected to channel 2, use it, otherwise use
  // the chip's internal sensor.
  //
  if (!inst->fan_descs[1].temp_valid)
    gcr |= 1 << 4;

  //
  // If either fan is 4-wire, we enable the high PWM frequency range.
  // This means that you'll probably get poor results if you have one of
  // each type of fan.
  //
  if (inst->fan_descs[0].four_wire | inst->fan_descs[1].four_wire)
    gcr |= 1 << 3;

  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_MAX6639_GLOBAL_CONFIG, 1, &gcr) != 1)
  {
    printf("hv_warning: can't configure max6639 temperature sensor at "
           "%d/%02x (2)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }

  //
  // Now set the OT limit for channel 0.
  //
  uint8_t ot_limit = MAX6639_REAL_TO_REPORTED(MAX6639_TEMP_CRIT_CPU);

  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_MAX6639_CH1_OT_LIM, 1, &ot_limit) != 1)
  {
    printf("hv_warning: can't configure max6639 temperature sensor at "
           "%d/%02x (3)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }

  //
  // Assert any extra signals defined in our BIB entry.
  //
  for (int i = 0; i < 2; i++)
    if (inst->extra_sigs[i].type != BI_SIGNAL_TYPE__VAL_NONE)
      drv_set_signal(inst->extra_sigs[i], DRV_SIGNAL_INIT | DRV_SIGNAL_ASSERT);

  //
  // Finally, set any fans to manual PWM mode, 100% duty cycle.  Each PWM
  // cycle has 120 time slots, so 100% duty cycle is a value of 120.
  //
  uint8_t fan_conf_1 = 1 << 7;
  uint8_t fan_duty = 120;

  if (inst->fan_descs[0].max_speed)
  {
    uint8_t fan_conf_2a = (inst->fan_descs[0].pwm_act_low) ? 0 : 1 << 1;
    uint8_t rpm_range = rpm_range_select(inst->fan_descs[0].max_speed * 100);

    fan_conf_1 |= rpm_range;
    if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH1_FAN_CONF1, 1, &fan_conf_1) != 1 ||
        i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH1_FAN_CONF2A, 1, &fan_conf_2a) != 1 ||
        i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH1_DUTY_CYC, 1, &fan_duty) != 1)
    {
      printf("hv_warning: can't configure %s sensor (channel %d)\n",
             max6639_get_temp_label(inst, 0), 0);
      switch_release(inst);
      return;
    }
  }

  if (inst->fan_descs[1].max_speed)
  {
    uint8_t fan_conf_2a = (inst->fan_descs[1].pwm_act_low) ? 0 : 1 << 1;
    uint8_t rpm_range = rpm_range_select(inst->fan_descs[1].max_speed * 100);

    fan_conf_1 &= ~0x3;
    fan_conf_1 |= rpm_range;
    if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH2_FAN_CONF1, 1, &fan_conf_1) != 1 ||
        i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH2_FAN_CONF2A, 1, &fan_conf_2a) != 1 ||
        i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
               i2cm_info[inst->bus]->channel, inst->i2c_addr,
               I2C_MAX6639_CH2_DUTY_CYC, 1, &fan_duty) != 1)
    {
      printf("hv_warning: can't configure %s sensor (channel %d)\n",
             max6639_get_temp_label(inst, 1), 1);
      switch_release(inst);
      return;
    }
  }
  switch_release(inst);
}


/** Get the temperature sensor input.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature value in degrees Kelvin, or -1 if there is an error.
 */
static int
max6639_get_temp(sens_inst_t* sensor, int chan)
{
  uint8_t val;
  int reg;

  switch_swing(sensor);

  // Read the extended temperature register and check the diode fault bit.
  reg = (chan == 0) ? I2C_MAX6639_CH1_EXT_TEMP : I2C_MAX6639_CH2_EXT_TEMP;
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  // If the diode is bad, we don't have a valid temperature.
  if (val & 1)
  {
    printf("hv_warning: %s (channel %d) diode sensor fault\n",
           max6639_get_temp_label(sensor, chan), chan);
    switch_release(sensor);
    return -1;
  }

  reg = (chan == 0) ? I2C_MAX6639_CH1_TEMP : I2C_MAX6639_CH2_TEMP;
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return val + HV_SYSCONF_TEMP_KTOC;
}


/** Get the OT temperature setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature value in degress Kelvin, or -1 if there is an error.
 */
static int
max6639_get_ot_limit(sens_inst_t* sensor, int chan)
{
  uint8_t crit = 0;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_OT_LIM : I2C_MAX6639_CH2_OT_LIM;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &crit) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return crit + HV_SYSCONF_TEMP_KTOC;
}


/** Set the OT temperature value.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param ot_limit OT temperature in degrees Kelvin.
 * @return 0 on succes, or negative error code if there is an error.
 */
static int
max6639_set_ot_limit(sens_inst_t* sensor, int chan, int ot_limit)
{
  int reg = (chan == 0) ? I2C_MAX6639_CH1_OT_LIM : I2C_MAX6639_CH2_OT_LIM;

  // Celsius degree in register.
  ot_limit = ot_limit - HV_SYSCONF_TEMP_KTOC;
  if (ot_limit < 0 || ot_limit > MAX6639_TEMP_CRIT_CPU)
    return HV_EINVAL;

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &ot_limit) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return 0;
}


/** Get the fan label string.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return the label string.
 */
static char*
max6639_get_fan_label(sens_inst_t* sensor, int chan)
{
  char* label;

  if (chan < 0 || chan > 1)
    label = "not exist";
  else if (sensor->fan_descs[chan].max_speed == 0)
    label = "not connected";
  else if (chan == 0 && sensor->bib_instance == 0)
    label = "cpu fan";
  else
    label = "board fan";

  return label;
}


/** Get the fan PWM setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return 0 to 255 PWM integer on success, or -1 if there is an error.
 */
static int
max6639_get_pwm(sens_inst_t* sensor, int chan)
{
  uint8_t val;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_DUTY_CYC : I2C_MAX6639_CH2_DUTY_CYC;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  // PWM in range 0 to 255, MAX6639 divided PWM cycle into 120 time slots.
  val = val * 255 / 120;
  return val;
}


/** Set the fan PWM.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param pwm PWM integer value.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
max6639_set_pwm(sens_inst_t* sensor, int chan, int pwm)
{
  int reg = (chan == 0) ? I2C_MAX6639_CH1_DUTY_CYC : I2C_MAX6639_CH2_DUTY_CYC;

  // PWM in range 0 to 255, MAX6639 divided PWM cycle into 120 time slots.
  if (pwm < 0 || pwm > 255)
    return HV_EINVAL;

  pwm = pwm * 120 / 255;

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &pwm) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return 0;
}


/** Get the fan pulses per revolution setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return 1 to 4 ppr value on success, or -1 if there is an error.
 */
static int
max6639_get_fan_ppr(sens_inst_t* sensor, int chan)
{
  uint8_t val;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_PPR_MT : I2C_MAX6639_CH2_PPR_MT;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return (val >> 6) + 1;
}


/** Set the fan pulses per revolution.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param ppr PPR value.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
max6639_set_fan_ppr(sens_inst_t* sensor, int chan, int ppr)
{
  uint8_t val;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_PPR_MT : I2C_MAX6639_CH2_PPR_MT;

  if (ppr <= 0 || ppr > 4)
    return HV_EINVAL;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  val &= 0x3F;
  val |= (ppr - 1) << 6;
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return 0;
}


/** Get the RPM range setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return RPM range value on success, or -1 if there is an error.
 */
static int
get_rpm_range(sens_inst_t* sensor, int chan)
{
  uint8_t fan_conf_1;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_FAN_CONF1 : I2C_MAX6639_CH2_FAN_CONF1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
               i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
               reg, 1, &fan_conf_1) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  uint8_t rpm_range = fan_conf_1 & 0x03;
  return rpm_ranges[rpm_range];
}


/** Get the current fan revolution speed.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return RPM value, or -1 if there is an error.
 */
static int
max6639_get_rpm(sens_inst_t* sensor, int chan)
{
  int rpm_range;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_TACH_CNT : I2C_MAX6639_CH2_TACH_CNT;

  if ((rpm_range = get_rpm_range(sensor, chan)) <= 0)
    return -1;

  switch_swing(sensor);

  uint8_t status;
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             I2C_MAX6639_STATUS, 1, &status) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  // D1: fan1 fault, D0: fan2 fault.
  if (status & (1 << (1 - chan)))
  {
    printf("hv_warning: %s (channel %d) fan fault\n",
           max6639_get_fan_label(sensor, chan), chan);
    switch_release(sensor);
    return -1;
  }

  uint8_t tach;
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tach) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  // The tachometer count is inversely proportional to the fan's RPM.
  return (tach == 255 || tach == 0) ? 0 : (rpm_range / 2 * 60 / tach);
}


/** Get the fan max speed setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return max RPM value, or -1 if there is an error.
 */
static int
max6639_get_fan_max(sens_inst_t* sensor, int chan)
{
  uint8_t val = 0;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_FAN_CONF1 : I2C_MAX6639_CH2_FAN_CONF1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  uint8_t rpm_range = val & 0x03;
  return rpm_ranges[rpm_range];
}


/** Get the desired fan target speed setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return RPM value, or -1 if there is an error.
 */
static int
max6639_get_fan_target(sens_inst_t* sensor, int chan)
{
  uint8_t val;
  int rpm_range;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_TGT_TACH : I2C_MAX6639_CH2_TGT_TACH;

  if ((rpm_range = get_rpm_range(sensor, chan)) <= 0)
    return -1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &val) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  // The tachometer count is inversely proportional to the fan's RPM.
  return (val == 255 || val == 0) ? 0 : (rpm_range / 2 * 60 / val);
}

/** Set the desired fan target speed.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param rpm target RPM value.
 * @return 0 on success, or -1 if there is an error.
 */
static int
max6639_set_fan_target(sens_inst_t* sensor, int chan, int rpm)
{
  uint8_t tach;
  int max;
  int rpm_range;
  int reg = (chan == 0) ? I2C_MAX6639_CH1_TGT_TACH : I2C_MAX6639_CH2_TGT_TACH;

  if ((rpm_range = get_rpm_range(sensor, chan)) <= 0)
    return -1;

  max = max6639_get_fan_max(sensor, chan);
  if (max < 0 || rpm > max)
    return -1;

  // The tachometer count is inversely proportional to the fan's RPM.
  tach = (rpm == 0) ? 255 : (rpm_range / 2 * 60 / rpm);

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tach) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return 0;
}

/** Get the fan control temperature sensor channel bitmap.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return the bitfields on success, or -1 if there is an error.
 */
static int
max6639_get_fan_bitmap(sens_inst_t* sensor, int chan)
{
  int bitmap = 0;
  uint8_t fan_conf_1;
  int reg = chan ? I2C_MAX6639_CH2_FAN_CONF1: I2C_MAX6639_CH1_FAN_CONF1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  if (fan_conf_1 & (1 << 3))
    bitmap |= 1 << 0;
  if (fan_conf_1 & (1 << 2))
    bitmap |= 1 << 1;

  switch_release(sensor);
  return bitmap;
}

/** Set fan control temperature sensor channel bitmap.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param bitmap the sensor channel bitmap.
 * @return 0 on success, or -1 if there is an error.
 */
static int
max6639_set_fan_bitmap(sens_inst_t* sensor, int chan, int bitmap)
{
  uint8_t fan_conf_1;
  int reg = chan ? I2C_MAX6639_CH2_FAN_CONF1: I2C_MAX6639_CH1_FAN_CONF1;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  fan_conf_1 &= ~0x0C;
  if (bitmap & (1 << 0))
    fan_conf_1 |= 1 << 3;
  if (bitmap & (1 << 1))
    fan_conf_1 |= 1 << 2;

  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &fan_conf_1) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  switch_release(sensor);
  return 0;
}

/** Get the fan start temperature.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature in degress Kelvin on success, or -1 if there is an error.
 */
static int
max6639_get_fan_start(sens_inst_t* sensor, int chan)
{
  uint8_t tstart;
  int reg;
  reg = chan ? I2C_MAX6639_CH2_MIN_FS_TEMP : I2C_MAX6639_CH1_MIN_FS_TEMP;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tstart) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  switch_release(sensor);
  return tstart + HV_SYSCONF_TEMP_KTOC;
}

/** Set the fan start temperature.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param tstart the min fan start termperature.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
max6639_set_fan_start(sens_inst_t* sensor, int chan, int tstart)
{
  int reg = chan ? I2C_MAX6639_CH2_MIN_FS_TEMP : I2C_MAX6639_CH1_MIN_FS_TEMP;

  tstart -= HV_SYSCONF_TEMP_KTOC;
  if (tstart < 0 || tstart > MAX6639_TEMP_LIMIT)
    return HV_EINVAL;

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tstart) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  switch_release(sensor);
  return 0;
}

/** Get the fan full speed temperature.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature in degrees Kelvin on success, or -1 if there is an error.
 */
static int
max6639_get_fan_full(sens_inst_t* sensor, int chan)
{
  uint8_t tfull;
  int reg = chan ? I2C_MAX6639_CH2_THERM_LIM : I2C_MAX6639_CH1_THERM_LIM;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tfull) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  switch_release(sensor);
  return tfull + HV_SYSCONF_TEMP_KTOC;
}

/** Set the fan start temperature.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param tstop the min fan start termperature.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
max6639_set_fan_full(sens_inst_t* sensor, int chan, int tfull)
{
  int reg = chan ? I2C_MAX6639_CH2_THERM_LIM : I2C_MAX6639_CH1_THERM_LIM;

  tfull -= HV_SYSCONF_TEMP_KTOC;
  if (tfull < 0 || tfull > MAX6639_TEMP_LIMIT)
    return HV_EINVAL;

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &tfull) != 1)
  {
    switch_release(sensor);
    return -1;
  }

  switch_release(sensor);
  return 0;
}

/** Read the register of the MAX6639 sensor chip.
 * @param sensor The sensor chip instance.
 * @param chan sensor or fan control channel.
 * @param reg register address.
 * @param buf data buffer.
 * @param count read up to count bytes.
 * @return 0 on success, or negative error code if there is an error.
 */
int
max6639_read(void* sensor, int chan, int reg, void* buf, int count)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  struct sensor_conf* conf;

  if (!(reg == SENSOR_TEMP_LABEL || reg == SENSOR_FAN_LABEL) &&
      (unsigned long)buf & (SENSOR_REG_WIDTH - 1))
    return HV_EINVAL;

  if (count < SENSOR_REG_WIDTH)
    return HV_EINVAL;

  int ret = 0;
  switch (reg)
  {
  case SENSOR_CHIP_CONF:
    conf = (struct sensor_conf *)buf;

    /* The onchip temperature diode is always present. */
    if (inst->fan_descs[0].temp_valid)
      conf->temp_chan = 2;
    else
      conf->temp_chan = 1;

    for (int i = 0; i < 2; i++)
      if (inst->fan_descs[i].max_speed)
        conf->fan_chan++;

    return HV_OK;

  case SENSOR_TEMP_LABEL:
    strncpy(buf, max6639_get_temp_label(inst, chan), count);
    return HV_OK;

  case SENSOR_FAN_LABEL:
    strncpy(buf, max6639_get_fan_label(inst, chan), count);
    return HV_OK;

  case SENSOR_TEMP_INPUT:
    ret = max6639_get_temp(inst, chan);
    break;

  case SENSOR_TEMP_CRIT:
    ret = max6639_get_ot_limit(inst, chan);
    break;

  case SENSOR_TEMP_FSTART:
    ret = max6639_get_fan_start(inst, chan);
    break;

  case SENSOR_TEMP_FFULL:
    ret = max6639_get_fan_full(inst, chan);
    break;

  case SENSOR_FAN_MODE:
    ret = max6639_get_mode(inst, chan);
    break;

  case SENSOR_FAN_PWM:
    ret = max6639_get_pwm(inst, chan);
    break;

  case SENSOR_FAN_PPR:
    ret = max6639_get_fan_ppr(inst, chan);
    break;

  case SENSOR_FAN_INPUT:
    ret = max6639_get_rpm(inst, chan);
    break;

  case SENSOR_FAN_MAX:
    ret = max6639_get_fan_max(inst, chan);
    break;

  case SENSOR_FAN_TGT:
    ret = max6639_get_fan_target(inst, chan);
    break;

  case SENSOR_FAN_CAP:
    // In auto mode, both fans can be controlled by either of the temperature
    // diode, if the diode is connected.
    for (int i = 0; i < 2; i++)
      if (inst->fan_descs[i].temp_valid)
        ret |= 1 << i;
    break;

  case SENSOR_FAN_CTL:
    ret = max6639_get_fan_bitmap(inst, chan);
    break;

  default:
    return HV_EINVAL;
  }

  if (ret < 0)
    return HV_EIO;
  else
  {
    *(int*) buf = ret;
    return HV_OK;
  }
}


/** Write the register of the MAX6639 sensor chip.
 * @param sensor The sensor chip instance.
 * @param chan sensor or fan control channel.
 * @param reg register address.
 * @param buf data buffer.
 * @param count write up to count bytes.
 * @return 0 on success, or negative error code if there is an error.
 */
int
max6639_write(void* sensor, int chan, int reg, void* buf, int count)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  int err;
  int val;

  if ((unsigned long)buf & (SENSOR_REG_WIDTH - 1))
    return HV_EINVAL;

  if (count != SENSOR_REG_WIDTH)
    return HV_EINVAL;

  val = *(int*)buf;
  switch (reg)
  {
  case SENSOR_TEMP_CRIT:
    err = max6639_set_ot_limit(inst, chan, val);
    break;

  case SENSOR_TEMP_FSTART:
    err = max6639_set_fan_start(inst, chan, val);
    break;

  case SENSOR_TEMP_FFULL:
    err = max6639_set_fan_full(inst, chan, val);
    break;

  case SENSOR_FAN_MODE:
    err = max6639_set_mode(inst, chan, val);
    break;

  case SENSOR_FAN_PWM:
    if (max6639_get_mode(inst, chan) != MAX6639_PWM_MODE)
    {
      int rpm = max6639_get_fan_target(inst, chan);
      max6639_set_fan_target(inst, chan, 0);
      err = max6639_set_mode(inst, chan, MAX6639_PWM_MODE);
      if (err)
      {
        max6639_set_fan_target(inst, chan, rpm);
        break;
      }
    }
    err = max6639_set_pwm(inst, chan, val);
    break;

  case SENSOR_FAN_PPR:
    err = max6639_set_fan_ppr(inst, chan, val);
    break;

  case SENSOR_FAN_TGT:
    err = max6639_set_fan_target(inst, chan, val);
    if (err)
      break;
    if (max6639_get_mode(inst, chan) != MAX6639_RPM_MODE)
      err = max6639_set_mode(inst, chan, MAX6639_RPM_MODE);
    break;

  case SENSOR_FAN_CTL:
    err = max6639_set_fan_bitmap(inst, chan, val);
    break;

  default:
    err = HV_EINVAL;
    break;
  }

  return err;
}


/** Read the cpu (remote) temperature from the MAX6639 sensor chip.
 * @param sensor The sensor chip instance.
 * @return The cpu temperature, in degrees Kelvin, or -1 if there is
 *         an error.
 */
long
max6639_read_cpu_temp(void* sensor)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  return max6639_get_temp(inst, 0);
}


/** Read the board (local) temperature from the MAX6639 sensor chip.
 * @param sensor The sensor chip instance.
 * @return The board temperature, in degrees Kelvin, or -1 if there is
 *         an error.
 */
long
max6639_read_board_temp(void* sensor)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  return max6639_get_temp(inst, 1);
}


/** Add a new temp_sensor_table entry. */
static const __TEMP_SENSOR_ATTR temp_sensor_t ts_table_entry_max6639 =
{
  .bib_type = BI_TEMP_CFG_TYPE__VAL_MAX6639,
  .init = max6639_init_temp_sens,
  .config = max6639_config_temp_sens,
  .read_cpu_temp = max6639_read_cpu_temp,
  .read_board_temp = max6639_read_board_temp,
  .read = max6639_read,
  .write = max6639_write,
  .name = "MAX6639",
};
