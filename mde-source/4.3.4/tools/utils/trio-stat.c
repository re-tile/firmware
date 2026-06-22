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

#include <sys/time.h>
#include <sys/types.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <gxpci/gxpci.h>
#include <arch/trio_pcie_intfc.h>

/* Interval in second. */
static int interval;

/* Indicate display count per second. */
static bool rate;

/* Print numbers in human readable format. */
static bool human = false;

/* Bytes recevied with each receive DMA request. */
static int rx_size = 4096;

// Maximum number of TRIO instances per Gx.
#define MAX_TRIO_INSTANCES  2
// Limit on the number of MACs per Trio Instance
#define MAX_TRIO_MACS       3
#define MAX_TRIO_LINK       (MAX_TRIO_INSTANCES * MAX_TRIO_MACS)

/* link count. */
static int   link_count;

typedef struct trio_link_s {
  uint8_t index;
  uint8_t mac;
  const char *name;
} trio_link_t;

/* link names. */
static trio_link_t trio_links[MAX_TRIO_LINK];

// This Trio (PCIe) context, which is used to reference the physical
// PCIe link being used.
gxio_trio_context_t trio_context_body[MAX_TRIO_INSTANCES];
gxio_trio_context_t* trio_context[MAX_TRIO_INSTANCES];

uint8_t trio_context_count = 0;


/* Timers. */
static struct timeval start_time, stop_time;

// register definitions in trio_pcie_intfc.h
//
// Packets transmitted
TRIO_PCIE_INTFC_TX_REQ_HDR_STATS_t tx_req_hdr_stats[MAX_TRIO_LINK];
// Bytes transmitted
TRIO_PCIE_INTFC_TX_PDAT_STATS_t tx_req_byte_stats[MAX_TRIO_LINK];
// Host Reads
TRIO_PCIE_INTFC_TX_CPL_STATS_t tx_host_read_stats[MAX_TRIO_LINK];

static void do_help(void)
{
  printf("\n"
         "usage: trio-stat  [--help]\n"
         "                  [-i | --interval <interval>]\n"
         "                  [-r | --rates]\n"
         "                  [-h | --human-readable]\n"
         "                  [-s | --size <bytes>]\n"
         "                   <link#1> <link#2>\n");
}

static void do_help_more(void)
{
  do_help();

  printf(
    " -i | --interval <#>  Repeat run every # seconds.\n"
    " -v | --verbose       Verbose display.\n"
    " -r | --rates         Calculate bits/second data rate.\n"
    " -s | --size <#>      Size of transfers from host to tile (default 4KB).\n"
    " -h | --human-readable Print numbers in human readable format.\n");
}

/******************************************************************************
 * void do_parse_args(int argc, char* argv[])
 * Description:Parse the command arguments.
 * Modified: list, rate, interval, total, verbose, preamble, num_counter,
 *           link_count.
 * Input: argc - argument count.
 *        argv[] - argument array.
 * Return: void
 *
 **/

