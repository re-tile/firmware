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
#if CFG_CMD_TFTP
#include "utils.h"


static int usbSet = 0;
static int romSet = 0;
static int hdSet = 0;
static int sdSet = 0;
static int memSet = 0;
static char* host = NULL;
static char* remFile = NULL;
static char* locFile = NULL;
static char* locFileWithPath = NULL;

static const struct option long_options[] = {
#if CFG_DEV_USB
  {"usb", 0, NULL, '1'},
#endif
#if CFG_DEV_HD
  {"hd", 0, NULL, '2'},
#endif
  {"rom", 0, NULL, '3'},
#if CFG_DEV_MEM
  {"mem", 0, NULL, '4'},
#endif
#if CFG_DEV_SD
  {"sd", 0, NULL, '5'},
#endif
  {NULL, 0, NULL, 0}
};


void
tftp_init(cmd_obj_t* cmd_obj)
{
}


int
tftp_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;         // Reset it before we call getopt_long_only
  opterr = 0;         // prevent getopt() print unexpected messages.

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
      romSet++;
      break;

    case '4':
      memSet++;
      break;

    case '5':
      sdSet++;
      break;

    default:
      if (valid_option(long_options, cmd_obj->argv[optind - 1]))
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM_FOR_OPTION);
      }
      else
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
      }
    }
  }

  if (usbSet + hdSet + sdSet + romSet + memSet == 0)
    return (cmd_obj->parse_result = ERROR_NEED_DEVICE);

  if (usbSet + hdSet + sdSet + romSet + memSet > 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_DEVICE);

  if (optind + 3 < cmd_obj->argc)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  if (optind + 2 > cmd_obj->argc)
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

  host = cmd_obj->argv[optind];
  remFile = cmd_obj->argv[optind + 1];
  if (optind + 3 == cmd_obj->argc)
  {
    if (romSet)
    {
      set_error_info(cmd_obj, optind + 2);
      return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
    }

    locFile = cmd_obj->argv[optind + 2];
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
tftp_modify(cmd_obj_t* cmd_obj)
{
  cmd_obj_t* cmd;
  char* file;
  char* path;
  char* remFilename;

  cmd_replace(cmd_obj, 0, "/sbin/busybox");

  add_param(cmd_obj, "tftp", NULL);
  add_param(cmd_obj, "-g", NULL);
  host = add_param(cmd_obj, host, NULL);

  add_param(cmd_obj, "-r", NULL);
  remFile = add_param(cmd_obj, remFile, NULL);  // remote file

  add_param(cmd_obj, "-l", NULL);

  if ((remFilename = strrchr(remFile, '/')) != NULL)
    remFilename++;
  else
    remFilename = remFile;

  if (hdSet)
    path = HD_PATH;
  else if (sdSet)
    path = SD_PATH;
  else if (usbSet)
    path = USB_PATH;
  else if (memSet)
    path = MEM_PATH;
  else
    path = ROM_PATH;

  file = locFile ? locFile : remFilename;

  locFileWithPath = add_param(cmd_obj, path, file, NULL);   // local file

  set_err_data(cmd_obj, "Failure when downloading the image via tftp\n");

  // Get rid of old parameter and move the new parameter
  // to the begin of argv array 
  param_update(cmd_obj);

  // FIXME:  This is wrong, it should use sbim.
  if (romSet)
  {
    cmd = add_cmd(cmd_obj, "/bin/dd", NULL, NULL);
    add_param(cmd, "if=", locFileWithPath, NULL);
    add_param(cmd, "of=", DEV_BOOT_IMAGE, NULL);
    set_user_data(cmd, "Updating the bootloader in SPI ROM\n");
    set_err_data(cmd, "Failure when updating the bootloader in SPI ROM\n");
  }

  return true;
}


int
tftp_execute(cmd_obj_t* cmd_obj)
{
  start_network();

  sys_execute(cmd_obj);
  return 0;
}


int
tftp_clean(cmd_obj_t* cmd_obj)
{
  usbSet = 0;
  romSet = 0;
  hdSet = 0;
  sdSet = 0;
  memSet = 0;
  host = NULL;
  remFile = NULL;
  locFile = NULL;
  locFileWithPath = NULL;

  return 0;
}


void
tftp_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
tftp_help()
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
#if CFG_DEV_ROM
  strcat(dev_string, "-rom | ");
#endif
  if (strlen(dev_string) > 0)
    dev_string[strlen(dev_string) - 3] = 0;

  printf("Usage: tftp <host> <remotefile> <%s> "
         "[localfile]\n", dev_string);
}

#endif /* CFG_CMD_TFTP */
