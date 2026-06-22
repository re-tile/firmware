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
 * Manage the "extra" hypervisor physical memory, and its virtual memory
 * overall.
 */

#ifndef _SYS_HV_MAPPING_H
#define _SYS_HV_MAPPING_H

#include <hv/hypervisor.h>

#include "param.h"
#include "tile.h"
#include "types.h"

extern char _sdata[];         /**< Start of data */
extern char _sdata_phys[];    /**< Start of data (PA) */
extern char _sstack[];        /**< Start of stack */
extern char _ebss[];          /**< End of uninitialized data area */
extern char _stext[];         /**< Start of text */
extern char _stext_phys[];    /**< Start of text (PA) */
extern char _etext[];         /**< End of text */
extern char _sshared[];       /**< Start of shared data */
extern char _eshared[];       /**< End of shared data */
extern char _sshared_phys[];  /**< Start of prototype shared data (PA) */
extern char _eshared_phys[];  /**< End of prototype shared data (PA) */

extern PA l2_flush_pa;
extern PA shared_mapping_table;
extern unsigned long shared_attr;

extern VA client_shared_client_va_base;
extern VA client_shared_client_va_last;

/** Structure defining a client-shared page. */
typedef struct
{
  uint8_t valid:1;     /**< Is there HV memory backing this page? */
  uint8_t readonly:1;  /**< Is this page read-only for the client? */
  uint8_t superonly:1; /**< Is this page accessible only to PL1? */
  PA pa;               /**< Physical address of the page */
  VA client_va;        /**< Client virtual address of the page */
  void* hv_addr;       /**< HV virtual address of the start of the page */
  int alloc_start;     /**< Offset of the next available byte in the page */
  int alloc_len;       /**< Number of remaining available bytes in the page */
}
client_shared_map_t;

extern client_shared_map_t client_shared_map[HV_NUM_CLIENT_SHARED_PAGES];

void init_local_alloc(void);
void init_va_pa_alloc(void);
void init_shared_alloc(struct slave_tile_state* sts);
void rehome_shared(void);
VA get_virt(size_t size, size_t alignment);
void free_virt(VA va, size_t size);
PA get_phys(PA size, size_t alignment);
PA avail_phys(size_t alignment);
PA vtop(VA va);
void* local_alloc(size_t size, size_t align);
void* shared_alloc(size_t size, size_t align);
void* client_shared_alloc(size_t size, size_t align, int readonly,
                          int superonly, VA* client_va);

int c_va_invalid(VA va, unsigned long len);

HV_VirtAddrRange syscall_inquire_virtual(int idx);

#endif /* _SYS_HV_MAPPING_H */
