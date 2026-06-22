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
#if CFG_CMD_ROUTE
#include "utils.h"


static char* action = NULL;
static int netSet = 0;
static int hostSet = 0;
static route_struct myroute;

static const struct option long_options[] = {
  {"mask", 1, NULL, '1'},
  {"gw", 1, NULL, '2'},
  {"dev", 1, NULL, '3'},
//   {"net", 0, NULL, '4'},
//   {"host", 0, NULL, '5'},
  {NULL, 0, NULL, 0}
};


static bool
valid_action(const char* const action)
{
  if (strncasecmp(action, "add", 3) && strncasecmp(action, "del", 3))
    return false;

  return true;
}


static bool
is_default(route_struct* route)
{
  return (0 == strncmp(route->dest, "default", 7) ? true : false);
}


static bool
have_mask(route_struct* route)
{
  return ((NULL == route->netmask) ? false : true);
}


static bool
have_opt(route_struct* route)
{
  return ((NULL == route->option) ? false : true);
}


static bool
have_gw(route_struct* route)
{
  return ((NULL == route->gateway) ? false : true);
}


static bool
have_dev(route_struct* route)
{
  return ((NULL == route->netintf) ? false : true);
}

static bool
action_is_add(const char* const action)
{
  return (0 == strncmp(action, "add", 3) ? true : false);
}

void
route_init(cmd_obj_t* cmd_obj)
{
  myroute.dest = NULL;
  myroute.netmask = NULL;
  myroute.gateway = NULL;
  myroute.netintf = NULL;
  myroute.option = NULL;
}


static bool
already_has_default_route(void)
{
  int index = 0;
  bool ret = false;
  char dest[ADDRESS_LEN];
  char mask[ADDRESS_LEN];
  char gw[ADDRESS_LEN];
  char dev[MAX_CHAR_PER_DEV_NAME];

  while (1)
  {
    bzero(dest, sizeof(dest));
    bzero(mask, sizeof(mask));
    bzero(gw, sizeof(gw));
    bzero(dev, sizeof(dev));

    if (!read_route(index++, dest, mask, gw, dev))
    {
      if (!strcmp(dest, "default"))
      {
        ret = true;
        break;
      }
    }
    else
      break;
  }
  return ret;
}


int
route_parse(cmd_obj_t* cmd_obj)
{
  int opt;

  optind = 0;      //Reset it before we call getopt_long_only
  opterr = 0;      //prevent getopt() print unexpected messages.

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1:2:3:",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':
      myroute.netmask = strdup(optarg);
      if (strlen(myroute.netmask) >= ADDRESS_LEN)
        myroute.netmask[ADDRESS_LEN - 1] = 0;
      break;

    case '2':
      myroute.gateway = strdup(optarg);
      if (strlen(myroute.gateway) >= ADDRESS_LEN)
        myroute.gateway[ADDRESS_LEN - 1] = 0;
      break;

    case '3':
      myroute.netintf = strdup(optarg);
      if (strlen(myroute.netintf) >= ADDRESS_LEN)
        myroute.netintf[ADDRESS_LEN - 1] = 0;
      break;

/*      case '4':
        netSet = 1;
        myroute.option = "-net";
        break;

      case '5':
        hostSet = 1;
        myroute.option = "-host";
        break;
*/

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

  if (netSet + hostSet > 1)
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  // Not just a show command
  if (cmd_obj->argc > 1)
  {
    if (optind + 2 > cmd_obj->argc)
      return (cmd_obj->parse_result = ERROR_MISSING_PARAM);

    if (!have_gw(&myroute))
      return (cmd_obj->parse_result = ERROR_NEED_GW);

    if (!have_dev(&myroute))
      return (cmd_obj->parse_result = ERROR_NEED_DEVICE);


    if (!valid_action(action = cmd_obj->argv[optind]))
    {
      set_error_info(cmd_obj, optind);
      return (cmd_obj->parse_result = ERROR_INVALID_ACTION);
    }

    if (action_is_add(action) && already_has_default_route())
      return (cmd_obj->parse_result = ERROR_ALREADY_HAS_DEFAULT_ROUTE);

    myroute.dest = cmd_obj->argv[optind + 1];

    // We only support default route so far.
    if (strncasecmp(myroute.dest, "default", 7))
      return (cmd_obj->parse_result = ERROR_ROUTE_TYPE_NOT_SUPPORT);
  }

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
route_modify(cmd_obj_t* cmd_obj)
{
  int pos = 0;

  cmd_replace(cmd_obj, pos, "/sbin/route");

  if (action)
  {
    action = add_param(cmd_obj, action, NULL);

    if (netSet || hostSet)
      myroute.option = add_param(cmd_obj, myroute.option, NULL);
    myroute.dest = add_param(cmd_obj, myroute.dest, NULL);
  }

  if (myroute.netmask)
  {
    add_param(cmd_obj, "netmask", NULL);
    myroute.netmask = add_param(cmd_obj, myroute.netmask, NULL);
  }
  if (myroute.gateway)
  {
    add_param(cmd_obj, "gw", NULL);
    myroute.gateway = add_param(cmd_obj, myroute.gateway, NULL);
  }
  if (myroute.netintf)
  {
    add_param(cmd_obj, "dev", NULL);
    myroute.netintf = add_param(cmd_obj, myroute.netintf, NULL);
  }

  // Get rid of old parameter and move the new parameter
  // to the begin of argv array 
  param_update(cmd_obj);

  return true;
}


void
route_set(const char* const action, route_struct* route)
{
  if (is_default(route))
  {
    if (!have_mask(route))
      route->netmask = "0.0.0.0";
  }
  else if (!have_opt(route) && !have_mask(route))
  {
    route->netmask = "255.255.255.255";
  }

  if (action_is_add(action))
    add_route(route->dest, route->netmask, route->gateway, route->netintf);
  else
    delete_route(route->dest, route->netmask, route->gateway, route->netintf);
}


int
route_execute(cmd_obj_t* cmd_obj)
{
  if (!sys_execute(cmd_obj) && (cmd_obj->argc > 1))
  {
    // The route command has been executed successfully. 
    // If it is a real conguration command instead of a show, 
    // we need to write the configuration to SPI ROM 
    route_set(action, &myroute);
  }

  return 0;
}


int
route_clean(cmd_obj_t* cmd_obj)
{
  action = NULL;
  myroute.dest = NULL;
  myroute.netmask = NULL;
  myroute.gateway = NULL;
  myroute.netintf = NULL;
  myroute.option = NULL;
  netSet = 0;
  hostSet = 0;

  return 0;
}


void
route_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);
}


void
route_help(void)
{
//  printf("Usage: route [add|del] [ [-net/-host] destination] [-mask netmask] 
// [-gw gateway] [-dev interface]\n");
  printf("Usage: route\n");
  printf("       route <add | del> <default> <-mask netmask> <-gw gateway> "
         "<-dev interface>\n");
}

#endif /* CFG_CMD_ROUTE */
