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
 * Routines to manage clients.
 */

#include <alloca.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

#include <arch/cycle.h>
#include <arch/idn.h>
#include <arch/ipi.h>
#include <arch/rsh.h>
#include <arch/sim.h>

#include <hv/hypervisor.h>

#include "bme.h"
#include "board_info.h"
#include "cfg.h"
#include "client.h"
#include "client_obj.h"
#include "config.h"
#include "debug.h"
#include "devices.h"
#include "drivers/sensor/sensor.h"
#include "drvintf.h"
#include "fault.h"
#include "filesys.h"
#include "hv.h"
#include "hvgdb.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "loader.h"
#include "mapping.h"
#include "misc.h"
#include "msg.h"
#include "physacc.h"
#include "rtc.h"
#include "tile.h"
#include "tlb.h"
#include "tsb.h"

/// Hypervisor glue code, to be provided as part of client's address space.
extern const unsigned char glue_data[];
/// Length of the hypervisor glue code.
extern const int glue_len;


/** Attempt to get the client's geometry from the filesystem, and return
 *  its upper-left and lower-right hand corners in hypervisor tile coordinate
 *  space; if it's not defined there, return a default geometry of all the
 *  physical tiles.
 * @param ulhc Pointer to returned upper-left-hand-corner coordinate.
 * @param lrhc Pointer to returned lower-right-hand-corner coordinate.
 * @param tiles Pointer to returned tile mask.
 */
void
determine_client_geometry(pos_t* ulhc, pos_t* lrhc, tile_mask* tiles)
{
  *ulhc = config.clients[my_client].ulhc;
  *lrhc = config.clients[my_client].lrhc;
  *tiles = config.clients[my_client].tiles;
}


/** Calculate the set of tiles which the client will use for hash-for-home.
 * @param home_map_tiles Pointer to returned tile mask.
 */
void
determine_client_home_map_tiles(tile_mask* home_map_tiles)
{
  if (config.nclients > 0)
    *home_map_tiles = config.clients[my_client].home_map_tiles;
  else
    *home_map_tiles = client_tiles;
}


/** Load the client program into the client's memory.
 * @return Client physical address of loaded executable's entry point.
 */
CPA
load_client()
{
  CPA client_entrypt;
  PA real_client_base;
  PA real_glue_base;

  // Enable client's fake P=V mode, so we can use virtual addresses to refer
  // to the client's physical memory.  Note that we don't actually use the
  // real_client_base and real_glue_base PA's; we're just calling c2r_pa()
  // to make sure everything's in range.

  enable_fake_physmem_mode();

  // Copy in client code and data from hypervisor filesystem to client's
  // physical memory area.

  assert(!c2r_pa(0, 1, &real_client_base));

  int inode = config.clients[my_client].bin_ino;

  if (inode < 0)
    client_entrypt = load_null_client();
  else
    client_entrypt = load(inode);

  // Copy in glue code to client's physical memory area.

  assert(!c2r_pa(HV_GLUE_START_CPA, HV_GLUE_RESERVED_SIZE, &real_glue_base));
  assert(glue_len <= HV_GLUE_RESERVED_SIZE);

  memcpy((void*) HV_GLUE_START_CPA, (void*) glue_data, HV_GLUE_RESERVED_SIZE);
  flush_range((VA) HV_GLUE_START_CPA, HV_GLUE_RESERVED_SIZE);

  return (client_entrypt);
}


/** Start executing the client program on this tile.
 * @param client_entrypt Client physical address at which to start executing.
 */
void
start_client(CPA client_entrypt)
{
  INIT_TRACE("start_client(client_entrypt=%llx)\n", client_entrypt);

  // Set the minimum protection level for the exceptions that will be
  // handled by the client to the client's PL.

  set_client_mpls();

  // Enable client's fake P=V mode.

  enable_fake_physmem_mode();

  // Jump to the client's entry point at his PL.

  jump2_vapl(client_entrypt, CLIENT_PL);

  /*NOTREACHED*/
}


/** Load and then start executing the client program on this tile.
 */
