/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
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

/**
 * @file
 * Implementation of mpipe gxio calls.
 */


#define _POSIX_C_SOURCE 199309L

#include "gxio/mpipe.h"

#ifndef __NEWLIB__
#include <pthread.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include <arch/cycle.h>
#include <arch/spr_def.h>

#include "mpipe_rpc_call.h"
#include "mpipe_info_rpc_call.h"



/* HACK: Avoid pointless "shadow" warnings. */
#define link link_shadow

#if !defined(__NEWLIB__)
/*
 * Set up weak linkage to the pthread lock/unlock functions so we can
 * avoid calling them in non-pthreaded programs.
 */
extern typeof(pthread_mutex_lock) pthread_mutex_lock __attribute__((weak));
extern typeof(pthread_mutex_unlock) pthread_mutex_unlock __attribute__((weak));
#endif

#if   !defined(__NEWLIB__)
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif


static gxio_mpipe_context_t  mpipe_context[GXIO_MPIPE_INSTANCE_MAX];

/*
 * gxio mpipe contexts.
 */
gxio_mpipe_context_t *gxio_mpipe_context[GXIO_MPIPE_INSTANCE_MAX] =
      { NULL, NULL };

static unsigned int mpipe_mode_flags;

#if   !defined(__NEWLIB__)
#define MUTEX_LOCK(_mutexp)                     \
  do {                                          \
    if (pthread_mutex_lock)                     \
      pthread_mutex_lock(_mutexp);              \
  } while(0)
#define MUTEX_UNLOCK(_mutexp)                     \
  do {                                            \
    if (pthread_mutex_unlock)                     \
      pthread_mutex_unlock(_mutexp);              \
  } while(0)
#else
#define  MUTEX_LOCK(_mutexp)
#define  MUTEX_UNLOCK(_mutexp)
#endif

int
__gxio_mpipe_init(gxio_mpipe_context_t* context, unsigned int mpipe_index,
                  unsigned int mode_flags)
{
  bool context_is_null = false;;
  gxio_mpipe_context_t context_local;
  char file[32];
  int fd;
  int i;

  if (mpipe_index >= GXIO_MPIPE_INSTANCE_MAX)
    return -EINVAL;

  /* Set the gxio_mpipe_mode. */
  MUTEX_LOCK(&mutex);
  if (!mpipe_mode_flags && mode_flags)
    mpipe_mode_flags = mode_flags;
  MUTEX_UNLOCK(&mutex);

  if (!context)
  {
    context_is_null = true;
    context = &context_local;
    memset(context, 0, sizeof(context_local));
  }

  if (context_is_null)
  {
    /* Check if the gxio_mpipe_context[mpipe_index] is initialized. */
    MUTEX_LOCK(&mutex);
    if (gxio_mpipe_context[mpipe_index])
    {
      /* The context is already initialized. */
      MUTEX_UNLOCK(&mutex);
      return 0;
    }
    MUTEX_UNLOCK(&mutex);
  }

  snprintf(file, sizeof(file), "/dev/iorpc/mpipe%d", mpipe_index);
  fd = open(file, O_RDWR);

  context->fd = fd;

  if (fd < 0)
    goto open_failed;

  /* Map in the MMIO space. */
  context->mmio_cfg_base =
    mmap(NULL, HV_MPIPE_CONFIG_MMIO_SIZE, PROT_READ,
         MAP_SHARED, context->fd, HV_MPIPE_CONFIG_MMIO_OFFSET);
  if (context->mmio_cfg_base == MAP_FAILED)
    goto cfg_failed;

  context->mmio_fast_base =
    mmap(NULL, HV_MPIPE_FAST_MMIO_SIZE, PROT_READ | PROT_WRITE,
         MAP_SHARED, context->fd, HV_MPIPE_FAST_MMIO_OFFSET);
  if (context->mmio_fast_base == MAP_FAILED)
    goto fast_failed;


  /* Initialize the stacks. */
  for (i = 0; i < 8; i++)
    context->__stacks.stacks[i] = 255;

  context->instance = mpipe_index;


  if (context_is_null)
  {
    MUTEX_LOCK(&mutex);

    if (gxio_mpipe_context[mpipe_index])
    {
      /* gxio_mpipe_context[mpipe_index] is initialized.
       * Destroy the context, then return.
       */
      gxio_mpipe_destroy(context);
      MUTEX_UNLOCK(&mutex);
      return 0;
    }

    /* Copy context into mpipe_context[mpipe_index]. */
    memcpy(&mpipe_context[mpipe_index], context,
           sizeof(gxio_mpipe_context_t));

    /* Assign gxio_mpipe_context[mpipe_index]. */
    gxio_mpipe_context[mpipe_index] = &mpipe_context[mpipe_index];

    MUTEX_UNLOCK(&mutex);
  }


  return 0;

 fast_failed:
  munmap(context->mmio_cfg_base, HV_MPIPE_CONFIG_MMIO_SIZE);
 cfg_failed:
  close(context->fd);
  context->fd = -1;
 open_failed:
  return -errno;
}


// Support 4.1.4 and earlier ABI for backwards binary compatibility
int
gxio_mpipe_init_414(gxio_mpipe_context_t* context, unsigned int mpipe_instance)
  __asm__("gxio_mpipe_init");
int
gxio_mpipe_init_414(gxio_mpipe_context_t* context, unsigned int mpipe_instance)
{
  return gxio_mpipe_init(context, mpipe_instance);
}


