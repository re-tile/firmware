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
#if CFG_CMD_DHCP
#include "utils.h"


void
dhcp_init(cmd_obj_t* cmd_obj)
{
}


int
dhcp_parse(cmd_obj_t* cmd_obj)
{
  if (cmd_obj->argc > 3)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  if (cmd_obj->argc < 3)
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

  if (strcasecmp(cmd_obj->argv[2], "yes") &&
      (strcasecmp(cmd_obj->argv[2], "no")))
  {
    set_error_info(cmd_obj, 2);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
dhcp_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
dhcp_execute(cmd_obj_t* cmd_obj)
{
  write_user_param(PARAM_DHCP, cmd_obj->argv[1], cmd_obj->argv[2]);

  start_dhcp();

  return 0;
}


int
dhcp_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
dhcp_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
dhcp_help()
{
  printf("Usage: dhcp <interface> <yes | no>\n");
}

#endif /* CFG_CMD_DHCP */
