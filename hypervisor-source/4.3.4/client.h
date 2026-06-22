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

#ifndef _SYS_HV_CLIENT_H
#define _SYS_HV_CLIENT_H

#include <hv/hypervisor.h>

#include "misc.h"
#include "tile_mask.h"
#include "types.h"

void determine_client_geometry(pos_t* ulhc, pos_t* lrhc, tile_mask* tiles);

void determine_client_home_map_tiles(tile_mask* home_map_tiles);

CPA load_client(void);
void start_client(CPA entry) __attribute__((__noreturn__));
void load_and_start_client(void);

void syscall_init(int version, int chip_num, int chip_rev_num, int client_pl);
void syscall_halt(void);
HV_NMI_Info syscall_send_nmi(HV_Coord tile, unsigned long info, uint64_t flags);
void syscall_power_off(void);
void syscall_restart(char* cmd, char* arg);
int syscall_reexec(CPA entry);
void syscall_start_all_tiles(void);
int syscall_get_command_line(char* buf, int length);
int syscall_set_command_line(char* buf, int length);
HV_RTCTime syscall_get_rtc(void);
void syscall_set_rtc(HV_RTCTime tm);
long syscall_sysconf(HV_SysconfQuery query);
int syscall_confstr(HV_ConfstrQuery query, char* buf, int len, long arg0,
                    long arg1);

#endif /* _SYS_HV_CLIENT_H */