int
gxio_mpipe_destroy(gxio_mpipe_context_t* context)
{
  int retval;

  if (!(context->instance < GXIO_MPIPE_INSTANCE_MAX &&
        context->instance >= 0) ||
      !context->mmio_fast_base || context->fd < 0)
    return -EINVAL;
  munmap(context->mmio_cfg_base, HV_MPIPE_CONFIG_MMIO_SIZE);
  munmap(context->mmio_fast_base, HV_MPIPE_FAST_MMIO_SIZE);
  retval = close(context->fd) ? -errno : 0;
  if (!retval)
  {
    MUTEX_LOCK(&mutex);
    if (context == gxio_mpipe_context[context->instance])
      gxio_mpipe_context[context->instance] = NULL;
    MUTEX_UNLOCK(&mutex);
  }
  return retval;
}


static int16_t gxio_mpipe_buffer_sizes[8] =
  { 128, 256, 512, 1024, 1664, 4096, 10368, 16384 };


gxio_mpipe_buffer_size_enum_t
gxio_mpipe_buffer_size_to_buffer_size_enum(size_t size)
{
  int i;
  for (i = 0; i < 7; i++)
    if (size <= gxio_mpipe_buffer_sizes[i])
      break;
  return i;
}


size_t
gxio_mpipe_buffer_size_enum_to_buffer_size(gxio_mpipe_buffer_size_enum_t
                                           buffer_size_enum)
{
  if (buffer_size_enum > 7)
    buffer_size_enum = 7;

  return gxio_mpipe_buffer_sizes[buffer_size_enum];
}


size_t
gxio_mpipe_calc_buffer_stack_bytes(unsigned long buffers)
{
  const int BUFFERS_PER_LINE = 12;

  /* Count the number of cachelines. */
  unsigned long lines = (buffers + BUFFERS_PER_LINE - 1) / BUFFERS_PER_LINE;

  /* Convert to bytes. */
  return lines * CHIP_L2_LINE_SIZE();
}

int
gxio_mpipe_alloc_buffer_stacks(gxio_mpipe_context_t* context,
                               unsigned int count, unsigned int first,
                               unsigned int flags)
{
  int retval;
  gxio_mpipe_context_t *other_context = NULL;

  if (mpipe_mode_flags & GXIO_MPIPE_MULTI_MPIPES)
  {
    other_context = gxio_mpipe_context[context->instance ? 0 : 1];

    if (other_context)
      flags |= HV_MPIPE_ALLOC_MULTI_MPIPES;
  }

  retval = gxio_mpipe_alloc_buffer_stacks_aux(context, count, first, flags);

  if (retval < 0)
    return retval;

  if (other_context)
  {
    first = retval;
    flags =  HV_MPIPE_ALLOC_MULTI_MPIPES_FIXED;
    retval = gxio_mpipe_alloc_buffer_stacks_aux(other_context, count, first, flags);
  }

  return retval;
}

int
gxio_mpipe_init_buffer_stack(gxio_mpipe_context_t* context,
                             unsigned int stack,
                             gxio_mpipe_buffer_size_enum_t buffer_size_enum,
                             void* mem, size_t mem_size,
                             unsigned int mem_flags)
{
  int result;

  memset(mem, 0, mem_size);

  result =
    gxio_mpipe_init_buffer_stack_aux(context, mem, mem_size, mem_flags,
                                     stack, buffer_size_enum);
  if (result < 0)
    return result;

  /* Save the stack. */
  context->__stacks.stacks[buffer_size_enum] = stack;

  return 0;
}



int
gxio_mpipe_register_page(gxio_mpipe_context_t* context,
                           unsigned int stack,
                           void* page, size_t page_size,
                           unsigned int page_flags)
{
  int i, retval;
  retval = gxio_mpipe_register_page_aux(context, page, page_size, page_flags,
                                        stack, (unsigned long)page >> 12);
  /* return upon error! */
  if (retval < 0)
    return retval;

  if (mpipe_mode_flags & GXIO_MPIPE_MULTI_MPIPES)
  {
    /* Loop for all other mpipe. */
    for (i = 0; i < GXIO_MPIPE_INSTANCE_MAX; i++)
    {
      gxio_mpipe_context_t* context_other;

      /* Skip one mpipe just did. */
      if (context->instance == i)
        continue;

      context_other = gxio_mpipe_context[i];

      /* context_other = NULL indicates no other mpipe instances. */
      if (context_other == NULL)
        break;

      retval = gxio_mpipe_register_page_aux(context_other, page, page_size,
                                            page_flags,
                                            stack, (unsigned long)page >> 12);
      if (retval < 0)
        return retval;
    }
  }

  return 0;
}



int
gxio_mpipe_init_notif_ring(gxio_mpipe_context_t* context,
                           unsigned int ring,
                           void* mem, size_t mem_size,
                           unsigned int mem_flags)
{
  return gxio_mpipe_init_notif_ring_aux(context, mem, mem_size, mem_flags,
                                        ring);
}


