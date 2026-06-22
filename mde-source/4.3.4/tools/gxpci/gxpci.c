/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

#define _XOPEN_SOURCE 500 // Needed to get pread/pwrite from unistd.h

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <gxio/common.h>
#include <gxio/trio.h>
#include <tmc/cpus.h>
#include <tmc/alloc.h>
#include <tmc/task.h>
#include <tmc/interrupt.h>
#include <tmc/ipi.h>

#include <gxpci/gxpci.h>

extern int gxpci_open_pq_h2t_queue(gxpci_context_t *context);
extern int gxpci_open_pq_t2h_queue(gxpci_context_t *context);
extern int gxpci_open_pq_duplex_queue(gxpci_context_t *h2t_context,
                                      gxpci_context_t *t2h_context);
extern int gxpci_pq_h2t_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                                  int min, int max);
extern int gxpci_pq_t2h_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                                  int min, int max);
extern int gxpci_pq_h2t_get_cmd_credits(gxpci_context_t *context);
extern int gxpci_pq_t2h_get_cmd_credits(gxpci_context_t *context);
extern int gxpci_pq_destroy(gxpci_context_t *context);

extern int gxpci_alloc_c2c_resource(gxpci_context_t *send_context,
                                    gxpci_context_t *recv_context,
                                    unsigned int rem_link_index);
extern int gxpci_free_c2c_resource(gxio_trio_context_t *trio_context);

extern int gxpci_open_c2c_send_queue(gxpci_context_t *context);
extern int gxpci_open_c2c_recv_queue(gxpci_context_t *context,
                                     unsigned int pkt_headroom,
                                     unsigned int recv_buf_size);
extern int gxpci_c2c_send_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd);
extern int gxpci_c2c_recv_cmd(gxpci_context_t *context, const gxpci_cmd_t *cmd);
extern int gxpci_c2c_send_get_comps(gxpci_context_t *context,
                                    gxpci_comp_t* cpls, int min, int max);
extern int gxpci_c2c_recv_get_comps(gxpci_context_t *context,
                                    gxpci_comp_t* cpls, int min, int max);
extern int gxpci_c2c_send_get_cmd_credits(gxpci_context_t *context);
extern int gxpci_c2c_recv_get_cmd_credits(gxpci_context_t *context);
extern int gxpci_c2c_destroy(gxpci_context_t *context);

/* Utility routine to get local and remote BAR address and size. */
int
get_bar_addr(gxpci_context_t *context, int local,
             tilegxpci_bar_info_t *bar_info)
{
  char device_name[40];
  int ret;
  int fd;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem",
           context->trio_index, context->mac);

  fd = open(device_name, O_RDWR);
  if (fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  if (local)
    bar_info->link_index = TILEGXPCI_LOCAL_LINK_INDEX;
  else
    bar_info->link_index = context->rem_link_index;

  ret = ioctl(fd, TILEPCI_IOC_GET_BAR, bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s ioctl failure: %s\n", device_name, strerror(errno));
    ret = -errno;
  }

  close(fd);

  return ret;
}

int
gxpci_get_msix_info(gxpci_context_t *context, tilegxpci_msix_info_t *msix_info)
{
  char device_name[40];
  int ret = 0;
  int fd;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem",
           context->trio_index, context->mac);

  fd = open(device_name, O_RDWR);
  if (fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  ret = ioctl(fd, TILEPCI_IOC_GET_MSIX, msix_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s TILEPCI_IOC_GET_MSIX ioctl failure: %s\n", device_name,
            strerror(errno));
    ret = -errno;
  }

  close(fd);

  return ret;
}

/* Utility routine to get the local VF BAR info. */
int
get_vf_bar_addr(gxpci_context_t *context, tilegxpci_bar_info_t *bar_info)
{
  char device_name[40];
  int ret;
  int fd;

  snprintf(device_name, sizeof(device_name), "/dev/trio%d-mac%d/barmem_vf",
           context->trio_index, context->mac);

  fd = open(device_name, O_RDWR);
  if (fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", device_name, strerror(errno));
    return -errno;
  }

  ret = ioctl(fd, TILEPCI_IOC_GET_VF_BAR, bar_info);
  if (ret < 0)
  {
    fprintf(stderr, "%s ioctl failure: %s\n", device_name, strerror(errno));
    ret = -errno;
  }

  close(fd);

  return ret;
}

/**
 * Allocate and initialize the memory map region that backs the MMIO space
 * for a queue. Note that the map size must be 4KB-aligned. We will allocate
 * from scatter queue regions first, then from memory map regions.
 */
