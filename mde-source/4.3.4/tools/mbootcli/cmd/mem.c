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
#if CFG_CMD_MEM
#include "utils.h"


void
mem_init(cmd_obj_t* cmd_obj)
{
}


int
mem_parse(cmd_obj_t* cmd_obj)
{
  if (!no_param(cmd_obj))
  {
    set_error_info(cmd_obj, 1);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
mem_modify(cmd_obj_t* cmd_obj)
{
  int pos = 0;

  cmd_replace(cmd_obj, pos, "/bin/cat");
  add_param(cmd_obj, "/proc/tile/memprof", NULL);

  return true;
}


int
mem_execute(cmd_obj_t* cmd_obj)
{
  sys_execute(cmd_obj);
  return 0;
}


int
mem_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
mem_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
mem_help()
{
  printf("Usage: mem\n");
}

#endif /* CFG_CMD_MEM */