void
load_and_start_client()
{
  if (config.clients[my_client].flags & CLIENT_BME)
  {
    config.clients[my_client].client_entry = load_bme();
    start_all_slave_clients();
    start_client_bme(config.clients[my_client].mem_base,
                     config.clients[my_client].mem_len,
                     config.clients[my_client].client_entry);
  }
  else
  {
    config.clients[my_client].client_entry = load_client();
    start_client(config.clients[my_client].client_entry);
  }
}


/** Handle the hv_init() syscall.
 * @param version The version of the hypervisor interface
 * that this program asserts it complies with, typically HV_VERSION.
 * @param chip_num Architecture number of the chip the client was built for.
 * @param chip_rev_num Revision number of the chip the client was built for.
 * @param client_pl Privilege level the client is built for
 *   (not required if interface_version_number == HV_VERSION_OLD_HV_INIT).
 */
void
syscall_init(int version, int chip_num, int chip_rev_num, int client_pl)
{
  SYSCALL_TRACE("init(ver=%d, num=%d, rev=%d, pl=%d)\n",
                version, chip_num, chip_rev_num, client_pl);

  // Version 11 clients differ from version 12 clients only in that they
  // don't expect the dimm[] information in the HV_MemoryControllerInfo.
  // Since that structure is returned in registers in any case, the extra
  // information will not be noticed by a version-11 client.
  if (version == 11)
    version = 12;

  // Version 13 clients expect client_pl to be set; version 12 clients
  // are similar but implicitly provide client_pl as 1.
  if (version == HV_VERSION_OLD_HV_INIT)
  {
    version = 13;
    client_pl = 1;
  }

  if (client_pl != 1 && client_pl != 2)
    panic("Client requested impossible PL %d\n", client_pl);
  if (client_pl != CLIENT_PL)
  {
    panic("This hypervisor is built to run clients at PL %d,\n"
          "but was asked to run the client at PL %d.\n"
#if CLIENT_PL == 2
          "You may rebuild the hypervisor with 'make HV_PL=2' (although\n"
          "this is deprecated) or configure newer Linuxes with KERNEL_PL=2.\n",
#else /* CLIENT_PL == 1 */
          "The standard MDE hypervisor should support this configuration,\n"
          "or you can configure Linux with KERNEL_PL=1.\n",
#endif
          CLIENT_PL, client_pl);
  }

  if (version != HV_VERSION)
    panic("Client built for hv version %d, but this hv is version %d\n",
          version, HV_VERSION);
  if (chip_num != __tile_chip__)
    panic("Client built for chip %d, but this hardware is chip %d\n",
          chip_num, __tile_chip__);
  if (chip_rev_num != __tile_chip_rev__)
    panic("Client built for chip rev %d, but this hardware is chip rev %d\n",
          chip_rev_num, __tile_chip_rev__);
}


/** Handle the hv_halt() syscall.
 */
void
syscall_halt()
{
  SYSCALL_TRACE("halt()\n");
  printf("Client requested halt.\n");
#ifdef DEBUG
  if (debug_flags & DEBUG_CYCLES)
    printf("Used %'lld cycles.\n", get_cycle_count() - init_cycle_count);
#endif
  exit(0);
}


/** Handle the hv_send_nmi() syscall.
 *  @param tile Tile to which the NMI request is sent.
 *  @param info NMI information which will be passed to the remote tile.
 *  @param flags Flags (HV_NMI_FLAG_xxx).
 *  @return Information about the requested NMI.
 */
HV_NMI_Info
syscall_send_nmi(HV_Coord tile, unsigned long info, uint64_t flags)
{
  HV_NMI_Info retval;

  SYSCALL_TRACE("send_nmi(tile: (%d, %d) info: %#lx, flags: %#llx)\n",
                tile.x, tile.y, info, flags);

  Lotar dest_lotar = HV_XY_TO_LOTAR(tile.x, tile.y);
  Lotar real_lotar;

  if (c2r_lotar(dest_lotar, &real_lotar) || real_lotar == my_lotar)
  {
    retval.result = HV_EINVAL;
    retval.pc = 0;
    return retval;
  }

  pos_t dest_pos = { .bits.x = HV_LOTAR_X(real_lotar),
                     .bits.y = HV_LOTAR_Y(real_lotar) };

  struct hv_msg_nmi nmi_msg =
  {
    .info = info,
    .flags = flags,
  };

  send_receive(dest_pos, HV_TAG_NMI, &nmi_msg, sizeof (nmi_msg),
               &retval, sizeof (retval), NULL, MSG_FLG_XMITFAIL);

  return retval;
}


