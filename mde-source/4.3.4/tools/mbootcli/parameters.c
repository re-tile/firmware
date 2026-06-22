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

#include "utils.h"
#include "flash.h"


static char* banner[MAX_PARAM];
static char buf[MAX_CHAR_PER_CMD];

// For the two SROM device separately. Assume that all the parameters in 
// one SROM device should be fitted into one flash sector.
//
struct param_block params[DEVICE_NUM];

// In order to keep the route name unique.
static int route_index = 0;

static void
write_magic_number(int device)
{
  if (device == BOOT_PARAM)
    *(int *) &params[BOOT_PARAM].buf = BOOT_MAGIC_NUMBER;

  if (device == USER_PARAM)
    *(int *) &params[USER_PARAM].buf = USER_MAGIC_NUMBER;

  params[device].param_len = 4;
  params[device].param_num = 0;
}


int
read_in_param(int device)
{
  int len = BUFSIZE;
  int read_len;
  int offset = 4; // 4 bytes are for the magic number.
  struct param p;
  struct param_block *pb = &params[device];

  memset(&params[device], 0, sizeof (struct param_block));

  if (read_flash_data(device, &len, &read_len, pb->buf) != 0)
  {
    write_magic_number(device);
    return -1;
  }
  if ((device == BOOT_PARAM) && (*(int *) pb->buf) != BOOT_MAGIC_NUMBER)
  {
    write_magic_number(device);
    return -1;
  }

  if ((device == USER_PARAM) && (*(int *) pb->buf) != USER_MAGIC_NUMBER)
  {
    write_magic_number(device);
    return -1;
  }

  pb->param_len = read_len;
  int i = 0;
  while (pb->param_len > (offset + 4))
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    // found out the "biggest" route name.
    if (*p.type == PARAM_ROUTE)
    {
      int name_index = strtol(p.value, NULL, 10);

      if (name_index >= route_index)
        route_index = name_index + 1;
    }

    p.length = (unsigned short *) &pb->buf[offset + 2];

    i++;
    offset += (4 + *p.length);
  }
  pb->param_num = i;

  return 0;
}


/** Searching for the entry specified by the match_str and param_type
 * @param device USER_PARAM or BOOT_PARAM
 * @param match_str the string identifies entries share the same type
 * @param param_type defined in parameters.h
 * @return 0 found matching entry, -1 not found
 */
static int
find_match_entry(int device, const char* match_str, unsigned short param_type,
                 struct param *p)
{
  int i;
  int offset = 4; // The first 4 bytes are the magic number.

  struct param_block *pb = &params[device];

  for (i = 0; i < pb->param_num && (offset + 4) <= pb->param_len; i++)
  {
    p->type = (unsigned short *) &pb->buf[offset];
    p->length = (unsigned short *) &pb->buf[offset + 2];
    p->value = &pb->buf[offset + 4];

    if (*p->type == param_type)
    {
      if (match_str == NULL)
        return 0;

      if (strncmp(p->value, match_str, strlen(match_str)) == 0)
      {
        // found match entry.
        return 0;
      }
    }

    offset += (4 + *p->length);    // 4 accounts for the type and length fields
  }

  return -1;
}


static int
read_param(int device, int type, const char* match_str, char* value, size_t size)
{
  struct param p;
  int len;

  value[0] = 0;
  if ((find_match_entry(device, match_str, type, &p)) == 0)
  {
    char* cp;

    if (match_str == NULL)
    {
      cp = p.value;
      len = *p.length;
    }
    else
    {
      cp = strchr(p.value, '=') + 1;
      len = *p.length - 1 - strlen(match_str);
    }

    if (cp != NULL)
      xstrncpy(value, cp, size);

    return 0;
  }

  return -1;
}


static inline unsigned short
round_to4(unsigned short len)
{
  return ((len + 3) & 0xFFFC);
}


static void
move_params(int device, int offset, struct param *p)
{
  char* src;
  char* dst;
  unsigned short len;
  int i;

  struct param_block *pb = &params[device];

  len = (unsigned short) (&pb->buf[pb->param_len] - &p->value[*p->length]);

  if (offset < 0)
  {
    src = &p->value[*p->length];
    dst = src + offset;
    dst = (dst > pb->buf) ? dst : pb->buf;

    for (i = 0; i < len; i++)
      *dst++ = *src++;

    // clear the memory after moving
    memset(dst, 0, -offset);
  }
  else
  {
    src = &pb->buf[pb->param_len - 1];
    dst = src + offset;
    dst = (dst <= &pb->buf[BUFSIZE]) ? dst : &pb->buf[BUFSIZE];

    for (i = 0; i < len; i++)
      *dst-- = *src--;
  }
}


