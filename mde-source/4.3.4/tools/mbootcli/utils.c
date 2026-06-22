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

#include <ctype.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <crypt.h>


extern char* strndup(const char* string, size_t n);

static int network_initialized = 0;


void
prt_header(void)
{
  printf(BANNER_STRING);

  fflush(stdout);
}


void
prt_wait(int secs)
{
  if (secs > 1)
    printf("Booting in %d seconds\r", secs);
  else
    printf("Booting in %d second \n\n\n", secs);

  fflush(stdout);
}


void
prt_prompt(const char* const prompt)
{
  printf("%s", prompt);
}


/** Print out the error msg and exit if ex is non-zero.
 * Assumes that "fmt" will fit into "buf", after various modifications.
 */
void
print_error(int ex, const char* fmt, ...)
{
  const char* cp1 = fmt;
  char buf[BUFSIZE];
  char* cp2 = buf;
  int len;
  va_list ap;
  va_start(ap, fmt);

  while (*cp1)
  {
    if (cp1[0] == '\r')
    {
    }
    else if (cp1[0] == '\n')
    {
      *cp2++ = '\r';
      *cp2++ = *cp1++;
    }
    else if (cp1[0] == '%' && cp1[1] == 'e')
    {
      // HACK: Handle "%e".
      len = strlen(strerror(errno));
      if ((cp2 + len) > (buf + sizeof(buf) - 1))
        break;
      strcpy(cp2, strerror(errno));
      cp2 += len;
      cp1 += 2;
    }
    else
    {
      *cp2++ = *cp1++;
    }
  }
  *cp2 = '\0';

  vfprintf(stderr, buf, ap);
  va_end(ap);
  if (ex)
    exit(ex);
}


/** Disable the whole line input.
 */
struct termios
set_cbreak(void)
{
  struct termios stored_settings;        // Saved original setting
  struct termios new_settings; // New setting

  tcgetattr(fileno(stdin), &stored_settings);

  new_settings = stored_settings;
  new_settings.c_lflag &= (~ICANON);
  new_settings.c_cc[VTIME] = 0;
  new_settings.c_cc[VMIN] = 1;

  tcsetattr(fileno(stdin), TCSANOW, &new_settings);

  return stored_settings;
}


/** Disable the echo of the keyboard input.
 */
struct termios
set_noecho(void)
{
  struct termios stored_settings;        // Saved original setting
  struct termios new_settings; // New setting

  tcgetattr(fileno(stdin), &stored_settings);

  new_settings = stored_settings;
  new_settings.c_lflag &= ~(ECHO);

  tcsetattr(fileno(stdin), TCSANOW, &new_settings);

  return stored_settings;
}


/** Resotre the terminal setting base on the stored_settings.
 */
void
restore_terminal(struct termios stored_settings)
{
  tcsetattr(fileno(stdin), TCSANOW, &stored_settings);
}


/** Malloc and initialize the memory to 0, or complain.
 */
void *
xmalloc(size_t size)
{
  char* addr = malloc(size);
  if (addr == NULL)
    print_error(1, "%s", "Memory allocation failed\n");
  memset(addr, 0, size);
  return addr;
}


char*
xstrdup(const char* str)
{
  char* copy = strdup(str);
  if (copy == NULL)
    print_error(1, "%s", "Memory allocation failed\n");
  return copy;
}

char*
xstrncpy(char* dst, const char* src, size_t n)
{
  char* ret = strncpy(dst, src, n);

  dst[n - 1] = 0;

  return ret;
}


/**  The function that actually do the cat job. Here the parameter "size" is
 * the maximum allowed size for the parameter "dst".
 */
char*
xstrcat_base(char* dst, size_t size, va_list args)
{
  char* s;

  while (1)
  {
    s = va_arg(args, char*);

    if (!s || (strlen(s) + strlen(dst)) >= size)
      break;

    strcat(dst, s);
  }

  return dst;
}


/** Note: We have to use NULL as the last parameter.
 */
