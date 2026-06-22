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
#if CFG_CMD_HELP
#include "utils.h"


static int cmd_ID = CMD_UNKNOWN;


void
help_init(cmd_obj_t* cmd_obj)
{
}


int
help_parse(cmd_obj_t* cmd_obj)
{
  if (cmd_obj->argc > 2)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  if (cmd_obj->argc == 2)
  {
    cmd_ID = map_cmd(cmd_obj->argv[1]);
    if (cmd_ID == CMD_UNKNOWN)
    {
      set_error_info(cmd_obj, 1);
      return (cmd_obj->parse_result = UNKNOWN_CMD_SPECIFIED);
    }
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
help_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
help_execute(cmd_obj_t* cmd_obj)
{
  describe_command(cmd_ID);
  return 0;
}


int
help_clean(cmd_obj_t* cmd_obj)
{
  cmd_ID = CMD_UNKNOWN;
  return 0;
}


void
help_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
help_help()
{
  printf("Usage: help [command]\n");
}

#endif /* CFG_CMD_HELP */
