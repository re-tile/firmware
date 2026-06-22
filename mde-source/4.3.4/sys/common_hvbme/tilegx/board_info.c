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
 *
 */

/**
 * @file
 * Board information access routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "board_info.h"

/** Strings for booleans. */
static char* boolean[2] = { "False", "True" };

/** Type for settings values. */
struct setting
{
  const long val;
  const char* name;
};


/** Look up a value in a settings table. */
static const char*
setting(const struct setting* settings, long val)
{
  for (/* above */; settings->name; settings++)
    if (settings->val == val)
      return settings->name;

  return "";
}


/** Return the correct number of spaces for the given indentation level. */
static char*
indent_str(int level)
{
  //
  // 40 spaces should be more than enough.
  //
  static char spaces[] = "                                        ";
  return spaces + sizeof (spaces) - 1 - 2 * level;
}

#ifdef __BIG_ENDIAN__

/** Extract a little-endian bit-string from a sequence of bytes.
 * @param ptr Pointer to the first significant byte of the sequence.
 * @param bit_offset Number of the first bit to place in the result.
 *   A bit_offset of zero corresponds to the least significant bit
 *   of the first byte of the sequence, 1 to the next most significant
 *   bit of that byte, 8 to the least significant bit of the second
 *   byte (ptr[1]), and so forth.
 * @param bit_width Number of bits to place in the result.  Must be
 *   non-negative; if larger than 64, only the first 64 bits will be
 *   returned.
 * @return The extracted bits, right-justified, so the first
 *   extracted bit is the least significant bit of the returned value.
 *   Other bits in the returned value are zero.
 */
static uint64_t
get_le_bits(uint8_t* ptr, int bit_offset, int bit_width)
{
  ptr += bit_offset / 8;
  bit_offset %= 8;

  uint64_t rv = 0;

  int nbytes = 1 + (bit_offset + bit_width - 1) / 8 - bit_offset / 8;

  for (int i = 0; i < nbytes; i++)
    rv |= (*ptr++ << (i * 8)) >> bit_offset;

  rv &= (1UL << bit_width) - 1;

  return rv;
}

#endif // __BIG_ENDIAN__



//
// CODE BELOW THIS POINT IS AUTOMATICALLY GENERATED -- DO NOT EDIT
//

static void dump_bi_i2c_addr(struct bi_i2c_addr*, int, int);
static void dump_bi_signal_gpio(struct bi_signal_gpio*, int, int);
static void dump_bi_signal_i2c(struct bi_signal_i2c*, int, int);
static void dump_bi_signal_reset(struct bi_signal_reset*, int, int);
static void dump_bi_signal_fixed(struct bi_signal_fixed*, int, int);
static void dump_bi_signal_enet_int(struct bi_signal_enet_int*, int, int);
static void dump_bi_signal(struct bi_signal*, int, int);
static void dump_bi_nom_tile_freq(struct bi_nom_tile_freq*, int, bi_inst_t);
static void dump_bi_board_part_num(struct bi_board_part_num*, int, bi_inst_t);
static void dump_bi_board_serial_num(struct bi_board_serial_num*, int, bi_inst_t);
static void dump_bi_chip_num(struct bi_chip_num*, int, bi_inst_t);
static void dump_bi_max_mem_speed(struct bi_max_mem_speed*, int, bi_inst_t);
static void dump_bi_board_rev(struct bi_board_rev*, int, bi_inst_t);
static void dump_bi_dimm_map_entry(struct bi_dimm_map_entry*, int, int);
static void dump_bi_dimm_map(struct bi_dimm_map*, int, bi_inst_t);
static void dump_bi_spd_data(struct bi_spd_data*, int, bi_inst_t);
static void dump_bi_dimm_inst(struct bi_dimm_inst*, int, int);
static void dump_bi_dimm_label(struct bi_dimm_label*, int, bi_inst_t);
static void dump_bi_fan_info(struct bi_fan_info*, int, int);
static void dump_bi_temp_cfg_max6639(struct bi_temp_cfg_max6639*, int, int);
static void dump_bi_temp_cfg_adt7467(struct bi_temp_cfg_adt7467*, int, int);
static void dump_bi_temp_cfg(struct bi_temp_cfg*, int, bi_inst_t);
static void dump_bi_board_description(struct bi_board_description*, int, bi_inst_t);
static void dump_bi_fail_led(struct bi_fail_led*, int, bi_inst_t);
static void dump_bi_firmware_rev(struct bi_firmware_rev*, int, bi_inst_t);
static void dump_bi_bom_rev(struct bi_bom_rev*, int, bi_inst_t);
static void dump_bi_rtc_pcf8563(struct bi_rtc_pcf8563*, int, int);
static void dump_bi_rtc_cfg(struct bi_rtc_cfg*, int, bi_inst_t);
static void dump_bi_aib_i2c(struct bi_aib_i2c*, int, int);
static void dump_bi_aib(struct bi_aib*, int, bi_inst_t);
static void dump_bi_watch_max6369(struct bi_watch_max6369*, int, int);
static void dump_bi_watch_pcf8563(struct bi_watch_pcf8563*, int, int);
static void dump_bi_watch_cfg(struct bi_watch_cfg*, int, bi_inst_t);
static void dump_bi_poweroff(struct bi_poweroff*, int, bi_inst_t);
static void dump_bi_i2c_dev_cfg(struct bi_i2c_dev_cfg*, int, bi_inst_t);
static void dump_bi_i2c_switch_inst(struct bi_i2c_switch_inst*, int, int);
static void dump_bi_i2c_switch(struct bi_i2c_switch*, int, bi_inst_t);
static void dump_bi_misc_reset(struct bi_misc_reset*, int, bi_inst_t);
static void dump_bi_io_disable(struct bi_io_disable*, int, bi_inst_t);
static void dump_bi_cpu_volt_range(struct bi_cpu_volt_range*, int, bi_inst_t);
static void dump_bi_mem_volt_range(struct bi_mem_volt_range*, int, bi_inst_t);
static void dump_bi_clock_inst(struct bi_clock_inst*, int, int);
static void dump_bi_shim_clock(struct bi_shim_clock*, int, bi_inst_t);
static void dump_bi_enet_led(struct bi_enet_led*, int, int);
static void dump_bi_phy_link_cfg(struct bi_phy_link_cfg*, int, bi_inst_t);
static void dump_bi_port_inst(struct bi_port_inst*, int, int);
static void dump_bi_pcie_id(struct bi_pcie_id*, int, int);
static void dump_bi_pcie_port_cfg(struct bi_pcie_port_cfg*, int, bi_inst_t);
static void dump_bi_gpio_pin_cfg(struct bi_gpio_pin_cfg*, int, bi_inst_t);
static void dump_bi_usb_port_cfg(struct bi_usb_port_cfg*, int, bi_inst_t);
static void dump_bi_sfp_cfg(struct bi_sfp_cfg*, int, bi_inst_t);
static void dump_bi_console_cfg(struct bi_console_cfg*, int, bi_inst_t);
static void dump_bi_i2c_arbiter(struct bi_i2c_arbiter*, int, bi_inst_t);
static void dump_bi_shim_virt_inst(struct bi_shim_virt_inst*, int, bi_inst_t);
static void dump_bi_cpu_volt_char(struct bi_cpu_volt_char*, int, bi_inst_t);
static void dump_bi_msh_reg_entry(struct bi_msh_reg_entry*, int, int);
static void dump_bi_msh_reg(struct bi_msh_reg*, int, bi_inst_t);
static void dump_bi_serdes_lane_char_entry(struct bi_serdes_lane_char_entry*, int, int);
static void dump_bi_serdes_lane_char(struct bi_serdes_lane_char*, int, bi_inst_t);
static void dump_bi_xaui_refclk(struct bi_xaui_refclk*, int, bi_inst_t);
static void dump_bi_gpio_name(struct bi_gpio_name*, int, bi_inst_t);
static void dump_bi_i2cm_ctl_cfg(struct bi_i2cm_ctl_cfg*, int, bi_inst_t);

static void
dump_bi_i2c_addr(struct bi_i2c_addr* ptr,
  int len, int indent)
{
  static const struct setting switch_inst_settings[] = {
    { 7, "NONE" },
    { 0, NULL },
  };
  printf("  %sI2C device address: 0x%x\n", indent_str(indent), ptr->dev_addr * 2);
  printf("  %sbus: %d\n", indent_str(indent), ptr->bus);
  printf("  %sswitch_inst: %d %s\n", indent_str(indent),
         ptr->switch_inst, setting(switch_inst_settings, ptr->switch_inst));
  printf("  %sswitch_chan: %d\n", indent_str(indent), ptr->switch_chan);
}


static void
dump_bi_signal_gpio(struct bi_signal_gpio* ptr,
  int len, int indent)
{
  printf("  %spin: %d\n", indent_str(indent), ptr->pin);
  printf("  %sbank: %d\n", indent_str(indent), ptr->bank);
  printf("  %sopen_drain: %s\n", indent_str(indent), boolean[ptr->open_drain]);
  printf("  %sinverted: %s\n", indent_str(indent), boolean[ptr->inverted]);
}


static void
dump_bi_signal_i2c(struct bi_signal_i2c* ptr,
  int len, int indent)
{
  static const struct setting type_settings[] = {
    { 0, "PCA9555" },
    { 0, NULL },
  };
  printf("  %stype: %d %s\n", indent_str(indent),
         ptr->type, setting(type_settings, ptr->type));
  printf("  %spin: %d\n", indent_str(indent), ptr->pin);
  printf("  %sinverted: %s\n", indent_str(indent), boolean[ptr->inverted]);
  printf("  %sI2C address:\n", indent_str(indent));
  dump_bi_i2c_addr(&ptr->addr, len - 1, indent + 1);
}


static void
dump_bi_signal_reset(struct bi_signal_reset* ptr,
  int len, int indent)
{
  printf("  %sinverted: %s\n", indent_str(indent), boolean[ptr->inverted]);
}


static void
dump_bi_signal_fixed(struct bi_signal_fixed* ptr,
  int len, int indent)
{
  printf("  %svalue: %s\n", indent_str(indent), boolean[ptr->value]);
}


static void
dump_bi_signal_enet_int(struct bi_signal_enet_int* ptr,
  int len, int indent)
{
  printf("  %spin: %d\n", indent_str(indent), ptr->pin);
  printf("  %sinverted: %s\n", indent_str(indent), boolean[ptr->inverted]);
}