/** Handle the hv_power_off() syscall.
 */
void
syscall_power_off()
{
  SYSCALL_TRACE("power_off()\n");
  printf("Client requested power-off.\n");
  ffsync(stdout);

  //
  // Wait for uart console device to transmit the last byte.
  //
  drv_udelay(100);

  bi_ptr_t resptr;

  if (bi_getparam(BI_TYPE_POWEROFF, 0, &resptr, NULL) != BI_NULL)
  {
    struct bi_poweroff* bi = resptr;
    drv_set_signal(bi->signal, DRV_SIGNAL_INIT | DRV_SIGNAL_ASSERT);
  }

  exit(0);
}


/** Handle the hv_restart() syscall.
 * @param cmd Const pointer to command to restart with, or NULL
 * @param arg Const pointer to argument string to restart with, or NULL
 */
void
syscall_restart(char* cmd, char* arg)
{
  char cmdstr[256];
  char argstr[256];

  SYSCALL_TRACE("restart(cmd=%p, arg=%p)\n", cmd, arg);

  if (cmd)
  {
    if (FAULT_BEGIN(cmd, sizeof (cmdstr)))
      panic("client requested restart, fault reading command");

    strncpy(cmdstr, cmd, sizeof (cmdstr));

    FAULT_END();
    cmdstr[sizeof (cmdstr) - 1] = '\0';
  }
  else
    cmdstr[0] = '\0';

  if (arg)
  {
    if (FAULT_BEGIN(arg, sizeof (argstr)))
      panic("client requested restart, fault reading args");

    strncpy(argstr, arg, sizeof (argstr));

    FAULT_END();
    argstr[sizeof (argstr) - 1] = '\0';
  }
  else
    argstr[0] = '\0';

  printf("Client requested restart, cmd = \"%s\", arg = \"%s\".\n", cmdstr,
         argstr);

  //
  // Parse command argument, if present.
  //
  const char* const sep = " \t";
  char* strtok_state;
  int altimage = 0;
  long wd_secs = 0;

  for (char* next_cmd = strtok_r(argstr, sep, &strtok_state);
       next_cmd;
       next_cmd = strtok_r(NULL, sep, &strtok_state))
  {
    const char* const kw_altimage = "altimage";
    const char* const kw_watchdog = "watchdog=";

    if (!strcmp(next_cmd, kw_altimage))
    {
      altimage = 1;
    }
    else if (!strncmp(next_cmd, kw_watchdog, strlen(kw_watchdog)))
    {
      next_cmd += strlen(kw_watchdog);
      if (str2l(next_cmd, NULL, 0, &wd_secs) ||
          wd_secs > SROMBOOT_SOFTREBOOT_WD_RMASK || wd_secs <= 0)
      {
        printf("hv_warning: unparseable or out-of-range %s reboot argument %s "
               "ignored\n", kw_watchdog, next_cmd);
        wd_secs = 0;
      }
    }
    else
    {
      printf("hv_warning: unrecognized reboot argument %s, ignored\n",
             next_cmd);
    }
  }

  uint32_t reset_flags = 0;
  if (altimage)
    reset_flags |= SROMBOOT_SOFTREBOOT_ACT_BADCRC;
  if (wd_secs)
    reset_flags |= wd_secs << SROMBOOT_SOFTREBOOT_WD_SHIFT;

  //
  // Once we have multi-client support we'll want to check to see whether
  // (a) this is the only client, or (b) this client has been given
  // permission to reset the whole chip.  For now, since we don't, we'll
  // just reset.  We'll only do so if we booted from the SROM (or, on Gx,
  // if we would reboot from the SROM after soft reset); otherwise, we
  // aren't likely to do anything useful.
  //
  RSH_BOOT_CONTROL_t rbc =
    {{ .boot_mode = RSH_BOOT_CONTROL__BOOT_MODE_VAL_NONE }};
  if (rshims[0])
    rbc.word = cfg_rd(rshims[0]->idn_ports[0].word, rshims[0]->channel,
                      RSH_BOOT_CONTROL);
  if (rbc.boot_mode == RSH_BOOT_CONTROL__BOOT_MODE_VAL_SPI)
  {
    printf("Resetting chip and restarting.\n");
    ffsync(stdout);

    //
    // Wait for uart console device to transmit the last byte.
    //
    drv_udelay(100);

    reset_chip(reset_flags);
  }
  else
    printf("Restart only possible after standalone boot; system halting.\n");

  //
  // reset_chip() shouldn't return, but just in case...
  //
  exit(0);
}


