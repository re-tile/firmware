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
 * Tile-monitor FIFO driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/ipi.h>
#include <arch/rsh.h>
#include <arch/sim.h>

#include "sys/libc/include/util.h"

#include "cfg.h"
#include "console.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "lock.h"
#include "tmfifo.h"
#include "types.h"

// #define TMFIFO_DEBUG
// #define TMFIFO_DEBUG_UART

#if defined(TMFIFO_DEBUG) || defined(TMFIFO_DEBUG_UART)
#ifdef TMFIFO_DEBUG_UART
/** Debug trace output. */
#define TRACE(...) fprintf(&uart_out_onlcr, __VA_ARGS__)
#else
/** Debug trace output. */
#define TRACE(...) tprintf(__VA_ARGS__)
#endif
#else
/** Debug trace output. */
#define TRACE(...) do { } while(0)
#endif


/** Lock used to make sure that only one tile allocates shared state. */
static spinlock_t tmfifo_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state object. */
tmfifo_state_t* tmfifo_state _SHARED = 0;

/** Handle tile-monitor FIFO output.
 * @param ts Driver state pointer. */
static void
tmfifo_outproc(tmfifo_state_t* ts)
{
  long t2h_avail = ts->t2h_size - cfg_rd(ts->rshim, 0,
                                         RSH_TM_TILE_TO_HOST_STS);

  TRACE("outproc, %ld words avail in t2h FIFO\n", t2h_avail);

  //
  // If we've gotten an INIT0 message, respond to it.
  //
  if (t2h_avail && ts->proto_init0)
  {
    ts->proto_out = min(ts->proto_init0, TMFIFO_PROTO_VERS);
    ts->proto_init0 = 0;
    uint_reg_t word = (TMFIFO_CTL_INIT1 << 16) | (ts->proto_out << 24);
    cfg_wr(ts->rshim, 0, RSH_TM_TILE_TO_HOST_DATA, word);
    t2h_avail--;
    TRACE("sent init1, proto_out now %d\n", ts->proto_out);
  }

  //
  // We can't send data on non-zero channels until our output protocol
  // version is at least 1.
  //
  int numchan = (ts->proto_out > 0) ? TM_CHANNELS : 1;
  int data_left = 0;

  for (int chan_offset = 0; chan_offset < numchan; chan_offset++)
  {
    int chan_idx = ((ts->first_outproc_chan) + chan_offset) % numchan;
    tmfifo_chan_t* chan = &ts->t2h_chans[chan_idx];

    long len = CHAN_BYTES(chan);

    if (!len)
      continue;

    //
    // We send an interrupt to the client if we drain data from a full
    // channel.
    //
    int do_intr = CHAN_FULL(chan);

    TRACE("%ld bytes in chan %d\n", len, chan_idx);

    //
    // Unlike previous versions of this driver, we never send packets larger
    // than the tmfifo's available space, since we don't know what other
    // channels we might want to service after we're done with this one.
    //
    // Note the casting of the output of sizeof to ssize_t; without this, the
    // first argument to min ends up being unsigned, which totally hoses
    // things, since in the full-FIFO case you expect it to be negative.
    //
    int bytes2xfer = min(t2h_avail * (ssize_t) sizeof (uint64_t) -
                         TMFIFO_PKT_HDR_LEN, len);

    data_left = (bytes2xfer != len);

    uint_reg_t word = bytes2xfer | (chan_idx << 12);  // XXX symbols
    int word_next_bit = 8 * TMFIFO_PKT_HDR_LEN;

    while (bytes2xfer > 0)
    {
      word |= (uint_reg_t) (unsigned char) chan->c[chan->head] << word_next_bit;
      word_next_bit += 8;
      chan->head = CHAN_NEXT_HEAD(chan, 1);
      bytes2xfer--;

      if (word_next_bit >= 64 || !bytes2xfer)
      {
        cfg_wr(ts->rshim, 0, RSH_TM_TILE_TO_HOST_DATA, word);
        t2h_avail--;
        word = 0;
        word_next_bit = 0;
      }
    }

    //
    // XXX We could check here to see if we got more space since we checked
    // up front, and if so, loop back and send more data.  However, we don't
    // necessarily want to send smaller and smaller packets to fill the FIFO,
    // since that wastes more space for headers; at some point we're better
    // off just waiting for it to drain more.  We should investigate this at
    // some point, and may also tune the low-water mark so that we get
    // interrupted at the right time; right now it's 80% of full.
    //

    if (do_intr && chan->ipi_addr)
    {
      TRACE("sending t2h intr\n");
      cfg_wr(my_ipi_pos.word, 0, chan->ipi_addr, 0);
    }

    // Send an IPI to the client if the console IPI flag was set.
    if (chan_idx == TM_CONS_CHAN && do_intr &&
        console_ipi_addr && console_ipi_pending)
    {
      CLR_PENDING_IPI_ALL();
      cfg_wr(my_ipi_pos.word, 0, console_ipi_addr, 0);
    }

    //
    // Note that we intentionally don't include this test in the for loop
    // condition.  That would mean we wouldn't run the loop at all when
    // we have no space, which seems efficient, but it would also mean that
    // we would never set data_left when we did have pending data, and thus
    // would never enable the LWM interrupt.
    //
    if (!t2h_avail)
      break;
  }

  //
  // Bump the first channel looked at so that we round-robin over them.
  //
  ts->first_outproc_chan = (ts->first_outproc_chan + 1) % numchan;

  //
  // We enable the interrupt iff we couldn't send all of the data we have,
  // or if we have control messages to send.
  //
  if (data_left || ts->proto_init0)
  {
    TRACE("enabling lwm intr\n");
    cfg_wr(ts->rshim, 0, RSH_INT_VEC0_W1TC, RSH_INT_VEC0_W1TC__TM_TTH_LWM_MASK);
  }

  TRACE("outproc done\n");
}


