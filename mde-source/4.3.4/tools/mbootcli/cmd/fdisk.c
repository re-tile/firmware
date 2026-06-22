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

// WARNING: This command uses "fdiskmw", not the standard "fdisk".

#include "config.h"
#if CFG_CMD_FDISK
#include "utils.h"

#include <ctype.h>
#include <sys/wait.h>


static bool listSet = false;
static bool sizeSet = false;
static bool activeSet = false;
static bool createSet = false;
static bool deleteSet = false;
static bool usbSet = false;
static bool hdSet = false;
static bool sdSet = false;
static char* start_ptr = NULL;
static char* stop_ptr = NULL;
static char device[MAX_CHAR_PER_PARAM];

static const struct option long_options[] = {
  {"l", 0, NULL, '1'},
  {"s", 1, NULL, '2'},
  {"a", 1, NULL, '3'},
  {"c", 1, NULL, '4'},
  {"d", 1, NULL, '5'},
  {"usb", 0, NULL, '6'},
  {"hd", 0, NULL, '7'},
  {"sd", 0, NULL, '8'},
  {NULL, 0, NULL, 0}
};


void
fdisk_init(cmd_obj_t* cmd_obj)
{
}


static int
parse_create_cmd(char* arg)
{
  char cp;
  unsigned int start = 0, stop = 0;

  if (arg == NULL)
    return ERROR_INVALID_PARAM_2;

  start_ptr = arg;

  cp = *arg++;
  while (isdigit(cp))
  {
    start = start * 10 + cp - '0';
    cp = *arg++;
  }

  if (cp != ':')
    return ERROR_INVALID_PARAM_2;

  // Then start_ptr point to a string refers the start cylinder.
  *(arg - 1) = '\0';

  stop_ptr = arg;

  cp = *arg++;
  while (isdigit(cp))
  {
    stop = stop * 10 + cp - '0';
    cp = *arg++;
  }

  if (cp != '\0')
    return ERROR_INVALID_PARAM_2;

  if ((start == 0) || (stop == 0) || (start >= stop))
    return ERROR_INVALID_PARAM_2;

  return 0;
}


int
fdisk_parse(cmd_obj_t* cmd_obj)
{
  int opt;
  int argc = cmd_obj->argc;

  optind = 0;      //Reset it before we call getopt_long_only
  opterr = 0;

  if (argc < 2)
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "12::3:4:5:67",
         long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':      // -l: list
      if (argc > 2)
      return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

      listSet = true;
      break;

    case '2':      // -s: show partition size
      sizeSet = true;
      break;

    case '3':      // -b: Set active partition.
      activeSet = true;
      break;

    case '4':      // -c: create parition
      if (argc < 4)
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_3);

      if (argc > 4)
        return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

      if (parse_create_cmd(strdup(optarg)) != 0)
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_2);

      createSet = true;
      break;

    case '5':      // -d : delete parition
      deleteSet = true;
      break;

    case '6':      // -usb
      usbSet = true;
      break;

    case '7':      // -hd
      hdSet = true;
      break;

    case '8':      // -sd
      sdSet = true;
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

    if ((sizeSet || deleteSet || activeSet))
    {
      if (argc < 3)
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_1);

      if (argc > 3)
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
      }

      if (map_linux_device(optarg, device))
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_1);
    }
  }

  if ((createSet && (usbSet + hdSet + sdSet) != 1) ||
      (!createSet && (usbSet + hdSet + sdSet) >= 1))
  {
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM_3);
  }

  return 0;
}


bool
fdisk_modify(cmd_obj_t* cmd_obj)
{
  cmd_replace(cmd_obj, 0, "/usr/sbin/fdiskmw");

  if (listSet)
    add_param(cmd_obj, "-l", NULL);

  if (activeSet)
    add_param(cmd_obj, "-a", NULL);

  if (deleteSet)
    add_param(cmd_obj, "-d", NULL);

  if (sizeSet)
    add_param(cmd_obj, "-s", NULL);

  if ((activeSet || deleteSet || sizeSet))
    add_param(cmd_obj, device, NULL);

  if (createSet)
  {
    add_param(cmd_obj, "-c", NULL);

    if (usbSet)
      add_param(cmd_obj, "/dev/sda", NULL);

    if (hdSet)
      add_param(cmd_obj, "/dev/hda", NULL);  

    if (sdSet)
      add_param(cmd_obj, "/dev/sda", NULL);

    add_param(cmd_obj, start_ptr, NULL);
    add_param(cmd_obj, stop_ptr, NULL);
  }

  // Get rid of old parameter and move the new parameter
  // to the begin of argv array 
  param_update(cmd_obj);
  return true;
}


int
fdisk_execute(cmd_obj_t* cmd_obj)
{
  sys_execute(cmd_obj);

  return 0;
}


int
fdisk_clean(cmd_obj_t* cmd_obj)
{
  listSet = false;
  sizeSet = false;
  activeSet = false;
  createSet = false;
  deleteSet = false;
  usbSet = false;
  hdSet = false;
  sdSet = false;
  start_ptr = NULL;
  stop_ptr = NULL;
  memset(device, 0, sizeof(device));

  return 0;
}


void
fdisk_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);

  switch (cmd_obj->parse_result)
  {
  case ERROR_INVALID_PARAM_1:
    printf("Invalid partition name.\n");
    break;

  case ERROR_INVALID_PARAM_2:
    printf("Invalid start or stop cylinder.\n");
    break;

  case ERROR_INVALID_PARAM_3:
    printf("You must specify the desired device, "
     "the start and stop cylinders.\n");
    break;

  default:
    break;
  }
}


void
fdisk_help()
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
  if (strlen(dev_string) > 0)
    dev_string[strlen(dev_string) - 3] = 0;

  printf("Usage: fdisk <-l>\n");
  printf("       fdisk <-a | -d | -s> <partition>\n");
  printf("       fdisk <-c start:stop> <%s>\n", dev_string);
}

#endif /* CFG_CMD_FDISK */
