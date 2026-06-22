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
#if CFG_CMD_PING
#include "utils.h"


void
ping_init(cmd_obj_t* cmd_obj)
{
}


int
ping_parse(cmd_obj_t* cmd_obj)
{
  if (cmd_obj->argc < 2)
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM);
  else if (cmd_obj->argc > 2)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  //
  // Do the simple check here about option.
  // So the real ping application will not complaine about the invalid option. 
  //
  if ('-' == (cmd_obj->argv[1])[0])
  {
    set_error_info(cmd_obj, 1);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
ping_modify(cmd_obj_t* cmd_obj)
{
  int pos = 0;
  cmd_replace(cmd_obj, pos, "/bin/ping");

  add_param(cmd_obj, "-c", NULL);
  add_param(cmd_obj, "4", NULL);

  return true;
}


int
ping_execute(cmd_obj_t* cmd_obj)
{
  start_network();

  sys_execute(cmd_obj);
  return 0;
}


int
ping_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
ping_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
ping_help()
{
  printf("Usage: ping <host>\n");
}

#endif /* CFG_CMD_PING */