static void
delete_param(int device, struct param_block *pb, struct param *p)
{
  int offset;

  offset = 0 - (4 + *p->length);
  move_params(device, offset, p);
  pb->param_len += offset;
  pb->param_num -= 1;

  pb->dirty = true;
}


void
clear_param(int type, char* match_str)
{
  struct param p;
  struct param_block *pb;

  int device = (type < PARAM_USER_OFFSET) ? BOOT_PARAM : USER_PARAM;
  pb = &params[device];

  // found no match entry
  if (((find_match_entry(device, match_str, type, &p))) == 0)
  {
    delete_param(device, pb, &p);
  }
}


static void
write_param(int device, int type, char* match_str, char* value)
{
  char* cp;
  struct param p;
  struct param_block *pb = &params[device];

  // found no match entry
  if (((find_match_entry(device, match_str, type, &p))) != 0)
  {
    p.type = (unsigned short *) &pb->buf[pb->param_len];
    p.length = (unsigned short *) &pb->buf[pb->param_len + 2];
    p.value = &pb->buf[pb->param_len + 4];

    pb->param_len += 4;
    pb->param_num += 1;

    *p.type = type;
    *p.length = 0;
  }

  unsigned short value_len = strlen(value) + 1;
  unsigned short id_len, len;

  if (match_str == NULL)
  {
    len = round_to4(value_len);
    id_len = 0;
  }
  else
  {
    id_len = strlen(match_str);
    len = round_to4(id_len + value_len + 1);
  }

  int offset = len - *p.length;

  if ((pb->param_len + offset) > BUFSIZE)
  {
    // No space left, so returns.
    if (match_str == NULL)
    {
      pb->param_len -= 4;
      pb->param_num -= 1;
    }
    return;
  }

  // make room for new parameter.
  if (offset != 0)
    move_params(device, offset, &p);

  // copy new parameter into params[]
  cp = p.value;
  memset(cp, 0, len);

  strncpy(cp, match_str, id_len);
  cp += id_len;

  if (match_str != NULL)
    *cp++ = '=';

  strncpy(cp, value, value_len);

  params[device].param_len += offset;
  *p.length = len;

  params[device].dirty = true;
}


int
read_cfg()
{
  return (read_in_param(BOOT_PARAM) | read_in_param(USER_PARAM));
}


int
read_boot_param(int type, const char* match_str, char* value, size_t size)
{
  return (read_param(BOOT_PARAM, type, match_str, value, size));
}


int
read_user_param(int type, const char* match_str, char* value, size_t size)
{
  return (read_param(USER_PARAM, type, match_str, value, size));
}


void
write_boot_flash()
{
  struct param_block *pb = &params[BOOT_PARAM];
  if (pb->dirty == true)
  {
    write_flash_data(BOOT_PARAM, pb->buf, pb->param_len);
  }

  pb->dirty = false;
}


void
write_boot_param(int type, char* match_str, char* value)
{
  write_param(BOOT_PARAM, type, match_str, value);
  //  write_boot_flash();
}


void
write_user_flash(void)
{
  struct param_block *pb = &params[USER_PARAM];
  if (pb->dirty == true)
  {
    write_flash_data(USER_PARAM, pb->buf, pb->param_len);
  }

  pb->dirty = false;
}


/** Save the changed parameter in the params[]
 * @param type defined in parameters.h
 * @param match_str: i.e.: eth0, eth1
 * @param value carries the parameter.
 */
void
write_user_param(int type, char* match_str, char* value)
{
  write_param(USER_PARAM, type, match_str, value);
  write_user_flash();
}


static void
init_param_banner(void)
{
  banner[PARAM_BOOT_DEVICE]       = "Boot device";
  banner[PARAM_BOOT_IMG]          = "Boot image";
  banner[PARAM_BOOT_ARGS]         = "Boot arguments";
  banner[PARAM_BOOT_HOST]         = "Boot host";
  banner[PARAM_BOOT_INITRD]       = "Boot initrd";

  banner[PARAM_BAUD_RATE]         = "Baud rate";
  banner[PARAM_IP_ADDRESS]        = "IP address";
  banner[PARAM_NET_MASK]          = "Subnet mask";
  banner[PARAM_MAC_ADDRESS]       = "MAC address";
  banner[PARAM_SPEED_DUPLEX]      = "Speed and duplex";
  banner[PARAM_DHCP]              = "DHCP";
  banner[PARAM_ROUTE]             = "Route";
  banner[PARAM_DEFAULT_ROUTE]     = "Default route";
}


