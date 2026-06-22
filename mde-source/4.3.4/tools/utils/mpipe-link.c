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

/*
 * @file mpipe-link.c
 * Manipulate and examine mPIPE link state from the command line.
 */

#include <sys/errno.h>
#include <sys/time.h>
#include <sys/types.h>

#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gxio/mpipe.h>


/*
 * Command options, first the long versions.
 */
static const struct option long_options[] =
{
  { .name = "list",            .has_arg = 0, .val = 'l' },
  { .name = "up",              .has_arg = 0, .val = 'u' },
  { .name = "down",            .has_arg = 0, .val = 'd' },
  { .name = "10m",             .has_arg = 0, .val = 'T' },
  { .name = "100m",            .has_arg = 0, .val = 'H' },
  { .name = "1g",              .has_arg = 0, .val = 'G' },
  { .name = "10g",             .has_arg = 0, .val = 'X' },
  { .name = "12g",             .has_arg = 0, .val = 'D' },
  { .name = "20g",             .has_arg = 0, .val = '2' },
  { .name = "25g",             .has_arg = 0, .val = '5' },
  { .name = "50g",             .has_arg = 0, .val = 'F' },
  { .name = "full",            .has_arg = 0, .val = 'f' },
  { .name = "half",            .has_arg = 0, .val = 'h' },
  { .name = "loop-mac",        .has_arg = 0, .val = 'm' },
  { .name = "loop-phy",        .has_arg = 0, .val = 'p' },
  { .name = "loop-ext",        .has_arg = 0, .val = 'e' },
  { .name = "watch",           .has_arg = 0, .val = 'w' },
  { 0 },
};

/**
 * Now the short ones.  Note that some long options are intentionally
 * omitted from the short options list, because they're hard to remember
 * and we don't want to have to document them.
 */
static const char options[] = "ludmpefhw";


static void
usage(char* msg)
{
  if (msg)
    fprintf(stderr, "Error: %s\n", msg);

  fprintf(stderr, "Usage: mpipe-link [options] <interface>\n");
  fprintf(stderr, " [-l | --list]     List interfaces\n");
  fprintf(stderr, " [-u | --up]       Bring link up at any supported speed\n");
  fprintf(stderr, " [-d | --down]     Bring link down\n");
  fprintf(stderr, " --10m             Bring link up at 10 Mbps\n");
  fprintf(stderr, " --100m            Bring link up at 100 Mbps\n");
  fprintf(stderr, " --1g              Bring link up at 1 Gbps\n");
  fprintf(stderr, " --10g             Bring link up at 10 Gbps\n");
  fprintf(stderr, " --12g             Bring link up at 12 Gbps\n");
  fprintf(stderr, " --20g             Bring link up at 20 Gbps\n");
  fprintf(stderr, " --25g             Bring link up at 25 Gbps\n");
  fprintf(stderr, " --50g             Bring link up at 50 Gbps\n");
  fprintf(stderr, " [-f | --full]     Enable full-duplex\n");
  fprintf(stderr, " [-h | --half]     Enable half-duplex\n");
  fprintf(stderr, " [-m | --loop-mac] Enable MAC loopback\n");
  fprintf(stderr, " [-p | --loop-phy] Enable PHY loopback\n");
  fprintf(stderr, " [-e | --loop-ext] Enable external loopback\n");
  fprintf(stderr, " [-w | --watch]    Watch link state changes\n");

  fprintf(stderr, "More than one speed option can be specified for some "
          "interfaces.\n");
  fprintf(stderr, "No more than one loopback option can be specified.\n");
  fprintf(stderr, "If no options are specified, link information "
          "is displayed.\n");

  exit(1);
}


