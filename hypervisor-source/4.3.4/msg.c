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
 * Routines related to hypervisor messaging.
 */

#include <stdio.h>
#include <string.h>

#include <arch/atomic.h>
#include <arch/cycle.h>
#include <arch/idn.h>
#include <arch/interrupts.h>
#include <arch/ipi.h>
#include <arch/sim.h>
#include <arch/spr.h>

#include "sys/libc/include/util.h"

#include "bits.h"
#include "bme_msg.h"
#include "config.h"
#include "client_msg.h"
#include "client_obj.h"
#include "console.h"
#include "debug.h"
#include "devices.h"
#include "downcall.h"
#include "drvintf.h"
#include "hv.h"
#include "hw_config.h"
#include "idn.h"
#include "lock.h"
#include "mapping.h"
#include "msg.h"
#include "mshim_acc.h"
#include "nap.h"
#include "post/post_ram.h"
#include "tsb.h"

#include "tilegx/drvintf_mmio.h"

//
// Define the interrupt dispatch table.  This is non-static because it's
// used in intvec.S; if its layout changes, code in the idn_intr macro there
// needs to be adjusted appropriately.
//

/** Interrupt dispatch table entry. */
typedef struct
{
  drv_intr_func* func;  /**< Function to call */
  void* arg;            /**< Argument to pass to the function */
}
int_disp_t;

/** Instant interrupt table */
int_disp_t instant_intr_table[1 << DRV_CHAN_WIDTH];
/** Delayed interrupt table */
int_disp_t delayed_intr_table[1 << DRV_CHAN_WIDTH];

// We set unused instant interrupt table function entries to this so that we
// ignore those interrupts.  FIXME: This probably ought to panic, eventually.
extern drv_intr_func drv_intr_exit;  /**< No-op interrupt routine */


//
// Define the reply channel table.
//
/** Reply channel table entry */
typedef struct
{

























  /** Actual reply type received; if zero, nothing received yet. */
  uint16_t rcvtype;
  /** Length of buf in words.  If zero, channel is free. */
  uint8_t bufwds;
  /** Actual number of words in received message; may be larger than
   *  bufwds, although we will not put more than bufwds words of data into
   *  *buf. */
  uint8_t rcvwds;
  /** Destination of message this channel is for (for debug purposes). */
  uint16_t dest;
  /** Type of message this channel is expecting (for debug purposes). */
  uint16_t msgtype;
  /** Buffer into which reply is placed.  If NULL, reply's length will
      be recorded but it won't be saved. */
  void* buf;

}
chan_t;

//
// The channel number we use is the tile index of the destination, with the
// priority in the high-order bits, so there's one channel per destination
// per priority.
//
/** Number of entries in reply channel table */
#define N_CHAN (HV_TILES * HV_MSG_MAXPRI)
/** Reply channel table */




static chan_t channels[N_CHAN];



// Define the pending message queue.  These are messages which have been taken
// off the IDN by msg_avail(), but not yet processed by msg_proc().

/** Pending message queue entry */
typedef struct p_msg
{
  /** Next message in list */







  struct p_msg* next;

  /** Tag from message */
  hv_tag tag;
  /** Number of words in message */
  int len;
  /** Message */
  unsigned long buf[HV_MAXMSGWDS];
} p_msg_t;

// The queue is implemented as a linked list, with a head and a tail pointer.
// msg_avail() will read and write these pointers, and can interrupt
// msg_proc(); msg_proc() must thus read and write these pointers in an
// interrupt critical section to ensure consistency.  We glom all of the
// pointers into a structure to make it easier to refer to them as volatile
// from msg_proc().

/** Pending message queue state */
static struct p_msg_ptrs
{
  // Head of pending message list, by priority
  p_msg_t* head[HV_MSG_MAXPRI + 1];
  // Tail of pending message list, by priority
  p_msg_t* tail[HV_MSG_MAXPRI + 1];
  // Free message structures
  p_msg_t* free;
}
p_msg_ptrs;

/** Current priority (i.e., priority of request we're currently processing). */
static int curpri = HV_MSG_PRI_IDLE;



















































int
register_intr(drv_intr_func* func, void* arg, int type, int intchan)
{
  int_disp_t* idp;
  drv_intr_func* empty_func;

  if (type == DRV_INTR_INSTANT)
  {
    if (intchan < 0 ||
        intchan >= sizeof (instant_intr_table) / sizeof (instant_intr_table[0]))
      return (1);

    idp = &instant_intr_table[intchan];
    empty_func = drv_intr_exit;
  }
  else if (type == DRV_INTR_DELAYED)
  {
    if (intchan < 0 ||
        intchan >= sizeof (delayed_intr_table) / sizeof (delayed_intr_table[0]))
      return (1);

    idp = &delayed_intr_table[intchan];
    empty_func = 0;
  }
  else
    return (1);

  if (idp->func != empty_func)
    return (1);

  idp->func = func;
  idp->arg = arg;
  __insn_mtspr(IPI_MASK_RESET_HV, 1UL << intchan);
  return (0);
}

/** Set up a channel number for reception.
 * @param dest Source of the reply.
 * @param type Type of message expected on the channel.
 * @param buf Buffer into which the eventual reply will be placed; could be
 *        NULL.
 * @param buflen Length of the buffer.
 * @return Zero if the channel was successfully initialized; nonzero if the
 *        channel was already busy.
 */
#define MSG_CHAN_BUSY (~0)
static uint32_t getchan(pos_t dest, uint32_t type, void* buf, size_t buflen)
{
  uint32_t chan = POS2IDX(dest) + (HV_TILES * (HV_PRI(type) - HV_MSG_MINPRI));

  if (chan >= N_CHAN)
    panic("channel number %d too large; dest %#x, type %#x",
          chan, dest.word, type);

  if (channels[chan].bufwds == 0)
  {
    channels[chan].rcvtype = 0;



    channels[chan].buf = buf;
    channels[chan].bufwds = B2W_DN(buflen);
    assert(channels[chan].bufwds);
    channels[chan].rcvwds = 0;
    channels[chan].dest = POS2IDX(dest);
    channels[chan].msgtype = type;

    return (chan);
  }

  return (MSG_CHAN_BUSY);
}

















































































































































































































