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
 * mPIPE driver.
 */

#include <arch/sim.h>

#include "rules.h"
#include "mpipe.h"

#include "hv.h"
#include "cfg.h"
#include "debug.h"
#include "filesys.h"

#include "mpipe_rpc_dispatch.h"



#if 0

#include <stdio.h>

static void
note(const char* format, ...)
{
  char buf[1024];

  va_list args;
  va_start(args, format);
  (void)vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (sim_is_simulator())
  {
    // Like "sim_print()", but with newline.
    for (int i = 0; buf[i] != 0; i++)
    {
      __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                   (buf[i] << _SIM_CONTROL_OPERATOR_BITS));
    }
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 ('\n' << _SIM_CONTROL_OPERATOR_BITS));
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 (SIM_PUTC_FLUSH_BINARY << _SIM_CONTROL_OPERATOR_BITS));
  }
  else
  {
    printf("%s\n", buf);
  }
}

#endif



// Note that text uses a count of uint32's, while data and regs use a
// count of uint16's.  For data, a count of "2048" is represented as "0".
// We always blast pairs of uint16's, except for the "exception" register.
//
static int
blast_classifier(mpipe_state_t* ms)
{
  classifier_info_t* info = &ms->classifier_info;
  classifier_blast_t* blast = &ms->classifier_blast;

  bool force = !blast->blasted;

  uint32_t ammo[2064];
  uint round = 0;
  int first;

  MPIPE_CLS_INIT_WDAT_BLAST_RECORD_FORMAT_t header = { .word = 0 };

  // Prepare to blast the classifier.

  first = -1;
  header.sel = MPIPE_CLS_INIT_WDAT_BLAST_RECORD_FORMAT__SEL_VAL_INST;

  for (uint offset = 0; offset < sizeof(info->text); offset += 4)
  {
    uint val = classifier_read_uint(info->text + offset, 4);
    if (force || val != classifier_read_uint(blast->text + offset, 4))
    {
      if (first < 0)
      {
        header.start_index = offset / 4;
        first = round++;
      }

      header.data_count = (round - first);
      ammo[first] = header.word;
      ammo[round++] = val;
    }
    else
    {
      first = -1;
    }
  }

  first = -1;
  header.sel = MPIPE_CLS_INIT_WDAT_BLAST_RECORD_FORMAT__SEL_VAL_TABLE;

  for (uint offset = 0; offset < sizeof(info->data); offset += 4)
  {
    uint val = classifier_read_uint(info->data + offset, 4);
    if (force || val != classifier_read_uint(blast->data + offset, 4))
    {
      if (first < 0)
      {
        header.start_index = offset / 2;
        first = round++;
      }

      header.data_count = (round - first) * 2;
      ammo[first] = header.word;
      ammo[round++] = (val << 16) | (val >> 16);
    }
    else
    {
      first = -1;
    }
  }

  header.sel = MPIPE_CLS_INIT_WDAT_BLAST_RECORD_FORMAT__SEL_VAL_GPR;

  // We just blast all or none of the registers, for simplicity.
  if (force || memcmp(blast->regs, info->regs, sizeof(info->regs)) != 0)
  {
    first = round++;
    header.start_index = 0;

    // Handle the "normal" registers.
    for (uint offset = 0; offset < sizeof(info->regs) - 2; offset += 4)
    {
      uint val = classifier_read_uint(info->regs + offset, 4);

      ammo[round++] = (val << 16) | (val >> 16);
    }

    // Handle the "exception target pc" register.
    uint val = classifier_read_uint(info->regs + sizeof(info->regs) - 2, 2);

    header.data_count = (round - first) * 2 - 1;
    ammo[first] = header.word;

    // ISSUE: One of these is ignored.
    ammo[round++] = (val << 16) | val;
  }

  if (round < 2064)
  {
    header.data_count = 0;
    header.start_index = 0;
    header.sel = MPIPE_CLS_INIT_WDAT_BLAST_RECORD_FORMAT__SEL_VAL_EOR;
    ammo[round++] = header.word;
  }


  // Actually blast.

  MPIPE_CLS_INIT_CTL_t ctl = {{
      .cls_sel = (1 << 10) - 1,
      .idx = 0,
      .struct_sel = MPIPE_CLS_INIT_CTL__STRUCT_SEL_VAL_PGMR,
    }};
  cfg_wr(ms->shim_pos.word, 0, MPIPE_CLS_INIT_CTL, ctl.word);

  for (uint i = 0; i < round; i++)
    cfg_wr(ms->shim_pos.word, 0, MPIPE_CLS_INIT_WDAT, ammo[i]);

  if (sim_is_simulator())
  {
    uint which = 0;

    // Load the magic section into the simulator.

    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_CLEAR_MPIPE_MAGIC_BYTES |
                 (which << _SIM_CONTROL_OPERATOR_BITS));

    for (uint i = 0; i < info->magic_size; i++)
    {
      uint8_t byte = info->xtra[info->magic_ptr + i];
      __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_APPEND_MPIPE_MAGIC_BYTE |
                   ((which | (byte << 8)) << _SIM_CONTROL_OPERATOR_BITS));
    }
  }

  uint all = (1 << 10) - 1;

  MPIPE_CLS_ENABLE_t enable;
  enable.word = 0;
  enable.flash = all;
  enable.enable = all;
  cfg_wr(ms->shim_pos.word, 0, MPIPE_CLS_ENABLE, enable.word);
  __insn_mf();

  // Wait for blasting to complete.
  while (1)
  {
    enable.word = cfg_rd(ms->shim_pos.word, 0, MPIPE_CLS_ENABLE);
    if (!enable.pgm_pnd)
      break;
  }

  // Latch the blasted state.
  memcpy(blast->text, info->text, sizeof(info->text));
  memcpy(blast->data, info->data, sizeof(info->data));
  memcpy(blast->regs, info->regs, sizeof(info->regs));
  blast->blasted = 1;

  return 0;
}


