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
 * Hypervisor messaging definitions.
 */

#ifndef _SYS_HV_MSG_H
#define _SYS_HV_MSG_H

#include "bits.h"
#include "drvintf.h"
#include "hv.h"
#include "misc.h"
#include "msgtag.h"
#include "tile.h"


/** Initialize the messaging subsystem.
 */
void init_msg(void);

/** Interrupt routine called when messages are available.
 * @param int_number Number of the interrupt.
 * @param sr Saved user registers.
 */
void msg_avail(int int_number, struct saved_regs* sr);

/** Interrupt routine called to process pending messages.  Note that this
 *  routine enables the IDN_CA interrupt if it was previously disabled.
 * @param int_number Number of the interrupt.
 * @param sr Saved user registers.
 */
void msg_proc(int int_number, struct saved_regs* sr);

/** Interrupt routine called to process IPI interrupts.
 * @param int_number Number of the interrupt.
 * @param sr Saved user registers.
 */
void ipi_proc(int int_number, struct saved_regs* sr);

/** Send a request.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number
 *        of words.
 * @param replychanp The channel on which the reply will be received is written
 *        here.
 * @param replybuf Buffer for the reply.  Can be NULL in which case the reply
 *        is not saved.
 * @param replybuflen Length of the reply buffer, in bytes.
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
#define send(dest, msgtype, msg, msglen, replychanp, replybuf, \
             replybuflen, flags) \
        send_var(dest, msgtype, msg, msglen, NULL, 0, replychanp, replybuf, \
                 replybuflen, flags)

/** Send a request with a variable-length component.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number
 *        of words.
 * @param buf Variable-length data; need not be word-aligned.
 * @param buflen Length of variable-length data, in bytes.
 * @param replychanp The channel on which the reply will be received is written
 *        here.
 * @param replybuf Buffer for the reply.  Can be NULL in which case the reply
 *        is not saved.
 * @param replybuflen Length of the reply buffer, in bytes.
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
int send_var(pos_t dest, uint32_t msgtype, void* msg, size_t msglen, void* buf,
             size_t buflen, uint32_t* replychanp, void* replybuf,
             size_t replybuflen, uint32_t flags);

/** Retrieve the reply message for a particular channel and free the channel.
 *  This routine blocks until a message is received.
 * @param replychan Channel number.
 * @param replylenp The length of the reply, in bytes, is written here.  Note
 *        that a pointer to the reply itself is not returned as that was
 *        supplied by the caller in the send() or send_var() call.
 * @param notimeout If nonzero, don't warn on message timeouts.
 * @return The message type of the reply.
 */
uint32_t getreply(uint32_t replychan, size_t* replylenp, int notimeout);

/** Send a reply.
 * @param dest Destination of the reply.
 * @param chan Channel for the reply.
 * @param msgtype Type of the reply.
 * @param msg The reply message; must be word-aligned.
 * @param msglen The length of the reply message, in bytes; must be an even
 *        number of words.
 */
#define reply(dest, chan, msgtype, msg, msglen) \
        reply_var(dest, chan, msgtype, msg, msglen, NULL, 0, 0, 0)

/** Send a reply with a specified tag.
 * @param dest Destination of the reply.
 * @param chan Channel for the reply.
 * @param msgtype Type of the reply.
 * @param msg The reply message; must be word-aligned.
 * @param msglen The length of the reply message, in bytes; must be an even
 *        number of words.
 * @param tag Tag to use.
 */
#define reply_tag(dest, chan, msgtype, msg, msglen, tag) \
        reply_var(dest, chan, msgtype, msg, msglen, NULL, 0, \
                  MSG_FLG_SENDTAG, tag)

/** Send a reply with a variable-length component.
 * @param dest Destination of the reply.
 * @param chan Channel for the reply.
 * @param msgtype Type of the reply.
 * @param msg The reply data.
 * @param msglen The length of the reply message, in bytes; must be an even
 *        number of words.
 * @param buf Variable-length data; need not be word-aligned.
 * @param buflen Length of variable-length data, in bytes.
 * @param flags Flags (MSG_FLG_xxxx).  Currently only MSG_FLG_SENDTAG is
 *        valid for reply_var().
 * @param send_tag Tag to be used if MSG_FLG_SENDTAG is on.
 */
