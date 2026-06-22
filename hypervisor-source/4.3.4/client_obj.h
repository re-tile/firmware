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
 * Routines to translate between client objects and real hardware objects.
 */

#ifndef _SYS_HV_CLIENT_OBJ_H
#define _SYS_HV_CLIENT_OBJ_H

#include <arch/chip.h>

#include <hv/hypervisor.h>

#include "misc.h"
#include "tile_mask.h"
#include "types.h"

void configure_client_geometry(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               tile_mask* tiles);
void get_client_geometry(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h,
                         client_tile_mask* c_tiles);
void get_client_flushinfo(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h,
                          client_tile_mask* flushable_tiles);
int c2r_lotar(Lotar client_lotar, Lotar* real_lotar);
int r2c_lotar(Lotar real_lotar, Lotar* client_lotar);
int allow_client_pte_lotar(pos_t position, Lotar* lotar);
void allow_client_pte_lotar_tile_mask(tile_mask* tiles);
int c2r_pte_lotar(Lotar client_lotar, Lotar* real_lotar);

int on_north_edge(Lotar client_lotar);
int on_south_edge(Lotar client_lotar);
int on_east_edge(Lotar client_lotar);
int on_west_edge(Lotar client_lotar);

void configure_client_asids(Asid base, Asid size);
int c2r_asid(Asid client_asid, Asid* real_asid);
int r2c_asid(Asid real_asid, Asid* client_asid);

void mask_to_home_map(tile_mask* home_map_tiles, uint32_t* home_map);
void configure_tile_home_mask(tile_mask* home_map_tiles);
void get_client_home_mask(client_tile_mask* home_map_mask);
void configure_tile_home_map(uint32_t* home_map);
void dump_home_map(uint32_t* home_map);

void configure_client_physmem(const PA base[MAX_MSHIMS],
                              const PA size[MAX_MSHIMS]);
uint32_t c2r_pa(CPA client_paddr, CPA len, PA* real_paddr);
uint32_t r2c_pa(PA real_paddr, PA len, CPA* client_paddr);
CPA client_mshim_size(int mshim);

HV_PhysAddrRange syscall_inquire_physical(int idx);
HV_ASIDRange syscall_inquire_asid(int idx);
HV_Topology syscall_inquire_topology(void);
HV_MemoryControllerInfo syscall_inquire_memory_controller(HV_Coord coord,
                                                          int idx);

/** Bit position of range bits within a CPA.  Note that Linux currently
 *  depends upon this (somewhat improperly, since it's not part of the
 *  HV API, but it's difficult to do the right thing at present). */











#define CPA_RANGE_SHIFT (CHIP_PA_WIDTH() - CHIP_LOG_NUM_MSHIMS())


extern int pg_shift_max;
extern PA client_fence_incoherent_pas[MAX_MSHIMS + 1];
extern PA hv_fence_incoherent_pas[MAX_MSHIMS + 1];

#endif /* _SYS_HV_CLIENT_OBJ_H */