int
gxio_mpipe_init_notif_group_and_buckets(gxio_mpipe_context_t* context,
                                        unsigned int group,
                                        unsigned int ring,
                                        unsigned int num_rings,
                                        unsigned int bucket,
                                        unsigned int num_buckets,
                                        gxio_mpipe_bucket_mode_t mode)
{
  int i;
  int result;

  gxio_mpipe_bucket_info_t bucket_info = {{
      .group = group,
      .mode = mode,
    }};

  gxio_mpipe_notif_group_bits_t bits = {{ 0 }};

  for (i = 0; i < num_rings; i++)
    gxio_mpipe_notif_group_add_ring(&bits, ring + i);

  result = gxio_mpipe_init_notif_group(context, group, bits);
  if (result != 0)
    return result;

  for (i = 0; i < num_buckets; i++)
  {
    bucket_info.notifring = ring + (i % num_rings);

    result = gxio_mpipe_init_bucket(context, bucket + i, bucket_info);
    if (result != 0)
      return result;
  }

  return 0;
}


int
gxio_mpipe_init_edma_ring(gxio_mpipe_context_t* context,
                          unsigned int ring, unsigned int channel,
                          void* mem, size_t mem_size, unsigned int mem_flags)
{
  memset(mem, 0, mem_size);

  return gxio_mpipe_init_edma_ring_aux(context, mem, mem_size, mem_flags,
                                       ring, channel);
}



int
gxio_mpipe_classifier_load_from_file(gxio_mpipe_context_t* context,
                                     const char* path)
{
  int fd = open(path, O_RDONLY);

  if (fd < 0)
    return -errno;

  struct stat sb;
  if (fstat(fd, &sb) != 0)
    return -errno;

  unsigned int size = sb.st_size;

  char buf[64*1024];
  if (size > sizeof(buf))
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;

  /* FIXME: Avoid interrupts. */
  ssize_t n = read(fd, buf, size);
  if ((unsigned int)n != size)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;

  int result = gxio_mpipe_classifier_load_from_bytes(context, buf, size);

  close(fd);

  return result;
}


int
gxio_mpipe_classifier_customize(gxio_mpipe_context_t* context,
                                const char* symbol,
                                const void* data, size_t len)
{
  _gxio_mpipe_symbol_name_t name;

  strncpy(name.name, symbol, sizeof(name.name));
  name.name[GXIO_MPIPE_SYMBOL_NAME_LEN - 1] = '\0';

  return gxio_mpipe_classifier_set_memory(context, name, data, len);
}



void
gxio_mpipe_rules_init(gxio_mpipe_rules_t* rules,
                      gxio_mpipe_context_t* context)
{
  rules->context = context;
  memset(&rules->list, 0, sizeof(rules->list));
}


int
gxio_mpipe_rules_begin(gxio_mpipe_rules_t* rules,
                       unsigned int bucket, unsigned int num_buckets,
                       gxio_mpipe_rules_stacks_t* stacks)
{
  int i;
  int stack = 255;

  gxio_mpipe_rules_list_t* list = &rules->list;

  /* Current rule. */
  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  unsigned int head = list->tail;

  /*
   * Align next rule properly.
   * Note that "dmacs_and_vlans" will also be aligned.
   */
  unsigned int pad = 0;
  while (((head + pad) % __alignof__(gxio_mpipe_rules_rule_t)) != 0)
    pad++;

  /*
   * Verify room.
   * ISSUE: Mark rules as broken on error?
   */
  if (head + pad + sizeof(*rule) >= sizeof(list->rules))
    return GXIO_MPIPE_ERR_RULES_FULL;

  /* Verify num_buckets is a power of 2. */
  if (__builtin_popcount(num_buckets) != 1)
    return GXIO_MPIPE_ERR_RULES_INVALID;

  /* Add padding to previous rule. */
  rule->size += pad;

  /* Start a new rule. */
  list->head = head + pad;

  rule = (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Default some values. */
  rule->headroom = 2;
  rule->tailroom = 0;
  rule->capacity = 16384;

  /* Save the bucket info. */
  rule->bucket_mask = num_buckets - 1;
  rule->bucket_first = bucket;

  for (i = 8 - 1; i >= 0; i--)
  {
    int maybe =
      stacks ? stacks->stacks[i] : rules->context->__stacks.stacks[i];
    if (maybe != 255)
      stack = maybe;
    rule->stacks.stacks[i] = stack;
  }

  if (stack == 255)
    return GXIO_MPIPE_ERR_RULES_INVALID;

  /* NOTE: Only entries at the end of the array can be 255. */
  for (i = 8 - 1; i > 0; i--)
  {
    if (rule->stacks.stacks[i] == 255)
    {
      rule->stacks.stacks[i] = stack;
      rule->capacity = gxio_mpipe_buffer_size_enum_to_buffer_size(i - 1);
    }
  }

  rule->size = sizeof(*rule);
  list->tail = list->head + rule->size;

  return 0;
}


int
gxio_mpipe_rules_add_channel(gxio_mpipe_rules_t* rules,
                             unsigned int channel)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify channel. */
  if (channel >= 32)
    return GXIO_MPIPE_ERR_RULES_INVALID;

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  rule->channel_bits |= (1UL << channel);

  return 0;
}



int
gxio_mpipe_rules_set_priority(gxio_mpipe_rules_t* rules,
                              int priority)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  rule->priority = priority;

  return 0;
}



int
gxio_mpipe_rules_set_headroom(gxio_mpipe_rules_t* rules,
                              uint8_t headroom)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  rule->headroom = headroom;

  return 0;
}



int
gxio_mpipe_rules_set_tailroom(gxio_mpipe_rules_t* rules,
                              uint8_t tailroom)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  rule->tailroom = tailroom;

  return 0;
}


int
gxio_mpipe_rules_set_capacity(gxio_mpipe_rules_t* rules,
                              uint16_t capacity)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  rule->capacity = capacity;

  return 0;
}