void reply_var(pos_t dest, uint32_t chan, uint32_t msgtype, void* msg,
               size_t msglen, void* buf, size_t buflen, int flags,
               uint32_t send_tag);

/** Send a request, and return the reply.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number 
 *        of words.
 * @param replybuf Buffer for the reply; must be word-aligned.  Can be NULL in
 *        which case the reply is not saved, although its length will still be
 *        checked (see below).
 * @param replybuflen Length of the reply buffer, in bytes; must be an even
 *        number of words.  The length of the reply must exactly equal
 *        replybuflen unless MSG_FLG_SHORTREPLY is set in flags.
 * @param replylenp The length of the received reply, in bytes, is written
 *        here.  Can be NULL in which case the length is not returned, 
 *        although it is still checked (see above).
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
#define send_receive(dest, msgtype, msg, msglen, replybuf, replybuflen, \
                     replylenp, flags) \
        send_receive_var(dest, msgtype, msg, msglen, NULL, 0, replybuf, \
                         replybuflen, replylenp, flags)

/** Send a request with a variable-length component, and return the reply.
 * @param dest Destination tile.
 * @param msgtype Message type.
 * @param msg The message to send; must be word-aligned.
 * @param msglen The length of the message, in bytes; must be an even number
 *        of words.
 * @param buf Variable-length data; need not be word-aligned.
 * @param buflen Length of variable-length data, in bytes.
 * @param replybuf Buffer for the reply; must be word-aligned.  Can be NULL in
 *        which case the reply is not saved, although its length will still be
 *        checked (see below).
 * @param replybuflen Length of the reply buffer, in bytes; must be an even
 *        number of words.  The length of the reply must exactly equal
 *        replybuflen unless MSG_FLG_SHORTREPLY is set in flags.
 * @param replylenp The length of the received reply, in bytes, is written
 *        here.  Can be NULL in which case the length is not returned,
 *        although it is still checked (see above).
 * @param flags Flags (MSG_FLG_xxx).
 * @return If MSG_FLG_XMITFAIL is set in flags, and the message cannot be
 *         transmitted because the IDN is already busy, 1 will be returned.
 *         Otherwise, any error results in a panic, and succesful completion
 *         returns 0.
 */
int send_receive_var(pos_t dest, uint32_t msgtype, void* msg, size_t msglen,
                     void* buf, size_t buflen, void* replybuf,
                     size_t replybuflen, size_t* replylenp, uint32_t flags);

/** Return error code on transmit failure (due to a busy IDN), don't panic */
#define MSG_FLG_XMITFAIL    0x01
/** Send to IDN 0 on dest */
#define MSG_FLG_SENDIDN0    0x02
/** Send to IDN 0 on dest with boot tag */
#define MSG_FLG_SENDBOOT    0x04
/** Send with specified tag (reply only) */
#define MSG_FLG_SENDTAG     0x08
/** It's OK if the reply is shorter than the provided buffer */
#define MSG_FLG_SHORTREPLY  0x10

/** Register a handler for an interrupt.
 * @param func Function which will be called when the interrupt occurs.
 * @param arg Argument which will be passed to the called interrupt handler.
 * @param type Type of the called interrupt handler (DRV_INTR_xxx).
 * @param chan Interrupt channel number which this handler will handle.
 * @return Zero if the handler was added successfully, nonzero if not.
 */
int register_intr(drv_intr_func* func, void* arg, int type, int chan);

/** Wait until a value in memory changes.  While waiting, process incoming
 *  messages whose priorities are higher than our current priority, unless
 *  the no_yield parameter is set.
 * @param valptr Pointer to target value.
 * @param valsize Size in bytes of value (1, 2, 4, or 8).
 * @param curval Value which *valptr currently has; when *valptr is different
 *        from this value, the function will return.
 * @param flags Flag bitmask (see NUC_FLG_xxx below).
 * @param msgtype Type of message being waited for
 * @param dest Destination of message being waited for
 * @param caller Caller PC, normally passed as __builtin_return_address(0).
 */
void nap_until_change(void* valptr, int valsize, uint64_t curval, int flags,
                      int msgtype, int dest, void* caller);

/** Do not process incoming messages while waiting for the value to change.
 * (Incoming _replies_ will be still processed, since they may very well be
 * what we're waiting for.)
 */
#define NUC_FLG_NO_YIELD	1

/** Do not time out the message, since it may take a long time. */
#define NUC_FLG_NO_TIMEOUT	2