static void
dump_bi_signal(struct bi_signal* ptr,
  int len, int indent)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "GPIO" },
    { 2, "I2C" },
    { 3, "RESET" },
    { 4, "FIXED" },
    { 5, "ENET_INT" },
    { 0, NULL },
  };
  printf("  %stype: %d %s\n", indent_str(indent),
         ptr->type, setting(type_settings, ptr->type));
  if (ptr->type == 0)
  {
    /* Null element, no output */
  }
  else if (ptr->type == 1)
  {
    printf("  %su_gpio:\n", indent_str(indent));
    dump_bi_signal_gpio(&ptr->u.gpio, len - 1, indent + 1);
  }
  else if (ptr->type == 2)
  {
    printf("  %su_i2c:\n", indent_str(indent));
    dump_bi_signal_i2c(&ptr->u.i2c, len - 1, indent + 1);
  }
  else if (ptr->type == 3)
  {
    printf("  %su_reset:\n", indent_str(indent));
    dump_bi_signal_reset(&ptr->u.reset, len - 1, indent + 1);
  }
  else if (ptr->type == 4)
  {
    printf("  %su_fixed:\n", indent_str(indent));
    dump_bi_signal_fixed(&ptr->u.fixed, len - 1, indent + 1);
  }
  else if (ptr->type == 5)
  {
    printf("  %su_enet_int:\n", indent_str(indent));
    dump_bi_signal_enet_int(&ptr->u.enet_int, len - 1, indent + 1);
  }
  else
  {
    printf("Union selector signal.u.type "
           "has invalid value %d\n", ptr->type);
  }
}


static void
dump_bi_nom_tile_freq(struct bi_nom_tile_freq* ptr,
  int len, bi_inst_t instance)
{
  printf("nom_tile_freq: instance %d\n", instance);
  printf("  Clock frequency: %d\n", ptr->clock);
}


static void
dump_bi_board_part_num(struct bi_board_part_num* ptr,
  int len, bi_inst_t instance)
{
  printf("board_part_num: instance %d\n", instance);
  printf("  Part number: %.*s\n", len, ptr->part_num);
}


static void
dump_bi_board_serial_num(struct bi_board_serial_num* ptr,
  int len, bi_inst_t instance)
{
  printf("board_serial_num: instance %d\n", instance);
  printf("  Serial number: %.*s\n", len, ptr->serial_num);
}


static void
dump_bi_chip_num(struct bi_chip_num* ptr,
  int len, bi_inst_t instance)
{
  printf("chip_num: instance %d\n", instance);
  printf("  Chip number: %.*s\n", len, ptr->chip_num);
}


static void
dump_bi_max_mem_speed(struct bi_max_mem_speed* ptr,
  int len, bi_inst_t instance)
{
  printf("max_mem_speed: instance %d\n", instance);
  for (int i = 0; i < (len) / sizeof (ptr->speed[0]); i++)
  {
    printf("  speed[%d]: %d\n", i, ptr->speed[i]);
  }
}


static void
dump_bi_board_rev(struct bi_board_rev* ptr,
  int len, bi_inst_t instance)
{
  printf("board_rev: instance %d\n", instance);
  printf("  Board revision: %.*s\n", len, ptr->board_rev);
}


static void
dump_bi_dimm_map_entry(struct bi_dimm_map_entry* ptr,
  int len, int indent)
{
  printf("  %sonboard: %s\n", indent_str(indent), boolean[ptr->onboard]);
  if (ptr->onboard == 0)
  {
    printf("  %saddr_i2c:\n", indent_str(indent));
    dump_bi_i2c_addr(&ptr->addr.i2c, len - 2, indent + 1);
  }
  else if (ptr->onboard == 1)
  {
    printf("  %saddr_onboard_inst: %d\n", indent_str(indent),
            ptr->addr.onboard_inst);
  }
  else
  {
    printf("Union selector dimm_map_entry.addr.onboard "
           "has invalid value %d\n", ptr->onboard);
  }
}


static void
dump_bi_dimm_map(struct bi_dimm_map* ptr,
  int len, bi_inst_t instance)
{
  printf("dimm_map: instance %d\n", instance);
  printf("  cs_per_slot: %d\n", ptr->cs_per_slot);
  printf("  dqs_offset: %d\n", ptr->dqs_offset * 10);
  for (int i = 0; i < (len - 4) / sizeof (ptr->map[0]); i++)
  {
    printf("  map[%d]:\n", i);
    dump_bi_dimm_map_entry(&ptr->map[i], 4, 1);
  }
}


static void
dump_bi_spd_data(struct bi_spd_data* ptr,
  int len, bi_inst_t instance)
{
  printf("spd_data: instance %d\n", instance);
  for (int i = 0; i < (len) / sizeof (ptr->spd[0]); i++)
  {
    printf("  spd[%d]: 0x%x\n", i, ptr->spd[i]);
  }
}


static void
dump_bi_dimm_inst(struct bi_dimm_inst* ptr,
  int len, int indent)
{
  printf("  %sdimm: %d\n", indent_str(indent), ptr->dimm);
  printf("  %sshim: %d\n", indent_str(indent), ptr->shim);
}


static void
dump_bi_dimm_label(struct bi_dimm_label* ptr,
  int len, bi_inst_t instance)
{
  printf("dimm_label: instance %d\n", instance);
  union u_dimm_inst {
    bi_inst_t instance;
    struct bi_dimm_inst structure;
  } u_dimm_inst = {
    .instance = instance
  };
  dump_bi_dimm_inst(&u_dimm_inst.structure, 1, sizeof (u_dimm_inst.structure));
  printf("  DIMM label: %.*s\n", len, ptr->label);
}


static void
dump_bi_fan_info(struct bi_fan_info* ptr,
  int len, int indent)
{
  printf("  %smax_speed: %d\n", indent_str(indent), ptr->max_speed * 100);
  printf("  %stach_ppr: %d\n", indent_str(indent), ptr->tach_ppr + 1);
  printf("  %sfour_wire: %s\n", indent_str(indent), boolean[ptr->four_wire]);
  printf("  %spwm_act_low: %s\n", indent_str(indent), boolean[ptr->pwm_act_low]);
  printf("  %stemp_valid: %s\n", indent_str(indent), boolean[ptr->temp_valid]);
}


static void
dump_bi_temp_cfg_max6639(struct bi_temp_cfg_max6639* ptr,
  int len, int indent)
{
  for (int i = 0; i < 2; i++)
  {
    printf("  %sfans[%d]:\n", indent_str(indent), i);
    dump_bi_fan_info(&ptr->fans[i], 2, indent + 1);
  }
  for (int i = 0; i < (len - 4) / sizeof (ptr->sigs[0]); i++)
  {
    printf("  %ssigs[%d]:\n", indent_str(indent), i);
    dump_bi_signal(&ptr->sigs[i], 4, indent + 1);
  }
}


static void
dump_bi_temp_cfg_adt7467(struct bi_temp_cfg_adt7467* ptr,
  int len, int indent)
{
  for (int i = 0; i < 4; i++)
  {
    printf("  %sfans[%d]:\n", indent_str(indent), i);
    dump_bi_fan_info(&ptr->fans[i], 2, indent + 1);
  }
  for (int i = 0; i < (len - 8) / sizeof (ptr->sigs[0]); i++)
  {
    printf("  %ssigs[%d]:\n", indent_str(indent), i);
    dump_bi_signal(&ptr->sigs[i], 4, indent + 1);
  }
}


static void
dump_bi_temp_cfg(struct bi_temp_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "UNKNOWN" },
    { 1, "LM84" },
    { 2, "LM95235" },
    { 3, "MAX6639" },
    { 4, "ADT7467" },
    { 0, NULL },
  };
  printf("temp_cfg: instance %d\n", instance);
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  printf("  I2C address:\n");
  dump_bi_i2c_addr(&ptr->addr, len - 2, 1);
  if (ptr->type == 0 || ptr->type == 1 || ptr->type == 2)
  {
    /* Null element, no output */
  }
  else if (ptr->type == 3)
  {
    printf("  u_max6639:\n");
    dump_bi_temp_cfg_max6639(&ptr->u.max6639, len - 4, 1);
  }
  else if (ptr->type == 4)
  {
    printf("  u_adt7467:\n");
    dump_bi_temp_cfg_adt7467(&ptr->u.adt7467, len - 4, 1);
  }
  else
  {
    printf("Union selector temp_cfg.u.type "
           "has invalid value %d\n", ptr->type);
  }
}


static void
dump_bi_board_description(struct bi_board_description* ptr,
  int len, bi_inst_t instance)
{
  printf("board_description: instance %d\n", instance);
  printf("  Board description: %.*s\n", len, ptr->desc);
}


static void
dump_bi_fail_led(struct bi_fail_led* ptr,
  int len, bi_inst_t instance)
{
  printf("fail_led: instance %d\n", instance);
  printf("  Board failure LED signal:\n");
  dump_bi_signal(&ptr->signal, len - 0, 1);
}


static void
dump_bi_firmware_rev(struct bi_firmware_rev* ptr,
  int len, bi_inst_t instance)
{
  printf("firmware_rev: instance %d\n", instance);
  printf("  Firmware revision: %.*s\n", len, ptr->rev);
}


static void
dump_bi_bom_rev(struct bi_bom_rev* ptr,
  int len, bi_inst_t instance)
{
  printf("bom_rev: instance %d\n", instance);
  printf("  BOM revision: %.*s\n", len, ptr->rev);
}


static void
dump_bi_rtc_pcf8563(struct bi_rtc_pcf8563* ptr,
  int len, int indent)
{
  printf("  %sI2C address:\n", indent_str(indent));
  dump_bi_i2c_addr(&ptr->addr, len - 0, indent + 1);
}


static void
dump_bi_rtc_cfg(struct bi_rtc_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "PCF8563" },
    { 0, NULL },
  };
  printf("rtc_cfg: instance %d\n", instance);
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  if (ptr->type == 1)
  {
    printf("  u_pcf8563:\n");
    dump_bi_rtc_pcf8563(&ptr->u.pcf8563, len - 1, 1);
  }
  else
  {
    printf("Union selector rtc_cfg.u.type "
           "has invalid value %d\n", ptr->type);
  }
}


static void
dump_bi_aib_i2c(struct bi_aib_i2c* ptr,
  int len, int indent)
{
  printf("  %sI2C address:\n", indent_str(indent));
  dump_bi_i2c_addr(&ptr->addr, len - 0, indent + 1);
}