/** Handle the hv_reexec() syscall.
 * @param entry CPA of the entry point of the newly started client.
 * @return If unsuccessful, a hypervisor error code; if successful, does
 *   not return.
 */
int
syscall_reexec(CPA entry)
{
  PA dummy_pa;

  SYSCALL_TRACE("reexec(entry=%#llX)\n", entry);

  if (c2r_pa(entry, 8, &dummy_pa))
    return (HV_EFAULT);

  //
  // We invalidate the L2 because the new booted OS can't know what's in it;
  // hitting in the L2 when doing noncacheable operations can cause problems.
  // (On TILEPro, we flush the L2 on any tile which could have been
  // participating in hash-for-home caching.)  Likewise we invalidate the L1I
  // to keep things easy for the client.  Finally, we flush all of the TLBs to
  // prevent multiple match errors in case the new OS installs translations
  // which match ones currently in the TLB.
  //
  syscall_flush_remote(0, HV_FLUSH_ALL, (unsigned long *) -1,
                       0, 0, 0, NULL, NULL, 0);
  flush_icache();
  clean_itlb(0);
  clean_dtlb(0);

  start_client(entry);

  /* NOTREACHED */
}

/** Length of command line set via hv_set_command_line(); -1 means it
 *  hasn't been set */
static int command_line_len = -1;
/** Command line set via hv_set_command_line() */
static char command_line[HV_COMMAND_LINE_LEN];

/** Handle the hv_get_command_line() syscall.
 * @param buf The physical address to write the command-line string to
 * @param length The length of buf, in characters
 * @return The actual length of the command line (may be larger than "length")
 */
int
syscall_get_command_line(char* buf, int length)
{
  SYSCALL_TRACE("get_command_line(buf=%p, len=%d)\n", buf, length);

  if (command_line_len >= 0)
  {
    if (command_line_len > length - 1)
    {
      ON_FAULT_RETURN_EFAULT(buf, 1);

      *buf = 0;

      FAULT_END();

      return (command_line_len + 1);
    }

    ON_FAULT_RETURN_EFAULT(buf, length);

    memcpy(buf, command_line, command_line_len);
    buf[command_line_len] = 0;

    FAULT_END();

    return (command_line_len + 1);
  }

  if (config.clients[my_client].arg.len <= 0)
  {
    SYSCALL_TRACE("get_command_line returns 1 (no args file found)\n");

    ON_FAULT_RETURN_EFAULT(buf, 1);

    *buf = 0;

    FAULT_END();

    return (1);
  }

  int inode = config.clients[my_client].arg.ino;
  int offset = config.clients[my_client].arg.off;
  int flen = config.clients[my_client].arg.len;

  if (flen > length - 1)
  {
    SYSCALL_TRACE("get_command_line returns %d (buffer too short)\n", flen + 1);

    ON_FAULT_RETURN_EFAULT(buf, 1);

    *buf = 0;

    FAULT_END();

    return (flen + 1);
  }

  if (fs_pread_user(inode, buf, flen, offset) == HV_EFAULT)
    return (HV_EFAULT);

  ON_FAULT_RETURN_EFAULT(buf + flen, 1);

  buf[flen] = '\0';

  FAULT_END();

  SYSCALL_TRACE("get_command_line returns %d\n", flen + 1);
  return (flen + 1);
}


/** Handle the hv_set_command_line() syscall.
 */
int
syscall_set_command_line(char* buf, int length)
{
  SYSCALL_TRACE("set_command_line(buf=%p, length=%d)\n", buf, length);

  if (length > HV_COMMAND_LINE_LEN)
    return (HV_EINVAL);

  ON_FAULT_RETURN_EFAULT(buf, length);

  command_line_len = 0;

  memcpy(command_line, buf, length);

  FAULT_END();

  command_line_len = length;

  return (0);
}