static uint16_t
classifier_csum(uint16_t word1, uint16_t word2)
{
  uint32_t sum = word1 + word2;
  return (uint16_t)((sum & 0xffff) + (sum >> 16));
}


static uint16_t
gxio_mpipe_rules_dmac_hash(gxio_mpipe_rules_dmac_t dmac, uint seed)
{
  // Logic copied from "classify.c", and converted from big-endian
  // "classifier_crc32_16()" to byte-wise "__insn_crc32_8()".

  uint32_t hash = -1U;

  hash = __insn_crc32_8(hash, seed & 0xFF);
  hash = __insn_crc32_8(hash, seed >> 8);

  hash = __insn_crc32_8(hash, dmac.octets[1]);
  hash = __insn_crc32_8(hash, dmac.octets[0]);
  hash = __insn_crc32_8(hash, dmac.octets[3]);
  hash = __insn_crc32_8(hash, dmac.octets[2]);
  hash = __insn_crc32_8(hash, dmac.octets[5]);
  hash = __insn_crc32_8(hash, dmac.octets[4]);

  hash = classifier_csum((hash & 0xFFFF), (hash >> 16));

  return hash & 0xFFFF;
}


// There can be at most 31 rules, because we use 31 bits to encode
// which rules are allowed by each channel, vlan, and dmac.
#define MAX_RULES 31

// There are only 20 legal channels.
#define MAX_CHANNELS 20

// The "table_uses[]" arrays are weird.  Each entry is zero if unused,
// 0x4000 + index if used by a given index, or 0x8000 + 0x4000 + index
// if used by a given index and also desired by some other index.  And
// note that we mask the original hash to find the desired entry, but
// then, as we advance in the face of conflicts, we do NOT mask, so we
// don't wrap, and may thus use up to "table_size + array_size" entries.
#define ENTRY_USED 0x4000
#define ENTRY_CONFLICT 0x8000