static void
dump_bi_aib(struct bi_aib* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "I2C" },
    { 0, NULL },
  };
  printf("aib: instance %d\n", instance);
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  if (ptr->type == 1)
  {
    printf("  u_i2c:\n");
    dump_bi_aib_i2c(&ptr->u.i2c, len - 1, 1);
  }
  else
  {
    printf("Union selector aib.u.type "
           "has invalid value %d\n", ptr->type);
  }
}


static void
dump_bi_watch_max6369(struct bi_watch_max6369* ptr,
  int len, int indent)
{
  printf("  %sI2C address:\n", indent_str(indent));
  dump_bi_i2c_addr(&ptr->addr, len - 0, indent + 1);
  printf("  %sSET0:\n", indent_str(indent));
  dump_bi_signal(&ptr->set0, len - 2, indent + 1);
  printf("  %sSET1:\n", indent_str(indent));
  dump_bi_signal(&ptr->set1, len - 6, indent + 1);
  printf("  %sSET1:\n", indent_str(indent));
  dump_bi_signal(&ptr->set2, len - 10, indent + 1);
  printf("  %sWDI:\n", indent_str(indent));
  dump_bi_signal(&ptr->wdi, len - 14, indent + 1);
  printf("  %sWDO:\n", indent_str(indent));
  dump_bi_signal(&ptr->wdo, len - 18, indent + 1);
}


static void
dump_bi_watch_pcf8563(struct bi_watch_pcf8563* ptr,
  int len, int indent)
{
  printf("  %sI2C address:\n", indent_str(indent));
  dump_bi_i2c_addr(&ptr->addr, len - 0, indent + 1);
  printf("  %sEnable signal:\n", indent_str(indent));
  dump_bi_signal(&ptr->enable, len - 2, indent + 1);
}


static void
dump_bi_watch_cfg(struct bi_watch_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "MAX6369" },
    { 1, "PCF8563" },
    { 0, NULL },
  };
  printf("watch_cfg: instance %d\n", instance);
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  if (ptr->type == 1)
  {
    printf("  u_max6369:\n");
    dump_bi_watch_max6369(&ptr->u.max6369, len - 1, 1);
  }
  else if (ptr->type == 1)
  {
    printf("  u_pcf8563:\n");
    dump_bi_watch_pcf8563(&ptr->u.pcf8563, len - 1, 1);
  }
  else
  {
    printf("Union selector watch_cfg.u.type "
           "has invalid value %d\n", ptr->type);
  }
}


static void
dump_bi_poweroff(struct bi_poweroff* ptr,
  int len, bi_inst_t instance)
{
  printf("poweroff: instance %d\n", instance);
  printf("  Poweroff signal:\n");
  dump_bi_signal(&ptr->signal, len - 0, 1);
}


static void
dump_bi_i2c_dev_cfg(struct bi_i2c_dev_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting page_size_settings[] = {
    { 0, "DEFAULT" },
    { 0, NULL },
  };
  static const struct setting write_cycle_settings[] = {
    { 0, "DEFAULT" },
    { 9, "MAX" },
    { 10, "MIN" },
    { 0, NULL },
  };
  printf("i2c_dev_cfg: instance %d\n", instance);
  printf("  I2C address:\n");
  dump_bi_i2c_addr(&ptr->addr, len - 0, 1);
  printf("  mem_addr_8bit: %s\n", boolean[ptr->mem_addr_8bit]);
  printf("  mem_addr_0bit: %s\n", boolean[ptr->mem_addr_0bit]);
  printf("  page_size: %d %s\n",
         ptr->page_size, setting(page_size_settings, ptr->page_size));
  printf("  write_cycle: %d %s\n",
         ptr->write_cycle, setting(write_cycle_settings, ptr->write_cycle));
  printf("  Device name: %.*s\n", len - 4, ptr->name);
}


static void
dump_bi_i2c_switch_inst(struct bi_i2c_switch_inst* ptr,
  int len, int indent)
{
  printf("  %sshim: %d\n", indent_str(indent), ptr->shim);
  printf("  %sswitch_inst: %d\n", indent_str(indent), ptr->switch_inst);
}


static void
dump_bi_i2c_switch(struct bi_i2c_switch* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "PCA954X_SWITCH" },
    { 2, "PCA954X_MUX" },
    { 3, "PCA9547" },
    { 0, NULL },
  };
  printf("i2c_switch: instance %d\n", instance);
  union u_i2c_switch_inst {
    bi_inst_t instance;
    struct bi_i2c_switch_inst structure;
  } u_i2c_switch_inst = {
    .instance = instance
  };
  dump_bi_i2c_switch_inst(&u_i2c_switch_inst.structure, 1, sizeof (u_i2c_switch_inst.structure));
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  printf("  I2C device address: 0x%x\n", ptr->dev_addr * 2);
  printf("  Switch conflict ports: 0x%x\n", ptr->conflict_ports);
  printf("  Reset signal:\n");
  dump_bi_signal(&ptr->reset, len - 4, 1);
}


static void
dump_bi_misc_reset(struct bi_misc_reset* ptr,
  int len, bi_inst_t instance)
{
  printf("misc_reset: instance %d\n", instance);
  printf("  Reset assertion time, us: %d\n", ptr->assert_time);
  for (int i = 0; i < (len - 4) / sizeof (ptr->resets[0]); i++)
  {
    printf("  resets[%d]:\n", i);
    dump_bi_signal(&ptr->resets[i], 4, 1);
  }
}


static void
dump_bi_io_disable(struct bi_io_disable* ptr,
  int len, bi_inst_t instance)
{
  printf("io_disable: instance %d\n", instance);
  for (int i = 0; i < (len) / sizeof (ptr->disable[0]); i++)
  {
    printf("  disable[%d]: 0x%llx\n", i, ptr->disable[i]);
  }
}


static void
dump_bi_cpu_volt_range(struct bi_cpu_volt_range* ptr,
  int len, bi_inst_t instance)
{
  printf("cpu_volt_range: instance %d\n", instance);
  printf("  Minimum CPU voltage: 0x%x\n", ptr->vmin);
  printf("  Maximum CPU voltage: 0x%x\n", ptr->vmax);
}


static void
dump_bi_mem_volt_range(struct bi_mem_volt_range* ptr,
  int len, bi_inst_t instance)
{
  printf("mem_volt_range: instance %d\n", instance);
  printf("  Minimum DDR3 voltage: 0x%x\n", ptr->vmin);
  printf("  Maximum DDR3 voltage: 0x%x\n", ptr->vmax);
}


static void
dump_bi_clock_inst(struct bi_clock_inst* ptr,
  int len, int indent)
{
  static const struct setting type_settings[] = {
    { 0, "MICA_CRYPTO" },
    { 1, "MICA_COMPRESS" },
    { 2, "TRIO" },
    { 3, "USB" },
    { 4, "MPIPE_MAIN" },
    { 5, "MPIPE_CLASSIFIER" },
    { 0, NULL },
  };
  printf("  %stype: %d %s\n", indent_str(indent),
         ptr->type, setting(type_settings, ptr->type));
  printf("  %sshim: %d\n", indent_str(indent), ptr->shim);
}


static void
dump_bi_shim_clock(struct bi_shim_clock* ptr,
  int len, bi_inst_t instance)
{
  printf("shim_clock: instance %d\n", instance);
  union u_clock_inst {
    bi_inst_t instance;
    struct bi_clock_inst structure;
  } u_clock_inst = {
    .instance = instance
  };
  dump_bi_clock_inst(&u_clock_inst.structure, 1, sizeof (u_clock_inst.structure));
  printf("  Frequency: %d\n", ptr->freq);
}


static void
dump_bi_enet_led(struct bi_enet_led* ptr,
  int len, int indent)
{
  static const struct setting cfg_settings[] = {
    { 0, "NONE" },
    { 1, "OFF" },
    { 2, "ON" },
    { 3, "LINK" },
    { 4, "LINK_ACT" },
    { 5, "LINK_ACT_TX" },
    { 6, "LINK_ACT_RX" },
    { 7, "ACT" },
    { 8, "ACT_TX" },
    { 9, "ACT_RX" },
    { 10, "SPEED_10M" },
    { 11, "SPEED_100M" },
    { 12, "SPEED_1G" },
    { 13, "FDX" },
    { 30, "INTR" },
    { 31, "DEFAULT" },
    { 0, NULL },
  };
  printf("  %scfg: %d %s\n", indent_str(indent),
         ptr->cfg, setting(cfg_settings, ptr->cfg));
}


static void
dump_bi_phy_link_cfg(struct bi_phy_link_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting lanes_settings[] = {
    { 0, "LANE0" },
    { 1, "LANE1" },
    { 2, "LANE2" },
    { 3, "LANE3" },
    { 4, "LANE4" },
    { 5, "LANE5" },
    { 6, "LANE6" },
    { 7, "LANE7" },
    { 8, "LANE8" },
    { 9, "LANE9" },
    { 10, "LANE10" },
    { 11, "LANE11" },
    { 12, "LANE12" },
    { 13, "LANE13" },
    { 14, "LANE14" },
    { 15, "LANE15" },
    { 16, "LANE0_3" },
    { 17, "LANE4_7" },
    { 18, "LANE8_11" },
    { 19, "LANE12_15" },
    { 20, "LANE13_15" },
    { 21, "LANE11_15" },
    { 22, "LANE6_15" },
    { 0, NULL },
  };
  static const struct setting link_name_num_settings[] = {
    { 0x3F, "DEFAULT" },
    { 0, NULL },
  };
  printf("phy_link_cfg: instance %d\n", instance);
  printf("  speed_10m: %s\n", boolean[ptr->speed_10m]);
  printf("  speed_100m: %s\n", boolean[ptr->speed_100m]);
  printf("  speed_1g: %s\n", boolean[ptr->speed_1g]);
  printf("  speed_10g: %s\n", boolean[ptr->speed_10g]);
  printf("  speed_20g: %s\n", boolean[ptr->speed_20g]);
  printf("  speed_25g: %s\n", boolean[ptr->speed_25g]);
  printf("  speed_50g: %s\n", boolean[ptr->speed_50g]);
  printf("  sfp_txout_inv: %s\n", boolean[ptr->sfp_txout_inv]);
  printf("  phy_auto_cfg: %s\n", boolean[ptr->phy_auto_cfg]);
  printf("  no_phy: %s\n", boolean[ptr->no_phy]);
  printf("  shared_reset: %s\n", boolean[ptr->shared_reset]);
  printf("  shared_intr: %s\n", boolean[ptr->shared_intr]);
  for (int i = 0; i < 6; i++)
  {
    printf("  leds[%d]:\n", i);
    dump_bi_enet_led(&ptr->leds[i], 1, 1);
  }
  printf("  mdio_bus_xgbe: %s\n", boolean[ptr->mdio_bus_xgbe]);
  printf("  mdio_addr: 0x%x\n", ptr->mdio_addr);
  printf("  num_mac_addrs: 0x%x\n", ptr->num_mac_addrs);
  printf("  MAC address: ");
  for (int i = 0; i < 6; i++)
  {
    if (i)
      printf(":");
    printf("%02x", ptr->mac_addr[i]);
  }
  printf("\n");
  printf("  lanes: 0x%x %s\n",
         ptr->lanes, setting(lanes_settings, ptr->lanes));
  printf("  link_name_num: %d %s\n",
         ptr->link_name_num, setting(link_name_num_settings, ptr->link_name_num));
  printf("  Reset signal:\n");
  dump_bi_signal(&ptr->reset_sig, len - 20, 1);
  printf("  Interrupt signal:\n");
  dump_bi_signal(&ptr->intr_sig, len - 24, 1);
}