int
gxpci_alloc_mapping_region(gxio_trio_context_t *trio_context, void *target_mem,
                           size_t target_size, unsigned int asid,
                           unsigned int mac, uint64_t pci_address)
{
  int mem_map_index;
  int err;

  mem_map_index = gxio_trio_alloc_scatter_queues(trio_context, 1, 0, 0);
  if (mem_map_index < 0)
  {
    mem_map_index = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(mem_map_index, "gxio_trio_alloc_memory_maps()");

    err = gxio_trio_init_memory_map(trio_context, mem_map_index, target_mem,
                                    target_size, asid, mac, pci_address,
                                    GXIO_TRIO_ORDER_MODE_UNORDERED);
    GXIO_VERIFY_ZERO(err, "gxio_trio_init_memory_map()");
  }
  else
  {
    err = gxio_trio_init_scatter_queue(trio_context, mem_map_index,
                                       target_size, asid, mac, pci_address,
                                       GXIO_TRIO_ORDER_MODE_UNORDERED);
    GXIO_VERIFY_ZERO(err, "gxio_trio_init_scatter_queue()");

    //
    // Back up the SQ range with real memory.
    //
    gxio_trio_push_scatter_queue_buffer(trio_context, mem_map_index,
                                        target_mem, 0);
  }

  return 0;
}

int
gxpci_init(gxio_trio_context_t *trio_context, gxpci_context_t *context,
           unsigned int trio_index, unsigned int mac)
{
  if (trio_index >= TILEGX_NUM_TRIO || mac >= TILEGX_TRIO_PCIES)
    return GXPCI_EINVAL;

  //
  // Fill in the context structure.
  //
  memset(context, 0, sizeof(*context));
  context->trio_context = trio_context;
  context->trio_index = trio_index;
  context->mac = mac;

  return 0;
}

int
gxpci_open_queue(gxpci_context_t *context, int asid, gxpci_queue_type_t type,
                 unsigned int rem_link_index, unsigned int queue_index, 
                 unsigned int pkt_headroom, unsigned int recv_buf_size)
{
  //
  // Make sure we're bound to one specific tile.
  //
  if (tmc_cpus_get_my_cpu() < 0)
    return GXPCI_EBINDCPU;

  context->rem_link_index = rem_link_index;
  context->type = type;
  context->queue_index = queue_index;

  //
  // Allocate an ASID if it isn't pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    asid = gxio_trio_alloc_asids(context->trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }
  context->resource.asid = asid;

  switch (type)
  {
    case GXPCI_PQ_H2T:

      return gxpci_open_pq_h2t_queue(context);
  
    case GXPCI_PQ_T2H:

      return gxpci_open_pq_t2h_queue(context);

    case GXPCI_C2C_SEND:

      return gxpci_open_c2c_send_queue(context);
  
    case GXPCI_C2C_RECV:

      return gxpci_open_c2c_recv_queue(context, pkt_headroom, recv_buf_size);
   
    default:
      return GXPCI_EINVAL;
  } 
}

int
gxpci_open_duplex_queue(gxpci_context_t *h2t_context,
                        gxpci_context_t *t2h_context,
                        int asid, gxpci_queue_type_t type,
                        unsigned int rem_link_index,
                        unsigned int queue_index)
{
  //
  // Error checkings.
  //
  if (h2t_context->trio_context == NULL ||
      t2h_context->trio_context == NULL)
    return GXPCI_EINVAL;

  //
  // PQ H2T and T2H contexts of a duplex queue must be consistent.
  //
  if (h2t_context->trio_context != t2h_context->trio_context ||
      h2t_context->trio_index != t2h_context->trio_index ||
      h2t_context->mac != t2h_context->mac)
    return GXPCI_EINVAL;

  gxio_trio_context_t *trio_context = h2t_context->trio_context;

  //
  // Allocate an ASID if it isn't pre-allocated.
  //
  if (asid == GXIO_ASID_NULL)
  {
    asid = gxio_trio_alloc_asids(trio_context, 1, 0, 0);
    GXIO_VERIFY_NON_NEGATIVE(asid, "gxio_trio_alloc_asids()");
  }
  h2t_context->resource.asid = asid;
  t2h_context->resource.asid = asid;

  switch (type)
  {
    case GXPCI_PQ_DUPLEX:

      h2t_context->type = GXPCI_PQ_H2T;
      t2h_context->type = GXPCI_PQ_T2H;
      h2t_context->queue_index = queue_index;
      t2h_context->queue_index = queue_index;

      return gxpci_open_pq_duplex_queue(h2t_context, t2h_context);

    case GXPCI_C2C_DUPLEX:

      // t2h_context refers to send_context, h2t_context to recv_context.
      return gxpci_alloc_c2c_resource(t2h_context, h2t_context, rem_link_index);

    default:
      return GXPCI_EINVAL;
  }
}

