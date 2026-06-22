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
 * TILE-Gx temperature sensor and fan controller pseudo driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "lock.h"

#include "sensor.h"


/** Sensor Device state structure pointer. */
static sensor_state_t* sensor_state _SHARED = { 0 };

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t sensor_alloc_lock _SHARED = DRV_SPINLOCK_INIT;


/** A helper routine for reading the fan regulator bitmap register.
 * @param ss pointer to the sensor state structure.
 * @param index channel index.
 * @param reg register index.
 * @param inst The sensor chip instance.
 * @param chan fan regulator channel index on the sensor chip.
 * @param buf buffer used to save the result.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
sens_read_bitmap(sensor_state_t* ss, int index, int reg,
                 sens_inst_t* inst, int chan, void* buf)
{
  int ret = 0;

  if ((unsigned long)buf & (SENSOR_REG_WIDTH - 1))
    return HV_EINVAL;

  int32_t val;
  if ((ret = inst->sensor->read(inst, chan, reg, &val, sizeof (val))))
    return ret;

  *(int32_t*)buf = val << ss->fan[index].off;
  return ret;
}


/**
 * Read the sensor chip register.
 * @param ss pointer to the sensor state structure.
 * @param index channel index.
 * @param reg register index.
 * @param buf buffer used to save the result.
 * @param len read length in bytes.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
sens_reg_read(sensor_state_t* ss, int index, int reg, void* buf, int len)
{
  sens_inst_t* inst;
  int chan;

  if (reg & SENSOR_REG_TYPE_FAN)
  {
    if (index > ss->fan_num)
      return HV_EINVAL;

    inst = temp_sensor[ss->fan[index].chip];
    chan = ss->fan[index].chan;
  }
  else
  {
    if (index > ss->temp_num)
      return HV_EINVAL;

    inst = temp_sensor[ss->temp[index].chip];
    chan = ss->temp[index].chan;
  }

  if (reg == SENSOR_FAN_CAP || reg == SENSOR_FAN_CTL)
    return sens_read_bitmap(ss, index, reg, inst, chan, buf);
  else
    return inst->sensor->read(inst, chan, reg, buf, len);
}


/** A helper routine for writing the fan regulator bitmap register.
 * @param ss pointer to the sensor state structure.
 * @param index fan regulator channel index.
 * @param reg register index.
 * @param inst The sensor chip instance.
 * @param chan fan regulator channel index on the sensor chip.
 * @param val the value to be written to register.
 * @return 0 on success, or netative error code if there is an error.
 */
static int
sens_write_bitmap(sensor_state_t* ss, int index, int reg,
                  sens_inst_t* inst, int chan, int val)
{
  int ret;
  int32_t cap;
  if ((ret = sens_reg_read(ss, index, SENSOR_FAN_CAP, &cap, SENSOR_REG_WIDTH)))
    return ret;

  val = (val & cap) >> ss->fan[index].off;
  ret = inst->sensor->write(inst, chan, reg, &val, sizeof (val));

  return ret;
}


/**
 * Write the sensor chip register.
 * @param ss pointer to the sensor state structure.
 * @param index channel index.
 * @param reg register index.
 * @param val the value to be written to register.
 * @return 0 on success, or negative error code if there is an error.
 */
static int
sens_reg_write(sensor_state_t* ss, int index, int reg, int val)
{
  sens_inst_t* inst;
  int chan;

  if (reg & SENSOR_REG_TYPE_FAN)
  {
    if (index > ss->fan_num)
      return HV_EINVAL;

    inst = temp_sensor[ss->fan[index].chip];
    chan = ss->fan[index].chan;
  }
  else
  {
    if (index > ss->temp_num)
      return HV_EINVAL;

    inst = temp_sensor[ss->temp[index].chip];
    chan = ss->temp[index].chan;
  }

  if (reg == SENSOR_FAN_CTL)
    return sens_write_bitmap(ss, index, reg, inst, chan, val);
  else
    return inst->sensor->write(inst, chan, reg, &val, sizeof (val));
}


/**
 * Sensor driver init routine.
 */
static int
gxsensor_init(const char* drvname, void** statepp, int instance, int tileno,
              pos_t tile, const struct dev_info* info, const char* args)
{
  sensor_state_t* ss;

  DEVICE_TRACE("%s: devname %s instance %#x tile %#x\n",
               __func__, drvname, instance, tile.word);

  if (instance >= 1)
  {
    tprintf("failed to init driver %s, max instances exceeded\n", drvname);
    return HV_ENODEV;
  }

  drv_spin_lock(&sensor_alloc_lock);
  ss = sensor_state;
  if (ss == NULL)
  {
    // Allocate our state.
    ss = drv_shared_state_zalloc(sizeof (*ss), 0);
    if (ss == NULL)
    {
      drv_spin_unlock(&sensor_alloc_lock);
      return HV_ENOMEM;
    }

    sensor_state = ss;
    drv_spin_lock_init(&ss->lock);
  }

  drv_spin_unlock(&sensor_alloc_lock);
  *statepp = ss;

  return HV_OK;
}


/**
 * Sensor driver open routine.
 */