int
send_var(pos_t dest, uint32_t msgtype, void* msg, size_t msglen, void* buf,
         size_t buflen, uint32_t* replychanp, void* replybuf,
         size_t replybuflen, uint32_t flags)
{
  //
  // If we're in the middle of sending another message, we can't start a new
  // one.
  //

  if (__insn_mfspr(SPR_IDN_PENDING) != 0)
  {
    if (flags & MSG_FLG_XMITFAIL)
      return (1);
    panic("can't send msgtype %#x, message already in progress", msgtype);
  }


  //
  // Make sure we can send this priority of message.
  // If we are waiting for a response to a previous message at the
  // same or higher priority, we can't start this one.
  //
  if (HV_PRI(msgtype) <= curpri)
  {
    if (flags & MSG_FLG_XMITFAIL)
      return (1);
    panic("send message priority %d too small; curpri %d, dest %#x, "
          "type %#x", HV_PRI(msgtype), curpri, dest.word, msgtype);
  }

  //
  // Get our channel.
  //
  uint32_t chan = getchan(dest, msgtype, replybuf, replybuflen);
  if (chan == MSG_CHAN_BUSY)
  {
    if (flags & MSG_FLG_XMITFAIL)
      return (1);
    panic("can't send msgtype %#x, channel %d busy", msgtype, chan);
  }

  *replychanp = chan;

  //
  // Send the message.
  //
  int msgwds = B2W_UP(msglen);
  int bufwds = B2W_UP(buflen);

  if (msgwds + bufwds + 1 > HV_MAXMSGWDS)
    panic("send message too large; msgwds %d bufwds %d", msgwds, bufwds);

  hv_tag tag =
  {
    .bits.len = msgwds + bufwds,
    .bits.chan = chan,
    .bits.type = msgtype,
  };

  if (flags & (MSG_FLG_SENDBOOT | MSG_FLG_SENDIDN0))
  {




















    idn_send(dest.word | (2 + msgwds + bufwds));
    idn_send(tag.word);
    idn_send(my_pos.word);
    unsigned long* p = msg;
    for (int len = msgwds; len; len--)
      idn_send(*p++);

  }
  else
  {



    idn_send(dest.word | (2 + msgwds + bufwds));
    idn_send(tag.word);
    idn_send(my_pos.word);
    unsigned long* p = msg;
    for (int len = msgwds; len; len--)
      idn_send(*p++);

    // Send trailing buffer; we check for the common case of it being
    // word-aligned to handle it more efficiently.

    if (((uintptr_t) buf & (sizeof (unsigned long) - 1)) == 0)
    {
      unsigned long* ui_bufp = buf;

      // First handle the full words.

      for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
        idn_send(*ui_bufp++);

      // Now do one last word to handle the residue.

      if (buflen)
#ifndef __BIG_ENDIAN__
        idn_send(*ui_bufp & RMASK(buflen * 8));
#else
        idn_send(*ui_bufp & LMASK(buflen * 8));
#endif
    }
    else
    {
      unsigned long outword;
      unsigned char* uc_bufp = buf;

      // First handle the full words.

      for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
      {
        outword = *uc_bufp++;
        for (int i = 1; i < sizeof (unsigned long); i++)
          outword |= *uc_bufp++ << (8 * i);
        idn_send(outword);
      }

      // Now do one last word to handle the residue.

      if (buflen)
      {
        int bitsleft = buflen * 8;

        outword = 0;
        for (int i = 0; i < bitsleft; i += 8)
          outword |= *uc_bufp++ << i;

        idn_send(outword);
      }
    }

  }

  return (0);
}


void
nap_until_change(void* valptr, int valsize, uint64_t curval, int flags,
                 int msgtype, int dest, void* caller)
{
  uint32_t oldics = __insn_mfspr(SPR_INTERRUPT_CRITICAL_SECTION);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  //
  // If we get device interrupts via IPI, then we want to allow them while
  // waiting for message replies, as long as we're currently at a lower
  // priority.
  //
  // FIXME: GX: this will change when we start doing hv messaging via
  // IPI; in that case we'll want to actually unmask device interrupts
  // in the IPI mask register, and potentially IPI bits which correspond
  // to higher-priority messages.
  //
  uint_reg_t mask_ipi = 0;
  if (curpri < HV_MSG_PRI_DRVINTR)
  {




    mask_ipi = (1UL << INT_IPI_HV) & __insn_mfspr(INTERRUPT_MASK_HV);
    __insn_mtspr(INTERRUPT_MASK_RESET_HV, mask_ipi);

  }

#ifndef NO_NAP_TIMEOUT
  unsigned long timeout, entry_cycle;
  if ((flags & NUC_FLG_NO_TIMEOUT) || sim_is_simulator())
  {
    // Looping here slows down the simulator dramatically, so don't do it.
    timeout = ULONG_MAX;
    entry_cycle = 0;
  }
  else
  {
    // We spin 40G cycles on the theory that this is in the 20-40 sec range,
    // and no reply message should ever take that long to show up.
    timeout = 40000000000;
    entry_cycle = get_cycle_count();
  }
#else
  // Wait forever.
  unsigned long timeout = ULONG_MAX;
#endif
  while (1)
  {
    uint64_t newval;
    switch (valsize)
    {
    case 8:
      newval = *(volatile uint64_t*) valptr;
      break;
    case 4:
      newval = *(volatile uint32_t*) valptr;
      break;
    case 2:
      newval = *(volatile uint16_t*) valptr;
      break;
    case 1:
      newval = *(volatile uint8_t*) valptr;
      break;
    default:
      panic("bad size in nap_until_change: %d", valsize);
    }
    if (newval != curval)
      break;

    volatile struct p_msg_ptrs *pmp = &p_msg_ptrs;
    int yield = 0;
    //
    // On a dedicated tile, we only want to process incoming messages when
    // the driver calls drv_yield().  Everwhere else, we process them when
    // they're higher than our current priority (i.e., the priority of the
    // message we're currently processing).
    //
    if (!(flags & NUC_FLG_NO_YIELD))
      for (int pri = HV_MSG_MAXPRI; pri > curpri; pri--)
        if (pmp->head[pri])
          yield = 1;

    if (yield)
    {
      __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
      unmask_intr(INT_INTCTRL_HV);
      mask_intr(INT_INTCTRL_HV);
      __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
    }
    else
    {
      int retval = nap_idn_ca_msg(timeout, msgtype, dest, caller);
#ifndef NO_NAP_TIMEOUT
      if (retval != 0)
      {
        long cycle = get_cycle_count();
        uint32_t rcvtypes[N_CHAN];   // capture values before printing
        for (int i = 0; i < N_CHAN; ++i)
          rcvtypes[i] = channels[i].rcvtype;
        tprintf("WARNING: waiting for reply message: timed out\n");
        tprintf("Now cycle %ld, timeout %ld cycles\n", cycle, timeout);
        tprintf("Caller %p with ICS %u\n", caller, oldics);
        tprintf("Message type %#x, dest %#x\n", msgtype, dest);
        for (int i = 0; i < N_CHAN; ++i)
          if (rcvtypes[i])
            tprintf("chan %d: %d\n", i, rcvtypes[i]);
        timeout = ULONG_MAX;
      }
#else
      assert(retval == 0);
#endif
    }
  }




  __insn_mtspr(INTERRUPT_MASK_SET_HV, mask_ipi);


  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, oldics);

