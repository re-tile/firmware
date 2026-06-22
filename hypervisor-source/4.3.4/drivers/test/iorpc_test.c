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
 * Test driver for iorpc.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"
#include "drvintf.h"
#include "iorpc_test.h"

#include "test_rpc_dispatch.h"

/** The largest RPC buffer we're willing to put on the stack. */
#define MAX_STACK_BYTES 4096

/** Lock used to make sure that only one tile allocates shared state. */
static spinlock_t test_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state object. */
iorpc_test_state_t* test_state _SHARED = NULL;

/** A helper routine for validating the service domain bits in a
    client device file handle. */
static bool
is_open_svc_dom(unsigned int index, iorpc_test_state_t* ts)
{
  return ((index < MAX_SVC_DOM) &&
          !(ts->svc_dom_avail_mask & (1 << index)));
}

/** Test driver init routine. */
static int
iorpc_test_init(const char* drvname, void** statepp, int instance,
                   int tileno, pos_t tile, const struct dev_info* info,
                   const char* args)
{
  // Each tile grabs the allocation lock in turn.  The first one
  // allocates our state object, and the others just copy the address.
  iorpc_test_state_t* ts;
  spin_lock(&test_alloc_lock);
  ts = test_state;
  if (ts == NULL)
  {
    ts = drv_shared_state_zalloc(sizeof(*ts), 0);
    if (ts == NULL)
    {
      spin_unlock(&test_alloc_lock);
      return (HV_EFAULT);
    }
    test_state = ts;

    // Initialize the new object.
    spin_lock_init(&ts->lock);
    ts->svc_dom_avail_mask = (1 << MAX_SVC_DOM) - 1;    
  }
  spin_unlock(&test_alloc_lock);
  
  *statepp = ts;  
  
  return (0);
}


