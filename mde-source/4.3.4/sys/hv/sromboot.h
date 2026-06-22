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
 * Definitions for the interface between the SROM booter and the L0.5/L1
 * booters.
 */

#ifndef _SYS_HV_SROMBOOT_H
#define _SYS_HV_SROMBOOT_H

//
// SROM booter <-> L1 booter interface, version 2.
//
//   After a target image is chosen, the SROM booter starts at the
//   beginning of the image, interprets it as a level-0 boot stream,
//   and loads it.  The cache is still in cache-as-RAM mode, and has been
//   fully initialized, starting at physical/virtual address 0x80000000.
//   The SROM booter itself is located at the end of available memory/cache
//   and is no more than 8K in length; thus, addresses 0x80000000 through
//   0x8000DFFF are available.
//
//   Once the image is loaded, the SROM booter jumps not to the address
//   listed in the bootstream segment header, but to bundle _after_ that
//   (i.e., the actual jump address is the stream's jump address plus 8).
//   This signals the loaded code that it was loaded from SROM and not
//   directly from the static network.  (We don't want to use a register to
//   signal this since the registers have indeterminate values at boot.)
//
//   When the image is jumped to at its second bundle, the SROM booter
//   guarantees the following:
//
//   - r50 contains the following SROM boot flags:
//
//     * SROMBOOT_SROMBOOT means that we're booting from SROM.
//
//     * SROMBOOT_BADCRC_REBOOT means that if the booter or the hypervisor
//       finds that the input boot stream is corrupted, that it should
//       reboot the chip and request that a different image be used (see
//       below).
//
//     Note that in version 1 of the interface, r50 was guaranteed to
//     be zero.  To handle this situation, when the L0.5 booter is entered
//     at its second bundle, it should always OR SROMBOOT_SROMBOOT into
//     the flags before passing them to the L1 booter.
//
//   - r51 is zero.  (This is reserved for future use.)
//
//   - r52 contains a pointer to the load_srom routine (see below).
//
//   - The address register in the SROM shim is pointing at the byte after
//     the level-0 boot stream which was just loaded.
//
//   Once the image is loaded, it is free to execute however it likes, and
//   may overwrite the SROM booter.  However, if it preserves the booter,
//   it it may later call back to the booter to request that more code be
//   loaded from SROM.  To do so, it can jump to the load_srom routine.
//   This routine treats the SROM contents, beginning at the current value
//   of the address register, as a secondary boot stream; it loads the
//   data into memory and then executes it.  load_srom does not modify r0
//   through r9, so they may be used to pass data on to the loaded code;
//   all other registers will be destroyed.  (Unlike with the initial
//   boot, load_srom does not modify the jump address, so if the loaded
//   code needs to know that it was booted from SROM, the first part of
//   the image should so inform it via one of r0-r9.)
//






//   It is possible to modify the behavior of the SROM booter upon soft
//   reset by setting processor registers to certain values.  (This scheme
//   assumes that the random register contents we see on initial startup
//   won't match those certain values; in practice we don't see too much
//   randomness on startup, and there really isn't any other option given
//   the hardware's reset behavior.)
//
//   The only action currently supported is to ask that the SROM booter
//   not use the boot image chosen by its normal boot policy, but to
//   use an alternate image instead (most likely the next-best image).
//   This request is used when an image is found to be corrupt during
//   boot, and when the SROM booter has requested that this action be
//   enabled via the SROM boot flags.
//
//   On TILEPro, to tell the SROM booter that it was entered due to a soft
//   reset, r50 is set to SROMBOOT_SOFTBOOT_FLAG_VAL.  The action to be
//   performed (one of the SROMBOOT_SOFTREBOOT_ACT_xxx values) is then
//   placed in r51.  On TILE-Gx, the action is placed in the rshim's
//   BREADCRUMB0 register; no flag is used in that case.
//

//
// These flags are passed from the SROM booter to the L0.5 booter in r50, and
// then to the L1 booter as its second argument.  They're only valid for the
// L0.5 booter when it's entered at its second bundle.
//
// In some ways it would have been easier to make these identical to the
// corresponding board flags, but we want it to be clear that these flags'
// values can't change, since newer L0.5/L1 booters need to work with old
// SROM booters.  (The board flags, on the other hand, can change at will
// since the booters and hypervisor are always delivered as a unit.)
//

/** We're booting from SROM using the SROM booter. */
#define SROMBOOT_SROMBOOT        0x1

/** If we find a bad CRC in the boot stream, reboot and request a different
 *  boot image. */
#define SROMBOOT_BADCRC_REBOOT   0x2

//
// These symbols define the values which are placed in RSH_BREADCRUMB0
// before chip reset in order to request that the SROM booter modify the
// boot sequence.  Note that these bits are all cleared to zero after
// they have been interpreted by the SROM booter, so that they do not
// apply to subsequent boots.
//

/** Action mask. */
#define SROMBOOT_SOFTREBOOT_ACT_MASK    0x1

/** Soft reboot action: none. */
#define SROMBOOT_SOFTREBOOT_ACT_NONE    0x0

/** Soft reboot action: reboot after CRC failure.  This really means "boot
 *  from an image other than the one you'd normally boot from", which is
 *  presumably the one that had the bad CRC.  Normally, this is only used
 *  when SROMBOOT_BADCRC_REBOOT was passed as an SROM boot flag, since if
 *  that's not true there isn't another image to boot from.  It can also be
 *  enabled on user request during a manual reboot; in that case we assume
 *  the user knows what they're doing. */
#define SROMBOOT_SOFTREBOOT_ACT_BADCRC  0x1

//
// Watchdog.  If the bits in this field are nonzero, they request that the
// SROM booter enable and configure the hardware watchdog so that it will
// reset the system after the given number of seconds.
//

/** Watchdog shift. */
#define SROMBOOT_SOFTREBOOT_WD_SHIFT    6

/** Watchdog width */
#define SROMBOOT_SOFTREBOOT_WD_WIDTH    10

/** Watchdog mask right-justified. */
#define SROMBOOT_SOFTREBOOT_WD_RMASK    0x3FF

/** Watchdog mask. */
#define SROMBOOT_SOFTREBOOT_WD_MASK     0xFFC0

/** Watchdog field. */
#define SROMBOOT_SOFTREBOOT_WD_FIELD    6,15


/** Bits in RSH_BREADCRUMB0 which are used by the soft reboot flags. */
#define SROMBOOT_BREADCRUMB0_MASK       0xFFC1

#endif /* _SYS_HV_SROMBOOT_H */
