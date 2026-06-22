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
 * Function prototypes and definitions for the hypervisor's level-1 boot
 * program; also defines the interface between the booter and the hypervisor.
 */

#ifndef _SYS_HV_L1BOOT_H
#define _SYS_HV_L1BOOT_H

#include <arch/spr.h>

#include <stdint.h>

#include "board_info.h"
#include "msgtag.h"
#include "param.h"
#include "types.h"

#ifdef L1BOOT
void boot_slave(void);
void boot_master(uint64_t boot_cycle, uint32_t board_flags);

/** State structure used by boot_probe_shim(). */
struct boot_probe_shim_state
{
  pos_t mshims[MAX_MSHIMS];       /**< List of mshim IDN addresses. */
  pos_t mshims_mdn[MAX_MSHIMS];   /**< List of mshim MDN addresses. */
  pos_t pcies[MAX_PCIES];         /**< List PCIe shim IDN addresses. */
  pos_t rshims[MAX_RSHIMS];       /**< List of rshim IDN addresses. */
#ifdef MAX_MPIPES
  pos_t mpipes[MAX_MPIPES];       /**< List of mPIPE addresses. */
#endif
#ifdef MAX_MICA_COMPS
  pos_t mica_comps[MAX_MICA_COMPS]; /**< List of MiCA compression addresses. */
#endif
#ifdef MAX_MICA_CRYPTOS
  pos_t mica_crypts[MAX_MICA_CRYPTOS]; /**< List of MiCA crypto addresses. */
#endif
#ifdef MAX_USB_HOSTS
  pos_t usb_hosts[MAX_USB_HOSTS]; /**< List of USB host addresses. */
#endif
  int num_mshims:8;               /**< Number of mshims found. */
  int num_pcies:8;                /**< Number of PCIe shims found. */
  int num_rshims:8;               /**< Number of rshims found. */
#ifdef MAX_MPIPES
  int num_mpipes:8;               /**< Number of mPIPE shims. */
#endif
#ifdef MAX_MICA_COMPS
  int num_mica_comps:8;           /**< Number of MiCA compression shims. */
#endif
#ifdef MAX_MICA_CRYPTOS
  int num_mica_crypts:8;          /**< Number of MiCA crypto shims found. */
#endif
#ifdef MAX_USB_HOSTS
  int num_usb_hosts:8;            /**< Number of USB host shims found. */
#endif
};

void uart_init(uint32_t refclk, struct bi_console_cfg* cfg);
void console_init(uint32_t refclk, struct bi_console_cfg* cfg,
                  uint32_t* board_flags);
void boot_putchar(char c);
int boot_getchar_timeout(int msec);
void boot_flush_output(void);
uint_reg_t boot_exit_protocol_mode(void);
void boot_restore_mode(uint_reg_t mode);

void boot_printf(const char* fmt, ...)
  __attribute__((__format__(__printf__,1,2)));

void boot_reset_chip(unsigned long flags) __attribute__((__noreturn__));

extern pos_t rshimaddr;
extern pos_t gpioaddr;

#ifndef POST_MODE
/** Whether we assume quick or thorough tests, or ask the user. */
#define POST_MODE POST_QUERY
#endif

#endif /* L1BOOT */

#ifdef L1BOOT
/** Convert a pos_t to a tile index. */
#define POS2IDX(pos) _POS2IDX(pos, ulhc)

/** Convert a tile index to a pos_t. */
#define IDX2POS(idx) _IDX2POS(idx, ulhc)
#endif /* L1BOOT */

/** Load code from the boot network and jump to it, passing it our arguments.
 * @param text_physaddr The physical address at which to load the HV.
 * @param firstaddr First address touched by the L1 booter.
 * @param lastaddr Last address touched by the L1 booter.
 * @param ulhc Coordinates of the upper left-hand corner of the chip.
 * @param lrhc Coordinates of the lower right-hand corner of the chip.
 * @param master Coordinate of the master tile.
 * @param rshim Coordinate of the rshim.
 * @param shim_mask Mask of shims which the hypervisor should probe.
 * @param board_flags Boot-detected board properties (BOARD_xxx).
 */