/** Handle message interrupts if appropriate; called while waiting for a
 *  spinlock.
 */
void handle_msg_intr(void);

//
// Messages handled by the hypervisor.
//

/** Configure tile.
 */
struct hv_msg_config
{
  struct slave_tile_state sts;  /**< Initial state for slave tile */
};
/** Configure tile tag */
#define HV_TAG_CONFIG              HV_MKTAG(1, HV_MSG_PRI_INIT)

/** Start a client.
 */
struct hv_msg_start_client
{
  /** Base client physical address on each shim */
  PA client_pa[MAX_MSHIMS];
  /** Size of client physical memory on each shim */
  PA client_len[MAX_MSHIMS];
  /** Client's entry point */
  CPA client_entry;
};
/** Start client tag */
#define HV_TAG_START_CLIENT        HV_MKTAG(2, HV_MSG_PRI_INIT)


/** Request to write to the console.
 */
struct hv_msg_write_console
{
  /** Number of bytes to write; these follow this message. */
  size_t len;
  /** Client number that is performing the write, or -1 if the write comes
   *  from the hypervisor itself. */
  int client_no;
};
/** Console write tag */
#define HV_TAG_WRITE_CONSOLE       HV_MKTAG(3, HV_MSG_PRI_CONSOLE)


/** Request to read from the console.
 */
struct hv_msg_read_console
{
  /** Number of bytes to attempt to read. */
  size_t len;
  /** Client number that is requesting the read, or -1 if the read comes
   *  from the hypervisor itself. */
  int client_no;
};
/** Console read tag */
#define HV_TAG_READ_CONSOLE        HV_MKTAG(4, HV_MSG_PRI_CONSOLE)


/** Data read from the console.
 */
struct hv_msg_read_console_reply
{
  size_t len;    /**< Number of bytes read; these follow this message. */
};
// No tag, since it's a reply to a HV_TAG_READ_CONSOLE message.


/** Supervisor-supervisor message.
 */
struct hv_msg_sv
{
  size_t len;    /**< Number of bytes in message; these follow this message. */
  uint32_t source; /**< Source of the message (HV_MSG_xxx). */
};
/** Supervisor message tag */
#define HV_TAG_MSG_SV              HV_MKTAG(5, HV_MSG_PRI_SVMSG)


/** Process a driver open request.
 */
struct hv_msg_drv_open
{
  long devhdl;                     /**< Device handle */
  char suffix[DRV_NAME_MAX];       /**< Device name suffix */
  uint32_t flags;                  /**< Flags */
};
/** Driver open tag */
#define HV_TAG_DRV_OPEN            HV_MKTAG(6, HV_MSG_PRI_DRVREQ)

/** Process a driver close request.
 */
struct hv_msg_drv_close
{
  long devhdl;                     /**< Device handle */
};
/** Driver close tag */
#define HV_TAG_DRV_CLOSE           HV_MKTAG(7, HV_MSG_PRI_DRVREQ)

/** Process a driver pread request.
 */
struct hv_msg_drv_pread
{
  long devhdl;                     /**< Device handle */
  uint32_t flags;                  /**< Flags */
  uint32_t len;                    /**< Length */
  uint64_t offset;                 /**< Offset */
};
/** Driver read tag */
#define HV_TAG_DRV_PREAD           HV_MKTAG(8, HV_MSG_PRI_DRVREQ)
/** Driver read tag, high priority */
#define HV_TAG_DRV_PREAD_HI        HV_MKTAG(8, HV_MSG_PRI_DRVREQ_HI)

/** Process a driver pwrite request.
 */
struct hv_msg_drv_pwrite
{
  long devhdl;                     /**< Device handle */
  uint32_t flags;                  /**< Flags */
  uint32_t len;                    /**< Length */
  uint64_t offset;                 /**< Offset */
};
/** Driver write tag */
#define HV_TAG_DRV_PWRITE          HV_MKTAG(9, HV_MSG_PRI_DRVREQ)
/** Driver write tag, high priority */
#define HV_TAG_DRV_PWRITE_HI       HV_MKTAG(9, HV_MSG_PRI_DRVREQ_HI)

/** Process a driver poll request.
 */
struct hv_msg_drv_poll
{
  long devhdl;                     /**< Device handle */
  uint32_t events;                 /**< Events to poll for */
  uint32_t intarg;                 /**< Interrupt argument */
};
/** Driver poll tag */
#define HV_TAG_DRV_POLL            HV_MKTAG(10, HV_MSG_PRI_DRVREQ)