static int
gxsensor_open(int devhdl, void* statep, const char* suffix,
              uint32_t flags, pos_t tile)
{
  DEVICE_TRACE("%s: devhdl %#x suffix \"%s\" flags %#x " "tile %#x\n",
               __func__, devhdl, suffix, flags, tile.word);

  sensor_state_t* ss = statep;

  int temp_index = 0;
  int fan_index = 0;
  for (int chip = 0; chip < SENSOR_NUM; chip++)
  {
    sens_inst_t* inst = temp_sensor[chip];
    struct sensor_conf* conf = &ss->conf[chip];
    if (inst->sensor->read == NULL || inst->sensor->write == NULL ||
        inst->sensor->read(inst, -1, SENSOR_CHIP_CONF, conf,
                           sizeof (struct sensor_conf)))
      continue;

    if (ss->temp_num + conf->temp_chan > TEMP_CHAN_NUM)
    {
      tprintf("sensor: sensor channel exceeds the max %d\n", TEMP_CHAN_NUM);
      break;
    }
    if (ss->fan_num + conf->fan_chan > FAN_CHAN_NUM)
    {
      tprintf("sensor: fan channel exceeds the max %d\n", FAN_CHAN_NUM);
      break;
    }

    for (int chan = 0; chan < conf->fan_chan; chan++, fan_index++)
    {
      ss->fan[fan_index].chip = chip;
      ss->fan[fan_index].chan = chan;
      ss->fan[fan_index].off = temp_index;
    }
    for (int chan = 0; chan < conf->temp_chan; chan++, temp_index++)
    {
      ss->temp[temp_index].chip = chip;
      ss->temp[temp_index].chan = chan;
    }

    ss->temp_num += conf->temp_chan;
    ss->fan_num += conf->fan_chan;
  }

  if (ss->temp_num > 0 || ss->fan_num > 0)
    return HV_OK;
  else
    return HV_ENODEV;
}


/**
 * Sensor driver read routine.
 */
static int
gxsensor_pread(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  sensor_state_t* ss = statep;
  int channel = SENSOR_CHANNEL_INDEX(offset);
  int reg = SENSOR_REG_ADDR(offset);
  char label[SENSOR_LABEL_LENGTH + 1] = { 0 };
  int ret = len;
  int32_t val;
  void* addr;

  DEVICE_TRACE("%s: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", __func__, devhdl, flags,
               va, len, offset, tile.word);

  drv_spin_lock(&ss->lock);
  switch (SENSOR_OFFSET_BASE(offset))
  {
  case SENSOR_TEMP_NR_OFF:
    if (len != sizeof(ss->temp_num))
    {
      ret = HV_EINVAL;
      break;
    }

    if (drv_copy_to_client(va, (char*)&ss->temp_num, len, flags))
      ret = HV_EFAULT;
    break;

  case SENSOR_FAN_NR_OFF:
    if (len != sizeof(ss->fan_num))
    {
      ret = HV_EINVAL;
      break;
    }

    if (drv_copy_to_client(va, (char*)&ss->fan_num, len, flags))
      ret = HV_EFAULT;
    break;

  case SENSOR_DATA_OFF:
    if (reg == SENSOR_CHIP_CONF)
    {
      ret = HV_EPERM;
      break;
    }

    if (reg == SENSOR_TEMP_LABEL || reg == SENSOR_FAN_LABEL)
    {
      addr = &label;
      if ((ret = sens_reg_read(ss, channel, reg, addr, SENSOR_LABEL_LENGTH)))
        break;
      len = min(len, strlen(label));
    }
    else
    {
      if ((len != SENSOR_REG_WIDTH))
      {
        ret = HV_EINVAL;
        break;
      }
      addr = &val;
      if ((ret = sens_reg_read(ss, channel, reg, addr, len)))
        break;
    }

    if (drv_copy_to_client(va, (char*)addr, len, flags))
      ret = HV_EFAULT;
    break;

  default:
    ret = HV_EINVAL;
    break;
  }

  drv_spin_unlock(&ss->lock);
  return ret;
}


/**
 * Sensor driver write routine.
 */
static int
gxsensor_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
                uint32_t len, uint64_t offset, pos_t tile)
{
  sensor_state_t* ss = statep;
  int channel = SENSOR_CHANNEL_INDEX(offset);
  int reg = SENSOR_REG_ADDR(offset);
  int ret = len;
  int32_t val;

  DEVICE_TRACE("%s: devhdl %#x flags %#x va %p len %d offset %#llx "
               "tile %#x\n", __func__, devhdl, flags,
               va, len, offset, tile.word);

  drv_spin_lock(&ss->lock);
  switch (SENSOR_OFFSET_BASE(offset))
  {
  case SENSOR_DATA_OFF:
    if (len != SENSOR_REG_WIDTH)
    {
      ret = HV_EINVAL;
      break;
    }

    if (reg == SENSOR_CHIP_CONF ||
        reg == SENSOR_TEMP_LABEL ||
        reg == SENSOR_TEMP_INPUT ||
        reg == SENSOR_FAN_LABEL ||
        reg == SENSOR_FAN_INPUT ||
        reg == SENSOR_FAN_MAX ||
        reg == SENSOR_FAN_CAP)
    {
      ret = HV_EPERM;
      break;
    }
    else
    {
      if (drv_copy_from_client((char*)&val, va, len, flags))
      {
        ret = HV_EFAULT;
        break;
      }
    }

    ret = sens_reg_write(ss, channel, reg, val);
    break;

  default:
    ret = HV_ENOTSUP;
    break;
  }

  drv_spin_unlock(&ss->lock);
  return ret;
}


/**
 * Sensor driver close routine.
 */
static int
gxsensor_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("%s: devhdl %#x\n", __func__, devhdl);
  return HV_OK;
}


/** TILE-GX sensor driver operations vector. */
static struct drv_ops gxsensor_ops = {
  .init        = gxsensor_init,
  .open        = gxsensor_open,
  .close       = gxsensor_close,
  .pread       = gxsensor_pread,
  .pwrite      = gxsensor_pwrite,
};

/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_sensor = {
  .shim_type = DEV_PSEUDO_SENSOR,
  .name      = "sensor",
  .desc      = "Temperature sensor and fan speed controller",
  .ops       = &gxsensor_ops,
};
