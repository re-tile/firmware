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
#if CFG_CMD_SHELL
#include "utils.h"
#include <sys/wait.h>


void
shell_init(cmd_obj_t* cmd_obj)
{
}


int
shell_parse(cmd_obj_t* cmd_obj)
{
  if (cmd_obj->argc != 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
shell_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


int
shell_execute(cmd_obj_t* cmd_obj)
{
  printf("\nType \"exit\" to return to mboot.\n\n");
  int rc = vfork();
  if (rc == 0)
  {
    execl("/bin/sh", "-sh", NULL);
    _exit(1);
  }
  if (rc > 0)
    waitpid(rc, NULL, 0);
  // Re-read the flash config, in case it was changed from the shell.
  read_cfg();
  return 0;
}


int
shell_clean(cmd_obj_t* cmd_obj)
{
  return 0;
}


void
shell_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
shell_help()
{
  printf("Usage: shell\n");
}

#endif /* CFG_CMD_SHELL */