char*
xstrcat(char* dst, size_t size, ...)
{
  va_list args;

  va_start(args, size);

  xstrcat_base(dst, size, args);

  va_end(args);

  return dst;
}


/** Count down and wait for user's input to enter CLI
 * Return true if user does NOT enter any key - default boot happens
 */
bool
count_down(int secs)
{
  fd_set readfds;
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  struct termios storedSetting;
  char tmp[2];

  // Terminal setting
  storedSetting = set_cbreak();
  set_noecho();

  if (secs)
    prt_header();

  for (; secs > 0; secs--)
  {
    // FD resetting
    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);

    prt_wait(secs);

    if (select(1, &readfds, NULL, NULL, &timeout) < 0)
      print_error(0, "%s", "Select Error");
    else
    {
      if (FD_ISSET(fileno(stdin), &readfds))
      {
        fgets(tmp, 2, stdin);        // Eat the input
        restore_terminal(storedSetting);
        printf("\n\n");
        return false;           // User enter key, enter mboot CLI
      }
      // No input yet, wait another second
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
    }
  }

  restore_terminal(storedSetting);

  return true;
}


/**  Check if the dhcp is configured to yes.
 */
static bool
need_dhcp(const char* const dev_name)
{
  char dhcp[6];

  if (read_user_param(PARAM_DHCP, dev_name, dhcp, sizeof(dhcp)) != 0)
    return false;

  if (!strncasecmp("no", dhcp, 2))
    return false;

  return true;
}


static bool
get_netintf_name(char** name, char* buf)
{
  int start;
  for (start = 0; isspace(buf[start]); start++);

  int end = start;

  while (buf[end] && buf[end] != ':' && !isspace(buf[end]))
    end++;

  if (buf[end] == ':')
  {
    if ((end - start) < NETIF_NAME_LEN)
    {
      *name = strndup(&buf[start], end - start + 1);
      (*name)[end - start] = '\0';
      buf = &buf[end];
    }
    else
    {
      // Name is too long.
      printf("WARNING: '%s' has long name.\n", buf);
      return false;
    }
  }
  else
  {
    // No interface found.
    printf("WARNING: '%s' has no interface.\n", buf);
    return false;
  }

  return true;
}


int
get_all_netintf(char** addr)
{
  FILE *fd;
  size_t len = 0;
  char *buf = NULL;
  int index = 0;

  fd = fopen(PROCNET_FILE, "r");

  if (!fd)
  {
    print_error(0, "%s", "Can not open net dev file\n");
    return 0;
  }

  /* Eat the first TWO lines. */
  getline(&buf, &len, fd);
  getline(&buf, &len, fd);

  while (index < MAX_NUM_OF_NETIF && getline(&buf, &len, fd) != -1)
  {
    if (get_netintf_name(&addr[index], buf))
      index++;
  }

  if (buf)
    free(buf);
  fclose(fd);
  return index;
}


/**  Start the dhcp.
 */
void
start_dhcp(void)
{
  char cmd[MAX_CHAR_PER_CMD];
  char* netintf_name[MAX_NUM_OF_NETIF];
  int num_of_netintf = 0;
  int index = 0;

  if (!enable_network())
    return;

  num_of_netintf = get_all_netintf(netintf_name);

  for (; index < num_of_netintf; index++)
  {
    bzero(cmd, sizeof(cmd));

    const char* interface = netintf_name[index];

    if (!strncmp(interface, "lo", 2))
      continue;

    if (!strncmp(interface, "sit0", 4))
      continue;

    if (!need_dhcp(interface))
      continue;

    xstrcat(cmd, sizeof(cmd), DHCP_CMD_LINE, " ", interface, NULL);

    system(cmd);
  }

  /* Free memory. */
  for (index = 0; index < num_of_netintf; index++)
    free(netintf_name[index]);
}


/** Set up the network device interface and bring up the interface.
 */