/** Handle the hv_get_rtc() syscall.
 * @return Time from the RTC chip; or 00:00 Jan 1, 1970 if no chip.
 */
HV_RTCTime
syscall_get_rtc()
{
  HV_RTCTime tm = { 0 };

  SYSCALL_TRACE("get_rtc()\n");
  
  if (!rtc_read_time)
    init_rtc();

  rtc_read_time(&tm);

  return tm;
}


/** Handle the hv_set_rtc() syscall.
 */
void
syscall_set_rtc(HV_RTCTime tm)
{
  SYSCALL_TRACE("set_rtc()\n");

  if (!rtc_write_time)
    init_rtc();

  rtc_write_time(tm);
}

/** Helper for syscall_set_speed().
 * @param cycle Pointer to a returned cycle count.
 * @param uptime Pointer to a returned rshim uptime value (which counts
 *  reference clock cycles) which more-or-less corresponds to *cycle.
 */
static void
get_cycle_uptime(uint64_t* cycle, uint64_t* uptime)
{
  //
  // We do this 3 times to try and reduce cache effects.
  //
  for (int i = 0; i < 3; i++)
  {
    //
    // We measure the cycle counter before and after reading the rshim, and
    // take their mean as the closest estimate to the time the read
    // actually happened at the shim.
    //
    uint64_t cycle_start = get_cycle_count();
    *uptime = cfg_rd(rshims[0]->idn_ports[0].word,
                     rshims[0]->channel, RSH_UPTIME);
    uint64_t cycle_end = get_cycle_count();
    *cycle = (cycle_start + cycle_end) / 2;
  }
}


/** Handle the hv_set_speed() syscall.
 */
HV_SetSpeed
syscall_set_speed(unsigned long speed, uint64_t start_cycle,
                  unsigned long flags)
{
  //
  // Note that the hypervisor API states that the returned delta_ns and
  // end_cycle are undefined when we're not really setting the clock speed.
  // We calculate them in all cases just because it's simpler.  The
  // client generally provides a start_cycle of 0 in this case, which means
  // the delta_ns computation will overflow and provide bogus results, but
  // since it's undefined we don't care.
  //
  HV_SetSpeed rv;

  static const long ns_per_sec = 1000000000;

  SYSCALL_TRACE("set_speed(speed=%ld, cycle=0x%llx, flags=0x%lx)\n", speed,
                start_cycle, flags);

  //
  // We return an error here if DFS is disabled, even if they're not even
  // trying to change the speed, because that makes the Linux cpufreq
  // framework disable itself during initialization.  If we only complained
  // when the client tried to change the speed, the framework would
  // advertise itself but then fail to work, which is somewhat unfriendly.
  //
  if (!config.dfs_core || sim_is_simulator())
  {
    rv.new_speed = HV_EPERM;
    rv.end_cycle = 0;
    rv.delta_ns = 0;
    return rv;
  }

  //
  // Take our initial timestamp.
  //
  uint64_t my_start_cycle;
  uint64_t my_start_uptime;
  get_cycle_uptime(&my_start_cycle, &my_start_uptime);

  //
  // The first part of the returned delta_ns is the time between the user's
  // start_cycle, and the cycle we recorded, which matches the start of our
  // uptime interval.  We compute this now because it relies upon the
  // pre-change CPU speed.
  //
  rv.delta_ns = ((my_start_cycle - start_cycle) * ns_per_sec) / cpu_speed;

  //
  // Do the actual set_speed operation.
  //
  long new_speed = set_speed(speed, flags);

  //
  // Take our final timestamp.
  //
  uint64_t my_end_cycle;
  uint64_t my_end_uptime;
  get_cycle_uptime(&my_end_cycle, &my_end_uptime);

  rv.new_speed = new_speed; 
  rv.end_cycle = my_end_cycle;

  //
  // Now we augment delta_ns with the uptime interval, the end of which
  // matches the end cycle we'll return.
  //
  rv.delta_ns += ((my_end_uptime - my_start_uptime) * ns_per_sec) / REFCLK;

  return rv;
}


/** Handle the hv_start_all_tiles() syscall.
 */
