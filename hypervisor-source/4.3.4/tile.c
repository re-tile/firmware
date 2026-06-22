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
 * Routines to do tile configuration.
 */

#include <stdio.h>

#include <arch/idn.h>
#include <arch/interrupts.h>

#include "sys/libc/include/util.h"

#include "bme.h"
#include "cfg.h"
#include "client.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "idn.h"
#include "mapping.h"
#include "misc.h"
#include "msg.h"
#include "mshim_acc.h"
#include "tile.h"


int start_client_flag;  /**< Set by msg.c when we get a start client message */

/** Initialize the remaining memory mapping registers for the master tile.
 */
void
init_memory_regs()
{
  assert(is_master);

  for (int i = 0; i < MAX_MSHIMS; i++)
    if (mshims[i])
    {
      int portidx = mshim_portidx_from_pos(my_pos, i);
      set_cbox_mmap_spr(i, mshims[i]->mdn_ports[portidx].word);
    }

  if (config.striping_requested && !(board_flags & BOARD_STRIPE_MEMORY))
  {
    printf("hv_warning: cannot stripe memory, shim sizes:");
    for (int i = 0; i < MAX_MSHIMS; i++)
      printf(" %lldMB", mshim_sizes[i] >> 20);
    printf("\n");
  }
  else if (board_flags & BOARD_STRIPE_LOSS)
    printf("hv_warning: forced memory striping reduced memory capacity\n");
}


/** Get a set of slave tiles running the hypervisor.
 * @param sts Slave tile state, initialized by the master tile.
 * @param mask Mask of tiles to start.
 */
void
init_slaves(struct slave_tile_state* sts, tile_mask* mask)
{
  extern uint32_t _start;  // Our entry point, and thus the slave's entry point

  // FIXME: this is all synchronous; we really want to get everyone doing
  // their various initialization steps in parallel, which will require some
  // kind of per-slave data structure which we move to various states as
  // we get acknowledgements.  Note that if we do that, we'll have to avoid
  // doing the device probe operation from every tile, as we do now, since
  // that would potentially overrun the 4-deep config read buffer in the shim.

  for (int y = chip_ulhc.bits.y; y <= chip_lrhc.bits.y; y++)
  {
    for (int x = chip_ulhc.bits.x; x <= chip_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };

      //
      // Only initialize clients named in the mask
      //
      if (!in_tile_mask(mask, tile))
        continue;

      assert(tile.word != my_pos.word);

      // Calculate source ID and configure source ID table in shims.  Note
      // that we assume we get the same source ID from the config routine in
      // each case.  Simultaneously, compute the message we'll send to the
      // tile with which it will initialize its CBOX mappings.

      struct boot_msg_execute exec_msg =
        { .cbox_mmap = { CBOX_CONFIG_IGNORE, CBOX_CONFIG_IGNORE,
                         CBOX_CONFIG_IGNORE, CBOX_CONFIG_IGNORE } };

      for (int i = 0; i < MAX_MSHIMS; i++)
      {
        if (mshims[i])
        {
          //
          // Note that we just spread references to the various MDN ports
          // among all of the tiles, rather than trying to optimize for the
          // shortest distance; the ports are right next to each other,
          // so there's not much difference in latency, and this does a
          // better job of handling different distributions of references.
          // For instance, a user might run two applications, one using
          // the tiles and the memory shims on the left half of the chip,
          // and the other using the tiles and the memory shims on the
          // right half.  In that configuration, a pure distance-based
          // assignment would likely underutilize some of the mshim
          // MDN ports.
          //
          int portidx = mshim_portidx_from_pos(tile, i);
          exec_msg.cbox_mmap[i] = mshims[i]->mdn_ports[portidx].word;
        }
      }
      // Note that this is mainly for the benefit of the FPGA, where we
      // only have 1 network port on the mshim and thus need a special
      // config.
      exec_msg.cbox_msr = __insn_mfspr(SPR_CBOX_MSR);
      exec_msg.mem_stripe_config = __insn_mfspr(SPR_MEM_STRIPE_CONFIG);
      exec_msg.paddr = vtop((VA) &_start);

      //
      // Send the "config CBOX and jump to hypervisor" message.
      //
      INIT_TRACE("slave (%d,%d), jump to hv\n", UXY(tile));

      send_receive(tile, BOOT_TAG_EXECUTE, &exec_msg, sizeof (exec_msg), NULL,
                   sizeof (unsigned long), NULL, MSG_FLG_SENDBOOT);

      //
      // Send the "Configure tile" message
      //
      INIT_TRACE("slave (%d,%d), config tile\n", UXY(tile));

      struct hv_msg_config config_msg = { .sts = *sts };

      send_receive(tile, HV_TAG_CONFIG, &config_msg, sizeof (config_msg),
                   NULL, sizeof (unsigned long), NULL, MSG_FLG_SENDIDN0);

      INIT_TRACE("slave (%d,%d), done\n", UXY(tile));
    }
  }
}


