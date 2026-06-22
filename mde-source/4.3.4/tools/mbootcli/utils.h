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

#ifndef TOOLS_MBOOTCLI_UTILS_H
#define TOOLS_MBOOTCLI_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <getopt.h>
#include <stdbool.h>

#include "parameters.h"

struct param
{
  unsigned short *type;
  unsigned short *length;
  char* value;
};

struct param_block
{
  char buf[BUFSIZE];
  unsigned short param_len; // the actual number of bytes of buf[] occupied.
  unsigned short param_num; // how many pairs of parameters.

  // ISSUE: Using "bool" here causes unaligned data references!
  int dirty;
};

// Common parse return code.
//
// ISSUE: This whole mechanism should just use allocated error strings.
//
typedef enum
{
  PARSE_SUCCESS = 0,
  ERROR_MISSING_PARAM,
  ERROR_TOOMANY_PARAM,
  ERROR_INVALID_PARAM,
  ERROR_NEED_DEVICE,
  ERROR_TOOMANY_DEVICE,
  UNKNOWN_CMD_SPECIFIED,
  ERROR_INVALID_ACTION,
  ERROR_NEED_GW,
  ERROR_ALREADY_HAS_DEFAULT_ROUTE,
  ERROR_NEED_IMGFILE,
  ERROR_ROUTE_TYPE_NOT_SUPPORT,
  ERROR_MISSING_PARAM_FOR_OPTION,

  // Customized error code.
  ERROR_CUSTOMIZED = 51,
  ERROR_MISSING_PARAM_1,
  ERROR_MISSING_PARAM_2,
  ERROR_MISSING_PARAM_3,
  ERROR_MISSING_PARAM_4,
  ERROR_INVALID_PARAM_1,
  ERROR_INVALID_PARAM_2,
  ERROR_INVALID_PARAM_3,
  ERROR_INVALID_PARAM_4,
  ERROR_INVALID_PARAM_5,
  ERROR_PARAM_MISMATCH,

  // Note: This must be the last general parse error code.
  UNKNOWN_ERROR_CODE

} pResult;

#define CMD_UNKNOWN -1

typedef struct cmd_obj cmd_obj_t;
struct cmd_obj
{
  int cmd_ID;
  char* cmdLine;
  int argc;
  int saved_argc;
  char* argv[MAX_PARAM_PER_CMD];

  cmd_obj_t* next_cmd;

  int parse_result;
  char* user_data;
  char* err_info;
};

typedef struct
{
  pResult parseResult;
  char* errorInfo;
} pErrorInfoType;


typedef struct
{
  char* dest;
  char* netmask;
  char* gateway;
  char* netintf;
  char* option;
} route_struct;



// From "utils.c".

void sig_func(int);
bool count_down(int);
struct termios set_cbreak(void);
struct termios set_noecho(void);
void restore_terminal(struct termios);
void prt_header(void);
void prt_wait(int);
void prt_prompt(const char* const);
void *xmalloc(size_t);
char* xstrdup(const char*);
char* xstrncpy(char* dst, const char* src, size_t n);
int get_all_netintf(char** addr);
void start_dhcp(void);
void start_netif(void);
void start_route(void);
bool is_net_mask(const char* const);
char* xstrcat(char* , size_t, ...);
char* xstrcat_base(char* , size_t, va_list);
void print_error(int ex, const char* fmt, ...);
char* itoa(char* , int);
bool valid_option(const struct option *, const char*);
void hide_and_restore_12(int action);
char my_read_char(const char* mesg);
int map_linux_device(const char* const part, char* device);
void start_network(void);
void stop_network(void);

// From "main.c".

void describe_command(int id);

bool no_param(const cmd_obj_t* const);
int get_param(const cmd_obj_t* const, const char*);
void cmd_replace(cmd_obj_t*, int, const char*);
char* add_param(cmd_obj_t*, ...);
cmd_obj_t *add_cmd(cmd_obj_t*, ...);
cmd_obj_t *prepend_cmd(cmd_obj_t*, ...);
void param_update(cmd_obj_t*);

int map_cmd(const char* const);

void set_user_data(cmd_obj_t*, char*);
void set_err_data(cmd_obj_t*, char*);
void set_error_info(cmd_obj_t*, int);

void prt_nfound(const cmd_obj_t* const);
void prt_parseError(const cmd_obj_t* const);
void prt_cmd(const cmd_obj_t* const);

int sys_execute(cmd_obj_t* const);

void boot_img(void);

bool enable_network(void);

// From "parameter.c"

void* get_default(const char* const, char*, size_t);

int read_boot_param(int type, const char* match_str, char* value, size_t size);
int read_user_param(int type, const char* match_str, char* value, size_t size);
void write_boot_param(int type, char* match_str, char* value);
void write_user_param(int type, char* match_str, char* value);

void write_boot_flash(void);
void write_user_flash(void);

int read_cfg(void);
void clear_boot(void);
void clear_user(void);

int showcfg(void);
int showcfg_old(void);
void clear_param(int type, char* match_str);

void add_route(char* ip_addr, char* mask, char* gw, char* dev);
int delete_route(char* ip_addr, char* mask, char* gw, char* dev);
int read_route(int route_num, char* ip_addr, char* mask, char* gw, char* dev);

void show_bootparam(void);


// From "cmd/*.c".
#define DECLARE_CMD(N) \
  extern void N ## _init(cmd_obj_t*); \
  extern int N ## _parse(cmd_obj_t*); \
  extern bool N ## _modify(cmd_obj_t*); \
  extern int N ## _execute(cmd_obj_t*); \
  extern int N ## _clean(cmd_obj_t*); \
  extern void N ## _error(const cmd_obj_t* const); \
  extern void N ## _help(void)

DECLARE_CMD(boot);
DECLARE_CMD(bootparam);
DECLARE_CMD(clearcfg);
DECLARE_CMD(dhcp);
DECLARE_CMD(exit);
DECLARE_CMD(fdisk);
DECLARE_CMD(ftp);
DECLARE_CMD(help);
DECLARE_CMD(ifconfig);
DECLARE_CMD(ls);
DECLARE_CMD(mem);
DECLARE_CMD(mkfs);
DECLARE_CMD(ping);
DECLARE_CMD(reboot);
DECLARE_CMD(rm);
DECLARE_CMD(route);
DECLARE_CMD(serial);
DECLARE_CMD(showcfg);
DECLARE_CMD(tftp);
DECLARE_CMD(shell);

#endif /* TOOLS_MBOOTCLI_UTILS_H */