/** Process a driver poll_cancel request.
 */
struct hv_msg_drv_poll_cancel
{
  long devhdl;                     /**< Device handle */
};
/** Driver poll cancel tag */
#define HV_TAG_DRV_POLL_CANCEL     HV_MKTAG(11, HV_MSG_PRI_DRVREQ)

/** Process a driver preada request.
 */
struct hv_msg_drv_preada
{
  long devhdl;                     /**< Device handle */
  uint32_t flags;                  /**< Flags */
  uint32_t sgl_len;                /**< Length of scatter-gather list, which
                                        follows structure */
  uint64_t offset;                 /**< Offset */
  uint32_t intarg;                 /**< Interrupt argument */
};
/** Driver async read tag */
#define HV_TAG_DRV_PREADA          HV_MKTAG(12, HV_MSG_PRI_DRVREQ)
/** Driver async read tag, high priority */
#define HV_TAG_DRV_PREADA_HI       HV_MKTAG(12, HV_MSG_PRI_DRVREQ_HI)

/** Process a driver pwritea request.
 */
struct hv_msg_drv_pwritea
{
  long devhdl;                     /**< Device handle */
  uint32_t flags;                  /**< Flags */
  uint32_t sgl_len;                /**< Length of scatter-gather list, which
                                        follows structure */
  uint64_t offset;                 /**< Offset */
  uint32_t intarg;                 /**< Interrupt argument */
};
/** Driver async write tag */
#define HV_TAG_DRV_PWRITEA         HV_MKTAG(13, HV_MSG_PRI_DRVREQ)
/** Driver async write tag, high priority */
#define HV_TAG_DRV_PWRITEA_HI      HV_MKTAG(13, HV_MSG_PRI_DRVREQ_HI)

/** Reply for a driver request.
 */
struct hv_msg_drv_reply
{
  long retval;                     /**< Return value */
};
// No tag, since it's a reply to a HV_TAG_DRV_xxx message.

/** Process a driver message request.
 */
struct hv_msg_drv_msg
{
  long devhdl;                     /**< Device handle */
  size_t msglen;                   /**< Message length; message follows */
};
/** Driver message tag */
#define HV_TAG_DRV_MSG             HV_MKTAG(14, HV_MSG_PRI_DRVMSG)

/** Reply for a driver message.
 */
struct hv_msg_drv_msg_reply
{
  long retval;                     /**< Return value */
  size_t replylen;                 /**< Length of reply; reply follows */
};
// No tag, since it's a reply to a HV_TAG_DRV_MSG message.

/** Handle a remote flush request.
 */
struct hv_msg_flush_remote
{
  PA cache_pa;             /**< PA for cache flush */
  unsigned long cache_control; /**< Length and control bits for cache flush */
  VA tlb_va;               /**< VA for TLB flush */
  unsigned long tlb_len;   /**< Length for TLB flush */
  Asid asid;               /**< ASID for asid flush */
  uint8_t page_shift;      /**< Log2 page size for TLB flush */
#ifndef __BIG_ENDIAN__
  uint8_t flush_tlb:1;     /**< Flush the TLB? */
  uint8_t flush_cache:1;   /**< Flush the L2$? */
  uint8_t flush_asid:1;    /**< Flush an ASID? */
#else   // __BIG_ENDIAN__
  uint8_t flush_asid:1;    /**< Flush an ASID? */
  uint8_t flush_cache:1;   /**< Flush the L2$? */
  uint8_t flush_tlb:1;     /**< Flush the TLB? */
#endif
};
/** Remote flush tag */
#define HV_TAG_FLUSH_REMOTE        HV_MKTAG(15, HV_MSG_PRI_FLUSH)

/** Perform a memory test.
 */
struct hv_msg_test_memory
{
  PA base_pa;           /**< PA of first byte to test */
  PA len;               /**< Number of bytes to test */
  uint32_t init_mem;    /**< Init the memory, not test */
};
/** Memory test tag */
#define HV_TAG_TEST_MEMORY         HV_MKTAG(16, HV_MSG_PRI_INIT)

/** Reply for a memory test request.
 */
struct hv_msg_test_memory_reply
{
  long nerrors;                    /**< Number of errors found during test */
};
// No tag, since it's a reply to a HV_TAG_TEST_MEMORY message.

