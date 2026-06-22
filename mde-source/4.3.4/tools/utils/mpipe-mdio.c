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

/**
 * @file mpipe-mdio.c
 * Toy to test the mPIPE MDIO interfaces.
 */

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gxio/mpipe.h>

#include <sys/errno.h>
#include <sys/time.h>
#include <sys/types.h>


/**
 * Command options, first the long versions.
 */
static const struct option long_options[] =
{
  { .name = "phy",               .has_arg = 1, .val = 'p' },
  { .name = "dev",               .has_arg = 1, .val = 'd' },
  { .name = "reg",               .has_arg = 1, .val = 'r' },
  { .name = "write",             .has_arg = 1, .val = 'w' },
  { 0 },
};

/**
 * Now the short ones.
 */
static const char options[] = "d:r:p:w:";


static void
usage(char* msg)
{
  if (msg)
    fprintf(stderr, "Error: %s\n", msg);

  fprintf(stderr, "\
Usage: mpipe-mdio [options] <linkname>\n\
       -p, --phy:   specify phy address; use link default if unspecified\n\
       -d, --dev:   specify clause 45 device address (0-31); if specified,\n\
                    use clause 45 format, else use clause 22 format\n\
       -r, --reg:   specify register offset (0-31 in clause 22, 0-65535 in\n\
                    clause 45)\n\
       -w, --write: specify data to write (0-65535); if omitted, read data\n");

  exit(1);
}


int
main(int argc, char** argv)
{
  int p_seen = 0, d_seen = 0, r_seen = 0, w_seen = 0;
  long phy_addr = -1, dev_addr = -1, reg_addr = 0, write_data = 0;

  int opt;

  while ((opt = getopt_long(argc, argv, options, long_options, NULL)) > 0)
  {
    //
    // Process individual options and their arguments.
    //
    switch (opt)
    {
    case 'p':   // --phy
      p_seen = 1;
      phy_addr = strtol(optarg, NULL, 0);
      if (phy_addr < 0 || phy_addr >= (1 << 5))
        usage("phy address out of range");
      break;

    case 'd':   // --dev
      d_seen = 1;
      dev_addr = strtol(optarg, NULL, 0);
      if (dev_addr < 0 || dev_addr >= (1 << 5))
        usage("dev address out of range");
      break;

    case 'r':   // --reg
      r_seen = 1;
      reg_addr = strtol(optarg, NULL, 0);
      break;

    case 'w':   // --write
      w_seen = 1;
      write_data = strtol(optarg, NULL, 0);
      if (write_data < 0 || write_data >= (1 << 16))
        usage("write data out of range");
      break;

    default:
      usage("unknown option");
      break;
    }
  }

  //
  // Always have to have a register address.
  //
  if (!r_seen)
    usage("must specify PHY and register address");

  //
  // Make sure register is within range; we can't do this above, where we
  // range-check the other options, because up there we don't yet know if
  // we're using clause 22 or 45.
  //
  if (reg_addr < 0 || reg_addr >= ((d_seen) ? (1 << 16) : (1 << 5)))
    usage("dev address out of range");

  if (optind >= argc)
    usage("must specify linkname");

  //
  // Initialize the GXIO library.  We're just going to do
  // mpipe_link_attr_set/_get, so tell it we aren't going to
  // send/receive packets.
  //
  char* interface = argv[optind];

  gxio_mpipe_context_t context;

  int status = gxio_mpipe_init(&context, gxio_mpipe_link_instance(interface));
  if (status < 0)
  {
    fprintf(stderr, "mpipe-mdio: can't initialize mPIPE: %s\n",
            gxio_strerror(status));
    exit(1);
  }

  gxio_mpipe_link_t lnk;
  uint32_t open_flags = GXIO_MPIPE_LINK_NO_DATA | GXIO_MPIPE_LINK_AUTO_NONE;

  if (w_seen)
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
  // Do the read or write.
  //
  if (w_seen)
  {
    int stat = gxio_mpipe_link_mdio_wr_ex(&lnk, phy_addr, dev_addr,
					  reg_addr, write_data);
    if (stat < 0)
    {
      fprintf(stderr, "mpipe-mdio: write failed (%s)\n", gxio_strerror(stat));
      exit(1);
    }
  }
  else
  {
    int read_data = gxio_mpipe_link_mdio_rd_ex(&lnk, phy_addr, dev_addr,
					       reg_addr);
    if (read_data < 0)
    {
      fprintf(stderr, "mpipe-mdio: read failed (%s)\n",
              gxio_strerror(read_data));
      exit(1);
    }

    printf("%#x\n", read_data);
  }

  return 0;
}