/** Test driver open routine - a new context number for each open. */
static int
iorpc_test_open(int devhdl, void* statep, const char* suffix,
                uint32_t flags, pos_t tile)
{
  iorpc_test_state_t* ts = statep;
  
  DEVICE_TRACE("iorpc_test_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  if (!strcmp(suffix, "/iorpc"))
  {
    spin_lock(&ts->lock);
    
    if (ts->svc_dom_avail_mask == 0)
    {
      spin_unlock(&ts->lock);
      return (GXIO_ERR_NO_SVC_DOM);
    }

    int svc_dom = __builtin_ctzll(ts->svc_dom_avail_mask);
    ts->svc_dom_avail_mask &= ~(1ULL << svc_dom);

    spin_unlock(&ts->lock);
    
    return svc_dom;
  }

  return (HV_ENODEV);
}

/** Test driver close routine. */
static int
iorpc_test_close(int devhdl, void* statep, pos_t tile)
{
  iorpc_test_state_t* ts = statep;
  unsigned int index = DRV_HDL2BITS(devhdl); 
  iorpc_svc_dom_t* dom = &ts->svc_doms[index];
  
  DEVICE_TRACE("iorpc_test_close: devhdl %#x\n", devhdl);

  spin_lock(&ts->lock);
  
  if (!is_open_svc_dom(index, ts))
  {
    spin_unlock(&ts->lock);
    return (GXIO_ERR_INVAL_SVC_DOM);
  }

  // Reset all service domain state.  Real drivers would need to reset
  // the hardware, shoot down MMIO mappings, etc.
  for (int i = 0; i < TEST_IORPC_NUM_REGS; i++)
    dom->regs.regs[i] = 0;
  for (int i = 0; i < MAX_BUFFERS; i++)
    dom->buffers[i].valid = 0;

  // Allow some future call to reuse the service domain.
  ts->svc_dom_avail_mask |= (1 << index);

  spin_unlock(&ts->lock);

  return (0);
}

/** Test driver read routine. */
static int
iorpc_test_pread(int devhdl, void* statep, uint32_t flags, char* va,
                 uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[MAX_STACK_BYTES];
  iorpc_test_state_t* ts = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("iorpc_test_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);
  
  spin_lock(&ts->lock);
  
  if (!is_open_svc_dom(index, ts))
  {
    result = GXIO_ERR_INVAL_SVC_DOM;
    goto end;
  }

  result = dispatch_gxio_test_read(offset, buf, len, ts, index);

  if (drv_copy_to_client(va, buf, len, flags))
  {
    result = HV_EFAULT;
    goto end;
  }

 end:
  spin_unlock(&ts->lock);
  return result;
}

/** Test driver write routine. */
static int
iorpc_test_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
                  uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[MAX_STACK_BYTES];
  iorpc_test_state_t* ts = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("iorpc_test_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);

  spin_lock(&ts->lock);
  
  if (!is_open_svc_dom(index, ts))
  {
    result = GXIO_ERR_INVAL_SVC_DOM;
    goto end;
  }
  
  if (drv_copy_from_client(buf, va, len, flags))
  {
    result = HV_EFAULT;
    goto end;
  }
  
  result = dispatch_gxio_test_write(offset, buf, len, ts, index);

 end:
  spin_unlock(&ts->lock);
  return result;
}

/** Handler for the write_reg() RPC call. */
int
handle_gxio_test_write_reg(iorpc_test_state_t* ts, int svc_dom,
                            unsigned int index, unsigned long value)
{
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  if (index < TEST_IORPC_NUM_REGS)
  {
    dom->regs.regs[index] = value;
    return 0;
  }
  else
    return GXIO_TEST_ERR_REG_NUMBER;
}

/** Handler for the read_reg() RPC call. */
int
handle_gxio_test_read_reg(iorpc_test_state_t* ts, int svc_dom,
                           unsigned int index, unsigned long *value)
{
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  if (index < TEST_IORPC_NUM_REGS)
  {
    *value = dom->regs.regs[index];
    return 0;
  }
  else
    return GXIO_TEST_ERR_REG_NUMBER;
}

/** Handler for the reset_regs() RPC call. */
int
handle_gxio_test_reset_regs(iorpc_test_state_t* ts, int svc_dom)
{
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  for (int i = 0; i < TEST_IORPC_NUM_REGS; i++)
    dom->regs.regs[i] = 0;

  return 0;
}

/** Handler for the read_all_regs() RPC call. */
int
handle_gxio_test_read_all_regs(iorpc_test_state_t* ts, int svc_dom,
                                struct test_iorpc_regs *regs_out)
{  
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  *regs_out = dom->regs;
  return 0;
}

/** Backend for all the buffer mapping RPCs - they differ in terms of
    the alignment checking done in the call stack, but by the time
    they reach this code they're all handled the same way. */
static int
iorpc_test_map_buffer(iorpc_test_state_t* ts, int svc_dom, PA pa,
                      size_t buf_size, struct iorpc_mem_attr attr,
                      unsigned int slot)
{
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  if (slot > MAX_BUFFERS)
    return GXIO_TEST_ERR_BUFFER_SLOT;
  else if (dom->buffers[slot].valid)
    return GXIO_ERR_ALREADY_INIT;
  else
  {
    dom->buffers[slot].valid = true;
    dom->buffers[slot].pa = pa;
    dom->buffers[slot].size = buf_size;
    dom->buffers[slot].attr = attr;
    
    return 0;
  }
}

/** Handler for the map_large_buffer() RPC call. */
int
handle_gxio_test_map_large_buffer(iorpc_test_state_t* ts, int svc_dom,
                                   PA pa, size_t buf_size,
                                   struct iorpc_mem_attr attr,
                                   unsigned int slot)
{
  return iorpc_test_map_buffer(ts, svc_dom, pa, buf_size, attr, slot);
}

/** Handler for the map_small_buffer() RPC call. */
int
handle_gxio_test_map_small_buffer(iorpc_test_state_t* ts, int svc_dom,
                                   PA pa, size_t buf_size,
                                   struct iorpc_mem_attr attr,
                                   unsigned int slot)
{
  return iorpc_test_map_buffer(ts, svc_dom, pa, buf_size, attr, slot);
}

/** Handler for the map_self_size_buffer() RPC call. */
int
handle_gxio_test_map_self_size_buffer(iorpc_test_state_t* ts, int svc_dom,
                                       PA pa, size_t buf_size,
                                       struct iorpc_mem_attr attr,
                                       unsigned int slot)
{
  return iorpc_test_map_buffer(ts, svc_dom, pa, buf_size, attr, slot);
}

/** Handler for the map_buffer() RPC call. */
int
handle_gxio_test_map_buffer(iorpc_test_state_t* ts, int svc_dom,
                             PA pa, size_t buf_size, struct iorpc_mem_attr attr,
                             unsigned int slot)
{
  return iorpc_test_map_buffer(ts, svc_dom, pa, buf_size, attr, slot);
}

/** Handler for the read_buffer_params() RPC call. */
int
handle_gxio_test_read_buffer_params(iorpc_test_state_t* ts, int svc_dom,
                                     unsigned int offset, uint64_t *pa,
                                     size_t *size, struct iorpc_mem_attr *attr)
{
  iorpc_svc_dom_t* dom = &ts->svc_doms[svc_dom];
  
  if (offset > MAX_BUFFERS ||
      !dom->buffers[offset].valid)
    return GXIO_TEST_ERR_BUFFER_SLOT;

  *pa = dom->buffers[offset].pa;
  *size = dom->buffers[offset].size;
  *attr = dom->buffers[offset].attr;

  return 0;
}

/** Handler for the write_data_array() RPC call. */
int
handle_gxio_test_write_data_array(iorpc_test_state_t* ts, int svc_dom,
                                   struct test_data_array data)
{
  ts->data_array = data;
  return 0;
}

/** Handler for the read_data_array() RPC call. */
int
handle_gxio_test_read_data_array(iorpc_test_state_t* ts, int svc_dom,
                                  struct test_data_array *data_out)
{
  *data_out = ts->data_array;
  return 0;
}

/** Handler for the write_data_array_segment() RPC call. */
int handle_gxio_test_write_data_array_segment(iorpc_test_state_t* ts,
                                               int svc_dom, void* data,
                                               size_t data_size)
{
  if (data_size > sizeof(ts->data_array.data))
    data_size = sizeof(ts->data_array.data);

  memcpy(ts->data_array.data, data, data_size);
  return 0;
}

/** Handler for the write_data_array_segment_ext() RPC call. */
int handle_gxio_test_write_data_array_segment_ext(iorpc_test_state_t* ts,
                                                   int svc_dom,
                                                   unsigned int offset,
                                                   void* data,
                                                   size_t data_size)
{
  if (offset > sizeof(ts->data_array.data))
    return GXIO_ERR_INVAL;
  
  size_t lim = offset + data_size;
  if (lim < offset ||
      lim > sizeof(ts->data_array.data))
    lim = sizeof(ts->data_array.data);

  memcpy(ts->data_array.data + offset, data, lim - offset);
  return 0;
}

/** Handler for the read_data_array_segment() RPC call. */
int handle_gxio_test_read_data_array_segment(iorpc_test_state_t* ts,
                                              int svc_dom, void* data,
                                              size_t data_size)
{
  if (data_size > sizeof(ts->data_array.data))
    data_size = sizeof(ts->data_array.data);

  memcpy(data, ts->data_array.data, data_size);
  return 0;
}

/** Handler for the read_data_array_segment_ext() RPC call. */
int handle_gxio_test_read_data_array_segment_ext(iorpc_test_state_t* ts,
                                                  int svc_dom,
                                                  unsigned int offset,
                                                  size_t *actually_read,
                                                  void* data,
                                                  size_t data_size)
{
  if (offset > sizeof(ts->data_array.data))
    return GXIO_ERR_INVAL;
  
  size_t lim = offset + data_size;
  if (lim < offset ||
      lim > sizeof(ts->data_array.data))
    lim = sizeof(ts->data_array.data);

  memcpy(data, ts->data_array.data + offset, lim - offset);
  *actually_read = lim - offset;
  
  return 0;
}


/** iorpc test driver operations vector */
static struct drv_ops iorpc_test_ops = {
  .init        = iorpc_test_init,
  .open        = iorpc_test_open,
  .close       = iorpc_test_close,
  .pread       = iorpc_test_pread,
  .pwrite      = iorpc_test_pwrite,
  .poll        = no_poll,
  .poll_cancel = no_poll_cancel,
  .preada      = no_preada,
  .pwritea     = no_pwritea,
  .msg         = no_msg,
  .service     = no_service,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_test_shared = {
  .shim_type  = 0,
  .name       = "iorpc_test",
  .desc       = "IORPC Test Device Driver",
  .ops        = &iorpc_test_ops,
};