#ifndef NO_NAP_TIMEOUT
  if (timeout == ULONG_MAX && entry_cycle)
    tprintf("Wait for reply message completed successfully (%lld cycles)\n",
            get_cycle_count() - entry_cycle);
#endif
}


uint32_t
getreply(uint32_t replychan, size_t* replylenp, int notimeout)
{
  if (replychan >= N_CHAN)
    panic("reply channel %d too big in getreply", replychan);

  chan_t* cp = &channels[replychan];

  int flags = 0;
  if (is_dedicated)
    flags |= NUC_FLG_NO_YIELD;
  if (notimeout)
    flags |= NUC_FLG_NO_TIMEOUT;
  nap_until_change(&cp->rcvtype, sizeof (cp->rcvtype), 0, flags,
                   cp->msgtype, cp->dest, __builtin_return_address(0));

  uint32_t retval = cp->rcvtype;









  *replylenp = W2B(cp->rcvwds);
  cp->rcvtype = cp->bufwds = 0;

  return (retval);
}


void
reply_var(pos_t dest, uint32_t msgchan, uint32_t msgtype, void* msg,
          size_t msglen, void* buf, size_t buflen, int flags, uint32_t send_tag)
{

  //
  // If we're in the middle of sending another message, we can't start a new
  // one.
  //
  if (__insn_mfspr(SPR_IDN_PENDING) != 0)
      panic("can't send replytype %#x, message already in progress", msgtype);


  //
  // Send the reply.
  //
  int msgwds = B2W_UP(msglen);
  int bufwds = B2W_UP(buflen);

  if (msgwds + bufwds > HV_MAXMSGWDS)
    panic("reply message too large; msgwds %d bufwds %d", msgwds, bufwds);

  hv_tag tag =
  {
    .bits.len = msgwds + bufwds,
    .bits.reply = 1,
    .bits.chan = msgchan,
    .bits.type = msgtype,
  };







  idn_send(dest.word | (1 + msgwds + bufwds));
  idn_send(tag.word);

  unsigned long* p = msg;
  for (int len = msgwds; len; len--)
    idn_send(*p++);

  // Send trailing buffer; we check for the common case of it being
  // word-aligned to handle it more efficiently.

  if (((uintptr_t) buf & (sizeof (unsigned long) - 1)) == 0)
  {
    unsigned long* ui_bufp = buf;

    // First handle the full words.

    for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
      idn_send(*ui_bufp++);

    // Now do one last word to handle the residue.

    if (buflen)
#ifndef __BIG_ENDIAN__
      idn_send(*ui_bufp & RMASK(buflen * 8));
#else
      idn_send(*ui_bufp & LMASK(buflen * 8));
#endif
  }
  else
  {
    unsigned long outword;
    unsigned char* uc_bufp = buf;

    // First handle the full words.

    for (; buflen >= sizeof (unsigned long); buflen -= sizeof (unsigned long))
    {
      outword = *uc_bufp++;
      for (int i = 1; i < sizeof (unsigned long); i++)
        outword |= *uc_bufp++ << (8 * i);
      idn_send(outword);
    }

    // Now do one last word to handle the residue.

    if (buflen)
    {
      int bitsleft = buflen * 8;

      outword = 0;
      for (int i = 0; i < bitsleft; i += 8)
        outword |= *uc_bufp++ << i;

      idn_send(outword);
    }
  }

}


int
send_receive_var(pos_t dest, uint32_t msgtype, void* msg, size_t msglen,
                 void* buf, size_t buflen, void* replybuf, size_t replybuflen,
                 size_t* replylenp, uint32_t flags)
{
  uint32_t replychan;

  int send_retval = send_var(dest, msgtype, msg, msglen, buf, buflen,
                             &replychan, replybuf, replybuflen, flags);
  if (send_retval)
    return (send_retval);

  size_t rcv_replylen;

  uint32_t rcv_type = getreply(replychan, &rcv_replylen, 0);

  if (rcv_type != msgtype)
    panic("message type mismatch: sent %#x, got %#x", msgtype, rcv_type);

  if (!(flags & MSG_FLG_SHORTREPLY) && rcv_replylen != replybuflen)
    panic("reply too short: type %#x, got %zd bytes, expected %zd bytes",
          msgtype, rcv_replylen, replybuflen);

  if (rcv_replylen > replybuflen)
    panic("reply too long: type %#x, got %zd bytes, expected no more than "
          "%zd bytes", msgtype, rcv_replylen, replybuflen);

  if (replylenp)
    *replylenp = rcv_replylen;

  return (0);
}










