void
print_link(FILE* f, uint32_t val, int do_updown)
{
  /** Translate link bits to text names. */
  static const struct
  {
    uint32_t val;
    const char* str;
  }
  val2str[] =
  {
    { GXIO_MPIPE_LINK_10M,      "10 Mbps" },
    { GXIO_MPIPE_LINK_100M,     "100 Mbps" },
    { GXIO_MPIPE_LINK_1G,       "1 Gbps" },
    { GXIO_MPIPE_LINK_10G,      "10 Gbps" },
    { GXIO_MPIPE_LINK_12G,      "12 Gbps" },
    { GXIO_MPIPE_LINK_20G,      "20 Gbps" },
    { GXIO_MPIPE_LINK_25G,      "25 Gbps" },
    { GXIO_MPIPE_LINK_50G,      "50 Gbps" },
    { GXIO_MPIPE_LINK_FDX,      "Full-duplex" },
    { GXIO_MPIPE_LINK_HDX,      "Half-duplex" },
    { GXIO_MPIPE_LINK_LOOP_MAC, "MAC loopback" },
    { GXIO_MPIPE_LINK_LOOP_PHY, "PHY loopback" },
    { GXIO_MPIPE_LINK_LOOP_EXT, "external loopback" },
  };

  int appending = 0;

  if (do_updown)
  {
    if (val & GXIO_MPIPE_LINK_SPEED_MASK)
      fputs("Up", f);
    else
      fputs("Down", f);
    appending = 1;
  }

  for (int i = 0; i < sizeof (val2str) / sizeof (val2str[0]); i++)
  {
    if (val & val2str[i].val)
    {
      if (appending)
        fputs(", ", f);
      fputs(val2str[i].str, f);
      appending = 1;
    }
  }
}


static uint32_t
mpipe_get_or_die(gxio_mpipe_link_t* lnk, uint32_t attr, char* errmsg)
{
  int64_t val = gxio_mpipe_link_get_attr(lnk, attr);
  if (val < 0)
  {
    fprintf(stderr, "mpipe-link: %s: %s\n", errmsg, gxio_strerror(val));
    exit(1);
  }

  return (uint32_t) val;
}


static void
mpipe_set_or_die(gxio_mpipe_link_t* lnk, uint32_t attr, int64_t val,
                 char* errmsg)
{
  int err = gxio_mpipe_link_set_attr(lnk, attr, val);
  if (err < 0)
  {
    fprintf(stderr, "mpipe-link: %s: %s\n", errmsg, gxio_strerror(err));
    exit(1);
  }
}