/** Handle tile-monitor FIFO input.
 * @param ts Driver state pointer.
 * @return Nonzero if outproc() should run. */
static int
tmfifo_drain(tmfifo_state_t* ts)
{
  int run_outproc = 0;
  tmfifo_chan_t* chan = &ts->h2t_chans[ts->pkt_chan_idx];

  int h2t_avail = cfg_rd(ts->rshim, 0, RSH_TM_HOST_TO_TILE_STS);

  TRACE("draining, %d words in h2t FIFO\n", h2t_avail);

  int do_intr = 0;
  int done = 0;
  int consumed_data = 0;

  while (!done)
  {
    if (ts->last_word_rem_bytes == 0)
    {
      if (h2t_avail == 0)
      {
        //
        // More data may well have shown up since we checked; let's see.
        //
        h2t_avail = cfg_rd(ts->rshim, 0, RSH_TM_HOST_TO_TILE_STS);
        if (h2t_avail == 0)
          break;
      }

      ts->last_fifo_word = cfg_rd(ts->rshim, 0, RSH_TM_HOST_TO_TILE_DATA);
      h2t_avail--;
      ts->last_word_rem_bytes = 8;
      consumed_data = 1;
    }

    if (ts->pkt_rem_bytes == 0)
    {
      //
      // XXX For now, we force packets to start on a word boundary, since
      // that's what the old protocol did.  We may want to change this in
      // the future, if we know our partner is a new one, as long as we
      // can be sure we don't have any synchronization issues.
      //
      if (ts->last_word_rem_bytes != 8)
      {
        ts->last_word_rem_bytes = 0;
        continue;
      }

      ts->pkt_rem_bytes = ts->last_fifo_word & 0xFFF; // XXX symbol

      if (ts->pkt_rem_bytes == 0)
      {
        TRACE("got control packet, val 0x%llx\n", ts->last_fifo_word);

        int ctl_type = (ts->last_fifo_word >> 16) & 0xFF; // XXX symbol

        switch (ctl_type)
        {
        case TMFIFO_CTL_INIT0:
          ts->proto_init0 = (ts->last_fifo_word >> 24) & 0xFF;
          run_outproc = 1;
          TRACE("got init0, proto was %d\n", ts->proto_init0);
          break;

        case TMFIFO_CTL_INIT2:
          ts->proto_in = (ts->last_fifo_word >> 24) & 0xFF;
          if (ts->proto_in > TMFIFO_PROTO_VERS)
            panic("tmfifo: bad protocol %d in INIT2, we support %d\n",
                  ts->proto_in, TMFIFO_PROTO_VERS);
          TRACE("got init2, proto_in now %d\n", ts->proto_in);
          //
          // XXX set up to send credit here
          //
          break;

        default:
          panic("tmfifo: unrecognized control packet 0x%llx\n",
                ts->last_fifo_word);
        }

        ts->last_word_rem_bytes = 0;
        continue;
      }
      else
      {
        ts->pkt_chan_idx = (ts->last_fifo_word >> 12) & 3;  // XXX symbols
        if (ts->pkt_chan_idx >= TM_CHANNELS)
        {
          tprintf("hv_warning: tmfifo: bad channel %d\n", ts->pkt_chan_idx);
          ts->pkt_chan_idx = 0;
        }
        chan = &ts->h2t_chans[ts->pkt_chan_idx];
        TRACE("got packet, chan %d, %d bytes\n", ts->pkt_chan_idx,
              ts->pkt_rem_bytes);
        ts->last_fifo_word >>= 16;
        ts->last_word_rem_bytes -= 2;
      }
    }

    while (ts->last_word_rem_bytes != 0 && ts->pkt_rem_bytes != 0)
    {
      int next_tail = CHAN_NEXT_TAIL(chan, 1);
      if (next_tail == chan->head)
      {
        done = 1;
        break;
      }
      chan->c[chan->tail] = (char) ts->last_fifo_word;
      chan->tail = next_tail;
      ts->last_word_rem_bytes--;
      ts->last_fifo_word >>= 8;
      ts->pkt_rem_bytes--;
      //
      // XXX Right now, we interrupt if any new data showed up.  A
      // potentially more efficient approach might be to only do this when
      // we finish a packet, or when we get enough data to fill the last
      // read request that was made.
      //
      do_intr = 1;
    }
  }

  //
  // XXX Note that we don't really need to do this if we've already sent an
  // interrupt and they haven't read anything since.  We might consider
  // some sort of 'interrupt armed' value.
  //
  if (do_intr && chan->ipi_addr)
  {
    TRACE("sending h2t intr\n");
    cfg_wr(my_ipi_pos.word, 0, chan->ipi_addr, 0);
  }

  if (ts->pkt_chan_idx == TM_CONS_CHAN && do_intr && console_ipi_addr)
    SET_PENDING_IPI_INPUT();

  // theory if that if we drained nothing, that means that we're stuck on
  // some full channel, and there's no point in trying again until we drain
  // some data, and when that happens the interrupt will be reenabled.
  if (consumed_data)
  {
    TRACE("enabling hwm intr\n");
    cfg_wr(ts->rshim, 0, RSH_INT_VEC0_W1TC, RSH_INT_VEC0_W1TC__TM_HTT_HWM_MASK);
  }

  TRACE("drain done, %d left in pkt on chan %d\n", ts->pkt_rem_bytes,
        ts->pkt_chan_idx);

  return run_outproc;
}


