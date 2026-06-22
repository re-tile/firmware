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
 * I2C Slave driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "fault.h"
#include "hv.h"
#include "hw_config.h"
#include "lock.h"
#include "mapping.h"
#include "types.h"

#include "i2cs.h"

#include "i2cs_rpc_dispatch.h"


#if 0
#define I2CS_DEBUG
#endif

#ifdef I2CS_DEBUG
/** Page-level debug tracing output. */
#define I2CS_TRACE tprintf
#else
/** No debug tracing output. */
#define I2CS_TRACE(...)
#endif


/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t i2cs_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state objects. */
static i2cs_state_t* i2cs_state _SHARED = 0;


/** Prepare to use the the I2C Slave. */
static void
i2cs_enable(pos_t pos, unsigned long chan)
{
  cfg_wr(pos.word, chan, I2CS_BASELINE_CTL,
         I2CS_BASELINE_CTL__ENABLE_MASK);

  //
  // Raise the drive strength, since the default is a bit low 
  // for some configurations.
  // 
  I2CS_ELECTRICAL_CONTROL_t iec =
  {
    .word = cfg_rd(pos.word, chan, I2CS_ELECTRICAL_CONTROL)
  };
  iec.elec_strength = 6;  // 12 mA
  cfg_wr(pos.word, chan, I2CS_ELECTRICAL_CONTROL, iec.word);
}


/** I2C Slave driver initialization routine. */
static int
i2cs_init(const char* drvname, void** statepp, int instance, int tileno,
          pos_t tile, const struct dev_info* info, const char* args)
{
  I2CS_TRACE("i2cs_init: drvname %s instance %d tileno %d tile %#x chan %d\n",
             drvname, instance, tileno, tile.word, (int)info->channel);

  if (instance != 0)
    return (HV_ENODEV);

  drv_spin_lock(&i2cs_alloc_lock);

  i2cs_state_t* is = i2cs_state;

  //
  // First core who calls i2cs_init allocates the shared state object.
  //
  if (is == NULL)
  {
    is = drv_shared_state_zalloc(sizeof(*is), 0);
    if (is == NULL)
    {
      drv_spin_unlock(&i2cs_alloc_lock);
      return (HV_ENOMEM);
    }
    i2cs_state = is;
    drv_spin_lock_init(&is->lock);
    is->shim_pos = info->idn_ports[0]; 
    is->chan = info->channel;
    is->is_mmio_mapped = 0;
    
    //
    // Enable the I2C Slave interface.
    //
    i2cs_enable(is->shim_pos, is->chan);
  }

  drv_spin_unlock(&i2cs_alloc_lock);

  *statepp = is;

  return (0);
}


/** I2C Slave driver open routine. */
static int
i2cs_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
          pos_t tile)
{
  i2cs_state_t* is = statep;

  I2CS_TRACE("i2cs_open: devhdl %#x flags %#x tile %#x\n", 
	     devhdl, flags, (int)tile.word);

  drv_spin_lock(&is->lock);

  //
  // Permit MMIO access by the first caller tile.
  //
  if (!is->is_mmio_mapped)
  { 
    is->is_mmio_mapped = 1;

    drv_spin_unlock(&is->lock);

    int err = drv_permit_mmio_access(is->shim_pos,
                                     HV_I2CS_MMIO_OFFSET,
                                     HV_I2CS_MMIO_SIZE,
                                     0);
    if (err)
    {
      drv_spin_lock(&is->lock);
      is->is_mmio_mapped = 0;
      drv_spin_unlock(&is->lock);
        
      I2CS_TRACE("Unexpected permit_mmio_access() failure at open\n");
      return (err);
    }

    return (I2CS_DEV_HANDLE);
  }
    
  drv_spin_unlock(&is->lock);

  return (I2CS_DEV_HANDLE);
}


/** I2C Slave driver close routine. */
static int
i2cs_close(int devhdl, void* statep, pos_t tile)
{
  i2cs_state_t* is = statep;

  I2CS_TRACE("i2cs_close: devhdl %#x\n", devhdl);

  //
  // Disable I2CS interrupts.
  //
  handle_gxio_i2cs_cfg_interrupt(is, 0, 0, -1, -1);

  drv_spin_lock(&is->lock);

  //
  // Ensure only one client (kernel) gets the MMIO mapping.
  //
  if (is->is_mmio_mapped)
  {
    is->is_mmio_mapped = 0;

    drv_spin_unlock(&is->lock);

    //
    // Deny MMIO access to the I2C Slave register space.
    //
    int err = drv_deny_mmio_access(is->shim_pos,
                                   HV_I2CS_MMIO_OFFSET,
                                   HV_I2CS_MMIO_SIZE,
                                   0);
    if (err)
    {
      I2CS_TRACE("Unexpected deny_mmio_access() failure at close\n");
      return (err);
    }

    return (0);
  }

  drv_spin_unlock(&is->lock);

  return (0);
}


