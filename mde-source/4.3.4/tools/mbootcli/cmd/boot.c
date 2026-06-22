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

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "config.h"
#if CFG_CMD_BOOT
#include "utils.h"


static int usbSet = 0;
static int memSet = 0;
static int hdSet = 0;
static int sdSet = 0;
static int tftpSet = 0;
static int netSet = 0;
static int bootSet = 0;

// Following are variables for boot ways other than net boot.
static char* imgFile = NULL;
static char* imgFilename = NULL;
static char* bootHost = NULL;

static char* initrdFile= NULL;
static char* initrdFilename= NULL;

// Following are variables for net boot.
// Note net boot will supress all local boot params during booting.

// macaddr entry for default net boot.
#define MACADDR_STR_FOR_DEFAULT "default"

// valid ethernet mac address will be stored as
// mac0, ..., mac5, 0, 0 in a int64_t variable,
// so following values will never be same as valid mac addresses.
#define MACADDR_ERROR_TILE_INTF    -1
#define MACADDR_ERROR_BOOTINFO     -2
#define MACADDR_FOR_DEFAULT        -3

static char netboot_rem_bootinfo_file[MAX_CHAR_PER_FILENAME];
static char netboot_rem_img[MAX_CHAR_PER_FILENAME];
static char netboot_rem_initrd[MAX_CHAR_PER_FILENAME];
static char netboot_rem_script[MAX_CHAR_PER_FILENAME];
static char netboot_host[MAX_CHAR_PER_PARAM];
static int netboot_continue = -1; //-1: unset, 0: stop, 1: continue
static char netboot_local_img[MAX_CHAR_PER_FILENAME];
static char netboot_local_initrd[MAX_CHAR_PER_FILENAME];
static char netboot_boot_args[MAX_CHAR_PER_PARAM];

static const struct option long_options[] = {

#if CFG_DEV_USB
  {"usb", 0, NULL, '1'},
#endif
#if CFG_DEV_HD
  {"hd", 0, NULL, '2'},
#endif
#if CFG_DEV_MEM
  {"mem", 0, NULL, '3'},
#endif
  {"img", 1, NULL, '4'},
#if CFG_DEV_TFTP
  {"tftp", 0, NULL, '5'},
#endif
  {"host", 1, NULL, '6'},
#if CFG_DEV_SD
  {"sd", 0, NULL, '7'},
#endif
#if CFG_DEV_NET
  {"net", 0, NULL, '8'},
#endif
  {"initrd", 1, NULL, '9'},
  {NULL, 0, NULL, 0}
};


void
boot_init(cmd_obj_t* cmd_Obj)
{
}


int
boot_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;  // Reset it before we call getopt_long_only
  opterr = 0;  // prevent getopt() print unexpected messages.

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1234:56:789:",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':
      usbSet = 1;
      break;

    case '2':
      hdSet = 1;
      break;

    case '3':
      memSet = 1;
      break;

    case '4':
      imgFile = strdup(optarg);
      break;

    case '5':
      tftpSet = 1;
      break;

    case '6':
      bootHost = strdup(optarg);
      break;

    case '7':
      sdSet = 1;
      break;

    case '8':
      netSet = 1;
      break;

    case '9':
      initrdFile = strdup(optarg);
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

  if (cmd_obj->argc == 1)
    bootSet = 1;

  if (usbSet + hdSet + sdSet + memSet + tftpSet + netSet > 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_DEVICE);

  if (optind < cmd_obj->argc)
  {
    set_error_info(cmd_obj, optind);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }
  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


/** Return the mac address of a tile ethernet interface.
 */
int64_t
intf_macaddr(char* intf)
{
  if (!strcmp(intf, MACADDR_STR_FOR_DEFAULT))
  {
      return MACADDR_FOR_DEFAULT;
  }
  else
  {
    int fd;
    struct ifreq ir;
    int64_t val = 0;
    char* c_val = (char*) &val;

    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
      return MACADDR_ERROR_TILE_INTF;

    memset(&ir, 0, sizeof(ir));
    strcpy(ir.ifr_name, intf);
    if (ioctl(fd, SIOCGIFHWADDR, &ir))
    {
      close(fd);
      return MACADDR_ERROR_TILE_INTF;
    }

    close(fd);

    for (int i = 0; i < 6; i++)
      c_val[i] = ir.ifr_hwaddr.sa_data[i];

    return val;
  }
}