void load_and_go_net(PA text_physaddr,
                     unsigned long* firstaddr, unsigned long* lastaddr,
                     pos_t ulhc, pos_t lrhc, pos_t master, pos_t rshim,
                     unsigned long shim_mask, uint32_t board_flags);

/** Load code from the SROM and jump to it, passing it our arguments.
 * @param text_physaddr The physical address at which to load the HV.
 * @param firstaddr First address touched by the L1 booter.
 * @param lastaddr Last address touched by the L1 booter.
 * @param ulhc Coordinates of the upper left-hand corner of the chip.
 * @param lrhc Coordinates of the lower right-hand corner of the chip.
 * @param master Coordinate of the master tile.
 * @param rshim Coordinate of the rshim.
 * @param shim_mask Mask of shims which the hypervisor should probe.
 * @param board_flags Boot-detected board properties (BOARD_xxx).
 * @param srom_dev SROM device cookie.
 */
void load_and_go_srom(PA text_physaddr,
                      unsigned long* firstaddr, unsigned long* lastaddr,
                      pos_t ulhc, pos_t lrhc, pos_t master, pos_t rshim,
                      unsigned long shim_mask, uint32_t board_flags,
                      int srom_dev);

// Board flags

/** Board has 100 MHz refclk; if not set, we assume it's 125 MHz. */
#define BOARD_100MHZ_REFCLK  0x1

/** Board is a bringup board, not a PCIe board; this should eventually turn
 *  into finer-grained characteristics. */
#define BOARD_BRINGUP_BOARD  0x2

/** An I2C PROM containing a board information block is present. */
#define BOARD_BI_I2C         0x4

/** Board was booted from SPI ROM. */
#define BOARD_BOOTED_SROM    0x8

/** The HVFS should be read from the SPI ROM.  (The SROM shim's address
 *  register points to the start of the HVFS image.) */
#define BOARD_SROM_HVFS      0x10

/** The booter requested that we do a thorough POST run.  Note that the
 *  hypervisor might decide otherwise based on other input (e.g., the
 *  configuration file). */
#define BOARD_POST_THOROUGH 0x20

/** We're striping over at least some of of our memory controllers.  Only
 *  possible if CHIP_HAS_MEM_STRIPE_CONFIG() is true. */
#define BOARD_STRIPE_MEMORY  0x40

/** Reboot (setting the appropriate soft reset state) if any component of
 *  the boot stream has a bad CRC.  This enables the SROM booter to fall
 *  back to an older (and hopefully non-corrupt) boot image. */
#define BOARD_BADCRC_REBOOT  0x80

/** The booter found a POST failure. */
#define BOARD_POST_FAILURE  0x100

/** There is a TILEPro36 on this board. */
#define BOARD_TILEPRO36     0x200

/** We're striping over at least some of our memory controllers, and we
 *  lost memory by doing so (i.e., we didn't have the same amount of memory
 *  on each controller).  Only possible if CHIP_HAS_MEM_STRIPE_CONFIG() is
 *  true.  BOARD_STRIPE_MEMORY will always be on if this is on. */
#define BOARD_STRIPE_LOSS   0x400

/** Board is an FPGA board. */
#define BOARD_FPGA          0x800

/** We're using UART 1 as the console. */
#define BOARD_CONSOLE_UART1 0x1000

/** We're using the rshim as the early console.  Not exclusive
    with BOARD_CONSOLE_UART1 since we might later shift to that. */
#define BOARD_CONSOLE_RSHIM 0x2000

/** We're using the tile-monitor FIFO as the console.  Note that, in almost
 *  all contexts, we don't use this flag, but instead do a dynamic check of
 *  the relevant rshim register bit, since it may have changed since the
 *  booter ran.  The main thing this tells us when set at boot is that we
 *  are not booting from UART.  It would probably be cleaner to figure
 *  out some way of sensing that directly, and then get rid of this flag. */
#define BOARD_CONSOLE_TMFIFO 0x4000