int
gxpci_post_cmd(gxpci_context_t *context, const gxpci_cmd_t* cmd)
{
  gxpci_queue_type_t type = context->type;

  switch (type)
  {
    case GXPCI_PQ_H2T:
    case GXPCI_PQ_H2T_VF:

      return gxpci_pq_h2t_cmd(context, cmd);

    case GXPCI_PQ_T2H:
    case GXPCI_PQ_T2H_VF:

      return gxpci_pq_t2h_cmd(context, cmd);

    case GXPCI_C2C_SEND:

      return gxpci_c2c_send_cmd(context, cmd);

    case GXPCI_C2C_RECV:

      return gxpci_c2c_recv_cmd(context, cmd);

    default:
      return GXPCI_EINVAL;
  }
}

int
gxpci_get_comps(gxpci_context_t *context, gxpci_comp_t* cpls,
                int min, int max)
{
  gxpci_queue_type_t type = context->type;

  switch (type)
  {
    case GXPCI_PQ_H2T:
    case GXPCI_PQ_H2T_VF:

      return gxpci_pq_h2t_get_comps(context, cpls, min, max);

    case GXPCI_PQ_T2H:
    case GXPCI_PQ_T2H_VF:

      return gxpci_pq_t2h_get_comps(context, cpls, min, max);

    case GXPCI_C2C_SEND:

      return gxpci_c2c_send_get_comps(context, cpls, min, max);

    case GXPCI_C2C_RECV:

      return gxpci_c2c_recv_get_comps(context, cpls, min, max);

    default:
      return GXPCI_EINVAL;
  }
}

int
gxpci_get_cmd_credits(gxpci_context_t *context)
{
  gxpci_queue_type_t type = context->type;

  switch (type)
  {
    case GXPCI_PQ_H2T:
    case GXPCI_PQ_H2T_VF:

      return gxpci_pq_h2t_get_cmd_credits(context);

    case GXPCI_PQ_T2H:
    case GXPCI_PQ_T2H_VF:

      return gxpci_pq_t2h_get_cmd_credits(context);

    case GXPCI_C2C_SEND:

      return gxpci_c2c_send_get_cmd_credits(context);

    case GXPCI_C2C_RECV:

      return gxpci_c2c_recv_get_cmd_credits(context);

    default: 
      return GXPCI_EINVAL;
  }
}

int
gxpci_iomem_register(gxpci_context_t *context, void *va, size_t size)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  int asid = context->resource.asid;
  int err;

  err = gxio_trio_register_page(trio_context, asid, va, size, 0);

  return err;
}
 
int
gxpci_destroy(gxpci_context_t *context)
{
  gxio_trio_context_t *trio_context = context->trio_context;
  gxpci_queue_type_t type = context->type;

  switch (type)
  {
    case GXPCI_PQ_H2T:
    case GXPCI_PQ_T2H:
    case GXPCI_PQ_H2T_VF:
    case GXPCI_PQ_T2H_VF:
      gxpci_pq_destroy(context);

      break;

    case GXPCI_C2C_SEND:
    case GXPCI_C2C_RECV:
      gxpci_c2c_destroy(context);

      break;

    default: 
      return GXPCI_EINVAL;
  }

  return gxio_trio_destroy(trio_context);
}

int
gxpci_destroy_duplex(gxpci_context_t *h2t_context,
                     gxpci_context_t *t2h_context,
                     gxpci_queue_type_t type)
{
  gxio_trio_context_t *trio_context = h2t_context->trio_context;

  switch (type)
  {
    case GXPCI_PQ_DUPLEX:
    case GXPCI_PQ_DUPLEX_VF:
      gxpci_pq_destroy(h2t_context);
      gxpci_pq_destroy(t2h_context);

      return gxio_trio_destroy(trio_context);

    case GXPCI_C2C_DUPLEX:

      return gxpci_free_c2c_resource(trio_context);;

    default:
      return GXPCI_EINVAL;
  }
}

static const char* const error_strings[(GXPCI_ERR_MAX - GXPCI_ERR_MIN) + 1] = {
  "Invalid argument",
  "Task not bound to exactly one cpu",
  "Insufficient command credits",
};

const char*
gxpci_strerror(int gxpci_errno)
{
  // Zero is a special case.
  if (gxpci_errno == 0)
    return "operation successful";

  // If it's outside our range, let gxio handle it.
  if (gxpci_errno < GXPCI_ERR_MIN || gxpci_errno > GXPCI_ERR_MAX)
    return gxio_strerror(gxpci_errno);

  // Return our error string.
  int err_index = (-gxpci_errno) - (-GXPCI_ERR_MAX);
  return error_strings[err_index];
}