void
syscall_start_all_tiles()
{
  static int slave_clients_started = 0;

  SYSCALL_TRACE("start_all_tiles()\n");

  if (config.clients[my_client].start_tile.word == my_pos.word &&
      !slave_clients_started)
  {
    start_all_slave_clients();
    slave_clients_started = 1;
  }
  else
    panic("start_all_tiles() called multiple times");
}


/** Handle the hv_sysconf() syscall.
 * @param query Which value is requested (HV_SYSCONF_xxx).
 * @return The requested value, or -1 the requested value is illegal or
 *         unavailable.
 */

long
syscall_sysconf(HV_SysconfQuery query)
{
  SYSCALL_TRACE("sysconf(query=%d)\n", query);

  switch (query)
  {
  case HV_SYSCONF_PAGE_SIZE_SMALL:
    return (page_size_small);

  case HV_SYSCONF_PAGE_SIZE_LARGE:
    return (page_size_large);

  case HV_SYSCONF_PAGE_SIZE_JUMBO:
    return (page_size_jumbo);

  case HV_SYSCONF_CPU_SPEED:
    return (cpu_speed);

  case HV_SYSCONF_CPU_TEMP:
    return temp_sensor[0]->sensor->read_cpu_temp(temp_sensor[0]);

  case HV_SYSCONF_BOARD_TEMP:
    return temp_sensor[0]->sensor->read_board_temp(temp_sensor[0]);

  default:
    return (-1);
  }
}


/** Find a specific string in the BIB; used to handle many simple
 *  syscall_confstr() requests.
 * @param type BIB item type.
 * @param instance BIB item instance.
 * @param retstr Pointer filled in with pointer to string, if found.
 * @param retlen Pointer filled in with length of string, if found.
 */
static void
simple_bib_confstr(int type, int instance, const char** retstr, int* retlen)
{
  uint32_t bidesc;
  bi_ptr_t bidata;

  bidesc = bi_getparam(type, instance, &bidata, NULL);
  if (bidesc != BI_NULL)
  {
    *retstr = (const char*) bidata;
    *retlen = strnlen(*retstr, BI_BYTES(bidesc));
  }
}


/** Handle the hv_confstr() syscall.
 * @param query Which value is requested (HV_CONFSTR_xxx).
 * @param buf Buffer in which to place the result, which will be
 *        null-terminated if there is sufficient room in the buffer.
 * @param len Length of the buffer.
 * @param arg0 Optional parameter identifying a specific object to be
 *        described; its presence and value depend upon query.
 * @param arg1 Optional parameter identifying a specific object to be
 *        described; its presence and value depend upon query.
 * @return If positive, the length of the requested value; if this is larger
 *        than len, the returned string will have been truncated.  If negative,
 *        a hypervisor error code.
 */
