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

#ifndef TOOLS_MBOOTCLI_CONFIG_H
#define TOOLS_MBOOTCLI_CONFIG_H

// How long to allow the user to interrupt the boot process.
// Set to 0 to disable interactive mode.
#define COUNT_DOWN_SECS        5

// The banner printed out on mboot startup.
#define BANNER_STRING          "\n\n==============Tilera mboot==============\n" \
                               "Press any key to enter mboot CLI mode\n\n"
// The mboot command prompt.
#define PROMPT                 "mboot: "

// The default boot params passed to the kernel
#define DEF_BOOT_PARAM         ""

  
// The default device to boot from.
#define DEF_DEV                "hd"
// The default image name to boot.
#define DEF_IMG                "vmlinux"
// Do we do DHCP by default?
#define DEF_DHCP               "no"

// Where the CF/SSD is mounted.
#define HD_PATH                "/tmp/hd_boot/"
// Where the SATA disk is mounted.
#define SD_PATH                "/tmp/sd_boot/"
// Where a USB device is mounted.
#define USB_PATH               "/tmp/usb_boot/"
// Where the rom is mounted.
#define ROM_PATH               "/tmp/rom_boot/"
// Where the memory is mounted.
#define MEM_PATH               "/tmp/mem_boot/"
// Where the files for net boot are stored.
#define NET_PATH               "/tmp/net_boot/"

#define NETBOOT_BOOTINFO_PATH  NET_PATH "info_path/"
#define NETBOOT_IMG_PATH       NET_PATH "img_path/"
#define NETBOOT_SCRIPT_PATH    NET_PATH "preboot_path/"

#define NETBOOT_INFOFILE       NETBOOT_BOOTINFO_PATH"info"

// SPI ROM
#define DEV_BOOT_IMAGE         "/dev/srom/bootimage"
#define DEV_USER_PARAM         "/dev/srom/userparam"
#define DEV_BOOT_PARAM         "/dev/srom/bootparam"

// The command line to start DHCP.
// "-n" requests exit on error if it fails to get a lease.
// "-q" requests exit on success rather than waiting and renewing again later.
// "-t" specifies retry times.
// "-i INTERFACE" specifies the interface to listen on.
#define DHCP_CMD_LINE          "/sbin/udhcpc -n -q -t 15 -i"
// Where network info can be found.
#define PROCNET_FILE           "/proc/net/dev"

// Amount of command history to save.
#define HISTORY_NUM            10

// Device configuration:  These are used to enable and disable
// specific boot devices, such as USB, SATA, CF, etc.

// We can boot from USB.
#define CFG_DEV_USB       0
// We can boot from a IDE device (SSD or CF)
#define CFG_DEV_HD        1
// We can boot from a SATA hard disk
#define CFG_DEV_SD        1
// We can boot from tftp.
#define CFG_DEV_TFTP      1
// We can boot from memory.
#define CFG_DEV_MEM       1
// We can boot from net automatically.
#define CFG_DEV_NET       1

// Enable/disable specific commands.  Set to 1 to disable a command.
#define CFG_CMD_BOOT       1
#define CFG_CMD_BOOTPARAM  1
#define CFG_CMD_CLEARCFG   1
#define CFG_CMD_DHCP       1
#define CFG_CMD_EXIT       1
#define CFG_CMD_FDISK      0
#define CFG_CMD_FTP        1
#define CFG_CMD_HELP       1
#define CFG_CMD_IFCONFIG   1
#define CFG_CMD_LS         1
#define CFG_CMD_MEM        0
#define CFG_CMD_MKFS       0
#define CFG_CMD_PING       1
#define CFG_CMD_REBOOT     1
#define CFG_CMD_RM         1
#define CFG_CMD_ROUTE      1
#define CFG_CMD_SERIAL     0
#define CFG_CMD_SHELL      1
#define CFG_CMD_SHOWCFG    1
#define CFG_CMD_TFTP       1

#endif /* TOOLS_MBOOTCLI_CONFIG_H */
