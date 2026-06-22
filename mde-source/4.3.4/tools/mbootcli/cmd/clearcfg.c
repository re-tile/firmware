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
#if CFG_CMD_CLEARCFG
#include "utils.h"


#define BOOT_DEVICE (1 << 1)
#define USER_DEVICE (1 << 2)

static int deviceSet = 0;
static int force = 0;


void
clearcfg_init(cmd_obj_t* cmd_obj)
{
}


int
clearcfg_parse(cmd_obj_t* cmd_obj)
{
  int index = 1;
  if (cmd_obj->argc > index && strcmp(cmd_obj->argv[index], "-f") == 0)
  {
    ++index;
    force = 1;
  }
  else
  {
    force = 0;
  }
  if (cmd_obj->argc == index)
  {
    deviceSet |= BOOT_DEVICE;
    deviceSet |= USER_DEVICE;
  }
  else if (cmd_obj->argc == index + 1)
  {
    if (strcasecmp(cmd_obj->argv[index], "boot") == 0)
      deviceSet |= BOOT_DEVICE;
    else if (strcasecmp(cmd_obj->argv[index], "user") == 0)
      deviceSet |= USER_DEVICE;
    else
    {
      set_error_info(cmd_obj, 1);
      return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
    }
  }
  else
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
clearcfg_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
clearcfg_execute(cmd_obj_t* cmd_obj)
{
  const char* mesg = NULL;

  if (!force)
  {
    if ((deviceSet & BOOT_DEVICE) && (deviceSet & USER_DEVICE))
      mesg = "Delete all the BOOT and USER configuration parameters in SROM?";
    else if (deviceSet & BOOT_DEVICE)
      mesg = "Delete all the BOOT configuration parameters in SROM?";
    else if (deviceSet & USER_DEVICE)
      mesg = "Delete all the USER configuration parameters in SROM?";

    char in = my_read_char(mesg);

    if (in == 'n')
      return 0;
  }

  if (deviceSet & BOOT_DEVICE)
    clear_boot();

  if (deviceSet & USER_DEVICE)
  {
    clear_user();
    // We've just erased the network configuration, so stop the
    // network to reflect the config.
    stop_network();
  }

  return 0;
}


int
clearcfg_clean(cmd_obj_t* cmd_obj)
{
  deviceSet = 0;
  return 0;
}


void
clearcfg_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
clearcfg_help()
{
  printf("Usage: clearcfg [boot | user]\n");
}

#endif /* CFG_CMD_CLEARCFG */