static void
dump_bi_port_inst(struct bi_port_inst* ptr,
  int len, int indent)
{
  printf("  %sshim: %d\n", indent_str(indent), ptr->shim);
  printf("  %sport: %d\n", indent_str(indent), ptr->port);
}


static void
dump_bi_pcie_id(struct bi_pcie_id* ptr,
  int len, int indent)
{
  printf("  %srev_id: 0x%x\n", indent_str(indent), ptr->rev_id);
  printf("  %sprog_intf: 0x%x\n", indent_str(indent), ptr->prog_intf);
  printf("  %ssubclass: 0x%x\n", indent_str(indent), ptr->subclass);
  printf("  %sbaseclass: 0x%x\n", indent_str(indent), ptr->baseclass);
  printf("  %svendor: 0x%x\n", indent_str(indent), ptr->vendor);
  printf("  %sdevice: 0x%x\n", indent_str(indent), ptr->device);
  printf("  %ssubsys_vendor: 0x%x\n", indent_str(indent), ptr->subsys_vendor);
  printf("  %ssubsys_device: 0x%x\n", indent_str(indent), ptr->subsys_device);
}


static void
dump_bi_pcie_port_cfg(struct bi_pcie_port_cfg* ptr,
  int len, bi_inst_t instance)
{
  printf("pcie_port_cfg: instance %d\n", instance);
  union u_port_inst {
    bi_inst_t instance;
    struct bi_port_inst structure;
  } u_port_inst = {
    .instance = instance
  };
  dump_bi_port_inst(&u_port_inst.structure, 1, sizeof (u_port_inst.structure));
  printf("  allow_rc: %s\n", boolean[ptr->allow_rc]);
  printf("  allow_ep: %s\n", boolean[ptr->allow_ep]);
  printf("  allow_sio: %s\n", boolean[ptr->allow_sio]);
  printf("  allow_x1: %s\n", boolean[ptr->allow_x1]);
  printf("  allow_x2: %s\n", boolean[ptr->allow_x2]);
  printf("  allow_x4: %s\n", boolean[ptr->allow_x4]);
  printf("  allow_x8: %s\n", boolean[ptr->allow_x8]);
  printf("  override_id: %s\n", boolean[ptr->override_id]);
  printf("  removable: %s\n", boolean[ptr->removable]);
  printf("  PERST signal:\n");
  dump_bi_signal(&ptr->perst_sig, len - 4, 1);
  if (ptr->override_id == 0)
  {
    /* Null element, no output */
  }
  else if (ptr->override_id == 1)
  {
    printf("  u_id:\n");
    dump_bi_pcie_id(&ptr->u.id, len - 8, 1);
  }
  else
  {
    printf("Union selector pcie_port_cfg.u.override_id "
           "has invalid value %d\n", ptr->override_id);
  }
}


static void
dump_bi_gpio_pin_cfg(struct bi_gpio_pin_cfg* ptr,
  int len, bi_inst_t instance)
{
  printf("gpio_pin_cfg: instance %d\n", instance);
  printf("  Input pins: 0x%llx\n", ptr->input);
  printf("  Output pins: 0x%llx\n", ptr->output);
  printf("  Output open-drain pins: 0x%llx\n", ptr->output_od);
}


static void
dump_bi_usb_port_cfg(struct bi_usb_port_cfg* ptr,
  int len, bi_inst_t instance)
{
  printf("usb_port_cfg: instance %d\n", instance);
  union u_port_inst {
    bi_inst_t instance;
    struct bi_port_inst structure;
  } u_port_inst = {
    .instance = instance
  };
  dump_bi_port_inst(&u_port_inst.structure, 1, sizeof (u_port_inst.structure));
  printf("  allow_device: %s\n", boolean[ptr->allow_device]);
  printf("  allow_host: %s\n", boolean[ptr->allow_host]);
  printf("  PHY reset signal:\n");
  dump_bi_signal(&ptr->phy_reset_sig, len - 4, 1);
}


static void
dump_bi_sfp_cfg(struct bi_sfp_cfg* ptr,
  int len, bi_inst_t instance)
{
  printf("sfp_cfg: instance %d\n", instance);
  union u_port_inst {
    bi_inst_t instance;
    struct bi_port_inst structure;
  } u_port_inst = {
    .instance = instance
  };
  dump_bi_port_inst(&u_port_inst.structure, 1, sizeof (u_port_inst.structure));
  printf("  i2c:\n");
  dump_bi_i2c_addr(&ptr->i2c, len - 2, 1);
  printf("  Receive loss signal:\n");
  dump_bi_signal(&ptr->rx_los_sig, len - 4, 1);
  printf("  Transmit fault signal:\n");
  dump_bi_signal(&ptr->tx_fault_sig, len - 8, 1);
  printf("  Transmit disable signal:\n");
  dump_bi_signal(&ptr->tx_disable_sig, len - 12, 1);
  printf("  Module absent signal:\n");
  dump_bi_signal(&ptr->mod_abs_sig, len - 16, 1);
  printf("  Link up signal:\n");
  dump_bi_signal(&ptr->link_led_sig, len - 20, 1);
}


static void
dump_bi_console_cfg(struct bi_console_cfg* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting parity_settings[] = {
    { 0, "NONE" },
    { 1, "MARK" },
    { 2, "SPACE" },
    { 3, "EVEN" },
    { 4, "ODD" },
    { 0, NULL },
  };
  static const struct setting data_bits_settings[] = {
    { 0, "EIGHT" },
    { 1, "SEVEN" },
    { 0, NULL },
  };
  static const struct setting stop_bits_settings[] = {
    { 0, "ONE" },
    { 1, "TWO" },
    { 0, NULL },
  };
  static const struct setting early_console_delay_settings[] = {
    { 31, "FOREVER" },
    { 0, NULL },
  };
  printf("console_cfg: instance %d\n", instance);
  printf("  baud_rate: %d\n", ptr->baud_rate);
  printf("  port: %d\n", ptr->port);
  printf("  parity: %d %s\n",
         ptr->parity, setting(parity_settings, ptr->parity));
  printf("  data_bits: %d %s\n",
         ptr->data_bits, setting(data_bits_settings, ptr->data_bits));
  printf("  stop_bits: %d %s\n",
         ptr->stop_bits, setting(stop_bits_settings, ptr->stop_bits));
  printf("  early_console_delay: %d %s\n",
         ptr->early_console_delay, setting(early_console_delay_settings, ptr->early_console_delay));
}


static void
dump_bi_i2c_arbiter(struct bi_i2c_arbiter* ptr,
  int len, bi_inst_t instance)
{
  static const struct setting type_settings[] = {
    { 0, "NONE" },
    { 1, "PCA9541" },
    { 0, NULL },
  };
  printf("i2c_arbiter: instance %d\n", instance);
  printf("  type: %d %s\n",
         ptr->type, setting(type_settings, ptr->type));
  printf("  I2C device address: 0x%x\n", ptr->dev_addr * 2);
  printf("  I2C switch channel: %d\n", ptr->switch_chan);
  printf("  Reset signal:\n");
  dump_bi_signal(&ptr->reset, len - 4, 1);
  printf("  Request signal:\n");
  dump_bi_signal(&ptr->req, len - 8, 1);
  printf("  Grant signal:\n");
  dump_bi_signal(&ptr->grant, len - 12, 1);
}


static void
dump_bi_shim_virt_inst(struct bi_shim_virt_inst* ptr,
  int len, bi_inst_t instance)
{
  printf("shim_virt_inst: instance %d\n", instance);
  union u_clock_inst {
    bi_inst_t instance;
    struct bi_clock_inst structure;
  } u_clock_inst = {
    .instance = instance
  };
  dump_bi_clock_inst(&u_clock_inst.structure, 1, sizeof (u_clock_inst.structure));
  printf("  Virtual instance: %d\n", ptr->virt_inst);
}


static void
dump_bi_cpu_volt_char(struct bi_cpu_volt_char* ptr,
  int len, bi_inst_t instance)
{
  printf("cpu_volt_char: instance %d\n", instance);
  printf("  Load Line Enable: %s\n", boolean[ptr->load_line]);
  printf("  Load Line Multiplier: %d\n", ptr->load_line_factor);
  printf("  Load Line Offset: %d\n", ptr->load_line_offset);
  printf("  Reserved: 0x%x\n", ptr->reserved);
}


static void
dump_bi_msh_reg_entry(struct bi_msh_reg_entry* ptr,
  int len, int indent)
{
  static const struct setting parameter_settings[] = {
    { 1, "RDLAT" },
    { 2, "ADDR_MIRROR" },
    { 3, "ODT_RTT_NOM" },
    { 4, "ODT_RTT_WR" },
    { 5, "CTRL_TERM" },
    { 0, NULL },
  };
  printf("  %smin_speed: %d\n", indent_str(indent), ptr->min_speed);
  printf("  %smin_dimm: %d\n", indent_str(indent), ptr->min_dimm + 1);
  printf("  %smin_rank: %d\n", indent_str(indent), ptr->min_rank + 1);
  printf("  %smin_voltage: %d\n", indent_str(indent), ptr->min_voltage * -6250 + 1212500);
  printf("  %sparameter: %d %s\n", indent_str(indent),
         ptr->parameter, setting(parameter_settings, ptr->parameter));
  printf("  %svalue: 0x%x\n", indent_str(indent), ptr->value);
}