int
syscall_confstr(HV_ConfstrQuery query, char* buf, int len,
                long arg0, long arg1)
{
  SYSCALL_TRACE("confstr(query=%d, buf=%p, len=%d)\n", query, buf, len);

  const char* retstr = NULL;
  int retlen = 0;

  switch (query)
  {
  case HV_CONFSTR_BOARD_PART_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_PART_NUM, 0, &retstr, &retlen);
    break;

  case HV_CONFSTR_BOARD_SERIAL_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_SERIAL_NUM, 0, &retstr, &retlen);
    break;

  case HV_CONFSTR_CHIP_SERIAL_NUM:
    simple_bib_confstr(BI_TYPE_CHIP_NUM, 0, &retstr, &retlen);
    break;

  case HV_CONFSTR_BOARD_REV:
    simple_bib_confstr(BI_TYPE_BOARD_REV, 0, &retstr, &retlen);
    break;

  case HV_CONFSTR_HV_SW_VER:
  {
    //
    // Our version string has "version" at the start, so you can do something
    // like "strings binary | grep version" to get it.  We don't want to
    // return that in our response, though.
    //
    const int verlen = sizeof("version ") - 1;
    retstr = hv_version + verlen;
    retlen = strlen(retstr);
    break;
  }

  case HV_CONFSTR_CHIP_MODEL:
    retstr = CHIP_ARCH_NAME;
    retlen = strlen(retstr);
    break;

  case HV_CONFSTR_BOARD_DESC:
    simple_bib_confstr(BI_TYPE_BOARD_DESCRIPTION, 0, &retstr, &retlen);
    break;

  case HV_CONFSTR_HV_CONFIG:
  {
    int inode = fs_findfile(CONFIG_NAME);
    if (inode < 0)
    {
      ON_FAULT_RETURN_EFAULT(buf, 1);
      *buf = 0;
      FAULT_END();
      return (1);
    }

    int flen;
    unsigned int flags;
    fs_stat(inode, &flen, &flags);

    int copylen = (flen > len) ? len : flen;

    if (fs_pread_user(inode, buf, copylen, 0) == HV_EFAULT)
      return (HV_EFAULT);

    if (copylen < len)
    {
      ON_FAULT_RETURN_EFAULT(buf + copylen, 1);

      buf[copylen] = '\0';

      FAULT_END();
    }

    return (flen + 1);
  }

  case HV_CONFSTR_HV_CONFIG_VER:
  {
    if (config.config_ver.len <= 0)
    {
      ON_FAULT_RETURN_EFAULT(buf, 1);
      *buf = 0;
      FAULT_END();
      return (1);
    }

    int flen = config.config_ver.len;
    int copylen = (flen > len) ? len : flen;

    if (fs_pread_user(config.config_ver.ino, buf, copylen,
                      config.config_ver.off) == HV_EFAULT)
      return (HV_EFAULT);

    if (copylen < len)
    {
      ON_FAULT_RETURN_EFAULT(buf + copylen, 1);

      buf[copylen] = '\0';

      FAULT_END();
    }

    return (flen + 1);
  }

  case HV_CONFSTR_MEZZ_PART_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_PART_NUM, 1, &retstr, &retlen);
    break;

  case HV_CONFSTR_MEZZ_SERIAL_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_SERIAL_NUM, 1, &retstr, &retlen);
    break;

  case HV_CONFSTR_MEZZ_REV:
    simple_bib_confstr(BI_TYPE_BOARD_REV, 1, &retstr, &retlen);
    break;

  case HV_CONFSTR_MEZZ_DESC:
    simple_bib_confstr(BI_TYPE_BOARD_DESCRIPTION, 1, &retstr, &retlen);
    break;

  case HV_CONFSTR_CPUMOD_PART_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_PART_NUM, 2, &retstr, &retlen);
    break;

  case HV_CONFSTR_CPUMOD_SERIAL_NUM:
    simple_bib_confstr(BI_TYPE_BOARD_SERIAL_NUM, 2, &retstr, &retlen);
    break;

  case HV_CONFSTR_CPUMOD_REV:
    simple_bib_confstr(BI_TYPE_BOARD_REV, 2, &retstr, &retlen);
    break;

  case HV_CONFSTR_CPUMOD_DESC:
    simple_bib_confstr(BI_TYPE_BOARD_DESCRIPTION, 2, &retstr, &retlen);
    break;

  case HV_CONFSTR_SWITCH_CONTROL:
    break;

  case HV_CONFSTR_CHIP_REV:
    if (rshims[0])
    {
      static const struct rev2str
      {
        int rev;
        int fuses;
        char* str;
      }
      rev2str[] =
      {
#if __tile_chip__ == 10
        // Gx36
        { 0x20, 0, "A0" },
        { 0x21, 0, "A1" },
        { 0x21, 1, "A1B" },
        { 0x23, 1, "A2" },
        { 0x23, 2, "A2" },  // Gx16 package substrate
        { 0x27, 1, "A3" },
        { 0x27, 2, "A3" },  // Gx16 package substrate
        // Gx72
        { 0x40, 0, "A0" },
#endif
        // Null string is end of list
        { -1, 0, NULL },
      };

      RSH_REV_ID_t rri =
      {
        .word = cfg_rd(rshims[0]->idn_ports[0].word, rshims[0]->channel,
                       RSH_REV_ID)
      };
      int rev = rri.chip_rev_id;

      RSH_EFUSE_CTL_t rec = {{ .index = 14 }};
      cfg_wr(rshims[0]->idn_ports[0].word, rshims[0]->channel,
             RSH_EFUSE_CTL, rec.word);

      do
      {
        rec.word = cfg_rd(rshims[0]->idn_ports[0].word, rshims[0]->channel,
                          RSH_EFUSE_CTL);
      }
      while (rec.read_pend);

      int fuses = cfg_rd(rshims[0]->idn_ports[0].word, rshims[0]->channel,
                         RSH_EFUSE_DATA) & 0xF;

      const struct rev2str *r2s;

      for (r2s = rev2str; r2s->str; r2s++)
        if (r2s->rev == rev && r2s->fuses == fuses)
          break;

      retstr = r2s->str;

      if (!retstr)
      {
        char* tmpstr = alloca(128);
        snprintf(tmpstr, 128, "unknown_0x%x_0x%x", rev, fuses);
        retstr = tmpstr;
      }

      retlen = strlen(retstr);
    }
    break;

  case HV_CONFSTR_HV_STATS:
  {
    //
    // If stats aren't on, return an error; this keeps the client from
    // advertising stats when it won't be able to get them.
    //
    if (!config.stats)
      return HV_ENOTSUP;

    //
    // Make sure they're targeting a legal tile.
    //
    Lotar real_lotar;
    if (c2r_lotar(arg0, &real_lotar))
      return (HV_EINVAL);

    //
    // Get the string in a large buffer, and return it.  Worst-case output
    // is ~120 events * ~80 chars/line = 9600 bytes, so 16 K should be
    // fine.
    //
    const int buflen = 16 * 1024;

    char* tmpstr = alloca(buflen);
    size_t tmplen = get_stats_string(tmpstr, buflen, HV_LOTAR_X(real_lotar),
                                     HV_LOTAR_Y(real_lotar), arg1 & 1);
    retlen = min(tmplen, buflen);
    retstr = tmpstr;

    break;
  }

  default:
    return (HV_EINVAL);
  }

  //
  // This is a bit tricky; retstr isn't guaranteed to be null-terminated, but
  // our output should be unless there's no room for the null.
  //
  int bytes_to_copy;
  if (retlen < len)
    bytes_to_copy = retlen;
  else
    bytes_to_copy = len;

  ON_FAULT_RETURN_EFAULT(buf, len);

  memcpy(buf, retstr, bytes_to_copy);
  if (bytes_to_copy < len)
    buf[bytes_to_copy] = 0;

  FAULT_END();

  return (retlen + 1);
}