/** Jump to code already loaded from the STN.
 * @param text_physaddr The physical address of the HV.
 * @param target Low 32 bits of physical address to jump to.
 * @param firstaddr First address touched by the L1 booter.
 * @param lastaddr Last address touched by the L1 booter.
 * @param ulhc Coordinates of the upper left-hand corner of the chip.
 * @param lrhc Coordinates of the lower right-hand corner of the chip.
 * @param master Coordinate of the master tile.
 */
void go(PA text_physaddr, unsigned long* firstaddr, unsigned long* lastaddr,
        pos_t ulhc, pos_t lrhc, pos_t master, PA target);

//
// The following four routines use the minimal assembly support for
// reading the SROM boot stream.
//

/** Retrieve the SROM device cookie.
 * @param rshim Tile coordinates of the rshim.
 * @return Device cookie.
 */
int early_srom_get_dev(uint32_t rshim);

/** Retrieve the current position of the SROM boot stream.
 * @param rshim Tile coordinates of the rshim.
 * @param dev Device cookie.
 * @return Boot stream position.
 */
uint32_t srom_get_addr(uint32_t rshim, int dev);

/** Set the current position of the SROM boot stream.
 * @param rshim Tile coordinates of the rshim.
 * @param dev Device cookie.
 * @param addr Boot stream position.
 */
void srom_set_addr(uint32_t rshim, int dev, uint32_t addr);

/** Read from the SROM via direct access, at the current position.
 * @param rshim Tile coordinates of the rshim.
 * @param dev Device cookie.
 * @param addr Destination for the read data.
 * @param nwds Number of words to read.
 */
void early_srom_read(uint32_t rshim, int dev, void* addr, int nwds);

// Final route for direction.  Yes, these are different than, and in a
// different order than, the static network values, and yes, that's the way
// the hardware actually works.

#define FR_NORTH   (0 << 30)      /**< Final route for north */
#define FR_SOUTH   (1 << 30)      /**< Final route for soth */
#define FR_EAST    (2 << 30)      /**< Final route for east */
#define FR_WEST    (3 << 30)      /**< Final route for west */

#define FR_MASK    (3 << 30)      /**< Mask to get the current setting */

// "No final route needed" magic value.
#define FR_NONE    (~0)           /**< No final route needed */

//
// Upper-left-hand corner coordinates that result from a dynamic boot.
//
#define BOOT_DYN_ULHC_X 1         /**< ULHC X coordinate for dyamic boot */
#define BOOT_DYN_ULHC_Y 1         /**< ULHC Y coordinate for dyamic boot */

//
// Messages handled by the slave boot code.
//

/**
 * Initial dynamic boot.  (This is only used as an acknowledgement; its payload
 * is one word which is the tile coordinate of the responder.)
 */
#define BOOT_TAG_DYNBOOT      HV_MKTAG(0xF0, HV_MSG_PRI_INIT)

/**
 * Initial boot.  (This is only used as an acknowledgement, whose payload is
 * one (ignored) word.)
 */
#define BOOT_TAG_BOOT         HV_MKTAG(0xF1, HV_MSG_PRI_INIT)

/**
 * Set client CBOX configuration registers, then start execution at the
 * given physical address; its payload is a struct boot_msg_execute.
 */
#define BOOT_TAG_EXECUTE      HV_MKTAG(0xF4, HV_MSG_PRI_INIT)
/** Message for BOOT_TAG_EXECUTE */
struct boot_msg_execute
{
  /** Physical address at which to start execution */
  PA paddr;
  /** CBOX_MMAP_n SPR, CBOX_CONFIG_IGNORE to not set */
  uint_reg_t cbox_mmap[4];
  /** CBOX_MSR SPR, CBOX_CONFIG_IGNORE to not set */
  uint_reg_t cbox_msr;
  /** MEM_STRIPE_CONFIG value */
  uint_reg_t mem_stripe_config;
};

/** Special value used in boot_msg_execute meaning "don't set this CBOX
 *  value". */
#define CBOX_CONFIG_IGNORE ~0U

#endif /* _SYS_HV_L1BOOT_H */
