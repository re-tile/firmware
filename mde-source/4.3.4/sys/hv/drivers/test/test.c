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
 * Test driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip.h>
#include <arch/idn.h>
#include <arch/ipi.h>

#include "config.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "test.h"
#include "tsb.h"


/** This points to a buffer used for shared memory testing. */
char* shared_buffer _SHARED;

/** Test driver init routine. */
static int
testdrv_init(const char* drvname, void** statepp, int instance, int tileno,
             pos_t tile, const struct dev_info* info, const char* args)
{
  if (instance >= TEST_MAX_INST)
    return (HV_ENODEV);

  //
  // Allocate our state
  //
  test_state_t* ts = drv_state_zalloc(sizeof (*ts), 0);

  if (!ts)
    return (HV_EFAULT);

  *statepp = ts;

  ts->instance = instance;
  ts->tileno = tileno;

  //
  // If we're the shared tile, get the shared buffer.  (We could do this
  // anywhere, as long as we did it only once, but doing it on the shared
  // tile is nice because it exercises the allocator from a tile
  // other than the chip's master boot tile.)
  //
  if (tileno < 0)
    shared_buffer = drv_shared_state_zalloc(TEST_SHAREBUF_SIZE, 0);

  //
  // If we're a shared tile or a dedicated tile, get the ctl buffer.
  //
  if (tileno != 0)
    ts->ctlbuf = drv_state_zalloc(TEST_CTLBUF_SIZE, 0);

  //
  // If we aren't a dedicated tile, get client-shared memory space.
  //
  if (tileno <= 0)
  {
    ts->tsd = drv_client_zalloc(sizeof (struct test_shared_data), 0, 1, 0,
                                &ts->ctsd);
    if (!ts->tsd)
      return (HV_EFAULT);
    ts->last_instant_intr = &ts->tsd->last_instant_intr;
    ts->last_delayed_intr = &ts->tsd->last_delayed_intr;
  }

  //
  // Save other useful data.
  //
  ts->my_pos = tile;
  ts->infop = info;
  if (info->num_stiles + info->num_dtiles != 0)
  {
    ts->fwd_tile = (info->num_stiles) ? info->stiles[0] : info->dtiles[0];
    ts->fwd = (ts->fwd_tile.word != tile.word);
  }

  //
  // If we're the device with the dedicated tiles, register our interrupts.
  //
  if (tileno <= 0 && !strcmp(drvname, "test_ded"))
  {
    if (drv_register_intr(testdrv_instant_intr, ts->last_instant_intr,
                          DRV_INTR_INSTANT,
                          info->intchan + TEST_INTCHAN_MSGINTR))
      return (HV_ERECIP);

    if (drv_register_intr(testdrv_delayed_intr, ts, DRV_INTR_DELAYED,
                          info->intchan + TEST_INTCHAN_MSGINTR))
      return (HV_ERECIP);

  }

  //
  // If we're the device with the shared tile, register our fastio handlers.
  //
  if (tileno <= 0 && !strcmp(drvname, "test_shared"))
  {
    ts->fastio_index = drv_alloc_fastio(1, 0);
    if (ts->fastio_index == ~0)
      return (HV_ERECIP);

    extern drv_fastio_func testdrv_fastio_a;
    drv_register_fastio(testdrv_fastio_a, &ts->last_fastio_a, ts->fastio_index);
  }

  //
  // If we had any arguments, we'd parse them here and save the state in *ts.
  //
  return (0);
}


/** Test driver open routine. */
static int
testdrv_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
             pos_t tile)
{
  DEVICE_TRACE("testdrv_open: devhdl %#x suffix \"%s\" flags %#x "
          "tile %#x\n", devhdl, suffix, flags, tile.word);

  // No actual preparation to do here, so we don't forward anything to our
  // remote tile.

  if (*suffix == '\0')
    return (0);

  if (!strcmp(suffix, "/ctl"))
    return (TEST_CTL_MASK);

  return (HV_ENODEV);
}


/** Test driver close routine. */
static int
testdrv_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("testdrv_close: devhdl %#x\n", devhdl);

  // Nothing to do on close.

  return (0);
}


