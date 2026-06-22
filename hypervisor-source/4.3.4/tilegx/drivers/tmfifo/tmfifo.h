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
 * Definitions for the tile-monitor FIFO driver.
 */

#ifndef _SYS_HV_DRV_TMFIFO_H
#define _SYS_HV_DRV_TMFIFO_H

#include <hv/drv_tmfifo_intf.h>
#include <hv/drv_tmfifo_proto.h>

#include "drvintf.h"
#include "lock.h"

/** Number of bytes in the per-channel FIFOs.  Must be a power of 2. */
#define CHAN_SIZE 4096

/** Number of channels supported. */
#define TM_CHANNELS 2

/** Channel used for the console. */
#define TM_CONS_CHAN 1

/** Per-half-channel state. */
typedef struct
{
  /** Data. */
  char c[CHAN_SIZE];

  /** Index of first readable character. */
  unsigned int head;

  /** Index of character after last readable character.  If head==tail,
   *  FIFO is empty. */
  unsigned int tail;

  /** Number of bytes reader last asked for but couldn't get. */
  uint32_t last_unavailable;

  /** Address to send IPIs for readable/writeable events; zero means that
   *  we won't send any IPIs.  (Note we only support interrupts to the
   *  client's PL, so zero can never be a valid value here). */
  PA ipi_addr;
}
tmfifo_chan_t;

/** True if a channel is full. */
#define CHAN_FULL(chan) \
  ((chan)->head == (((chan)->tail + 1) & (CHAN_SIZE - 1)))

/** True if a channel is empty. */
#define CHAN_EMPTY(chan) \
  ((chan)->head == (chan)->tail)

/** Number of bytes in a channel. */
#define CHAN_BYTES(chan) \
  (((chan)->tail - (chan)->head) & (CHAN_SIZE - 1))

/** Number of contiguous bytes in a channel. */
#define CHAN_CONTIG_BYTES(chan) \
    (((chan)->head <= (chan)->tail) ? (chan)->tail - (chan)->head \
                                    : CHAN_SIZE - (chan)->head);

/** Number of contiguous free bytes in a channel. */
#define CHAN_CONTIG_SPACE(chan) \
  (((chan)->head <= (chan)->tail) ? CHAN_SIZE - (chan)->tail - \
                                    ((chan)->head == 0) \
                                  : (chan)->head - (chan)->tail - 1)

/** Channel head pointer incremented by a value. */
#define CHAN_NEXT_HEAD(chan, val) \
  (((chan)->head + (val)) & (CHAN_SIZE - 1))

/** Channel tail pointer incremented by a value. */
#define CHAN_NEXT_TAIL(chan, val) \
  (((chan)->tail + (val)) & (CHAN_SIZE - 1))


/** Global tile-monitor FIFO state. */
typedef struct
{
  //
  // General driver state.
  //

  /** Must hold this lock to modify shared data. */
  spinlock_t lock;

  /** rshim address. */
  uint32_t rshim;

  /** Tile to host FIFO size. */
  int t2h_size;

  /** Interrupt channel number. */
  int intchan;

  /** Protocol version with which to interpret incoming data. */
  uint8_t proto_in;

  /** Protocol version with which to format outgoing data. */
  uint8_t proto_out;

  /** Protocol version received from host in an INIT0 request.  Note
   *  that this is used as a flag to tell us that we need to send an
   *  INIT1, and is cleared after we've done so. */
  uint8_t proto_init0;

  //
  // Drainer state.
  //

  /** Number of bytes remaining in the current packet.  If 0, we aren't
   *  in the middle of a packet. */
  unsigned int pkt_rem_bytes;

  /** Channel that the current packet is going to. */
  unsigned int pkt_chan_idx;

  /** Last word read from the FIFO; this gets shifted down as bytes are
   *  consumed from it. */
  uint_reg_t last_fifo_word;

  /** Bytes remaining in last_fifo_word. */
  unsigned int last_word_rem_bytes;

  //
  // Outproc state.
  //

  /** First channel to check next time we run outproc. This rotates in
   *  order to prevent one channel from getting all of the output bandwidth. */
  int first_outproc_chan;

  //
  // Host-to-tile state.
  //

  /** Host-to-tile channel halves. */
  tmfifo_chan_t h2t_chans[TM_CHANNELS];

  //
  // Tile-to-host state.
  //

  /** Tile-to-host channel halves. */
  tmfifo_chan_t t2h_chans[TM_CHANNELS];
}
tmfifo_state_t;

#endif /* _SYS_HV_DRV_TMFIFO_H */
