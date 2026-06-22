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
 * Exit handling.
 */

#include <stdio.h>
#include <unistd.h>

#include <arch/rsh.h>
#include <arch/spr.h>

#include "cfg.h"
#include "config.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "lock.h"
#include "misc.h"
#include "msg.h"

/** Exit lock. */
static spinlock_t exit_lock _SHARED;

/** Handle a full-chip exit operation.
 * @param status Exit status; nonzero means we're panicing.
 */
void
global_exit(int status)
{
  //
  // If this is a panic, and we're supposed to reboot on panic, and we
  // can, do so.
  //
  if (status && config.reboot_on_panic_requested)
  {
    if (board_flags & BOARD_BOOTED_SROM)
    {
      printf("Restart after panic requested; resetting chip and "
             "restarting.\n");
      ffsync(stdout);
      reset_chip(0);
    }
    else
      printf("Restart after panic requested, but only possible "
             "after standalone boot.\n");
  }

  if (status)
    printf("System halted.\n");

  //
  // Flush any pending console output.
  //
  ffsync(stdout);

  //
  // Spin down the core PLL.  This is useful if we're shutting down for
  // overtemperature reasons; if we're really, really hot, even quiescing
  // all of the tiles may not be enough to stop a thermal runaway.
  //
  cfg_wr(rshims[0]->idn_ports[0].word, 0, RSH_CLOCK_CONTROL, 0);

  //
  // If we're supposed to halt the whole chip, do so.
  //
  if (config.halt_full_chip_requested)
  {
    //
    // Do a global chip quiesce by asserting broadcast wire #0.  During
    // hypervisor startup, we configured this diagnostic network so that it
    // is broadcast to all tiles.  We also configured each tile so that
    // when it receives this signal, it will quiesce execution (stop
    // fetching instructions).
    //
    //
    // On Gx, we have a choice between pulsing the wire once, or keeping it
    // asserted; in order to keep the tiles quiesced we need the latter.
    //
    __insn_mtspr(SPR_DIAG_BCST_TRIGGER,
                 1 << SPR_DIAG_BCST_TRIGGER__LEVEL_SHIFT);
  }

  //
  // If we didn't do a quiesce, or if it didn't work for some reason, exit.
  //
  _exit(status);
}


/** Handle a single-tile exit operation.
 * @param status Exit status; nonzero means we're panicing.
 */
void
exit(int status)
{
  static int has_exited = 0;

  //
  // If we've already exited, there's some sort of recursive panic or
  // something going on; just stop.
  //
  if (has_exited++)
    _exit(status);

  //
  // Try to get the exit lock.
  //
  if (!spin_trylock(&exit_lock))
  {
    //
    // We got the lock.  If we're the console tile, perform the global exit
    // function; otherwise, send an exit message to the console tile,
    // asking it to do so.
    //
    if (my_pos.word == chip_console.word)
    {
      global_exit(status);
    }
    else
    {
      struct hv_msg_global_exit wr_msg =
      {
        .status = status,
      };

      long ackbuf;

      //
      // If the send fails, we just keep trying; it's not like there's
      // anything better for us to do.
      //
      while (send_receive(chip_console, HV_TAG_GLOBAL_EXIT, &wr_msg,
                          sizeof (wr_msg), &ackbuf, sizeof (ackbuf), NULL,
                          MSG_FLG_XMITFAIL))
        ;
    }
  }
  else
  {
    //
    // We didn't get the lock.  If we're the console tile, then we need to
    // be prepared to handle the message we're going to get from whoever
    // _did_ get the lock, so we don't exit; instead we spin processing
    // messages.
    //
    if (my_pos.word == chip_console.word)
    {
      while (1)
        drv_yield();
    }
  }

  _exit(status);
}