static void
dump_bi_msh_reg(struct bi_msh_reg* ptr,
  int len, bi_inst_t instance)
{
  printf("msh_reg: instance %d\n", instance);
  for (int i = 0; i < (len) / sizeof (ptr->entries[0]); i++)
  {
    printf("  entries[%d]:\n", i);
    dump_bi_msh_reg_entry(&ptr->entries[i], 8, 1);
  }
}


static void
dump_bi_serdes_lane_char_entry(struct bi_serdes_lane_char_entry* ptr,
  int len, int indent)
{
  printf("  %srx_len: %d\n", indent_str(indent), ptr->rx_len);
  printf("  %stx_len: %d\n", indent_str(indent), ptr->tx_len);
  printf("  %sReserved: 0x%x\n", indent_str(indent), ptr->reserved);
}


static void
dump_bi_serdes_lane_char(struct bi_serdes_lane_char* ptr,
  int len, bi_inst_t instance)
{
  printf("serdes_lane_char: instance %d\n", instance);
  union u_clock_inst {
    bi_inst_t instance;
    struct bi_clock_inst structure;
  } u_clock_inst = {
    .instance = instance
  };
  dump_bi_clock_inst(&u_clock_inst.structure, 1, sizeof (u_clock_inst.structure));
  for (int i = 0; i < (len) / sizeof (ptr->entries[0]); i++)
  {
    printf("  entries[%d]:\n", i);
    dump_bi_serdes_lane_char_entry(&ptr->entries[i], 4, 1);
  }
}


static void
dump_bi_xaui_refclk(struct bi_xaui_refclk* ptr,
  int len, bi_inst_t instance)
{
  printf("xaui_refclk: instance %d\n", instance);
  printf("  speed: %d\n", ptr->speed);
}


static void
dump_bi_gpio_name(struct bi_gpio_name* ptr,
  int len, bi_inst_t instance)
{
  printf("gpio_name: instance %d\n", instance);
  printf("  Input pins: 0x%llx\n", ptr->input);
  printf("  Output pins: 0x%llx\n", ptr->output);
  printf("  Output open-drain pins: 0x%llx\n", ptr->output_od);
  printf("  Inverted pins: 0x%llx\n", ptr->invert);
  printf("  Name: %.*s\n", len - 32, ptr->name);
}


static void
dump_bi_i2cm_ctl_cfg(struct bi_i2cm_ctl_cfg* ptr,
  int len, bi_inst_t instance)
{
  printf("i2cm_ctl_cfg: instance %d\n", instance);
  printf("  I2C frequency: %d\n", ptr->freq_khz);
  printf("  I2C glitch mask: %d\n", ptr->glitch);
  printf("  I2C electrical: 0x%x\n", ptr->elec);
}


/** Dump out a board information block.
 * @param blockbuf Block to dump.
 * @param blocklen Length in bytes of the block.
 */
