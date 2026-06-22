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

#include "config.h"
#if CFG_CMD_BOOTPARAM
#include "utils.h"


static bool showSet = false;
static bool cfgSet = false;
static int param_num = 0;
static char bp_ID[4];


void
bootparam_init(cmd_obj_t* cmd_obj)
{
}


int
bootparam_parse(cmd_obj_t* cmd_obj)
{
  int index = 0;

  if (cmd_obj->argc == 1)
  {
    showSet = true;
    return (cmd_obj->parse_result = PARSE_SUCCESS);
  }
  else
  {
    cfgSet = true;

    param_num = cmd_obj->argc - 1;
    index = get_param(cmd_obj, "-dev");
    if (index > 0)
    {
      if ((index + 1) >= cmd_obj->argc)
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

      char* device_name = cmd_obj->argv[index + 1];

      if (
#if CFG_DEV_HD
        (strcasecmp(device_name, "hd") != 0) &&
#endif
#if CFG_DEV_SD
        (strcasecmp(device_name, "sd") != 0) &&
#endif
#if CFG_DEV_USB
        (strcasecmp(device_name, "usb") != 0) &&
#endif
#if CFG_DEV_TFTP
        (strcasecmp(device_name, "tftp") != 0) &&
#endif
#if CFG_DEV_NET
        (strcasecmp(device_name, "net") != 0) &&
#endif
        1)
      {
        set_error_info(cmd_obj, index + 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
      }

      write_boot_param(PARAM_BOOT_DEVICE, NULL, device_name);
      write_boot_flash();
      cmd_obj->argv[index][0] = '\0';
      device_name[0] = '\0';
      param_num -= 2;
    }

    index = get_param(cmd_obj, "-img");
    if (index > 0)
    {
      if ((index + 1) >= cmd_obj->argc)
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

      write_boot_param(PARAM_BOOT_IMG, NULL, cmd_obj->argv[index + 1]);
      write_boot_flash();
      cmd_obj->argv[index][0] = '\0';
      cmd_obj->argv[index + 1][0] = '\0';
      param_num -= 2;
    }

    index = get_param(cmd_obj, "-host");
    if (index > 0)
    {
      if ((index + 1) >= cmd_obj->argc)
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

      write_boot_param(PARAM_BOOT_HOST, NULL, cmd_obj->argv[index + 1]);
      write_boot_flash();
      cmd_obj->argv[index][0] = '\0';
      cmd_obj->argv[index + 1][0] = '\0';
      param_num -= 2;
    }

    index = get_param(cmd_obj, "-initrd");
    if (index > 0)
    {
      if ((index + 1) >= cmd_obj->argc)
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

      write_boot_param(PARAM_BOOT_INITRD, NULL, cmd_obj->argv[index + 1]);
      write_boot_flash();
      cmd_obj->argv[index][0] = '\0';
      cmd_obj->argv[index + 1][0] = '\0';
      param_num -= 2;
    }
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
bootparam_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
bootparam_execute(cmd_obj_t* cmd_obj)
{
  int i;
  char bootarg[MAX_CHAR_PER_PARAM] = {0};
  char parambuf[MAX_CHAR_PER_PARAM];
  int do_update = 0;

  if (showSet == true)
    show_bootparam();

  if (cfgSet == true)
  {
    // clear all the arguments first.
    if (param_num > 0)
    {
      for (i = 1;; i++)
      {
        sprintf(bp_ID, "%d", i);
        if (read_boot_param(PARAM_BOOT_ARGS, bp_ID, parambuf,
	                    sizeof(parambuf)) != 0)
          break;
        clear_param(PARAM_BOOT_ARGS, bp_ID);
      }
    }

    // Concatenate all of the arguments together.
    for (i = 1; i < cmd_obj->argc; i++)
    {
      if (cmd_obj->argv[i][0] != '\0')
      {
        if (strlen(bootarg) + strlen(cmd_obj->argv[i]) + 2 > MAX_CHAR_PER_PARAM)
        {
          print_error(0, "Boot arguments too long, truncated to \"%s\"\n", bootarg);
          break;
        }
        do_update++;
        strcat(bootarg, cmd_obj->argv[i]);
        strcat(bootarg, " ");
      }
    }

    // If there's something to write to flash then write it.
    if (do_update)
    {
      bootarg[strlen(bootarg) - 1] = '\0';  // Zap extra trailing space
      sprintf(bp_ID, "%d", 1);
      write_boot_param(PARAM_BOOT_ARGS, bp_ID, bootarg);
      write_boot_flash();
    }
  }

  return 0;
}


int
bootparam_clean(cmd_obj_t* cmd_obj)
{
  showSet = false;
  cfgSet = false;
  param_num = 0;

  return 0;
}


void
bootparam_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
bootparam_help()
{
  printf("Usage: bootparam [argument_string]\n"
         "       bootparam [-dev device_name] [-img image_name] "
         "[-host ip_address]\n"
         "           device_name = one of:"
#if CFG_DEV_USB
         " usb"
#endif
#if CFG_DEV_HD
         " hd"
#endif
#if CFG_DEV_SD
         " sd"
#endif
#if CFG_DEV_TFTP
         " tftp"
#endif
#if CFG_DEV_NET
         " net"
#endif
         "\n");
}

#endif /* CFG_CMD_BOOTPARAM */