int
gxio_mpipe_rules_add_dmac(gxio_mpipe_rules_t* rules,
                          gxio_mpipe_rules_dmac_t dmac)
{
  int i;
  uint8_t* base;
  uint8_t* ptr;

  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  base = rule->dmacs_and_vlans;

  /* Collapse duplicates. */
  for (i = 0; i < rule->num_dmacs; i++)
  {
    uint8_t* old = base + i * sizeof(dmac);
    if (memcmp(old, &dmac, sizeof(dmac)) == 0)
      return 0;
  }

  /*
   * Verify room.
   * ISSUE: Mark rules as broken on error?
   */
  if (list->tail + sizeof(dmac) >= sizeof(list->rules))
    return GXIO_MPIPE_ERR_RULES_FULL;

  ptr = base + rule->num_dmacs * sizeof(dmac);

  /* Slide down any vlans. */
  if (rule->num_vlans != 0)
    memmove(ptr + sizeof(dmac), ptr, rule->num_vlans * 2);

  *(gxio_mpipe_rules_dmac_t*)ptr = dmac;

  list->tail += sizeof(dmac);
  rule->size += sizeof(dmac);
  rule->num_dmacs++;

  return 0;
}


int
gxio_mpipe_rules_add_vlan(gxio_mpipe_rules_t* rules,
                          gxio_mpipe_rules_vlan_t vlan)
{
  gxio_mpipe_rules_list_t* list = &rules->list;

  gxio_mpipe_rules_rule_t* rule =
    (gxio_mpipe_rules_rule_t*)(list->rules + list->head);

  uint8_t* base;
  int i;
  uint8_t* ptr;

  /* Verify begun. */
  if (list->tail == 0)
    return GXIO_MPIPE_ERR_RULES_EMPTY;

  base = rule->dmacs_and_vlans + rule->num_dmacs * 6;

  /* Collapse duplicates. */
  for (i = 0; i < rule->num_vlans; i++)
  {
    uint8_t* old = base + i * sizeof(vlan);
    if (*(gxio_mpipe_rules_vlan_t*)old == vlan)
      return 0;
  }

  /*
   * Verify room.
   * ISSUE: Mark rules as broken on error?
   */
  if (list->tail + sizeof(vlan) >= sizeof(list->rules))
    return GXIO_MPIPE_ERR_RULES_FULL;

  ptr = base + rule->num_vlans * sizeof(vlan);

  *(gxio_mpipe_rules_vlan_t*)ptr = vlan;

  list->tail += sizeof(vlan);
  rule->size += sizeof(vlan);
  rule->num_vlans++;

  return 0;
}


int
gxio_mpipe_rules_commit(gxio_mpipe_rules_t* rules)
{
  gxio_mpipe_rules_list_t* list = &rules->list;
  unsigned int size = offsetof(gxio_mpipe_rules_list_t, rules) + list->tail;
  return gxio_mpipe_commit_rules(rules->context, list, size);
}


int
gxio_mpipe_iqueue_init(gxio_mpipe_iqueue_t* iqueue,
                       gxio_mpipe_context_t* context,
                       unsigned int ring,
                       void* mem, size_t mem_size, unsigned int mem_flags)
{
  /* The init call below will verify that "mem_size" is legal. */
  unsigned int num_entries = mem_size / sizeof(gxio_mpipe_idesc_t);

  iqueue->context = context;
  iqueue->idescs = (gxio_mpipe_idesc_t*) mem;
  iqueue->ring = ring;
  iqueue->num_entries = num_entries;
  iqueue->mask_num_entries = num_entries - 1;
  iqueue->log2_num_entries = __builtin_ctz(num_entries);
  iqueue->head = 1;
#ifdef __BIG_ENDIAN__
  iqueue->swapped = 0;
#endif

  /* Initialize the "tail". */
  __gxio_mmio_write(mem, iqueue->head);

  return gxio_mpipe_init_notif_ring(context, ring, mem, mem_size, mem_flags);
}



/*
 * Helper function for "gxio_mpipe_iqueue_peek()" so that backtraces
 * and profiles will clearly indicate that we're wasting time waiting.
 */
static int __NEVER_INLINE
__gxio_mpipe_iqueue_wait_for_packets(gxio_mpipe_iqueue_t* iqueue,
                                     gxio_mpipe_idesc_t** idesc_ref)
{
  int backoff = 1;
  while (1)
  {
    int i, result;
    for (i = backoff; i > 0; i--)
      __insn_mfspr(SPR_PASS);
    if (backoff < 128)
      backoff *= 2;

    result = gxio_mpipe_iqueue_try_peek(iqueue, idesc_ref);
    if (result != GXIO_MPIPE_ERR_IQUEUE_EMPTY)
      return result;
  }
}


int
gxio_mpipe_iqueue_peek(gxio_mpipe_iqueue_t* iqueue,
                       gxio_mpipe_idesc_t** idesc_ref)
{
  int result = gxio_mpipe_iqueue_try_peek(iqueue, idesc_ref);
  if (result != GXIO_MPIPE_ERR_IQUEUE_EMPTY)
    return result;

  return __gxio_mpipe_iqueue_wait_for_packets(iqueue, idesc_ref);
}