// Determine the maximum number of "conflicts" that can be encountered
// when hashing a value into the given table.
//
static uint
max_conflicts(uint16_t table_uses[], uint table_size)
{
  uint conflicts = 0;

  for (uint j = 0; j < table_size; j++)
  {
    uint num = 0;
    while (table_uses[j] >= ENTRY_CONFLICT)
    {
      num++;
      j++;
    }
    if (conflicts < num)
      conflicts = num;
  }

  return conflicts;
}


// The default classifier supports 128 dmacs with 16 overflows, and
// 256 vlans with 32 overflows, but a custom classifier could change
// the sizes of "dmac_table" or "vlan_table", or even delete them.
// The absolute maximums are encoded below, for simplicity.

/** Absolute max number of dmacs (4096 / 10) **/
#define MAX_DMACS 409

/** Absolute max number of vlans (4096 / 6) **/
#define MAX_VLANS 682



/** Help verify a "rule". */
static int
verify_rule(mpipe_state_t* ms, int svc_dom,
            gxio_mpipe_rules_rule_t* rule)
{
  // ISSUE: Verify "bucket_mask" is "2^N - 1"?

  // Verify "bucket_mask" and "bucket_first".
  for (int i = 0; i <= rule->bucket_mask; i++)
  {
    int bucket = rule->bucket_first + i;
    if (!good_bucket(ms, svc_dom, bucket))
      return GXIO_MPIPE_ERR_RULES_INVALID;
  }

  // Verify "stacks".
  for (int i = 0; i < 8; i++)
  {
    int stack = rule->stacks.stacks[i];
    if (!good_stack(ms, svc_dom, stack))
      return GXIO_MPIPE_ERR_RULES_INVALID;
  }

  if (~ms->svc_dom_resources[svc_dom].channels & rule->channel_bits)
    return GXIO_MPIPE_ERR_RULES_INVALID;

  if (rule->channel_bits == 0)
    rule->channel_bits = ms->svc_dom_resources[svc_dom].channels;

  return 0;
}