/** Start one slave client.
 * @param slave_pos Tile to start.
 * @param client_num Index of client in config.clients[].
 */
void
start_slave_client(pos_t slave_pos, int client_num)
{
  struct hv_msg_start_client client_msg;

  for (int i = 0; i < MAX_MSHIMS; i++)
  {
    client_msg.client_pa[i] = config.clients[client_num].mem_base[i];
    client_msg.client_len[i] = config.clients[client_num].mem_len[i];
  }
  client_msg.client_entry = config.clients[client_num].client_entry;

  send_receive(slave_pos, HV_TAG_START_CLIENT,
               &client_msg, sizeof (client_msg),
               NULL, sizeof (unsigned long), NULL, 0);
}


/** Get all slave tiles in our client running the supervisor.
 */
void
start_all_slave_clients()
{
  // FIXME: this is all synchronous; we really want to get everyone doing
  // their various initialization steps in parallel, which will require some
  // kind of per-slave data structure which we move to various states as
  // we get acknowledgements.

  for (int y = client_ulhc.bits.y; y <= client_lrhc.bits.y; y++)
  {
    for (int x = client_ulhc.bits.x; x <= client_lrhc.bits.x; x++)
    {
      pos_t tile = { .bits.x = x, .bits.y = y };

      //
      // Don't try to initialize ourselves, or tiles which aren't in this
      // client
      //
      if (tile.word == my_pos.word || !in_tile_mask(&client_tiles, tile))
        continue;

      start_slave_client(tile, my_client);
    }
  }
}


/** Initialize a slave tile and wait for a configuration message from the
 *  master.
 * @param sts Slave tile state, modified by this routine based on the
 *        message received from the master tile.
 */
void
slave_init(struct slave_tile_state* sts)
{
  IDN0_SET_BUSY();















  //
  // Send "hv now running" acknowledgement to the master.
  //
  long ack = 0;
  reply(chip_master, POS2IDX(my_pos), BOOT_TAG_EXECUTE, &ack, sizeof (ack));

  //
  // Wait for a configuration message.
  //
  union
  {
    struct hv_msg_config config_msg;
    unsigned long words[B2W_UP(sizeof(struct hv_msg_config))];
  } u;
  unsigned long* p = u.words;






















  hv_tag tag = { .word = (uint32_t)idn0_receive() };

  if (tag.bits.type != HV_TAG_CONFIG)
    panic("init_slave() got bad message type: %#x", tag.bits.type);

  // Discard sender's coordinates
  (void) idn0_receive();

  for (int len = tag.bits.len; len; len--)
    *p++ = idn0_receive();


  IDN0_CLEAR_BUSY();

  //
  // Update the slave_tile_state, and return
  //
  *sts = u.config_msg.sts;
}


/**
 * Wait until we're told to start our client, then do so.
 */
void
wait_for_start_client()
{
  nap_until_change(&start_client_flag, sizeof (start_client_flag), 0,
                   NUC_FLG_NO_TIMEOUT, 0x1234, 0,
                   __builtin_return_address(0));

  //
  // If this is a dedicated driver tile, run the service routine instead
  // of starting the client.
  //
  call_driver_service();

  //
  // If this is the first tile in the client, it needs to load the client
  // executable first.
  //
  if (config.clients[my_client].start_tile.word == my_pos.word)
    load_and_start_client();
  else if (config.clients[my_client].flags & CLIENT_BME)
    start_client_bme(config.clients[my_client].mem_base,
                     config.clients[my_client].mem_len,
                     config.clients[my_client].client_entry);
  else
    start_client(config.clients[my_client].client_entry);
}


/** Wait for a "start client" message from the master tile, then execute it.
 */
void
slave_idle()
{
  //
  // Send "hv now configured" acknowledgement to the master.
  //
  long ack = 0;
  reply(chip_master, POS2IDX(my_pos), HV_TAG_CONFIG, &ack, sizeof (ack));
  wait_for_start_client();
}