int
gxio_mpipe_iqueue_try_get(gxio_mpipe_iqueue_t* iqueue,
                          gxio_mpipe_idesc_t* idesc)
{
  gxio_mpipe_idesc_t* next;
  int result = gxio_mpipe_iqueue_try_peek(iqueue, &next);
  if (result < 0)
    return result;
  *idesc = *next;
  gxio_mpipe_iqueue_consume(iqueue, next);
  return 0;
}


int
gxio_mpipe_iqueue_get(gxio_mpipe_iqueue_t* iqueue,
                      gxio_mpipe_idesc_t* idesc)
{
  gxio_mpipe_idesc_t* next;
  int result = gxio_mpipe_iqueue_peek(iqueue, &next);
  if (result < 0)
    return result;
  *idesc = *next;
  gxio_mpipe_iqueue_consume(iqueue, next);
  return 0;
}



int
gxio_mpipe_equeue_init(gxio_mpipe_equeue_t* equeue,
                       gxio_mpipe_context_t* context,
                       unsigned int ering,
                       unsigned int channel,
                       void* mem, unsigned int mem_size,
                       unsigned int mem_flags)
{
  /* The init call below will verify that "mem_size" is legal. */
  unsigned int num_entries = mem_size / sizeof(gxio_mpipe_edesc_t);

  /* Offset used to read number of completed commands. */
  MPIPE_EDMA_POST_REGION_ADDR_t offset;
  int i;
  unsigned int flags = 0;

  int result =
    gxio_mpipe_init_edma_ring(context, ering, channel,
                              mem, mem_size, mem_flags);
  if (result < 0)
    return result;

  memset(equeue, 0, sizeof(*equeue));

  offset.word = 0;
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_EDMA - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  offset.ring = ering;

  if (mpipe_mode_flags & GXIO_MPIPE_RHWB)
    flags |= DMA_QUEUE_EQUEUE_RHWB;

  equeue->context = context;
  equeue->other_context = NULL;
  if (mpipe_mode_flags & GXIO_MPIPE_MULTI_MPIPES)
  {
    equeue->other_context = gxio_mpipe_context[1 ^ context->instance];
  }
  /*
   * If there is no other mPIPE instance, disable the Remote Buffer Return.
   */
  if (equeue->other_context == NULL)
    flags &= ~DMA_QUEUE_EQUEUE_RHWB;

  __gxio_dma_queue_init(&equeue->dma_queue,
                        context->mmio_fast_base + offset.word,
                        num_entries,
                        flags);

  equeue->edescs = mem;
  equeue->mask_num_entries = num_entries - 1;
  equeue->log2_num_entries = __builtin_ctz(num_entries);
  equeue->inst_map =
    (uint_reg_t)equeue->context->instance << EDESC_W1_INST_OFFSET;
  equeue->ering = ering;
  equeue->channel = channel;
  equeue->rhwb_complete_count = 0;
  for (i = 0; i < 8; i++)
    equeue->rhwb_complete_bin[i] = 0;
  return 0;
}

/* NOTE: It is a copy from tmc lib */
static uint64_t
gxio_mpipe_get_cpu_speed(void)
{
  static uint64_t hz;
  static bool done;

  if (!done)
  {
    /* Acquire (part of) "/proc/cpuinfo". */
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd == -1)
      return 0;
    char buffer[1024];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n < 0)
      return 0;
    buffer[n] = '\0';

    /* Find the first "cpu MHz" line. */
    char* p = strstr(buffer, "cpu MHz");
    if (p == NULL)
      return 0;

    /* Clip at the end of line. */
    char* e = strchr(p, '\n');
    if (e == NULL)
      return 0;
    *e = '\0';

    /* Find the colon. */
    char* c = strchr(p, ':');
    if (c == NULL)
      return 0;

    /* Extract the actual "MHz". */
    double mhz = strtod(c + 1, NULL);

    /*
     * Store "Hz" in static variable, fence so that we're
     * guaranteed it's visible, then set "done".
     */
    hz = (uint64_t)(mhz * 1000000 + 0.5);
    __sync_synchronize();
    done = true;
  }

  return hz;
}

int
gxio_mpipe_set_timestamp(gxio_mpipe_context_t* context,
                         const struct timespec *ts)
{
  uint64_t cycles = get_cycle_count();

  return gxio_mpipe_set_timestamp_aux(context, (uint64_t)ts->tv_sec,
                                      (uint64_t)ts->tv_nsec,
                                      (uint64_t)cycles);
}

int
gxio_mpipe_get_timestamp(gxio_mpipe_context_t* context,
                         struct timespec *ts)
{
  int ret;

  uint64_t cycles_prev, cycles_now, clock_rate;
  cycles_prev = get_cycle_count();

  ret = gxio_mpipe_get_timestamp_aux(context, (uint64_t *)&ts->tv_sec,
                                     (uint64_t *)&ts->tv_nsec,
                                     (uint64_t *)&cycles_now);
  if (ret < 0)
  {
    return ret;
  }

  clock_rate = gxio_mpipe_get_cpu_speed();

  ts->tv_nsec -= (cycles_now - cycles_prev) * 1000000000LL / clock_rate;
  if (ts->tv_nsec < 0) {
    ts->tv_nsec += 1000000000LL;
    ts->tv_sec -= 1;
  }
  return ret;
}

int
gxio_mpipe_adjust_timestamp(gxio_mpipe_context_t* context,
                            int64_t delta)
{
  return gxio_mpipe_adjust_timestamp_aux(context, delta);
}


/** Get our internal context used for link name access.  This context is
 *  special in that it is not associated with an mPIPE service domain.
 */