/** Read bytes from a channel.
 * @param ts Driver state pointer.
 * @param chan_idx Channel to read.
 * @param flags Flags from the pread entry point.
 * @param va Client virtual address.
 * @param len Number of bytes to read.
 * @return Combined bytes read/readable status from TMFIFO_MAKE_RETVAL().
 */
static int
tmfifo_rdchan(tmfifo_state_t* ts, int chan_idx, uint32_t flags,
              char* va, uint32_t len)
{
  tmfifo_chan_t* chan = &ts->h2t_chans[chan_idx];

  int chan_was_full = CHAN_FULL(chan);
  int rd_cnt = 0;

  while (len && chan->head != chan->tail)
  {
    int bytes2xfer = CHAN_CONTIG_BYTES(chan);
    bytes2xfer = min(bytes2xfer, len);

    if (drv_copy_to_client(va, &chan->c[chan->head], bytes2xfer, flags))
    {
      if (rd_cnt)
        break;
      return HV_EFAULT;
    }

    chan->head = CHAN_NEXT_HEAD(chan, bytes2xfer);
    len -= bytes2xfer;
    va += bytes2xfer;
    rd_cnt += bytes2xfer;
  }

  //
  // If there's data for this channel in the partially read word, then we
  // need to process it now.  Otherwise, if that happens to be the end of
  // the last packet the host has sent, we won't ever deliver it to the
  // client.  (We aren't going to get an interrupt for it, since we've
  // already pulled it out of the hardware, and we might not tell the
  // client to come back for more data, because we may have just totally
  // drained the channel FIFO.)
  //
  if (ts->last_word_rem_bytes && ts->pkt_rem_bytes &&
      ts->pkt_chan_idx == chan_idx)
  {
    TRACE("draining partial word for read channel %d\n", chan_idx);

    if (tmfifo_drain(ts))
      tmfifo_outproc(ts);
  }

  int could_read_more = !CHAN_EMPTY(chan);

  TRACE("read chan %d: %d bytes, residue %d, could read more %d\n", chan_idx,
        rd_cnt, len, could_read_more);

  //
  // If we drained a full channel and we're currently blocked on it, we enable
  // interrupts.
  //
  if (chan_was_full && ts->pkt_chan_idx == chan_idx && ts->pkt_rem_bytes)
  {
    TRACE("read from full chan, enabling hwm intr\n");
    cfg_wr(ts->rshim, 0, RSH_INT_VEC0_W1TC, RSH_INT_VEC0_W1TC__TM_HTT_HWM_MASK);
  }

  return TMFIFO_MAKE_RETVAL(rd_cnt, could_read_more);
}


