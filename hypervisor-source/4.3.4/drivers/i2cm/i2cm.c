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
 * I2C master driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/i2cm.h>


#include "sys/libc/include/util.h"

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "i2c_acc.h"
#include "i2cm.h"
#include "types.h"

/** Largest read or write we'll do; if a request is larger, we just do this
 *  many bytes and allow the client to resubmit the rest.  (Ideally we
 *  wouldn't have a limit, but we need one since we allocate space for the
 *  data on our stack.) */
#define HV_I2CM_CHUNK_SIZE 1024

/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t i2cm_alloc_lock _SHARED = DRV_SPINLOCK_INIT;

/** Address of the shared state object. */
i2cm_state_t* i2cm_state[MAX_I2CMS] _SHARED = { 0 };


/* i2c answers the probe, but isn't wired up */


/** I2C Master driver probe routine. */
static int
i2cm_probe(const char* drvname, int instance,
           pos_t tile, const struct dev_info* info)
{
  //
  // Save info pointer in global variable for use by board info block code.
  //
  if (instance < MAX_I2CMS)
    i2cm_info[instance] = info;

  return (0);
}


/** Dump the contents of all SPD PROMs to the console. */
void
i2cm_dump_spd(i2cm_state_t* is)
{
  printf("Debug dump of SPD PROMs:\n");

  for (int dev = 0; dev < 8; dev++)
  {
    uint32_t buf[32];

    //
    // First read just one byte to see if the device is really there;
    // otherwise, if it's not, the I2C shim hangs trying to complete the
    // whole read.
    //
    int len = i2c_rd(is->shim_pos, is->shim_chan, I2C_ROM_ADDR(dev), 0, 1,
                     buf);
    if (len <= 0)
    {
      printf("Target %d unreadable, code %d.\n", dev, len);
      continue;
    }

    //
    // Now try to read in the whole PROM
    //
    len = i2c_rd(is->shim_pos, is->shim_chan, I2C_ROM_ADDR(dev), 0,
                 sizeof (buf), buf);

    if (len <= 0)
    {
      printf("Target %d unreadable, code %d.\n", dev, len);
      continue;
    }

    printf("Target %d:\n    ", dev);
    unsigned char* cp = (unsigned char*) buf;
    for (int i = 0; i < len; i++)
    {
      if (i && !(i & 0xF))
        printf("\n    ");
      printf("%02x ", cp[i]);
    }
    printf("\n");
  }
}


static struct i2c_dev_attr*
get_i2c_dev_attr(i2cm_state_t* is, uint16_t slave_addr)
{
  for (int i = 0; i < is->num_devs; i++)
  {
    struct i2c_dev_attr* i2c_dev = &is->i2c_dev_table[i];

    if (slave_addr == i2c_dev->dev_desc.addr)
      return i2c_dev;
  }

  return NULL;
}

static void
create_i2c_dev_table(i2cm_state_t* is)
{
  int dev = 0;

  uint32_t i2c_desc;
  bi_ptr_t resptr;
  int offset = 0;

  while ((i2c_desc = bi_getparam(BI_TYPE_I2C_DEV_CFG, -1, &resptr,
                                 &offset)) != BI_NULL)
  {
    if (dev >= MAX_NUM_I2C_DEVS)
    {
      printf("hv_warning: BIB contains more than max %d I2C devices "
             "that are exposed to the client, ignoring extras\n", dev);
      break;
    }

    struct i2c_dev_attr* i2c_dev = &is->i2c_dev_table[dev];

    uint32_t wds = BI_WDS(i2c_desc);

    struct bi_i2c_dev_cfg* bi = resptr;

    if (bi->addr.bus != is->instance)
      continue;

    i2c_dev->dev_desc.addr = bi->addr.dev_addr << 1;

    i2c_dev->addr_size =
      (bi->mem_addr_0bit) ? 0 : (bi->mem_addr_8bit) ? 1 : 2;

    i2c_dev->switch_inst = bi->addr.switch_inst;
    i2c_dev->switch_chan = bi->addr.switch_chan;

    switch (bi->page_size)
    {
      case BI_I2C_DEV_CFG_PAGE_SIZE__VAL_DEFAULT:
        i2c_dev->page_size = 8;
        break;
      default:
        i2c_dev->page_size = 1 << bi->page_size;
        break;
    }

    switch (bi->write_cycle)
    {
      case BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_DEFAULT:
        i2c_dev->write_cycle = 5;
        break;
      case BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_MAX:
        i2c_dev->write_cycle = 9;
        break;
      case BI_I2C_DEV_CFG_WRITE_CYCLE__VAL_MIN:
        i2c_dev->write_cycle = 0;
        break;
      default:
        i2c_dev->write_cycle = bi->write_cycle;
        break;
    }

    if (wds > 1)
    {
      int name_len = (wds - 1) * sizeof (uint32_t);
      name_len = min(name_len, I2C_DEV_NAME_SIZE - 1);
      strncpy(i2c_dev->dev_desc.name, bi->name, name_len);
      i2c_dev->dev_desc.name[name_len] = '\0';
    }

    dev++;
  }

  is->num_devs = dev;

  return;
}