void
bi_dumpbuf(uint32_t* blockbuf, int blocklen)
{
  int offset = 0;

  printf("Board Information Block dump (block is %d bytes total):\n",
         blocklen);

  while (1)
  {
    uint32_t* resbuf;

    uint32_t desc = bi_find(blockbuf, blocklen, -1, -1, &resbuf, &offset);

    if (desc == BI_NULL)
      break;

    uint32_t type = BI_TYPE(desc);
    uint32_t inst = BI_INST(desc);
    uint32_t wds = BI_WDS(desc);

    switch(type)
    {
    case BI_TYPE_NOM_TILE_FREQ:
      dump_bi_nom_tile_freq((struct bi_nom_tile_freq*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_PART_NUM:
      dump_bi_board_part_num((struct bi_board_part_num*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_SERIAL_NUM:
      dump_bi_board_serial_num((struct bi_board_serial_num*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CHIP_NUM:
      dump_bi_chip_num((struct bi_chip_num*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MAX_MEM_SPEED:
      dump_bi_max_mem_speed((struct bi_max_mem_speed*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_REV:
      dump_bi_board_rev((struct bi_board_rev*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_DIMM_MAP:
      dump_bi_dimm_map((struct bi_dimm_map*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SPD_DATA:
      dump_bi_spd_data((struct bi_spd_data*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_DIMM_LABEL:
      dump_bi_dimm_label((struct bi_dimm_label*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_TEMP_CFG:
      dump_bi_temp_cfg((struct bi_temp_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_DESCRIPTION:
      dump_bi_board_description((struct bi_board_description*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_FAIL_LED:
      dump_bi_fail_led((struct bi_fail_led*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_FIRMWARE_REV:
      dump_bi_firmware_rev((struct bi_firmware_rev*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOM_REV:
      dump_bi_bom_rev((struct bi_bom_rev*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_RTC_CFG:
      dump_bi_rtc_cfg((struct bi_rtc_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_AIB:
      dump_bi_aib((struct bi_aib*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_WATCH_CFG:
      dump_bi_watch_cfg((struct bi_watch_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_POWEROFF:
      dump_bi_poweroff((struct bi_poweroff*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_DEV_CFG:
      dump_bi_i2c_dev_cfg((struct bi_i2c_dev_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_SWITCH:
      dump_bi_i2c_switch((struct bi_i2c_switch*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MISC_RESET:
      dump_bi_misc_reset((struct bi_misc_reset*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_IO_DISABLE:
      dump_bi_io_disable((struct bi_io_disable*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CPU_VOLT_RANGE:
      dump_bi_cpu_volt_range((struct bi_cpu_volt_range*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MEM_VOLT_RANGE:
      dump_bi_mem_volt_range((struct bi_mem_volt_range*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SHIM_CLOCK:
      dump_bi_shim_clock((struct bi_shim_clock*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_PHY_LINK_CFG:
      dump_bi_phy_link_cfg((struct bi_phy_link_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_PCIE_PORT_CFG:
      dump_bi_pcie_port_cfg((struct bi_pcie_port_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_GPIO_PIN_CFG:
      dump_bi_gpio_pin_cfg((struct bi_gpio_pin_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_USB_PORT_CFG:
      dump_bi_usb_port_cfg((struct bi_usb_port_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SFP_CFG:
      dump_bi_sfp_cfg((struct bi_sfp_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CONSOLE_CFG:
      dump_bi_console_cfg((struct bi_console_cfg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_ARBITER:
      dump_bi_i2c_arbiter((struct bi_i2c_arbiter*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SHIM_VIRT_INST:
      dump_bi_shim_virt_inst((struct bi_shim_virt_inst*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CPU_VOLT_CHAR:
      dump_bi_cpu_volt_char((struct bi_cpu_volt_char*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MSH_REG:
      dump_bi_msh_reg((struct bi_msh_reg*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SERDES_LANE_CHAR:
      dump_bi_serdes_lane_char((struct bi_serdes_lane_char*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_XAUI_REFCLK:
      dump_bi_xaui_refclk((struct bi_xaui_refclk*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_GPIO_NAME:
      dump_bi_gpio_name((struct bi_gpio_name*) resbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2CM_CTL_CFG:
      dump_bi_i2cm_ctl_cfg((struct bi_i2cm_ctl_cfg*) resbuf, 4 * wds, inst);
      break;
    default:
      printf("unknown BIB item type %d, skipping %d words\n", type, wds);
      break;
    }
  }
}

#ifdef __BIG_ENDIAN__

static void xlate_bi_i2c_addr(uint8_t*, struct bi_i2c_addr*, int);
static void xlate_bi_signal_gpio(uint8_t*, struct bi_signal_gpio*, int);
static void xlate_bi_signal_i2c(uint8_t*, struct bi_signal_i2c*, int);
static void xlate_bi_signal_reset(uint8_t*, struct bi_signal_reset*, int);
static void xlate_bi_signal_fixed(uint8_t*, struct bi_signal_fixed*, int);
static void xlate_bi_signal_enet_int(uint8_t*, struct bi_signal_enet_int*, int);
static void xlate_bi_signal(uint8_t*, struct bi_signal*, int);
static void xlate_bi_nom_tile_freq(uint32_t*, int, uint8_t*);
static void xlate_bi_board_part_num(uint32_t*, int, uint8_t*);
static void xlate_bi_board_serial_num(uint32_t*, int, uint8_t*);
static void xlate_bi_chip_num(uint32_t*, int, uint8_t*);
static void xlate_bi_max_mem_speed(uint32_t*, int, uint8_t*);
static void xlate_bi_board_rev(uint32_t*, int, uint8_t*);
static void xlate_bi_dimm_map_entry(uint8_t*, struct bi_dimm_map_entry*, int);
static void xlate_bi_dimm_map(uint32_t*, int, uint8_t*);
static void xlate_bi_spd_data(uint32_t*, int, uint8_t*);
static void xlate_bi_dimm_inst(uint8_t*, struct bi_dimm_inst*, int);
static void xlate_bi_dimm_label(uint32_t*, int, uint8_t*);
static void xlate_bi_fan_info(uint8_t*, struct bi_fan_info*, int);
static void xlate_bi_temp_cfg_max6639(uint8_t*, struct bi_temp_cfg_max6639*, int);
static void xlate_bi_temp_cfg_adt7467(uint8_t*, struct bi_temp_cfg_adt7467*, int);
static void xlate_bi_temp_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_board_description(uint32_t*, int, uint8_t*);
static void xlate_bi_fail_led(uint32_t*, int, uint8_t*);
static void xlate_bi_firmware_rev(uint32_t*, int, uint8_t*);
static void xlate_bi_bom_rev(uint32_t*, int, uint8_t*);
static void xlate_bi_rtc_pcf8563(uint8_t*, struct bi_rtc_pcf8563*, int);
static void xlate_bi_rtc_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_aib_i2c(uint8_t*, struct bi_aib_i2c*, int);
static void xlate_bi_aib(uint32_t*, int, uint8_t*);
static void xlate_bi_watch_max6369(uint8_t*, struct bi_watch_max6369*, int);
static void xlate_bi_watch_pcf8563(uint8_t*, struct bi_watch_pcf8563*, int);
static void xlate_bi_watch_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_poweroff(uint32_t*, int, uint8_t*);
static void xlate_bi_i2c_dev_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_i2c_switch_inst(uint8_t*, struct bi_i2c_switch_inst*, int);
static void xlate_bi_i2c_switch(uint32_t*, int, uint8_t*);
static void xlate_bi_misc_reset(uint32_t*, int, uint8_t*);
static void xlate_bi_io_disable(uint32_t*, int, uint8_t*);
static void xlate_bi_cpu_volt_range(uint32_t*, int, uint8_t*);
static void xlate_bi_mem_volt_range(uint32_t*, int, uint8_t*);
static void xlate_bi_clock_inst(uint8_t*, struct bi_clock_inst*, int);
static void xlate_bi_shim_clock(uint32_t*, int, uint8_t*);
static void xlate_bi_enet_led(uint8_t*, struct bi_enet_led*, int);
static void xlate_bi_phy_link_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_port_inst(uint8_t*, struct bi_port_inst*, int);
static void xlate_bi_pcie_id(uint8_t*, struct bi_pcie_id*, int);
static void xlate_bi_pcie_port_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_gpio_pin_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_usb_port_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_sfp_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_console_cfg(uint32_t*, int, uint8_t*);
static void xlate_bi_i2c_arbiter(uint32_t*, int, uint8_t*);
static void xlate_bi_shim_virt_inst(uint32_t*, int, uint8_t*);
static void xlate_bi_cpu_volt_char(uint32_t*, int, uint8_t*);
static void xlate_bi_msh_reg_entry(uint8_t*, struct bi_msh_reg_entry*, int);
static void xlate_bi_msh_reg(uint32_t*, int, uint8_t*);
static void xlate_bi_serdes_lane_char_entry(uint8_t*, struct bi_serdes_lane_char_entry*, int);
static void xlate_bi_serdes_lane_char(uint32_t*, int, uint8_t*);
static void xlate_bi_xaui_refclk(uint32_t*, int, uint8_t*);
static void xlate_bi_gpio_name(uint32_t*, int, uint8_t*);
static void xlate_bi_i2cm_ctl_cfg(uint32_t*, int, uint8_t*);

static void
xlate_bi_i2c_addr(uint8_t* idata, struct bi_i2c_addr* obj, int len)
{
  obj->dev_addr = get_le_bits(idata, 0, 7);
  obj->bus = get_le_bits(idata, 7, 3);
  obj->switch_inst = get_le_bits(idata, 10, 3);
  obj->switch_chan = get_le_bits(idata, 13, 3);
}


static void
xlate_bi_signal_gpio(uint8_t* idata, struct bi_signal_gpio* obj, int len)
{
  obj->pin = get_le_bits(idata, 0, 6);
  obj->bank = get_le_bits(idata, 6, 2);
  obj->open_drain = get_le_bits(idata, 8, 1);
  obj->inverted = get_le_bits(idata, 9, 1);
}


static void
xlate_bi_signal_i2c(uint8_t* idata, struct bi_signal_i2c* obj, int len)
{
  obj->type = get_le_bits(idata, 0, 2);
  obj->pin = get_le_bits(idata, 3, 4);
  obj->inverted = get_le_bits(idata, 7, 1);
  xlate_bi_i2c_addr(idata + 1, &obj->addr, 2);
}


static void
xlate_bi_signal_reset(uint8_t* idata, struct bi_signal_reset* obj, int len)
{
  obj->inverted = get_le_bits(idata, 0, 1);
}


static void
xlate_bi_signal_fixed(uint8_t* idata, struct bi_signal_fixed* obj, int len)
{
  obj->value = get_le_bits(idata, 0, 1);
}


static void
xlate_bi_signal_enet_int(uint8_t* idata, struct bi_signal_enet_int* obj, int len)
{
  obj->pin = get_le_bits(idata, 0, 2);
  obj->inverted = get_le_bits(idata, 3, 1);
}


static void
xlate_bi_signal(uint8_t* idata, struct bi_signal* obj, int len)
{
  obj->type = get_le_bits(idata, 0, 8);
  if (obj->type == 0)
  {
    /* Null element, no translation */
  }
  else if (obj->type == 1)
  {
    xlate_bi_signal_gpio(idata + 1, &obj->u.gpio, 2);
  }
  else if (obj->type == 2)
  {
    xlate_bi_signal_i2c(idata + 1, &obj->u.i2c, 3);
  }
  else if (obj->type == 3)
  {
    xlate_bi_signal_reset(idata + 1, &obj->u.reset, 1);
  }
  else if (obj->type == 4)
  {
    xlate_bi_signal_fixed(idata + 1, &obj->u.fixed, 1);
  }
  else if (obj->type == 5)
  {
    xlate_bi_signal_enet_int(idata + 1, &obj->u.enet_int, 1);
  }
  else
  {
    printf("hv_warning: BIB error: union selector signal.u.type "
           "has invalid value %d\n", obj->type);
  }
}


static void
xlate_bi_nom_tile_freq(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_nom_tile_freq), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_nom_tile_freq) / 4, len / 4)];
  struct bi_nom_tile_freq* obj = (struct bi_nom_tile_freq*) odata;

  obj->clock = get_le_bits(idata, 0, 32);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_board_part_num(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_board_part_num), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_board_part_num) / 4, len / 4)];
  struct bi_board_part_num* obj = (struct bi_board_part_num*) odata;

  memcpy(obj->part_num, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_board_serial_num(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_board_serial_num), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_board_serial_num) / 4, len / 4)];
  struct bi_board_serial_num* obj = (struct bi_board_serial_num*) odata;

  memcpy(obj->serial_num, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_chip_num(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_chip_num), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_chip_num) / 4, len / 4)];
  struct bi_chip_num* obj = (struct bi_chip_num*) odata;

  memcpy(obj->chip_num, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_max_mem_speed(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_max_mem_speed), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_max_mem_speed) / 4, len / 4)];
  struct bi_max_mem_speed* obj = (struct bi_max_mem_speed*) odata;

  for (int i = 0; i < (len) / sizeof (obj->speed[0]); i++)
  {
    obj->speed[i] = get_le_bits(idata, 0 + i * 16, 16);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_board_rev(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_board_rev), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_board_rev) / 4, len / 4)];
  struct bi_board_rev* obj = (struct bi_board_rev*) odata;

  memcpy(obj->board_rev, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_dimm_map_entry(uint8_t* idata, struct bi_dimm_map_entry* obj, int len)
{
  obj->onboard = get_le_bits(idata, 0, 1);
  if (obj->onboard == 0)
  {
    xlate_bi_i2c_addr(idata + 2, &obj->addr.i2c, 2);
  }
  else if (obj->onboard == 1)
  {
    obj->addr.onboard_inst = get_le_bits(idata, 16, 16);
  }
  else
  {
    printf("hv_warning: BIB error: union selector dimm_map_entry.addr.onboard "
           "has invalid value %d\n", obj->onboard);
  }
}


static void
xlate_bi_dimm_map(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_dimm_map), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_dimm_map) / 4, len / 4)];
  struct bi_dimm_map* obj = (struct bi_dimm_map*) odata;

  obj->cs_per_slot = get_le_bits(idata, 0, 3);
  obj->dqs_offset = get_le_bits(idata, 8, 8);
  for (int i = 0; i < (len - 4) / sizeof (obj->map[0]); i++)
  {
    xlate_bi_dimm_map_entry(idata + 4 + i * 4, &obj->map[i], 4);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_spd_data(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_spd_data), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_spd_data) / 4, len / 4)];
  struct bi_spd_data* obj = (struct bi_spd_data*) odata;

  memcpy(obj->spd, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_dimm_inst(uint8_t* idata, struct bi_dimm_inst* obj, int len)
{
  obj->dimm = get_le_bits(idata, 0, 3);
  obj->shim = get_le_bits(idata, 3, 3);
}


static void
xlate_bi_dimm_label(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_dimm_label), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_dimm_label) / 4, len / 4)];
  struct bi_dimm_label* obj = (struct bi_dimm_label*) odata;


  struct bi_dimm_inst inst_struct = { 0 };
  xlate_bi_dimm_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  memcpy(obj->label, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_fan_info(uint8_t* idata, struct bi_fan_info* obj, int len)
{
  obj->max_speed = get_le_bits(idata, 0, 8);
  obj->tach_ppr = get_le_bits(idata, 8, 2);
  obj->four_wire = get_le_bits(idata, 10, 1);
  obj->pwm_act_low = get_le_bits(idata, 11, 1);
  obj->temp_valid = get_le_bits(idata, 15, 1);
}


static void
xlate_bi_temp_cfg_max6639(uint8_t* idata, struct bi_temp_cfg_max6639* obj, int len)
{
  for (int i = 0; i < 2; i++)
  {
    xlate_bi_fan_info(idata + 0 + i * 2, &obj->fans[i], 2);
  }
  for (int i = 0; i < (len - 4) / sizeof (obj->sigs[0]); i++)
  {
    xlate_bi_signal(idata + 4 + i * 4, &obj->sigs[i], 4);
  }
}


static void
xlate_bi_temp_cfg_adt7467(uint8_t* idata, struct bi_temp_cfg_adt7467* obj, int len)
{
  for (int i = 0; i < 4; i++)
  {
    xlate_bi_fan_info(idata + 0 + i * 2, &obj->fans[i], 2);
  }
  for (int i = 0; i < (len - 8) / sizeof (obj->sigs[0]); i++)
  {
    xlate_bi_signal(idata + 8 + i * 4, &obj->sigs[i], 4);
  }
}


static void
xlate_bi_temp_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_temp_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_temp_cfg) / 4, len / 4)];
  struct bi_temp_cfg* obj = (struct bi_temp_cfg*) odata;

  obj->type = get_le_bits(idata, 0, 16);
  xlate_bi_i2c_addr(idata + 2, &obj->addr, 2);
  if (obj->type == 0 || obj->type == 1 || obj->type == 2)
  {
    /* Null element, no translation */
  }
  else if (obj->type == 3)
  {
    xlate_bi_temp_cfg_max6639(idata + 4, &obj->u.max6639, len - 4);
  }
  else if (obj->type == 4)
  {
    xlate_bi_temp_cfg_adt7467(idata + 4, &obj->u.adt7467, len - 4);
  }
  else
  {
    printf("hv_warning: BIB error: union selector temp_cfg.u.type "
           "has invalid value %d\n", obj->type);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_board_description(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_board_description), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_board_description) / 4, len / 4)];
  struct bi_board_description* obj = (struct bi_board_description*) odata;

  memcpy(obj->desc, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_fail_led(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_fail_led), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_fail_led) / 4, len / 4)];
  struct bi_fail_led* obj = (struct bi_fail_led*) odata;

  xlate_bi_signal(idata + 0, &obj->signal, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_firmware_rev(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_firmware_rev), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_firmware_rev) / 4, len / 4)];
  struct bi_firmware_rev* obj = (struct bi_firmware_rev*) odata;

  memcpy(obj->rev, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_bom_rev(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_bom_rev), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_bom_rev) / 4, len / 4)];
  struct bi_bom_rev* obj = (struct bi_bom_rev*) odata;

  memcpy(obj->rev, idata, len);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_rtc_pcf8563(uint8_t* idata, struct bi_rtc_pcf8563* obj, int len)
{
  xlate_bi_i2c_addr(idata + 0, &obj->addr, 2);
}


static void
xlate_bi_rtc_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_rtc_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_rtc_cfg) / 4, len / 4)];
  struct bi_rtc_cfg* obj = (struct bi_rtc_cfg*) odata;

  obj->type = get_le_bits(idata, 0, 8);
  if (obj->type == 1)
  {
    xlate_bi_rtc_pcf8563(idata + 1, &obj->u.pcf8563, 2);
  }
  else
  {
    printf("hv_warning: BIB error: union selector rtc_cfg.u.type "
           "has invalid value %d\n", obj->type);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_aib_i2c(uint8_t* idata, struct bi_aib_i2c* obj, int len)
{
  xlate_bi_i2c_addr(idata + 0, &obj->addr, 2);
}


static void
xlate_bi_aib(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_aib), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_aib) / 4, len / 4)];
  struct bi_aib* obj = (struct bi_aib*) odata;

  obj->type = get_le_bits(idata, 0, 8);
  if (obj->type == 1)
  {
    xlate_bi_aib_i2c(idata + 1, &obj->u.i2c, 2);
  }
  else
  {
    printf("hv_warning: BIB error: union selector aib.u.type "
           "has invalid value %d\n", obj->type);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_watch_max6369(uint8_t* idata, struct bi_watch_max6369* obj, int len)
{
  xlate_bi_i2c_addr(idata + 0, &obj->addr, 2);
  xlate_bi_signal(idata + 2, &obj->set0, 4);
  xlate_bi_signal(idata + 6, &obj->set1, 4);
  xlate_bi_signal(idata + 10, &obj->set2, 4);
  xlate_bi_signal(idata + 14, &obj->wdi, 4);
  xlate_bi_signal(idata + 18, &obj->wdo, 4);
}


static void
xlate_bi_watch_pcf8563(uint8_t* idata, struct bi_watch_pcf8563* obj, int len)
{
  xlate_bi_i2c_addr(idata + 0, &obj->addr, 2);
  xlate_bi_signal(idata + 2, &obj->enable, 4);
}


static void
xlate_bi_watch_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_watch_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_watch_cfg) / 4, len / 4)];
  struct bi_watch_cfg* obj = (struct bi_watch_cfg*) odata;

  obj->type = get_le_bits(idata, 0, 8);
  if (obj->type == 1)
  {
    xlate_bi_watch_max6369(idata + 1, &obj->u.max6369, 22);
  }
  else if (obj->type == 1)
  {
    xlate_bi_watch_pcf8563(idata + 1, &obj->u.pcf8563, 6);
  }
  else
  {
    printf("hv_warning: BIB error: union selector watch_cfg.u.type "
           "has invalid value %d\n", obj->type);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_poweroff(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_poweroff), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_poweroff) / 4, len / 4)];
  struct bi_poweroff* obj = (struct bi_poweroff*) odata;

  xlate_bi_signal(idata + 0, &obj->signal, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_i2c_dev_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_i2c_dev_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_i2c_dev_cfg) / 4, len / 4)];
  struct bi_i2c_dev_cfg* obj = (struct bi_i2c_dev_cfg*) odata;

  xlate_bi_i2c_addr(idata + 0, &obj->addr, 2);
  obj->mem_addr_8bit = get_le_bits(idata, 16, 1);
  obj->mem_addr_0bit = get_le_bits(idata, 17, 1);
  obj->page_size = get_le_bits(idata, 18, 4);
  obj->write_cycle = get_le_bits(idata, 22, 4);
  memcpy(obj->name, idata+ 4, len - 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_i2c_switch_inst(uint8_t* idata, struct bi_i2c_switch_inst* obj, int len)
{
  obj->shim = get_le_bits(idata, 0, 4);
  obj->switch_inst = get_le_bits(idata, 4, 4);
}


static void
xlate_bi_i2c_switch(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_i2c_switch), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_i2c_switch) / 4, len / 4)];
  struct bi_i2c_switch* obj = (struct bi_i2c_switch*) odata;


  struct bi_i2c_switch_inst inst_struct = { 0 };
  xlate_bi_i2c_switch_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  obj->type = get_le_bits(idata, 0, 8);
  obj->dev_addr = get_le_bits(idata, 8, 7);
  obj->conflict_ports = get_le_bits(idata, 15, 8);
  xlate_bi_signal(idata + 4, &obj->reset, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_misc_reset(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_misc_reset), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_misc_reset) / 4, len / 4)];
  struct bi_misc_reset* obj = (struct bi_misc_reset*) odata;

  obj->assert_time = get_le_bits(idata, 0, 32);
  for (int i = 0; i < (len - 4) / sizeof (obj->resets[0]); i++)
  {
    xlate_bi_signal(idata + 4 + i * 4, &obj->resets[i], 4);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_io_disable(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_io_disable), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_io_disable) / 4, len / 4)];
  struct bi_io_disable* obj = (struct bi_io_disable*) odata;

  for (int i = 0; i < (len) / sizeof (obj->disable[0]); i++)
  {
    obj->disable[i] = get_le_bits(idata, 0 + i * 64, 64);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_cpu_volt_range(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_cpu_volt_range), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_cpu_volt_range) / 4, len / 4)];
  struct bi_cpu_volt_range* obj = (struct bi_cpu_volt_range*) odata;

  obj->vmin = get_le_bits(idata, 0, 32);
  obj->vmax = get_le_bits(idata, 32, 32);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_mem_volt_range(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_mem_volt_range), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_mem_volt_range) / 4, len / 4)];
  struct bi_mem_volt_range* obj = (struct bi_mem_volt_range*) odata;

  obj->vmin = get_le_bits(idata, 0, 32);
  obj->vmax = get_le_bits(idata, 32, 32);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_clock_inst(uint8_t* idata, struct bi_clock_inst* obj, int len)
{
  obj->type = get_le_bits(idata, 0, 4);
  obj->shim = get_le_bits(idata, 4, 3);
}


static void
xlate_bi_shim_clock(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_shim_clock), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_shim_clock) / 4, len / 4)];
  struct bi_shim_clock* obj = (struct bi_shim_clock*) odata;


  struct bi_clock_inst inst_struct = { 0 };
  xlate_bi_clock_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  obj->freq = get_le_bits(idata, 0, 32);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_enet_led(uint8_t* idata, struct bi_enet_led* obj, int len)
{
  obj->cfg = get_le_bits(idata, 0, 5);
}


static void
xlate_bi_phy_link_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_phy_link_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_phy_link_cfg) / 4, len / 4)];
  struct bi_phy_link_cfg* obj = (struct bi_phy_link_cfg*) odata;

  obj->speed_10m = get_le_bits(idata, 0, 1);
  obj->speed_100m = get_le_bits(idata, 1, 1);
  obj->speed_1g = get_le_bits(idata, 2, 1);
  obj->speed_10g = get_le_bits(idata, 3, 1);
  obj->speed_20g = get_le_bits(idata, 4, 1);
  obj->speed_25g = get_le_bits(idata, 5, 1);
  obj->speed_50g = get_le_bits(idata, 6, 1);
  obj->sfp_txout_inv = get_le_bits(idata, 10, 1);
  obj->phy_auto_cfg = get_le_bits(idata, 11, 1);
  obj->no_phy = get_le_bits(idata, 12, 1);
  obj->shared_reset = get_le_bits(idata, 13, 1);
  obj->shared_intr = get_le_bits(idata, 14, 1);
  for (int i = 0; i < 6; i++)
  {
    xlate_bi_enet_led(idata + 2 + i * 1, &obj->leds[i], 1);
  }
  obj->mdio_bus_xgbe = get_le_bits(idata, 64, 1);
  obj->mdio_addr = get_le_bits(idata, 65, 5);
  obj->num_mac_addrs = get_le_bits(idata, 72, 5);
  memcpy(obj->mac_addr, idata+ 10, 6);
  obj->lanes = get_le_bits(idata, 128, 5);
  obj->link_name_num = get_le_bits(idata, 136, 6);
  xlate_bi_signal(idata + 20, &obj->reset_sig, 4);
  xlate_bi_signal(idata + 24, &obj->intr_sig, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_port_inst(uint8_t* idata, struct bi_port_inst* obj, int len)
{
  obj->shim = get_le_bits(idata, 0, 3);
  obj->port = get_le_bits(idata, 3, 4);
}


static void
xlate_bi_pcie_id(uint8_t* idata, struct bi_pcie_id* obj, int len)
{
  obj->rev_id = get_le_bits(idata, 0, 8);
  obj->prog_intf = get_le_bits(idata, 8, 8);
  obj->subclass = get_le_bits(idata, 16, 8);
  obj->baseclass = get_le_bits(idata, 24, 8);
  obj->vendor = get_le_bits(idata, 32, 16);
  obj->device = get_le_bits(idata, 48, 16);
  obj->subsys_vendor = get_le_bits(idata, 64, 16);
  obj->subsys_device = get_le_bits(idata, 80, 16);
}


static void
xlate_bi_pcie_port_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_pcie_port_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_pcie_port_cfg) / 4, len / 4)];
  struct bi_pcie_port_cfg* obj = (struct bi_pcie_port_cfg*) odata;


  struct bi_port_inst inst_struct = { 0 };
  xlate_bi_port_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  obj->allow_rc = get_le_bits(idata, 0, 1);
  obj->allow_ep = get_le_bits(idata, 1, 1);
  obj->allow_sio = get_le_bits(idata, 2, 1);
  obj->allow_x1 = get_le_bits(idata, 3, 1);
  obj->allow_x2 = get_le_bits(idata, 4, 1);
  obj->allow_x4 = get_le_bits(idata, 5, 1);
  obj->allow_x8 = get_le_bits(idata, 6, 1);
  obj->override_id = get_le_bits(idata, 7, 1);
  obj->removable = get_le_bits(idata, 8, 1);
  xlate_bi_signal(idata + 4, &obj->perst_sig, 4);
  if (obj->override_id == 0)
  {
    /* Null element, no translation */
  }
  else if (obj->override_id == 1)
  {
    xlate_bi_pcie_id(idata + 8, &obj->u.id, 12);
  }
  else
  {
    printf("hv_warning: BIB error: union selector pcie_port_cfg.u.override_id "
           "has invalid value %d\n", obj->override_id);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_gpio_pin_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_gpio_pin_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_gpio_pin_cfg) / 4, len / 4)];
  struct bi_gpio_pin_cfg* obj = (struct bi_gpio_pin_cfg*) odata;

  obj->input = get_le_bits(idata, 0, 64);
  obj->output = get_le_bits(idata, 64, 64);
  obj->output_od = get_le_bits(idata, 128, 64);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_usb_port_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_usb_port_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_usb_port_cfg) / 4, len / 4)];
  struct bi_usb_port_cfg* obj = (struct bi_usb_port_cfg*) odata;


  struct bi_port_inst inst_struct = { 0 };
  xlate_bi_port_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  obj->allow_device = get_le_bits(idata, 0, 1);
  obj->allow_host = get_le_bits(idata, 1, 1);
  xlate_bi_signal(idata + 4, &obj->phy_reset_sig, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_sfp_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_sfp_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_sfp_cfg) / 4, len / 4)];
  struct bi_sfp_cfg* obj = (struct bi_sfp_cfg*) odata;


  struct bi_port_inst inst_struct = { 0 };
  xlate_bi_port_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  xlate_bi_i2c_addr(idata + 2, &obj->i2c, 2);
  xlate_bi_signal(idata + 4, &obj->rx_los_sig, 4);
  xlate_bi_signal(idata + 8, &obj->tx_fault_sig, 4);
  xlate_bi_signal(idata + 12, &obj->tx_disable_sig, 4);
  xlate_bi_signal(idata + 16, &obj->mod_abs_sig, 4);
  xlate_bi_signal(idata + 20, &obj->link_led_sig, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_console_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_console_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_console_cfg) / 4, len / 4)];
  struct bi_console_cfg* obj = (struct bi_console_cfg*) odata;

  obj->baud_rate = get_le_bits(idata, 0, 32);
  obj->port = get_le_bits(idata, 32, 1);
  obj->parity = get_le_bits(idata, 33, 3);
  obj->data_bits = get_le_bits(idata, 36, 1);
  obj->stop_bits = get_le_bits(idata, 37, 1);
  obj->early_console_delay = get_le_bits(idata, 40, 5);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_i2c_arbiter(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_i2c_arbiter), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_i2c_arbiter) / 4, len / 4)];
  struct bi_i2c_arbiter* obj = (struct bi_i2c_arbiter*) odata;

  obj->type = get_le_bits(idata, 0, 8);
  obj->dev_addr = get_le_bits(idata, 8, 7);
  obj->switch_chan = get_le_bits(idata, 16, 2);
  xlate_bi_signal(idata + 4, &obj->reset, 4);
  xlate_bi_signal(idata + 8, &obj->req, 4);
  xlate_bi_signal(idata + 12, &obj->grant, 4);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_shim_virt_inst(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_shim_virt_inst), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_shim_virt_inst) / 4, len / 4)];
  struct bi_shim_virt_inst* obj = (struct bi_shim_virt_inst*) odata;


  struct bi_clock_inst inst_struct = { 0 };
  xlate_bi_clock_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  obj->virt_inst = get_le_bits(idata, 0, 8);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_cpu_volt_char(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_cpu_volt_char), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_cpu_volt_char) / 4, len / 4)];
  struct bi_cpu_volt_char* obj = (struct bi_cpu_volt_char*) odata;

  obj->load_line = get_le_bits(idata, 0, 1);
  obj->load_line_factor = get_le_bits(idata, 1, 21);
  obj->load_line_offset = get_le_bits(idata, 22, 9);
  obj->reserved = get_le_bits(idata, 63, 1);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_msh_reg_entry(uint8_t* idata, struct bi_msh_reg_entry* obj, int len)
{
  obj->min_speed = get_le_bits(idata, 0, 12);
  obj->min_dimm = get_le_bits(idata, 12, 2);
  obj->min_rank = get_le_bits(idata, 14, 2);
  obj->min_voltage = get_le_bits(idata, 16, 6);
  obj->parameter = get_le_bits(idata, 32, 8);
  obj->value = get_le_bits(idata, 48, 16);
}