void
init_msg()
{
  for (int i = 0;
       i < sizeof (instant_intr_table) / sizeof (instant_intr_table[0]); i++)
    instant_intr_table[i].func = drv_intr_exit;

  //
  // Ideally, we'd have one message per remote tile per priority level.  At
  // the moment, doing that would require that we spend a bit too much memory
  // on the message buffers, so we're going to allocate:
  // - one message for every remote tile (i.e., the number of tiles minus
  //   one), plus
  // - one for every possible outstanding delayed device interrupt, plus
  // - one extra for every four tiles, to cope with the fact that we may get
  //   multiple messages per tile in some cases, plus
  // - one for each message priority level, so we have at least one per
  //   priority even on very small simulator configurations.
  // We should in the future expand this to the theoretically maximum required
  // number of messages, by either (a) shrinking the maxmimum message size,
  // or (b) being more intelligent about exactly which tiles need more
  // buffers (e.g., driver master tiles, the console master tile), and then
  // allocating them a larger per-tile data page.
  //
  int width = chip_logical_lrhc.bits.x -
              chip_logical_ulhc.bits.x + 1;
  int height = chip_logical_lrhc.bits.y -
               chip_logical_ulhc.bits.y + 1;
  int n_tiles = height * width;
  int n_msgs = (n_tiles - 1) + n_tiles / 4 + HV_MSG_MAXPRI;

  for (struct device* devp = devices; devp->name; devp++)
    if (devp->drv)
      n_msgs += devp->drv->maxdelint;

  //
  // Allocate n_msgs messages, and link them all onto the freelist.
  //
  struct p_msg_ptrs *pmp = &p_msg_ptrs;



  pmp->free = local_alloc(n_msgs * sizeof (*pmp->free), CHIP_L2_LINE_SIZE());









  pmp->free[n_msgs - 1].next = NULL;
  for (int i = n_msgs - 2; i >= 0; i--)
    pmp->free[i].next = &pmp->free[i + 1];


  for (int pri = 0; pri <= HV_MSG_MAXPRI; pri++)
    pmp->head[pri] = pmp->tail[pri] = NULL;





  init_idn_ca();

}


/** Do initial processing of an incoming message.
 * @param msg The message.
 * @return Nonzero if the message was a reply, zero if it was a request.
 */
static int
msg_arrived(p_msg_t* msg)
{
  struct p_msg_ptrs *pmp = &p_msg_ptrs;

  if (msg->tag.bits.reply)
  {
    //
    // The message is a reply; save it in the appropriate channel buffer.
    //
    int chan = msg->tag.bits.chan;

    if (chan >= N_CHAN)
      panic("bad reply channel %d; msgtag %#x", chan, msg->tag.word);

    if (unlikely(!channels[chan].bufwds))
    {
      panic_start("reply message for unexpected channel %d; tag %#x",
                  chan, msg->tag.word);
      for (int i = 0; i < msg->len; i++)
        tprintf("msg word %3d: %#011lX\n", i, msg->buf[i]);
      panic_end();
    }

    channels[chan].rcvwds = msg->len;

    int copywds = (msg->len > channels[chan].bufwds) ?
                  channels[chan].bufwds: msg->len;

    if (channels[chan].buf)
      memcpy(channels[chan].buf, msg->buf, W2B(copywds));

    channels[chan].rcvtype = msg->tag.bits.type;

    return 1;
  }

  //
  // No, not a reply, so we need to save it for later processing.
  // Remember that we got a message, and link msg onto the correct
  // pending list.
  //
  int pri = (msg->tag.word == DRV_INTR_DELAYED_TAG) ?
              HV_MSG_PRI_DRVINTR : HV_PRI(msg->tag.bits.type);
  msg->next = NULL;
  if (pmp->head[pri])
  {
    pmp->tail[pri]->next = msg;
    pmp->tail[pri] = msg;
  }
  else
    pmp->head[pri] = pmp->tail[pri] = msg;

  return 0;
}


/** Do next stage processing after some messages have been processed by
 *  msg_arrived().
 * @param int_number Interrupt number.
 * @param sr Saved registers.
 * @param got_request Did we get at least one request?
 * @param got_reply Did we get at least one reply?
 */
void
msgs_arrived(int int_number, struct saved_regs* sr, int got_request,
             int got_reply)
{
  //
  // If we got anything, wake up a potentially napping interruptee.
  //
  if (got_request || got_reply)
    wake_idn_ca_msg(sr);

  //
  // If we got any requests, run the processor.
  //
  if (got_request)
  {
    //
    // If we got here by interrupting the client, then we can just run
    // the processor straightaway.  Otherwise, we set intctrl_2 so that
    // we'll run it once we return from whatever routine we interrupted.
    // (We might have interrupted the processor, but that's OK; we'll set
    // intctrl_2, return to it, and when it's done it'll clear intctrl_2.)
    //
    if ((sr->ex_context_1 & SPR_EX_CONTEXT_0_1__PL_MASK) <= CLIENT_PL)
      msg_proc(int_number, sr);
    else
      __insn_mtspr(INTCTRL_HV_STATUS, 1);
  }
}




void
msg_avail(int int_number, struct saved_regs* sr)
{
  struct p_msg_ptrs *pmp = &p_msg_ptrs;
  //
  // If cur_msg is nonzero, it's a partially full message; it will be appended
  // to until the message is complete.  We declare this here, rather than above
  // with the queue, because this state is _not_ visible to msg_proc().
  //
  static p_msg_t* cur_msg = NULL;

  int got_request = 0;
  int got_reply = 0;

  //
  // Loop until we've drained the IDN.
  //
  unsigned long avail_reg;
  while ((avail_reg = __insn_mfspr(SPR_IDN_DATA_AVAIL)) &
         SPR_IDN_DATA_AVAIL__AVAIL_0_MASK)
  {
    //
    // If we're starting a new message, save the tag and empty its buffer.
    //
    if (!cur_msg)
    {
      if (!pmp->free)
        panic("no free message buffers");

      cur_msg = pmp->free;
      pmp->free = pmp->free->next;

      cur_msg->tag.word = idn0_receive();
      cur_msg->len = 0;

      //
      // If we just read the only word of this message, make sure the the
      // read loop below doesn't run.
      //
      if (avail_reg & SPR_IDN_DATA_AVAIL__EOP_0_MASK)
        avail_reg &= ~SPR_IDN_DATA_AVAIL__AVAIL_0_MASK;
      else
        avail_reg = __insn_mfspr(SPR_IDN_DATA_AVAIL);
    }

    unsigned long* p = &cur_msg->buf[cur_msg->len];

    //
    // Receive until we finish the message or there isn't any incoming data.
    //

    while (avail_reg & SPR_IDN_DATA_AVAIL__AVAIL_0_MASK)
    {
      if (cur_msg->len >= HV_MAXMSGWDS)
        panic("incoming message too large (>%d words)", HV_MAXMSGWDS);

      *p++ = idn0_receive();
      cur_msg->len++;

      if (avail_reg & SPR_IDN_DATA_AVAIL__EOP_0_MASK)
        break;

      avail_reg = __insn_mfspr(SPR_IDN_DATA_AVAIL);
    }

    //
    // We finished this message.
    //
    if (avail_reg & SPR_IDN_DATA_AVAIL__EOP_0_MASK)
    {
      if (msg_arrived(cur_msg))
      {
        //
        // Message was a reply, which was processsed, so put cur_msg back
        // on the free list.
        //
        cur_msg->next = pmp->free;
        pmp->free = cur_msg;
        got_reply = 1;
      }
      else
        got_request = 1;

      cur_msg = NULL;
    }
    else
    {
      //
      // We haven't finished, so stop, since we must have emptied the network;
      // no need to do another SPR read.
      //
      break;
    }
  }

  //
  // Kick off further message processing if needed.
  //
  msgs_arrived(int_number, sr, got_request, got_reply);
}