/** I2C Master driver initialization routine. */
static int
i2cm_init(const char* drvname, void** statepp, int instance, int tileno,
          pos_t tile, const struct dev_info* info, const char* args)
{
  if (instance >= MAX_I2CMS)
    return HV_ENODEV;

  drv_spin_lock(&i2cm_alloc_lock);
  i2cm_state_t* is = i2cm_state[instance];
  if (is == NULL)
  {
    is = drv_shared_state_zalloc(sizeof(*is), 0);
    if (is == NULL)
    {
      drv_spin_unlock(&i2cm_alloc_lock);
      return HV_ENOMEM;
    }
    i2cm_state[instance] = is;

    is->instance = instance;
    is->shim_pos = info->idn_ports[0];
    is->shim_chan = info->channel;

    //
    // Enable the device.
    //
    i2c_enable_bib(info->idn_ports[0], info->channel);

    //
    // If requested, dump the SPDs.
    //
    if (args && strstr(args, "dumpspd=1"))
      i2cm_dump_spd(is);

    //
    // Set up the table of i2c devices.
    //
    create_i2c_dev_table(is);
  }
  drv_spin_unlock(&i2cm_alloc_lock);

  *statepp = is;

  return (0);
}

/** I2C Master driver open routine. */
static int
i2cm_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
          pos_t tile)
{
  DEVICE_TRACE("i2cm_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  return (0);
}


/** I2C Master driver close routine. */
static int
i2cm_close(int devhdl, void* statep, pos_t tile)
{
  return (0);
}

/** I2C Master driver read routine. */
static int
i2cm_pread(int devhdl, void* statep, uint32_t flags, char* va,
           uint32_t len, uint64_t offset, pos_t tile)
{
  i2cm_state_t* is = statep;

  DEVICE_TRACE("i2cm_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // Handle the special cases.
  //
  if (offset == I2C_GET_NUM_DEVS_OFF)
  {
    if (len != sizeof (is->num_devs))
      return (HV_EINVAL);

    if (drv_copy_to_client(va, (char*) &is->num_devs, len, flags))
       return (HV_EFAULT);

    return (len);
  }
  else if (offset >= I2C_GET_DEV_INFO_OFF &&
           offset < I2C_GET_DEV_INFO_OFF +
                    is->num_devs * sizeof (tile_i2c_desc_t))
  {
    offset -= I2C_GET_DEV_INFO_OFF;

    len = min(len, is->num_devs * sizeof (tile_i2c_desc_t) - offset);

    tile_i2c_desc_t i2c_desc[is->num_devs];

    for (int dev = 0; dev < is->num_devs; dev++)
    {
      struct i2c_dev_attr* i2c_dev = &is->i2c_dev_table[dev];

      i2c_desc[dev] = i2c_dev->dev_desc;
    }

    if (drv_copy_to_client(va, (char*) i2c_desc + offset, len, flags))
       return (HV_EFAULT);

    return (len);
  }

  //
  // We use the offset to pass in the i2c slave address and data offset.
  //
  tile_i2c_addr_desc_t i2c_addr_desc = { .word = offset };

  uint16_t slave_addr = i2c_addr_desc.addr;

  uint16_t data_offset = i2c_addr_desc.data_offset;

  struct i2c_dev_attr* i2c_dev;

  i2c_dev = get_i2c_dev_attr(is, slave_addr);
  if (i2c_dev == NULL)
    return (HV_ENODEV);

  switch (i2c_dev->addr_size)
  {
  case 0:
    slave_addr |= I2C_DEV_NOADDR;
    break;
  case 1:
    break;
  case 2:
    slave_addr |= I2C_DEV_16BIT;
    break;
  }

  //
  // This is the data address on the i2c device.
  //
  uint16_t data_addr;

  //
  // The first read segment uses the data address set by the preceding
  // dummy write.
  //
  data_addr = i2c_dev->dev_addr + data_offset;

  if (len)
  {
    char buf[HV_I2CM_CHUNK_SIZE];

    if (len > HV_I2CM_CHUNK_SIZE)
      len = HV_I2CM_CHUNK_SIZE;

    //
    // Handle the read.
    //
    int ret = 1;

    i2c_switch_swing(is->instance, i2c_dev->switch_inst, i2c_dev->switch_chan);

    ret = i2c_rd(is->shim_pos, is->shim_chan, slave_addr, data_addr,
                 len, buf);

    i2c_switch_release(is->instance, i2c_dev->switch_inst);

    if (ret <= 0)
      return (HV_EIO);

    //
    // Now copy the data to the client buffer.
    //
    if (drv_copy_to_client((char*)va, (char*)buf, len, flags))
      return (HV_EFAULT);
  }

  return (len);
}


/** I2C Master driver write routine. */
static int
i2cm_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  i2cm_state_t* is = statep;

  DEVICE_TRACE("i2cm_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // We use the offset to pass in the i2c slave address and data offset.
  //
  tile_i2c_addr_desc_t i2c_addr_desc = { .word = offset };

  uint16_t slave_addr = i2c_addr_desc.addr;

  uint16_t data_offset = i2c_addr_desc.data_offset;

  struct i2c_dev_attr* i2c_dev;

  i2c_dev = get_i2c_dev_attr(is, slave_addr);
  if (i2c_dev == NULL)
    return (HV_ENODEV);

  //
  // This is the width of the data address, 8-bit or 16-bit.
  //
  int addr_size;

  //
  // Obtain the write target address based on the address size.
  //
  addr_size = i2c_dev->addr_size;
  switch (addr_size)
  {
  case 0:
    slave_addr |= I2C_DEV_NOADDR;
    break;
  case 1:
    break;
  case 2:
    slave_addr |= I2C_DEV_16BIT;
    break;
  }

  //
  // This is the length of the actual data.
  //
  uint16_t data_bytes = len;

  //
  // This is the data address on the i2c device.
  //
  uint16_t data_addr = 0;

  //
  // This marks the starting address of the actual data.
  //
  char* client_va;

  //
  // The first write segment contains the data address in the first 0, 1 or 2
  // bytes.
  //
  if (data_offset == 0)
  {
    if (len < addr_size)
      return (HV_EINVAL);

    if (drv_copy_from_client((char*)&data_addr, va, addr_size, flags))
      return (HV_EFAULT);

    data_bytes -= addr_size;

    //
    // Device byte address is in big-endian format.
    //
    if (addr_size == 2)
      data_addr = ((data_addr & 0xff) << 8) | (data_addr >> 8);

    i2c_dev->dev_addr = data_addr;

    client_va = va + addr_size;
  }
  else
  {
    data_addr = i2c_dev->dev_addr + data_offset - addr_size;
    client_va = va;
  }

  if (data_bytes)
  {
    //
    // This is not a dummy write.
    //

    char buf[HV_I2CM_CHUNK_SIZE];

    if (data_bytes > HV_I2CM_CHUNK_SIZE)
    {
      len -= data_bytes - HV_I2CM_CHUNK_SIZE;
      data_bytes = HV_I2CM_CHUNK_SIZE;
    }

    //
    // Now copy the data from the client buffer.
    //
    if (drv_copy_from_client(buf, client_va, data_bytes, flags))
      return (HV_EFAULT);

    //
    // Finally, handle the write.
    //
    i2c_switch_swing(is->instance, i2c_dev->switch_inst, i2c_dev->switch_chan);

    int ret = i2c_wrx(is->shim_pos, is->shim_chan, slave_addr, data_addr,
                      data_bytes, buf, i2c_dev->page_size,
                      i2c_dev->write_cycle);

    i2c_switch_release(is->instance, i2c_dev->switch_inst);

    if (ret <= 0)
      return (HV_EIO);
  }

  return (len);
}


/** I2C Master driver operations vector */
static struct drv_ops i2cm_ops = {
  .probe       = i2cm_probe,
  .init        = i2cm_init,
  .open        = i2cm_open,
  .close       = i2cm_close,
  .pread       = i2cm_pread,
  .pwrite      = i2cm_pwrite,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_i2cm = {
  .shim_type  = I2CM_DEV_INFO__TYPE_VAL_I2CM,
  .name       = "i2cm",
  .desc       = "I2C Master",
  .ops        = &i2cm_ops,
  .stilereq   = 1,
};