static void
xlate_bi_msh_reg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_msh_reg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_msh_reg) / 4, len / 4)];
  struct bi_msh_reg* obj = (struct bi_msh_reg*) odata;

  for (int i = 0; i < (len) / sizeof (obj->entries[0]); i++)
  {
    xlate_bi_msh_reg_entry(idata + 0 + i * 8, &obj->entries[i], 8);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_serdes_lane_char_entry(uint8_t* idata, struct bi_serdes_lane_char_entry* obj, int len)
{
  obj->rx_len = get_le_bits(idata, 0, 12);
  obj->tx_len = get_le_bits(idata, 12, 12);
  obj->reserved = get_le_bits(idata, 24, 8);
}


static void
xlate_bi_serdes_lane_char(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_serdes_lane_char), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_serdes_lane_char) / 4, len / 4)];
  struct bi_serdes_lane_char* obj = (struct bi_serdes_lane_char*) odata;


  struct bi_clock_inst inst_struct = { 0 };
  xlate_bi_clock_inst(instance, &inst_struct, 1);
  *instance = *(bi_inst_t *) &inst_struct;

  for (int i = 0; i < (len) / sizeof (obj->entries[0]); i++)
  {
    xlate_bi_serdes_lane_char_entry(idata + 0 + i * 4, &obj->entries[i], 4);
  }

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_xaui_refclk(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_xaui_refclk), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_xaui_refclk) / 4, len / 4)];
  struct bi_xaui_refclk* obj = (struct bi_xaui_refclk*) odata;

  obj->speed = get_le_bits(idata, 0, 28);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_gpio_name(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_gpio_name), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_gpio_name) / 4, len / 4)];
  struct bi_gpio_name* obj = (struct bi_gpio_name*) odata;

  obj->input = get_le_bits(idata, 0, 64);
  obj->output = get_le_bits(idata, 64, 64);
  obj->output_od = get_le_bits(idata, 128, 64);
  obj->invert = get_le_bits(idata, 192, 64);
  memcpy(obj->name, idata+ 32, len - 32);

  memcpy(ioptr, obj, len);
}