static gxio_mpipe_context_t*
_gxio_get_link_context(void)
{
  static gxio_mpipe_context_t context;
  static gxio_mpipe_context_t* contextp;
  static int tried_open = 0;

  MUTEX_LOCK(&mutex);

  if (!tried_open)
  {
    int i = 0;
    tried_open = 1;

    /*
     * "4" here is the maximum possible number of mPIPE shims; it's
     * an exaggeration but we shouldn't ever go beyond 2 anyway.
     */
    for (i = 0; i < 4; i++)
    {
      char file[80];

      snprintf(file, sizeof(file), "/dev/iorpc/mpipe_info%d", i);
      context.fd = open(file, O_RDWR);
      if (context.fd < 0)
        continue;

      contextp = &context;
      break;
    }
  }

  MUTEX_UNLOCK(&mutex);

  return contextp;
}

int
gxio_mpipe_link_instance(const char* link_name)
{
  _gxio_mpipe_link_name_t name;
  gxio_mpipe_context_t* context = _gxio_get_link_context();

  if (!context)
    return GXIO_ERR_NO_DEVICE;

  strncpy(name.name, link_name, sizeof(name.name));
  name.name[GXIO_MPIPE_LINK_NAME_LEN - 1] = '\0';

  return gxio_mpipe_info_instance_aux(context, name);
}


int
gxio_mpipe_link_enumerate(int idx, char* link_name)
{
  int rv;
  _gxio_mpipe_link_name_t name;
  _gxio_mpipe_link_mac_t mac;

  gxio_mpipe_context_t* context = _gxio_get_link_context();
  if (!context)
    return GXIO_ERR_NO_DEVICE;

  rv = gxio_mpipe_info_enumerate_aux(context, idx, &name, &mac);
  if (rv >= 0)
    strncpy(link_name, name.name, sizeof (name.name));

  return rv;
}


int
gxio_mpipe_link_enumerate_mac(int idx, char* link_name, uint8_t* link_mac)
{
  int rv;
  _gxio_mpipe_link_name_t name;
  _gxio_mpipe_link_mac_t mac;

  gxio_mpipe_context_t* context = _gxio_get_link_context();
  if (!context)
    return GXIO_ERR_NO_DEVICE;

  rv = gxio_mpipe_info_enumerate_aux(context, idx, &name, &mac);
  if (rv >= 0)
  {
    strncpy(link_name, name.name, sizeof (name.name));
    memcpy(link_mac, mac.mac, sizeof (mac.mac));
  }

  return rv;
}


int
gxio_mpipe_link_open(gxio_mpipe_link_t* link, gxio_mpipe_context_t* context,
                     const char* link_name, unsigned int flags)
{
  _gxio_mpipe_link_name_t name;
  int rv;

  strncpy(name.name, link_name, sizeof(name.name));
  name.name[GXIO_MPIPE_LINK_NAME_LEN - 1] = '\0';

  rv = gxio_mpipe_link_open_aux(context, name, flags);
  if (rv < 0)
    return rv;

  link->context = context;
  link->channel = rv >> 8;
  link->mac = rv & 0xFF;

  if (flags & GXIO_MPIPE_LINK_WAIT)
    return gxio_mpipe_link_wait(link, -1);

  return 0;
}

int
gxio_mpipe_link_close(gxio_mpipe_link_t* link)
{
  return gxio_mpipe_link_close_aux(link->context, link->mac);
}


int
gxio_mpipe_link_set_attr(gxio_mpipe_link_t* link, uint32_t attr, int64_t val)
{
  return gxio_mpipe_link_set_attr_aux(link->context, link->mac, attr, val);
}


int
gxio_mpipe_link_mdio_rd_ex(gxio_mpipe_link_t* link, int phy, int dev, int addr)
{
  return gxio_mpipe_link_mdio_rd_aux(link->context, link->mac, phy, dev, addr);
}


int
gxio_mpipe_link_mdio_wr_ex(gxio_mpipe_link_t* link, int phy, int dev, int addr,
			   uint16_t val)
{
  return gxio_mpipe_link_mdio_wr_aux(link->context, link->mac, phy, dev,
                                     addr, val);
}


int
gxio_mpipe_link_mdio_rd(gxio_mpipe_link_t* link, int dev, int addr)
{
  return gxio_mpipe_link_mdio_rd_aux(link->context, link->mac, -1, dev, addr);
}


int
gxio_mpipe_link_mdio_wr(gxio_mpipe_link_t* link, int dev, int addr,
                        uint16_t val)
{
  return gxio_mpipe_link_mdio_wr_aux(link->context, link->mac, -1, dev,
                                     addr, val);
}


int64_t
gxio_mpipe_link_get_attr(gxio_mpipe_link_t* link, uint32_t attr)
{
  int64_t data;
  int rv;

  rv = gxio_mpipe_link_get_attr_aux(link->context,
                                    (link->mac << 24) | (attr & 0xFFFFFF),
                                    &data);
  return (rv < 0) ? rv : data;
}


int64_t
gxio_mpipe_link_get_lattr(gxio_mpipe_link_t* link, uint32_t attr,
                          size_t offset, void* buf, size_t length)
{
  if ((offset >> 16) != 0)
    return GXIO_ERR_INVAL;

  return gxio_mpipe_link_get_lattr_aux(link->context,
                                       (link->mac << 24) | (offset << 8) |
                                       (attr & 0xFF), buf, length);
}


