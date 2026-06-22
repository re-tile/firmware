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
#if CFG_CMD_EXIT
#include "utils.h"


void
exit_init(cmd_obj_t* cmd_obj)
{
}


int
exit_parse(cmd_obj_t* cmd_obj)
{
  if (cmd_obj->argc != 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
exit_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


/** Returns -1, which is caught by do_it() and treated as an EOF,
 * causing the command loop to exit and retry the boot process.
 */
int
exit_execute(cmd_obj_t* cmd_obj)
{
  return -1;
}


int
exit_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
exit_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
exit_help()
{
  printf("Usage: exit\n");
}

#endif /* CFG_CMD_EXIT */