static void do_parse_args(int argc, char* argv[])
{
  int  i = 0;

  if (argc == 1)
  {
    do_help();
    exit(0);
  }

  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      do_help_more();
      exit(0);
    }
    else if ((!strcmp(argv[i], "-r")) || (!strcmp(argv[i], "--rate")) ||
             (!strcmp(argv[i], "--rates")))
    {
      rate = true;
    }
    else if ((!strcmp(argv[i], "-i")) || (!strcmp(argv[i], "--interval")))
    {
      if(argc > ++i)
      {
        interval = atoi(argv[i]);
        if (interval < 0)
        {
          fprintf(stderr, "invalid arg %d, %s.\n", i, argv[i]);
          exit(1);
        }
        else if(interval > 60)
        {
          printf("warning: interval setting is more than 1 min!");
        }
      }
    }
    else if ((!strcmp(argv[i], "-s")) || (!strcmp(argv[i], "--size")))
    {
      if(argc > ++i)
      {
        rx_size = atoi(argv[i]);
        if (rx_size < 0)
        {
          fprintf(stderr, "invalid arg %d, %s.\n", i, argv[i]);
          exit(1);
        }
        else if(rx_size > (16 * 1024))
        {
          printf("warning: receive size setting is more than 16KB!");
        }
      }
    }
    else if ((!strcmp(argv[i], "-h")) ||
             (!strcmp(argv[i], "--human-readable")))
    {
      human = true;
    }
    else if ((strstr(argv[i], "trio")) && (strstr(argv[i], "mac")))
    {
      if (link_count < MAX_TRIO_LINK)
      {
        unsigned int index, mac;
        if (sscanf(argv[i], "trio%u_mac%u", &index, &mac) != 2)
        {
          fprintf(stderr, "Unknown trio link: '%s'\n", argv[i]);
          exit(1);
        }
        else if (index >= trio_context_count)
        {
          fprintf(stderr, "trio link: '%s'. Only %d trio contexts found\n",
                  argv[i], trio_context_count);
          exit(1);
        }
        else if (mac >= MAX_TRIO_MACS)
        {
          fprintf(stderr, "trio link: '%s'. mac can't be larger than %d\n",
                  argv[i], MAX_TRIO_MACS - 1);
          exit(1);
        }
        else
        {
          trio_links[link_count].index = index;
          trio_links[link_count].mac = mac;
          trio_links[link_count].name = argv[i];
          link_count++;
        }
      }
    }
    else if ((strlen(argv[i]) > 2) && (argv[i][0] == '-') && argv[i][1] != '-')
    {
      fprintf(stderr, "Invalid arg: multi-character flag not supported. (%s)\n",
              argv[i]);
      exit(1);
    }
    else
    {
      fprintf(stderr, "Invalid arg: %d, %s.\n", i, argv[i]);
      exit(1);
    }
  }

  if (rate && !interval)
  {
    fprintf(stderr, "Invalid args: --rate requires --interval.\n");
    exit(1);
  }

  if (!link_count)
  {
    // Assume link0_mac0
    trio_links[link_count].index = 0;
    trio_links[link_count].mac = 0;
    trio_links[link_count].name = "trio0_mac0";
    link_count++;
  }
}

static const char *N_to_STR(char *str, int len, uint64_t value)
{
#define  KILO  (  1000ULL  )
#define  MEGA  (KILO * KILO)
#define  GIGA  (MEGA * KILO)
#define  TERA  (GIGA * KILO)
#define  PETA  (TERA * KILO)

  if (!human)
  {
    snprintf(str, len, "%lld", (unsigned long long)value);
    return str;
  }

  if (value < KILO)
  {
    snprintf(str, len, "%d", (int)value);
  }
  else if (value < MEGA)
  {
    snprintf(str, len, "%d.%02dK",
             (int)(value / KILO), (int)((value % KILO) / (KILO / 100)));
  }
  else if (value < GIGA)
  {
    snprintf(str, len, "%d.%02dM",
             (int)(value / MEGA), (int)((value % MEGA) / (MEGA / 100)));
  }
  else if (value < TERA)
  {
    snprintf(str, len, "%d.%02dG",
             (int)(value / GIGA), (int)((value % GIGA) / (GIGA / 100)));
  }
  else if (value < PETA)
  {
    snprintf(str, len, "%d.%02dT",
             (int)(value / TERA), (int)((value % TERA) / (TERA / 100)));
  }
  else
  {
    snprintf(str, len, "%d.%02dP",
             (int)(value / PETA), (int)((value % PETA) / (PETA / 100)));
  }

  return str;
}