/** Return an ethernet mac address in the bootinfo file.
 */
int64_t
bootinfo_macaddr(char* macaddr)
{
  if (macaddr == NULL)
    return MACADDR_ERROR_BOOTINFO;

  if (!strcmp(macaddr, MACADDR_STR_FOR_DEFAULT))
  {
    return MACADDR_FOR_DEFAULT;
  }
  else
  {
    int64_t val = 0;
    char* c_val = (char*) &val;
    int nbytes;

    nbytes = sscanf(macaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    &c_val[0], &c_val[1], &c_val[2],
                    &c_val[3], &c_val[4], &c_val[5]);
    if (nbytes != 6)
      return MACADDR_ERROR_BOOTINFO;
    return val;
  }
}


/** Read tftp server address and boot info file name.
 * These info come from DHCP.
 *
 */
int
netboot_read_info()
{
  FILE* fp;
  ssize_t retval;
  size_t len = 0;
  char *line = NULL;

  netboot_rem_bootinfo_file[0] = 0;
  netboot_host[0] = 0;

  fp = fopen(NETBOOT_INFOFILE, "r");
  if (fp == NULL)
    return -1;
 
  retval = getline(&line, &len, fp);
  while (retval != -1)
  {
    char* token = strtok(line, " \t\n");

    if (token && !strcmp(token, "bootinfo"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(netboot_rem_bootinfo_file, token, MAX_CHAR_PER_FILENAME);
    }
    else if (token && !strcmp(token, "host"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(netboot_host, token, ADDRESS_LEN);
    }

    retval = getline(&line, &len, fp);
  }

  if (line)
    free(line);
  fclose(fp);

  return 0;
}


/** Read boot info for a specified interface and apply if OK.
 * Will search in the netboot_rem_bootinfo_file.
 * Return 0 after success and non-zero when failure occurs.
 */
int
intf_read_bootinfo(char *bootinfo_file, char *intf)
{
  int64_t macaddr_intf, macaddr_tmp;
  ssize_t retval;
  size_t len = 0;
  FILE *fp;
  char *line = NULL;
  char macaddr[MAX_CHAR_PER_PARAM];
  int macaddr_matched;

  netboot_rem_img[0] = 0;
  netboot_rem_initrd[0] = 0;
  netboot_boot_args[0] = 0;
  netboot_rem_script[0] = 0;
 
  macaddr_intf = intf_macaddr(intf);
  if (macaddr_intf == MACADDR_ERROR_BOOTINFO)
    return -1;

  fp = fopen(bootinfo_file, "r");
  if (fp == NULL)
    return -1;

  macaddr_matched = 0;
  retval = getline(&line, &len, fp);
  while (retval != -1)
  {
    char* token = strtok(line, " \t\n");
    if (token && !strcmp(token, "macaddr"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(macaddr, token, MAX_CHAR_PER_PARAM);

      macaddr_tmp = bootinfo_macaddr(macaddr);
      macaddr_matched = (macaddr_tmp == macaddr_intf);
    }

    if (macaddr_matched)
      break;

    // This line does not belong to the interface.
    // Just simply igonore the line.
    retval = getline(&line, &len, fp);
  }

  retval = getline(&line, &len, fp);
  while(retval != -1)
  {
    char* token = strtok(line, " \t\n");

    if (!token)
    {
      //a blank line, so move to the following line.
      retval = getline(&line, &len, fp);
      continue;
    }

    if (!strcmp(token, "enable"))
    {
      int enabled = 0;

      token = strtok(NULL, " \t\n");
      if (token)
        sscanf(token, "%d", &enabled);
      if (enabled == 0)
      {
        // Not enabled interface.
        netboot_continue = 0;
        break;
      }
    }
    else if (!strcmp(token, "file"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(netboot_rem_img, token, MAX_CHAR_PER_FILENAME);
    }
    else if (!strcmp(token, "initrd"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(netboot_rem_initrd, token, MAX_CHAR_PER_FILENAME);
    }

    else if (!strcmp(token, "args"))
    {
      while ((token = strtok(NULL, " \t\n")))
        xstrcat(netboot_boot_args, sizeof(netboot_boot_args),
	  token, " ", NULL);
    }
    else if (!strcmp(token, "preboot"))
    {
      token = strtok(NULL, " \t\n");
      if (token)
        xstrncpy(netboot_rem_script, token, MAX_CHAR_PER_FILENAME);
    }
    else if (!strcmp(token, "macaddr"))
    {
       // Entries for another mac address or a comment line.
       // Just safely ignore the line.
       break;
    }
    else if (token[0] != '#')
    {
      // Not a blank line and not a comment line.
      // Maybe typo in the file, so warn here.
      print_error(0, "  Warning: Unknown enties in bootinfo file %s:\n    %s\n",
                  bootinfo_file, token);
    }

    retval = getline(&line, &len, fp);
  }

  if (line)
    free(line);
  fclose(fp);

  if (macaddr_matched  && netboot_continue && netboot_rem_img[0])
    return 0;

  return -1;
}


bool
netboot_modify(cmd_obj_t* cmd_obj)
{
  char* rem_filename;
  int status, num_intf, index;
  char* intf_name[MAX_NUM_OF_NETIF];
  char cmd[MAX_CHAR_PER_CMD];
  char local_file[MAX_CHAR_PER_FILENAME];

  // Get all interfaces and try one by one.
  num_intf = get_all_netintf(intf_name);
  
  for (index = 0; index < num_intf; index++)
  {
    if (!strncmp(intf_name[index], "lo", 2))
      continue;

    netboot_continue = -1;
    netboot_local_img[0] = 0;
    netboot_local_initrd[0] = 0;
    print_error(0, "Trying netboot over %s...\n", intf_name[index]);
    // DHCP to get the host and bootinfo file name.
    cmd[0] = 0;
    xstrcat(cmd, sizeof(cmd), DHCP_CMD_LINE, " ", intf_name[index], NULL);

    status = system(cmd);
    if (status)
    {
      print_error(0, "  Failure during netboot dhcp\n");
      continue;
    }

    // Read netboot host and bootinfo file name.
    status = netboot_read_info();
    if (status || netboot_host[0] == 0 || netboot_rem_bootinfo_file[0] == 0)
    {
      print_error(0, "  Failure when reading host and bootinfo filename\n");
      continue;
    }

    // Download the boot info file via tftp.
    cmd[0] = 0;
    xstrcat(cmd, sizeof(cmd), "/sbin/busybox tftp -g ", netboot_host, NULL);
    xstrcat(cmd, sizeof(cmd), " -r ", netboot_rem_bootinfo_file, NULL);
    if ((rem_filename = strrchr(netboot_rem_bootinfo_file, '/')) != NULL)
      rem_filename++;
    else
      rem_filename = netboot_rem_bootinfo_file;
    local_file[0] = 0;
    xstrcat(local_file, sizeof(local_file),
                         NETBOOT_BOOTINFO_PATH, rem_filename, NULL);
    xstrcat(cmd, sizeof(cmd), " -l ", local_file, NULL);

    status = system(cmd);
    if (status)
    {
      print_error(0, "  Failure when downloading the bootinfo file\n");
      continue;
    }

    // Read the bootinfo file
    status = intf_read_bootinfo(local_file, intf_name[index]);
    if (status)
    {
      netboot_continue = -1;
      print_error(0, "  net booting over interface %s failed.\n"
                  "  Falling back on the default boot setting in "
		  "the bootinfo file.\n", intf_name[index]);
      status = intf_read_bootinfo(local_file, "default");
      if (status)
      {
        print_error(0, "  Can not find boot entries for this interface. Possible"
                    " causes include:\n"
                    "    No entries for this interface and no default setting for"
                    " net boot.\n"
                    "    The interface and default setting are disabled in the"
                    " bootinfo file.\n"
                    "    Can not open the bootinfo file due to some reasons.\n");
        continue;
      }
    }

    // Download the boot image file.
    cmd[0] = 0;
    xstrcat(cmd, sizeof(cmd), "/sbin/busybox tftp -g ", netboot_host, NULL);
    xstrcat(cmd, sizeof(cmd), " -r ", netboot_rem_img, NULL);
    if ((rem_filename = strrchr(netboot_rem_img, '/')) != NULL)
      rem_filename++;
    else
      rem_filename = netboot_rem_img;
    netboot_local_img[0] = 0;
    xstrcat(netboot_local_img, sizeof(netboot_local_img),
            NETBOOT_IMG_PATH, rem_filename, NULL);

    xstrcat(cmd, sizeof(cmd), " -l ", netboot_local_img, NULL);

    status = system(cmd);
    if (status)
    {
      print_error(0, "  Failure when downloading the boot image\n");
      break;
    }

    // Download the initrd file.
    if (netboot_rem_initrd[0])
    {
      cmd[0] = 0;
      xstrcat(cmd, sizeof(cmd), "/sbin/busybox tftp -g ", netboot_host, NULL);
      xstrcat(cmd, sizeof(cmd), " -r ", netboot_rem_initrd, NULL);
      if ((rem_filename = strrchr(netboot_rem_initrd, '/')) != NULL)
        rem_filename++;
      else
        rem_filename = netboot_rem_initrd;
      netboot_local_initrd[0] = 0;
      xstrcat(netboot_local_initrd, sizeof(netboot_local_initrd),
              NETBOOT_IMG_PATH, rem_filename, NULL);

      xstrcat(cmd, sizeof(cmd), " -l ", netboot_local_initrd, NULL);

      status = system(cmd);
      if (status)
      {
        print_error(0, "  Failure when downloading the initrd file\n");
        break;
      }
    }

    if (netboot_rem_script[0])
    {
      // Download the preboot script file.
      cmd[0] = 0;
      xstrcat(cmd, sizeof(cmd), "/sbin/busybox tftp -g ", netboot_host, NULL);
      xstrcat(cmd, sizeof(cmd), " -r ", netboot_rem_script, NULL);
      if ((rem_filename = strrchr(netboot_rem_script, '/')) != NULL)
        rem_filename++;
      else
        rem_filename = netboot_rem_script;
      local_file[0] = 0;
      xstrcat(local_file, sizeof(local_file), NETBOOT_SCRIPT_PATH, rem_filename, NULL);
      xstrcat(cmd, sizeof(cmd), " -l ", local_file, NULL);

      status = system(cmd);
      if (status)
      {
        print_error(0, "  Failure when downloading the preboot script\n");
        break;
      }

      // Execute the preboot script.
      cmd[0] = 0;
      xstrcat(cmd, sizeof(cmd), "/sbin/busybox sh ", local_file, NULL);

      // We do not terminate if running the preboot script fails.
      status = system(cmd);
      if (status)
        print_error(0, "  Failure when running the preboot script\n");
    }

    // We finally finished all net boot preparation work for an interface.
    // Set the flag so boot_execute() can boot with the boot file and arguments
    // we specified.
    netboot_continue = 1;
    break;
  }

  // Free memory for intf name strings.
  for (index = 0; index < num_intf; index++)
    free(intf_name[index]);

  // Commands for new kernel executing.
  if (netboot_continue == 1)
  {
    char tmp[MAX_CHAR_PER_PARAM + sizeof("--command-line=")];

    cmd_replace(cmd_obj, 0, "/sbin/kexec");
    add_param(cmd_obj, "-l", NULL);
    add_param(cmd_obj, netboot_local_img, NULL);

    if (netboot_local_initrd[0])
      add_param(cmd_obj, "--initrd=", netboot_local_initrd, NULL);

    if (netboot_boot_args[0])
    {
      strcpy(tmp, "--command-line=");
      strcat(tmp, netboot_boot_args);
      add_param(cmd_obj, tmp, NULL);
    }

    param_update(cmd_obj);

    add_cmd(cmd_obj, "/sbin/kexec", "-e", NULL);
  }

  return true;
}


bool
otherboot_modify(cmd_obj_t* cmd_obj)
{
  char* path;
  char boot_args[MAX_CHAR_PER_PARAM] = {0};
  char boot_img[MAX_CHAR_PER_PARAM] = {0};
  char boot_initrd[MAX_CHAR_PER_PARAM] = {0};
  char boot_host[MAX_CHAR_PER_PARAM] = {0};
  char numBuf[30];
  bool have_boot_param = false;

  // Turn the "boot" command into "kexec".
  cmd_replace(cmd_obj, 0, "/sbin/kexec");

  // Get image name.
  if (hdSet)
    path = HD_PATH;
  else if (sdSet)
    path = SD_PATH;
  else if (usbSet)
    path = USB_PATH;
  else
    path = MEM_PATH;

  // imgFile will already be set if the boot command was given a
  // filename to boot.
  if (!imgFile)
  {
    if (read_boot_param(PARAM_BOOT_IMG, NULL, boot_img, sizeof(boot_img)))
      get_default("image", boot_img, sizeof(boot_img));
    imgFile = boot_img;
  }

  if ((imgFilename = strrchr(imgFile, '/')) != NULL)
    imgFilename++;
  else
    imgFilename = imgFile;

  // Get the initrd file name from srom.
  if (!initrdFile)
  {
    if (!read_boot_param(PARAM_BOOT_INITRD, NULL, boot_initrd, sizeof(boot_initrd)))
      initrdFile = boot_initrd;
  }

  if (initrdFile)
  {
    if ((initrdFilename = strrchr(initrdFile, '/')) != NULL)
      initrdFilename++;
    else
      initrdFilename = initrdFile;
  }

  // For boot cases that need to create a new boot image file, we put the
  // image file under the corresponding device path (i.e. *_PATH).
  if (tftpSet)
  {
    // In the tftp case, imgFile can be a semicolon-separated list, so
    // we loop over that list to create a script, basically:
    //     tftp file1 || tftp file2 || ...
    // To make this work, we use a fixed name for the local files, for
    // the succeeding kexec operation.
    imgFilename = "ImageFile";
    initrdFilename = "InitrdFile";

    add_param(cmd_obj, "-l", NULL);
    add_param(cmd_obj, path, imgFilename, NULL);
    if (initrdFile)
      add_param(cmd_obj, "--initrd=", path, initrdFilename, NULL);
  }
  else
  {
    add_param(cmd_obj, "-l", NULL);
    add_param(cmd_obj, path, imgFile, NULL);
    if (initrdFile)
      add_param(cmd_obj, "--initrd=", path, initrdFile, NULL);
  }

  // Build the parameter which sets the command line.  The looping is so that
  // we continue to work on systems where an older version of mboot may have
  // added boot arguments to the bootparams in chunks; we no longer do this,
  // but add only one string containing all of the parameters.
  int boot_arg_index = 1;
  int boot_args_len = strlen(boot_args);
  while (!read_boot_param(PARAM_BOOT_ARGS, itoa(numBuf, boot_arg_index++),
			  boot_args + boot_args_len,
			  MAX_CHAR_PER_PARAM - 1 - boot_args_len))
  {
    strcat(boot_args, " ");
    have_boot_param = true;
    boot_args_len = strlen(boot_args);
  }

  if (have_boot_param)
    boot_args[strlen(boot_args) - 1] = '\0';
  else
    get_default("bootparam", boot_args, sizeof(boot_args));

  if (boot_args[0])
  {
    char tmp[MAX_CHAR_PER_PARAM + sizeof("--command-line=")];
    strcpy(tmp, "--command-line=");
    strcat(tmp, boot_args);
    add_param(cmd_obj, tmp, NULL);
  }

  param_update(cmd_obj);

  // Add another kexec command to boot the image.
  add_cmd(cmd_obj, "/sbin/kexec", "-e", NULL);

  // If we're booting via tftp then prepend a tftp command to download
  // the image to memory.
  if (tftpSet)
  {
    char tftp_cmd[MAX_CHAR_PER_PARAM] = {0};

    if (!bootHost)
    {
      read_boot_param(PARAM_BOOT_HOST, NULL, boot_host, sizeof(boot_host));
      bootHost = boot_host;
    }
    if (!bootHost)
    {
      print_error(0, "No boot host specified for TFTP.\n");
      return false;
    }

    prepend_cmd(cmd_obj, "/sbin/busybox", "sh", "-c", NULL);

    char* end_ptr = imgFile + strlen(imgFile);

    while (true)
    {
      // Null-terminate current image file name.
      char* semicolon_ptr = strchr(imgFile, ';');
      if (semicolon_ptr)
        *semicolon_ptr = '\0';
      else
        semicolon_ptr = end_ptr;

      // Advance file name pointer past leading white space.
      while (imgFile < end_ptr &&
             isspace(*imgFile))
        imgFile ++;

      if (*imgFile)
      {
        // Create tftp command string.
        xstrcat(tftp_cmd, sizeof(tftp_cmd),
                "tftp",
                " -g ", bootHost,
                " -r ", imgFile, 
                " -l ", MEM_PATH, imgFilename,
                NULL);
      }
      else
      {
        // Dummy command for empty file name.
        xstrcat(tftp_cmd, sizeof(tftp_cmd), "false", NULL);
      }  

      // Advance file name pointer past semicolon.
      imgFile = semicolon_ptr + 1;

      if (imgFile >= end_ptr)
        // Terminate loop;
        break;

      // Add disjunction operator.
      xstrcat(tftp_cmd, sizeof(tftp_cmd), " || ", NULL);
    }
    add_param(cmd_obj, tftp_cmd, NULL);

    if (initrdFile)
    {
      prepend_cmd(cmd_obj, "/sbin/busybox", "tftp", "-g", bootHost,
      		  "-r", initrdFile, NULL);
      add_param(cmd_obj, "-l", NULL);
      add_param(cmd_obj, MEM_PATH, initrdFilename, NULL);
    }
  }

  return true;
}


bool
boot_modify(cmd_obj_t* cmd_obj)
{
  if (tftpSet || netSet)
    start_network();

  if (netSet == 1)
    return netboot_modify(cmd_obj);
  else
    return otherboot_modify(cmd_obj);
}


int
boot_execute(cmd_obj_t* cmd_obj)
{
  // If we were called with no arguments, then signal a restart to the
  // command loop.
  if (bootSet)
    return -1;

  sys_execute(cmd_obj);

  return 0;
}


int
boot_clean(cmd_obj_t* cmd_obj)
{
  usbSet = 0;
  memSet = 0;
  hdSet = 0;
  sdSet = 0;
  tftpSet = 0;
  netSet = 0;
  bootSet = 0;
  imgFile = NULL;
  imgFilename = NULL;
  bootHost = NULL;
  initrdFile = NULL;
  initrdFilename = NULL;

  netboot_rem_bootinfo_file[0] = 0;
  netboot_rem_img[0] = 0;
  netboot_rem_initrd[0] = 0;
  netboot_rem_script[0] = 0;
  netboot_host[0] = 0;
  netboot_continue = -1;
  netboot_local_img[0] = 0;
  netboot_boot_args[0] = 0;

  return 0;
}


void
boot_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
boot_help()
{
  char dev_string[64] = {0};

#if CFG_DEV_USB
  strcat(dev_string, "-usb | ");
#endif
#if CFG_DEV_HD
  strcat(dev_string, "-hd | ");
#endif
#if CFG_DEV_SD
  strcat(dev_string, "-sd | ");
#endif
#if CFG_DEV_MEM
  strcat(dev_string, "-mem | ");
#endif
#if CFG_DEV_TFTP
  strcat(dev_string, "-tftp | ");
#endif
#if CFG_DEV_NET
  strcat(dev_string, "-net | ");
#endif

if (strlen(dev_string) > 0)
  dev_string[strlen(dev_string) - 3] = 0;
  
  printf("Usage: boot <%s> [-host host] [-img imgFile] [-initrd initrdFile] \n", dev_string); 
}

#endif /* CFG_CMD_BOOT */
