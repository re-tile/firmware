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
#if CFG_CMD_RM
#include "utils.h"


static int usbSet = 0;
static int memSet = 0;
static int hdSet = 0;
static int sdSet = 0;
static char* fileName = NULL;

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
rm_init(cmd_obj_t* cmd_obj)
{
}


int
rm_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;      // Reset it before we call getopt_long_only
  opterr = 0;      // prevent getopt() print unexpected messages.

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "123",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':
      usbSet++;
      break;

    case '2':
      hdSet++;
      break;

    case '3':
      memSet++;
      break;

    case '4':
      sdSet++;
      break;

    default:
      set_error_info(cmd_obj, optind - 1);
      return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
    }
  }

  if (usbSet + hdSet + sdSet + memSet == 0)
    return (cmd_obj->parse_result = ERROR_NEED_DEVICE);

  if (usbSet + hdSet + sdSet + memSet > 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_DEVICE);

  if (2 == cmd_obj->argc)
  {
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM);
  }
  else if (cmd_obj->argc > 3)
  {
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);
  }

  fileName = cmd_obj->argv[optind];

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
rm_modify(cmd_obj_t* cmd_obj)
{
  char* path;
  int pos = 0;

  cmd_replace(cmd_obj, pos, "/bin/rm");

  if (hdSet)
    path = HD_PATH;
  else if (sdSet)
    path = SD_PATH;
  else if (usbSet)
    path = USB_PATH;
  else
    path = MEM_PATH;

  add_param(cmd_obj, path, fileName, NULL);
  param_update(cmd_obj);

  return true;
}


int
rm_execute(cmd_obj_t* cmd_obj)
{
  sys_execute(cmd_obj);

  return 0;
}


int
rm_clean(cmd_obj_t* cmd_obj)
{
  usbSet = 0;
  memSet = 0;
  hdSet = 0;
  sdSet = 0;
  fileName = NULL;

  return 0;
}


void
rm_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
rm_help(void)
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

  printf("Usage: rm <%s> <filename>\n", dev_string);
}

#endif /* CFG_CMD_RM */