int64_t
gxio_mpipe_link_mac_rd(gxio_mpipe_link_t* link, int addr)
{
  int64_t data;
  int rv;

  rv = gxio_mpipe_link_mac_rd_aux(link->context,
                                  (link->mac << 24) | (addr & 0xFFFFFF),
                                  &data);
  return (rv < 0) ? rv : data;
}


int
gxio_mpipe_link_mac_wr(gxio_mpipe_link_t* link, int addr, uint32_t val)
{
  return gxio_mpipe_link_mac_wr_aux(link->context, link->mac, addr, val);
}


int
gxio_mpipe_link_wait(gxio_mpipe_link_t* link, int timeout)
{
  /*
   * FIXME: eventually we may want to just use gxio_mpipe_link_get_pollfd()
   * and then poll() on that fd, but for now we just get status and sleep,
   * looping until the link is up or we time out.
   */
  const int64_t ms_per_s = 1000;
  const int64_t ns_per_s = 1000 * 1000 * 1000;

  const int64_t timeout_ns = timeout * (ns_per_s / ms_per_s);

  int64_t thisdelay_ns = 250 * (ns_per_s / ms_per_s);
  int64_t totdelay_ns = 0;

  int64_t state = gxio_mpipe_link_get_attr(link,
                                           GXIO_MPIPE_LINK_CURRENT_STATE);

  while (!(state & GXIO_MPIPE_LINK_SPEED_MASK))
  {
    if (timeout_ns >= 0 && totdelay_ns >= timeout_ns)
      return GXIO_ERR_TIMEOUT;

    if (timeout_ns >= 0 && timeout_ns < totdelay_ns + thisdelay_ns)
      thisdelay_ns = timeout_ns - totdelay_ns;

#ifdef __NEWLIB__
    /* For newlib we sleep too much.  This is temporary, so we don't care. */
    sleep((thisdelay_ns + ns_per_s) / ns_per_s);
#else
    struct timespec ts;
    ts.tv_sec = thisdelay_ns / ns_per_s;
    ts.tv_nsec = thisdelay_ns % ns_per_s;

    nanosleep(&ts, NULL);
#endif

    totdelay_ns += thisdelay_ns;
    thisdelay_ns *= 2;
    if (thisdelay_ns >= 5 * ns_per_s)
      thisdelay_ns = 5 * ns_per_s;

    state = gxio_mpipe_link_get_attr(link, GXIO_MPIPE_LINK_CURRENT_STATE);
  }

  return 0;
}


int
gxio_mpipe_link_get_pollfd(gxio_mpipe_context_t* context)
{
  int fd = open("/dev/iorpc/pollfd", O_RDWR);

  if (fd < 0)
    return -errno;

  int err = gxio_mpipe_link_cfg_pollfd(context, fd, -1);

  if (err < 0)
  {
    close(fd);
    return err;
  }

  return fd;
}


int
gxio_mpipe_link_arm_pollfd(gxio_mpipe_context_t* context, int fd)
{
  return gxio_mpipe_arm_pollfd(context, fd);
}

int
gxio_mpipe_link_close_pollfd(gxio_mpipe_context_t* context, int fd)
{
  return gxio_mpipe_close_pollfd(context, fd);
}

/*
 * This function perfrom remote buffer retrun starting from given
 * slot for RHWB_BATCH descriptors.
 * @param equeue  An egress queue.
 * @param slot start position in the equeue for buffer return.
 */
static void
__gxio_mpipe_remote_buffer_return_batch(gxio_mpipe_equeue_t* equeue,
                                        unsigned long slot)
{
  int i;
  unsigned long rhwb_slot = slot & equeue->mask_num_entries;

  MPIPE_BSM_REGION_ADDR_t offset;
  MPIPE_BSM_REGION_VAL_t val;
  gxio_mpipe_edesc_t* edesc_prefetch;
  char *mmio_fast_base = equeue->other_context->mmio_fast_base;

  edesc_prefetch = &equeue->edescs[rhwb_slot];

  /*
   * Prefetch 2 cachelines.
   * Caller guaranteed that the 1st slot is cacheline aligned.
   */

  __insn_prefetch(edesc_prefetch += 4);
  __insn_prefetch(edesc_prefetch += 4);

  offset.word = 0;
  offset.region =
    MPIPE_MMIO_ADDR__REGION_VAL_BSM - MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
  /*
   * Now perform remote buffer return for RHWB_BATCH slots.
   * For better performance, use SW prefetch in the loop.
   */
  for (i = 0; i < RHWB_BATCH; i += 4)
  {
    int k;
    __insn_prefetch(edesc_prefetch += 4);
    for (k = 0; k < 4; k++)
    {
      uint_reg_t rhwb_w1 = equeue->edescs[rhwb_slot + i + k].words[1];

      if (rhwb_w1 & EDESC_W1_RHWB_MASK)
      {
        /*
         * If the RHWB bit is set, SW need do remote buffer return.
         */
        offset.stack = (rhwb_w1 >> EDESC_W1_STACK_IDX_OFFSET) & 0x1F;
        val.word = rhwb_w1;
        /*
         * Do mmio write to return a buffer to mPIPE context:
         * equeue->other_context.
         */
        __gxio_mmio_write(mmio_fast_base + offset.word, val.word);
      }
    }
  }
}

/*
 * This function will be called within __gxio_dma_queue_update_credits()
 * in dma_queue.c. For given completion range - from orig_hw_complete_count
 * to new_hw_complete_count, do SW buffer return, update equeue's
 * rhwb_complete_count field and return new credits, which will be added
 * back to dma_queue's credits_and_next_index field.
 */

