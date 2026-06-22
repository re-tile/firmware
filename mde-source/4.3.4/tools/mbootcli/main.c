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
#include "utils.h"
#include "parameters.h"

#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>


typedef struct
{
  // Command specific initialization function 
  void (*cmd_init)(cmd_obj_t *);

  // Return 0 if no parse error. Only the syntax is checked here. 
  int (*cmd_parse)(cmd_obj_t *);

  // Do some command line midification when  necessary. Will return false 
  // if error happens. The error here include something like  
  // "file does not exist". Actually this kind of error will be caught 
  // when the command is executed. But we need to try to hide the error 
  // output from other application such as busybox.
  // So we also do some check in modify function. 
  //
  bool (*cmd_modify)(cmd_obj_t *);

  // Command execution 
  int (*cmd_execute)(cmd_obj_t *);

  // Command specific cleaning function 
  int (*cmd_clean)(cmd_obj_t *);

  // Parse error print out 
  void (*cmd_error)(const cmd_obj_t* const);

  // Command specific help function 
  void (*cmd_help)(void);

  char *cmd_name;

  char *cmd_description;

} cmd_func_type;


// Define the actual commands.

#define DEFINE_CMD(N, S, H) \
  { N ## _init, N ## _parse, N ## _modify, N ## _execute, N ## _clean, \
    N ## _error, N ## _help, S, H }

static cmd_func_type commands[] = {
#if CFG_CMD_LS
  DEFINE_CMD(ls, "ls", "List files on boot devices"),
#endif
#if CFG_CMD_RM
  DEFINE_CMD(rm, "rm", "Remove files from a boot device"),
#endif
#if CFG_CMD_PING
  DEFINE_CMD(ping, "ping", "Ping a host"),
#endif
#if CFG_CMD_IFCONFIG
  DEFINE_CMD(ifconfig, "ifconfig", "Configure network interface"),
#endif
#if CFG_CMD_SERIAL
  DEFINE_CMD(serial, "serial", "Setup serial port"),
#endif
#if CFG_CMD_BOOT
  DEFINE_CMD(boot, "boot", "Boot the final image"),
#endif
#if CFG_CMD_REBOOT
  DEFINE_CMD(reboot, "reboot", "Reboot the whole system including bootloader"),
#endif
#if CFG_CMD_FDISK
  DEFINE_CMD(fdisk, "fdisk", "Partition a storage device"),
#endif
#if CFG_CMD_MKFS
  DEFINE_CMD(mkfs, "mkfs", "Format a partition"),
#endif
#if CFG_CMD_TFTP
  DEFINE_CMD(tftp, "tftp", "Get a file via TFTP"),
#endif
#if CFG_CMD_FTP
  DEFINE_CMD(ftp, "ftp", "Get a file via FTP"),
#endif
#if CFG_CMD_MEM
  DEFINE_CMD(mem, "mem", "Show memory information"),
#endif
#if CFG_CMD_HELP
  DEFINE_CMD(help, "help", "Show all commands"),
#endif
#if CFG_CMD_SHOWCFG
  DEFINE_CMD(showcfg, "showcfg", "Show the current configuration"),
#endif
#if CFG_CMD_CLEARCFG
  DEFINE_CMD(clearcfg, "clearcfg", "Clear configuration"),
#endif
#if CFG_CMD_BOOTPARAM
  DEFINE_CMD(bootparam, "bootparam", "Set/display boot parameters"),
#endif
#if CFG_CMD_DHCP
  DEFINE_CMD(dhcp, "dhcp", "Enable/disable DHCP"),
#endif
#if CFG_CMD_ROUTE
  DEFINE_CMD(route, "route", "Set/show routing information"),
#endif
#if CFG_CMD_EXIT
  DEFINE_CMD(exit, "exit", "Exit the CLI and continue booting"),
#endif
#if CFG_CMD_SHELL
  DEFINE_CMD(shell, "shell", "Shell prompt"),
#endif
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};



void
describe_command(int id)
{
  if (id != CMD_UNKNOWN)
  {
    commands[id].cmd_help();
  }
  else
  {
    printf("\n============== COMMANDS SUPPORTED ==============\n");

    for (int i = 0; commands[i].cmd_name != NULL; i++)
    {
      printf("%-10s -- %s\n", commands[i].cmd_name,
             commands[i].cmd_description);
    }
  }
}



static void
initialize_cmd(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return;

  memset(cmd_obj, 0, sizeof(*cmd_obj));

  cmd_obj->cmd_ID = CMD_UNKNOWN;
}


bool
no_param(const cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return false;

  return (cmd_obj->argc == 1) ? true : false;
}


static bool
max_param(const cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return false;

  return (cmd_obj->argc == MAX_PARAM_PER_CMD) ? true : false;
}


/** Check to see if it's an unknown command. 
 */
static bool
unknown_cmd(const cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return false;

  return (cmd_obj->cmd_ID == CMD_UNKNOWN);
}


/** Return the position of param if it exists,
 * or return 0 otherwise 
 */
int
get_param(const cmd_obj_t* const cmd_obj, const char *param)
{
  if (!cmd_obj)
    return 0;

  for (int i = 1; i < cmd_obj->argc; i++)
  {
    if (!strcasecmp(param, cmd_obj->argv[i]))
      return i;
  }

  return 0;
}


/** Replace the parameter in the cmd_obj at the position specified
 *  by pos with the string str. 
 */
void
cmd_replace(cmd_obj_t* cmd_obj, int pos, const char *str)
{
  if (!cmd_obj)
    return;

  free(cmd_obj->argv[pos]);
  cmd_obj->argv[pos] = xstrdup(str);
}


/** Note: We have to use NULL as the last
 *  parameter for the add_param function.  So we know when to stop. 
 */
char *
add_param(cmd_obj_t* cmd_obj, ...)
{
  if (!cmd_obj)
    return NULL;

  char argv[MAX_CHAR_PER_PARAM];
  char *param;
  va_list args;

  bzero(argv, MAX_CHAR_PER_PARAM);

  va_start(args, cmd_obj);

  xstrcat_base(argv, sizeof(argv), args);

  cmd_obj->argv[cmd_obj->argc++] = param = xstrdup(argv);

  va_end(args);

  return param;
}


/** Note: We have to use NULL as the last  parameter for the add_cmd function.
 * So we know when to stop.  The new command object is create internally.  So
 * we don't need to do any parse and modify. All the parameter should be on
 * the right to run format. So sys_execute will be called recursively to 
 * execute all the command. 
 */
cmd_obj_t*
add_cmd(cmd_obj_t* cmd_obj, ...)
{
  if (!cmd_obj)
    return NULL;

  cmd_obj_t* cmd = (cmd_obj_t*) xmalloc(sizeof (cmd_obj_t));
  char *s;

  va_list args;
  va_start(args, cmd_obj);

  while (1)
  {
    s = va_arg(args, char *);

    if (!s)
      break;

    cmd->argv[cmd->argc++] = xstrdup(s);
  }

  // Append the new command to the end of object list
  while (cmd_obj->next_cmd)
    cmd_obj = cmd_obj->next_cmd;
  cmd_obj->next_cmd = cmd;

  cmd->saved_argc = cmd->argc;
  return cmd;
}

cmd_obj_t*
prepend_cmd(cmd_obj_t* cmd_obj, ...)
{
  if (!cmd_obj)
    return NULL;

  cmd_obj_t* cmd = (cmd_obj_t*) xmalloc(sizeof (cmd_obj_t));
  char *s;

  bcopy((void *)cmd_obj, (void *)cmd, sizeof(cmd_obj_t));
  bzero((void *)cmd_obj, sizeof(cmd_obj_t));

  va_list args;
  va_start(args, cmd_obj);

  while (1)
  {
    s = va_arg(args, char *);

    if (!s)
      break;

    cmd_obj->argv[cmd_obj->argc++] = xstrdup(s);
  }

  // Append the new command to the end of object list
  cmd_obj->next_cmd = cmd;

  cmd_obj->saved_argc = cmd_obj->argc;
  return cmd;
}


/** Remove the parameter at the position specified
 *  by pos from cmd_obj. 
 */
static void
rm_param(cmd_obj_t* cmd_obj, int pos)
{
  if (!cmd_obj)
    return;

  free(cmd_obj->argv[pos]);
  cmd_obj->argv[pos] = NULL;
}


/** Get rid of all the old param and
 *  move the new param to the head of argv array 
 */
void
param_update(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return;

  // Remove all the old param.
  for (int i = 1; i < cmd_obj->saved_argc; i++)
    rm_param(cmd_obj, i);

  memmove(&cmd_obj->argv[1], &cmd_obj->argv[cmd_obj->saved_argc],
          sizeof (char *) * (cmd_obj->argc - cmd_obj->saved_argc));

  cmd_obj->argc = cmd_obj->argc - cmd_obj->saved_argc + 1;
  cmd_obj->saved_argc = cmd_obj->argc;
}


/** Clean all the parameters in cmd_obj. 
 */
static void
clean_param(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return;

  for (; cmd_obj->argc > 0; cmd_obj->argc--)
    rm_param(cmd_obj, cmd_obj->argc - 1);

  cmd_obj->argc = 0;
  cmd_obj->saved_argc = 0;
  cmd_obj->cmd_ID = CMD_UNKNOWN;
  cmd_obj->parse_result = 0;

  free(cmd_obj->user_data);
  cmd_obj->user_data = NULL;

  free(cmd_obj->err_info);
  cmd_obj->err_info = NULL;
}


/** Clean the cmd object list. 
 */
static void
clean_cmd(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return;

  cmd_obj_t* tmpPtr = cmd_obj->next_cmd;
  cmd_obj_t* tmpNextPtr;

  clean_param(cmd_obj);

  // Reset the saved command line
  free(cmd_obj->cmdLine);

  // Clean all the intenerly added cmd object
  while (tmpPtr)
  {
    tmpNextPtr = tmpPtr->next_cmd;
    clean_param(tmpPtr);
    free(tmpPtr);
    tmpPtr = tmpNextPtr;
  }

  cmd_obj->next_cmd = NULL;
}


/** Get the command ID from the command string. 
 */
int
map_cmd(const char *const str)
{
  for (int i = 0; commands[i].cmd_name != NULL; i++)
  {
    if (!strcmp(commands[i].cmd_name, str))
      return i;
  }

  return CMD_UNKNOWN;
}



pErrorInfoType pErrInfo[] = {
  { PARSE_SUCCESS, "Parse is successful" },
  { ERROR_MISSING_PARAM, "Missing parameter(s)." },
  { ERROR_TOOMANY_PARAM, "Too many parameter(s)." },
  { ERROR_INVALID_PARAM, "Invalid parameter" },
  { ERROR_NEED_DEVICE, "You must specify a device" },
  { ERROR_TOOMANY_DEVICE, "Too many devices have been specified" },
  { UNKNOWN_CMD_SPECIFIED, "Unknown command" },
  { ERROR_INVALID_ACTION, "Invalid action" },
  { ERROR_NEED_GW, "You must specify a gateway" },
  { ERROR_ALREADY_HAS_DEFAULT_ROUTE, "Default route already exists" },
  { ERROR_NEED_IMGFILE, "You must specify an image file" },
  { ERROR_ROUTE_TYPE_NOT_SUPPORT, "Only default route is supported" },
  { ERROR_MISSING_PARAM_FOR_OPTION, "Missing parameter(s) for option" }
};


/** General parse error printout. The specific command
 *  can has its own parse error printout.
 */
void
prt_parseError(const cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return;

  // Nothing to do for success.
  // Caller will handle customized codes.
  if (cmd_obj->parse_result <= PARSE_SUCCESS ||
      cmd_obj->parse_result >= ERROR_CUSTOMIZED)
    return;

  if (cmd_obj->err_info)
    print_error(0, "%s: %s\n", pErrInfo[cmd_obj->parse_result].errorInfo,
                cmd_obj->err_info);
  else
    print_error(0, "%s\n", pErrInfo[cmd_obj->parse_result].errorInfo);
}


/** Handle unknown command here. 
 */
void
prt_nfound(const cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return;

  printf("Unknown command: %s\n\n", cmd_obj->argv[0]);
}


/** Print out the command that will be executed.
 *  This is for debug only. 
 */
void
prt_cmd(const cmd_obj_t* const cmd_obj)
{
#if 0
  if (!cmd_obj)
    return;

  int i;
  printf("Command: ");
  for (i = 0; i < cmd_obj->argc; i++)
    printf("\"%s\" ", cmd_obj->argv[i]);

  printf("\n");
  fflush(stdout);
#endif
}



/** Remove all kind of space from both ends of line. Then move the real
 *  content to the begin of the line. Return false if it is empty line.
 */
static bool
rm_space(char *line)
{
  char *s = line;

  while (isspace(*s))
    s++;

  if (*s != 0)
  {
    char *t = s + strlen(s) - 1;
    while (t > s && isspace(*t))
      t--;
    *++t = '\0';

    memmove(line, s, strlen(s) + 1);
    return true;
  }

  return false;
}


/** Assign the argc and argv. 
 */
static void
get_arg(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return;

  int wstart = 0;               // wstart -- the start place of a word
  int wend = 0;                 // wend -- the 1st space after a word 
  int i = 0;
  int j = 0;
  int index = 0;

  char *argv;
  char *str = cmd_obj->cmdLine;
  int numOfChar = strlen(str);

  for (i = 0; i < numOfChar;)
  {
    if (isspace(str[i]))
    {
      ++i;
      continue;
    }

    // Here is the 1st non-space character
    wstart = i;

    for (j = i + 1; j < numOfChar; j++)
    {
      if (isspace(str[j]))
        break;
    }

    if (max_param(cmd_obj))
    {
      clean_param(cmd_obj);
      printf("Exceeding the maximum number of parameter: %d",
             MAX_PARAM_PER_CMD);
      continue;
    }

    // Set the starting point for searching the next param
    i = j;

    wend = j;

    argv = xmalloc(sizeof (char) * (wend - wstart) + 1);

    xstrncpy(argv, &str[wstart], (wend - wstart) + 1);

    cmd_obj->argv[index++] = argv;
    cmd_obj->argc = index;
    cmd_obj->saved_argc = cmd_obj->argc;
  }
}


/** Fill in the command obj info.
 * Returns 0 for valid command, 1 for invalid command, -1 on EOF.
 */
static int
get_cmdInfo(cmd_obj_t* cmd_obj)
{
  if (!cmd_obj)
    return 1;

  initialize_cmd(cmd_obj);

  size_t len = 0;
  char *line = NULL;

  // Read a line.
  printf("%s", PROMPT);
  fflush(stdout);
  if (getline(&line, &len, stdin) == -1)
    return -1;  // EOF

  cmd_obj->cmdLine = line;

  // Strip whitespace, and return 1 if empty.
  if (!rm_space(cmd_obj->cmdLine))
    return 1;

  get_arg(cmd_obj);

  cmd_obj->cmd_ID = map_cmd(cmd_obj->argv[0]);

  // Handle unknown commands.
  if (unknown_cmd(cmd_obj))
  {
    prt_nfound(cmd_obj);
    clean_cmd(cmd_obj);
    return 1;
  }

  // Valid command
  return 0;
}


static int
get_cmd_from_argv(cmd_obj_t* cmd_obj, int argc, char** argv)
{
  if (!cmd_obj)
    return 1;

  if (argc >= MAX_PARAM_PER_CMD)
    return 1;

  // Copy arguments, and count total characters as we go.
  // We strdup() them since the model here is that they are passed to
  // free later.
  int cmdlen = 0;
  for (int i = 0; i < argc; ++i)
  {
    cmd_obj->argv[i] = strdup(argv[i]);
    cmdlen += strlen(argv[i]) + 1;   /* separator space, or NUL at end */
  }
  cmd_obj->argv[argc] = NULL;
  cmd_obj->argc = cmd_obj->saved_argc = argc;

  // Construct a concatenated copy of the command line in case
  // some part of the system wants to print it out.  No need to
  // worry too hard about quoting, since we won't re-parse it.
  cmd_obj->cmdLine = malloc(cmdlen);
  cmd_obj->cmdLine[0] = '\0';
  for (int i = 0; i < argc; ++i) {
    if (i)
      strcat(cmd_obj->cmdLine, " ");
    strcat(cmd_obj->cmdLine, argv[i]);
  }
  if (!rm_space(cmd_obj->cmdLine))
    return 1;

  // Look up the command.
  cmd_obj->cmd_ID = map_cmd(cmd_obj->argv[0]);
  if (unknown_cmd(cmd_obj))
  {
    prt_nfound(cmd_obj);
    clean_cmd(cmd_obj);
    return 1;
  }

  // Valid command
  return 0;
}  


/** Set user_data parameter of cmd_obj. 
 */
void
set_user_data(cmd_obj_t* cmd_obj, char *data)
{
  if (!cmd_obj)
    return;

  free(cmd_obj->user_data);
  cmd_obj->user_data = xstrdup(data);
}


/** Set err_info parameter of cmd_obj. 
 */
void
set_err_data(cmd_obj_t* cmd_obj, char *data)
{
  if (!cmd_obj)
    return;

  free(cmd_obj->err_info);
  cmd_obj->err_info = xstrdup(data);
}


void
set_error_info(cmd_obj_t* cmd_obj, int index)
{
  if ((!cmd_obj) || (cmd_obj->argc <= index))
    return;

  set_err_data(cmd_obj, cmd_obj->argv[index]);
}


/** This function will handle all the valid commands.
 * The parameter could be wrong though. 
 * Return the value of the execute method.
 */
static int
do_it(cmd_obj_t* cmd_obj)
{
  int retval = 0;

  if (!cmd_obj)
    return 0;

  cmd_func_type * cmd_func = &commands[cmd_obj->cmd_ID];

  // Command specific initial function.
  cmd_func->cmd_init(cmd_obj);

  if (cmd_func->cmd_parse(cmd_obj) == 0)
  {
    // Successful parse, so execute.
    if (cmd_func->cmd_modify(cmd_obj))
      retval = cmd_func->cmd_execute(cmd_obj);
  }
  else
  {
    // Wrong parameter.
    cmd_func->cmd_error(cmd_obj);
    cmd_func->cmd_help();
  }

  // Clean up.
  cmd_func->cmd_clean(cmd_obj);
  clean_cmd(cmd_obj);

  return retval;
}


/** Boot the final image.
 */
void
boot_img(void)
{
  char *boot_dev;
  char dev[MAX_CHAR_PER_DEV_NAME];

  if (read_boot_param(PARAM_BOOT_DEVICE, NULL, dev, sizeof(dev)))
  {
    xstrncpy(dev, DEF_DEV, sizeof(dev));
  }

  if (!strncasecmp(dev, "hd", MAX_CHAR_PER_DEV_NAME))
    boot_dev = "hd";
  if (!strncasecmp(dev, "sd", MAX_CHAR_PER_DEV_NAME))
    boot_dev = "sd";
  else if (!strncasecmp(dev, "usb", MAX_CHAR_PER_DEV_NAME))
    boot_dev = "usb";
  else if (!strncasecmp(dev, "tftp", MAX_CHAR_PER_DEV_NAME))
    boot_dev = "tftp";
  else if (!strncasecmp(dev, "net", MAX_CHAR_PER_DEV_NAME))
    boot_dev = "net";

  print_error(0, "Booting from %s\n", boot_dev);

  // Use the boot command to do the default booting.
  // So hand-make a message and then call the functions
  // just like user enter a boot command from the CLI. 
  char *cmd_line = xmalloc(32);
  xstrcat(cmd_line, 32, "boot", " -", boot_dev, NULL);
  cmd_obj_t cmd_obj_body;
  cmd_obj_t* cmd_obj = &cmd_obj_body;
  initialize_cmd(cmd_obj);
  cmd_obj->cmdLine = cmd_line;
  get_arg(cmd_obj);
  cmd_obj->cmd_ID = map_cmd(cmd_obj->argv[0]);
  do_it(cmd_obj);
}


/** Call other program such as busybox to do the job for us.
 */
int
sys_execute(cmd_obj_t* const cmd_obj)
{
  if (!cmd_obj)
    return -1;

  prt_cmd(cmd_obj);

  if (cmd_obj->user_data)
    printf("%s", cmd_obj->user_data);

  // Terminate.
  cmd_obj->argv[cmd_obj->argc] = NULL;

  pid_t pid = fork();
  if (pid < 0)
    print_error(1, "%s", "fork failed");

  if (pid == 0)
  {
    execv(cmd_obj->argv[0], cmd_obj->argv);
    exit(1);
  }

  int status = 1;
  waitpid(pid, &status, 0);

  // If we have other internally added cmd object
  // on the list, execute them. 
  if ((status != 0) && (cmd_obj->err_info))
    print_error(0, "%s", cmd_obj->err_info);

  if ((status == 0) && (cmd_obj->next_cmd))
    sys_execute(cmd_obj->next_cmd);

  return status;
}


/** Get default value when the SPI ROM does not have corresponding value. 
 */
void *
get_default(const char* const variable, char* param, size_t size)
{
  const char * from;
  if (!strcmp(variable, "image"))
    from = DEF_IMG;
  else if (!strcmp(variable, "dhcp"))
    from = DEF_DHCP;
  else if (!strcmp(variable, "dev"))
    from = DEF_DEV;
  else if (!strcmp(variable, "bootparam"))
    from = DEF_BOOT_PARAM;
  else
    return NULL;

  xstrncpy(param, from, size);

  return param;
}


#if 0
/** Signal handler routine. 
 */
static void
sig_routine(int sig)
{
  // Just ignore signals for now
  return;
}
#endif


/** Signal initialization. 
 */
static void
sig_init(void)
{
  // No keyboard signals are delivered from the console, where mbootcli
  // usually runs, so handlers for SIGINT, SIGQUIT, and SIGTSTP are
  // unnecessary.  And in addition, since users may occasionally want to
  // run mbootcli via telnet on a live system to update the bootparams,
  // it's convenient to be able to ^C out when they're done.  Furthermore,
  // if run over a telnet session that hangs up, it's good if SIGHUP kills
  // the program instead of putting it into a cpu-burning loop reading
  // EOF out of stdin.  And there's no excuse for ignoring SIGTERM.
#if 0
  signal(SIGINT, sig_routine);
  signal(SIGQUIT, sig_routine);
  signal(SIGHUP, sig_routine);
  signal(SIGTERM, sig_routine);
  signal(SIGTSTP, sig_routine);
#endif
}

void
print_mboot_help()
{
      printf("mboot usage:\n");
      printf("  -h                : print this help\n");
      printf("  -b                : boot (can interrupt into the interactive mode)\n");
      printf("  -c cmd cmd_args   : execute a command line and exit\n");
      printf("  No argument       : enter the mboot interactive mode\n");
      printf("  Run help in the interactive mode for more sub-command info.\n");
}

static int enable_network_flag = 1;

bool
enable_network(void)
{
  return enable_network_flag;
}

int
main(int argc, char **argv)
{
  int opt;
  int boot_mode = 0;
  int run_cmd_only = 0;

  // Set signal handler
  sig_init();

  while ((opt = getopt(argc, argv, "+hcb")) != -1)
  {
    switch (opt)
    {
    case 'h':
      print_mboot_help();
      exit(0);

    case 'c':
      run_cmd_only = 1;
      break;

    case 'b':
      boot_mode = 1;
      break;

    case '?':
      print_mboot_help();
      exit(1);
    }
  }

  enable_network_flag = boot_mode;

  // If without options, enter the mboot interactive mode directly.
  if (argc == 1)
  {
      int retval = 0;

      read_cfg();

      // Start the network for the interactive session.
      start_network();
      while (retval != -1)
      {
        cmd_obj_t cmd_obj;
        // Execute commands, until get_cmdInfo() returns -1,
        // indicating EOF, or do_it() returns -1 (returned from a
        // command's _execute method), indicating that we should retry
        // the boot process.
        if ((retval = get_cmdInfo(&cmd_obj)) == 0)
          retval = do_it(&cmd_obj);
      }

      exit(1);
  } 

  // Just execute a mboot sub-command.
  if (run_cmd_only == 1)
  {
      // If passed "-c cmd arg ...", run a single command and exit.
      if (optind < argc)
      {
        cmd_obj_t cmd_obj;
        initialize_cmd(&cmd_obj);
        if (get_cmd_from_argv(&cmd_obj, argc - optind, argv + optind) != 0)
          exit(1);
        read_cfg();
        exit(do_it(&cmd_obj));
      }
      else
        print_error(1, "No command and argument for the -c option.\n");
  }

  // Enter the count down boot interface.
  if (boot_mode != 1)
    exit(0);

  while (true)
  {
    // Get configuration from SPI ROM.  The config will be re-read
    // every time through this loop, in case the user changed
    // something in interactive mode.
    read_cfg();

    // If the countdown timeout is zero then we don't prompt for an
    // interactive session.
    if (COUNT_DOWN_SECS && !count_down(COUNT_DOWN_SECS))
    {
      int retval = 0;

      // Start the network for the interactive session.
      start_network();
      while (retval != -1)
      {
        cmd_obj_t cmd_obj;
        // Execute commands, until get_cmdInfo() returns -1,
        // indicating EOF, or do_it() returns -1 (returned from a
        // command's _execute method), indicating that we should retry
        // the boot process.
        if ((retval = get_cmdInfo(&cmd_obj)) == 0)
          retval = do_it(&cmd_obj);
      }
      continue;
    }

    // Boot default image.
    boot_img();

    // If we got here then the boot failed, so stop the network.
    //
    // This will make sure that we always restart the network before
    // we try to use it, guaranteeing a fresh configuration.  The
    // concern is that, if the boot fails, and we retry, we always
    // want to retry the network configuration in case it's changed
    // (I.E. if the DHCP server has changed, and might now allow us to
    // boot).
    stop_network();
    print_error(0, "Booting failed.\n");
  }

  // Never reached.
  return 0;
}
