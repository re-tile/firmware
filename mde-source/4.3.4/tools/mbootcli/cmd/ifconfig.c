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
#if CFG_CMD_IFCONFIG
#include "utils.h"
#include "flash.h"

#include <sys/wait.h>


static char interface[MAX_CHAR_PER_PARAM];
static char hostname[MAX_CHAR_PER_PARAM];
static char netmask[MAX_CHAR_PER_PARAM];
static char MAC_addr[MAX_CHAR_PER_PARAM];
static bool interfaceSet = false;
static bool hostSet = false;
static int maskSet = 0;
static int MACSet = 0;
static int upSet = 0;
static int downSet = 0;
static int modeSet = 0;
static char* duplex = NULL;
static char* speed = NULL;

static const struct option long_options[] = {
  {"mac", 1, NULL, '1'},
  {"mask", 1, NULL, '2'},
  {"up", 0, NULL, '3'},
  {"down", 0, NULL, '4'},
#ifdef BUG_4560_FIXED
  {"mode", 1, NULL, '5'},
#endif
  {NULL, 0, NULL, 0}
};


void
ifconfig_init(cmd_obj_t* cmd_obj)
{
  memset(interface, 0, sizeof(interface));
  memset(hostname, 0, sizeof(hostname));
  memset(netmask, 0, sizeof(netmask));
  memset(MAC_addr, 0, sizeof(MAC_addr));
}


#ifdef BUG_4560_FIXED
static int
mode_parse(const char* arg)
{
  int len;

  if (arg == NULL)
    return -1;

  if (((strcasecmp(arg, "10h") == 0) ||
       (strcasecmp(arg, "10f") == 0) ||
       (strcasecmp(arg, "100h") == 0) ||
       (strcasecmp(arg, "100f") == 0) ||
       (strcasecmp(arg, "1000h") == 0) || (strcasecmp(arg, "1000f") == 0)))
  {
    len = strlen(arg);
    if ((arg[len - 1] == 'h') || (arg[len - 1] == 'H'))
      duplex = "half";
    else
      duplex = "full";

    speed = (len == 3) ? "10" : ((len == 4) ? "100" : "1000");

    return 0;
  }

  return -1;
}
#endif


int
ifconfig_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;                        // Reset it before we call getopt_long_only
  opterr = 0;

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1:2:345:",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':                        // -mac
      xstrncpy(MAC_addr, optarg, sizeof(MAC_addr));
      MACSet += 1;
      break;

    case '2':                        // -mask
      xstrncpy(netmask, optarg, sizeof(netmask));
      maskSet += 1;;
      break;

    case '3':                        // -up
      upSet += 1;;
      break;

    case '4':                        // down
      downSet += 1;
      break;

#ifdef BUG_4560_FIXED
    case '5':                        // mode
      if (!mode_parse(optarg))
        modeSet += 1;
      else
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_1);
      }
      break;
#endif

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

  if (cmd_obj->argc > optind)
  {
    // The idea identical to busybox/networking/ifconfig.c,
    // The first not "-a" parameter is treated as the interface name.
    xstrncpy(interface, cmd_obj->argv[optind++], sizeof(interface));
    interfaceSet = true;

    if (cmd_obj->argc > optind)
    {
      //
      // If CLI bring one of these arguments to busybox, it will regard
      // it as a valid argument, other than the host name.
      //
      char* cp = cmd_obj->argv[optind++];
      if ((!strcmp(cp, "arp")) || (!strcmp(cp, "trailers")) ||
          (!strcmp(cp, "promisc")) || (!strcmp(cp, "multicast")) ||
          (!strcmp(cp, "allmulti")) || (!strcmp(cp, "dynamic")) ||
          (!strcmp(cp, "up")) || (!strcmp(cp, "down")))
      {
        set_error_info(cmd_obj, optind - 1);
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
      }

      xstrncpy(hostname, cp, sizeof(hostname));
      hostSet = true;
    }

    if (cmd_obj->argc > optind)
      return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);
  }

  if ((modeSet || maskSet || MACSet || downSet || upSet) && !interfaceSet)
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM_3);

  if ((modeSet) && (hostSet || maskSet || MACSet || downSet || upSet))
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM_2);

  if ((modeSet > 1) || (hostSet > 1) || (maskSet > 1) || (MACSet > 1)
      || (downSet > 1) || (upSet > 1))
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return 0;
}


bool
ifconfig_modify(cmd_obj_t* cmd_obj)
{
  if (modeSet == 0)
  {
    cmd_replace(cmd_obj, 0, "/sbin/busybox");
    add_param(cmd_obj, "ifconfig", NULL);

    if (interfaceSet)
      add_param(cmd_obj, interface, NULL);

    if (hostSet)
      add_param(cmd_obj, hostname, NULL);

    if (maskSet)
    {
      add_param(cmd_obj, "netmask", NULL);
      add_param(cmd_obj, netmask, NULL);
    }

    if (MACSet)
    {
      add_param(cmd_obj, "hw", NULL);
      add_param(cmd_obj, "ether", NULL);
      add_param(cmd_obj, MAC_addr, NULL);
    }

    if (upSet)
      add_param(cmd_obj, "up", NULL);

    if (downSet)
      add_param(cmd_obj, "down", NULL);

    param_update(cmd_obj);
  }
  else
  {
    cmd_replace(cmd_obj, 0, "/usr/sbin/ethtool");
    add_param(cmd_obj, "-s", NULL);
    add_param(cmd_obj, interface, NULL);
    add_param(cmd_obj, "speed", NULL);
    add_param(cmd_obj, speed, NULL);
    add_param(cmd_obj, "duplex", NULL);
    add_param(cmd_obj, duplex, NULL);
    param_update(cmd_obj);
  }

  return true;
}


int
ifconfig_execute(cmd_obj_t* cmd_obj)
{
  int status;

  status = sys_execute(cmd_obj);

  if (((status == 0)) && (interface[0] != '\0'))
  {
    if (hostSet == true)
      write_user_param(PARAM_IP_ADDRESS, interface, hostname);

    if (maskSet)
      write_user_param(PARAM_NET_MASK, interface, netmask);

    if (MACSet)
      write_user_param(PARAM_MAC_ADDRESS, interface, MAC_addr);
  }

  return 0;
}


int
ifconfig_clean(cmd_obj_t* cmd_obj)
{
  memset(interface, 0, sizeof(interface));
  memset(hostname, 0, sizeof(hostname));
  memset(netmask, 0, sizeof(netmask));
  memset(MAC_addr, 0, sizeof(MAC_addr));
  interfaceSet = false;
  hostSet = false;
  maskSet = 0;
  MACSet = 0;
  modeSet = 0;
  upSet = 0;
  downSet = 0;
  duplex = NULL;
  speed = NULL;

  return 0;
}


void
ifconfig_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);

  switch (cmd_obj->parse_result)
  {
  case ERROR_INVALID_PARAM_1:
    printf("Invalid speed and duplex mode.\n");
    break;

  case ERROR_INVALID_PARAM_2:
    printf("The speed and duplex mode setting should not combined "
           "with other arguments.\n");
    break;

  case ERROR_INVALID_PARAM_3:
    printf("You must specify the name of the interface.\n");
    break;

  default:
    break;
  }
}


void
ifconfig_help(void)
{
  printf("Usage: ifconfig [interface] [ip_address] [-mask network_mask] "
         "[-mac mac_address] [-up | -down]\n");
  printf("       ifconfig [interface] "
#ifdef BUG_4560_FIXED
         "[-mode duplex_speed]"
#endif
         "\n");
}

#endif /* CFG_CMD_IFCONFIG */
