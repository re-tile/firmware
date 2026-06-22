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
 * Plugin for the LM95235 temperature sensor.
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





#define LM95235_TEMP_DELTA_CPU    -6 /**< LM95235 cpu temperature offset */
#define LM95235_TEMP_DELTA_BOARD   0 /**< LM95235 board temperature offset */

#define LM95235_SIGNED_TEMP_LIMIT   127 /**< LM95235 max temp in signed reg */
#define LM95235_UNSIGNED_TEMP_LIMIT 255 /**< LM95235 max temp in unsigned reg */

// On the TILExpress-64 and TILExpress-20G, T_CRIT is hardwired to board power.
// We set the LM95235 to assert T_CRIT at a very high temperature.

/** LM95235 critical cpu temperature */
#define LM95235_TEMP_CRIT_CPU    125

/** Lock used to make sure that only one tile allocates shared data. */
static spinlock_t lm_alloc_lock _SHARED = SPINLOCK_INIT;


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
lm95235_init_temp_sens(void** sensorpp, uint32_t desc, bi_ptr_t resptr)
{
  sens_inst_t* inst = *sensorpp;
  spin_lock(&lm_alloc_lock);
  if (inst == NULL)
  {
    inst = drv_shared_state_zalloc(sizeof (sens_inst_t), 0);
    if (inst == NULL)
    {
      spin_unlock(&lm_alloc_lock);
      return HV_EFAULT;
    }
    *sensorpp = inst;
  }

  struct bi_temp_cfg* bi = resptr;
  inst->i2c_addr = bi->addr.dev_addr << 1;
  inst->bus = bi->addr.bus;
  inst->switch_inst = bi->addr.switch_inst;
  inst->switch_chan = bi->addr.switch_chan;
  inst->bib_instance = BI_INST(desc);

  spin_unlock(&lm_alloc_lock);
  return HV_OK;
}


/** Configure the critical temperature thresholds in the LM95235.
 * The sensor is hard-wired to the board's power, and will shut the board
 * down if these temperatures are exceeded.
 * @param sensor The sensor chip instance.
 */
void
lm95235_config_temp_sens(void* sensor)
{
  // Set defaults in the cpu and board critical limit registers
  // (which are hard wired on the board to shut off power if the
  // critical temperature is exceeded).

  sens_inst_t* inst = (sens_inst_t*)sensor;
  switch_swing(inst);

  // Mask off all but CPU critical temperature (Remote !T_CRIT)
  uint8_t config1 = I2C_LM95235_CFG1_LOC_OS_MASK |
    I2C_LM95235_CFG1_LOC_TCRIT_MASK | 
    I2C_LM95235_CFG1_REM_OS_MASK;

  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_LM95235_W_CFG1, 1, &config1) != 1)
  {
    printf("hv_warning: can't configure lm95235 temperature sensor at "
           "%d/%02x (1)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }

  // For Gx, we turn off the digital filter and use Diode Model 2.
  uint8_t config2 = I2C_LM95235_CFG2_FLT_OS_MASK |
    I2C_LM95235_CFG2_FLT_TCRIT_MASK | I2C_LM95235_CFG2_OS_ENABLE_MASK;

  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_LM95235_RW_CFG2, 1, &config2) != 1)
  {
    printf("hv_warning: can't configure lm95235 temperature sensor at "
           "%d/%02x (2)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }

  // Set critical CPU temperature.
  uint8_t crit_cpu_temp = LM95235_TEMP_CRIT_CPU - LM95235_TEMP_DELTA_CPU;
  if (i2c_wr(i2cm_info[inst->bus]->idn_ports[0],
             i2cm_info[inst->bus]->channel, inst->i2c_addr,
             I2C_LM95235_RW_REM_CRIT_LIM, 1, &crit_cpu_temp) != 1)
  {
    printf("hv_warning: can't configure lm95235 temperature sensor at "
           "%d/%02x (3)\n", inst->bus, inst->i2c_addr);
    switch_release(inst);
    return;
  }
  switch_release(inst);
}


/** Get the temperature sensor input.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature in degrees Kelvin, or -1 if there is an error.
 */
static int
lm95235_get_temp(sens_inst_t* sensor, int chan)
{
  int8_t temp;

  switch_swing(sensor);
  if (chan == 0)
  {
    // Read the signed register first to check for a below-
    // freezing cpu.
    if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
               i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
               I2C_LM95235_R_REM_MSB, 1, &temp) != 1)
    {
      printf("error reading reg %d from i2c addr %d\n",
             I2C_LM95235_R_REM_MSB, sensor->i2c_addr);
      switch_release(sensor);
      return -1;
    }

    if (temp < 0)
    {
      switch_release(sensor);
      // Return value adjusted based on TT parts
      return ((int) temp + LM95235_TEMP_DELTA_CPU + HV_SYSCONF_TEMP_KTOC);
    }
    else
    {
      uint8_t unsigned_temp;

      // Read the unsigned register, which has a greater range.
      if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
                 i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
                 I2C_LM95235_R_REM_UNS_MSB, 1, &unsigned_temp) != 1)
      {
        switch_release(sensor);
        return -1;
      }

      switch_release(sensor);

      if (((long)unsigned_temp) >= LM95235_UNSIGNED_TEMP_LIMIT)
        return (HV_SYSCONF_OVERTEMP + HV_SYSCONF_TEMP_KTOC);

      return (((long)unsigned_temp) + LM95235_TEMP_DELTA_CPU +
              HV_SYSCONF_TEMP_KTOC);
    }
  }
  else
  {
    if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
               i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
               I2C_LM95235_R_LOC_MSB, 1, &temp) != 1)
    {
      switch_release(sensor);
      return -1;
    }

    switch_release(sensor);

    if (temp >= LM95235_SIGNED_TEMP_LIMIT)
      return (HV_SYSCONF_OVERTEMP + HV_SYSCONF_TEMP_KTOC);

    return (temp + LM95235_TEMP_DELTA_BOARD + HV_SYSCONF_TEMP_KTOC);
  }
}