void
start_netif(void)
{
  char cmd[MAX_CHAR_PER_CMD];
  char ip[ADDRESS_LEN];
  char mask[ADDRESS_LEN];
  char* netintf_name[MAX_NUM_OF_NETIF];
  int num_of_netintf = 0;
  int index = 0;

  num_of_netintf = get_all_netintf(netintf_name);

  for (; index < num_of_netintf; index++)
  {
    cmd[0] = 0;

    if (!strncmp(netintf_name[index], "lo", 2))
      continue;                 // leave the lo interface alone.

    if (!strncmp(netintf_name[index], "sit0", 4))
      continue;                 // leave the sit0 interface alone.

    if (read_user_param(PARAM_IP_ADDRESS, netintf_name[index],
                        ip, sizeof(ip)) != 0)
      continue;

    if (need_dhcp(netintf_name[index]))
      continue;

    xstrcat(cmd, sizeof(cmd), "ifconfig", " ",
            netintf_name[index], " ", ip, " ", NULL);

    if (0 == read_user_param(PARAM_NET_MASK, netintf_name[index],
                             mask, sizeof(mask)))
      xstrcat(cmd, sizeof(cmd), "netmask", " ", mask, NULL);

    system(cmd);
  }

  // Free memory.
  for (index = 0; index < num_of_netintf; index++)
    free(netintf_name[index]);
}

/** Bring any configured network interfaces down.
 */
void
shutdown_netif(void)
{
  char cmd[MAX_CHAR_PER_CMD];
  char* netintf_name[MAX_NUM_OF_NETIF];
  int num_of_netintf = 0;
  int index = 0;

  num_of_netintf = get_all_netintf(netintf_name);

  for (; index < num_of_netintf; index++)
  {
    cmd[0] = 0;

    if (!strncmp(netintf_name[index], "lo", 2))
      continue;                 // leave the lo interface alone.

    if (!strncmp(netintf_name[index], "sit0", 4))
      continue;                 // leave the sit0 interface alone.

    xstrcat(cmd, sizeof(cmd), "ifconfig", " ", netintf_name[index], " ",
            "down", NULL);

    system(cmd);
  }

  // Free memory.
  for (index = 0; index < num_of_netintf; index++)
    free(netintf_name[index]);
}


/**  Configure the routing infomation.
 */
void
start_route(void)
{
  int index = 0;
  char cmd[MAX_CHAR_PER_CMD];
  char dest[ADDRESS_LEN];
  char mask[ADDRESS_LEN];
  char gw[ADDRESS_LEN];
  char dev[MAX_CHAR_PER_DEV_NAME];

  while (1)
  {
    bzero(cmd, sizeof(cmd));
    bzero(dest, sizeof(dest));
    bzero(mask, sizeof(mask));
    bzero(gw, sizeof(gw));
    bzero(dev, sizeof(dev));

    if (!read_route(index++, dest, mask, gw, dev))
    {
      if (!strcmp(dest, "default"))        // For default route
      {
         xstrcat(cmd, sizeof(cmd), "route add", " ", dest, " ", "netmask", " ", mask, NULL);

        if (strlen(gw))
          xstrcat(cmd, sizeof(cmd), " ", "gw", " ", gw, NULL);

        if (strlen(dev))
          xstrcat(cmd, sizeof(cmd), " ", "dev", " ", dev, NULL);
      }
      else                        // For net and host route
      {
        xstrcat(cmd, sizeof(cmd), "route add", NULL);

        if (is_net_mask(mask))        // For net route
        {
          xstrcat(cmd, sizeof(cmd), " ", "-net", " ", dest, " ", "netmask", " ", mask, NULL);

          if (strlen(gw))
            xstrcat(cmd, sizeof(cmd), " ", "gw", " ", gw, NULL);

          if (strlen(dev))
            xstrcat(cmd, sizeof(cmd), " ", "dev", " ", dev, NULL);
        }
        else                        // For host route
        {
          xstrcat(cmd, sizeof(cmd), " ", dest, NULL);

          if (strlen(gw))
            xstrcat(cmd, sizeof(cmd), " ", "gw", " ", gw, NULL);

          if (strlen(dev))
            xstrcat(cmd, sizeof(cmd), " ", "dev", " ", dev, NULL);
        }
      }

      system(cmd);
    }
    else
      break;
  }

  return;
}

