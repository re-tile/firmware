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
#if CFG_CMD_SHOWCFG
#include "utils.h"


void
showcfg_init(cmd_obj_t* cmd_obj)
{
}


int
showcfg_parse(cmd_obj_t* cmd_obj)
{
  if (!no_param(cmd_obj))
  {
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
showcfg_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
showcfg_execute(cmd_obj_t* cmd_obj)
{
  showcfg();

  return 0;
}


int
showcfg_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
showcfg_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
showcfg_help()
{
  printf("Usage: showcfg\n");
}

#endif /* CFG_CMD_SHOWCFG */