/** Perform an MMIO permit/deny operation.
 */
struct hv_msg_mmio_access
{
  pos_t shimaddr;       /**< I/O shim to which accesses will be made */
  PA start;             /**< First valid byte of MMIO range */
  PA len;               /**< Number of bytes in the MMIO range */
};
/** MMIO permit tag */
#define HV_TAG_PERMIT_MMIO_ACC     HV_MKTAG(17, HV_MSG_PRI_DRVREQ)

/** MMIO deny tag */
#define HV_TAG_DENY_MMIO_ACC       HV_MKTAG(18, HV_MSG_PRI_DRVREQ)

/** Reply for an MMIO access test request.
 */
struct hv_msg_mmio_access_reply
{
  int retval;                      /**< Error code */
  int dummy;                       /**< Ensure struct at least a word long */
};
// No tag, since it's a reply to a HV_TAG_{PERMIT,DENY}_MMIO_ACC message.

/** Perform a global exit operation.
 */
struct hv_msg_global_exit
{
  int status;           /**< Exit status; nonzero for a panic */
  int dummy;            /**< Ensure struct at least a word long */
};
/** Global exit tag */
#define HV_TAG_GLOBAL_EXIT         HV_MKTAG(19, HV_MSG_PRI_CONSOLE)

/** Remote flush tag, cache flush.  This is a pseudo-tag, allocated just
 *  for statistics purposes; it is never sent in a message. */
#define HV_TAG_FLUSH_REMOTE_CACHE  HV_MKTAG(20, HV_MSG_PRI_FLUSH)

/** Ping a remote tile.
 */
struct hv_msg_ping
{
  long dummy;           /**< Dummy value */
};
/** Ping tag */
#define HV_TAG_PING                HV_MKTAG(21, HV_MSG_PRI_INIT)

/** Reply for a ping request.
 */
struct hv_msg_ping_reply
{
  uint_reg_t cycle;     /**< Cycle counter value */
};
// No tag, since it's a reply to a HV_TAG_PING message.

/** Request to flush the console.
 */
struct hv_msg_flush_console
{
  /** Client number that is performing the flush, or -1 if the flush comes
   *  from the hypervisor itself. */
  int client_no;
  /** Ensure struct at least a word long */
  int dummy;
};
/** Console flush tag */
#define HV_TAG_FLUSH_CONSOLE       HV_MKTAG(22, HV_MSG_PRI_CONSOLE)

/** Message to trigger an NMI.
 */
struct hv_msg_nmi
{
  unsigned long info;   /**< NMI info */
  uint64_t flags;       /**< Flags */
};

/** Trigger an NMI on a remote tile. */
#define HV_TAG_NMI                 HV_MKTAG(23, HV_MSG_PRI_SVMSG)

// Note: when adding a new tag, you must update HV_MAX_TAG and
// MSG_NAME_TBL, below.

/** Largest tag number.  Note that this is not itself a tag. */
#define HV_MAX_TAG 24

extern struct hv_stats msg_stats[];

/** Message name table. */
#define MSG_NAME_TBL \
static const char* const msg_names[HV_MAX_TAG + 1] = \
{ \
  "delayed_intr",        /* 0  */ \
  "config",              /* 1  */ \
  "start_client",        /* 2  */ \
  "write_console",       /* 3  */ \
  "read_console",        /* 4  */ \
  "msg_sv",              /* 5  */ \
  "drv_open",            /* 6  */ \
  "drv_close",           /* 7  */ \
  "drv_pread",           /* 8  */ \
  "drv_pwrite",          /* 9  */ \
  "drv_poll",            /* 10 */ \
  "drv_poll_cancel",     /* 11 */ \
  "drv_preada",          /* 12 */ \
  "drv_pwritea",         /* 13 */ \
  "drv_msg",             /* 14 */ \
  "flush_remote:tlb",    /* 15 */ \
  "test_memory",         /* 16 */ \
  "permit_mmio_acc",     /* 17 */ \
  "deny_mmio_acc",       /* 18 */ \
  "global_exit",         /* 19 */ \
  "flush_remote:cache",  /* 20 */ \
  "ping",                /* 21 */ \
  "flush_console",       /* 22 */ \
  "nmi",                 /* 23 */ \
};

#endif /* _SYS_HV_MSG_H */