/** Handle the hv_get_ipi_pte() syscall.
 * @param tile Tile which will receive the IPI.
 * @param pl Indicates which IPI register set to use.
 * @param pte Filled with resulting PTE.
 * @return Zero if no error, non-zero for invalid parameters.
 */
int
syscall_get_ipi_pte(HV_Coord tile, int pl, HV_PTE* pte)
{
  // Make sure this is a legal tile and IPI #.
  Lotar client_lotar = HV_XY_TO_LOTAR(tile.x, tile.y);
  Lotar real_lotar;
  if (c2r_lotar(client_lotar, &real_lotar) || pl > CLIENT_PL)
    return (HV_EINVAL);

  // Construct the resulting pte.









  IPI_REMOTE_TRIGGER_ADDR_t addr = {{
    .tile_y = HV_LOTAR_Y(real_lotar),
    .tile_x = HV_LOTAR_X(real_lotar),
    .ipi = pl
  }};


  HV_PTE result = { 0 };
  result = hv_pte_set_mode(result, HV_PTE_MODE_MMIO);
  //
  // (0,0) is a special case meaning "Use the closest IPI shim".
  //
  result = hv_pte_set_lotar(result, HV_XY_TO_LOTAR(0, 0));
  result = hv_pte_set_pa(result, addr.word);

  // Write back result.
  ON_FAULT_RETURN_EFAULT(pte, sizeof(*pte));
  *pte = result;
  FAULT_END();

  return 0;
}



/** Handle the hv_store_mapping() syscall.
 * @param va Virtual address being mapped.
 * @param len Length of memory at va being mapped.
 * @param cpa Client physical address of memory.
 * @return Zero if no error; Non-zero if mapping not valid.
 */
uint32_t
syscall_store_mapping(VA va, uint32_t len, CPA cpa)
{
  PA pa;

  uint32_t err = drv_cpa2pa(cpa, len, &pa);

  if (err)
    return err;

#if HV_PL == 2
  hvgdb_store_mapping(va, len, pa);
#endif

  return 0;
}