int
update_classifier(mpipe_state_t* ms)
{
  classifier_info_t* info = &ms->classifier_info;

  gxio_mpipe_rules_rule_t* rules[MAX_RULES];
  int num_rules = 0;

  // Handle customization of "MAX_RULES".
  uint16_t max_rules =
    classifier_get_symbol_size(info, "rule_structs") / 16;
  if (max_rules > MAX_RULES)
    max_rules = MAX_RULES;

  // Verify the rules, and sort them by priority.
  for (int svc_dom = 0; svc_dom < MPIPE_MMIO_NUM_SVC_DOM; ++svc_dom)
  {
    // Paranoia.
    if ((ms->svc_dom_avail_mask & (1ull << svc_dom)) != 0)
      continue;

    gxio_mpipe_rules_list_t* list = &ms->rules_list[svc_dom];

    uint head = 0;
    while (head < list->tail)
    {
      gxio_mpipe_rules_rule_t* rule =
        (gxio_mpipe_rules_rule_t*)(list->rules + head);

      // Verify size, acknowledging possible padding.
      if (head + rule->size > list->tail)
        return GXIO_MPIPE_ERR_RULES_CORRUPT;
      uint expect = sizeof(*rule) + rule->num_vlans * 2 + rule->num_dmacs * 6;
      if (rule->size != expect && rule->size != expect + 2)
        return GXIO_MPIPE_ERR_RULES_CORRUPT;

      int err = verify_rule(ms, svc_dom, rule);
      if (err != 0)
        return err;

      if (num_rules >= max_rules)
        return GXIO_MPIPE_ERR_RULES_FULL;

      // Insert the rule.
      int i = num_rules++;
      while (i > 0 && rules[i - 1]->priority > rule->priority)
      {
        rules[i] = rules[i - 1];
        i--;
      }
      rules[i] = rule;

      head += rule->size;
    }
  }

  // For each rule, headroom, then tailroom, then capacity (uint16be),
  // then bucket mask (uint16be), then bucket first (uint16be), then,
  // for each of the 8 possible buffer sizes, a buffer stack index for
  // packets of the given size.  ISSUE: Make an actual "struct"?
  uint8_t rule_structs[max_rules][16];
  memset(rule_structs, 0, sizeof(rule_structs));

  // The rules for each channel.
  uint32_t channel_rules[MAX_CHANNELS];
  memset(channel_rules, 0, sizeof(channel_rules));

  // The default rules for the "dmac table".
  uint32_t dmac_table_rules = 0;

  // The default rules for the "vlan table".
  uint32_t vlan_table_rules = 0;

  // The distinct dmacs, and their rules.
  uint16_t dmac_array_size = 0;
  gxio_mpipe_rules_dmac_t dmac_array_keys[MAX_DMACS];
  uint32_t dmac_array_rules[MAX_DMACS];

  // The distinct vlans, and their rules.
  uint16_t vlan_array_size = 0;
  gxio_mpipe_rules_vlan_t vlan_array_keys[MAX_VLANS];
  uint32_t vlan_array_rules[MAX_VLANS];


  // Analyze the rules (first pass).
  for (int rule_id = 0; rule_id < num_rules; rule_id++)
  {
    gxio_mpipe_rules_rule_t* rule = rules[rule_id];

    uint32_t rule_bit = cpu_to_be32(1UL << rule_id);

    for (int channel = 0; channel < MAX_CHANNELS; channel++)
    {
      if (rule->channel_bits == 0 || (rule->channel_bits & (1U << channel)))
        channel_rules[channel] |= rule_bit;
    }

    if (rule->num_dmacs == 0)
    {
      dmac_table_rules |= rule_bit;
    }

    if (rule->num_vlans == 0)
    {
      vlan_table_rules |= rule_bit;
    }

    // ISSUE: What is the proper error?
    if (rule->headroom >= 128)
      return GXIO_MPIPE_ERR_RULES_CORRUPT;

    // Save the headroom/tailroom.
    rule_structs[rule_id][0] = rule->headroom;
    rule_structs[rule_id][1] = rule->tailroom;

    // Save the capacity.
    rule_structs[rule_id][2] = rule->capacity / 256;
    rule_structs[rule_id][3] = rule->capacity & 255;

    // Save the bucket info.
    rule_structs[rule_id][4] = rule->bucket_mask / 256;
    rule_structs[rule_id][5] = rule->bucket_mask & 255;
    rule_structs[rule_id][6] = rule->bucket_first / 256;
    rule_structs[rule_id][7] = rule->bucket_first & 255;

    // Save the buffer stacks.
    memcpy(&rule_structs[rule_id][8], &rule->stacks, 8);
  }

  // Analyze the rules (second pass).
  for (int rule_id = 0; rule_id < num_rules; rule_id++)
  {
    gxio_mpipe_rules_rule_t* rule = rules[rule_id];

    uint32_t rule_bit = cpu_to_be32(1UL << rule_id);

    uint8_t* data = rule->dmacs_and_vlans;

    for (int i = 0; i < rule->num_dmacs; i++)
    {
      gxio_mpipe_rules_dmac_t dmac = *(gxio_mpipe_rules_dmac_t*)data;

      // Find existing entry, or add new one, initializing the "rules"
      // to the "default" rules (the rules with no dmac restrictions).
      int k;
      for (k = 0; k < dmac_array_size; k++)
      {
        gxio_mpipe_rules_dmac_t old = dmac_array_keys[k];
        if (memcmp(&old, &dmac, sizeof(dmac)) == 0)
          break;
      }
      if (k == dmac_array_size)
      {
        if (dmac_array_size == MAX_DMACS)
          return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;
        dmac_array_size++;
        dmac_array_keys[k] = dmac;
        dmac_array_rules[k] = dmac_table_rules;
      }

      // Add in the current rule.
      dmac_array_rules[k] |= rule_bit;

      data += sizeof(dmac);
    }

    for (int i = 0; i < rule->num_vlans; i++)
    {
      gxio_mpipe_rules_vlan_t vlan = *(gxio_mpipe_rules_vlan_t*)data;

      // Find existing entry, or add new one, initializing the "rules"
      // to the "default" rules (the rules with no vlan restrictions).
      int k;
      for (k = 0; k < vlan_array_size; k++)
      {
        if (vlan_array_keys[k] == vlan)
          break;
      }
      if (k == vlan_array_size)
      {
        if (vlan_array_size == MAX_VLANS)
          return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;
        vlan_array_size++;
        vlan_array_keys[k] = vlan;
        vlan_array_rules[k] = vlan_table_rules;
      }

      // Add in the current rule.
      vlan_array_rules[k] |= rule_bit;

      data += sizeof(vlan);
    }
  }


  // This may be customized to arbitrary size, or even deleted entirely.
  uint16_t dmac_table_bytes = classifier_get_symbol_size(info, "dmac_table");

  // Allow empty table only if no entries needed.
  // See also the "while (dmac_table_bytes != 0)" below.
  if (dmac_table_bytes == 0 && dmac_array_size != 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

  // Determine minimum table size (assume at least one "empty" entry).
  // ISSUE: Without dynamic arrays, round up to "table_bytes / (10 * 2)"?
  uint16_t dmac_table_size = 1;
  while (dmac_table_size < dmac_array_size + 1)
    dmac_table_size *= 2;

  // See docs for "ENTRY_USED" above.
  uint16_t dmac_table_uses[MAX_DMACS * 2];

  uint16_t dmac_table_seed = 0;

  uint dmac_table_limit = 0;

  // ISSUE: Scanning all 65536 seeds is NOT cheap!  Maybe for now we
  // should start the table_size values at half of the table_bytes
  // maximum table sizes, since the memory is reserved anyway, to
  // increase the odds that the first seed will prevent conflicts?

  // ISSUE: Maybe we should put a hard limit on the total number of
  // allowed conflicts (we have a "soft" limit based on the actual
  // maximal sizes of the relevent hash tables).

  // HACK: Shorthand for "if (...) while (true)".
  while (dmac_table_bytes != 0)
  {
    // Verify minimal sizes.
    // The "actual" sizes will be verified below.
    if (dmac_table_size > MAX_DMACS)
      return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

    //--note("Trying %d entries for %d dmacs.",
    //--     dmac_table_size, dmac_array_size);

    dmac_table_seed = 0;
    uint dmac_pain = -1U;
    for (uint seed = 0; true; seed++)
    {
      // The final round repeats the work done by the best round.
      bool done = (seed == 65536);
      if (done)
        seed = dmac_table_seed;

      memset(dmac_table_uses, 0, sizeof(dmac_table_uses));

      for (int i = 0; i < dmac_array_size; i++)
      {
        gxio_mpipe_rules_dmac_t dmac = dmac_array_keys[i];

        uint16_t hash = gxio_mpipe_rules_dmac_hash(dmac, seed);

        uint16_t j = hash & (dmac_table_size - 1);

        while (dmac_table_uses[j] != 0)
        {
          // Mark as a conflict and advance.
          dmac_table_uses[j++] |= ENTRY_CONFLICT;
        }

        dmac_table_uses[j] = i | ENTRY_USED;
      }

      // Each dmac conflict costs about 12 cycles.
      uint pain = max_conflicts(dmac_table_uses, dmac_table_size) * 12;

      if (dmac_pain > pain)
      {
        //--note("Seed %d yielded pain %d.", seed, pain);

        dmac_table_seed = seed;
        dmac_pain = pain;

        if (pain == 0)
          break;
      }

      if (done)
        break;
    }

    // Drop unused overflow entries.
    dmac_table_limit = dmac_table_size + dmac_array_size;
    while (dmac_table_limit > dmac_table_size &&
           dmac_table_uses[dmac_table_limit - 1] == 0)
      dmac_table_limit--;

    // Avoid overflowing the actual classifier array.
    if (dmac_table_limit * 10 > dmac_table_bytes)
      return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

    // Try to avoid overflowing either array, because that would
    // then cause the checks above to FAIL, for no good reason.
    // Note that if we double the table size, and ASSUME that the
    // overflow cannot grow, then we might use as many entries as
    // "size * 2 + over = (size * 2) + (limit - size) = size + limit".
    if ((dmac_table_size + dmac_table_limit) * 10 > dmac_table_bytes)
      break;

    if (dmac_pain == 0)
      break;

    dmac_table_size *= 2;
  }


  // This may be customized to arbitrary size, or even deleted entirely.
  uint16_t vlan_table_bytes = classifier_get_symbol_size(info, "vlan_table");

  // Allow empty table only if no entries needed.
  // See also the "while (vlan_table_bytes != 0)" below.
  if (vlan_table_bytes == 0 && vlan_array_size != 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

  // Determine minimum table size (assume at least one "empty" entry).
  // ISSUE: Without dynamic arrays, round up to "table_bytes / (6 * 2)"?
  uint16_t vlan_table_size = 1;
  while (vlan_table_size < vlan_array_size + 1)
    vlan_table_size *= 2;

  uint16_t vlan_table_uses[MAX_VLANS * 2];

  uint16_t vlan_table_seed = 0;

  uint vlan_table_limit = 0;

  // HACK: Shorthand for "if (...) while (true)".
  while (vlan_table_bytes != 0)
  {
    // Verify minimal sizes.
    // The "actual" sizes will be verified below.
    if (vlan_table_size > MAX_VLANS)
      return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

    //--note("Trying %d entries for %d vlans.",
    //--     vlan_table_size, vlan_array_size);

    vlan_table_seed = 0;
    uint vlan_pain = -1U;
    for (uint seed = 0; true; seed++)
    {
      bool done = (seed == 65536);
      if (done)
        seed = vlan_table_seed;

      memset(vlan_table_uses, 0, sizeof(vlan_table_uses));

      for (int i = 0; i < vlan_array_size; i++)
      {
        gxio_mpipe_rules_vlan_t vlan = vlan_array_keys[i];

        uint32_t hash = -1U;

        // FIXME: Verify this.
        hash = __insn_crc32_8(hash, seed & 0xFF);
        hash = __insn_crc32_8(hash, seed >> 8);

        // FIXME: Verify this.
        hash = __insn_crc32_8(hash, vlan & 0xFF);
        hash = __insn_crc32_8(hash, vlan >> 8);

        hash = classifier_csum((hash & 0xFFFF), (hash >> 16));

        uint16_t j = hash & (vlan_table_size - 1);

        while (vlan_table_uses[j] != 0)
        {
          // Mark as a conflict and advance.
          vlan_table_uses[j++] |= ENTRY_CONFLICT;
        }

        vlan_table_uses[j] = i | ENTRY_USED;
      }

      // Each vlan conflict costs about 8 cycles.
      uint pain = max_conflicts(vlan_table_uses, vlan_table_size) * 8;

      if (vlan_pain > pain)
      {
        //--note("Seed %d yielded pain %d.", seed, pain);

        vlan_table_seed = seed;
        vlan_pain = pain;

        if (pain == 0)
          break;
      }

      if (done)
        break;
    }

    //--note("Current dmac pain %d, vlan pain %d.", dmac_pain, vlan_pain);

    // Drop unused overflow entries.
    vlan_table_limit = vlan_table_size + vlan_array_size;
    while (vlan_table_limit > vlan_table_size &&
           vlan_table_uses[vlan_table_limit - 1] == 0)
      vlan_table_limit--;

    // Avoid overflowing the actual classifier array.
    if (vlan_table_limit * 6 > vlan_table_bytes)
      return GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX;

    // Try to avoid overflowing either array, because that would
    // then cause the checks above to FAIL, for no good reason.
    // Note that if we double the table size, and ASSUME that the
    // overflow cannot grow, then we might use as many entries as
    // "size * 2 + over = (size * 2) + (limit - size) = size + limit".
    if ((vlan_table_size + vlan_table_limit) * 6 > vlan_table_bytes)
      vlan_pain = 0;

    if (vlan_pain == 0)
      break;

    vlan_table_size *= 2;
  }


  // The "rule bit" used for marking conflicts.
  uint32_t conflict_bit = cpu_to_be32(1UL << MAX_RULES);


  // Initialize the dmac table.

  // Prepare "empty" entry.
  // NOTE: This can be any value.
  gxio_mpipe_rules_dmac_t dmac_empty_key =
    {{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }};
  uint32_t dmac_empty_rules = dmac_table_rules;
  if (dmac_array_size > 0)
  {
    dmac_empty_key = dmac_array_keys[0];
    dmac_empty_rules = dmac_array_rules[0];
  }

  // Array of (dmac (6), rules (4)).
  uint8_t dmac_table[MAX_DMACS * 2][10];
  memset(dmac_table, 0, sizeof(dmac_table));

  for (uint j = 0; j < dmac_table_limit; j++)
  {
    uint i = dmac_table_uses[j];

    *(gxio_mpipe_rules_dmac_t*)(&dmac_table[j][0]) =
      (i != 0) ? dmac_array_keys[i & 0x3FFF] : dmac_empty_key;

    uint32_t rule_ids =
      (((i != 0) ? dmac_array_rules[i & 0x3FFF] : dmac_empty_rules) |
       ((i >= ENTRY_CONFLICT) ? conflict_bit : 0));
    // NOTE: This may yield a triple bswap.
    classifier_write_uint(&dmac_table[j][6], 4, cpu_to_be32(rule_ids));
  }


  // Initialize the vlan table.

  // Prepare "empty" entry.
  // NOTE: This can be any value.
  gxio_mpipe_rules_vlan_t vlan_empty_key = 0xFFFF;

  // Array of (vlan (2), rules (4)).
  uint8_t vlan_table[MAX_VLANS * 2][6];
  memset(vlan_table, 0, sizeof(vlan_table));

  for (uint j = 0; j < vlan_table_limit; j++)
  {
    uint i = vlan_table_uses[j];

    gxio_mpipe_rules_vlan_t vlan =
      (i != 0) ? vlan_array_keys[i & 0x3FFF] : vlan_empty_key;
    classifier_write_uint(&vlan_table[j][0], 2, vlan);

    uint32_t rule_ids =
      (((i != 0) ? vlan_array_rules[i & 0x3FFF] : vlan_table_rules) |
       ((i >= ENTRY_CONFLICT) ? conflict_bit : 0));
    // NOTE: This may yield a triple bswap.
    classifier_write_uint(&vlan_table[j][2], 4, cpu_to_be32(rule_ids));
  }

  //--note("Using %d dmac entries and %d vlan entries.",
  //--     dmac_table_limit, vlan_table_limit);

  uint16_t instance = 0;
  
  // The instance bit is defined as the upper bit (7) of CTR0 byte in the
  // packet descriptor.
  if (ms->instance)
    instance = cpu_to_be16(0x0080);
  
  // Modify the classifier.

  int err = 0;

  err = err ?:
    classifier_set_memory(info, "instance",
                          (void*)&instance,
                          sizeof(instance));

  err = err ?:
    classifier_set_memory(info, "rule_structs",
                          (void*)rule_structs,
                          sizeof(rule_structs));

  err = err ?:
    classifier_set_memory(info, "channel_rules",
                          (void*)channel_rules,
                          sizeof(channel_rules));

  if (dmac_table_bytes != 0)
  {
    err = err ?:
      classifier_set_memory(info, "dmac_table",
                            (void*)dmac_table,
                            dmac_table_bytes);

    uint16_t dmac_table_mask = cpu_to_be16(dmac_table_size - 1);
    err = err ?:
      classifier_set_memory(info, "dmac_table_mask",
                            (void*)&dmac_table_mask,
                            sizeof(dmac_table_mask));

    dmac_table_seed = cpu_to_be16(dmac_table_seed);
    err = err ?:
      classifier_set_memory(info, "dmac_table_seed",
                            (void*)&dmac_table_seed,
                            sizeof(dmac_table_seed));

    err = err ?:
      classifier_set_memory(info, "dmac_table_rules",
                            (void*)&dmac_table_rules,
                            sizeof(dmac_table_rules));
  }

  if (vlan_table_bytes != 0)
  {
    err = err ?:
      classifier_set_memory(info, "vlan_table",
                            (void*)vlan_table,
                            vlan_table_bytes);

    uint16_t vlan_table_mask = cpu_to_be16(vlan_table_size - 1);
    err = err ?:
      classifier_set_memory(info, "vlan_table_mask",
                            (void*)&vlan_table_mask,
                            sizeof(vlan_table_mask));

    vlan_table_seed = cpu_to_be16(vlan_table_seed);
    err = err ?:
      classifier_set_memory(info, "vlan_table_seed",
                            (void*)&vlan_table_seed,
                            sizeof(vlan_table_seed));

    err = err ?:
      classifier_set_memory(info, "vlan_table_rules",
                            (void*)&vlan_table_rules,
                            sizeof(vlan_table_rules));
  }

  if (err != 0)
    return err;

  err = classifier_apply_relocs(info);
  if (err != 0)
    return err;

  return blast_classifier(ms);
}



int
handle_gxio_mpipe_commit_rules(mpipe_state_t* ms, int svc_dom,
                               void* blob, size_t blob_size)
{
  gxio_mpipe_rules_list_t* list = &ms->rules_list[svc_dom];

  if (blob_size > sizeof(*list))
    return GXIO_MPIPE_ERR_RULES_FULL;

  // NOTE: The blob must be at least two bytes long for "tail" to be valid.
  if (blob_size < 4 || blob_size != 4 + ((gxio_mpipe_rules_list_t*)blob)->tail)
    return GXIO_MPIPE_ERR_RULES_CORRUPT;

  gxio_mpipe_rules_list_t old = *list;

  // Commit the rules.
  memset(list, 0, sizeof(*list));
  memcpy(list, blob, blob_size);

  // Update the classifier.
  int err = update_classifier(ms);

  // Uncommit the rules on error.
  if (err != 0)
    *list = old;

  return err;
}


int
handle_gxio_mpipe_classifier_load_from_bytes(mpipe_state_t* ms, int svc_dom,
                                             void* blob, size_t blob_size)
{
  classifier_info_t* info = &ms->classifier_info;

  int err = classifier_parse(info, blob, blob_size);
  if (err != 0)
    return err;

  return update_classifier(ms);
}


int
handle_gxio_mpipe_classifier_set_memory(mpipe_state_t* ms, int svc_dom,
                                        _gxio_mpipe_symbol_name_t name,
                                        void* blob, size_t blob_size)
{
  classifier_info_t* info = &ms->classifier_info;

  int err = classifier_set_memory(info, name.name, blob, blob_size);
  if (err != 0)
    return err;

  return update_classifier(ms);
}


int
mpipe_open_aux(mpipe_state_t* ms, int svc_dom)
{
  int result;

  // Only load once.
  if (ms->classifier_blast.blasted)
    return 0;

  // Look for a "classifier" in the HVFS.
  int fd = fs_findfile("classifier");
  if (fd < 0)
    return 0;

  // Acquire and verify the size.
  int len;
  unsigned int flags;
  fs_stat(fd, &len, &flags);
  if (len > sizeof(ms->rpc_buf))
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;

  // HACK: Use "rpc_buf" as scratch space.
  char* buf = ms->rpc_buf;

  // Read the classifier.
  result = fs_pread(fd, buf, len, 0);
  if (result != len)
    panic("Failed to read 'classifier' from HVFS");

  classifier_info_t* info = &ms->classifier_info;

  int err = classifier_parse(info, (uint8_t*)buf, len);
  if (err != 0)
    return err;

  return update_classifier(ms);
}


int
mpipe_close_aux(mpipe_state_t* ms, int svc_dom)
{
  // Forget any "rules".
  memset(&ms->rules_list[svc_dom], 0, sizeof(ms->rules_list[svc_dom]));

  return update_classifier(ms);
}