void
ipi_proc(int int_number, struct saved_regs* sr)
{
  //
  // Note that masked events still show up in the events SPR, so we need
  // to do the mask ourselves in case some of these events should be
  // ignored.
  //
  uint_reg_t events = __insn_mfspr(IPI_EVENT_HV) & 



                      ~__insn_mfspr(IPI_MASK_HV);

  __insn_mtspr(IPI_EVENT_RESET_HV, events);

















































  //
  // At this point, any remaining events -- and we know we have at least
  // one -- must be driver interrupts.
  //
  if (curpri >= HV_MSG_PRI_DRVINTR)
    panic("IPI when curpri already at PRI_DRVINTR");

  int old_curpri = curpri;
  curpri = HV_MSG_PRI_DRVINTR;

  for (; events; events &= events - 1)
  {
    int chan = __builtin_ctz(events);

    if (chan >= sizeof (delayed_intr_table) / sizeof (delayed_intr_table[0]))
      panic("out-of-range IPI %d", chan);

    int_disp_t* entry = &delayed_intr_table[chan];

    if (entry->func == NULL)
      panic("unregistered IPI %d", chan);

    entry->func(entry->arg, (void*) (long) chan, 0);
  }

  curpri = old_curpri;

  //
  // If we interrupted someone who was napping, wake them up.  This handles
  // the case where we get an IPI while napping, the IDN reply that the
  // napper was waiting for then comes in and interrupts the IPI-handling
  // routine, and the wake_idn_ca_msg() at the end of msg_avail() thus
  // doesn't wake anyone up.
  //
  wake_idn_ca_msg(sr);
}


#ifdef CONSOLE_DEBUG
char last_outbuf[64 * 1024];
int last_outbuf_pos;
#endif

/** Message statistics table, indexed by message number; index 0 is for
 *  delayed interrupts. */
struct hv_stats msg_stats[HV_MAX_TAG + 1];

