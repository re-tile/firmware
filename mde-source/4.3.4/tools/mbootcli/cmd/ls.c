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
#if CFG_CMD_LS
#include "utils.h"

#include <sys/types.h>


static int usbSet = 0;
static int memSet = 0;
static int hdSet = 0;
static int sdSet = 0;
static int num_of_dev = 0;

static const struct option long_options[] = {
#if CFG_DEV_USB
  {"usb", 0, NULL, '1'},
#endif
#if CFG_DEV_HD
  {"hd", 0, NULL, '2'},
#endif
#if CFG_DEV_MEM
  {"mem", 0, NULL, '3'},
#endif
#if CFG_DEV_SD
  {"sd", 0, NULL, '4'},
#endif
  {NULL, 0, NULL, 0}
};


void
ls_init(cmd_obj_t* cmd_obj)
{
}


int
ls_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0; // Reset it before we call getopt_long_only
  opterr = 0; // prevent getopt() print unexpected messages.

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1234",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
#if CFG_DEV_USB
    case '1':
      usbSet++;
      break;
#endif
#if CFG_DEV_HD
    case '2':
      hdSet++;
      break;
#endif
#if CFG_DEV_MEM
    case '3':
      memSet++;
      break;
#endif
#if CFG_DEV_SD
    case '4':
      sdSet++;
      break;
#endif
    default:
      set_error_info(cmd_obj, optind - 1);
      return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
    }
  }

  if (usbSet + hdSet + sdSet + memSet == 0)
#if CFG_DEV_USB
    usbSet =
#endif
#if CFG_DEV_HD
      hdSet =
#endif
#if CFG_DEV_SD
      sdSet =
#endif
#if CFG_DEV_MEM
      memSet =
#endif
      1;

  if (optind < cmd_obj->argc)
  {
    set_error_info(cmd_obj, optind);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }

  num_of_dev = usbSet + hdSet + sdSet + memSet;
  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
ls_modify(cmd_obj_t* cmd_obj)
{
  int pos = 0;
  int time = num_of_dev;
  cmd_obj_t* cmd;

  while (time)
  {
    if (time == num_of_dev)
    {
      cmd_replace(cmd_obj, pos, "/bin/ls");

      if (hdSet)
      {
#if CFG_DEV_HD
        set_user_data(cmd_obj, "\nThe image files in HD:\n\n");
        add_param(cmd_obj, HD_PATH, NULL);
        hdSet = 0;
#endif
      }
      else if (sdSet)
      {
#if CFG_DEV_SD
        set_user_data(cmd_obj, "\nThe image files in SD:\n\n");
        add_param(cmd_obj, SD_PATH, NULL);
        sdSet = 0;
#endif
      }
      else if (usbSet)
      {
#if CFG_DEV_USB
        usbSet = 0;
        set_user_data(cmd_obj, "\nThe image files in USB:\n\n");
        add_param(cmd_obj, USB_PATH, NULL);
#endif
      }
      else if (memSet)
      {
#if CFG_DEV_MEM
        memSet = 0;
        set_user_data(cmd_obj, "\nThe image files in MEM:\n\n");
        add_param(cmd_obj, MEM_PATH, NULL);
#endif
      }
      param_update(cmd_obj);
    }
    else
    {
      if (hdSet)
      {
#if CFG_DEV_HD
        hdSet = 0;
        cmd = add_cmd(cmd_obj, "/bin/ls", HD_PATH, NULL);
        set_user_data(cmd, "\nThe image files in HD:\n\n");
#endif
      }
      else if (sdSet)
      {
#if CFG_DEV_SD
        sdSet = 0;
        cmd = add_cmd(cmd_obj, "/bin/ls", SD_PATH, NULL);
        set_user_data(cmd, "\nThe image files in SD:\n\n");
#endif
      }
      else if (usbSet)
      {
#if CFG_DEV_USB
        usbSet = 0;
        cmd = add_cmd(cmd_obj, "/bin/ls", USB_PATH, NULL);
        set_user_data(cmd, "\nThe image files in USB:\n\n");
#endif
      }
      else if (memSet)
      {
#if CFG_DEV_MEM
        memSet = 0;
        cmd = add_cmd(cmd_obj, "/bin/ls", MEM_PATH, NULL);
        set_user_data(cmd, "\nThe image files in MEM:\n\n");
#endif
      }
    }
    time--;
  }
  return true;
}


int
ls_execute(cmd_obj_t* cmd_obj)
{
  sys_execute(cmd_obj);

  return 0;
}


int
ls_clean(cmd_obj_t* cmd_obj)
{
  usbSet = 0;
  memSet = 0;
  hdSet = 0;
  sdSet = 0;
  num_of_dev = 0;

  return 0;
}


void
ls_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
ls_help()
{
  char dev_string[64] = {0};

#if CFG_DEV_USB
  strcat(dev_string, "-usb | ");
#endif
#if CFG_DEV_HD
  strcat(dev_string, "-hd | ");
#endif
#if CFG_DEV_SD
  strcat(dev_string, "-sd | ");
#endif
#if CFG_DEV_MEM
  strcat(dev_string, "-mem | ");
#endif
  if (strlen(dev_string) > 0)
    dev_string[strlen(dev_string) - 3] = 0;
  printf("Usage: ls [%s]\n", dev_string);
}

#endif /* CFG_CMD_LS */