/** Test driver read routine. */
static int
testdrv_pread(int devhdl, void* statep, uint32_t flags, char* va,
              uint32_t len, uint64_t offset, pos_t tile)
{
  test_state_t* ts = statep;

  DEVICE_TRACE("testdrv_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // We only do pread/pwrite on the control device.
  //
  if ((DRV_HDL2BITS(devhdl) & TEST_CTL_MASK) == 0)
    return (HV_ENOTSUP);

  //
  // Some special offsets get processed here rather than on the remote tile.
  //
  if (offset >= TEST_CTL_INSTINTR_OFF)
  {
    if (offset == TEST_CTL_INSTINTR_OFF)
    {
      uint32_t tmpintval = *ts->last_instant_intr >> DRV_CHAN_WIDTH;
      if (len > sizeof (tmpintval))
        len = sizeof (tmpintval);
      if (drv_copy_to_client(va, (char*) &tmpintval, len, flags))
        return (HV_EFAULT);
      *ts->last_instant_intr = 0;

      return (len);
    }
    else if (offset == TEST_CTL_DELAYINTR_OFF)
    {
      uint32_t tmpintval = *ts->last_delayed_intr >> DRV_CHAN_WIDTH;
      if (len > sizeof (tmpintval))
        len = sizeof (tmpintval);
      if (drv_copy_to_client(va, (char*) &tmpintval, len, flags))
        return (HV_EFAULT);
      *ts->last_delayed_intr = 0;

      return (len);
    }
    else if (offset == TEST_CTL_SHAREDATA_OFF)
    {
      unsigned long tmpval = ts->ctsd;
      if (len > sizeof (tmpval))
        len = sizeof (tmpval);
      if (drv_copy_to_client(va, (char*) &tmpval, len, flags))
        return (HV_EFAULT);

      return (len);
    }
    else if (offset == TEST_CTL_FASTIO_IDX_OFF)
    {
      uint32_t tmpval = ts->fastio_index;
      if (len > sizeof (tmpval))
        len = sizeof (tmpval);
      if (drv_copy_to_client(va, (char*) &tmpval, len, flags))
        return (HV_EFAULT);

      return (len);
    }
    else if (offset == TEST_CTL_FASTIO_A_OFF)
    {
      uint32_t tmpval = ts->last_fastio_a;
      if (len > sizeof (tmpval))
        len = sizeof (tmpval);
      if (drv_copy_to_client(va, (char*) &tmpval, len, flags))
        return (HV_EFAULT);

      return (len);
    }
    else if (offset == TEST_CTL_INTRNUM_OFF)
    {
      uint32_t tmpval = ts->intr_index;
      if (len > sizeof (tmpval))
        len = sizeof (tmpval);
      if (drv_copy_to_client(va, (char*) &tmpval, len, flags))
        return (HV_EFAULT);

      return (len);
    }
    else if (offset > TEST_CTL_RW_SHR_LOC_OFF &&
             offset < TEST_CTL_RW_SHR_LOC_OFF + TEST_SHAREBUF_SIZE)
    {
      offset -= TEST_CTL_RW_SHR_LOC_OFF;
      if (offset + len > TEST_SHAREBUF_SIZE)
        len = TEST_SHAREBUF_SIZE - offset;

      if (drv_copy_to_client(va, shared_buffer + offset, len, flags))
        return (HV_EFAULT);

      return (len);
    }
    else
      return (HV_EINVAL);
  }

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_pread_remote(devhdl, flags, va, len, offset, ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // We don't do anything special when the memory buffer is bad, but we need
  // to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  //
  // Right now we can read the small control buffer or the shared buffer.
  //
  if (ts->ctlbuf && offset < TEST_CTLBUF_SIZE &&
      offset + len <= TEST_CTLBUF_SIZE)
  {
    memcpy(va, ts->ctlbuf + offset, len);
    return (len);
  }
  else if (offset > TEST_CTL_RW_SHR_SHR_OFF &&
           offset < TEST_CTL_RW_SHR_SHR_OFF + TEST_SHAREBUF_SIZE)
  {
    offset -= TEST_CTL_RW_SHR_SHR_OFF;
    if (offset + len > TEST_SHAREBUF_SIZE)
      len = TEST_SHAREBUF_SIZE - offset;

    if (drv_copy_to_client(va, shared_buffer + offset, len, flags))
      return (HV_EFAULT);

    return (len);
  }
  else
    return (HV_EINVAL);
}


/** Test driver write routine. */
static int
testdrv_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  test_state_t* ts = statep;

  DEVICE_TRACE("testdrv_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  //
  // We only do pread/pwrite on the control device.
  //
  if ((DRV_HDL2BITS(devhdl) & TEST_CTL_MASK) == 0)
    return (HV_ENOTSUP);

  //
  // Handle special +1 region to test remote forwarding of changed data.
  //
  if (offset >= TEST_CTL_PLUSONE_OFF &&
      offset + len < TEST_CTL_PLUSONE_OFF + TEST_CTLBUF_SIZE)
  {
    char buf[TEST_CTLBUF_SIZE];

    if (drv_copy_from_client(buf, va, len, flags))
      return (HV_EFAULT);

    for (int i = 0; i < len; i++)
      buf[i]++;

    if (ts->fwd)
      return (drv_pwrite_remote(devhdl, flags | DRV_FLG_HVADDR, buf,
              len, offset - TEST_CTL_PLUSONE_OFF, ts->fwd_tile));
    else if (ts->ctlbuf)
    {
      memcpy(ts->ctlbuf + offset, buf, len);
      return (len);
    }
    else
      return (HV_EINVAL);
  }

  //
  // Most MMIO operations get executed on the local tile.
  //
  if (offset >= TEST_CTL_MMIO_P_OFF && offset <= TEST_CTL_MMIO_O_OFF &&
      len == sizeof (struct test_mmio_op))
  {
    struct test_mmio_op tmo;
    if (drv_copy_from_client((char*) &tmo, va, len, flags))
      return (HV_EFAULT);
    pos_t shimaddr = { .bits.x = tmo.shim_x, .bits.y = tmo.shim_y };
    switch (offset)
    {
      case TEST_CTL_MMIO_P_OFF:
        return (drv_permit_mmio_access(shimaddr, tmo.pa, tmo.len, my_client));

      case TEST_CTL_MMIO_D_OFF:
        return (drv_deny_mmio_access(shimaddr, tmo.pa, tmo.len, my_client));

      case TEST_CTL_MMIO_O_OFF:
        return (mmio_access_ok(shimaddr, tmo.pa, tmo.len));

      default:
        return (HV_EIO);
    }
  }

  //
  // Local shared buffer writes get executed on the local tile.
  //
  if (offset > TEST_CTL_RW_SHR_LOC_OFF &&
      offset < TEST_CTL_RW_SHR_LOC_OFF + TEST_SHAREBUF_SIZE)
 {
   offset -= TEST_CTL_RW_SHR_LOC_OFF;
   if (offset + len > TEST_SHAREBUF_SIZE)
     len = TEST_SHAREBUF_SIZE - offset;

   if (drv_copy_from_client(shared_buffer + offset, va, len, flags))
     return (HV_EFAULT);

   return (len);
 }

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_pwrite_remote(devhdl, flags, va, len, offset, ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  //
  // We don't do anything special when the memory buffer is bad, but we need
  // to return the right error code.
  //
  if (flags & DRV_FLG_FAULT)
    return (HV_EFAULT);

  //
  // One MMIO operation gets executed on the remote tile.
  //
  if (offset == TEST_CTL_MMIO_OR_OFF && len == sizeof (struct test_mmio_op))
  {
    struct test_mmio_op tmo;
    if (drv_copy_from_client((char*) &tmo, va, len, flags))
      return (HV_EFAULT);
    pos_t shimaddr = { .bits.x = tmo.shim_x, .bits.y = tmo.shim_y };
    return (mmio_access_ok(shimaddr, tmo.pa, tmo.len));
  }

  //
  // First handle the special offsets.
  //
  if (offset >= TEST_CTL_INSTINTR_OFF)
  {
    char* dstaddr;
    int dstlen;

    if (offset == TEST_CTL_INSTINTR_OFF)
    {
      dstaddr = (char*) &ts->cause_instant_intr;
      dstlen = 4;
      ts->dest_instant_intr = tile;
    }
    else if (offset == TEST_CTL_DELAYINTR_OFF)
    {
      dstaddr = (char*) &ts->cause_delayed_intr;
      dstlen = 4;
      ts->dest_delayed_intr = tile;
    }
    else
      return (HV_EINVAL);

    if (len > dstlen)
      len = dstlen;

    if (drv_copy_from_client(dstaddr, va, len, flags))
      return (HV_EFAULT);
    else
      return (len);
  }

  //
  // Now the things which write/read the control buffer.
  //
  if (offset >= TEST_CTL_MINUSONE_OFF &&
      offset + len <= TEST_CTL_MINUSONE_OFF + TEST_CTLBUF_SIZE)
  {
    //
    // Route the data to the second dedicated tile to be transformed, then
    // write the small control buffer with the result.
    //

    if (ts->tileno != 1)
      return (HV_EINVAL);

    int retval = 0;
    offset -= TEST_CTL_MINUSONE_OFF;

    while (len)
    {
      struct testdrv_msg msg =
      {
        .op = TESTDRV_MSG_MINUSONE,
      };
      char reply[TEST_DRVMSG_SIZE];

      int pass_len = len;
      if (pass_len > sizeof (msg.data))
        pass_len = sizeof (msg.data);

      memcpy(msg.data, va, pass_len);
      msg.len = pass_len;

      int pass_retval = drv_send_msg(devhdl, &msg, sizeof (msg), reply,
                                     sizeof (reply), NULL,
                                     ts->infop->dtiles[1]);

      if (pass_retval > 0)
      {
        memcpy(ts->ctlbuf + offset, reply, pass_retval);

        retval += pass_retval;
        len -= pass_retval;
        offset += pass_retval;
        va += pass_retval;
      }
      else if (pass_retval == 0)
        return (retval);
      else
        return (pass_retval);
    }

    return (retval);
  }
  else if (ts->ctlbuf && offset < TEST_CTLBUF_SIZE &&
           offset + len <= TEST_CTLBUF_SIZE)
  {
    //
    // Write the small control buffer locally.
    //
    if (drv_copy_from_client(ts->ctlbuf + offset, va, len, flags))
      return (HV_EFAULT);
    else
      return (len);
  }
  else if (offset > TEST_CTL_RW_SHR_SHR_OFF &&
           offset < TEST_CTL_RW_SHR_SHR_OFF + TEST_SHAREBUF_SIZE)
  {
    //
    // Write the shared buffer locally.
    //
    offset -= TEST_CTL_RW_SHR_SHR_OFF;
    if (offset + len > TEST_SHAREBUF_SIZE)
      len = TEST_SHAREBUF_SIZE - offset;

    if (drv_copy_from_client(shared_buffer + offset, va, len, flags))
      return (HV_EFAULT);

    return (len);
  }
  else
    return (HV_EINVAL);
}


/** Test driver poll routine. */
static int
testdrv_poll(int devhdl, void* statep, uint32_t events, uint32_t intarg,
             pos_t tile)
{
  test_state_t* ts = statep;

  //
  // We only do poll on the control device.  (A normal device would probably
  // never do it there, but since we're a testing device, we do.)
  //
  if ((DRV_HDL2BITS(devhdl) & TEST_CTL_MASK) == 0)
    return (HV_ENOTSUP);

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_poll_remote(devhdl, events, intarg, ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  // Just for testing, we only support one poller
  if (ts->pollstate)
    return (HV_EBUSY);

  ts->pollstate = events;
  ts->poller = tile;
  ts->poll_intarg = intarg;

  return (0);
}


/** Test driver poll_cancel routine. */
static int
testdrv_poll_cancel(int devhdl, void* statep, pos_t tile)
{
  test_state_t* ts = statep;

  //
  // We only do poll on the control device.  (A normal device would probably
  // never do it there, but since we're a testing device, we do.)
  //
  if ((DRV_HDL2BITS(devhdl) & TEST_CTL_MASK) == 0)
    return (HV_ENOTSUP);

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_poll_cancel_remote(devhdl, ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  // Just for testing, we only support one poller
  if (ts->pollstate && ts->poller.word == tile.word)
    ts->pollstate = 0;

  return (0);
}


/** Test driver async read routine. */
static int
testdrv_preada(int devhdl, void* statep, uint32_t flags, uint32_t sgl_len,
               HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg,
               pos_t tile)
{
  test_state_t* ts = statep;

  //
  // We don't do preada/pwritea on the control device.
  //
  if (DRV_HDL2BITS(devhdl) & TEST_CTL_MASK)
    return (HV_ENOTSUP);

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_preada_remote(devhdl, flags, sgl_len, sgl, offset, intarg,
                              ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  // TODO put some code here

  return (0);
}


/** Test driver async write routine. */
static int
testdrv_pwritea(int devhdl, void* statep, uint32_t flags, uint32_t sgl_len,
                HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg,
                pos_t tile)
{
  test_state_t* ts = statep;

  //
  // We don't do preada/pwritea on the control device.
  //
  if (DRV_HDL2BITS(devhdl) & TEST_CTL_MASK)
    return (HV_ENOTSUP);

  //
  // Forward on to the remote tile if needed.
  //
  if (ts->fwd)
    return (drv_pwritea_remote(devhdl, flags, sgl_len, sgl, offset, intarg,
                               ts->fwd_tile));

  //
  // If we get here, we must be the remote tile.
  //

  // TODO put some code here

  return (0);
}


/** Test driver service routine. */
static int __attribute__((__noreturn__))
testdrv_service(void* statep)
{
  test_state_t* ts = statep;

  DEVICE_TRACE("testdrv_service: instance %d, tileno %d\n", ts->instance,
               ts->tileno);

  uint32_t count;

  while (1)
  {
    // FIXME: this should turn into "drv_nap(); drv_yield()" as soon as
    // drv_nap() is implemented, at least in the case where we don't have
    // an outstanding poll request.

    drv_yield();

    //
    // If we have an outstanding poll request, handle it.
    //
    if (ts->pollstate & HV_DEVPOLL_WRITE)
    {
      if (!drv_deliver_intr(ts->poller, ts->poll_intarg, HV_DEVPOLL_WRITE))
      {
        // If we successfully delivered the interrupt, clear the polling state.
        ts->pollstate = 0;
      }
    }

    //
    // If interrupts have been requested, handle them.
    //

    if (ts->cause_instant_intr)
    {
      idn_send(ts->dest_instant_intr.word | 1 | (1 << 30));
      idn_send(ts->cause_instant_intr + ts->infop->intchan);
      ts->cause_instant_intr = 0;
    }


    if (ts->cause_delayed_intr)
    {

      IPI_REMOTE_TRIGGER_ADDR_t addr =
      {{
        .tile_y = ts->dest_delayed_intr.bits.y,
        .tile_x = ts->dest_delayed_intr.bits.x,
        .ipi = HV_PL,
        .event = ts->infop->intchan,
      }};









      cfg_wr(my_ipi_pos.word, 0, addr.word, 0);
      ts->cause_delayed_intr = 0;
    }

    count++;
  }
}


/** Test driver msg routine. */
static void
testdrv_msg(int devhdl, void* statep, drv_reply_msg_token_t token,
            void* msg, int msglen, pos_t tile)
{
  DEVICE_TRACE("testdrv_msg: devhdl %#x, msg %p, msglen %d\n", devhdl, msg,
               msglen);

  struct testdrv_msg* tmsgp = msg;

  int retval = HV_ENOTSUP;
  char replybuf[DRV_MAX_MSG_LEN];
  int replylen = 0;

  if (tmsgp->op == TESTDRV_MSG_MINUSONE)
  {
    char* src = tmsgp->data;
    char* dst = replybuf;

    for (int i = 0; i < tmsgp->len; i++)
      *dst++ = (*src++) - 1;

    replylen = tmsgp->len;
    retval = tmsgp->len;
  }

  drv_reply_msg(token, retval, replybuf, replylen, tile);
}


/** Test driver delayed interrupt routine. */
void
testdrv_delayed_intr(void* intarg, void* msg, int len)
{
  test_state_t* ts = (test_state_t*) intarg;

  *ts->last_delayed_intr = 1 << DRV_CHAN_WIDTH;
}



/** Test driver operations vector */
static struct drv_ops testdrv_ops = {
  .init        = testdrv_init,
  .open        = testdrv_open,
  .close       = testdrv_close,
  .pread       = testdrv_pread,
  .pwrite      = testdrv_pwrite,
  .poll        = testdrv_poll,
  .poll_cancel = testdrv_poll_cancel,
  .preada      = testdrv_preada,
  .pwritea     = testdrv_pwritea,
  .service     = testdrv_service,
  .msg         = testdrv_msg,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_test_shared = {
  .shim_type  = 0,
  .name       = "test_shared",
  .desc       = "Test Device Driver -- shared tile",
  .ops        = &testdrv_ops,
  .stilereq   = 1,
};

//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_test_ded = {
  .shim_type  = 0,
  .name       = "test_ded",
  .desc       = "Test Device Driver -- 2 dedicated tiles",
  .ops        = &testdrv_ops,
  .dtilereq   = 2,
  .maxdelint  = 1,
  .intchanreq = 4,
};
