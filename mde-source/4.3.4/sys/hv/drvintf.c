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
 * Device driver interface routines.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <arch/cycle.h>
#include <arch/interrupts.h>

#include <hv/iorpc.h>

#include "sys/libc/include/util.h"

#include "board_info.h"
#include "client_msg.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "fastio.h"
#include "fault.h"
#include "gpio_acc.h"
#include "hv.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "idn.h"
#include "lock.h"
#include "mapping.h"
#include "mshim_acc.h"
#include "msg.h"
#include "tlb.h"

//
// Routines to be used when a driver does not support a function.
//
/** No open routine. */
int
no_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
        pos_t tile)
{
  return HV_ENOTSUP;
}

/** No close routine. */
int
no_close(int devhdl, void* statep, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No pread routine. */
int
no_pread(int devhdl, void* statep, uint32_t flags, char* va, uint32_t len,
         uint64_t offset, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No pwrite routine. */
int
no_pwrite(int devhdl, void* statep, uint32_t flags, char* va, uint32_t len,
          uint64_t offset, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No poll routine. */
int
no_poll(int devhdl, void* statep, uint32_t flags, uint32_t intarg, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No poll_cancel routine. */
int
no_poll_cancel(int devhdl, void* statep, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No preada routine. */
int
no_preada(int devhdl, void* statep, uint32_t flags,  uint32_t sgl_len,
          HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No pwritea routine. */
int
no_pwritea(int devhdl, void* statep, uint32_t flags,  uint32_t sgl_len,
           HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg, pos_t tile)
{
  return HV_ENOTSUP;
}

/** No msg routine. */
void
no_msg(int devhdl, void* statep, drv_reply_msg_token_t token,
       void* msg, int msglen, pos_t tile)
{
  drv_reply_msg(token, HV_ENOTSUP, NULL, 0, tile);
}

/** No service routine. */
int
no_service(void* statep)
{
  return HV_ENOTSUP;
}


/** No get current frequency routine. */
long
no_get_cur_freq(const struct dev_info* info, int clock_index)
{
  return HV_ENOTSUP;
}

/** No get desired frequency routine. */
long
no_get_desired_freq(const struct dev_info* info, int clock_index)
{
  return HV_ENOTSUP;
}

/** No set frequency routine. */
int
no_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  return HV_ENOTSUP;
}


//
// Routines to be used when a driver does not need to do anything to support
// a function.
//
/** Null probe routine. */
int
null_probe(const char* drvname, int instance, pos_t tile,
           const struct dev_info* info)
{
  return 0;
}

/** Null init routine. */
int
null_init(const char* drvname, void** statepp, int instance, int tileno,
          pos_t tile, const struct dev_info* info, const char* args)
{
  return 0;
}

/** Null close routine. */
int
null_close(int devhdl, void* statep, pos_t tile)
{
  return 0;
}

/** Null close_all routine. */
int
null_close_all(int devhdl, void* statep)
{
  return 0;
}


//
// Driver support routines.
//
void*
drv_state_alloc(int size, int align)
{
  void* retval = local_alloc(size, align);
  if (!retval)
    panic("drv_state_alloc: couldn't get %d bytes for caller at %p",
          size, __builtin_return_address(0));
  return (retval);
}


void*
drv_state_zalloc(int size, int align)
{
  void* retval = local_alloc(size, align);
  if (!retval)
    panic("drv_state_zalloc: couldn't get %d bytes for caller at %p",
          size, __builtin_return_address(0));
  memset(retval, 0, size);
  return (retval);
}


void*
drv_shared_state_alloc(int size, int align)
{
  void* retval = shared_alloc(size, align);
  if (!retval)
    panic("drv_shared_state_alloc: couldn't get %d bytes for caller at %p",
          size, __builtin_return_address(0));
  return (retval);
}


void*
drv_shared_state_zalloc(int size, int align)
{
  void* retval = shared_alloc(size, align);
  if (!retval)
    panic("drv_shared_state_zalloc: couldn't get %d bytes for caller at %p",
          size, __builtin_return_address(0));
  memset(retval, 0, size);
  return (retval);
}


void*
drv_client_alloc(int size, int align, int readonly, int superonly,
                 VA* client_va)
{
  void* retval = client_shared_alloc(size, align, readonly, superonly,
                                     client_va);
  assert (retval);
  return (retval);
}


void*
drv_client_zalloc(int size, int align, int readonly, int superonly,
                  VA* client_va)
{
  void* retval = client_shared_alloc(size, align, readonly, superonly,
                                     client_va);
  assert (retval);
  memset(retval, 0, size);
  return (retval);
}


int
drv_map_dtlb_page(PA page_start, int size, uint32_t flags, Lotar lotar,
                  VA* va)
{
  uint32_t mode = (flags >> DRV_MAP_MODE_SHIFT) & DRV_MAP_MODE_RMASK;

  // Compatibility with older API where a 0 flag meant locally cached.
  if (mode == 0)
    mode = DRV_MAP_MODE_CACHE_NO_L3;
  
  // Verify that 'size' is a valid hardware page size.
  if (__insn_pcnt(size) != 1)
    return HV_EINVAL;
  int ps = TTE_SHIFT_TO_PS(__insn_ctz(size));
  if (ps < TTE_PS_MIN || ps > TTE_PS_MAX)
    return HV_EINVAL;

  // Check PA alignment.
  if (page_start & (size - 1))
    return HV_EINVAL;

  if (!is_dedicated)
    return HV_ENOTSUP;

  *va = get_virt(size, size);

  // Our mode flags are compatible with the HV_PTE_MODE flags, so we
  // can pass them straight through.
  return install_wired_mapping(*va, page_start, ps, mode, lotar);
}


int
drv_unmap_dtlb_page(VA va, int size, uint32_t flags)
{
  if (!is_dedicated)
    return HV_ENOTSUP;

  if (remove_wired_tte_va(va))
    return HV_ENOENT;

  free_virt(va, size);

  return 0;
}


int
drv_allow_client_pte_lotar(pos_t position, Lotar* lotar)
{
  return allow_client_pte_lotar(position, lotar);
}


int
drv_deliver_intr(pos_t tile, uint32_t intarg, uint32_t intdata)
{
  HV_IntrMsg im;

  im.intarg = intarg;
  im.intdata = intdata;

  return (send_sv_message(tile, HV_MSG_INTR, (void*) &im, sizeof (im)));
}


int
drv_register_intr(drv_intr_func* func, void* arg, int type, int chan)
{
  return (register_intr(func, arg, type, chan));
}


int
drv_copy_to_client(char* client_va, char* hv_va, int len, uint32_t flags)
{
  if (flags & DRV_FLG_HVADDR)
    memcpy(client_va, hv_va, len);
  else
  {
    ON_FAULT_RETURN_EFAULT(client_va, len);
    memcpy(client_va, hv_va, len);
    FAULT_END();
  }

  return (0);
}


int
drv_copy_from_client(char* hv_va, char* client_va, int len, uint32_t flags)
{
  if (flags & DRV_FLG_HVADDR)
    memcpy(hv_va, client_va, len);
  else
  {
    ON_FAULT_RETURN_EFAULT(client_va, len);
    memcpy(hv_va, client_va, len);
    FAULT_END();
  }

  return (0);
}


#if DRV_MAX_TILE_INDEX < HV_TILES
#error DRV_MAX_TILE_INDEX needs to be at least HV_TILES
#endif

uint32_t
drv_tile2index(pos_t tile)
{
  return (POS2IDX(tile));
}


pos_t
drv_index2tile(uint32_t idx)
{
  return (IDX2POS(idx));
}


uint32_t
drv_cpa2pa(CPA client_pa, CPA len, PA* real_pa)
{
  return (c2r_pa(client_pa, len, real_pa));
}


int drv_c2r_lotar(Lotar client_lotar, Lotar* real_lotar)
{
  return (c2r_lotar(client_lotar, real_lotar));
}


int drv_c2r_pte_lotar(Lotar client_lotar, Lotar* real_lotar)
{
  return (c2r_pte_lotar(client_lotar, real_lotar));
}


void
drv_nap_until_change(void* valptr, int valsize, uint32_t curval)
{
  // Don't time out, since this may be used for a driver to wait
  // for a status change on some piece of state that won't change
  // until a request is made for the state to change.
  nap_until_change(valptr, valsize, curval, NUC_FLG_NO_TIMEOUT,
                   0xDDDD, 0, __builtin_return_address(0));
}


void
drv_yield()
{
  unmask_intr(INT_INTCTRL_HV);
  mask_intr(INT_INTCTRL_HV);
}


int
drv_open_remote(int devhdl, const char* suffix, uint32_t flags, pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_open msg =
  {
    .devhdl = devhdl,
    .flags = flags,
  };
  strcpy(msg.suffix, suffix);

  send_receive(rem_tile, HV_TAG_DRV_OPEN, &msg, sizeof (msg), &rpl,
               sizeof (rpl), NULL, 0);
  return (rpl.retval);
}


int
drv_close_remote(int devhdl, pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_close msg =
  {
    .devhdl = devhdl,
  };

  send_receive(rem_tile, HV_TAG_DRV_CLOSE, &msg, sizeof (msg), &rpl,
               sizeof (rpl), NULL, 0);
  return (rpl.retval);
}


int
drv_pread_remote(int devhdl, uint32_t flags, char* va,
                 uint32_t len, uint64_t offset, pos_t rem_tile)
{
  int retval = 0;
  union
  {
    struct hv_msg_drv_reply reply;
    unsigned long words[B2W_UP(DRV_ATOMIC_LEN +
                               sizeof (struct hv_msg_drv_reply))];
  } buf;
  struct hv_msg_drv_pread msg =
  {
    .devhdl = devhdl,
  };

  uint32_t msgtag =
    (flags & DRV_FLG_2NDHOP) ? HV_TAG_DRV_PREAD_HI : HV_TAG_DRV_PREAD;

  while (len)
  {
    int pass_len = len;
    if (pass_len > DRV_ATOMIC_LEN)
      pass_len = DRV_ATOMIC_LEN;

    msg.offset = offset;

    if (pass_len < len)
      msg.flags = flags | DRV_FLG_PARTIAL;
    else
      msg.flags = flags;

    msg.len = pass_len;
    size_t buflen;

    send_receive(rem_tile, msgtag, &msg, sizeof (msg), buf.words,
                 sizeof (buf.words), &buflen, MSG_FLG_SHORTREPLY);

    struct hv_msg_drv_reply* rplp = &buf.reply;
    assert(buflen >= sizeof (*rplp));

    int pass_retval = rplp->retval;
    if (pass_retval < 0)
    {
      retval = pass_retval;
      break;
    }

    assert(pass_retval <= buflen - sizeof (*rplp));

    if (flags & DRV_FLG_HVADDR)
      memcpy(va, &rplp[1], pass_retval);
    else
    {
      if (FAULT_BEGIN(va, pass_retval))
      {
        msg.len = 0;
        msg.flags = flags | DRV_FLG_FAULT;

        send_receive(rem_tile, msgtag, &msg, sizeof (msg), NULL,
                     sizeof (struct hv_msg_drv_reply), NULL, 0);

        retval = HV_EFAULT;
        break;
      }
      memcpy(va, &rplp[1], pass_retval);
      FAULT_END();
    }

    len -= pass_retval;
    offset += pass_retval;
    retval += pass_retval;
    va += pass_retval;

    //
    // We exit early for a short read.
    //
    if (pass_retval < pass_len)
      break;
  }

  return (retval);
}


int
drv_pwrite_remote(int devhdl, uint32_t flags, char* va,
                  uint32_t len, uint64_t offset, pos_t rem_tile)
{
  int retval = 0;
  unsigned long buf[B2W_UP(DRV_ATOMIC_LEN)];
  struct hv_msg_drv_pwrite msg =
  {
    .devhdl = devhdl,
  };

  uint32_t msgtag =
    (flags & DRV_FLG_2NDHOP) ? HV_TAG_DRV_PWRITE_HI : HV_TAG_DRV_PWRITE;

  while (len)
  {
    int pass_len = len;
    if (pass_len > DRV_ATOMIC_LEN)
      pass_len = DRV_ATOMIC_LEN;

    msg.offset = offset;

    if (flags & DRV_FLG_HVADDR)
      memcpy(buf, va, pass_len);
    else
    {
      if (FAULT_BEGIN(va, pass_len))
      {
        msg.len = 0;
        msg.flags = flags | DRV_FLG_FAULT;

        send_receive(rem_tile, msgtag, &msg, sizeof (msg), NULL,
                     sizeof (struct hv_msg_drv_reply), NULL, 0);

        retval = HV_EFAULT;
        break;
      }
      memcpy(buf, va, pass_len);
      FAULT_END();
    }

    if (pass_len < len)
      msg.flags = flags | DRV_FLG_PARTIAL;
    else
      msg.flags = flags;

    msg.len = pass_len;

    struct hv_msg_drv_reply rpl;

    send_receive_var(rem_tile, msgtag, &msg, sizeof (msg), buf,
                     pass_len, &rpl, sizeof (rpl), NULL, 0);

    int pass_retval = rpl.retval;

    if (pass_retval < 0)
    {
      retval = pass_retval;
      break;
    }

    len -= pass_retval;
    offset += pass_retval;
    retval += pass_retval;
    va += pass_retval;

    //
    // We exit early for a short write.
    //
    if (pass_retval < pass_len)
      break;
  }

  return (retval);
}


int
drv_poll_remote(int devhdl, uint32_t events, uint32_t intarg,
                pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_poll msg =
  {
    .devhdl = devhdl,
    .events = events,
    .intarg = intarg,
  };

  send_receive(rem_tile, HV_TAG_DRV_POLL, &msg, sizeof (msg), &rpl,
               sizeof (rpl), NULL, 0);
  return (rpl.retval);
}


int
drv_poll_cancel_remote(int devhdl, pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_poll_cancel msg =
  {
    .devhdl = devhdl,
  };

  send_receive(rem_tile, HV_TAG_DRV_POLL_CANCEL, &msg, sizeof (msg), &rpl,
               sizeof (rpl), NULL, 0);
  return (rpl.retval);
}


int
drv_preada_remote(int devhdl, uint32_t flags, uint32_t sgl_len,
                  HV_SGL sgl[sgl_len], uint64_t offset,
                  uint32_t intarg, pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_preada msg =
  {
    .devhdl = devhdl,
    .flags = flags,
    .sgl_len = sgl_len,
    .offset = offset,
    .intarg = intarg,
  };

  uint32_t msgtag =
    (flags & DRV_FLG_2NDHOP) ? HV_TAG_DRV_PREADA_HI : HV_TAG_DRV_PREADA;

  send_receive_var(rem_tile, msgtag, &msg, sizeof (msg), sgl,
                   sgl_len * sizeof (sgl[0]), &rpl, sizeof (rpl),
                   NULL, 0);
  return (rpl.retval);
}


int
drv_pwritea_remote(int devhdl, uint32_t flags, uint32_t sgl_len,
                   HV_SGL sgl[sgl_len], uint64_t offset,
                   uint32_t intarg, pos_t rem_tile)
{
  struct hv_msg_drv_reply rpl;
  struct hv_msg_drv_pwritea msg =
  {
    .devhdl = devhdl,
    .flags = flags,
    .sgl_len = sgl_len,
    .offset = offset,
    .intarg = intarg,
  };

  uint32_t msgtag =
    (flags & DRV_FLG_2NDHOP) ? HV_TAG_DRV_PWRITEA_HI : HV_TAG_DRV_PWRITEA;

  send_receive_var(rem_tile, msgtag, &msg, sizeof (msg), sgl,
                   sgl_len * sizeof (sgl[0]), &rpl, sizeof (rpl),
                   NULL, 0);
  return (rpl.retval);
}


int
drv_send_msg(int devhdl, void* msg, int msglen, void* reply,
             int replybuflen, int* replylen, pos_t rem_tile)
{
  struct hv_msg_drv_msg drv_msg =
  {
    .devhdl = devhdl,
    .msglen = msglen,
  };

  union
  {
    struct hv_msg_drv_msg_reply reply;
    unsigned long words[HV_MAXMSGWDS];
  } buf;
  size_t buflen;

  if (msglen < 0 || msglen > DRV_MAX_MSG_LEN)
    return (HV_EINVAL);

  send_receive_var(rem_tile, HV_TAG_DRV_MSG, &drv_msg, sizeof (drv_msg), msg,
                   msglen, buf.words, sizeof (buf.words), &buflen,
                   MSG_FLG_SHORTREPLY);

  struct hv_msg_drv_msg_reply* rplp = &buf.reply;

  if (rplp->replylen > 0 && reply)
    memcpy(reply, &rplp[1], rplp->replylen);

  if (replylen)
    *replylen = rplp->replylen;
  return (rplp->retval);
}


void
drv_reply_msg(drv_reply_msg_token_t token, int retval, void* replybuf,
              int replylen, pos_t tile)
{
  if (replybuf == NULL)
    replylen = 0;

  assert (replylen >= 0 && replylen <= DRV_MAX_MSG_LEN);

  struct hv_msg_drv_msg_reply reply =
  {
    .retval = retval,
    .replylen = replylen,
  };

  reply_var(tile, (uint32_t) token, HV_TAG_DRV_MSG, &reply, sizeof (reply),
            replybuf, replylen, 0, 0);
}


//
// FIXME: need new versions of these for Gx, since the port addressing is
// more complex.
//


//
// Define the fastio dispatch table.  This is non-static because it's used in
// intvec.S; if its layout changes, code in the syscall_hand macro there needs
// to be adjusted appropriately.
//

/** Fastio dispatch table entry format */
typedef struct
{
  drv_fastio_func* func;    ///< Function to call
  void* arg;                ///< Argument to pass to the function
}
fastio_disp_t;

/** The fastio dispatch table itself. */
fastio_disp_t fastio_table[1 << FASTIO_INDEX_WIDTH];

//
// We set unused fastio table function entries to this so that we return an
// appropriate error; it's defined in intvec.S.
//
extern drv_fastio_func no_fastio;  ///< Invalid fastio function handler

//
// Availability information for the sets of available indices.  These are
// laid out to maintain the property that user indices, and only user indices,
// have bit (FASTIO_INDEX_WIDTH - 1) set; this is used by the syscall handler
// to do permission checking.
//
/** Next available supervisor-only fast I/O index */
static int fastio_super_next = 0;

/** Number of available supervisor-only fast I/O indices */
static int fastio_super_avail = (1 << FASTIO_INDEX_WIDTH) / 2;

/** Next available user/supervisor fast I/O index */
static int fastio_user_next = (1 << FASTIO_INDEX_WIDTH) / 2;

/** Number of available user/supervisor fast I/O indices */
static int fastio_user_avail = (1 << FASTIO_INDEX_WIDTH) / 2;


/** Initialize the fast I/O table. */
void
fastio_init()
{
  for (int i = 0; i < sizeof (fastio_table) / sizeof (fastio_table[0]); i++)
    fastio_table[i].func = no_fastio;
}

uint32_t
drv_alloc_fastio(int nentries, int superonly)
{
  int* nextp;
  int* availp;

  if (nentries <= 0)
    return (~0);

  if (superonly)
  {
    nextp = &fastio_super_next;
    availp = &fastio_super_avail;
  }
  else
  {
    nextp = &fastio_user_next;
    availp = &fastio_user_avail;
  }

  if (nentries < *availp)
  {
    int oldnext = *nextp;
    *nextp += nentries;
    *availp -= nentries;
    //
    // The syscall interrupt handler uses the high bit as a flag for fast
    // I/O functions.
    //
    return (oldnext | (1 << 31));
  }

  return (~0);
}


void
drv_register_fastio(drv_fastio_func* func, void* arg, int index)
{
  fastio_disp_t* fdp = &fastio_table[index & FASTIO_INDEX_MASK];

  if (fdp->func != no_fastio)
    panic("attempt to register already-registered fastio index %#x", index);

  fdp->func = func;
  fdp->arg = arg;
}


void
drv_unregister_fastio(int index)
{
  fastio_disp_t* fdp = &fastio_table[index & FASTIO_INDEX_MASK];

  if (fdp->func == no_fastio)
    panic("attempt to unregister already-unregistered fastio index %#x", index);

  fdp->func = no_fastio;
}


uint64_t
drv_timer_start(uint32_t usec)
{
  uint64_t cur_time = get_cycle_count();
  return (cur_time + ((uint64_t) early_cpu_speed() * usec) / 1000000ULL);
}


int
drv_timer_done(uint64_t timer)
{
  return (get_cycle_count() >= timer);
}


void
drv_udelay(uint32_t usec)
{
  uint64_t timer = drv_timer_start(usec);

  while (!drv_timer_done(timer))
    ;
}



/** Bitmap of unused interrupt channels. */
static uint64_t avail_intchan = 0;

int
drv_alloc_intchan()
{
  static int once = 1;

  if (once)
  {
    once = 0;
    avail_intchan = RMASK(CHIP_IPI_EVENTS()) &
                    ~((1UL << config.first_dyn_intchan) - 1);
  }

  int chan = __builtin_ffsl(avail_intchan) - 1;
  if (chan < 0)
    return (-1);

  avail_intchan &= ~(1L << chan);

  return chan;
}

void
drv_free_intchan(int chan)
{
  if (chan < 0 || chan >= CHIP_IPI_EVENTS())
    return;

  avail_intchan |= 1L << chan;

  return;
}



int
drv_next_opt(char** argptr, char** opt, char** val)
{
  char* p = *argptr;
  *opt = 0;
  *val = 0;

  //
  // Skip leading whitespace.
  //
  while (isspace(*p))
    p++;

  //
  // If no option, we're done.
  //
  if (!*p)
    return (0);

  //
  // Found an option; record its start, skip to the end of it.
  //
  *opt = p;
  while (*p && *p != '=' && !isspace(*p))
    p++;

  //
  // If there's a value, terminate the option, record the start of the value,
  // then skip to the end of the option.
  //
  if (*p == '=')
  {
    *p++ = '\0';
    *val = p;

    while (*p && !isspace(*p))
      p++;
  }

  //
  // Terminate the option or value, save the string pointer, and return.
  //
  if (*p)
    *p++ = '\0';

  *argptr = p;

  return (1);
}

/** Value for an empty drv_iomem_va2pa table entry.  Note that various bits
 *  of xgbe and pcie driver assembly code assume that the high bit of this
 *  value is set. */
#define DRV_IOMEM_EMPTY 0xFFFF

// I/O memory VA to PA table; see drvintf.h for full description.
uint16_t drv_iomem_va2pa[DRV_IOMEM_NUM_ARENAS] =
{
  [0 ... DRV_IOMEM_NUM_ARENAS - 1] = DRV_IOMEM_EMPTY,
};

static uint8_t drv_iomem_refcnts[DRV_IOMEM_NUM_ARENAS];

int
drv_register_iomem(VA va, PA pa)
{
  //
  // Make sure the buffer is properly aligned.
  //
  if ((va & (DRV_IOMEM_ARENA_SIZE - 1)) != 0 ||
      (pa & (DRV_IOMEM_ARENA_SIZE - 1)) != 0)
    return (HV_EFAULT);

  int index = (va >> DRV_IOMEM_ARENA_SHIFT) & (DRV_IOMEM_NUM_ARENAS - 1);
  uint16_t pageno = pa >> DRV_IOMEM_ARENA_SHIFT;

  //
  // If the memory is already taken, and the registered address is
  // different, fail.  Otherwise, just bump the ref count and return.
  //
  if (drv_iomem_va2pa[index] != DRV_IOMEM_EMPTY)
  {
    if ((drv_iomem_va2pa[index] >> 1) == pageno)
    {
      drv_iomem_refcnts[index]++;
      assert(drv_iomem_refcnts[index] != 0);
      return (0);
    }
    else
      return (HV_EBUSY);
  }

  //
  // If we're physically contiguous with the previous entry, clear its
  // noncontiguous bit.
  //
  int prev_index = (index - 1) & (DRV_IOMEM_NUM_ARENAS - 1);
  if (drv_iomem_va2pa[prev_index] != DRV_IOMEM_EMPTY &&
      drv_iomem_va2pa[prev_index] >> 1 == pageno - 1)
    drv_iomem_va2pa[prev_index] &= ~1;

  //
  // Check the subsequent entry to set the noncontiguous bit in our
  // entry properly.
  //
  int next_index = (index + 1) & (DRV_IOMEM_NUM_ARENAS - 1);
  if (drv_iomem_va2pa[next_index] != DRV_IOMEM_EMPTY &&
      drv_iomem_va2pa[next_index] >> 1 == pageno + 1)
    drv_iomem_va2pa[index] = pageno << 1;
  else
    drv_iomem_va2pa[index] = (pageno << 1) | 1;

  // This is the first assignment, so the refcnt better have been zero.
  assert(drv_iomem_refcnts[index] == 0);
  drv_iomem_refcnts[index]++;
  
  DEVICE_TRACE("iomem registered: VA %#lx, PA %#llX, size %#x\n",
               va, pa, DRV_IOMEM_ARENA_SIZE);

  return (0);
}

int
drv_unregister_iomem(VA va, int size)
{
  //
  // Make sure the buffer is big enough and properly aligned.
  //
  if (size == 0 ||
      (size & (DRV_IOMEM_ARENA_SIZE - 1)) != 0 ||
      (va & (DRV_IOMEM_ARENA_SIZE - 1)) != 0)
    return (HV_EFAULT);

  //
  // Figure out which, and how many, entries we'll be modifying.
  //
  int index = (va >> DRV_IOMEM_ARENA_SHIFT) & (DRV_IOMEM_NUM_ARENAS - 1);
  int num_idx = size >> DRV_IOMEM_ARENA_SHIFT;

  //
  // Check to see that the whole range is actually registered first,
  // so we can fail without unregistering anything if not.
  //
  for (int i = index; i < index + num_idx; i++)
    if (drv_iomem_va2pa[i & (DRV_IOMEM_NUM_ARENAS - 1)] == DRV_IOMEM_EMPTY)
      return (HV_EINVAL);

  //
  // Now go through again and actually do the unregistration.
  //
  for (int i = index; i < index + num_idx; i++)
  {
    int arena = (i & (DRV_IOMEM_NUM_ARENAS - 1));
    assert(drv_iomem_refcnts[arena] != 0);
    drv_iomem_refcnts[arena]--;
    if (drv_iomem_refcnts[arena] == 0)
    {
      drv_iomem_va2pa[arena] = DRV_IOMEM_EMPTY;
      
      //
      // If the previous entry is valid, set its noncontiguous bit.
      //
      if (drv_iomem_va2pa[(index - 1) & (DRV_IOMEM_NUM_ARENAS - 1)] !=
          DRV_IOMEM_EMPTY)
        drv_iomem_va2pa[(index - 1) & (DRV_IOMEM_NUM_ARENAS - 1)] |= 1;
    }
  }

  DEVICE_TRACE("iomem unregistered: VA %#lx, size %#x\n", va, size);

  return (0);
}


int
drv_convert_iomem_va2pa(VA va, int size, PA *pa)
{
  int start_index = (va >> DRV_IOMEM_ARENA_SHIFT);
  int finish_index = ((va + size - 1) >> DRV_IOMEM_ARENA_SHIFT);

  if (size > DRV_IOMEM_ARENA_SIZE)
    return (HV_EINVAL);
  
  // Make sure the iomem is registered.
  uint16_t start_entry = drv_iomem_va2pa[start_index];
  if (start_entry == DRV_IOMEM_EMPTY)
    return (HV_EFAULT);

  // If we span more than one entry, and we're not contiguous, fail.
  if ((start_index != finish_index) &&
      (start_entry & 0x1))
    return (HV_EFAULT);
  
  uint16_t pageno = (start_entry >> 1);
  *pa = (((PA)pageno) << DRV_IOMEM_ARENA_SHIFT) |
    (va & (DRV_IOMEM_ARENA_SIZE - 1));
  
  return (0);
}


void
drv_enable_idn1_intr()
{
  enable_idn1_intr();
}


void
drv_disable_idn1_intr()
{
  disable_idn1_intr();
}


int
drv_set_signal(sigdesc_t sig_desc, int action)
{
  return set_signal(sig_desc, action);
}


void
drv_spin_lock(drv_spinlock_t* mutex)
{
  spin_lock((spinlock_t*) mutex);
}


int
drv_spin_trylock(drv_spinlock_t* mutex)
{
  return (spin_trylock((spinlock_t*) mutex));
}


void
drv_spin_unlock(drv_spinlock_t* mutex)
{
  spin_unlock((spinlock_t*) mutex);
}


void
drv_spin_lock_init(drv_spinlock_t* mutex)
{
  spin_lock_init((spinlock_t*) mutex);
}


int
drv_translate_iorpc(uint64_t offset, void* buffer, uint32_t size,
                    unsigned long flags)
{
  union iorpc_offset off = { .offset = offset };

  switch (off.format)
  {
  case IORPC_FORMAT_NONE:
    return (0);

  case IORPC_FORMAT_USER_MEM:
    // The kernel driver should have translated VA to PA, but
    // apparently it didn't.  Perhaps the opcode encoding is
    // mismatched?
    tprintf("hv_warning: IORPC error: Unexpected IORPC_FORMAT_USER_MEM\n");
    return (GXIO_ERR_OPCODE);

  case IORPC_FORMAT_KERNEL_MEM:
    {
      // Make sure the memory buffer is legal.
      union iorpc_mem_buffer* params = (union iorpc_mem_buffer*) buffer;
      PA real_pa;
      size_t buf_size = params->kernel.size;
      if (drv_cpa2pa(params->kernel.cpa, buf_size, &real_pa))
        return (HV_EFAULT);

      //
      // Modern HVs should maintain CPA == PA; make sure that's true, since
      // that means the client has accurate alignment information.  We don't
      // compare bits above the largest legal page size, since they can't
      // cause any alignment problems.
      //
      assert(((real_pa ^ params->kernel.cpa) & RMASK64(pg_shift_max)) == 0);

      // Check operation-specific alignment constraints.
      if ((flags & DRV_IORPC_FLAG_ALIGN_4KB) &&
          (real_pa & ((1 << 12) - 1)) != 0)
        return (GXIO_ERR_ALIGNMENT);
      
      if ((flags & DRV_IORPC_FLAG_ALIGN_64KB) &&
          (real_pa & ((1 << 16) - 1)) != 0)
        return (GXIO_ERR_ALIGNMENT);

      if (flags & DRV_IORPC_FLAG_ALIGN_SELF_SIZE)
      {
        if (__insn_pcnt(buf_size) != 1 ||
            (real_pa & (buf_size - 1)) != 0)
          return (GXIO_ERR_ALIGNMENT);
      }

      // Make sure the PTE is coherent and cacheable; translate into
      // the easier-to-use struct iorpc_mem_attr format.
      struct iorpc_mem_attr mem_attr = { 0 };
      HV_PTE pte = params->kernel.pte;
      
      switch (hv_pte_get_mode(pte))
      {
      case HV_PTE_MODE_CACHE_TILE_L3:
        mem_attr.hfh = 0;
        break;  
      case HV_PTE_MODE_CACHE_HASH_L3:
        mem_attr.hfh = 1;
        break;
      default:
        return (GXIO_ERR_COHERENCE);
      }

      // Use the pte2aer() functions to make sure we translate the
      // client lotar correctly.
      SPR_AAR_t aar;
      if (pte2aar(params->kernel.pte, &aar.word, 0))
        return (HV_EFAULT);
      mem_attr.lotar_x = aar.location_x_or_page_mask;
      mem_attr.lotar_y = aar.location_y_or_page_offset;
      mem_attr.nt_hint = (params->kernel.flags &
                          IORPC_MEM_BUFFER_FLAG_NT_HINT) != 0;
      mem_attr.io_pin = (params->kernel.flags &
                         IORPC_MEM_BUFFER_FLAG_IO_PIN) != 0;

      // Write back the translated memory buffer parameters.
      params->hv.pa = real_pa;
      params->hv.size = buf_size;
      params->hv.attr = mem_attr;      
    }
    return (0);

  case IORPC_FORMAT_KERNEL_INTERRUPT:
    {
      union iorpc_interrupt* params = (union iorpc_interrupt*) buffer;
      // Verify "x" and "y".
      Lotar client_lotar = HV_XY_TO_LOTAR(params->kernel.x, params->kernel.y);
      Lotar real_lotar;
      if (c2r_lotar(client_lotar, &real_lotar))
        return (HV_EINVAL);
      // Verify "ipi".
      if (params->kernel.ipi >= HV_PL)
        return (HV_EINVAL);
    }
    return (0);

  case IORPC_FORMAT_KERNEL_POLLFD_SETUP:
    {
      union iorpc_pollfd_setup* params = (union iorpc_pollfd_setup*) buffer;
      // Verify "x" and "y".
      Lotar client_lotar = HV_XY_TO_LOTAR(params->kernel.x, params->kernel.y);
      Lotar real_lotar;
      if (c2r_lotar(client_lotar, &real_lotar))
        return (HV_EINVAL);
      // Verify "ipi".
      if (params->kernel.ipi >= HV_PL)
        return (HV_EINVAL);
    }
    return (0);

  default:
    // This is an illegal opcode.
    tprintf("hv_warning: IORPC error: Illegal opcode %d\n", off.format);
    return (GXIO_ERR_OPCODE);
  }
}