void
msg_proc(int int_number, struct saved_regs* sr)
{
  volatile struct p_msg_ptrs *pmp = &p_msg_ptrs;

  //
  // We must be interruptible by incoming data during processing to prevent
  // deadlocks.
  //
  __insn_mtspr(SPR_IDN_AVAIL_EN,
               SPR_IDN_AVAIL_EN__EN_0_MASK | SPR_IDN_AVAIL_EN__EN_1_MASK);


  //
  // Get into the critical section for our initial check at the top of the
  // loop.
  //
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);

  while (1)
  {
    //
    // Get the next message off the highest-priority non-empty queue which
    // can interrupt us; if they're all empty, we're done.
    //
    p_msg_t* cur_msg = NULL;
    int old_curpri = curpri;
    for (int pri = HV_MSG_MAXPRI; pri > curpri; pri--)
    {
      if (pmp->head[pri])
      {
        cur_msg = pmp->head[pri];
        pmp->head[pri] = pmp->head[pri]->next;
        curpri = pri;
        break;
      }
    }

    if (!cur_msg)
      break;

    //
    // Device interrupts arrive via IPI, and we need to prevent them when
    // we end up at a higher priority.  (These interrupts are normally
    // masked while in the hypervisor, but we might have enabled them in
    // nap_until_change().)
    //
    uint_reg_t masked_ipi = 0;
    if (curpri >= HV_MSG_PRI_DRVINTR)
    {




      masked_ipi = (1UL << INT_IPI_HV) & ~__insn_mfspr(INTERRUPT_MASK_HV);
      __insn_mtspr(INTERRUPT_MASK_SET_HV, masked_ipi);

    }

    //
    // Get out of the critical section so we can be interrupted during our
    // processing.
    //
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);

    long start_cycle = get_cycle_count();

    if (cur_msg->tag.word == DRV_INTR_DELAYED_TAG)
    {
      int msgwds = cur_msg->len;
      unsigned long* msgbuf = &cur_msg->buf[0];

      assert(msgwds >= 1);

      int chan = (msgbuf[0] & DRV_CHAN_MASK) >> DRV_CHAN_SHIFT;
      if (chan >= sizeof (delayed_intr_table) / sizeof (delayed_intr_table[0]))
        panic("bad channel %d in delayed interrupt message %#lx", chan,
              msgbuf[0]);

      int_disp_t* entry = &delayed_intr_table[chan];

      if (entry->func == NULL)
        panic("unhandled channel %d in delayed interrupt message %#lx",
              chan, msgbuf[0]);

      entry->func(entry->arg, msgbuf, msgwds * sizeof (msgbuf[0]));

      if (config.stats)
      {
        long tot_cycle = get_cycle_count() - start_cycle;
        msg_stats[0].tot_cycles += tot_cycle;
        msg_stats[0].max_cycles =
          max(msg_stats[0].max_cycles, tot_cycle);
        msg_stats[0].num_events++;
      }
    }
    else
    {
      uint32_t msgtype = cur_msg->tag.bits.type;
      uint32_t msgchan = cur_msg->tag.bits.chan;
      int msgwds = cur_msg->len;

      if (msgwds < cur_msg->tag.bits.len)
        panic("message too short; expected %d actual %d tag %#x",
              cur_msg->tag.bits.len, msgwds, cur_msg->tag.word);

      //
      // For requests, first word is always the sender's coordinates.
      //
      pos_t sender = { .word = cur_msg->buf[0] };
      unsigned long* msgbuf = &cur_msg->buf[1];

      long start_cycle = get_cycle_count();

      switch (msgtype)
      {
      case HV_TAG_WRITE_CONSOLE:
        {
          struct hv_msg_write_console* wr_msg =
            (struct hv_msg_write_console*) msgbuf;

          if (my_pos.word != chip_console.word)
            panic("remote console write from (%d,%d) to non-console tile "
                  "(%d,%d)", UXY(sender), UXY(my_pos));

#ifdef LABEL_CONSOLE_OUTPUT
          char outbuf[W2B(HV_MAXMSGWDS) + 16];

          snprintf(outbuf, sizeof (outbuf), "(%d,%d) ", UXY(sender));
          int preflen = strlen(outbuf);
#else
          char outbuf[W2B(HV_MAXMSGWDS)];

          outbuf[0] = '\0';
          const int preflen = 0;
#endif

          int len = wr_msg->len;
          if (len > W2B(msgwds) - sizeof (*wr_msg))
            len = W2B(msgwds) - sizeof (*wr_msg);

#ifdef CONSOLE_DEBUG
          int last_avail = sizeof (last_outbuf) - last_outbuf_pos;
          int last_bytes = len;
          if (last_bytes > last_avail)
            last_bytes = last_avail;

          memcpy(&last_outbuf[last_outbuf_pos], &wr_msg[1], last_bytes);
          flush_range((VA) &last_outbuf[last_outbuf_pos], last_bytes);
          last_outbuf_pos += last_bytes;
          flush_range((VA) &last_outbuf_pos, 4);
#endif

          memcpy(&outbuf[preflen], (char*) &wr_msg[1], len);

          size_t retval;
          if (wr_msg->client_no < 0)
            retval = stdout->ops->write(outbuf, preflen + len, 0, stdout->pvt);
          else if (config.nregclients <= 1)
            retval = client_stdout->ops->write(outbuf, preflen + len, 0,
                                               client_stdout->pvt);
          else
            retval = cons_write_to_output_buffer(wr_msg->client_no,
                                                 outbuf, preflen + len);
          reply(sender, msgchan, msgtype, &retval, sizeof (retval));
        }
        break;

      case BME_TAG_WRITE_CONSOLE:
        {
          unsigned long replytag = *(unsigned long*) msgbuf;

          struct bme_msg_write_console* wr_msg =
            (struct bme_msg_write_console*)msgbuf;

          if (my_pos.word != chip_console.word)
            panic("remote console write from (%d,%d) to non-console tile "
                  "(%d,%d)", UXY(sender), UXY(my_pos));

#ifdef LABEL_CONSOLE_OUTPUT
          char outbuf[W2B(HV_MAXMSGWDS) + 16];

          snprintf(outbuf, sizeof (outbuf), "(%d,%d) ", UXY(sender));
          int preflen = strlen(outbuf);
#else
          char outbuf[W2B(HV_MAXMSGWDS)];

          outbuf[0] = '\0';
          const int preflen = 0;
#endif

          int len = wr_msg->len;

          if (len > W2B(msgwds) - sizeof (*wr_msg))
            len = W2B(msgwds) - sizeof (*wr_msg);

          memcpy(&outbuf[preflen], (char*) &wr_msg[1], len);

          size_t retval = stdout->ops->write(outbuf, preflen + len, 0,
                                             stdout->pvt);

          reply_tag(sender, msgchan, msgtype, &retval, sizeof (retval),
                    replytag);
        }
        break;

      case HV_TAG_READ_CONSOLE:
        {
          struct hv_msg_read_console* rd_msg =
            (struct hv_msg_read_console*) msgbuf;

          struct hv_msg_read_console_reply rpl_msg;

          if (my_pos.word != chip_console.word)
            panic("remote console read from (%d,%d) to non-console tile "
                  "(%d,%d)", UXY(sender), UXY(my_pos));

          int bytes_to_read = rd_msg->len;
          if (bytes_to_read > W2B(HV_MAXMSGWDS) - sizeof (rpl_msg))
            bytes_to_read = W2B(HV_MAXMSGWDS) - sizeof (rpl_msg);

          unsigned long inbuf[B2W_UP(bytes_to_read)];

          int bytes_read;
          if (rd_msg->client_no < 0)
            bytes_read = stdin->ops->read((char*) inbuf, bytes_to_read, 0,
                                          stdin->pvt);
          else
            bytes_read = cons_read_from_input_buffer(rd_msg->client_no,
                                                     (char *) inbuf,
                                                     bytes_to_read);

          rpl_msg.len = bytes_read;
          if (bytes_read < 0)
            bytes_read = 0;

          reply_var(sender, msgchan, msgtype, &rpl_msg, sizeof (rpl_msg),
                    inbuf, bytes_read, 0, 0);
        }
        break;

      case HV_TAG_FLUSH_CONSOLE:
        {
          long ack = 0;
          if (my_pos.word != chip_console.word)
            panic("remote console flush from (%d,%d) to non-console tile "
                  "(%d,%d)", UXY(sender), UXY(my_pos));

          ack = stdout->ops->sync(stdout->pvt);
          reply(sender, msgchan, msgtype, &ack, sizeof (ack));
        }
        break;

      case HV_TAG_MSG_SV:
        {
          struct hv_msg_sv* sv_msg = (struct hv_msg_sv*) msgbuf;

          int len = sv_msg->len;
          if (len > W2B(msgwds) - sizeof (*sv_msg))
            len = W2B(msgwds) - sizeof (*sv_msg);

          long ack = deliver_local_message(sv_msg->source, &sv_msg[1], len);

          reply(sender, msgchan, msgtype, &ack, sizeof (ack));
        }
        break;

      case HV_TAG_START_CLIENT:
        {
          struct hv_msg_start_client* msg =
           (struct hv_msg_start_client*) msgbuf;

          //
          // Configure the client information.
          //
          configure_client_physmem(msg->client_pa, msg->client_len);

          //
          // Save the data in our client structure.
          //
          for (int i = 0; i < MAX_MSHIMS; i++)
          {
            config.clients[my_client].mem_base[i] = msg->client_pa[i];
            config.clients[my_client].mem_len[i] = msg->client_len[i];
          }
          config.clients[my_client].client_entry = msg->client_entry;

          //
          // Set flag to make the code which was waiting for this message start
          // the client when we return.
          //
          start_client_flag = 1;

          long ack = 0;
          reply(sender, msgchan, msgtype, &ack, sizeof (ack));
        }
        break;

      case HV_TAG_DRV_OPEN:
        {
          struct hv_msg_drv_open* msg = (struct hv_msg_drv_open*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];

          struct hv_msg_drv_reply reply;

          if (devp->drv)
          {
            const struct drv_ops* ops = devp->drv->ops;

            reply.retval = ops->open(msg->devhdl, devp->drv_state,
                                     msg->suffix, msg->flags, sender);
          }
          else
            reply.retval = HV_ENODEV;

          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_CLOSE:
        {
          struct hv_msg_drv_close* msg = (struct hv_msg_drv_close*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->close(msg->devhdl,
                                                       devp->drv_state,
                                                       sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_PREAD:
      case HV_TAG_DRV_PREAD_HI:
        {
          struct hv_msg_drv_pread* msg = (struct hv_msg_drv_pread*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          unsigned long buf[B2W_UP(DRV_ATOMIC_LEN)];
          struct hv_msg_drv_reply reply = { ops->pread(msg->devhdl,
                                                       devp->drv_state,
                                                       msg->flags |
                                                         DRV_FLG_HVADDR,
                                                       (char*) buf,
                                                       msg->len,
                                                       msg->offset, sender) };
          int len = (reply.retval > 0) ? reply.retval : 0;
          reply_var(sender, msgchan, msgtype, &reply, sizeof (reply), buf,
                    len, 0, 0);
        }
        break;

      case HV_TAG_DRV_PWRITE:
      case HV_TAG_DRV_PWRITE_HI:
        {
          struct hv_msg_drv_pwrite* msg = (struct hv_msg_drv_pwrite*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->pwrite(msg->devhdl,
                                                        devp->drv_state,
                                                        msg->flags |
                                                          DRV_FLG_HVADDR,
                                                        (char*) &msg[1],
                                                        msg->len,
                                                        msg->offset, sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_POLL:
        {
          struct hv_msg_drv_poll* msg = (struct hv_msg_drv_poll*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->poll(msg->devhdl,
                                                      devp->drv_state,
                                                      msg->events,
                                                      msg->intarg, sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_POLL_CANCEL:
        {
          struct hv_msg_drv_poll_cancel* msg =
            (struct hv_msg_drv_poll_cancel*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->poll_cancel(msg->devhdl,
                                                             devp->drv_state,
                                                             sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_PREADA:
      case HV_TAG_DRV_PREADA_HI:
        {
          struct hv_msg_drv_preada* msg = (struct hv_msg_drv_preada*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->preada(msg->devhdl,
                                                        devp->drv_state,
                                                        msg->flags,
                                                        msg->sgl_len,
                                                        (HV_SGL*) &msg[1],
                                                        msg->offset,
                                                        msg->intarg,
                                                        sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_PWRITEA:
      case HV_TAG_DRV_PWRITEA_HI:
        {
          struct hv_msg_drv_pwritea* msg = (struct hv_msg_drv_pwritea*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          struct hv_msg_drv_reply reply = { ops->pwritea(msg->devhdl,
                                                         devp->drv_state,
                                                         msg->flags,
                                                         msg->sgl_len,
                                                         (HV_SGL*) &msg[1],
                                                         msg->offset,
                                                         msg->intarg,
                                                         sender) };
          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DRV_MSG:
        {
          struct hv_msg_drv_msg* msg = (struct hv_msg_drv_msg*) msgbuf;

          const struct device* devp = &devices[HDL2IDX(msg->devhdl)];
          const struct drv_ops* ops = devp->drv->ops;

          ops->msg(msg->devhdl, devp->drv_state,
                   (drv_reply_msg_token_t) msgchan, &msg[1], msg->msglen,
                   sender);

          // We don't do a reply here; the driver must call drv_reply_msg().
        }
        break;

      case HV_TAG_FLUSH_REMOTE:
        {
          struct hv_msg_flush_remote* msg =
            (struct hv_msg_flush_remote*) msgbuf;

          long retval = handle_flush_remote(msg->cache_pa, msg->cache_control,
                                            msg->tlb_va, msg->tlb_len,
                                            msg->asid, msg->flush_tlb,
                                            msg->page_shift, msg->flush_cache,
                                            msg->flush_asid);

          reply(sender, msgchan, msgtype, &retval, sizeof (retval));
          //
          // If this request did a cache flush, change the message tag so
          // the stats code can count it differently than if it just did a
          // TLB flush.
          //
          if (msg->cache_control)
            msgtype = HV_TAG_FLUSH_REMOTE_CACHE;
        }
        break;

      case HV_TAG_TEST_MEMORY:
        {
          struct hv_msg_test_memory* msg =
            (struct hv_msg_test_memory*) msgbuf;

          struct hv_msg_test_memory_reply reply = {
            .nerrors = handle_test_memory(msg->base_pa, msg->len,
                                          msg->init_mem),
          };

          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_PERMIT_MMIO_ACC:
        {
          struct hv_msg_mmio_access* msg =
            (struct hv_msg_mmio_access*) msgbuf;

          struct hv_msg_mmio_access_reply reply = {
            .retval = permit_mmio_access(msg->shimaddr, msg->start, msg->len),
          };

          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_DENY_MMIO_ACC:
        {
          struct hv_msg_mmio_access* msg =
            (struct hv_msg_mmio_access*) msgbuf;

          struct hv_msg_mmio_access_reply reply = {
            .retval = deny_mmio_access(msg->shimaddr, msg->start, msg->len),
          };

          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_GLOBAL_EXIT:
        {
          struct hv_msg_global_exit* msg =
            (struct hv_msg_global_exit*) msgbuf;

          global_exit(msg->status);
          //
          // We shouldn't actually ever get here...
          //
          long ack = 0;

          reply(sender, msgchan, msgtype, &ack, sizeof (ack));
        }
        break;

      case HV_TAG_PING:
        {
          //
          // Our reply contains the current cycle count so the sender can
          // figure out how long it took the message to get here, and how
          // long the reply took to get back.
          //
          struct hv_msg_ping_reply reply = {
            .cycle = get_cycle_count(),
          };

          reply(sender, msgchan, msgtype, &reply, sizeof (reply));
        }
        break;

      case HV_TAG_NMI:
        {
          struct hv_msg_nmi* msg = (struct hv_msg_nmi*) msgbuf;
          HV_NMI_Info retval;

          if ((sr->ex_context_1 & SPR_EX_CONTEXT_0_1__PL_MASK) > CLIENT_PL)
          {
            retval.result = HV_NMI_RESULT_FAIL_HV;
            retval.pc = 0;
          }
          else if ((sr->ex_context_1 ==
                    (SPR_EX_CONTEXT_2_1__ICS_MASK | CLIENT_PL)) &&
                   !(msg->flags & HV_NMI_FLAG_FORCE))
          {
            retval.result = HV_NMI_RESULT_FAIL_ICS;
            retval.pc = sr->ex_context_0;
          }
	  else
	  {
            retval.result = HV_NMI_RESULT_OK;
            retval.pc = sr->ex_context_0;
            __insn_mtspr(EX_CONTEXT_CL_0, sr->ex_context_0);
            __insn_mtspr(EX_CONTEXT_CL_1, sr->ex_context_1);
            __insn_mtspr(SYSTEM_SAVE_CL_2, msg->info);
            sr->ex_context_0 = __insn_mfspr(INTERRUPT_VECTOR_BASE_CL) +
                              (INT_NMI_DWNCL << 8);
            sr->ex_context_1 = SPR_EX_CONTEXT_2_1__ICS_MASK | CLIENT_PL;
          }

          reply(sender, msgchan, msgtype, &retval, sizeof (retval));
        }
        break;

      default:
        panic("unrecognized message type %#x", msgtype);
      }

      if (config.stats)
      {
        long tot_cycle = get_cycle_count() - start_cycle;
        int msgnum = HV_MSGNUM(msgtype);
        msg_stats[msgnum].tot_cycles += tot_cycle;
        msg_stats[msgnum].max_cycles =
          max(msg_stats[msgnum].max_cycles, tot_cycle);
        msg_stats[msgnum].num_events++;
      }
    }
    //
    // Get back into the critical section so that we can free the message we
    // just processed, and be ready for the check at the top of this loop.
    //
    __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);



    cur_msg->next = pmp->free;
    pmp->free = cur_msg;

    curpri = old_curpri;



    __insn_mtspr(INTERRUPT_MASK_RESET_HV, masked_ipi);

  }

  //
  // There are no more messages above our current priority level waiting to
  // be processed, so we clear intctrl_hv.  (If there are messages at or below
  // our priority waiting to be processed, it must be the case that we
  // interrupted a previous invocation of msg_proc(), since that's the only
  // thing which could have raised our priority level above the default.
  // Thus, those lower-priority messages will eventually be processed when
  // we resume execution of that previous invocation.)
  //
  __insn_mtspr(INTCTRL_HV_STATUS, 0);
  __insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
}


void
handle_msg_intr()
{
  for (int pri = HV_MSG_MAXPRI; pri > curpri; pri--)
  {
    if (p_msg_ptrs.head[pri])
    {
      unmask_intr(INT_INTCTRL_HV);
      mask_intr(INT_INTCTRL_HV);
      return;
    }
  }
}

/** Ping other tiles and print a latency table.  This routine is not
 *  normally called, although we build it by default to prevent bitrot.
 *  The easiest way to use it is to stick this code in hv(), right
 *  before it calls load_and_start_client():
 *
 *    if (is_master)
 *    {
 *      extern void msg_ping_test(void);
 *      msg_ping_test();
 *    }
 */
void
msg_ping_test()
{
  const int npings = 1000;

  printf("        %8s %8s %8s %8s %8s %8s\n",
         "min_send", "max_send", "avg_send",
         "min_recv", "max_recv", "avg_recv");

  for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
  {
    for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };

      //
      // Don't ping ourselves.
      //
      if (tile.word == my_pos.word)
        continue;

      //
      // Initialize the stats.
      //
      uint_reg_t min_send_delay = ~0;
      uint_reg_t min_recv_delay = ~0;
      uint_reg_t max_send_delay = 0;
      uint_reg_t max_recv_delay = 0;
      uint_reg_t tot_send_delay = 0;
      uint_reg_t tot_recv_delay = 0;

      struct hv_msg_ping ping_msg = { 0 };
      struct hv_msg_ping_reply reply = { 0 };

      //
      // Send some pings, update stats.
      //
      for (int i = 0; i < npings; i++)
      {
        uint_reg_t start_cycle = get_cycle_count();
        send_receive(tile, HV_TAG_PING, &ping_msg, sizeof (ping_msg),
                     &reply, sizeof (reply), NULL, 0);
        uint_reg_t end_cycle = get_cycle_count();

        uint_reg_t send_delay = reply.cycle - start_cycle;
        uint_reg_t recv_delay = end_cycle - reply.cycle;

        min_send_delay = min(min_send_delay, send_delay);
        max_send_delay = max(max_send_delay, send_delay);
        tot_send_delay += send_delay;
        min_recv_delay = min(min_recv_delay, recv_delay);
        max_recv_delay = max(max_recv_delay, recv_delay);
        tot_recv_delay += recv_delay;
      }

      //
      // Print stats.
      //
      printf("(%d,%d) %8llu %8llu %8llu %8llu %8llu %8llu\n",
             x, y, min_send_delay, max_send_delay, tot_send_delay / npings,
             min_recv_delay, max_recv_delay, tot_recv_delay / npings);
    }
  }
}