void
start_network(void)
{
  if (!enable_network())
    return;

  if (!network_initialized)
  {
    start_dhcp();

    start_netif();

    start_route();

    network_initialized = 1;
  }
}

void
stop_network(void)
{
  if (!enable_network())
    return;

  shutdown_netif();

  network_initialized = 0;
}

bool
is_net_mask(const char* const mask)
{
  char f1[8];
  char f2[8];
  char f3[8];
  char f4[8];

  bzero(f1, sizeof(f1));
  bzero(f2, sizeof(f2));
  bzero(f3, sizeof(f3));
  bzero(f4, sizeof(f4));

  sscanf(mask, "%s.%s.%s.%s", f1, f2, f3, f4);

  if (0 == atoi(f4))
    return true;

  return false;
}


/** Caller of this function have to make sure that the src is int and
 * the dst is big enough to hold the result.
 */
char*
itoa(char* dst, int src)
{
  int tmp;
  int index = 0;
  char tmpChar;

  while (1)
  {
    dst[index++] = src % 10 + '0';
    if (!(src = src / 10))
      break;
  }
  dst[index] = '\0';

  // Now flip the sequence
  tmp = 0;
  while (tmp < index - 1)
  {
    tmpChar = dst[tmp];
    dst[tmp++] = dst[--index];
    dst[index] = tmpChar;
  }

  return dst;
}


bool
valid_option(const struct option * option_list, const char* myoption)
{
  if (!myoption)
    return false;

  // Skip all the leading '-'.
  while (*myoption == '-')
    myoption++;

  int i;
  for (i = 0; option_list[i].name != NULL; i++)
  {
    if (!strcmp(option_list[i].name, myoption))
      return true;
  }

  return false;
}


void
hide_and_restore_12(int action)
{
  static int fd, fd1, fd2;

  // Flush all the streams.
  fflush(NULL);

  // hide the stdout and stderr
  if (action == 0)
  {
    fd = open("/dev/null", O_WRONLY, 0644);
    fd1 = dup(fd);
    fd2 = dup(fd);

    dup2(1, fd1);                // save the context of stdout in fd1.
    dup2(2, fd2);                // save the context of stderr in fd2.

    dup2(fd, 1);                // redirect 1 to fd, something like 1>fd
    dup2(fd, 2);
  }

  // restore the stdout and stderr
  if (action == 1)
  {
    if (fd1 > 0)
      dup2(fd1, 1);

    if (fd2 > 0)
      dup2(fd2, 2);

    if (fd > 0)
      close(fd);
  }
}


char
my_read_char(const char* mesg)
{
  fflush(stdout);
  char buf[80];

  char in = '-';
  while ((in != 'y') && (in != 'n'))
  {
    printf("\n%s (y or n)", mesg);
    fgets(buf, 80, stdin);
    if (strlen(buf) == 2)
      in = tolower(buf[0]);
  }

  return in;
}


/** map the device name to linux conventional.
 */
int
map_linux_device(const char* const part, char* device)
{
  // the partition name should be in the form of "h1part1" or "u1part3"
  // 'h1part0 means the whole disk'
  if (!((strlen(part) == 7) &&
        ((part[0] == 'h') || (part[0] == 'u') || (part[0] == 's')) &&
        (isdigit(part[1]) && isdigit(part[6])) &&
        (strncmp(&part[2], "part", 4) == 0)))
  {
    return -1;
  }

  if (part[0] == 'h')
    strcpy(device, "/dev/hd");
  else if (part[0] == 'u')
    strcpy(device, "/dev/ub");
  else if (part[0] == 's')
    strcpy(device, "/dev/sd");

  device[7] = part[1] - '1' + 'a';
  if (part[6] == '0')         // part0 refers to the whole disk.
  {
    device[8] = '\0';
  }
  else
  {
    device[8] = part[6];
    device[9] = '\0';
  }

  return 0;
}