uint64_t
__gxio_mpipe_remote_buffer_return(__gxio_dma_queue_t *dma_queue,
                                  uint64_t orig_hw_complete_count,
                                  uint64_t new_hw_complete_count)
{
#define container_of(_ptr, _type, _member)              \
  (_type *)((char *)(_ptr) - offsetof(_type, _member))

  gxio_mpipe_equeue_t *equeue = container_of(dma_queue,
                                             gxio_mpipe_equeue_t,
                                             dma_queue);

  uint64_t  start = orig_hw_complete_count & ~(RHWB_BATCH - 1);
  uint64_t  rhwb_bin_max = 1ULL << (equeue->log2_num_entries - 3);
  uint64_t  rhwb_bin_index;
  uint32_t  rhwb_bin_count;
  uint64_t  delta = 0;

  /*
   * This while loop is very tricky. Basically it does 2 things.
   *
   * (1) Based on the hw_complete_count, performed the remote buffer
   *     return for slots at boundary of modulo RHWB_BATCH.
   * (2) Update the rhwb_complete_count. This count will be updated
   *     as a slow fashion, and always points to a 1/8 segment of whole
   *     queue. The slots between the rhwb_complete_count and
   *     hw_complete_count are under process for buffer return.
   *     In order to provide non-blocking operation for each caller,
   *     we use 8 32-bit counters - rhwb_complete_bin[8] in the
   *     gxio_mpipe_equeue_t to record the buffer return info. Each is
   *     for 1/8 segment of the queue. rhwb_complete_count points to
   *     the current rhwb_complete_bin counter, once the counter
   *     reaches its max (1/8 of the equeue), rhwb_complete_count is
   *     updated to point to next 1/8 segment. Also, the credits is
   *     granted based on the new rhwb_complete_count as granularity
   *     of 1/8 of the queue size so the head of new descriptor will
   *     never overwrite the descriptors whose buffer is not returned
   *     yet.
   */

  while ((start + RHWB_BATCH) <= new_hw_complete_count)
  {
    /* Do buffer return for RHWB_BATCH slots. */
    __gxio_mpipe_remote_buffer_return_batch(equeue, start);

    /*
     * Calculate the corresponding index to array
     * equeue->rhwb_complete_bin[8] based on the current
     * buffer return batch starting position (start).
     */
    rhwb_bin_index = (start >> (equeue->log2_num_entries - 3)) & 7;

    /*
     * Atomic add RHWB_BATCH to counter:
     *     equeue->rhwb_complete_bin[rhwb_bin_index]
     */
    rhwb_bin_count = __insn_fetchadd4(
      &equeue->rhwb_complete_bin[rhwb_bin_index],
      RHWB_BATCH);

    if ((rhwb_bin_count + RHWB_BATCH) == rhwb_bin_max)
    {
      /*
       * The last RHWB_BATCH batch for this 1/8 segment.
       */
      uint64_t rhwb_start = start & ~(rhwb_bin_max - 1);
      uint64_t rhwb_complete_count;
      int i, backoff = 1;

      /* Reset the bin counter. */
      equeue->rhwb_complete_bin[rhwb_bin_index] = 0;

      /*
       * Try to increase the rhwb_complete_count if it points to
       * current 1/8 segment of the queue.
       */
      rhwb_complete_count = arch_atomic_val_compare_and_exchange(
        &equeue->rhwb_complete_count, rhwb_start,
        rhwb_start + rhwb_bin_max);

      /*
       * If the above compare_and_exchange failed, that means
       * equeue->rhwb_complete_count is not pointing to rhwb_start
       * yet. Retry the compare swap operation in the following
       * loop until success. Note: The chance to enter this loop
       * is very small.
       */

      while (__unlikely(rhwb_complete_count != rhwb_start))
      {
        /* Backoff in a loop. */
        for (i = backoff; i > 0; i--)
          __insn_mfspr(SPR_PASS);

        /* Adjust the backoff loop count. */
        if (backoff < 256)
          backoff *= 2;

        /* Try again to bump the rhwb_complete_count. */
        rhwb_complete_count = arch_atomic_val_compare_and_exchange(
          &equeue->rhwb_complete_count, rhwb_start,
          rhwb_start + rhwb_bin_max);
      }

      /*
       * Grant more credits i.e. 1/8 segment of the queue.
       */
      delta += rhwb_bin_max;
    }
    /* Remote buffer return for next BATCH. */
    start += RHWB_BATCH;
  }
  /* Return new credit count. */
  return delta;
}

/** Set the priority level to an eMDA ring.
 * @param equeue An egress queue to set priority level.
 * @param priority A priority level.
 * @return 0 on success, negative on failure.
 */
int
gxio_mpipe_equeue_set_priority(
  gxio_mpipe_equeue_t *equeue,
  gxio_mpipe_equeue_priority_enum_t priority)
{
  int retval = gxio_mpipe_edma_ring_set_priority(
    equeue->context, equeue->ering, priority);

  return retval;
}

/** Retrieve the priority level of an eDMA ring.
 * @param equeue An egress queue to get priority level.
 * @return priority level, negative on failure.
 */
int
gxio_mpipe_equeue_get_priority(gxio_mpipe_equeue_t *equeue)
{
  int retval = gxio_mpipe_edma_ring_get_priority(
    equeue->context, equeue->ering);

  return retval;
}