int
main(int argc, char** argv)
{
  int opt;

  // Are we listing the interfaces?
  int do_list = 0;
  // Are we changing the configuration?
  int do_config = 0;
  // What is the desired new configuration?
  uint32_t new_config = 0;
  // Are we watching an interface?
  int do_watch = 0;

  while ((opt = getopt_long(argc, argv, options, long_options, NULL)) > 0)
  {
    //
    // Process individual options and their arguments.
    //
    switch (opt)
    {
    case 'l':   // --list
      do_list = 1;
      break;

    case 'u':   // --up
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_ANYSPEED;
      break;

    case 'd':   // --down
      do_config = 1;
      new_config = 0;
      break;

    case 'T':   // --10m
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_10M;
      break;

    case 'H':   // --100m
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_100M;
      break;

    case 'G':   // --1g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_1G;
      break;

    case 'X':   // --10g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_10G;
      break;

    case 'D':   // --12g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_12G;
      break;

    case '2':   // --20g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_20G;
      break;

    case '5':   // --25g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_25G;
      break;

    case 'F':   // --50g
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_50G;
      break;

    case 'f':   // --full
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_FDX;
      break;

    case 'h':   // --half
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_HDX;
      break;

    case 'm':   // --loop-mac
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_LOOP_MAC;
      break;

    case 'p':   // --loop-phy
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_LOOP_PHY;
      break;

    case 'e':   // --loop-ext
      do_config = 1;
      new_config |= GXIO_MPIPE_LINK_LOOP_EXT;
      break;

    case 'w':   // --watch
      do_watch = 1;
      break;

    default:
      usage(NULL);
      break;
    }
  }

  if (do_list)
  {
    if (optind != argc)
      usage("can't specify an interface with --list");
    if (do_config)
      usage("can't specify any config flags with --list");
    if (do_watch)
      usage("can't watch interfaces with --list");
  }
  else
  {
    if (optind + 1 != argc)
      usage("need exactly one interface");
  }

  if (__builtin_popcount(new_config & GXIO_MPIPE_LINK_LOOP_MASK) > 1)
  {
    usage("can't specify multiple loopback modes");
  }

  if (do_list)
  {
    int idx = 0;
    char name[GXIO_MPIPE_LINK_NAME_LEN];

    while (gxio_mpipe_link_enumerate(idx++, name) >= 0)
      puts(name);

    return 0;
  }

  char* interface = argv[optind];

  // Initialize the GXIO library.
  gxio_mpipe_context_t context;

  int status = gxio_mpipe_init(&context, gxio_mpipe_link_instance(interface));
  if (status < 0)
  {
    fprintf(stderr, "mpipe-link: can't initialize mPIPE: %s\n",
            gxio_strerror(status));
    exit(1);
  }

  gxio_mpipe_link_t lnk;
  uint32_t open_flags = GXIO_MPIPE_LINK_NO_DATA | GXIO_MPIPE_LINK_AUTO_NONE;

  if (do_config)
    open_flags |= GXIO_MPIPE_LINK_CTL;
  else
    open_flags |= GXIO_MPIPE_LINK_STATS;

  status = gxio_mpipe_link_open(&lnk, &context, interface, open_flags);

  if (status < 0)
  {
    fprintf(stderr, "mpipe-link: can't open link %s: %s\n",
            interface, gxio_strerror(status));
    exit(1);
  }

  //
  // Do the requested operation.
  //
  if (do_config)
  {
    //
    // Make the change.
    //
    mpipe_set_or_die(&lnk, GXIO_MPIPE_LINK_DESIRED_STATE, new_config,
                     "can't set link state");
  }
  else
  {
    //
    // Print the current set of states.
    //
    uint32_t possible_state =
      mpipe_get_or_die(&lnk, GXIO_MPIPE_LINK_POSSIBLE_STATE,
                       "can't get link capabilities");
    fputs("Link capabilities:  ", stdout);
    print_link(stdout, possible_state, 0);
    fputs("\n", stdout);

    uint32_t desired_state =
      mpipe_get_or_die(&lnk, GXIO_MPIPE_LINK_DESIRED_STATE,
                       "can't get desired link state");
    fputs("Desired link state: ", stdout);
    print_link(stdout, desired_state, 1);
    fputs("\n", stdout);

    uint32_t current_state =
      mpipe_get_or_die(&lnk, GXIO_MPIPE_LINK_CURRENT_STATE,
                       "can't get current link state");
    fputs("Current link state: ", stdout);
    print_link(stdout, current_state, 1);
    fputs("\n", stdout);
  }

  if (do_watch)
  {
    fputs("Watching link state changes...\n", stdout);

    int pfd = gxio_mpipe_link_get_pollfd(&context);
    if (pfd < 0)
    {
      fprintf(stderr, "mpipe-link: get_pollfd failure on link %s: %s\n",
              interface, gxio_strerror(pfd));
      exit(1);
    }

    struct pollfd pollfd =
    {
      .fd = pfd,
      .events = POLLIN,
    };

    while (1)
    {
      int arm_stat = gxio_mpipe_link_arm_pollfd(&context, pfd);
      if (arm_stat < 0)
      {
        fprintf(stderr, "mpipe-link: arm_pollfd failure on link %s: %s\n",
                interface, gxio_strerror(arm_stat));
        exit(1);
      }

      if (poll(&pollfd, 1, -1) <= 0)
      {
        perror("mpipe-link: poll failed");
        exit(1);
      }

      uint32_t current_state =
        mpipe_get_or_die(&lnk, GXIO_MPIPE_LINK_CURRENT_STATE,
                         "can't get current link state");
      fprintf(stdout, "%s link state changed, now: ", interface);
      print_link(stdout, current_state, 1);
      fputs("\n", stdout);
    }
  }

  return 0;
}