/** Write bytes to a channel.
 * @param ts Driver state pointer.
 * @param chan_idx Channel to write.
 * @param flags Flags from the pwrite entry point.
 * @param va Client virtual address.
 * @param len Number of bytes to write.
 * @param force_outproc If nonzero, always run the output processor.
 * @return Combined bytes written/writable status from TMFIFO_MAKE_RETVAL().
 */
static int
tmfifo_wrchan(tmfifo_state_t* ts, int chan_idx, uint32_t flags,
              char* va, uint32_t len, int force_outproc)
{
  TRACE("write chan %d: %d bytes requested\n", chan_idx, len);

  tmfifo_chan_t* chan = &ts->t2h_chans[chan_idx];

  int chan_was_empty = (chan->head == chan->tail);
  int wr_cnt = 0;

  while (len && !CHAN_FULL(chan))
  {
    int bytes2xfer = CHAN_CONTIG_SPACE(chan);
    bytes2xfer = min(bytes2xfer, len);

    if (drv_copy_from_client(&chan->c[chan->tail], va, bytes2xfer, flags))
    {
      if (wr_cnt)
        break;
      return HV_EFAULT;
    }

    chan->tail = CHAN_NEXT_TAIL(chan, bytes2xfer);

    len -= bytes2xfer;
    va += bytes2xfer;
    wr_cnt += bytes2xfer;
  }

  //
  // Note that we're getting the full state before we call outproc.  If we
  // checked after it did so, we might report that the channel was writable
  // even though we did not write all of the data requested, which seems
  // like it would be confusing.  Note that outproc will interrupt the
  // client if it removes data from a full channel, so the client will
  // eventually get the right idea about the channel's writability.
  //
  int could_write_more = !CHAN_FULL(chan);

  //
  // We call the output processor in two cases:
  //
  // - We added data to an empty channel.
  //
  // - Our caller asked us to run it.  This happens when we're called for
  //   console output, and prevents a deadlock which might otherwise occur
  //   when this tile, or the tile doing the console output, is the tile
  //   which handles the tmfifo interrupts.  (What happens is that the
  //   hardware FIFO fills, then the write FIFO for the relevant channel
  //   fills; once this happens, cons_tmfifo_write() just keeps on calling
  //   us, preventing the interrupt routine from running, but we wouldn't
  //   ever call outproc since we aren't adding anything to an empty
  //   channel).
  //
  // XXX Should we call outproc only this once, or do a loop?  If we did
  // a loop we might want to change the could_write_more check to be done
  // afterwards, and the interrupt from outproc to only happen when it
  // ran due to the low-water-mark interrupt.
  //
  if ((chan_was_empty && wr_cnt) || force_outproc)
    tmfifo_outproc(ts);

  TRACE("write chan %d: %d bytes written, could write more %d\n", chan_idx,
        wr_cnt, could_write_more);

  return TMFIFO_MAKE_RETVAL(wr_cnt, could_write_more);
}