static int
print_item(struct param *p)
{
  char* cp;

  if (p == NULL)
    return -1;

  if ((*p->type < MAX_PARAM) && p->value != NULL)
  {
    printf("    %-25s: ", banner[*p->type]);
    cp = strchr(p->value, '=');
    cp = (cp == NULL) ? p->value : cp + 1;
    printf("%s\n", cp);
  }

  return 0;
}


static bool
ever_printed(char* interface)
{
  static int index = 0;
  static char printed_int[10][20];
  int i;

  if ((interface == NULL) || (index == 9))
  {
    index = 0;
    return false;
  }

  for (i = 0; i < index; i++)
  {
    if (strcmp(printed_int[i], interface) == 0)
      return true;
  }

  strcpy(printed_int[index], interface);
  index += 1;
  return false;
}


static const char* 
print_route(const char* cp, const char* mesg)
{
  char ch;
  int i = 0;

  ch = cp[i++];
  if ((ch != ' ') && (ch != '\0'))
  {
    printf("    %-25s: ", mesg);
    while ((ch != ' ') && (ch != '\0'))
    {
      putchar(ch);
      ch = cp[i++];
    }
    putchar('\n');
  }

  return &cp[i];
}


void
show_bootparam(void)
{
  int i, offset = 4;                // The first 4 bytes are the magic number.
  struct param p;
  struct param_block *pb;

  init_param_banner();

  pb = &params[BOOT_PARAM];

  buf[0] = '\0';

  for (i = 0; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if ((*p.type == PARAM_BOOT_ARGS) && (p.value != NULL))
    {
      strncat(buf, (strchr(p.value, '=') + 1), *p.length);
      strcat(buf, " ");
    }

    offset += (4 + *p.length);  // 4 accounts for the type and length fields
  }

  if (buf[0] != '\0')
    printf("    %-25s: %s\n", banner[PARAM_BOOT_ARGS], buf);
}

int
showcfg(void)
{
  int i, device, offset = 4;   // The first 4 bytes are the magic number.
  struct param p;
  struct param_block *pb;

  init_param_banner();

  printf("\nBOOT PARAMETERS:\n");
  device = BOOT_PARAM;
  pb = &params[device];

  // print the name of the default device.
  if ((find_match_entry(device, NULL, PARAM_BOOT_DEVICE, &p)) == 0)
  {
    print_item(&p);
  }

  // print the name of the default image.
  if ((find_match_entry(device, NULL, PARAM_BOOT_IMG, &p)) == 0)
  {
    print_item(&p);
  }

  // print the name of the default image.
  if ((find_match_entry(device, NULL, PARAM_BOOT_HOST, &p)) == 0)
  {
    print_item(&p);
  }

  // print the name of the default initramfs file.
  if ((find_match_entry(device, NULL, PARAM_BOOT_INITRD, &p)) == 0)
  {
    print_item(&p);
  }


  // print the booting paramters.
  show_bootparam();

  // ----------------------------------
  // print the user parameters.
  device = USER_PARAM;
  printf("\nUSER PARAMETERS:\n");
  pb = &params[device];

  // print the baud rate of the serial port.
  offset = 4;
  for (i = 0; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if ((*p.type == PARAM_BAUD_RATE) && (p.value != NULL))
    {
      // printf the name of the interface.
      int j = 0;
      char ch = p.value[j++];
      putchar('\n');
      while ((ch != '\0') && (ch != '='))
      {
        putchar(ch);
        ch = p.value[j++];
      }

      printf(":\n");

      // print the baud rate.
      printf("    %-25s: %s\n", banner[*p.type], (strchr(p.value, '=') + 1));
    }

    offset += (4 + *p.length);        // 4 accounts for the type and length fields
  }

  // print the network interface parameters.
  offset = 4;
  for (i = 0; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if ((*p.type == PARAM_IP_ADDRESS) ||
        (*p.type == PARAM_DHCP) ||
        (*p.type == PARAM_MAC_ADDRESS) ||
        (*p.type == PARAM_SPEED_DUPLEX) || (*p.type == PARAM_NET_MASK))
    {
      // print the name of the interface.
      int j = 0;
      char interface[20];
      while ((p.value[j] != '\0') && (p.value[j] != '='))
      {
        interface[j] = p.value[j];
        j += 1;
      }
      interface[j] = '\0';
      if (ever_printed(interface) == false)
      {
        printf("\n%s:\n", interface);

        struct param pp;

        // print the DHCP entry.
        if ((find_match_entry(device, interface, PARAM_DHCP, &pp)) == 0)
          print_item(&pp);

        // print the ip-address.
        if ((find_match_entry(device, interface, PARAM_IP_ADDRESS, &pp)) == 0)
          print_item(&pp);

        // print the subnet mask.
        if ((find_match_entry(device, interface, PARAM_NET_MASK, &pp)) == 0)
          print_item(&pp);

        // print the MAC-address.
        if ((find_match_entry(device, interface, PARAM_MAC_ADDRESS, &pp)) == 0)
          print_item(&pp);

        // print the ip-address.
        if ((find_match_entry(device, interface, PARAM_SPEED_DUPLEX, &pp)) == 0)
          print_item(&pp);
      }
    }

    offset += (4 + *p.length);  // 4 accounts for the type and length fields
  }

  // clear the temporary variables.
  ever_printed(NULL);

  // print the routing table.
  offset = 4;
  int index = 1;
  for (i = 0; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if ((*p.type == PARAM_ROUTE) && (p.value != NULL))
    {
      printf("\nRoute %d:\n", index++);

      const char* cp = strchr(p.value, '=') + 1;
      cp = print_route(cp, "Destination address");
      cp = print_route(cp, "Subnet mask");
      cp = print_route(cp, "Gateway");
      cp = print_route(cp, "Interface");
    }
    offset += (4 + *p.length);  // 4 accounts for the type and length fields
  }

  // print the default gateway.
  if ((find_match_entry(device, NULL, PARAM_DEFAULT_ROUTE, &p)) == 0)
    print_item(&p);

  return 0;
}