static void
xlate_bi_i2cm_ctl_cfg(uint32_t* ioptr, int len, bi_inst_t* instance)
{
  uint8_t idata[max(sizeof (struct bi_i2cm_ctl_cfg), len)];
  memset(idata, 0, sizeof (idata));
  memcpy(idata, ioptr, len);
  uint32_t odata[max(sizeof (struct bi_i2cm_ctl_cfg) / 4, len / 4)];
  struct bi_i2cm_ctl_cfg* obj = (struct bi_i2cm_ctl_cfg*) odata;

  obj->freq_khz = get_le_bits(idata, 0, 9);
  obj->glitch = get_le_bits(idata, 12, 6);
  obj->elec = get_le_bits(idata, 18, 10);

  memcpy(ioptr, obj, len);
}


/** Translate a BIB from little-endian format to big-endian format.
 * @param blockbuf Block to translate.
 * @param blocklen Length in bytes of the block.
 */
void
bi_buf_to_be(uint32_t* blockbuf, int blocklen)
{
  while (blocklen > 0)
  {
    uint32_t desc = le32_to_cpu(*blockbuf);
    *blockbuf++ = desc;
    blocklen -= 4;

    uint32_t type = BI_TYPE(desc);
    bi_inst_t* inst = ((bi_inst_t*) blockbuf) - 2;
    uint32_t wds = BI_WDS(desc);

    wds = min(wds, blocklen / 4);

    switch(type)
    {
    case BI_TYPE_NOM_TILE_FREQ:
      xlate_bi_nom_tile_freq(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_PART_NUM:
      xlate_bi_board_part_num(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_SERIAL_NUM:
      xlate_bi_board_serial_num(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CHIP_NUM:
      xlate_bi_chip_num(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MAX_MEM_SPEED:
      xlate_bi_max_mem_speed(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_REV:
      xlate_bi_board_rev(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_DIMM_MAP:
      xlate_bi_dimm_map(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SPD_DATA:
      xlate_bi_spd_data(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_DIMM_LABEL:
      xlate_bi_dimm_label(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_TEMP_CFG:
      xlate_bi_temp_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOARD_DESCRIPTION:
      xlate_bi_board_description(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_FAIL_LED:
      xlate_bi_fail_led(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_FIRMWARE_REV:
      xlate_bi_firmware_rev(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_BOM_REV:
      xlate_bi_bom_rev(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_RTC_CFG:
      xlate_bi_rtc_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_AIB:
      xlate_bi_aib(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_WATCH_CFG:
      xlate_bi_watch_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_POWEROFF:
      xlate_bi_poweroff(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_DEV_CFG:
      xlate_bi_i2c_dev_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_SWITCH:
      xlate_bi_i2c_switch(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MISC_RESET:
      xlate_bi_misc_reset(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_IO_DISABLE:
      xlate_bi_io_disable(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CPU_VOLT_RANGE:
      xlate_bi_cpu_volt_range(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MEM_VOLT_RANGE:
      xlate_bi_mem_volt_range(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SHIM_CLOCK:
      xlate_bi_shim_clock(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_PHY_LINK_CFG:
      xlate_bi_phy_link_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_PCIE_PORT_CFG:
      xlate_bi_pcie_port_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_GPIO_PIN_CFG:
      xlate_bi_gpio_pin_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_USB_PORT_CFG:
      xlate_bi_usb_port_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SFP_CFG:
      xlate_bi_sfp_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CONSOLE_CFG:
      xlate_bi_console_cfg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2C_ARBITER:
      xlate_bi_i2c_arbiter(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SHIM_VIRT_INST:
      xlate_bi_shim_virt_inst(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_CPU_VOLT_CHAR:
      xlate_bi_cpu_volt_char(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_MSH_REG:
      xlate_bi_msh_reg(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_SERDES_LANE_CHAR:
      xlate_bi_serdes_lane_char(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_XAUI_REFCLK:
      xlate_bi_xaui_refclk(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_GPIO_NAME:
      xlate_bi_gpio_name(blockbuf, 4 * wds, inst);
      break;
    case BI_TYPE_I2CM_CTL_CFG:
      xlate_bi_i2cm_ctl_cfg(blockbuf, 4 * wds, inst);
      break;
    default:
      printf("unknown BIB item type %d, can't translate, "
             "skipping %d words\n", type, wds);
      break;
    }

    blockbuf += wds;
    blocklen -= 4 * wds;
  }
}

#endif // __BIG_ENDIAN__