/** Read the cpu (remote) temperature from the LM95235 sensor chip.
 * @param sensor The sensor chip instance.
 * @return The cpu temperature, in degrees Kelvin, or -1 if there is
 *         an error.
 */
long
lm95235_read_cpu_temp(void* sensor)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  return lm95235_get_temp(inst, 0);
}


/** Read the board (local) temperature from the LM95235 sensor chip.
 * @param sensor The sensor chip instance.
 * @return The board temperature, in degrees Kelvin, or -1 if there is
 *         an error.
 */
long
lm95235_read_board_temp(void* sensor)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;
  return lm95235_get_temp(inst, 1);
}


/** Get the temperature sensor label string.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return the label string.
 */
static char*
lm95235_get_temp_label(sens_inst_t* sensor, int chan)
{
  char* label;

  // TODO: Read the label from BIB, it requires add such label info to BIB.
  if (chan < 0 || chan > 1)
    label = "not exist";
  else if (chan == 0 && sensor->bib_instance == 0)
    label = "cpu temp";
  else
    label = "board temp";

  return label;
}


/** Get the OT temperature setting.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @return temperature value in degrees Kelvin, or -1 if there is an error.
 */
static int
lm95235_get_ot_limit(sens_inst_t* sensor, int chan)
{
  uint8_t ot;
  int reg = chan ? I2C_LM95235_RW_LOC_LIM : I2C_LM95235_RW_REM_CRIT_LIM;

  switch_swing(sensor);
  if (i2c_rd(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &ot) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return (int)(ot + HV_SYSCONF_TEMP_KTOC);
}


/** Set the OT temperature value.
 * @param sensor The sensor chip instance.
 * @param chan sensor channel.
 * @param ot OT temperature in degrees Kelvin.
 * @return 0 on succes, or -1 if there is an error.
 */
static int
lm95235_set_ot_limit(sens_inst_t* sensor, int chan, int ot)
{
  int reg = chan ? I2C_LM95235_RW_LOC_LIM : I2C_LM95235_RW_REM_CRIT_LIM;

  // Celsius degree in register.
  ot -= HV_SYSCONF_TEMP_KTOC;

  switch_swing(sensor);
  if (i2c_wr(i2cm_info[sensor->bus]->idn_ports[0],
             i2cm_info[sensor->bus]->channel, sensor->i2c_addr,
             reg, 1, &ot) != 1)
  {
    switch_release(sensor);
    return -1;
  }
  switch_release(sensor);

  return 0;
}


/** Read the register of the LM95235 sensor chip.
 * @param sensor The sensor chip instance.
 * @param chan sensor or fan control channel.
 * @param reg register address.
 * @param buf data buffer.
 * @param count read up to count bytes.
 * @return 0 on success, or negative if there is an error.
 */
int
lm95235_read(void* sensor, int chan, int reg, void* buf, int count)
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
    conf->temp_chan = 2;
    conf->fan_chan = 0;

    return HV_OK;

  case SENSOR_TEMP_LABEL:
    strncpy(buf, lm95235_get_temp_label(inst, chan), count);
    return HV_OK;

  case SENSOR_TEMP_INPUT:
    ret = lm95235_get_temp(inst, chan);
    break;

  case SENSOR_TEMP_CRIT:
    ret = lm95235_get_ot_limit(inst, chan);
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


/** Write the register of the LM95235 sensor chip.
 * @param sensor The sensor chip instance.
 * @param chan sensor or fan control channel.
 * @param reg register address.
 * @param buf data buffer.
 * @param count write up to count bytes.
 * @return 0 on success, or negative if there is an error.
 */
int
lm95235_write(void* sensor, int chan, int reg, void* buf, int count)
{
  sens_inst_t* inst = (sens_inst_t*)sensor;

  if ((unsigned long)buf & (SENSOR_REG_WIDTH - 1))
    return HV_EINVAL;

  if (count != SENSOR_REG_WIDTH)
    return HV_EINVAL;

  int err;
  int val = *(int*)buf;
  switch (reg)
  {
  case SENSOR_TEMP_CRIT:
    err = lm95235_set_ot_limit(inst, chan, val);
    break;

  default:
    err = HV_EINVAL;
    break;
  }

  return err;
}


/** Add a new temp_sensor_table entry. */
static const __TEMP_SENSOR_ATTR temp_sensor_t ts_table_entry_lm95235 =
{
  .bib_type = BI_TEMP_CFG_TYPE__VAL_LM95235,
  .init = lm95235_init_temp_sens,
  .config = lm95235_config_temp_sens,
  .read_cpu_temp = lm95235_read_cpu_temp,
  .read_board_temp = lm95235_read_board_temp,
  .read = lm95235_read,
  .write = lm95235_write,
  .name = "LM95235",
};