void
print_param(int type, char* match_str)
{
  struct param p;
  int device;

  device = (type < PARAM_USER_OFFSET) ? BOOT_PARAM : USER_PARAM;

  if ((find_match_entry(device, match_str, type, &p)) == 0)
  {
    print_item(&p);
  }
}


void
clear_user(void)
{
  memset(&params[USER_PARAM], 0, sizeof (struct param_block));
  write_magic_number(USER_PARAM);
  write_flash_data(USER_PARAM, params[USER_PARAM].buf,
                   params[USER_PARAM].param_len);
}

void
clear_boot(void)
{
  memset(&params[BOOT_PARAM], 0, sizeof (struct param_block));
  write_magic_number(BOOT_PARAM);
  write_flash_data(BOOT_PARAM, params[BOOT_PARAM].buf,
                   params[BOOT_PARAM].param_len);
}


void
add_route(char* ip_addr, char* mask, char* gw, char* dev)
{
  char route_name[10];

  sprintf(buf, "%s %s %s %s", ip_addr, mask, gw, dev);

  // The routing table begins with the route_index--an integer number.
  sprintf(route_name, "%d", route_index++);

  write_param(USER_PARAM, PARAM_ROUTE, route_name, buf);
  write_user_flash();
}


int
delete_route(char* ip_addr, char* mask, char* gw, char* dev)
{
  char* cp;
  int i, offset = 4;           // The first 4 bytes are the magic number.
  struct param_block *pb = &params[USER_PARAM];
  struct param p;

  sprintf(buf, "%s %s %s %s", ip_addr, mask, gw, dev);

  for (i = 0; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if (*p.type == PARAM_ROUTE)
    {
      cp = strchr(p.value, '=') + 1;

      if ((cp != NULL) && (strcmp(buf, cp) == 0))
      {
        delete_param(USER_PARAM, pb, &p);
        write_user_flash();
        return 0;
      }
    }
    offset += (4 + *p.length); // 4 accounts for the type and length fields
  }
  return -1;
}


static const char* 
read_route_entry(const char* cp, char* dst)
{
  char ch;
  int i = 0;

  ch = cp[i++];
  if ((ch != ' ') && (ch != '\0'))
  {
    while ((ch != ' ') && (ch != '\0'))
    {
      *dst++ = ch;
      ch = cp[i++];
    }
  }

  *dst = '\0';

  return &cp[i];
}


int
read_route(int route_num, char* ip_addr, char* mask, char* gw, char* dev)
{
  static int i = 0;
  static int offset = 4;        // The first 4 bytes are the magic number.
  struct param_block *pb = &params[USER_PARAM];
  struct param p;

  if (route_num == 0)
  {
    i = 0;
    offset = 4;
  }

  for (; (i < pb->param_num) && ((offset + 4) <= pb->param_len); i++)
  {
    p.type = (unsigned short *) &pb->buf[offset];
    p.length = (unsigned short *) &pb->buf[offset + 2];
    p.value = &pb->buf[offset + 4];

    if (*p.type == PARAM_ROUTE)
    {
      const char* cp;
      cp = strchr(p.value, '=') + 1;
      cp = read_route_entry(cp, ip_addr);
      cp = read_route_entry(cp, mask);
      cp = read_route_entry(cp, gw);
      cp = read_route_entry(cp, dev);

      i += 1;
      offset += (4 + *p.length);

      return 0;
    }
    offset += (4 + *p.length); // 4 accounts for the type and length fields
  }

  return -1;
}
