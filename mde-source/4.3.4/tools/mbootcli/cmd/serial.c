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
#if CFG_CMD_SERIAL
#include "utils.h"

#include <ctype.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>


typedef struct
{
  unsigned short baud;
  unsigned short value;
} baud_mapping;


static int arr_bauds[] = {
  B0, B50, B75, B110, B134, B150, B200, B300, B600, B1200, B1800, B2400,
  B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800
};

static int arr_speed[] = {
  0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
  4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800
};


static bool portSet = false;
static bool baudSet = false;
static char* baud_str = NULL;
static int port = 0;
static unsigned int baud_rate = 0;

static const struct option long_options[] = {
  {"port", 1, NULL, '1'},
  {NULL, 0, NULL, 0}
};


static unsigned int
get_speed( speed_t baud)
{
  int num_of_item = sizeof (arr_speed)/ sizeof (int);
  int i;
  for (i = 0; i < num_of_item; i++)
  {
    if (arr_bauds[i] == baud)
      return arr_speed[i];
  }

  return 0;
}


static speed_t
get_baud(unsigned int speed)
{
  int num_of_item = sizeof (arr_speed)/ sizeof (int);
  int i;
  for (i=0; i<num_of_item; i++)
  {
    if (arr_speed[i] == speed)
      return arr_bauds[i];
  }

  return -1;
}


unsigned int
read_baud(int p_num)
{
  struct termios mode;
  char device[15];

  sprintf(device, "/dev/ttyS%d", p_num);

  int fd = open(device, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    return -1;

  memset(&mode, 0, sizeof(mode));
  if (tcgetattr(fd, &mode) != 0)
  {
    return -1;
  }
  close(fd);

  return cfgetispeed(&mode);
}


void
set_baud(int p_num)
{
  struct termios mode;
  char device[15];

  sprintf(device, "/dev/ttyS%d", p_num);

  int fd = open(device, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
  {
    printf("Can not open serial port %d\n", p_num + 1);
    return;
  }

  tcgetattr(fd, &mode);

  // i.e. from 38400 to B38400
  speed_t baud = get_baud(baud_rate);
  if ((!cfsetispeed(&mode, baud)) && (!cfsetospeed(&mode, baud)))
  {
    if (!tcsetattr(fd, TCSADRAIN, &mode))
    {
      struct termios new_mode;
      memset(&new_mode, 0, sizeof(new_mode));
      tcgetattr(fd, &new_mode);
      if (cfgetispeed(&new_mode) == baud)
      {
        char buf[10];
        sprintf(buf, "Serial %d", p_num + 1);
        write_user_param(PARAM_BAUD_RATE, buf, baud_str);
        printf("serial port %d has been set to %d baud.\n", p_num + 1, baud_rate);
        close(fd);
        return;
      }
    }
  }

  printf("Can not set the baud rate %d to serial port %d\n",
         baud_rate, p_num + 1);
  close(fd);
}


void
serial_init(cmd_obj_t* cmd_obj)
{
}


int
serial_parse(cmd_obj_t* cmd_obj)
{
  int opt;
  char* ret = NULL;

  optind = 0;
  opterr = 0;

  while ((opt = getopt_long_only(cmd_obj->argc, cmd_obj->argv, "1:",
                                 long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case '1':

      errno = 0;

      // the port number must follow the -port option.
      port = strtol(optarg, &ret, 10);
      if ((errno != 0) || (*ret != '\0') || (port == 0))
        return (cmd_obj->parse_result = ERROR_INVALID_PARAM_1);

      if (portSet)
        return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

      portSet = true;

      // setting the baud rate.
      if (optind == (cmd_obj->argc - 1))
      {
        errno = 0;
        baud_str = cmd_obj->argv[optind];
        baud_rate = strtol(baud_str, &ret, 0);
        if ((errno != 0) || (*ret != '\0') || (baud_rate == 0))
        {
          return (cmd_obj->parse_result = ERROR_INVALID_PARAM_2);
        }
        baudSet = true;
      }

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

  if ((!portSet) && (cmd_obj->argc > 1))
  {
    set_error_info(cmd_obj, optind);
    return (cmd_obj->parse_result = ERROR_INVALID_PARAM);
  }

  if (cmd_obj->argc > (optind + 1))
    return (cmd_obj->parse_result = ERROR_TOOMANY_PARAM);

  return (cmd_obj->parse_result = PARSE_SUCCESS);
}


bool
serial_modify(cmd_obj_t* cmd_obj)
{
  return true;
}


static int
filter_ttyS(const struct dirent *file)
{
  if (strncmp(file->d_name, "ttyS", 4) == 0)
    return 1;
  else
    return 0;
}


static void
show_baudrate(void)
{
  unsigned int ret;

  if (portSet)
  {
    ret = read_baud(port - 1);

    if (ret != -1)
      printf("Serial port %d: %d baud\n", port, get_speed(ret));
    else
      printf("Can not open serial port %d\n", port);
  }
  else
  {
    struct dirent **eps;

    int n = scandir("/dev", &eps, filter_ttyS, alphasort);
    if (n > 0)
    {
      int cnt;
      for (cnt = 0; cnt < n; ++cnt)
      {
        char* cp = eps[cnt]->d_name;

        if (strlen(cp) >= 4)
        {
          int p_num = strtol(&cp[4], NULL, 10);
          ret = read_baud(p_num);
          if (ret != -1)
            printf("Serial port %d: %d baud\n", p_num + 1,
                   get_speed(ret));
        }
      }
    }
  }
}


int
serial_execute(cmd_obj_t* cmd_obj)
{
  if (!baudSet)
  {
    show_baudrate();
  }
  else
  {
    set_baud(port - 1);
  }
  return 0;
}


int
serial_clean(cmd_obj_t* cmd_obj)
{
  portSet = false;
  baudSet = false;
  port = -1;
  baud_rate = 0;
  baud_str = NULL;

  return 0;
}


void
serial_error(const cmd_obj_t* const cmd_obj)
{
  prt_parseError(cmd_obj);

  switch (cmd_obj->parse_result)
  {
  case ERROR_INVALID_PARAM_1:
    printf("Invalid serial port number.\n");
    break;

  case ERROR_INVALID_PARAM_2:
    printf("Invalid serial baud rate.\n");
    break;

  default:
    break;
  }
}


void
serial_help()
{
  printf("Usage: serial [-port n] [baud_rate]\n");
}

#endif /* CFG_CMD_SERIAL */