/** Tile-monitor FIFO interrupt routine. */
static void
tmfifo_intr(void* intarg, void* msg, int len)
{
  tmfifo_state_t* ts = intarg;

  uint_reg_t intrs = cfg_rd(ts->rshim, 0, RSH_INT_VEC0_W1TC);

  spin_lock(&ts->lock);

  int run_outproc = 0;
  if (intrs & RSH_INT_VEC0_W1TC__TM_HTT_HWM_MASK)
    run_outproc = tmfifo_drain(ts);

  if ((intrs & RSH_INT_VEC0_W1TC__TM_TTH_LWM_MASK) || run_outproc)
    tmfifo_outproc(ts);

  // Send an IPI to the client if the console IPI flag was set.
  if (ts->pkt_chan_idx == TM_CONS_CHAN &&
      console_ipi_addr && console_ipi_pending)
  {
    CLR_PENDING_IPI_ALL();
    cfg_wr(my_ipi_pos.word, 0, console_ipi_addr, 0);
  }

  spin_unlock(&ts->lock);
}


/** Tile-monitor FIFO driver init routine. */
static int
tmfifo_init(const char* drvname, void** statepp, int instance, int tileno,
            pos_t tile, const struct dev_info* info, const char* args)
{
  if (instance > 0)
  {
    tprintf("hv_warning: failed to init driver %s, max instances "
            "exceeded\n", drvname);
    return (HV_ENODEV);
  }

  tmfifo_state_t* ts;
  spin_lock(&tmfifo_alloc_lock);
  ts = tmfifo_state;
  if (ts == NULL)
  {
    ts = drv_shared_state_zalloc(sizeof(*ts), 0);
    if (ts == NULL)
    {
      spin_unlock(&tmfifo_alloc_lock);
      return (HV_EFAULT);
    }
    tmfifo_state = ts;
    //
    // Here we do our initialization which only needs to happen once.
    //

    // Save rshim address.
    ts->rshim = rshims[0]->idn_ports[0].word;

    // Save interrupt channel number.
    ts->intchan = info->intchan;

    //
    // Take the rshim packet generator out of boot mode.
    //
    cfg_wr(ts->rshim, 0, RSH_PG_CTL, 0);

    // Get the write FIFO size.  The read side has a size, too, but we
    // don't need to get it since we always just read what's available.
    RSH_TM_TILE_TO_HOST_CTL_t t2hc;
    t2hc.word = cfg_rd(ts->rshim, 0, RSH_TM_TILE_TO_HOST_CTL);
    ts->t2h_size = t2hc.max_entries;
  }
  spin_unlock(&tmfifo_alloc_lock);

  if (tileno < 0 && !sim_is_simulator())
  {
    //
    // Register our interrupt handler.
    //
    if (drv_register_intr(tmfifo_intr, ts, DRV_INTR_DELAYED, ts->intchan))
      return HV_ERECIP;

    //
    // Point the hardware interrupts at this tile.
    //
    RSH_INT_BIND_t rib =
    {{
      .enable = 1,
      .mode = 0,
      .tileid = DRV_COORDS_TO_TILE_ID(my_pos.bits.x, my_pos.bits.y),
      .dev_sel = RSH_INT_BIND__DEV_SEL_VAL_CH0,
      .int_num = HV_PL,
      .evt_num = ts->intchan,
    }};

    rib.bind_sel = RSH_INT_BIND__BIND_SEL_VAL_TM_HTT_HWM;
    cfg_wr(ts->rshim, 0, RSH_INT_BIND, rib.word);

    rib.bind_sel = RSH_INT_BIND__BIND_SEL_VAL_TM_TTH_LWM;
    cfg_wr(ts->rshim, 0, RSH_INT_BIND, rib.word);

    //
    // Set host-to-tile high-water mark.
    //
    RSH_TM_HOST_TO_TILE_CTL_t h2tc;
    h2tc.word = cfg_rd(ts->rshim, 0, RSH_TM_HOST_TO_TILE_CTL);
    h2tc.hwm = 1;
    cfg_wr(ts->rshim, 0, RSH_TM_HOST_TO_TILE_CTL, h2tc.word);

    //
    // Set tile-to-host low-water mark.
    //
    RSH_TM_TILE_TO_HOST_CTL_t t2hc;
    t2hc.word = cfg_rd(ts->rshim, 0, RSH_TM_TILE_TO_HOST_CTL);
    t2hc.lwm = (ts->t2h_size * 8) / 10;
    cfg_wr(ts->rshim, 0, RSH_TM_TILE_TO_HOST_CTL, t2hc.word);
  }

  //
  // Here's our initialization which needs to happen on every tile.
  //

  *statepp = ts;

  return 0;
}


