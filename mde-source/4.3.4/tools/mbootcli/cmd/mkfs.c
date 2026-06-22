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
#if CFG_CMD_MKFS
#include "utils.h"

#include <ctype.h>
#include <sys/wait.h>
#include <fcntl.h>


static int typeSet = 0;
static int partSet = 0;
static char* partition = NULL;
static char device[10];
static char fs_type[10];

static const struct option long_options[] = {
  {"t", 1, NULL, '1'},
  {"p", 1, NULL, '2'},
  {NULL, 0, NULL, 0}
};


void
mkfs_init(cmd_obj_t* cmd_obj)
{
}


int
mkfs_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;    // Reset it before we call getopt_long_only
  opterr = 0;    // prevent getopt() print unexpected messages. 

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1:2:",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':       // -t: fs type
      xstrncpy(fs_type, optarg, sizeof(fs_type));

      if (fs_type[0] == '-')
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM_1);

      typeSet += 1;
      break;

    case '2':       // -p: partition name
      partition = strdup(optarg);

      if ((partition == NULL) || (partition[0] == '-'))
        return (cmd_obj->parse_result = ERROR_MISSING_PARAM_2);

      if (map_linux_device(partition, device) != 0)
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
      }

      partSet += 1;
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

  // If no partition name in the command line
  if (!partSet)
    return (cmd_obj->parse_result = ERROR_MISSING_PARAM_2);

  // default file system is of ext2 type.
  if (!typeSet)
    strcpy(fs_type, "ext2");
  else
  {
    if (!((!strcasecmp(fs_type, "ext2")) || (!strcasecmp(fs_type, "ext3"))
          || (!strcasecmp(fs_type, "fat16")) ||
          (!strcasecmp(fs_type, "fat32"))))
    {
      return (cmd_obj->parse_result = ERROR_INVALID_PARAM_1);
    }

    if ((strncmp(device, "/dev/ub", 7) == 0) && (strcmp(fs_type, "ext3") == 0))
    {
      return (cmd_obj->parse_result = ERROR_PARAM_MISMATCH);
    }
  }

  if ((optind < cmd_obj->argc) || (partSet > 1) || (typeSet > 1))
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
mkfs_modify(cmd_obj_t* cmd_obj)
{
  if (strcasecmp(fs_type, "ext2") == 0)
    cmd_replace(cmd_obj, 0, "/sbin/mke2fs");

  if (strcasecmp(fs_type, "ext3") == 0)
  {
    cmd_replace(cmd_obj, 0, "/sbin/mke2fs");
    add_param(cmd_obj, "-j", NULL);
  }

  if (strcasecmp(fs_type, "fat16") == 0)
    cmd_replace(cmd_obj, 0, "/sbin/mkdosfs");

  if (strcasecmp(fs_type, "fat32") == 0)
  {
    cmd_replace(cmd_obj, 0, "/sbin/mkdosfs");
    add_param(cmd_obj, "-F", NULL);
    add_param(cmd_obj, "32", NULL);
  }

  add_param(cmd_obj, device, NULL);

  param_update(cmd_obj);

  return true;
}


int
mkfs_execute(cmd_obj_t* cmd_obj)
{
  int status;

  prt_cmd(cmd_obj);

  printf("Making %s file system on partition %s ... ", fs_type, partition);
  fflush(stdout);

  // hide the stdout and stderr.
  hide_and_restore_12(0);

  status = sys_execute(cmd_obj);

  // hide the stdout and stderr.
  hide_and_restore_12(1);

  // the program executed normally.
  if ((WIFEXITED(status) != 0) && (status == 0))
  {
    printf(" done.\n");
    fflush(stdout);
    return 0;
  }
  else
  {
    printf("\nFailure making file system.\n");
    fflush(stdout);
    return -1;
  }
}

int
mkfs_clean(cmd_obj_t* cmd_obj)
{
  typeSet = 0;
  partSet = 0;
  memset(device, 0, sizeof(device));
  memset(fs_type, 0, sizeof(fs_type));

  free(partition);
  partition = NULL;

  return 0;
}


void
mkfs_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
  switch (cmd_obj->parse_result)
  {
  case ERROR_MISSING_PARAM_1:
    printf("You must specify the desired type of file system.\n");
    break;

  case ERROR_MISSING_PARAM_2:
    printf("You must specify the desired partition name.\n");
    break;

  case ERROR_PARAM_MISMATCH:
    printf("USB mass storage device does not support ext3 file system.\n");
    break;

  case ERROR_INVALID_PARAM_1:
    printf("mkfs can only support ext2,ext3,fat16 and fat32.\n");
    break;

  case ERROR_INVALID_PARAM_2:
    printf("Invalid partition name specified.\n");
    break;

  default:
    break;
  }
}


void
mkfs_help(void)
{
  printf("Usage: mkfs [-t <ext2 | ext3 | fat32 | fat16>] <-p partition>\n");
}

#endif /* CFG_CMD_MKFS */