/** I2C Slave driver close_all routine. */
static int
i2cs_close_all(int dev_idx, void* statep)
{
  int devhdl = MK_HDL(dev_idx, I2CS_DEV_HANDLE);

  I2CS_TRACE("i2cs_close_all: dev_idx %d\n", dev_idx);

  i2cs_close(devhdl, statep, my_pos);

  return (0);
}


/** I2C Slave driver read routine. */
static int
i2cs_pread(int devhdl, void* statep, uint32_t flags, char* va,
           uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[1024];
  i2cs_state_t* is = statep;
  int result;

  I2CS_TRACE("i2cs_pread: devhdl %#x flags %#x va %p len %d "
            "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
            tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);

  drv_spin_lock(&is->lock);

  result = dispatch_gxio_i2cs_read(offset, buf, len, is);

  drv_spin_unlock(&is->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    result = HV_EFAULT;
 
  return (result);
}


/** I2C Slave driver write routine. */
static int
i2cs_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[1024];
  i2cs_state_t* is = statep;
  int result;

  I2CS_TRACE("i2cs_pwrite: devhdl %#x flags %#x va %p len %d "
             "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
             tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);

  if (drv_copy_from_client(buf, va, len, flags))
    return (HV_EFAULT);

  drv_spin_lock(&is->lock);

  result = dispatch_gxio_i2cs_write(offset, buf, len, is);

  drv_spin_unlock(&is->lock);

  return (result);
}


/** I2C Slave driver operations vector */
static struct drv_ops i2cs_ops = {
  .init        = i2cs_init,
  .open        = i2cs_open,
  .close       = i2cs_close,
  .close_all   = i2cs_close_all,
  .pread       = i2cs_pread,
  .pwrite      = i2cs_pwrite
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_i2cs = {
  .shim_type  = I2CS_DEV_INFO__TYPE_VAL_I2CS,
  .name       = "i2cs",
  .desc       = "I2C Slave",
  .ops        = &i2cs_ops,
};


///////////////////////////////////////////////////////////////////
//                        Global Methods                         //
///////////////////////////////////////////////////////////////////


static int contained_by(unsigned long bound_offset,
		        unsigned long bound_size,
                        unsigned long input_offset,
                        unsigned long input_size)
{
  if (input_offset < bound_offset ||
      input_offset + input_size > bound_offset + bound_size ||
      input_offset + input_size < input_offset)
    return (0);

  return (1);
}


/** Configure shim interrupts. */
int 
handle_gxio_i2cs_cfg_interrupt(i2cs_state_t* is,
			       int inter_x, int inter_y,
			       int inter_ipi, int inter_event)
{
  //
  // Unmask interrupts for TFIFO_READ and RFIFO_WRITE.
  //
  I2CS_INT_VEC_MASK_t int_mask = 
  {
    .word = cfg_rd(is->shim_pos.word, is->chan, I2CS_INT_VEC_MASK)
  };

  int_mask.tfifo_read = 0;
  int_mask.rfifo_write = 0;

  cfg_wr(is->shim_pos.word, is->chan, I2CS_INT_VEC_MASK, int_mask.word);

  //
  // Bind the IPI interrupt.
  //
  RSH_INT_BIND_t binding_setup = 
  {{
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .dev_sel = RSH_INT_BIND__DEV_SEL_VAL_I2CS,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 0,
  }};

  if (inter_event < 0)
    binding_setup.enable = 0;
  else
    binding_setup.enable = 1;

  cfg_wr(is->shim_pos.word, RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_RSHIM, 
         RSH_INT_BIND, binding_setup.word);

  return (0);
}


/** Return the base PTE that the client should use to access our
    shim's MMIO registers. */
int
handle_gxio_i2cs_get_mmio_base(i2cs_state_t* is, HV_PTE *base)
{
  PA pa = 0;
  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(is->shim_pos.bits.x,
                                             is->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;
  return (0);
}


/** Check whether an MMIO range is legal. */
int
handle_gxio_i2cs_check_mmio_offset(i2cs_state_t* is,
                                   unsigned long offset, unsigned long size)
{
  if (contained_by(HV_I2CS_MMIO_OFFSET, HV_I2CS_MMIO_SIZE,
		   offset, size))
    return (0);
    
  I2CS_TRACE("check_mmio_offset() failed\n");
  return (GXIO_ERR_MMIO_ADDRESS);
}