/** Tile-monitor FIFO driver open routine. */
static int
tmfifo_open(int devhdl, void* statep, const char* suffix,
            uint32_t flags, pos_t tile)
{
  DEVICE_TRACE("tmfifo_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  long channel;
  char* endptr;

  if (suffix[0] == '/' && !str2l(&suffix[1], &endptr, 10, &channel) &&
      *endptr == '\0' && channel >= 0 && channel < TM_CHANNELS &&
      channel != TM_CONS_CHAN)
    return channel;

  return (HV_ENODEV);
}


/** Tile-monitor FIFO driver read routine. */
static int
tmfifo_pread(int devhdl, void* statep, uint32_t flags, char* va,
             uint32_t len, uint64_t offset, pos_t tile)
{
  DEVICE_TRACE("tmfifo_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  tmfifo_state_t* ts = statep;
  int chan_idx = DRV_HDL2BITS(devhdl);

  if (chan_idx < 0 || chan_idx >= TM_CHANNELS || chan_idx == TM_CONS_CHAN)
    return HV_ENODEV;

  spin_lock(&ts->lock);

  int rv = tmfifo_rdchan(ts, chan_idx, flags, va, len);

  spin_unlock(&ts->lock);

  return rv;
}


/** Tile-monitor FIFO driver write routine. */
static int
tmfifo_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
              uint32_t len, uint64_t offset, pos_t tile)
{
  DEVICE_TRACE("tmfifo_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  tmfifo_state_t* ts = statep;
  int chan_idx = DRV_HDL2BITS(devhdl);

  if (chan_idx < 0 || chan_idx >= TM_CHANNELS || chan_idx == TM_CONS_CHAN)
    return HV_ENODEV;

  if (offset != TMFIFO_CONFIGURE_INTR)
  {
    spin_lock(&ts->lock);

    int rv = tmfifo_wrchan(ts, chan_idx, flags, va, len, 0);

    spin_unlock(&ts->lock);

    return rv;
  }
  else
  {
    struct tmfifo_intr_config tic;
    Lotar real_readable_lotar;
    Lotar real_writable_lotar;

    if (len != sizeof (tic))
      return HV_EINVAL;

    if (drv_copy_from_client((char*) &tic, va, len, flags))
      return HV_EFAULT;

    if ((tic.readable_event >= 0 && drv_c2r_lotar(tic.readable_lotar,
                                                  &real_readable_lotar)) ||
        (tic.writable_event >= 0 && drv_c2r_lotar(tic.writable_lotar,
                                                  &real_writable_lotar)))
      return HV_EINVAL;

    if (tic.readable_event > 31 || tic.writable_event > 31)
      return HV_EINVAL;

    spin_lock(&ts->lock);

    tmfifo_chan_t* h2t_chan = &ts->h2t_chans[chan_idx];
    tmfifo_chan_t* t2h_chan = &ts->t2h_chans[chan_idx];

    //
    // Point the interrupts at the destination tile.
    //
    if (tic.readable_event >= 0)
    {








      IPI_REMOTE_TRIGGER_ADDR_t addr = {{
          .tile_y = HV_LOTAR_Y(real_readable_lotar),
          .tile_x = HV_LOTAR_X(real_readable_lotar),
          .ipi = CLIENT_PL,
          .event = tic.readable_event,
      }};


      h2t_chan->ipi_addr = addr.word;
    }
    else
      h2t_chan->ipi_addr = 0;

    if (tic.writable_event >= 0)
    {








      IPI_REMOTE_TRIGGER_ADDR_t addr = {{
          .tile_y = HV_LOTAR_Y(real_writable_lotar),
          .tile_x = HV_LOTAR_X(real_writable_lotar),
          .ipi = CLIENT_PL,
          .event = tic.writable_event,
      }};

      t2h_chan->ipi_addr = addr.word;
    }
    else
      t2h_chan->ipi_addr = 0;

    //
    // Inform the client about the current state of the FIFOs via
    // interrupt.
    //

    //
    // For host-to-tile, see if we have any data in the channel; if so,
    // send an interrupt to the driver.
    //
    if (!CHAN_EMPTY(h2t_chan) && h2t_chan->ipi_addr)
      cfg_wr(my_ipi_pos.word, 0, h2t_chan->ipi_addr, 0);

    //
    // For tile-to-host, see if we have any space in the channel; if so,
    // send an interrupt to the driver.
    //
    if (!CHAN_FULL(t2h_chan) && t2h_chan->ipi_addr)
      cfg_wr(my_ipi_pos.word, 0, t2h_chan->ipi_addr, 0);

    spin_unlock(&ts->lock);

    return len;
  }
}

/** Write to the tile-monitor FIFO console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer; if this is zero, this is a
 *        hypervisor write, otherwise it is a client write.
 * @return Number of characters written.
 */
static int
tmfifo_cons_write(char* s, int len, unsigned int offset, void* private)
{
  int orig_len = len;

  TRACE("cons_write(len %d)\n", len);

  //
  // For hypervisor output, we spin on output, never do short writes.
  // For client output, if the console interrupt is enabled, we are
  // able to do short writes and then interrupt the client when we have
  // more space for output.
  //
  while (len)
  {
    spin_lock(&tmfifo_state->lock);

    int rv = tmfifo_wrchan(tmfifo_state, TM_CONS_CHAN, DRV_FLG_HVADDR, s,
                           len, 1);
    spin_unlock(&tmfifo_state->lock);

    if (rv < 0)
      return (orig_len > len) ? (orig_len - len) : rv;

    len -= TMFIFO_RETVAL_BYTES(rv);
    s += TMFIFO_RETVAL_BYTES(rv);

    if (len > 0 && !(unsigned long)private && console_ipi_addr)
    {
      SET_PENDING_IPI_OUTPUT();
      break;
    }
  }

  TRACE("cons_write returning %d\n", orig_len - len);
  return orig_len - len;
}


/** Write to the tile-monitor FIFO console, with nl->crnl translation.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer; if this is zero, this is a
 *        hypervisor write, otherwise it is a client write.
 * @return Number of characters written.
 */
static int
tmfifo_cons_write_onlcr(char* s, int len, unsigned int offset, void* private)
{
  int orig_len = len;

  while (len)
  {
    char* nl = memchr(s, '\n', len);

    if (nl)
    {
      int rv = tmfifo_cons_write(s, nl - s, offset, private);
      if (rv < 0)
        return (orig_len > len) ? (orig_len - len) : rv;

      len -= rv;

      rv = tmfifo_cons_write("\r\n", 2, offset, private);
      if (rv < 0)
        return (orig_len > len) ? (orig_len - len) : rv;

      //
      // Note that we know that tmfifo_cons_write retries until it writes
      // everything, or it gets an error.  We're thus not handling the case
      // where we write \r\n and both bytes don't get written, as it would
      // make this already-too-complex little routine even more complicated.
      //
      s = nl + 1;
      len--;
    }
    else
    {
      int rv = tmfifo_cons_write(s, len, offset, private);

      if (rv < 0)
        return (orig_len > len) ? (orig_len - len) : rv;

      len -= rv;
      s += rv;
    }
  }

  return orig_len;
}


/** Read from the tile-monitor FIFO console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param offset File offset; unused for this file.
 * @param private Private data pointer; unused for this file.
 * @return Number of characters read.
 */
static int
tmfifo_cons_read(char* s, int len, unsigned int offset, void* private)
{
  TRACE("cons_read(len %d)\n", len);

  spin_lock(&tmfifo_state->lock);

  int rv = tmfifo_rdchan(tmfifo_state, TM_CONS_CHAN, DRV_FLG_HVADDR, s, len);

  spin_unlock(&tmfifo_state->lock);

  if (rv < 0)
    return rv;

  return TMFIFO_RETVAL_BYTES(rv);
}


/** Wait for the tile-monitor console to drain.
 * @param private Private file state (not used by this device).
 * @return Zero if all data was successfully flushed.
 */
static int
tmfifo_cons_sync(void* private)
{
  tmfifo_state_t* ts = tmfifo_state;

  spin_lock(&ts->lock);

  //
  // We want to wait until both the console FIFO and the actual rshim
  // hardware FIFO are empty.  We might be the interrupt tile, and since
  // we're spinning, the interrupt routine is never going to run.  So, we
  // just call the output processor until things are drained.
  //
  while (CHAN_BYTES(&ts->t2h_chans[TM_CONS_CHAN]) ||
         cfg_rd(ts->rshim, 0, RSH_TM_TILE_TO_HOST_STS) > 0)
    tmfifo_outproc(ts);

  spin_unlock(&ts->lock);

  return 0;
}


/** Tile-monitor FIFO console file operations vector. */
static struct _file_ops tmfifo_fops =
{
  .write = tmfifo_cons_write,
  .read = tmfifo_cons_read,
  .sync = tmfifo_cons_sync,
};

/** Buffer for tile-monitor FIFO console output file. */
static char tmfifo_outbuf[256];

/** Tile-monitor FIFO console output file. */
FILE tmfifo_out =
{
  .buf = tmfifo_outbuf,
  .len = sizeof (tmfifo_outbuf),
  .ptr = tmfifo_outbuf,
  .wrem = sizeof (tmfifo_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &tmfifo_fops
};

/** Buffer for tile-monitor FIFO console input file.  This is tiny in order
 *  to prevent out-of-order characters being delivered when the supervisor
 *  is reading from the tile which owns the UART and tiles which access it
 *  remotely.  (See more explanation of this problem in cons_remote.c.) Note
 *  that we're already doing plenty of buffering with the channel FIFOs in
 *  the tmfifo driver.
 */
static char tmfifo_inbuf[1];

/** Tile-monitor FIFO console input file. */
FILE tmfifo_in =
{
  .buf = tmfifo_inbuf,
  .len = sizeof (tmfifo_inbuf),
  .ptr = tmfifo_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &tmfifo_fops
};


/** Tile-monitor FIFO console file operations vector, with nl->crnl
 *  translation. */
static struct _file_ops tmfifo_onlcr_fops =
{
  .write = tmfifo_cons_write_onlcr,
  .read = tmfifo_cons_read,
};

/** Buffer for tmfifo console output file, with nl->crnl translation. */
static char tmfifo_outbuf_onlcr[256];

/** Tile-monitor FIFO console output file, with nl->crnl translation. */
FILE tmfifo_out_onlcr =
{
  .buf = tmfifo_outbuf_onlcr,
  .len = sizeof (tmfifo_outbuf_onlcr),
  .ptr = tmfifo_outbuf_onlcr,
  .wrem = sizeof (tmfifo_outbuf_onlcr),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &tmfifo_onlcr_fops,
  .pvt = (void *) 1,
};


/** Tile-monitor FIFO driver operations vector. */
static struct drv_ops tmfifo_ops = {
  .init        = tmfifo_init,
  .open        = tmfifo_open,
  .pread       = tmfifo_pread,
  .pwrite      = tmfifo_pwrite,
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_tmfifo = {
  .shim_type  = DEV_PSEUDO_TMFIFO,
  .name       = "tmfifo",
  .desc       = "Tile-monitor FIFO",
  .ops        = &tmfifo_ops,
  .stilereq   = 1,
  .intchanreq = 1,
  .flags      = DRV_FLG_AUTOMATIC,
};