void do_trio_stats(void)
{
  for (int link = 0; link < link_count; link++)
  {
    int mac = trio_links[link].mac;
    int link_index = trio_links[link].index;
    gxio_trio_context_t* context = trio_context[link_index];
    uint64_t mac_reg_offset;
    mac_reg_offset = (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
                      TRIO_CFG_REGION_ADDR__INTFC_SHIFT );
    mac_reg_offset |= (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

    const uint32_t cfg_shift = TRIO_CFG_REGION_ADDR__REG_SHIFT;

    // Packets transmitted
    tx_req_hdr_stats[link].word = 
      __gxio_mmio_read(context->mmio_base_mac +
                       ((TRIO_PCIE_INTFC_TX_REQ_HDR_STATS << cfg_shift) |
                        mac_reg_offset));

    // Bytes transmitted
    tx_req_byte_stats[link].word =
      __gxio_mmio_read(context->mmio_base_mac +
                       ((TRIO_PCIE_INTFC_TX_PDAT_STATS << cfg_shift) |
                        mac_reg_offset));

    // Reads from the Host
    tx_host_read_stats[link].word =
      __gxio_mmio_read(context->mmio_base_mac +
                       ((TRIO_PCIE_INTFC_TX_CPL_STATS << cfg_shift) |
                        mac_reg_offset));
  }
}

void trio_stats_display(void)
{
  static bool header = false;

  if ((header == false))
  {
    /* Print header if not did yet or in verbose mode. */
    if (!rate)
      printf("%-10s %8s %16s %8s %16s %8s %16s\n",
             "Link", "Tx pkt", "Tx bits", "Rx pkt", "Rx bits",
             "Host reads", "Host bits");
    else
      printf("%-10s %8s %16s %8s %16s %8s %16s\n",
             "LINK", "Tx pkt/s", "Tx bits/s", "Rx pkt/s", "Rx bits/s",
             "Host reads/s", "Host bits/s");

    printf("------------------------------------------------"
           "--------------------------------------------\n");

    header = true;
  }

  for (int link = 0; link < link_count; link++)
  {
    printf("%-10s ", trio_links[link].name);

    uint64_t rx_packets = tx_req_hdr_stats[link].np_hdr_cnt;
    uint64_t tx_packets = tx_req_hdr_stats[link].p_hdr_cnt;
    
    uint64_t tx_bytes = tx_req_byte_stats[link].p_byte_cnt;
    
    // For cases where the Receive DMA size is fixed and known.
    uint64_t rx_bytes = rx_size * rx_packets;
    
    // Host reads
    uint64_t host_reads = tx_host_read_stats[link].cpl_hdr_cnt;
    
    // The cpl_byte_cnt does not clear on read.
    static uint64_t previous_cpl_byte_cnt[MAX_TRIO_LINK];
    uint64_t cpl_byte_cnt = tx_host_read_stats[link].cpl_byte_cnt;
    uint64_t host_read_bytes = 0;
    if (previous_cpl_byte_cnt[link])
      host_read_bytes = cpl_byte_cnt - previous_cpl_byte_cnt[link];
      
    previous_cpl_byte_cnt[link] = cpl_byte_cnt;
    
    char str[32];
    
    if (!rate)
    {
      N_to_STR(str, sizeof(str), tx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), tx_bytes * 8);
      printf("%16s ", str);
      
      N_to_STR(str, sizeof(str), rx_packets);
      printf("%8s", str);
      N_to_STR(str, sizeof(str), rx_bytes * 8);
      printf("%16s ", str);
      
      N_to_STR(str, sizeof(str), host_reads);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), host_read_bytes * 8);
      printf("%16s\n", str);
    }
    else
    {
      
      uint32_t delta_ms = (stop_time.tv_sec - start_time.tv_sec) * 1000 +
        (stop_time.tv_usec - start_time.tv_usec) / 1000;
      
      if (delta_ms == 0)
        delta_ms = 1; // In case divided by ZERO.
      
      tx_packets = (tx_packets * 1000) / delta_ms;
      rx_packets = (rx_packets * 1000) / delta_ms;
      tx_bytes = (tx_bytes * 1000) / delta_ms;
      rx_bytes = (rx_bytes * 1000) / delta_ms;
      
      host_reads = (host_reads * 1000) / delta_ms;
      host_read_bytes = (host_read_bytes * 1000) / delta_ms;
      
      N_to_STR(str, sizeof(str), tx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), tx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), rx_packets);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), rx_bytes * 8);
      printf("%16s ", str);
      N_to_STR(str, sizeof(str), host_reads);
      printf("%8s ", str);
      N_to_STR(str, sizeof(str), host_read_bytes * 8);
      printf("%16s\n", str);
    }
  }
}

int main(int argc, char *argv[])
{
  interval = 1;

  for (int trio_index = 0; trio_index < MAX_TRIO_INSTANCES; trio_index++)
  {
    // Get a gxio context, which is shared by all the gxpci contexts.
    trio_context[trio_index] = &trio_context_body[trio_index];
    int result = gxio_trio_init(trio_context[trio_index], trio_index);
    if (result < 0) {
      trio_context[trio_index] = NULL;
      break;
    }
    trio_context_count++;
  }

  /* parse the input arguments. */
  do_parse_args(argc, argv);

  do_trio_stats();

  /* Mark the start time. */
  gettimeofday(&start_time, NULL);

  while (interval > 0)
  {
    /* Subsequent display in a loop every <internal> seconds, not need
     * preciously. */
    sleep(interval);

    do_trio_stats();

    /* Mark the stop time. */
    gettimeofday(&stop_time, NULL);

    trio_stats_display();

    /* Save stop timer as start timer, in case in a loop. */
    start_time = stop_time;

    fflush(stdout);
  }

  return 0;
}
